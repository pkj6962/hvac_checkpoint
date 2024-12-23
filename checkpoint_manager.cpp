#include "checkpoint_manager.h"
#include "hvac_comm.h"
extern "C"
{
#include "hvac_logging.h"
#include <fcntl.h>
#include <cassert>
#include <unistd.h>
}

// Moved its definition into hvac_comm.cpp to avoid undefined symbol error in client side. 
// CheckpointManager checkpoint_manager;


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

// void CheckpointManager::send_chunk_to_remote(const std::string &filename, const char *data, size_t size, int local_fd)
// {
//   ssize_t bytes_written = -1;
//   hg_bool_t done = HG_FALSE;
//   pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
//   pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//   int host = std::hash<std::string>{}(filename) % server_count;
//   int current_host = atoi(getenv("PMI_RANK"));
//   if (host == current_host)
//   {
//     host = (host + 1) % server_count;
//   }

//   hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
//   hvac_rpc_state_p->bytes_written = &bytes_written;
//   hvac_rpc_state_p->done = &done;
//   hvac_rpc_state_p->cond = &cond;
//   hvac_rpc_state_p->mutex = &mutex;

//   hvac_client_comm_gen_write_rpc(host, local_fd, data, size, -1, hvac_rpc_state_p);
//   bytes_written = hvac_write_block(host, &done, &bytes_written, &cond, &mutex);
// }

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
      // send_chunk_to_remote(filename, chunk->buffer.get(), CHUNK_SIZE, local_fd);
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
      // send_chunk_to_remote(filename, chunk->buffer.get(), CHUNK_SIZE, local_fd);
    }
  }
}

// void CheckpointManager::finalize_file_write(const std::string &filename, int local_fd)
// {
//   std::lock_guard<std::mutex> lock(mtx);

//   auto it = file_metadata.find(filename);
//   if (it == file_metadata.end())
//   {
//     return;
//   }

//   const auto &meta = it->second;
//   if (meta.chunk_indexes.empty())
//   {
//     return;
//   }

//   size_t &current_chunk_index = current_file_chunk_index[filename];
//   CheckpointChunk *chunk = get_current_chunk(current_chunk_index);

//   // Send any remaining data in the last chunk
//   size_t file_data_in_chunk = meta.size % CHUNK_SIZE;
//   if (file_data_in_chunk > 0)
//   {
//     send_chunk_to_remote(filename, chunk->buffer.get(), file_data_in_chunk, local_fd);
//   }
// }

void CheckpointManager::read_file_metadata(const std::string &filename)
{
  /*
  filename으로 file_metadata에서 FileMetadta 취득... 
  그로부터 size 출력 
  디버깅 목적: file_metatdata에 filename 키로서 반드시 존재할 것으로 가정
  */
  try
  {
    auto &meta = file_metadata[filename]; 
    L4C_INFO("%s: size: %lld", meta.size); 
  } catch (...)
  {
    L4C_INFO("%s not exists in Checkpoint Manager", filename); 
  }
}


int CheckpointManager::open_checkpoint(const std::string &filename, int flag)
{
  /*
  파일 FILENAME에 오픈 요청에대 해 fd 부여 및 fd to filename 매핑 필요
  */ 
 int fd; 
  try{
    fd = global_fd;
    global_fd -= 1; 

    fd_to_path[fd] = filename;
    fd_to_offset[fd] = 0; 
  }
  catch (...)
  {
    L4C_INFO("Exception occured in open_checkpoint"); 
  }

  return fd; 
}

size_t CheckpointManager::read_checkpoint(int fd, void *buf, size_t count)
{
    std::lock_guard<std::mutex> lock(mtx);

    // Map the file descriptor to the corresponding file path and offset
    if (fd_to_path.find(fd) == fd_to_path.end())
    {
        L4C_INFO("Invalid file descriptor: %d", fd);
        exit(-1); 
        // return;
    }

    const std::string &filename = fd_to_path[fd];
    size_t offset = fd_to_offset[fd];

    if (file_metadata.find(filename) == file_metadata.end())
    {
        L4C_INFO("File metadata not found for: %s", filename.c_str());
        exit(-1); 
        // return;
    }

    auto &meta = file_metadata[filename];
    size_t remaining = count;
    size_t readbytes = 0; 
    char *output_buf = static_cast<char *>(buf);

    while (remaining > 0 && offset < meta.size)
    {
        // Determine the chunk index and position within the chunk
        size_t chunk_index = offset / CHUNK_SIZE;
        size_t chunk_offset = offset % CHUNK_SIZE;

        if (chunk_index >= meta.chunk_indexes.size())
        {
            L4C_INFO("Invalid chunk index for offset: %zu", offset);
            exit(-1); 
            // return;
        }

        CheckpointChunk *chunk = get_current_chunk(meta.chunk_indexes[chunk_index]);

        // Calculate how much data to read from this chunk
        size_t to_read = std::min(remaining, CHUNK_SIZE - chunk_offset);
        std::memcpy(output_buf, chunk->buffer.get() + chunk_offset, to_read);

        // Update pointers and counters
        output_buf += to_read;
        remaining -= to_read;
        offset += to_read;
        readbytes += to_read; 
    }

    if (remaining > 0)
    {
        L4C_INFO("Requested more data than available in checkpoint");
    }

    return readbytes; 
}

int CheckpointManager::close_checkpoint(int fd)
{
  if (fd_to_path.find(fd) == fd_to_path.end())
  {
    L4C_INFO("Invalid file descriptor"); 
  }
  fd_to_path.erase(fd);
  fd_to_offset.erase(fd); 
  return 0; 
}