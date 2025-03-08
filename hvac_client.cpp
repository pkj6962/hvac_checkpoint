// Starting to use CPP functionality

#include <map>
#include <string>
#include <filesystem>
#include <iostream>
#include <assert.h>
#include <mutex>
#include <unistd.h> 
#include <sys/stat.h>

#include "hvac_internal.h"
#include "hvac_logging.h"
#include "hvac_comm.h"
#include "hvac_hashing.h"
#include "hvac_write_data_mover.h"

#define VIRTUAL_NODE_CNT 100

__thread bool tl_disable_redirect = false;
bool g_disable_redirect = true;
bool g_hvac_initialized = false;
bool g_hvac_comm_initialized = false;
bool g_mercury_init = false;

uint32_t g_hvac_server_count = 0;
uint32_t hvac_client_per_node = 0;
char *hvac_data_dir = NULL;
char *hvac_checkpoint_dir = NULL;

pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

std::map<int, off64_t> fd_to_offset;
std::map<int, std::string> fd_map;
std::map<int, int> fd_redir_map;
std::map<int, int> fd_to_dramfd;

// sy: add
const int TIMEOUT_LIMIT = 3;
HashRing<string, string> *hashRing; // ptr to the consistent hashing object
vector<bool> failure_flags;
static void initialize_real_functions(); 



/* Devise a way to safely call this and initialize early */
static void __attribute__((constructor)) hvac_client_init()
{
  pthread_mutex_lock(&init_mutex);
  if (g_hvac_initialized)
  {
    pthread_mutex_unlock(&init_mutex);
    return;
  }
  hvac_init_logging();

  L4C_INFO("1");
  char *hvac_data_dir_c = getenv("HVAC_DATA_DIR");
  char *hvac_checkpoint_dir_c = getenv("HVAC_CHECKPOINT_DIR");

  if (getenv("HVAC_SERVER_COUNT") != NULL)
  {
    g_hvac_server_count = atoi(getenv("HVAC_SERVER_COUNT"));
  }
  else
  {
    L4C_FATAL("Please set enviroment variable HVAC_SERVER_COUNT\n");
    //   exit(-1);
    return;
  }

  if (getenv("HVAC_CLIENT_PER_NODE") != NULL)
  {
    hvac_client_per_node = atoi(getenv("HVAC_CLIENT_PER_NODE"));
  }

  L4C_INFO("2");

  if (hvac_data_dir_c != NULL)
  {
    hvac_data_dir = (char *)malloc(strlen(hvac_data_dir_c) + 1);
    snprintf(hvac_data_dir, strlen(hvac_data_dir_c) + 1, "%s", hvac_data_dir_c);
  }

  if (hvac_checkpoint_dir_c != NULL)
  {
    hvac_checkpoint_dir = (char *)malloc(strlen(hvac_checkpoint_dir_c) + 1);
    snprintf(hvac_checkpoint_dir, strlen(hvac_checkpoint_dir_c) + 1, "%s", hvac_checkpoint_dir_c);
  }

  L4C_INFO("3");

  /* sy: add */
  initialize_hash_ring(g_hvac_server_count, VIRTUAL_NODE_CNT);
  L4C_INFO("4");

  hvac_get_addr();
  L4C_INFO("5");

  initialize_real_functions();

  // Farid: this must be run only once
  start_background_worker();

  g_hvac_initialized = true;
  pthread_mutex_unlock(&init_mutex);

  L4C_INFO("6");

  g_disable_redirect = false;
}

static void __attribute((destructor)) hvac_client_shutdown()
{
  hvac_shutdown_comm();
  delete hashRing;
}

#include <dlfcn.h>

FILE* (*__real_fopen)(const char *, const char *) = nullptr;
FILE* (*__real_fopen64)(const char *, const char *) = nullptr;
ssize_t (*__real_pread)(int, void *, size_t, off_t) = nullptr;
ssize_t (*__real_readv)(int, const struct iovec *, int) = nullptr;
ssize_t (*__real_write)(int, const void *, size_t) = nullptr;
int (*__real_open)(const char *, int, ...) = nullptr;
int (*__real_open64)(const char *, int, ...) = nullptr;
ssize_t (*__real_read)(int, void *, size_t) = nullptr;
ssize_t (*__real_read64)(int, void *, size_t) = nullptr;
int (*__real_close)(int) = nullptr;
off_t (*__real_lseek)(int, off_t, int) = nullptr;
off64_t (*__real_lseek64)(int, off64_t, int) = nullptr;

static void initialize_real_functions() {
    __real_fopen = (FILE* (*)(const char*, const char*)) dlsym(RTLD_NEXT, "fopen");
    __real_fopen64 = (FILE* (*)(const char*, const char*)) dlsym(RTLD_NEXT, "fopen64");
    __real_pread = (ssize_t (*)(int, void*, size_t, off_t)) dlsym(RTLD_NEXT, "pread");
    __real_readv = (ssize_t (*)(int, const struct iovec*, int)) dlsym(RTLD_NEXT, "readv");
    __real_write = (ssize_t (*)(int, const void*, size_t)) dlsym(RTLD_NEXT, "write");
    __real_open = (int (*)(const char*, int, ...)) dlsym(RTLD_NEXT, "open");
    __real_open64 = (int (*)(const char*, int, ...)) dlsym(RTLD_NEXT, "open64");
    __real_read = (ssize_t (*)(int, void*, size_t)) dlsym(RTLD_NEXT, "read");
    __real_read64 = (ssize_t (*)(int, void*, size_t)) dlsym(RTLD_NEXT, "read64");
    __real_close = (int (*)(int)) dlsym(RTLD_NEXT, "close");
    __real_lseek = (off_t (*)(int, off_t, int)) dlsym(RTLD_NEXT, "lseek");
    __real_lseek64 = (off64_t (*)(int, off64_t, int)) dlsym(RTLD_NEXT, "lseek64");

    if (!__real_fopen || !__real_write || !__real_read || !__real_open) {
        fprintf(stderr, "HVAC ERROR: Failed to load system calls via dlsym\n");
        exit(1);
    }
}

// sy: add. initialization function for hash ring & timeout counter
void initialize_hash_ring(int serverCount, int vnodes)
{
  hashRing = new HashRing<string, string>(vnodes);
  for (int i = 1; i <= serverCount; ++i)
  {
    string server = "server" + to_string(i);
    hashRing->AddNode(server);
  }
  timeout_counters.resize(serverCount, 0);
  failure_flags.resize(serverCount, false);
}

// New version of HVAC_TRACK_FILE
bool hvac_track_file(const char *path, int flags, int fd)
{
  if (strstr(path, ".ports.cfg.") != NULL)
  {
    return false;
  }

  if (strstr(path, ".metadata") != NULL)
  {
    return false;
  }

  if (strstr(path, "train_params.yaml") != NULL)
  {
    return false;
  }

  // 임시 코드 - 디버깅 후 삭제 예정
  if (strstr(path, "strace_output") != NULL)
  {
    return false;
  }

  bool tracked = false;
  bool is_write_mode = (flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR;
  bool is_read_mode = (flags & O_ACCMODE) == O_RDONLY || (flags & O_ACCMODE) == O_RDWR;

  try
  {
    std::string ppath = std::filesystem::canonical(path).parent_path();
    // L4C_INFO("path: %s", path);

    // Check if the file is for reading (existing HVAC_DATA_DIR tracking)
    int access_mode = flags & O_ACCMODE;
    // L4C_INFO("mode: %d", access_mode);

    if ((flags & O_ACCMODE) == O_RDONLY)
    {

      if (hvac_data_dir != NULL)
      {
        std::string test = std::filesystem::canonical(hvac_data_dir);
        if (ppath.find(test) != std::string::npos)
        {
          L4C_INFO("Tracking used HVAC_DATA_DIR file %s", path);
          fd_map[fd] = std::filesystem::canonical(path).string();
          tracked = true;
        }
        // 이 조건이 필요한지 확인 필요
        // else if (ppath == std::filesystem::current_path())
        // {
        //   L4C_INFO("Tracking used CWD file %s", path);
        //   fd_map[fd] = std::filesystem::canonical(path).string();
        //   tracked = true;
        // }
      }
      if (hvac_checkpoint_dir != NULL)
      {
        // 체크포인트 복구: 읽기 모드이고 파일 경로가 체크포인트 디렉토리 내부인 경우
        std::string test = std::filesystem::canonical(hvac_checkpoint_dir);
        if (ppath.find(test) != std::string::npos)
        {
          L4C_INFO("Tracking used HVAC_CHECKPOINT_DIR(read) file %s", path);
          fd_map[fd] = std::filesystem::canonical(path).string();
          tracked = true;
        }
      }
    }
    // Check if the file is for writing (new HVAC_CHECKPOINT_DIR tracking)
    else if (is_write_mode)
    {
      if (hvac_checkpoint_dir != NULL)
      {
        std::string test = std::filesystem::canonical(hvac_checkpoint_dir);
        if (ppath.find(test) != std::string::npos)
        {
          L4C_INFO("Tracking used HVAC_CHECKPOINT_DIR(write) file %s", path);
          fd_map[fd] = std::filesystem::canonical(path).string();
          tracked = true;

          // logic for DRAMFS WRITE
          // string drampath = hvac_get_drampath(ppath);
          string drampath = hvac_get_drampath(path);
          L4C_INFO("drampath in hvac_track_file: %s", drampath.c_str());
          
          struct stat fd_stat; 
          if(fstat(fd, &fd_stat) !=0)
          {
            L4C_FATAL("fstat failed"); 
            exit(1); 
          }

          if (S_ISDIR(fd_stat.st_mode))
          {
            L4C_INFO("Directory: %s", drampath.c_str()); 
            if (mkdir(drampath.c_str(), 0775) == 0)
            {
              L4C_FATAL("directory creation failed"); 
              exit(1); 
            };
          }
          /*
          디렉토리인 경우 디렉토리 생성
          */
          else{
            int dramfd = open(drampath.c_str(), O_WRONLY | O_CREAT, 0644); 
            L4C_INFO("DRAMFD IN HVAC_TRACK_FILE: %d %d", dramfd, errno); 
            fd_to_dramfd[fd] = dramfd;
            fd_to_offset[dramfd] = 0;
          }
        }
      }
    }
  }
  catch (...)
  {
    // Handle exceptions if path canonicalization fails
    L4C_INFO("Process reached here");
  }

  hg_bool_t done = HG_FALSE;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  // Send RPC to tell the server to open the file
  if (tracked)
  {
    if (!g_mercury_init)
    {
      hvac_init_comm(false);
      hvac_client_comm_register_rpc();
      g_mercury_init = true;
    }
    hvac_open_state_t *hvac_open_state_p = (hvac_open_state_t *)malloc(sizeof(hvac_open_state_t));
    hvac_open_state_p->done = &done;
    hvac_open_state_p->cond = &cond;
    hvac_open_state_p->mutex = &mutex;

    int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
    // 디버깅 목적으로 임시로 mpi_rank로 클라이언트 랭크 판단
    int current_host = atoi(getenv("PMI_RANK"));
    // int current_host = atoi(getenv("RANK"));
    // int current_host = atoi(getenv("MPI_RANK"));

    if (is_write_mode)
    {
      // host = current_host / hvac_client_per_node;
    // JH comment: For load balancing in small experiment scale, we just set server next to the client rank.
    // JH
      host = (current_host +1) % g_hvac_server_count; 


      L4C_INFO("Remote open - Host %d %s %d %d (rank: %d)", host, path, is_write_mode, is_read_mode, current_host);
      hvac_client_comm_gen_open_rpc(host, fd_map[fd], fd, hvac_open_state_p);
      hvac_client_block(host, &done, &cond, &mutex);
    }
    else if (is_read_mode)
    {
      // host = hvac_extract_rank(fd_map[fd].c_str()) / hvac_client_per_node;
      host = hvac_extract_rank(fd_map[fd].c_str()) ;
      // TODO: 클라이언트 사이드 fd_to_path 선언 및 할당
      
      // If the file is generated by me, I don't need to request to server but open file by myself. 
      if (host == current_host)
      {
        string drampath = hvac_get_drampath(path);
        int dramfd = open(drampath.c_str(), O_RDONLY);
        if (dramfd < 0)
        {
          L4C_INFO("DRAMFS file open failed");
        }
        fd_redir_map[fd] = dramfd; 
        L4C_INFO("Client %d opened file %s itself\n", client_rank, drampath.c_str()); 
      }
      else 
      {
        L4C_INFO("Remote open - Host %d %s %d %d (rank: %d)", host, path, is_write_mode, is_read_mode, current_host);
        hvac_client_comm_gen_open_rpc(host, fd_map[fd], fd, hvac_open_state_p);
        hvac_client_block(host, &done, &cond, &mutex);
      }
      fd_to_offset[fd] = 0;
    }
      
    
  }
  return tracked;
}

ssize_t hvac_dram_write(int fd, const void *buf, size_t count)
{
  int dramfd = fd_to_dramfd[fd];
  // ssize_t byteswritten = 0;
  ssize_t byteswritten = write(dramfd, buf, count);

  if (byteswritten < 0)
  {
    L4C_FATAL("Dram write failed %d %d %d %s", dramfd, fd, errno, fd_map[fd].c_str());
    // exit(-1);
  }
  // assert(byteswritten == count);
  fd_to_offset[dramfd] += byteswritten;

  // L4C_INFO("dramfd: %d", dramfd);
  const char *_drampath = hvac_fetch_path(dramfd);
  // L4C_INFO("drampath: %s", _drampath);

  string drampath = string(_drampath);
  off_t offset = fd_to_offset[dramfd] - byteswritten;
  // Junghwan TODO: the path_hash should be calculated from the original path of the file
  // in order to keep the hashing consistent.
  int path_hash = std::hash<std::string>{}(fd_map[fd]);
  enqueue_write_task(drampath, path_hash, fd, count);
  return byteswritten; 
}

ssize_t hvac_cache_write(int fd, int path_hash, const void *buf, size_t count)
{
  // TODO: revisit this for fault tolerance in case of unexpected behavior
  ssize_t bytes_written = -1;


  // if (hvac_file_tracked(fd))
  // {
  
    // Farid (Note for Junghwan):
    // This filepath should not be used, as it fd_map might have been modified by this point.
    // Instead we should use the path_hash provided earlier for the queued task.
    // std::string filepath = fd_map[fd];

    hg_bool_t done = HG_FALSE;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
    hvac_rpc_state_p->bytes_written = &bytes_written;
    hvac_rpc_state_p->done = &done;
    hvac_rpc_state_p->cond = &cond;
    hvac_rpc_state_p->mutex = &mutex;

    // Generate the write RPC request.
    // AS IS: Send request to local server 
    // TO be: Send request to remote NVMe server 

    // int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
  
    int current_host = atoi(getenv("PMI_RANK"));
    // int current_host = atoi(getenv("MPI_RANK"));
    // int current_host = atoi(getenv("RANK"));
    // int host = current_host / hvac_client_per_node;
    int host = path_hash % g_hvac_server_count; 
    
    // JH comment: For load balancing in small experiment scale, we just set server next to the client rank.
    host = (current_host +1) % g_hvac_server_count; 


    hvac_client_comm_gen_write_rpc(host, fd, buf, count, -1, hvac_rpc_state_p);
    // assert (false); 
    // Wait for the server to process the write request.
    bytes_written = hvac_write_block(host, &done, &bytes_written, &cond, &mutex);
    L4C_INFO("bytes_written/count\t%lld/%lld | %d", bytes_written, count, host);
    if (bytes_written == -1)
    {
      fd_map.erase(fd);
    }
  // }
  // else
  // {
  //   // Redirect to __real_write; handle __real_open appropriately for fd
  //   bytes_written = -1;
  // }

  return bytes_written;
}

// ssize_t hvac_cache_write(int fd, const void *buf, size_t count)
// {
//   ssize_t bytes_written = -1;
//   hg_bool_t done = HG_FALSE;
//   pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
//   pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//   if (hvac_file_tracked(fd))
//   {
//     // TODO: It should be changed so that the client sends requests to the local server.
//     // Determine which server to communicate vi with based on the file descriptor.

//     const char *rank_str = getenv("PMI_RANK");
//     const char *world_size_str = getenv("SLURM_NTASKS");
//     int client_rank = atoi(rank_str);
//     int world_size = atoi(world_size_str);
//     // TODOL What if N(clients):1(server) model in single node?
//     int host = client_rank / (world_size / g_hvac_server_count);

//     L4C_INFO("NVMe buffering(write) - Host %d", host);

//     hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
//     hvac_rpc_state_p->bytes_written = &bytes_written;
//     hvac_rpc_state_p->done = &done;
//     hvac_rpc_state_p->cond = &cond;
//     hvac_rpc_state_p->mutex = &mutex;

//     // Generate the write RPC request.
//     hvac_client_comm_gen_write_rpc(host, fd, buf, count, -1, hvac_rpc_state_p);

//     // Wait for the server to process the write request.
//     bytes_written = hvac_write_block(host, &done, &bytes_written, &cond, &mutex);
//     if (bytes_written == -1)
//     {
//       fd_map.erase(fd);
//     }
//   }
//   L4C_INFO("Client is redirected to real_write");
//   // Non-HVAC Writes should return -1.
//   return bytes_written;
// }

/* Need to clean this up - in theory the RPC should time out if the request hasn't been serviced we'll go to the file-system?
 * Maybe not - we'll roll to another server.
 * For now we return true to keep the good path happy
 */
ssize_t hvac_remote_read(int fd, void *buf, size_t count)
{
  /* HVAC Code */
  /* Check the local fd - if it's tracked we pass it to the RPC function
   * The local FD is converted to the remote FD with the buf and count
   * We must know the remote FD to avoid collision on the remote side
   */
  ssize_t bytes_read = -1;
  hg_bool_t done = HG_FALSE;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  /* sy: Determine the node failure by checking the timeout limit and failure flags.
      If the failure is detected, 1) remove the node from the hash ring
      2) erase the fd from the fd_map */
  if (hvac_file_tracked(fd))
  {
    // int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
    // HVAC recovery: Logic to find the server that stores in-memory checkpoint
    int client_rank = hvac_extract_rank(fd_map[fd].c_str());
    int host = client_rank / hvac_client_per_node;
    int current_host = atoi(getenv("PMI_RANK")) / hvac_client_per_node;

    
    if (client_rank == current_host)
    {
      bytes_read = pread(fd_redir_map[fd], buf, count, fd_to_offset[fd]); 
      if (bytes_read < 0)
      {
        L4C_INFO("Client DRAMFS read failed"); 
      }
      else
      {
        ;
        // L4C_INFO("Client read DRAMFS from itself"); 
      } 
    }
    else
    {
    
      hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
      hvac_rpc_state_p->bytes_read = &bytes_read;
      hvac_rpc_state_p->done = &done;
      hvac_rpc_state_p->cond = &cond;
      hvac_rpc_state_p->mutex = &mutex;

      // hvac_client_comm_gen_read_rpc(host, fd, buf, count, -1, hvac_rpc_state_p);
      hvac_client_comm_gen_read_rpc(host, fd, buf, count, fd_to_offset[fd], hvac_rpc_state_p);
      bytes_read = hvac_read_block(host, &done, &bytes_read, &cond, &mutex);
      if (bytes_read == -1)
      {
        fd_map.erase(fd);
      }
    }
    
    // JH: Client side Offset management
    fd_to_offset[fd] += bytes_read;
  }
  /* Non-HVAC Reads come from base */
  return bytes_read;
}

/* Need to clean this up - in theory the RPC should time out if the request hasn't been serviced we'll go to the file-system?
 * Maybe not - we'll roll to another server.
 * For now we return true to keep the good path happy
 */
ssize_t hvac_remote_pread(int fd, void *buf, size_t count, off_t offset)
{
  /* HVAC Code */
  /* Check the local fd - if it's tracked we pass it to the RPC function
   * The local FD is converted to the remote FD with the buf and count
   * We must know the remote FD to avoid collision on the remote side
   */
  ssize_t bytes_read = -1;
  hg_bool_t done = HG_FALSE;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  /* sy: Determine the node failure by checking the timeout limit and failure flags.
          If the failure is detected, 1) remove the node from the hash ring
          2) erase the fd from the fd_map */
  if (hvac_file_tracked(fd))
  {
    // int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
    // HVAC recovery: Logic to find the server that stores in-memory checkpoint
    int client_rank = hvac_extract_rank(fd_map[fd].c_str());
    int host = client_rank / hvac_client_per_node;

    // sy: modified logic
    hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
    hvac_rpc_state_p->bytes_read = &bytes_read;
    hvac_rpc_state_p->done = &done;
    hvac_rpc_state_p->cond = &cond;
    hvac_rpc_state_p->mutex = &mutex;

    hvac_client_comm_gen_read_rpc(host, fd, buf, count, offset, hvac_rpc_state_p);
    bytes_read = hvac_read_block(host, &done, &bytes_read, &cond, &mutex);
    if (bytes_read == -1)
    {
      fd_map.erase(fd);
    }
  }
  /* Non-HVAC Reads come from base */
  return bytes_read;
}

ssize_t hvac_remote_lseek(int fd, off64_t offset, int whence)
{
  /* HVAC Code */
  /* Check the local fd - if it's tracked we pass it to the RPC function
   * The local FD is converted to the remote FD with the buf and count
   * We must know the remote FD to avoid collision on the remote side
   */
  ssize_t bytes_lseek = -1;
  // 자체 mutex 선언
  hg_bool_t done = HG_FALSE;
  pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

  if (hvac_file_tracked(fd))
  {
    //		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
    // HVAC recovery: Logic to find the server that stores in-memory checkpoint
    int client_rank = hvac_extract_rank(fd_map[fd].c_str());
    int host = client_rank / hvac_client_per_node;

    // TODO: client 자료구조 선언 및 활용
    switch (whence)
    {
    case SEEK_SET:
      fd_to_offset[fd] = offset;
      break;
    case SEEK_CUR:
      fd_to_offset[fd] = fd_to_offset[fd] + offset;
      break;
    case SEEK_END:
      L4C_INFO("checkpoint manager - lseek: it reaches on SEEK_END case: Check if it is valid operation");
      // fd_to_path 로 file_path 얻고 이로써 file_metadtaa 취할 수 있어
      // string file_path = fd_to_path[fd];
      // auto & meta = file_metadata[file_path];
      // fd_to_offset[fd] = meta.size + offset;
      // We should set offset in further write as much as not only bytes written but also offset increased this time.
    }
    // L4C_INFO("lseek:  %lld %lld", offset, fd_to_offset[fd]); // (파일:오프셋: (fd, offset, whence)
    return fd_to_offset[fd];

    // 자체 hvac_rpc_state_t 자료구조 선언
    // hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
    // hvac_rpc_state_p->bytes_read = &bytes_read;
    // hvac_rpc_state_p->done = &done;
    // hvac_rpc_state_p->cond = &cond;
    // hvac_rpc_state_p->mutex = &mutex;

    // hvac_client_comm_gen_seek_rpc에 자체 자료구조 패스
    // hvac_client_comm_gen_seek_rpc(host, fd, offset, whence, hvac_rpc_state_p);

    // hvac_seek_block에
    // bytes_lseek =  hvac_seek_block(host, &done, &bytes_lseek, &cond, &mutex);

    /*
    L4C_INFO("Remote seek - Host %d", host);
    hvac_client_comm_gen_seek_rpc(host, fd, offset, whence);
    bytes_lseek = hvac_seek_block();
    L4C_INFO("bytes_lseek:%lld", bytes_lseek);
    return bytes_lseek;
    */
  }
  /* Non-HVAC Reads come from base */
  return bytes_lseek;
}

// Farid (Note for Junghwan):
// We must make sure that the enqueue_close_task is called before the next hvac_open_cb
void hvac_close_write(int fd) {
  enqueue_close_task(fd);
}

void hvac_remote_close(int fd)
{
  // fd로써 access_mode 추출... 읽기 모드시에만 rpc 전송
  int flag = fcntl(fd, F_GETFL);
  int access_mode = flag & O_ACCMODE;

  if (hvac_file_tracked(fd) && (access_mode == O_RDONLY))
  {

    int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;
    int current_host = atoi(getenv("PMI_RANK"));
    // int current_host = atoi(getenv("MPI_RANK"));
    host = hvac_extract_rank(fd_map[fd].c_str()) / hvac_client_per_node;

    hvac_rpc_state_t_close *rpc_state = (hvac_rpc_state_t_close *)malloc(sizeof(hvac_rpc_state_t_close));
    rpc_state->done = false;
    rpc_state->timeout = false;
    rpc_state->host = 0;
    hvac_client_comm_gen_close_rpc(host, fd, rpc_state);
  }
  else if (hvac_file_tracked(fd) && (access_mode == O_WRONLY))
  {
    // Logic for DRAM write
    int dramfd = fd_to_dramfd[fd];
    fd_to_dramfd.erase(fd);
    // L4C_INFP("fd")
    fd_to_offset[dramfd] = 0;
    close(dramfd);
  }
  else
  {
    L4C_INFO("This should not be reached");
    exit(-1);
  }
}

bool hvac_file_tracked(int fd)
{
  try
  {
    if (fd_map.empty())
    { // sy: add
      return false;
    }
    return (fd_map.find(fd) != fd_map.end());
  }
  catch (...)
  {
    L4C_INFO("hvac_file_tracked(): this should not be reached");
  }
}

const char *hvac_get_path(int fd)
{
  try
  {
    string path = "/proc/self/fd/" + to_string(fd);
    char filepath[256];
    ssize_t len = readlink(path.c_str(), filepath, sizeof(filepath) - 1);
    filepath[len] = '\0';

    // L4C_INFO("fd on HVAC_GET_PATH: %d %s\n", fd, filepath);
    if (fd_map.empty())
    { // sy: add
      return NULL;
    }

    if (fd_map.find(fd) != fd_map.end())
    {
      return fd_map[fd].c_str();
    }
  }
  catch (...)
  {
    L4C_INFO("hvac_get_path(): this should not be reached");
  }
  return NULL;
}

bool hvac_remove_fd(int fd)
{
  if (fd_map.empty())
  { // sy: add
    return false;
  }
  hvac_remote_close(fd);
  return fd_map.erase(fd);
}

int hvac_extract_rank(const char *file_path)
{
  int rank, idx;

  string str = file_path;
  idx = str.find_last_of('.');
  rank = file_path[idx - 3] - '0';

  return rank;
}

const char *
hvac_fetch_path(int fd)
{
  char *path = new char[PATH_MAX];
  char fd_path[PATH_MAX];

  snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
  ssize_t len = readlink(fd_path, path, PATH_MAX - 1);
  path[len]= '\0'; 
  // L4C_INFO("drampath: %s", path);
  
  return path;
}