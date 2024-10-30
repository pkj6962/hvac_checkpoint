#include "hvac_comm.h"
#include "hvac_data_mover_internal.h"

extern "C" {
#include "hvac_logging.h"
#include <fcntl.h>
#include <cassert>
#include <unistd.h>
}


#include <string>
#include <iostream>
#include <map>	

static hg_class_t *hg_class = NULL;
static hg_context_t *hg_context = NULL;
static int hvac_progress_thread_shutdown_flags = 0;
static int hvac_server_rank = -1;
static int server_rank = -1;

char server_addr_str[128]; 

/* struct used to carry state of overall operation across callbacks */
struct hvac_rpc_state {
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    hg_handle_t handle;
    hvac_rpc_in_t in;
};

// sy: add - Extract IP address for node checking
void extract_ip_portion(const char* full_address, char* ip_portion, size_t max_len) {
    const char* pos = strchr(full_address, ':');
    if (pos) {
        pos = strchr(pos + 1, ':');
    }
    if (pos) {
        size_t len = pos - full_address + 1;
        if (len < max_len) {
            strncpy(ip_portion, full_address, len);
            ip_portion[len] = '\0';
        } else {
            strncpy(ip_portion, full_address, max_len - 1);
            ip_portion[max_len - 1] = '\0';
        }
    } else {
        strncpy(ip_portion, full_address, max_len - 1);
        ip_portion[max_len - 1] = '\0';
    }
}


// sy: add - logging function
void initialize_log(int rank, const char *type) {
    char log_filename[64];
//    snprintf(log_filename, sizeof(log_filename), "%s_node_%d.log", type, rank);
	const char *logdir = getenv("HVAC_LOG_DIR");
    snprintf(log_filename, sizeof(log_filename), "%s/%s_node_%d.log", logdir, type, rank);

    FILE *log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
        perror("Failed to create log file");
        exit(EXIT_FAILURE);
    }

    fprintf(log_file, "Log file for %s rank %d\n", type, rank);
    fclose(log_file);
}

// sy: add - logging function
void logging_info(log_info_t *info, const char *type) {
    FILE *log_file;
    char log_filename[64];
	const char *logdir = getenv("HVAC_LOG_DIR");
	snprintf(log_filename, sizeof(log_filename), "%s/%s_node_%d.log", logdir, type, info->server_rank);

//    snprintf(log_filename, sizeof(log_flename), "%s_node_%d.log", type, info->server_rank);

    log_file = fopen(log_filename, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return;
    }

    fprintf(log_file, "[%s][%s][%d][%d][%d][%s][%d][%d][%ld.%06ld]\n",
            info->filepath,
            info->request,
            info->flag,
            info->client_rank,
            info->server_rank,
            info->expn,
			info->n_epoch,
			info->n_batch,
            (long)info->clocktime.tv_sec, (long)info->clocktime.tv_usec);

    fclose(log_file);
}



//Initialize communication for both the client and server
//processes
//This is based on the rpc_engine template provided by the mercury lib
void hvac_init_comm(hg_bool_t listen)
{
	const char *info_string = "ofi+verbs://";  
//	char *rank_str = getenv("PMI_RANK");  
//    server_rank = atoi(rank_str);
    pthread_t hvac_progress_tid;

    HG_Set_log_level("DEBUG");

    /* Initialize Mercury with the desired network abstraction class */
    hg_class = HG_Init(info_string, listen);
	if (hg_class == NULL){
		L4C_FATAL("Failed to initialize HG_CLASS Listen Mode : %d\n", listen);
	}

    /* Create HG context */
    hg_context = HG_Context_create(hg_class);
	if (hg_context == NULL){
		L4C_FATAL("Failed to initialize HG_CONTEXT\n");
	}

	//Only for server processes
	if (listen)
	{
		char *rank_str = getenv("PMI_RANK");  
    	server_rank = atoi(rank_str);
        L4C_DEBUG("rank_str: %s", rank_str);
		if (rank_str != NULL){
			hvac_server_rank = atoi(rank_str);
			const char *type = "server";
			//initialize_log(hvac_server_rank, type);
		}else
		{
			L4C_FATAL("Failed to extract rank\n");
		}
	}

	L4C_INFO("Mecury initialized");
	//TODO The engine creates a pthread here to do the listening and progress work
	//I need to understand this better I don't want to create unecessary work for the client
	if (pthread_create(&hvac_progress_tid, NULL, hvac_progress_fn, NULL) != 0){
		L4C_FATAL("Failed to initialized mecury progress thread\n");
	}
}

void hvac_shutdown_comm()
{
    int ret = -1;

    hvac_progress_thread_shutdown_flags = true;

	if (hg_context == NULL)
		return;

//    ret = HG_Context_destroy(hg_context);
//    assert(ret == HG_SUCCESS);

//    ret = HG_Finalize(hg_class);
//    assert(ret == HG_SUCCESS);


}

void *hvac_progress_fn(void *args)
{
	hg_return_t ret;
	unsigned int actual_count = 0;

	while (!hvac_progress_thread_shutdown_flags){
		do{
			ret = HG_Trigger(hg_context, 0, 1, &actual_count);
		} while (
			(ret == HG_SUCCESS) && actual_count && !hvac_progress_thread_shutdown_flags);
		if (!hvac_progress_thread_shutdown_flags)
			HG_Progress(hg_context,100);
	}
	
	return NULL;
}

/* I think only servers need to post their addresses. */
/* There is an expectation that the server will be started in 
 * advance of the clients. Should the servers be started with an
 * argument regarding the number of servers? */
void hvac_comm_list_addr()
{
	char self_addr_string[PATH_MAX];
	char filename[PATH_MAX];
    hg_addr_t self_addr;
	FILE *na_config = NULL;
	hg_size_t self_addr_string_size = PATH_MAX;
//	char *stepid = getenv("PMIX_NAMESPACE");
	char *jobid = getenv("SLURM_JOBID");
	//char *jobid = getenv("MY_JOBID");

	sprintf(filename, "./.ports.cfg.%s", jobid);
	/* Get self addr to tell client about */
    HG_Addr_self(hg_class, &self_addr);
    HG_Addr_to_string(
        hg_class, self_addr_string, &self_addr_string_size, self_addr);
    HG_Addr_free(hg_class, self_addr);
    
	extract_ip_portion(self_addr_string, server_addr_str, sizeof(server_addr_str));

    /* Write addr to a file */
    na_config = fopen(filename, "a+");
    if (!na_config) {
        L4C_ERR("Could not open config file from: %s\n",
            filename);
        exit(0);
    }
    fprintf(na_config, "%d %s\n", hvac_server_rank, self_addr_string);
    fclose(na_config);
}


char *buffer_to_hex(const void *buf, size_t size) {
    const char *hex_digits = "0123456789ABCDEF";
    const unsigned char *buffer = (const unsigned char *)buf;

    char *hex_str = (char *)malloc(size * 2 + 1); // 2 hex chars per byte + null terminator
    if (!hex_str) {
        perror("malloc");
        return NULL;
    }
    for (size_t i = 0; i < size; ++i) {
        hex_str[i * 2] = hex_digits[(buffer[i] >> 4) & 0xF];
        hex_str[i * 2 + 1] = hex_digits[buffer[i] & 0xF];
    }
    hex_str[size * 2] = '\0'; // Null terminator
    return hex_str;
}

/* callback triggered upon completion of bulk transfer */
static hg_return_t
hvac_rpc_handler_bulk_cb(const struct hg_cb_info *info)
{
    struct hvac_rpc_state *hvac_rpc_state_p = (struct hvac_rpc_state*)info->arg;
    int ret;
    hvac_rpc_out_t out;
    out.ret = hvac_rpc_state_p->size;
//	L4C_INFO("out.ret server %d\n", out.ret);
//    assert(info->ret == 0);

	if (info->ret != 0) {
        L4C_DEBUG("Callback info contains an error: %d\n", info->ret);
        // Free resources and return the error
        HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
        HG_Destroy(hvac_rpc_state_p->handle);
        free(hvac_rpc_state_p->buffer);
        free(hvac_rpc_state_p);
        return (hg_return_t)info->ret;
    }

// sy: commented
	
//	 char *hex_buf = buffer_to_hex(hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
  //          if (hex_buf) {
    //            L4C_INFO("Buffer content before rpc transfer: %s", hex_buf);
      //          free(hex_buf);
        //    }
    ret = HG_Respond(hvac_rpc_state_p->handle, NULL, NULL, &out);
//    assert(ret == HG_SUCCESS);        

	if (ret != HG_SUCCESS) {
        L4C_DEBUG("Failed to send response: %d\n", ret);
        // Free resources and return the error
        HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
        HG_Destroy(hvac_rpc_state_p->handle);
        free(hvac_rpc_state_p->buffer);
        free(hvac_rpc_state_p);
        return (hg_return_t)ret;
    }

//	char *hex_buff = buffer_to_hex(hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
  //          if (hex_buff) {
    //            L4C_INFO("Buffer content after rpc transfer: %s", hex_buff);
      //          free(hex_buff);
        //    }

    HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
//	L4C_INFO("Info Server: Freeing Bulk Handle\n");
    HG_Destroy(hvac_rpc_state_p->handle);
    free(hvac_rpc_state_p->buffer);
    free(hvac_rpc_state_p);
    return (hg_return_t)0;
}


static hg_return_t
hvac_rpc_handler(hg_handle_t handle)
{
    int ret;
    struct hvac_rpc_state *hvac_rpc_state_p;
    const struct hg_info *hgi;
    ssize_t readbytes;
	log_info_t log_info;
	struct timeval tmp_time; 

    hvac_rpc_state_p = (struct hvac_rpc_state*)malloc(sizeof(*hvac_rpc_state_p));

    /* decode input */
    ret = HG_Get_input(handle, &hvac_rpc_state_p->in);   
	if (ret != HG_SUCCESS) {
        L4C_DEBUG("HG_Get_input failed with error code %d\n", ret);
        free(hvac_rpc_state_p);
        return (hg_return_t)ret;
    }
    gettimeofday(&log_info.clocktime, NULL);
    
    /* This includes allocating a target buffer for bulk transfer */
    hvac_rpc_state_p->buffer = calloc(1, hvac_rpc_state_p->in.input_val);
    assert(hvac_rpc_state_p->buffer);

    hvac_rpc_state_p->size = hvac_rpc_state_p->in.input_val;
    hvac_rpc_state_p->handle = handle;
    
    /* register local target buffer for bulk access */

    hgi = HG_Get_info(handle);
 //   assert(hgi);
	if (!hgi) {
        L4C_DEBUG("HG_Get_info failed\n");
        return (hg_return_t)ret;
    }
    ret = HG_Bulk_create(hgi->hg_class, 1, &hvac_rpc_state_p->buffer,
        &hvac_rpc_state_p->size, HG_BULK_READ_ONLY,
        &hvac_rpc_state_p->bulk_handle);
    assert(ret == 0);
    
	// sy: add - logging code*/
    /*
    snprintf(log_info.filepath, sizeof(log_info.filepath), "fd_%d", hvac_rpc_state_p->in.localfd); 
    log_info.filepath[sizeof(log_info.filepath) - 1] = '\0';
    strncpy(log_info.request, "read", sizeof(log_info.request) - 1);
    log_info.request[sizeof(log_info.request) - 1] = '\0';

//    hg_bool_t cmp_result = HG_Addr_cmp(hvac_comm_get_class(), hgi->addr, server_address);
	char client_addr_str[128];
    size_t client_addr_str_size = sizeof(client_addr_str);
    ret = HG_Addr_to_string(hvac_comm_get_class(), client_addr_str, &client_addr_str_size, hgi->addr);
	char client_ip[128];
	extract_ip_portion(client_addr_str, client_ip, sizeof(client_ip));

	log_info.flag = (strcmp(server_addr_str, client_ip) == 0) ? 1 : 0;

    log_info.client_rank = hvac_rpc_state_p->in.client_rank;
    log_info.server_rank = server_rank;
    strncpy(log_info.expn, "SReceive", sizeof(log_info.expn) - 1);
    log_info.expn[sizeof(log_info.expn) - 1] = '\0';
    log_info.n_epoch = -1;
    log_info.n_batch = -1;
    logging_info(&log_info, "server");
    */
	hvac_rpc_out_t out;

    if (hvac_rpc_state_p->in.offset == -1){
        readbytes = read(hvac_rpc_state_p->in.accessfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
        if (readbytes == -1)
        {
            L4C_INFO("errno: %d", errno); 
        }

        string filename = "/proc/self/fd/" + to_string(in.accessfd); 
        // L4C_INFO("Server Rank %d : Read %ld bytes from file %s, fd: %d", server_rank,readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str(), hvac_rpc_state_p->in.accessfd);
        L4C_INFO("Server Rank %d : Read %ld bytes from file %s, fd: %d", server_rank,readbytes, filename, hvac_rpc_state_p->in.accessfd);
        
/*
		if (readbytes < 0) {
            readbytes = read(hvac_rpc_state_p->in.localfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
            L4C_DEBUG("Server Rank %d : Retry Read %ld bytes from file %s at offset %ld", server_rank, readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str(), hvac_rpc_state_p->in.offset);
		}
*/
    }else
    {
        
		//gettimeofday(&log_info.clocktime, NULL);
		//strncpy(log_info.expn, "SSNVMeRequest", sizeof(log_info.expn) - 1);
    	//log_info.expn[sizeof(log_info.expn) - 1] = '\0';
        readbytes = pread(hvac_rpc_state_p->in.accessfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size, hvac_rpc_state_p->in.offset);
		//gettimeofday(&tmp_time, NULL);	
        
        L4C_INFO("Server Rank %d : PRead %ld bytes from file %s at offset %ld", server_rank, readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str(),hvac_rpc_state_p->in.offset );
/*
		 char *hex_buf = buffer_to_hex(hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
            if (hex_buf) {
                L4C_INFO("Buffer content after remote read: %s", hex_buf);
                free(hex_buf);
            }
*/	
		if (readbytes < 0) { //sy: add
			strncpy(log_info.expn, "Fail", sizeof(log_info.expn) - 1);
            log_info.expn[sizeof(log_info.expn) - 1] = '\0';
            // logging_info(&log_info, "server");
/*
        const char* original_path = fd_to_path[hvac_rpc_state_p->in.accessfd].c_str();
        int original_fd = open(original_path, O_RDONLY);
        if (original_fd != -1) {
            readbytes = pread(original_fd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size, hvac_rpc_state_p->in.offset);
            L4C_DEBUG("Server Rank %d : Retry PRead %ld bytes from file %s at offset %ld", server_rank, readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str(), hvac_rpc_state_p->in.offset);
            close(original_fd);
        } 
		else {
			readbytes = pread(hvac_rpc_state_p->in.localfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size, hvac_rpc_state_p->in.offset);
            if(readbytes<0){
				L4C_DEBUG("Server Rank %d : Failed to open original file %s", server_rank, original_path);
			}
        }
*/
//		if(readbytes<0){
//			L4C_DEBUG("Server Rank %d : Failed to open original file %s", server_rank, original_path);
                HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
                free(hvac_rpc_state_p->buffer);
                L4C_DEBUG("server read failed -1\n");
                out.ret = -1;  // Indicate failure
                HG_Respond(handle, NULL, NULL, &out);
                free(hvac_rpc_state_p);
                return HG_SUCCESS;
//		}

    	}
	}
	
    //Reduce size of transfer to what was actually read 
    //We may need to revisit this.
    hvac_rpc_state_p->size = readbytes;
//	L4C_DEBUG("readbytes before transfer %d\n", readbytes);
    /* initiate bulk transfer from client to server */
    ret = HG_Bulk_transfer(hgi->context, hvac_rpc_handler_bulk_cb, hvac_rpc_state_p,
        HG_BULK_PUSH, hgi->addr, hvac_rpc_state_p->in.bulk_handle, 0,
        hvac_rpc_state_p->bulk_handle, 0, hvac_rpc_state_p->size, HG_OP_ID_IGNORE);
 
    assert(ret == 0);

    (void) ret;

    return (hg_return_t)ret;
}




static hg_return_t
hvac_open_rpc_handler(hg_handle_t handle)
{
    hvac_open_in_t in;
    hvac_open_out_t out;    
	const struct hg_info *hgi;
	int nvme_flag = 0;
	
    // L4C_INFO("aaa"); 
    int ret = HG_Get_input(handle, &in);
    // L4C_INFO("bbb"); 
    assert(ret == 0);
    string redir_path = in.path;

	//sy: add - for logging
	hgi = HG_Get_info(handle);
    if (!hgi) {
        L4C_DEBUG("HG_Get_info failed\n");
        return (hg_return_t)ret;
    }
	// log_info_t log_info;
    // strncpy(log_info.filepath, in.path, sizeof(log_info.filepath) - 1);
    // log_info.filepath[sizeof(log_info.filepath) - 1] = '\0';
    // strncpy(log_info.request, "open", sizeof(log_info.request) - 1);
    // log_info.request[sizeof(log_info.request) - 1] = '\0';

	// char client_addr_str[128];
    // size_t client_addr_str_size = sizeof(client_addr_str);
    // ret = HG_Addr_to_string(hvac_comm_get_class(), client_addr_str, &client_addr_str_size, hgi->addr);
    // char client_ip[128];
    // extract_ip_portion(client_addr_str, client_ip, sizeof(client_ip));

    // log_info.flag = (strcmp(server_addr_str, client_ip) == 0) ? 1 : 0;
	
    // log_info.client_rank = in.client_rank;
    // log_info.server_rank = server_rank;
    // strncpy(log_info.expn, "SReceive", sizeof(log_info.expn) - 1);
    // log_info.expn[sizeof(log_info.expn) - 1] = '\0';
    // log_info.n_epoch = in.localfd;
    // log_info.n_batch = -1;
    // gettimeofday(&log_info.clocktime, NULL);
    // // logging_info(&log_info, "server");

	// strncpy(log_info.expn, "SPFSRequest", sizeof(log_info.expn) - 1);
    // log_info.expn[sizeof(log_info.expn) - 1] = '\0';
	
	pthread_mutex_lock(&path_map_mutex); //sy: add
    // PATH_CACHE_MAP stores Path to NVMe Cache if exists. 
    if (path_cache_map.find(redir_path) != path_cache_map.end())
    {
   //     L4C_INFO("Server Rank %d : Successful Redirection %s to %s", server_rank, redir_path.c_str(), path_cache_map[redir_path].c_str());
        redir_path = path_cache_map[redir_path];
		// strncpy(log_info.expn, "SNVMeRequest", sizeof(log_info.expn) - 1);
        // log_info.expn[sizeof(log_info.expn) - 1] = '\0';
        nvme_flag = 1;
    }
	pthread_mutex_unlock(&path_map_mutex); //sy: add	
    // L4C_INFO("Server Rank %d : Successful Open %s", server_rank, in.path);    
    // L4C_INFO("Server Rank %d : Successful Open %s", server_rank, redir_path.c_str());    

    // gettimeofday(&log_info.clocktime, NULL);
    // logging_info(&log_info, "server");
    out.ret_status = open(redir_path.c_str(),O_RDONLY);  
    L4C_INFO("Server Rank %d : Successful Open %s %d", server_rank, redir_path.c_str(), out.ret_status);    

	// gettimeofday(&log_info.clocktime, NULL);
    // if (nvme_flag) {
    //     strncpy(log_info.expn, "SNVMeReceive", sizeof(log_info.expn) - 1);
    //     log_info.expn[sizeof(log_info.expn) - 1] = '\0';
    // } else {
    //     strncpy(log_info.expn, "SPFSReceive", sizeof(log_info.expn) - 1);
    //     log_info.expn[sizeof(log_info.expn) - 1] = '\0';
    // }
    // logging_info(&log_info, "server");

    fd_to_path[out.ret_status] = in.path;  
    HG_Respond(handle,NULL,NULL,&out);

    return (hg_return_t)ret;

}

static hg_return_t
hvac_close_rpc_handler(hg_handle_t handle)
{
    hvac_close_in_t in;
	const struct hg_info *hgi;
	int nvme_flag = 0;
	struct timeval tmp_time;
	log_info_t log_info;

    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);
	gettimeofday(&log_info.clocktime, NULL);
 //   L4C_INFO("Closing File %d\n",in.fd);
    ret = close(in.fd);
//    assert(ret == 0);
//	out.done = ret;
	

	// sy: add - logging code
	hgi = HG_Get_info(handle);
    if (!hgi) {
        L4C_DEBUG("HG_Get_info failed\n");
		fd_to_path.erase(in.fd);
       	return (hg_return_t)ret;
	}
	snprintf(log_info.filepath, sizeof(log_info.filepath), "fd_%d", in.fd);    
    strncpy(log_info.request, "close", sizeof(log_info.request) - 1);
    log_info.request[sizeof(log_info.request) - 1] = '\0';


	char client_addr_str[128];
    size_t client_addr_str_size = sizeof(client_addr_str);
    ret = HG_Addr_to_string(hvac_comm_get_class(), client_addr_str, &client_addr_str_size, hgi->addr);
    char client_ip[128];
    extract_ip_portion(client_addr_str, client_ip, sizeof(client_ip));

    log_info.flag = (strcmp(server_addr_str, client_ip) == 0) ? 1 : 0;

    log_info.client_rank = in.client_rank;
    log_info.server_rank = server_rank;
    strncpy(log_info.expn, "SReceive", sizeof(log_info.expn) - 1);
    log_info.expn[sizeof(log_info.expn) - 1] = '\0';
    log_info.n_epoch = -1;
    log_info.n_batch = -1;
    // logging_info(&log_info, "server");

	gettimeofday(&log_info.clocktime, NULL);
	strncpy(log_info.expn, "SReceive", sizeof(log_info.expn) - 1);
    log_info.expn[sizeof(log_info.expn) - 1] = '\0';



    //Signal to the data mover to copy the file
	
	pthread_mutex_lock(&path_map_mutex); //sy: add
    if (path_cache_map.find(fd_to_path[in.fd]) == path_cache_map.end())
    {
		strncpy(log_info.expn, "SNVMeRequest", sizeof(log_info.expn) - 1);
    	log_info.expn[sizeof(log_info.expn) - 1] = '\0';
 //       L4C_INFO("Caching %s",fd_to_path[in.fd].c_str());
        pthread_mutex_lock(&data_mutex);
        data_queue.push(fd_to_path[in.fd]);
        pthread_cond_signal(&data_cond);
        pthread_mutex_unlock(&data_mutex);
		nvme_flag = 1;
    }
	pthread_mutex_unlock(&path_map_mutex); //sy: add
	if (nvme_flag) {
		// logging_info(&log_info, "server");
		strncpy(log_info.expn, "SNVMeReceive", sizeof(log_info.expn) - 1);
        log_info.expn[sizeof(log_info.expn) - 1] = '\0';
	}		
	else{
		strncpy(log_info.expn, "SPFSRequest", sizeof(log_info.expn) - 1);
        log_info.expn[sizeof(log_info.expn) - 1] = '\0';
		// logging_info(&log_info, "server");
        strncpy(log_info.expn, "SPFSReceive", sizeof(log_info.expn) - 1);
        log_info.expn[sizeof(log_info.expn) - 1] = '\0';

	}	
	log_info.clocktime = tmp_time;
    // logging_info(&log_info, "server");	
	
	fd_to_path.erase(in.fd);
    return (hg_return_t)ret;
}

static hg_return_t
hvac_seek_rpc_handler(hg_handle_t handle)
{
    hvac_seek_in_t in;
    hvac_seek_out_t out;    
    int ret = HG_Get_input(handle, &in);
    assert(ret == 0);

    out.ret = lseek64(in.fd, in.offset, in.whence);

    HG_Respond(handle,NULL,NULL,&out);

    return (hg_return_t)ret;
}


/* register this particular rpc type with Mercury */
hg_id_t
hvac_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_base_rpc", hvac_rpc_in_t, hvac_rpc_out_t, hvac_rpc_handler);

    return tmp;
}

hg_id_t
hvac_open_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_open_rpc", hvac_open_in_t, hvac_open_out_t, hvac_open_rpc_handler);

    return tmp;
}

hg_id_t
hvac_close_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_close_rpc", hvac_close_in_t, void, hvac_close_rpc_handler);
    

    int ret =  HG_Registered_disable_response(hg_class, tmp,
                                           HG_TRUE);                        
    assert(ret == HG_SUCCESS);

    return tmp;
}

/* register this particular rpc type with Mercury */
hg_id_t
hvac_seek_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_seek_rpc", hvac_seek_in_t, hvac_seek_out_t, hvac_seek_rpc_handler);

    return tmp;
}

/* Create context even for client */
void
hvac_comm_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle)
{    
    L4C_INFO("abc"); 
    hg_return_t ret = HG_Create(hg_context, addr, id, handle);
    L4C_INFO("def"); 
    assert(ret==HG_SUCCESS);    
}

/*Free the addr */
void 
hvac_comm_free_addr(hg_addr_t addr)
{
    hg_return_t ret = HG_Addr_free(hg_class,addr);
    assert(ret==HG_SUCCESS);
}

hg_class_t *hvac_comm_get_class()
{
    return hg_class;
}

hg_context_t *hvac_comm_get_context()
{
    return hg_context;
}
