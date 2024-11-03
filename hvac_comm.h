#ifndef __HVAC_RPC_ENGINE_INTERNAL_H__
#define __HVAC_RPC_ENGINE_INTERNAL_H__
extern "C" {
#include <mercury.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>
}

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

using namespace std;


/* struct used to carry state of overall operation across callbacks */
struct hvac_rpc_state_t_client {
    uint32_t value;
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    hg_handle_t handle;
    int local_fd; //sy: add
    int offset; //sy: add
    ssize_t *bytes_read; //sy: add
    ssize_t *byres_written; // jh: add 
    hg_bool_t *done; //sy: add
    pthread_cond_t *cond; //sy: add
    pthread_mutex_t *mutex; //sy: add
	char filepath[256]; //sy: add
	uint32_t svr_hash; //sy: add
};

// Carry CB Information for CB
struct hvac_open_state_t{
    uint32_t local_fd;
	char filepath[256];
	uint32_t svr_hash;
    hg_bool_t *done;
    pthread_cond_t *cond;
    pthread_mutex_t *mutex;
};

struct hvac_rpc_state_t_close {
    bool done;
    bool timeout;
	int local_fd;
    uint32_t host;
    hg_addr_t addr;
    std::condition_variable cond;
    std::mutex mutex;
    hg_handle_t handle;
};

// sy: add for logging
typedef struct {
    char filepath[256];
    char request[256];
    int flag;
    int client_rank;
    int server_rank;
    char expn[256];
	int n_epoch;
	int n_batch;
    struct timeval clocktime;
} log_info_t;

// sy: For detecting the failure
extern std::vector<int> timeout_counters;
extern mutex timeout_mutex;
// sy: for logging
extern hg_addr_t my_address;
extern int client_rank;

/* visible API for example RPC operation */

//RPC Open Handler
MERCURY_GEN_PROC(hvac_open_out_t, ((int32_t)(ret_status)))
MERCURY_GEN_PROC(hvac_open_in_t, ((hg_string_t)(path))((int32_t)(client_rank))((int32_t)(localfd)))

//BULK Read Handler
MERCURY_GEN_PROC(hvac_rpc_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(hvac_rpc_in_t, ((int32_t)(input_val))((hg_bulk_t)(bulk_handle))((int32_t)(accessfd))((int32_t)(localfd))((int64_t)(offset))((int32_t)(client_rank)))

//RPC Seek Handler
MERCURY_GEN_PROC(hvac_seek_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(hvac_seek_in_t, ((int32_t)(fd))((int32_t)(offset))((int32_t)(whence)))


//Close Handler input arg
MERCURY_GEN_PROC(hvac_close_in_t, ((int32_t)(fd))((int32_t)(client_rank)))

//General
void hvac_init_comm(hg_bool_t listen);
void *hvac_progress_fn(void *args);
void hvac_comm_list_addr();
void hvac_comm_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle);
void hvac_shutdown_comm();
void hvac_comm_free_addr(hg_addr_t addr);

//Retrieve the static variables
hg_class_t *hvac_comm_get_class();
hg_context_t *hvac_comm_get_context();

//Client
void hvac_client_comm_gen_seek_rpc(uint32_t svr_hash, int fd, int offset, int whence);
void hvac_client_comm_gen_read_rpc(uint32_t svr_hash, int localfd, void* buffer, ssize_t count, off_t offset, hvac_rpc_state_t_client *hvac_rpc_state_p);
void hvac_client_comm_gen_write_rpc(uint32_t svr_hash, int localfd, void* buffer, ssize_t count, off_t offset, hvac_rpc_state_t_client *hvac_rpc_state_p);
void hvac_client_comm_gen_open_rpc(uint32_t svr_hash, string path, int fd, hvac_open_state_t *hvac_open_state_p);
void hvac_client_comm_gen_close_rpc(uint32_t svr_hash, int fd, hvac_rpc_state_t_close* rpc_state);
hg_addr_t hvac_client_comm_lookup_addr(int rank);
void hvac_client_comm_register_rpc();
void hvac_client_block(uint32_t host, hg_bool_t *done, pthread_cond_t *cond, pthread_mutex_t *mutex);
ssize_t hvac_read_block(uint32_t host, hg_bool_t *done, ssize_t *bytes_read, pthread_cond_t *cond, pthread_mutex_t *mutex);
ssize_t hvac_write_block(uint32_t host, hg_bool_t *done, ssize_t *bytes_read, pthread_cond_t *cond, pthread_mutex_t *mutex);
ssize_t hvac_seek_block();

//For FT
void initialize_hash_ring(int serverCount, int vnodes);
void extract_ip_portion(const char* full_address, char* ip_portion, size_t max_len);
/*sy: function for debugging */
char *buffer_to_hex(const void *buf, size_t size);

/*sy: functions for logging */
void initialize_log(int rank, const char *type);
void logging_info(log_info_t *info, const char *type);
void hvac_get_addr();

//Mercury common RPC registration
hg_id_t hvac_rpc_register(void);
hg_id_t hvac_write_rpc_register(void);
hg_id_t hvac_open_rpc_register(void);
hg_id_t hvac_close_rpc_register(void);
hg_id_t hvac_seek_rpc_register(void);
#endif

