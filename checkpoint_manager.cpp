#include <iostream>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <cstring>

#define CHUNK_SIZE (1L * 24 * 1024 * 1024)

// TODO add handling of FD on the remote;; maybe no need; just send with the path

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

  CheckpointChunk() : buffer(std::make_unique<char[]>(CHUNK_SIZE)), offset(0), full(false) {}
};

class CheckpointManager
{
private:
  std::unordered_map<std::string, FileMetadata> file_metadata;
  std::unordered_map<std::string, size_t> current_file_chunk_index;
  std::vector<std::unique_ptr<CheckpointChunk>> chunks;
  std::mutex mtx;
  size_t global_chunk_index = 0;

  CheckpointChunk *get_current_chunk(size_t chunk_index)
  {
    if (chunk_index >= chunks.size())
    {
      allocate_new_chunk();
    }
    return chunks[chunk_index].get();
  }

  void allocate_new_chunk()
  {
    chunks.push_back(std::make_unique<CheckpointChunk>());
    global_chunk_index = chunks.size() - 1;
  }

  // Might need to revisit parameters
  void send_chunk_to_remote(const char *data, size_t size)
  {
    // TODO: add RPC
  }

public:
  CheckpointManager() { allocate_new_chunk(); }

  void write_checkpoint(const std::string &filename, const void *buf, size_t count)
  {
    const char *data = static_cast<const char *>(buf);
    size_t remaining = count;

    {
      std::lock_guard<std::mutex> lock(mtx);

      auto &meta = file_metadata[filename];
      size_t &current_chunk_index = current_file_chunk_index[filename];

      if (meta.chunk_indexes.empty())
      {
        current_chunk_index = global_chunk_index;
        meta.chunk_indexes.push_back(global_chunk_index);
      }

      while (remaining > 0)
      {
        CheckpointChunk *chunk = get_current_chunk(current_chunk_index);
        size_t space_in_chunk = CHUNK_SIZE - chunk->offset;

        if (space_in_chunk == 0)
        {
          chunk->full = true;
          send_chunk_to_remote(chunk->buffer.get(), CHUNK_SIZE);
          allocate_new_chunk();
          current_chunk_index = global_chunk_index;
          meta.chunk_indexes.push_back(global_chunk_index);
          chunk = get_current_chunk(current_chunk_index);
          space_in_chunk = CHUNK_SIZE;
        }

        size_t to_write = std::min(remaining, space_in_chunk);
        std::memcpy(chunk->buffer.get() + chunk->offset, data, to_write);
        chunk->offset += to_write;
        meta.size += to_write;
        remaining -= to_write;
        data += to_write;

        if (chunk->offset == CHUNK_SIZE)
        {
          chunk->full = true;
          send_chunk_to_remote(chunk->buffer.get(), CHUNK_SIZE);
        }
      }
    }
  }

  void finalize_file_write(const std::string &filename)
  {
    std::lock_guard<std::mutex> lock(mtx);

    auto it = file_metadata.find(filename);
    if (it == file_metadata.end())
    {
      return;
    }

    const auto &meta = it->second;
    if (meta.chunk_indexes.empty())
    {
      return;
    }

    size_t &current_chunk_index = current_file_chunk_index[filename];
    CheckpointChunk *chunk = get_current_chunk(current_chunk_index);

    size_t file_data_in_chunk = meta.size % CHUNK_SIZE;
    if (file_data_in_chunk > 0)
    {
      send_chunk_to_remote(chunk->buffer.get(), file_data_in_chunk);
    }
  }
};
