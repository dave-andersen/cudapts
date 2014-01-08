//===
// by xolokram/TB
// 2013
//===

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <map>
#include <inttypes.h>
#include <sys/mman.h>

#include "main_poolminer.hpp"

#if defined(__GNUG__) && !defined(__MINGW32__) && !defined(__MINGW64__)
#include <sys/syscall.h>
#include <sys/time.h> //depr?
#include <sys/resource.h>
#elif defined(__MINGW32__) || defined(__MINGW64__)
#include <windows.h>
#endif

#define VERSION_MAJOR 0
#define VERSION_MINOR 7
#define VERSION_EXT "RC2 <experimental>"

#define MAX_THREADS 64

/*********************************
* global variables, structs and extern functions
*********************************/

int COLLISION_TABLE_BITS;
bool use_avxsse4;
size_t thread_num_max;
static size_t fee_to_pay;
static size_t miner_id;
static boost::asio::ip::tcp::socket* socket_to_server;
static boost::posix_time::ptime t_start;
static std::map<int,unsigned long> statistics;
static bool running;
std::string pool_username;
std::string pool_password;

/* bleah this shouldn't be global so we can run one instance
 * on all GPUs. */
int gpu_device_id = 0;

/*********************************
* class CBlockProviderGW to (incl. SUBMIT_BLOCK)
*********************************/

class CBlockProviderGW : public CBlockProvider {
public:

	CBlockProviderGW() : CBlockProvider(), nTime_offset(0), _block(NULL) {}

	virtual ~CBlockProviderGW() { /* TODO */ }

	virtual unsigned int GetAdjustedTimeWithOffset(unsigned int thread_id) {
		return nTime_offset + ((((unsigned int)time(NULL) + thread_num_max) / thread_num_max) * thread_num_max) + thread_id;
	}

	virtual blockHeader_t* getBlock(unsigned int thread_id, unsigned int last_time, unsigned int counter) {
		blockHeader_t* block = NULL;
		{
			boost::shared_lock<boost::shared_mutex> lock(_mutex_getwork);
			if (_block == NULL) return NULL;
			block = new blockHeader_t;
			memcpy(block, _block, 80+32+8);
		}		
		unsigned int new_time = GetAdjustedTimeWithOffset(thread_id);
		new_time += counter * thread_num_max;
		block->nTime = new_time;
		//std::cout << "[WORKER" << thread_id << "] block @ " << new_time << std::endl;
		return block;
	}
	
	virtual blockHeader_t* getOriginalBlock() {
		//boost::shared_lock<boost::shared_mutex> lock(_mutex_getwork);
		return _block;
	}
	
	virtual void setBlockTo(blockHeader_t* newblock) {
		blockHeader_t* old_block = NULL;
		{
			boost::unique_lock<boost::shared_mutex> lock(_mutex_getwork);
			old_block = _block;
			_block = newblock;
		}
		if (old_block != NULL) delete old_block;
	}

	void setBlocksFromData(unsigned char* data) {
		blockHeader_t* block = new blockHeader_t;
		memcpy(block, data, 80); //0-79
		block->birthdayA = 0;    //80-83
		block->birthdayB = 0;    //84-87
		memcpy(((unsigned char*)block)+88,data+80, 32);
		//
		unsigned int nTime_local = time(NULL);
		unsigned int nTime_server = block->nTime;
		nTime_offset = nTime_local > nTime_server ? 0 : (nTime_server-nTime_local);
		//
		setBlockTo(block);
	}

	void submitBlock(blockHeader_t *block, unsigned int thread_id) {
		if (socket_to_server != NULL) {
			blockHeader_t submitblock; //!
			memcpy((unsigned char*)&submitblock, (unsigned char*)block, 88);
			std::cout << "[WORKER] collision found: " << submitblock.birthdayA << " <-> " << submitblock.birthdayB << " #" << totalCollisionCount << " @ " << submitblock.nTime << " by " << thread_id << std::endl;
			boost::system::error_code submit_error = boost::asio::error::host_not_found;
			if (socket_to_server != NULL) boost::asio::write(*socket_to_server, boost::asio::buffer((unsigned char*)&submitblock, 88), boost::asio::transfer_all(), submit_error); //FaF
			//if (submit_error)
			//	std::cout << submit_error << " @ submit" << std::endl;
			if (!submit_error)
				++totalShareCount;
		}
	}

protected:
	unsigned int nTime_offset;
	boost::shared_mutex _mutex_getwork;
	blockHeader_t* _block;
};

/*********************************
* multi-threading
*********************************/

class CMasterThreadStub {
public:
  virtual void wait_for_master() = 0;
  virtual boost::shared_mutex& get_working_lock() = 0;
};

class CWorkerThread { // worker=miner
public:

	CWorkerThread(CMasterThreadStub *master, unsigned int id, CBlockProviderGW *bprovider)
		: _collisionMap(NULL), _working_lock(NULL), _id(id), _master(master), _bprovider(bprovider), _thread(&CWorkerThread::run, this) {

	}
		
  template<int COLLISION_TABLE_SIZE, int COLLISION_KEY_MASK, int CTABLE_BITS, SHAMODE shamode>
	void mineloop() {
		unsigned int blockcnt = 0;
		blockHeader_t* thrblock = NULL;
		blockHeader_t* orgblock = NULL;
		while (running) {
			if (orgblock != _bprovider->getOriginalBlock()) {
				orgblock = _bprovider->getOriginalBlock();
				blockcnt = 0;
			}
			thrblock = _bprovider->getBlock(_id, thrblock == NULL ? 0 : thrblock->nTime, blockcnt);
			if (orgblock == _bprovider->getOriginalBlock()) {
				++blockcnt;
			}
			if (thrblock != NULL) {
			  protoshares_process_512<COLLISION_TABLE_SIZE,COLLISION_KEY_MASK,CTABLE_BITS,shamode>(thrblock, _collisionMap, _bprovider, _id, _gpu, _hashblock);
			} else
				boost::this_thread::sleep(boost::posix_time::seconds(1));
		}
	}
	
	template<SHAMODE shamode>
	void mineloop_start() {
		switch (COLLISION_TABLE_BITS) {
		  case 20: mineloop<(1<<20),(0xFFFFFFFF<<(32-(32-20))),20,shamode>(); break;
		  case 21: mineloop<(1<<21),(0xFFFFFFFF<<(32-(32-21))),21,shamode>(); break;
		  case 22: mineloop<(1<<22),(0xFFFFFFFF<<(32-(32-22))),22,shamode>(); break;
		  case 23: mineloop<(1<<23),(0xFFFFFFFF<<(32-(32-23))),23,shamode>(); break;
			case 24: mineloop<(1<<24),(0xFFFFFFFF<<(32-(32-24))),24,shamode>(); break;
			case 25: mineloop<(1<<25),(0xFFFFFFFF<<(32-(32-25))),25,shamode>(); break;
			case 26: mineloop<(1<<26),(0xFFFFFFFF<<(32-(32-26))),26,shamode>(); break;
			case 27: mineloop<(1<<27),(0xFFFFFFFF<<(32-(32-27))),27,shamode>(); break;
			case 28: mineloop<(1<<28),(0xFFFFFFFF<<(32-(32-28))),28,shamode>(); break;
			case 29: mineloop<(1<<29),(0xFFFFFFFF<<(32-(32-29))),29,shamode>(); break;
			case 30: mineloop<(1<<30),(0xFFFFFFFF<<(32-(32-30))),30,shamode>(); break;
			default: break;
		}
	}

	void run() {
		std::cout << "[WORKER" << _id << "] Hello, World!" << std::endl;

		/* Ensure that thread is pinned to its allocation */
		_hashblock = (uint64_t *)malloc(sizeof(uint64_t) * (1<<26));
			_gpu = new GPUHasher(gpu_device_id);
			_gpu->Initialize();
 
#ifndef MAP_HUGETLB
			_collisionMap = (uint32_t*)malloc(sizeof(uint32_t)*(1 << COLLISION_TABLE_BITS));
#else

			size_t bigbufsize = sizeof(uint32_t)*(1 << COLLISION_TABLE_BITS);
			void *addr;
			addr = mmap(0, bigbufsize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB, 0, 0);
			if (addr == MAP_FAILED) {
			  perror("Could not mmap hugepage, reverting to malloc");
			  _collisionMap = (uint32_t*)malloc(sizeof(uint32_t)*(1 << COLLISION_TABLE_BITS));
			} else {
			  _collisionMap = (uint32_t *)addr;
			}
#endif


		{
			//set low priority
#if defined(__GNUG__) && !defined(__MINGW32__) && !defined(__MINGW64__)
			pid_t tid = (pid_t) syscall (SYS_gettid);
			setpriority(PRIO_PROCESS, tid, 1);
#elif defined(__MINGW32__) || defined(__MINGW64__)
			HANDLE th = _thread.native_handle();
			if (!SetThreadPriority(th, THREAD_PRIORITY_LOWEST))
				std::cerr << "failed to set thread priority to low" << std::endl;
#endif
		}
		_master->wait_for_master();
		std::cout << "[WORKER" << _id << "] GoGoGo!" << std::endl;
		boost::this_thread::sleep(boost::posix_time::seconds(1));
		if (use_avxsse4)
			mineloop_start<AVXSSE4>(); // <-- work loop
		else
			mineloop_start<SPHLIB>(); // ^
		std::cout << "[WORKER" << _id << "] Bye Bye!" << std::endl;
	}

	void work() { // called from within master thread
		_working_lock = new boost::shared_lock<boost::shared_mutex>(_master->get_working_lock());
	}

protected:
  uint32_t* _collisionMap;
  boost::shared_lock<boost::shared_mutex> *_working_lock;
  unsigned int _id;
  CMasterThreadStub *_master;
  CBlockProviderGW  *_bprovider;
  GPUHasher *_gpu;
  uint64_t *_hashblock;
  boost::thread _thread;
};

class CMasterThread : public CMasterThreadStub {
public:

  CMasterThread(CBlockProviderGW *bprovider) : CMasterThreadStub(), _bprovider(bprovider) {}

  void run() {

	{
		boost::unique_lock<boost::shared_mutex> lock(_mutex_master);
		std::cout << "spawning " << thread_num_max << " worker thread(s)" << std::endl;

		for (unsigned int i = 0; i < thread_num_max; ++i) {
			CWorkerThread *worker = new CWorkerThread(this, i, _bprovider);
			worker->work();
		}
	}

    boost::asio::io_service io_service;
    boost::asio::ip::tcp::resolver resolver(io_service); //resolve dns
	boost::asio::ip::tcp::resolver::query query("ptsmine.beeeeer.org", "1337");
	//boost::asio::ip::tcp::resolver::query query("127.0.0.1", "1337");
    boost::asio::ip::tcp::resolver::iterator endpoint;
	boost::asio::ip::tcp::resolver::iterator end;
	boost::asio::ip::tcp::no_delay nd_option(true);
	boost::asio::socket_base::keep_alive ka_option(true);

	while (running) {
		endpoint = resolver.resolve(query);
		boost::scoped_ptr<boost::asio::ip::tcp::socket> socket;
		boost::system::error_code error_socket = boost::asio::error::host_not_found;
		while (error_socket && endpoint != end)
		{
			//socket->close();
			socket.reset(new boost::asio::ip::tcp::socket(io_service));
			boost::asio::ip::tcp::endpoint tcp_ep = *endpoint++;
			socket->connect(tcp_ep, error_socket);
			std::cout << "connecting to " << tcp_ep << std::endl;
		}
		socket->set_option(nd_option);
		socket->set_option(ka_option);

		if (error_socket) {
			std::cout << error_socket << std::endl;
			boost::this_thread::sleep(boost::posix_time::seconds(10));
			continue;
		} else {
			t_start = boost::posix_time::second_clock::local_time();
			totalCollisionCount = 0;
			totalShareCount = 0;
		}

		{ //send hello message
			char* hello = new char[pool_username.length()+/*v0.2/0.3=*/2+/*v0.4=*/20+/*v0.7=*/1+pool_password.length()];
			memcpy(hello+1, pool_username.c_str(), pool_username.length());
			*((unsigned char*)hello) = pool_username.length();
			*((unsigned char*)(hello+pool_username.length()+1)) = 0; //hi, i'm v0.4+
			*((unsigned char*)(hello+pool_username.length()+2)) = VERSION_MAJOR;
			*((unsigned char*)(hello+pool_username.length()+3)) = VERSION_MINOR;
			*((unsigned char*)(hello+pool_username.length()+4)) = thread_num_max;
			*((unsigned char*)(hello+pool_username.length()+5)) = fee_to_pay;
			*((unsigned short*)(hello+pool_username.length()+6)) = miner_id;
			*((unsigned int*)(hello+pool_username.length()+8)) = 0;
			*((unsigned int*)(hello+pool_username.length()+12)) = 0;
			*((unsigned int*)(hello+pool_username.length()+16)) = 0;
			*((unsigned char*)(hello+pool_username.length()+20)) = pool_password.length();
			memcpy(hello+pool_username.length()+21, pool_password.c_str(), pool_password.length());
			*((unsigned short*)(hello+pool_username.length()+21+pool_password.length())) = 0; //EXTENSIONS
			boost::system::error_code error;
			socket->write_some(boost::asio::buffer(hello, pool_username.length()+2+20+1+pool_password.length()), error);
			//if (error)
			//	std::cout << error << " @ write_some_hello" << std::endl;
			delete[] hello;
		}

		socket_to_server = socket.get(); //TODO: lock/mutex

		int reject_counter = 0;
		bool done = false;
		while (!done) {
			int type = -1;
			{ //get the data header
				unsigned char buf = 0; //get header
				boost::system::error_code error;
				size_t len = boost::asio::read(*socket_to_server, boost::asio::buffer(&buf, 1), boost::asio::transfer_all(), error);
				if (error == boost::asio::error::eof)
					break; // Connection closed cleanly by peer.
				else if (error) {
					//std::cout << error << " @ read_some1" << std::endl;
					break;
				}
				type = buf;
				if (len != 1)
					std::cout << "error on read1: " << len << " should be " << 1 << std::endl;
			}

			switch (type) {
				case 0: {
					size_t buf_size = 112; //*thread_num_max;
					unsigned char* buf = new unsigned char[buf_size]; //get header
					boost::system::error_code error;
					size_t len = boost::asio::read(*socket_to_server, boost::asio::buffer(buf, buf_size), boost::asio::transfer_all(), error);
					if (error == boost::asio::error::eof) {
						done = true;
						break; // Connection closed cleanly by peer.
					} else if (error) {
						//std::cout << error << " @ read2a" << std::endl;
						done = true;
						break;
					}
					if (len == buf_size) {
						_bprovider->setBlocksFromData(buf);
						std::cout << "[MASTER] work received - ";
						if (_bprovider->getOriginalBlock() != NULL) print256("sharetarget", (uint32_t*)(_bprovider->getOriginalBlock()->targetShare));
						else std::cout << "<NULL>" << std::endl;
					} else
						std::cout << "error on read2a: " << len << " should be " << buf_size << std::endl;
					delete[] buf;
				} break;
				case 1: {
					size_t buf_size = 4;
					int buf; //get header
					boost::system::error_code error;
					size_t len = boost::asio::read(*socket_to_server, boost::asio::buffer(&buf, buf_size), boost::asio::transfer_all(), error);
					if (error == boost::asio::error::eof) {
						done = true;
						break; // Connection closed cleanly by peer.
					} else if (error) {
						//std::cout << error << " @ read2b" << std::endl;
						done = true;
						break;
					}
					if (len == buf_size) {
						int retval = buf > 1000 ? 1 : buf;
						std::cout << "[MASTER] submitted share -> " <<
							(retval == 0 ? "REJECTED" : retval < 0 ? "STALE" : retval ==
							1 ? "BLOCK" : "SHARE") << std::endl;
						if (retval > 0)
							reject_counter = 0;
						else
							reject_counter++;
						if (reject_counter >= 3) {
							std::cout << "too many rejects (3) in a row, forcing reconnect." << std::endl;
							socket->close();
							done = true;
						}
						{
							std::map<int,unsigned long>::iterator it = statistics.find(retval);
							if (it == statistics.end())
								statistics.insert(std::pair<int,unsigned long>(retval,1));
							else
								statistics[retval]++;
							stats_running();
						}
					} else
						std::cout << "error on read2b: " << len << " should be " << buf_size << std::endl;
				} break;
				case 2: {
					//PING-PONG EVENT, nothing to do
				} break;
				default: {
					//std::cout << "unknown header type = " << type << std::endl;
				}
			}
		}

		_bprovider->setBlockTo(NULL);
		socket_to_server = NULL; //TODO: lock/mutex		
		std::cout << "no connection to the server, reconnecting in 10 seconds" << std::endl;
		boost::this_thread::sleep(boost::posix_time::seconds(10));
	}
  }

  ~CMasterThread() {}

  void wait_for_master() {
    boost::shared_lock<boost::shared_mutex> lock(_mutex_master);
  }

  boost::shared_mutex& get_working_lock() {
    return _mutex_working;
  }

private:

  void wait_for_workers() {
    boost::unique_lock<boost::shared_mutex> lock(_mutex_working);
  }

  CBlockProviderGW  *_bprovider;

  boost::shared_mutex _mutex_master;
  boost::shared_mutex _mutex_working;

	// Provides real time stats
	void stats_running() {
		if (!running) return;
		std::cout << std::fixed;
		std::cout << std::setprecision(1);
		boost::posix_time::ptime t_end = boost::posix_time::second_clock::local_time();
		unsigned long rejects = 0;
		unsigned long stale = 0;
		unsigned long valid = 0;
		unsigned long blocks = 0;
		for (std::map<int,unsigned long>::iterator it = statistics.begin(); it != statistics.end(); ++it) {
			if (it->first < 0) stale += it->second;
			if (it->first == 0) rejects = it->second;
			if (it->first == 1) blocks = it->second;
			if (it->first > 1) valid += it->second;
		}
		std::cout << "[STATS] " << t_end << " | ";
		if ((t_end - t_start).total_seconds() > 0) {
			std::cout << static_cast<double>(totalCollisionCount) / (static_cast<double>((t_end - t_start).total_seconds()) / 60.0) << " c/m | ";
			std::cout << static_cast<double>(totalShareCount) / (static_cast<double>((t_end - t_start).total_seconds()) / 60.0) << " sh/m | ";			
		}
		if (valid+blocks+rejects+stale > 0) {
			std::cout << "VL: " << valid+blocks << " (" << (static_cast<double>(valid+blocks) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%), ";
			std::cout << "RJ: " << rejects << " (" << (static_cast<double>(rejects) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%), ";
			std::cout << "ST: " << stale << " (" << (static_cast<double>(stale) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%)" << std::endl;
		} else {
			std::cout <<  "VL: " << 0 << " (" << 0.0 << "%), ";
			std::cout <<  "RJ: " << 0 << " (" << 0.0 << "%), ";
			std::cout <<  "ST: " << 0 << " (" << 0.0 << "%)" << std::endl;
		}
	}
};

/*********************************
* exit / end / shutdown
*********************************/

void exit_handler() {
	//cleanup for not-retarded OS
	if (socket_to_server != NULL) {
		socket_to_server->close();
		socket_to_server = NULL;
	}
	running = false;
}

#if defined(__MINGW32__) || defined(__MINGW64__)

//#define WIN32_LEAN_AND_MEAN
//#include <windows.h>

BOOL WINAPI ctrl_handler(DWORD dwCtrlType) {
	//'special' cleanup for windows
	switch(dwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT: {
			if (socket_to_server != NULL) {
				socket_to_server->close();
				socket_to_server = NULL;
			}
			running = false;
		} break;
		default: break;
	}
	return FALSE;
}

#elif defined(__GNUG__) && !defined(__APPLE__)

static sighandler_t set_signal_handler (int signum, sighandler_t signalhandler) {
   struct sigaction new_sig, old_sig;
   new_sig.sa_handler = signalhandler;
   sigemptyset (&new_sig.sa_mask);
   new_sig.sa_flags = SA_RESTART;
   if (sigaction (signum, &new_sig, &old_sig) < 0)
      return SIG_ERR;
   return old_sig.sa_handler;
}

void ctrl_handler(int signum) {
	exit(1);
}

#endif

void print_help(const char* _exec) {
	std::cerr << "usage: " << _exec << " <payout-address> <threads-to-use> [memory-option] [mode]" << std::endl;
	std::cerr << std::endl;
	std::cerr << "memory-option: integer value - memory usage" << std::endl;
	std::cerr << "\t\t20 -->    4 MB per thread (not recommended)" << std::endl;
	std::cerr << "\t\t21 -->    8 MB per thread (not recommended)" << std::endl;
	std::cerr << "\t\t22 -->   16 MB per thread (not recommended)" << std::endl;
	std::cerr << "\t\t23 -->   32 MB per thread (not recommended)" << std::endl;
	std::cerr << "\t\t24 -->   64 MB per thread (not recommended)" << std::endl;
	std::cerr << "\t\t25 -->  128 MB per thread" << std::endl;
	std::cerr << "\t\t26 -->  256 MB per thread" << std::endl;
	std::cerr << "\t\t27 -->  512 MB per thread (default)" << std::endl;
	std::cerr << "\t\t28 --> 1024 MB per thread" << std::endl;
	std::cerr << "\t\t29 --> 2048 MB per thread" << std::endl;
	std::cerr << "\t\t30 --> 4096 MB per thread" << std::endl;
	std::cerr << std::endl;
	std::cerr << "mode: string - mining implementation" << std::endl;
	std::cerr << "\t\tavx --> use AVX (Intel optimized)" << std::endl;
	std::cerr << "\t\tsse4 --> use SSE4 (Intel optimized)" << std::endl;
	std::cerr << "\t\tsph --> use SPHLIB" << std::endl;
	std::cerr << std::endl;
	std::cerr << "example:" << std::endl;
	std::cerr << "> " << _exec << " PbfspbvSWxYqrp3DpRH7bsrmEqzY3418Ap 4 25 sse4" << std::endl;
}

/*********************************
* main - this is where it begins
*********************************/
int main(int argc, char **argv)
{
	std::cout << "********************************************" << std::endl;
	std::cout << "*** cudapts - Nvidia PTS Pool Miner v" << VERSION_MAJOR << "." << VERSION_MINOR << " " << VERSION_EXT << std::endl;
	std::cout << "*** by dga - based on ptsminer." << std::endl;
        std::cout << "*** If you like this software, please consider sending tips to:" << std::endl;
        std::cout << "*** PTS:  Pr8cnhz5eDsUegBZD4VZmGDARcKaozWbBc" << std::endl;
        std::cout << "*** BTC:  17sb5mcCnnt4xH3eEkVi6kHvhzQRjPRBtS" << std::endl;
        std::cout << "*** Your donations will encourage further optimization and development" << std::endl;
	std::cout << "***" << std::endl;
	std::cout << "*** press CTRL+C to exit" << std::endl;
	std::cout << "********************************************" << std::endl;
	
	if (argc < 3 || argc > 5)
	{
		print_help(argv[0]);
		return EXIT_FAILURE;
	}

	use_avxsse4 = false;
	if (argc == 5) {
		std::string mode_param(argv[4]);
		if (mode_param == "avx") {
			Init_SHA512_avx();
			use_avxsse4 = true;
			std::cout << "using AVX" << std::endl;
		} else if (mode_param == "sse4") {
			Init_SHA512_sse4();
			use_avxsse4 = true;
			std::cout << "using SSE4" << std::endl;
		} else if (mode_param == "sph") {
			std::cout << "using SPHLIB" << std::endl;
		} else {
			std::cout << "invalid mode" << std::endl << std::endl;
			print_help(argv[0]);
			return EXIT_FAILURE;
		}
	} else {
#ifdef	__x86_64__
		processor_info_t proc_info;
		cpuid_basic_identify(&proc_info);
		if (proc_info.proc_type == PROC_X64_INTEL || proc_info.proc_type == PROC_X64_AMD) {
			if (proc_info.avx_level > 0) {
				Init_SHA512_avx();
				use_avxsse4 = true;
				std::cout << "using AVX" << std::endl;
			} else if (proc_info.sse_level >= 4) {
				Init_SHA512_sse4();
				use_avxsse4 = true;
				std::cout << "using SSE4" << std::endl;
			} else
				std::cout << "using SPHLIB (no avx/sse4)" << std::endl;
		} else
			std::cout << "using SPHLIB (unsupported arch)" << std::endl;
#else
		//TODO: make this compatible with 32bit systems
		std::cout << "**** >>> WARNING" << std::endl;
		std::cout << "**" << std::endl;
		std::cout << "**" << "SSE4/AVX auto-detection not available on your machine" << std::endl;
		std::cout << "**" << "please enable SSE4 or AVX manually" << std::endl;
		std::cout << "**" << std::endl;
		std::cout << "**** >>> WARNING" << std::endl;
#endif
	}

	t_start = boost::posix_time::second_clock::local_time();
	running = true;

#if defined(__MINGW32__) || defined(__MINGW64__)
	SetConsoleCtrlHandler(ctrl_handler, TRUE);
#elif defined(__GNUG__) && !defined(__APPLE__)
	set_signal_handler(SIGINT, ctrl_handler);
#endif

	const int atexit_res = std::atexit(exit_handler);
	if (atexit_res != 0)
		std::cerr << "atexit registration failed, shutdown will be dirty!" << std::endl;

	// init everything:
	socket_to_server = NULL;
	thread_num_max = 1; //GetArg("-genproclimit", 1); // what about boost's hardware_concurrency() ?
	gpu_device_id = atoi(argv[2]); 
	if (argc == 4 || argc == 5)
		COLLISION_TABLE_BITS = atoi(argv[3]);
	else
		COLLISION_TABLE_BITS = 27;
	fee_to_pay = 0; //GetArg("-poolfee", 3);
	miner_id = 0; //GetArg("-minerid", 0);
	pool_username = argv[1]; //GetArg("-pooluser", "");
	pool_password = "blabla"; //GetArg("-poolpassword", "");
	
	if (COLLISION_TABLE_BITS < 20 || COLLISION_TABLE_BITS > 30)
	{
		std::cerr << "unsupported memory option, choose a value between 20 and 31" << std::endl;
		return EXIT_FAILURE;
	}

	if (thread_num_max == 0 || thread_num_max > MAX_THREADS)
	{
		std::cerr << "usage: " << "current maximum supported number of threads = " << MAX_THREADS << std::endl;
		return EXIT_FAILURE;
	}

	{
		unsigned char pw[32];
		//SPH
		sph_sha256_context c256_sph;		
		sph_sha256_init(&c256_sph);
		sph_sha256(&c256_sph, (unsigned char*)pool_password.c_str(), pool_password.size());
		sph_sha256_close(&c256_sph, pw);
		//print256("sph",(uint32_t*)pw);
		//
		std::stringstream ss;
		ss << std::setw(5) << std::setfill('0') << std::hex << (pw[0] ^ pw[5] ^ pw[2] ^ pw[7]) << (pw[4] ^ pw[1] ^ pw[6] ^ pw[3]);
		pool_password = ss.str();	
	}

	// ok, start mining:
	CBlockProviderGW* bprovider = new CBlockProviderGW();
	CMasterThread *mt = new CMasterThread(bprovider);
	mt->run();

	// end:
	return EXIT_SUCCESS;
}

/*********************************
* and this is where it ends
*********************************/
