#include "checkpoint_manager.h"

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

void CheckpointManager::send_chunk_to_remote(const char *data, size_t size)
{
  // TODO add handling of FD on the remote;; maybe no need; just send with the path
}

void CheckpointManager::write_checkpoint(const std::string &filename, const void *buf, size_t count)
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
      send_chunk_to_remote(chunk->buffer.get(), CHUNK_SIZE);
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
      send_chunk_to_remote(chunk->buffer.get(), CHUNK_SIZE);
    }
  }
}

void CheckpointManager::finalize_file_write(const std::string &filename)
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

  // Send any remaining data in the last chunk
  size_t file_data_in_chunk = meta.size % CHUNK_SIZE;
  if (file_data_in_chunk > 0)
  {
    send_chunk_to_remote(chunk->buffer.get(), file_data_in_chunk);
  }
}
