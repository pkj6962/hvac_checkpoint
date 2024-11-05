/* Data mover responsible for maintaining the NVMe state
 * and prefetching the data
 */
#include <filesystem>
#include <string>
#include <queue>
#include <iostream>

#include <pthread.h>
#include <string.h>

#include "hvac_logging.h"
#include "hvac_data_mover_internal.h"
using namespace std;
namespace fs = std::filesystem;

pthread_cond_t data_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t path_map_mutex = PTHREAD_MUTEX_INITIALIZER;

map<int, string> fd_to_path;
map<string, string> path_cache_map;
queue<string> data_queue;

void *hvac_data_mover_fn(void *args)
{
  queue<string> local_list;

  if (getenv("BBPATH") == NULL)
  {
    L4C_ERR("Set BBPATH Prior to using HVAC");
  }

  string nvmepath = string(getenv("BBPATH")) + "/XXXXXX";

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

    /* Now we copy the local list to the NVMes*/
    while (!local_list.empty())
    {
      char *newdir = (char *)malloc(strlen(nvmepath.c_str()) + 1);
      strcpy(newdir, nvmepath.c_str());
      mkdtemp(newdir);
      string dirpath = newdir;
      string filename = dirpath + string("/") + fs::path(local_list.front().c_str()).filename().string();

      try
      {
        L4C_INFO("Data mover a");
        fs::copy(local_list.front(), filename);
        L4C_INFO("Data mover b");
        pthread_mutex_lock(&path_map_mutex); // sy: add
        L4C_INFO("Data mover c");
        path_cache_map[local_list.front()] = filename;
        L4C_INFO("Data mover d");
        pthread_mutex_unlock(&path_map_mutex); // sy: add
      }
      catch (...)
      {
        L4C_INFO("Failed to copy %s to %s\n", local_list.front().c_str(), filename);
      }
      local_list.pop();
    }
  }
  return NULL;
}
