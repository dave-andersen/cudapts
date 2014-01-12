#if defined(__MINGW64__)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_io.hpp>
#include <cstring>
#include <unordered_map>
#include "gpuhash.h"
//#include <libcuckoo/cuckoohash_map.hh>
//#include <libcuckoo/city_hasher.hh>

extern "C" {
#include "sph_sha2.h"
}
#include "cpuid.h"
#include "sha512.h"

enum SHAMODE { SPHLIB = 0, AVXSSE4 };

typedef struct {
  // comments: BYTES <index> + <length>
  int32_t nVersion;            // 0+4
  uint8_t hashPrevBlock[32];       // 4+32
  uint8_t hashMerkleRoot[32];      // 36+32
  uint32_t  nTime;               // 68+4
  uint32_t  nBits;               // 72+4
  uint32_t  nNonce;              // 76+4
  uint32_t  birthdayA;          // 80+32+4 (uint32_t)
  uint32_t  birthdayB;          // 84+32+4 (uint32_t)
  uint8_t   targetShare[32];
} blockHeader_t;              // = 80+32+8 bytes header (80 default + 8 birthdayA&B + 32 target)

class CBlockProvider {
public:
  CBlockProvider() { }
  ~CBlockProvider() { }
  virtual blockHeader_t* getBlock(unsigned int thread_id, unsigned int last_time, unsigned int counter) = 0;
  virtual blockHeader_t* getOriginalBlock() = 0;
  virtual void setBlockTo(blockHeader_t* newblock) = 0;
  virtual void submitBlock(blockHeader_t* block, unsigned int thread_id) = 0;
  virtual unsigned int GetAdjustedTimeWithOffset(unsigned int thread_id) = 0;
};

volatile uint64_t totalCollisionCount = 0;
volatile uint64_t totalShareCount = 0;

#define MAX_MOMENTUM_NONCE (1<<26) // 67.108.864
#define SEARCH_SPACE_BITS  50
#define BIRTHDAYS_PER_HASH 8

void print256(const char* bfstr, uint32_t* v) {
  std::stringstream ss;
  for(ptrdiff_t i=7; i>=0; --i)
    ss << std::setw(8) << std::setfill('0') << std::hex << v[i];
  ss.flush();
  std::cout << bfstr << ": " << ss.str().c_str() << std::endl;
}

template<SHAMODE shamode>
bool protoshares_revalidateCollision(blockHeader_t* block, uint8_t* midHash, uint32_t indexA, uint32_t indexB, uint64_t birthday, CBlockProvider* bp, unsigned int thread_id)
{
  uint8_t tempHash[32+4];
  memcpy(tempHash+4, midHash, 32);
  totalCollisionCount += 2; // we can use every collision twice -> A B and B A (srsly?)
  //printf("Collision found %8d = %8d | num: %d\n", indexA, indexB, totalCollisionCount);
        
  sph_sha256_context c256; //SPH
		
  // get full block hash (for A B)
  block->birthdayA = indexA;
  block->birthdayB = indexB;
  uint8_t proofOfWorkHash[32];        
  //SPH
  sph_sha256_init(&c256);
  sph_sha256(&c256, (unsigned char*)block, 80+8);
  sph_sha256_close(&c256, proofOfWorkHash);
  sph_sha256_init(&c256);
  sph_sha256(&c256, (unsigned char*)proofOfWorkHash, 32);
  sph_sha256_close(&c256, proofOfWorkHash);
  bool hashMeetsTarget = true;
  uint32_t* generatedHash32 = (uint32_t*)proofOfWorkHash;
  uint32_t* targetHash32 = (uint32_t*)block->targetShare;
  for(uint64_t hc=7; hc!=0; hc--)
    {
      if( generatedHash32[hc] < targetHash32[hc] )
	{
	  hashMeetsTarget = true;
	  break;
	}
      else if( generatedHash32[hc] > targetHash32[hc] )
	{
	  hashMeetsTarget = false;
	  break;
	}
    }
  if( hashMeetsTarget )
    bp->submitBlock(block, thread_id);
		
  // get full block hash (for B A)
  block->birthdayA = indexB;
  block->birthdayB = indexA;
  //SPH
  sph_sha256_init(&c256);
  sph_sha256(&c256, (unsigned char*)block, 80+8);
  sph_sha256_close(&c256, proofOfWorkHash);
  sph_sha256_init(&c256);
  sph_sha256(&c256, (unsigned char*)proofOfWorkHash, 32);
  sph_sha256_close(&c256, proofOfWorkHash);
  hashMeetsTarget = true;
  generatedHash32 = (uint32_t*)proofOfWorkHash;
  targetHash32 = (uint32_t*)block->targetShare;
  for(uint64_t hc=7; hc!=0; hc--)
    {
      if( generatedHash32[hc] < targetHash32[hc] )
	{
	  hashMeetsTarget = true;
	  break;
	}
      else if( generatedHash32[hc] > targetHash32[hc] )
	{
	  hashMeetsTarget = false;
	  break;
	}
    }
  if( hashMeetsTarget )
    bp->submitBlock(block, thread_id);

  return true;
}

template<int COLLISION_TABLE_SIZE, int COLLISION_KEY_MASK, int COLLISION_TABLE_BITS, SHAMODE shamode>
void protoshares_process_512(blockHeader_t* block, CBlockProvider* bp, unsigned int thread_id, GPUHasher *_gpu, uint64_t *hashblock)
{
  // generate mid hash using sha256 (header hash)
  uint8_t midHash[32+4];
  {
    //SPH
    sph_sha256_context c256;
    sph_sha256_init(&c256);
    sph_sha256(&c256, (unsigned char*)block, 80);
    sph_sha256_close(&c256, midHash+4);
    sph_sha256_init(&c256);
    sph_sha256(&c256, (unsigned char*)(midHash+4), 32);
    sph_sha256_close(&c256, midHash+4);
  }

  SHA512_Context c512_avxsse;
  
  SHA512_Init(&c512_avxsse);
  SHA512_Update_Simple(&c512_avxsse, midHash, 32+4);
  SHA512_PreFinal(&c512_avxsse);

  *(uint32_t *)(&c512_avxsse.buffer.bytes[0]) = 0;
  _gpu->ComputeHashes((uint64_t *)c512_avxsse.buffer.bytes, hashblock);
  uint32_t n_hashes_plus_one = *((uint32_t *)hashblock);
  std::unordered_map<uint64_t, uint32_t> resmap;

  for (uint32_t i = 0; i < (n_hashes_plus_one-1); i++) {
    uint64_t birthday = hashblock[1+i*2];
    uint32_t mine = hashblock[1+i*2+1];
    std::unordered_map<uint64_t,uint32_t>::const_iterator r = resmap.find(birthday);
    if (r != resmap.end()) {
      uint32_t other = r->second;
      protoshares_revalidateCollision<shamode>(block, midHash+4, other, mine, birthday, bp, thread_id);
    }
    resmap[birthday] = mine;
  }
}
