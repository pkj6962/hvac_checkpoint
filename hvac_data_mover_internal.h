#ifndef __HVAC_DATA_MOVER_INTERNAL_H__
#define __HVAC_DATA_MOVER_INTERNAL_H__

#include <queue>
#include <map>

#define WRITE_OP 1
#define CLOSE_OP 2

using namespace std;
/*Data Mover */

extern pthread_cond_t data_cond;
extern pthread_mutex_t data_mutex;
extern pthread_mutex_t path_map_mutex;
extern queue<string> data_queue;
extern map<int, string> fd_to_path;
extern map<string, string> path_cache_map;
/* Prefix to identify write operations */
extern string WRITE_PREFIX;

extern pthread_cond_t flush_cond; 
extern pthread_mutex_t flush_mutex;


extern int64_t start_time; 

// Data structure for background PFS Flush. 
extern queue <struct flush_entry> flush_queue; // hvac_data_mover.cpp 및 hvac_comm.cpp (bulk_write_cb)와 공유
struct flush_entry
{
  int op;         // open, write, close --> 
  int fd;         // filename or GlobalFD
  void * buf; 
  uint64_t size; 
  uint64_t offset;
// }flush_entry; 
}; 

// extern struct flush_entry flush_entry; 




  





void *hvac_data_mover_fn(void *args);
void *hvac_flush_fn(void *args); 






#endif
