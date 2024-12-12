#include "checkpoint_manager.h"
#include "hvac_comm.h"

int server_count = atoi(getenv("HVAC_SERVER_COUNT"));
CheckpointChunk::CheckpointChunk()
    : buffer(std::make_unique<char[]>(CHUNK_SIZE)), offset(0), full(false) {}

CheckpointManager::CheckpointManager()
{
  allocate_new_chunk();
}

CheckpointChunk *CheckpointManager::get_current_chunk(size_t chunk_index)
{
  if (chunk_index >= chunks.size())
  {
    allocate_new_chunk();
  }
  return chunks[chunk_index].get();
}

void CheckpointManager::allocate_new_chunk()
{
  chunks.push_back(std::make_unique<CheckpointChunk>());
  global_chunk_index = chunks.size() - 1;
}

void CheckpointManager::write_checkpoint(const std::string &filename, const void *buf, size_t count, int local_fd)
{
  const char *data = static_cast<const char *>(buf);
  size_t remaining = count;

  std::lock_guard<std::mutex> lock(mtx); // Ensure thread safety

  auto &meta = file_metadata[filename];
  size_t &current_chunk_index = current_file_chunk_index[filename];

  // Initialize file metadata if this is the first write
  if (meta.chunk_indexes.empty())
  {
    current_chunk_index = global_chunk_index;
    meta.chunk_indexes.push_back(global_chunk_index);
  }

  while (remaining > 0)
  {
    CheckpointChunk *chunk = get_current_chunk(current_chunk_index);
    size_t space_in_chunk = CHUNK_SIZE - chunk->offset;

    // If chunk is full, send it and allocate a new one
    if (space_in_chunk == 0)
    {
      chunk->full = true;
      allocate_new_chunk();
      current_chunk_index = global_chunk_index;
      meta.chunk_indexes.push_back(global_chunk_index);
      chunk = get_current_chunk(current_chunk_index);
      space_in_chunk = CHUNK_SIZE;
    }

    // Write data to the current chunk
    size_t to_write = std::min(remaining, space_in_chunk);
    std::memcpy(chunk->buffer.get() + chunk->offset, data, to_write);
    chunk->offset += to_write;
    meta.size += to_write;
    remaining -= to_write;
    data += to_write;

    // Mark chunk as full and send it if completely filled
    if (chunk->offset == CHUNK_SIZE)
    {
      chunk->full = true;
    }
  }
}

ssize_t CheckpointManager::pread(int fd, void *buf, size_t count, off_t offset)
{
  std::lock_guard<std::mutex> lock(mtx);

  auto path_it = fd_to_path_map.find(fd);
  // File has been removed from cache;; needs to be fetched from the NVME cache.
  if (path_it == fd_to_path_map.end())
  {
    return -1;
  }
  const std::string &filename = path_it->second;

  auto meta_it = file_metadata.find(filename);
  if (meta_it == file_metadata.end())
  {
    return -1;
  }
  const auto &meta = meta_it->second;
  if (offset >= meta.size)
  {
    return 0; // EOF
  }

  size_t bytes_to_read = std::min(count, meta.size - offset);
  if (bytes_to_read == 0)
  {
    return 0;
  }

  char *output = static_cast<char *>(buf);
  size_t bytes_read = 0;
  size_t current_offset = offset;

  // Find the starting chunk and offset within that chunk
  size_t chunk_size = CHUNK_SIZE;
  size_t start_chunk_index = offset / chunk_size;
  size_t chunk_offset = offset % chunk_size;

  while (bytes_read < bytes_to_read && start_chunk_index < meta.chunk_indexes.size())
  {
    size_t current_chunk_index = meta.chunk_indexes[start_chunk_index];
    CheckpointChunk *chunk = get_current_chunk(current_chunk_index);

    size_t remaining_in_chunk = chunk->offset - chunk_offset;
    size_t to_read = std::min(bytes_to_read - bytes_read, remaining_in_chunk);

    if (to_read > 0)
    {
      std::memcpy(output + bytes_read,
                  chunk->buffer.get() + chunk_offset,
                  to_read);
      bytes_read += to_read;
    }

    start_chunk_index++;
    chunk_offset = 0; // Reset offset for subsequent chunks
  }

  return bytes_read;
}

int CheckpointManager::finalize_file_write(const std::string &filename, int local_fd)
{
  std::lock_guard<std::mutex> lock(mtx);
  // Add fd to path mapping
  fd_to_path_map[current_fd] = filename;
  current_fd--;
  return current_fd + 1;
}
