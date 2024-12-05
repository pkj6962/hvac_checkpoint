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
string WRITE_PREFIX = "HVAC_WRITE_PREFIX:";

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

      bool is_write = (entry.compare(0, strlen(WRITE_PREFIX), WRITE_PREFIX) == 0);
      string path = is_write ? entry.substr(strlen(WRITE_PREFIX)) : entry;

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
