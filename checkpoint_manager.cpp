#include "checkpoint_manager.h"
#include "hvac_comm.h"

#include <numeric>

extern "C"
{
#include "hvac_logging.h"
#include <fcntl.h>
#include <cassert>
#include <unistd.h>
}

// Moved its definition into hvac_comm.cpp to avoid undefined symbol error in client side. 
// CheckpointManager checkpoint_manager;


vector<long long> read_latencies; 

// int server_count = atoi(getenv("HVAC_SERVER_COUNT"));
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
    L4C_INFO("Checkpoint manager: New file metadata is created for %s: %lld", filename.c_str(), global_chunk_index); 
    // Needed logic for multi-client environment, maybe.
    allocate_new_chunk();

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
    L4C_INFO("%s: size: %lld", filename.c_str(), meta.size); 
  } catch (...)
  {
    L4C_INFO("%s not exists in Checkpoint Manager", filename); 
  }
}


// Function called on the Read mode open
int CheckpointManager::open_checkpoint(const std::string &filename, int flag)
{
  /*
  파일 FILENAME에 오픈 요청에대 해 fd 부여 및 fd to filename 매핑 필요
  */ 
 int fd; 
  try{
    // TODO: we have to check whether FILENAME is stored in FILE_METADATA
    fd = global_fd;
    global_fd -= 1; 

    fd_to_path[fd] = filename;
    // fd_to_offset[fd] = 0; 

    // 오픈 시도하는 파일에 대한 메타데이터 조사: 
    read_file_metadata(filename); 

  }
  catch (...)
  {
    L4C_INFO("Exception occured in open_checkpoint"); 
  }

  return fd; 
}
/*
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

    // 체크포인트 요청 처리 핸들링 여부 로그 
    L4C_INFO("checkpoint manager - read: %d %lld | %lld | %lld", fd, count, fd_to_offset[fd], meta.size); 


    
    // Debug: Mercury latency 조사 - 서버 do nothing 
    // 현재 오프셋, 카운트, 파일 크기 기준으로 총읽기량 결정 
    // size_t to_read = (fd_to_offset[fd] + count <= meta.size)? count : meta.size - fd_to_offset[fd];
    // fd_to_offset[fd] += to_read; 
    // readbytes = to_read; 
     
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
        // 현재는 남은요구읽기량, 청크내남은량만 고려... 파일 전체 남은량(meta.size-offset) 고려 안돼   
        // size_t to_read = std::min(remaining, CHUNK_SIZE - chunk_offset);
        // 청크내남은량, 파일내남은양을 하나의 변수로 취급: 
        // size_t to_read = std::min(remaining, CHUNK_SIZE - chunk_offset);
        size_t to_read = std::min(remaining, chunk->offset - chunk_offset);
        std::memcpy(output_buf, chunk->buffer.get() + chunk_offset, to_read);

        // Update pointers and counters
        output_buf += to_read;
        remaining -= to_read;
        offset += to_read;
        // 파일 오프셋은 읽어나갈 수록 바뀌어나가 
        fd_to_offset[fd] += to_read; 
        readbytes += to_read; 
    }

    if (remaining > 0)
    {
        L4C_INFO("Requested more data than available in checkpoint");
    }

    return readbytes; 
}
*/

size_t CheckpointManager::read_checkpoint(int fd, void *buf, size_t count, off64_t file_offset)
{
    // std::lock_guard<std::mutex> lock(mtx);


    auto start = chrono::high_resolution_clock::now();


    // Map the file descriptor to the corresponding file path and offset
    if (fd_to_path.find(fd) == fd_to_path.end())
    {
        L4C_INFO("Invalid file descriptor: %d", fd);
        exit(-1); 
        // return;
    }

    const std::string &filename = fd_to_path[fd];
    size_t offset = file_offset;

    if (file_metadata.find(filename) == file_metadata.end())
    {
        L4C_INFO("File metadata not found for: %s", filename.c_str());
        exit(-1); 
        // return;
    }

    auto &meta = file_metadata[filename];
    size_t remaining = count;
    size_t readbytes = 0; 

    // 체크포인트 요청 처리 핸들링 여부 로그 
    L4C_INFO("checkpoint manager - read: %d %lld | %lld | %lld", fd, count, offset, meta.size); 


 
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
        // 현재는 남은요구읽기량, 청크내남은량만 고려... 파일 전체 남은량(meta.size-offset) 고려 안돼   
        // size_t to_read = std::min(remaining, CHUNK_SIZE - chunk_offset);
        // 청크내남은량, 파일내남은양을 하나의 변수로 취급: 
        // size_t to_read = std::min(remaining, CHUNK_SIZE - chunk_offset);
        size_t to_read = std::min(remaining, chunk->offset - chunk_offset);
        // chunk->offset: chunk내 현재까지 써진 위치 / cnunk_offset: chunk내 현재 요청받은 위치  값 조사
        L4C_INFO("%lld %lld %lld", remaining, chunk->offset, chunk_offset);
        std::memcpy(output_buf, chunk->buffer.get() + chunk_offset, to_read);


        // Update pointers and counters
        output_buf += to_read;
        remaining -= to_read;
        offset += to_read;
        // 파일 오프셋은 읽어나갈 수록 바뀌어나가 
        // fd_to_offset[fd] += to_read; 
        readbytes += to_read; 
    }

    if (remaining > 0)
    {
        L4C_INFO("Requested more data than available in checkpoint");
    }
    auto end = chrono::high_resolution_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    read_latencies.push_back(latency.count()); 
  

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

  // 마지막으로 읽은 시점의 offset 조사
  L4C_INFO("checkpoint manager - close: %s %lld", fd_to_path[fd].c_str(), fd_to_offset[fd]); 
  long long total_latencies = std::accumulate(read_latencies.begin(), read_latencies.end(), 0LL); 
  // fd, 요청 개수, 요청 총합(ms), 
  L4C_INFO("close:\nfd:%d\ncount:%d\nlatencies:%lld ms", fd, read_latencies.size(), total_latencies);

  return 0; 
}


// TODO: This should be deprecated -This function will not be called for Client-side offset managmement.
off64_t CheckpointManager::lseek_checkpoint(int fd, off64_t offset, int whence)
{
  if (fd_to_offset.find(fd) == fd_to_offset.end())
  {
    L4C_INFO("checkpoint_manager - Lseek: Invalid File Descriptor"); 
  }

  switch(whence)
  {
    case SEEK_SET: 
      fd_to_offset[fd] = offset; break; 
    case SEEK_CUR: 
      fd_to_offset[fd] = fd_to_offset[fd] + offset; break; 
    case SEEK_END:  
      L4C_INFO("checkpoint manager - lseek: it reaches on SEEK_END case: Check if it is valid operation"); 
      // fd_to_path 로 file_path 얻고 이로써 file_metadtaa 취할 수 있어 
      string file_path = fd_to_path[fd]; 
      auto & meta = file_metadata[file_path]; 
      fd_to_offset[fd] = meta.size + offset; 
      // We should set offset in further write as much as not only bytes written but also offset increased this time.
  }
  L4C_INFO("lseek:  %lld %lld", offset, fd_to_offset[fd]);  // (파일:오프셋: (fd, offset, whence)
  return fd_to_offset[fd]; 
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