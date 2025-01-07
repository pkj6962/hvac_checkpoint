/* Data mover responsible for maintaining the NVMe state
 * and prefetching the data
 */
#include <filesystem>
#include <string>
#include <queue>
#include <iostream>

#include <pthread.h>
#include <string.h>
#include <cerrno>
#include <cstring>

#include <unistd.h> 


#include "hvac_logging.h"
#include "hvac_data_mover_internal.h"

using namespace std;
namespace fs = std::filesystem;

pthread_cond_t data_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t path_map_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t flush_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t flush_cond =  PTHREAD_COND_INITIALIZER;

map<int, string> fd_to_path;
map<string, string> path_cache_map;
queue<string> data_queue;
string WRITE_PREFIX = "HVAC_WRITE_PREFIX:";

queue <struct flush_entry> flush_queue; 
extern struct flush_entry flush_entry; 
// struct flush_entry
// {
//   int op;         // open, write, close --> 
//   int fd;         // filename or GlobalFD
//   void * buf; 
//   uint64_t size; 
//   uint64_t offset;
// }flush_entry; 

// queue <struct flush_entry> flush_queue; // hvac_data_mover.cpp 및 hvac_comm.cpp (bulk_write_cb)와 공유
  







void *hvac_data_mover_fn(void *args)
{
  queue<string> local_list;

  if (getenv("BBPATH") == NULL)
  {
    L4C_ERR("Set BBPATH Prior to using HVAC");
  }

  string nvmepath = string(getenv("BBPATH")) + "/XXXXXX";
  L4C_INFO("BBpath: %s", getenv("BBPATH"));

  while (1)
  {
    pthread_mutex_lock(&data_mutex);

    while (data_queue.empty())
    {
      pthread_cond_wait(&data_cond, &data_mutex);
    }
    /* We can do stuff here when signaled */
    while (!data_queue.empty())
    {
      local_list.push(data_queue.front());
      data_queue.pop();
    }

    pthread_mutex_unlock(&data_mutex);

    while (!local_list.empty())
    {
      string entry = local_list.front();

      bool is_write = (entry.compare(0, WRITE_PREFIX.length(), WRITE_PREFIX) == 0);
      string path = is_write ? entry.substr(WRITE_PREFIX.length()) : entry;

      try
      {
        if (is_write)
        {
          filesystem::path filepath = path;

          pthread_mutex_lock(&path_map_mutex);
          string local_path = path_cache_map[path];
          pthread_mutex_unlock(&path_map_mutex);
          fs::copy(local_path, path);
          L4C_INFO("Succeeded to copy %s to PFS", path.c_str());
        }
        else
        {
          char *newdir = (char *)malloc(strlen(nvmepath.c_str()) + 1);
          strcpy(newdir, nvmepath.c_str());
          mkdtemp(newdir);
          string dirpath = newdir;
          free(newdir);
          string filename = dirpath + string("/") + fs::path(path).filename().string();
          fs::copy(path, filename);
          L4C_INFO("Succeeded to copy %s to %s\n", local_list.front().c_str(), filename.c_str());

          pthread_mutex_lock(&path_map_mutex); // sy: add
          path_cache_map[path] = filename;
          pthread_mutex_unlock(&path_map_mutex); // sy: add
        }
      }
      catch (...)
      {
        L4C_INFO("Failed to copy %s\n", local_list.front().c_str(), errno);
        perror("Copy error:");
      }

      local_list.pop();
    }
  }
  return NULL;
}






void* hvac_flush_fn(void *args)
{

  queue <struct flush_entry> local_list;  
  while (true)
  {
    pthread_mutex_lock(&flush_mutex);     
    while (flush_queue.empty())
    {
        pthread_cond_wait(&flush_cond, &flush_mutex); 
    }

    while(!flush_queue.empty())
    {
      local_list.push(flush_queue.front());
      flush_queue.pop(); 
    }

    pthread_mutex_unlock(&flush_mutex); 

    while (!local_list.empty())
    {
        struct flush_entry entry = local_list.front();
        local_list.pop(); 

        L4C_INFO("PFS Flusher: %d %d %lld", entry.op, entry.fd, entry.size); 

        if (entry.op == CLOSE_OP)
        {
          close(entry.fd); 

          // Debug: RPC 전송 전 clock() 시간 측정 
          auto end = std::chrono::high_resolution_clock::now();   
          auto deserializedStart = std::chrono::high_resolution_clock::time_point(std::chrono::nanoseconds(start_time)); 
          auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end-deserializedStart);  
          auto duration = latency.count(); 
          L4C_INFO("Flush duration: %lld", duration); 

          continue;
        }
        
        // Logic for Checkpoint write 
        ssize_t bytes_written = write(entry.fd, entry.buf, entry.size); 
        if (bytes_written == -1)
        {
          L4C_FATAL("write failed on pfs flush function"); 
        }
        else
        {          
          // free(entry.buf); 
        }
    }
  }
  
}
