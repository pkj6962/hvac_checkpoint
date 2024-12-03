#ifndef CHECKPOINT_MANAGER_H
#define CHECKPOINT_MANAGER_H

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <cstring>

#define CHUNK_SIZE (1L * 24 * 1024 * 1024)

// TODO: add comments, document code
struct FileMetadata
{
  size_t size;
  std::vector<size_t> chunk_indexes;
};

struct CheckpointChunk
{
  std::unique_ptr<char[]> buffer;
  size_t offset;
  bool full;

  CheckpointChunk();
};

class CheckpointManager
{
private:
  std::unordered_map<std::string, FileMetadata> file_metadata;
  std::unordered_map<std::string, size_t> current_file_chunk_index;
  std::vector<std::unique_ptr<CheckpointChunk>> chunks;
  std::mutex mtx;
  size_t global_chunk_index = 0;

  CheckpointChunk *get_current_chunk(size_t chunk_index);
  void allocate_new_chunk();
  void send_chunk_to_remote(const char *data, size_t size);

public:
  CheckpointManager();
  void write_checkpoint(const std::string &filename, const void *buf, size_t count);
  void finalize_file_write(const std::string &filename);
};

#endif
