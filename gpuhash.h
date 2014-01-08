class GPUHasher {
public:
  GPUHasher(int gpu_device_id);
  int Initialize();
  int ComputeHashes(uint64_t data[16], uint64_t H[8]);
  ~GPUHasher();
 private:
  int device_id;
  uint64_t *dev_data;
  uint64_t *dev_hashes;
  uint32_t *dev_countbits;
  uint64_t *dev_results;
};
