//Starting to use CPP functionality


#include <map>
#include <string>
#include <filesystem>
#include <iostream>
#include <assert.h>
#include <mutex>

#include "hvac_internal.h"
#include "hvac_logging.h"
#include "hvac_comm.h"
#include "hvac_hashing.h"

#define VIRTUAL_NODE_CNT 100

__thread bool tl_disable_redirect = false;
bool g_disable_redirect = true;
bool g_hvac_initialized = false;
bool g_hvac_comm_initialized = false;
bool g_mercury_init=false;

uint32_t g_hvac_server_count = 0;
char *hvac_data_dir = NULL;
char *hvac_checkpoint_dir = NULL; 

pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

std::map<int,std::string> fd_map;
std::map<int, int > fd_redir_map;
//sy: add
const int TIMEOUT_LIMIT = 3;
HashRing<string, string>* hashRing; // ptr to the consistent hashing object
vector<bool> failure_flags;


/* Devise a way to safely call this and initialize early */
static void __attribute__((constructor)) hvac_client_init()
{	
    pthread_mutex_lock(&init_mutex);
    if (g_hvac_initialized){
        pthread_mutex_unlock(&init_mutex);
        return;
    }
    hvac_init_logging();

    char * hvac_data_dir_c = getenv("HVAC_DATA_DIR");
	char * hvac_checkpoint_dir_c = getenv("HVAC_CHECKPOINT_DIR"); 

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


    if (hvac_data_dir_c != NULL)
    {
		hvac_data_dir = (char *)malloc(strlen(hvac_data_dir_c) + 1);
		snprintf(hvac_data_dir, strlen(hvac_data_dir_c) + 1, "%s", hvac_data_dir_c);
    }


	
	if (hvac_checkpoint_dir_c != NULL)
	{
		hvac_checkpoint_dir = (char *)malloc(strlen(hvac_checkpoint_dir_c) + 1); 
		snprintf(hvac_checkpoint_dir, strlen(hvac_checkpoint_dir_c)+1, "%s", hvac_checkpoint_dir_c); 
	}
	



    
	/* sy: add */
	initialize_hash_ring(g_hvac_server_count, VIRTUAL_NODE_CNT);
	hvac_get_addr();
    g_hvac_initialized = true;
    pthread_mutex_unlock(&init_mutex);
    
	g_disable_redirect = false;
}

static void __attribute((destructor)) hvac_client_shutdown()
{
    hvac_shutdown_comm();
	delete hashRing;
}

//sy: add. initialization function for hash ring & timeout counter
void initialize_hash_ring(int serverCount, int vnodes) {
    hashRing = new HashRing<string, string>(vnodes);
    for (int i = 1; i <= serverCount; ++i) {
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
    
    bool tracked = false;
    try {
        std::string ppath = std::filesystem::canonical(path).parent_path();
        L4C_INFO("path: %s", path); 

        // Check if the file is for reading (existing HVAC_DATA_DIR tracking)
        
        int access_mode  = flags & O_ACCMODE ; 
        L4C_INFO("mode: %d", access_mode);
        if ((flags & O_ACCMODE) == O_RDONLY) {

            if (hvac_data_dir != NULL) {
                std::string test = std::filesystem::canonical(hvac_data_dir);
                if (ppath.find(test) != std::string::npos) {
                    L4C_INFO("Tracking used HVAC_DATA_DIR file %s", path);
                    fd_map[fd] = std::fffilesystem::canonical(path);
                    tracked = true;
                }
            } else if (ppath == std::filesystem::current_path()) {
                L4C_INFO("Tracking used CWD file %s", path);
                fd_map[fd] = std::filesystem::canonical(path);
                tracked = true;
            }
        }
         //Check if the file is for writing (new HVAC_CHECKPOINT_DIR tracking)
         else if ((flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR) {
             if (hvac_checkpoint_dir != NULL) {
                 std::string test = std::filesystem::canonical(hvac_checkpoint_dir);
                 if (ppath.find(test) != std::string::npos) {
                     L4C_INFO("Tracking used HVAC_CHECKPOINT_DIR file %s", path);
                     fd_map[fd] = std::filesystem::canonical(path);
                     tracked = true;
                 }
             }
         }
    } catch (...) {
        // Handle exceptions if path canonicalization fails
        L4C_INFO("Process reached here"); 
    }
    
    hg_bool_t done = HG_FALSE;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    // Send RPC to tell the server to open the file 
    if (tracked) {
        if (!g_mercury_init) {
            // char * rank_str = getenv("PMI_RANK"); 
            // if (rank_str == NULL)
            //     L4C_INFO("Rank Before init: NULL");
            // else {
            //     client_rank = atoi(rank_str); 
            //     L4C_INFO("Rank Before init: %d", client_rank);
            // }		

            hvac_init_comm(false);	
            hvac_client_comm_register_rpc();
            g_mercury_init = true;
        }
        
		hvac_open_state_t *hvac_open_state_p = (hvac_open_state_t *)malloc(sizeof(hvac_open_state_t));
        hvac_open_state_p->done = &done;
        hvac_open_state_p->cond = &cond;
        hvac_open_state_p->mutex = &mutex;	
        int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
        L4C_INFO("Remote open - Host %d", host);
    
        hvac_client_comm_gen_open_rpc(host, fd_map[fd], fd, hvac_open_state_p);
		hvac_client_block(host, &done, &cond, &mutex);
    }

    return tracked;
}


/*
bool hvac_track_file(const char *path, int flags, int fd)
{       
	if (strstr(path, ".ports.cfg.") != NULL)
	{
		return false;
	}
	//Always back out of RDONLY
	bool tracked = false;
	if ((flags & O_ACCMODE) == O_WRONLY) {
		return false;
	}

	if ((flags & O_APPEND)) {
		return false;
	}    

	try {
		std::string ppath = std::filesystem::canonical(path).parent_path();
		// Check if current file exists in HVAC_DATA_DIR
		if (hvac_data_dir != NULL){
			std::string test = std::filesystem::canonical(hvac_data_dir);
			
			if (ppath.find(test) != std::string::npos)
			{
				//L4C_FATAL("Got a file want a stack trace");
				L4C_INFO("Traacking used HV_DD file %s",path);
				fd_map[fd] = std::filesystem::canonical(path);
				tracked = true;
			}		
		}else if (ppath == std::filesystem::current_path()) {       
			L4C_INFO("Traacking used CWD file %s",path);
			fd_map[fd] = std::filesystem::canonical(path);
			tracked = true;
		}
	} catch (...)
	{
		//Need to do something here
	}

    hg_bool_t done = HG_FALSE;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


	// Send RPC to tell server to open file 
	if (tracked){
		if (!g_mercury_init){
			hvac_init_comm(false);	
			//I think I only need to do this once 
			hvac_client_comm_register_rpc();
			g_mercury_init = true;
			// initialize_hash_ring(g_hvac_server_count, VIRTUAL_NODE_CNT);
			const char *type = "client"; 
			//const char *rank_str = getenv("HOROVOD_RANK");
			//int client_rank = atoi(rank_str);
			//initialize_log(client_rank, type);
			hvac_get_addr();
		}
		// sy: modified logic
		hvac_open_state_t *hvac_open_state_p = (hvac_open_state_t *)malloc(sizeof(hvac_open_state_t));
        hvac_open_state_p->done = &done;
        hvac_open_state_p->cond = &cond;
        hvac_open_state_p->mutex = &mutex;	
		// hvac_open_state_p->flags = flags; 
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
        //string hostname = hashRing->GetNode(fd_map[fd]);
		//int host = hashRing->ConvertHostToNumber(hostname);
//		L4C_INFO("Remote open - Host %d", host);
		//{
        //    std::lock_guard<std::mutex> lock(timeout_mutex);
			
//			L4C_INFO("host %d\n", host);
//			L4C_INFO("cnt %d\n",timeout_counters[host]);
          //  if (timeout_counters[host] >= TIMEOUT_LIMIT && !failure_flags[host]) {
          //      L4C_INFO("Host %d reached timeout limit, skipping", host);
		  //		hashRing->RemoveNode(hostname);
		//		failure_flags[host] = true;
		//		hostname = hashRing->GetNode(fd_map[fd]); // sy: Imediately directed to the new node
          //      host = hashRing->ConvertHostToNumber(hostname);
//				L4C_INFO("new host %d\n", host);
         //   }
        //}
        
		hvac_client_comm_gen_open_rpc(host, fd_map[fd], fd, hvac_open_state_p);
		hvac_client_block(host, &done, &cond, &mutex);
        
	}
    L4C_INFO("Hi! I'm readched here %d.", getpid()); 

	return tracked;
}
*/




ssize_t hvac_cache_write(int fd, const void *buf, size_t count)
{
    ssize_t bytes_written = -1;
	hg_bool_t done = HG_FALSE; 
	pthread_cond_t cond = PTHREAD_COND_INITIALIZER; 
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITALIZER; 

    if (hvac_file_tracked(fd)) 
    {
		// TODO: It should be changed so that the client sends requests to the local server. 
        // Determine which server to communicate with based on the file descriptor.

		
		const char *rank_str = getenv("PMI_RANK");
		int client_rank = atoi(rank_str);
        // TODOL What if N(clients):1(server) model in single node? 
		int host = client_rank; 
		// int host = client_rank  % NUM_NODE; // maybe we can refer to enviornment variable.  
        // int host = hash<string>{}(fd_map[fd]) % g_hvac_server_count;
        L4C_INFO("NVMe buffering(write) - Host %d", host);

		hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client*)malloc(sizeof(hvac_rpc_state_t_client)); 
		hvac_rpc_state_p->bytes_written = &bytes_written; 
		hvac_rpc_state_p->done = &done; 
		hvac_rpc_state_p->cond = &cond; 
		hvac_rpc_state_p->mutex = &mutex; 

        // Generate the write RPC request.
        // hvac_client_comm_gen_write_rpc(host, fd, buf, count, -1, hvac_rpc_state_p);

        // Wait for the server to process the write request.
        // bytes_written = hvac_write_block(host, &done, &bytes_read, &cond, &mutex);
		if(bytes_written == -1){
            fd_map.erase(fd);
        }
	}
    L4C_INFO("Client is redirected to real_write"); 
    // Non-HVAC Writes should return -1.
    return bytes_written;
}






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
	if (hvac_file_tracked(fd)){

        L4C_INFO("remote-read:a"); 
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		/*
        string hostname = hashRing->GetNode(fd_map[fd]);
        int host = hashRing->ConvertHostToNumber(hostname);
//        L4C_INFO("Remote read - Host %d", host);
        {
            std::lock_guard<std::mutex> lock(timeout_mutex);

  //          L4C_INFO("host %d\n", host);
//          L4C_INFO("cnt %d\n",timeout_counters[host]);
            if (timeout_counters[host] >= TIMEOUT_LIMIT && !failure_flags[host]) {
                L4C_INFO("Host %d reached timeout limit, skipping", host);
                hashRing->RemoveNode(hostname);
                failure_flags[host] = true;
//                hostname = hashRing->GetNode(fd_map[fd]);
//                host = hashRing->ConvertHostToNumber(hostname);
				fd_map.erase(fd);
				return bytes_read;	
            }
        }
        */
		// sy: modified logic
        hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
        hvac_rpc_state_p->bytes_read = &bytes_read;
        hvac_rpc_state_p->done = &done;
        hvac_rpc_state_p->cond = &cond;
        hvac_rpc_state_p->mutex = &mutex;

		hvac_client_comm_gen_read_rpc(host, fd, buf, count, -1, hvac_rpc_state_p);
		bytes_read = hvac_read_block(host, &done, &bytes_read, &cond, &mutex);		
		if(bytes_read == -1){
            fd_map.erase(fd);
        }
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
	if (hvac_file_tracked(fd)){
//		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		string hostname = hashRing->GetNode(fd_map[fd]);
        int host = hashRing->ConvertHostToNumber(hostname);
     //   L4C_INFO("Remote pread - Host %d", host);
        {
            std::lock_guard<std::mutex> lock(timeout_mutex);

//            L4C_INFO("host %d\n", host);
//          L4C_INFO("cnt %d\n",timeout_counters[host]);
            if (timeout_counters[host] >= TIMEOUT_LIMIT && !failure_flags[host]) {
//                L4C_INFO("Host %d reached timeout limit, skipping", host);
                hashRing->RemoveNode(hostname);
                failure_flags[host] = true;
//                hostname = hashRing->GetNode(fd_map[fd]);
//                host = hashRing->ConvertHostToNumber(hostname);
//                L4C_INFO("new host %d\n", host);
				fd_map.erase(fd);
				return bytes_read;
            }
        }
		//sy: modified logic		
        hvac_rpc_state_t_client *hvac_rpc_state_p = (hvac_rpc_state_t_client *)malloc(sizeof(hvac_rpc_state_t_client));
        hvac_rpc_state_p->bytes_read = &bytes_read;
        hvac_rpc_state_p->done = &done;
        hvac_rpc_state_p->cond = &cond;
        hvac_rpc_state_p->mutex = &mutex;

		hvac_client_comm_gen_read_rpc(host, fd, buf, count, offset, hvac_rpc_state_p);
		bytes_read = hvac_read_block(host, &done, &bytes_read, &cond, &mutex);   	
		if(bytes_read == -1){
			fd_map.erase(fd);
		}
	}
	/* Non-HVAC Reads come from base */
	return bytes_read;
}

ssize_t hvac_remote_lseek(int fd, int offset, int whence)
{
		/* HVAC Code */
	/* Check the local fd - if it's tracked we pass it to the RPC function
	 * The local FD is converted to the remote FD with the buf and count
	 * We must know the remote FD to avoid collision on the remote side
	 */
	ssize_t bytes_read = -1;
	if (hvac_file_tracked(fd)){
//		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		string hostname = hashRing->GetNode(fd_map[fd]);
        int host = hashRing->ConvertHostToNumber(hostname);
		L4C_INFO("Remote seek - Host %d", host);		
		hvac_client_comm_gen_seek_rpc(host, fd, offset, whence);
		bytes_read = hvac_seek_block();   		
		return bytes_read;
	}
	/* Non-HVAC Reads come from base */
	return bytes_read;
}

void hvac_remote_close(int fd){
	if (hvac_file_tracked(fd)){
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		/*
        string hostname = hashRing->GetNode(fd_map[fd]);
        host = hashRing->ConvertHostToNumber(hostname);
        {
            std::lock_guard<std::mutex> lock(timeout_mutex);
            if (timeout_counters[host] >= TIMEOUT_LIMIT && !failure_flags[host]) {
                L4C_INFO("Host %d reached timeout limit, skipping", host);
                hashRing->RemoveNode(hostname);
                failure_flags[host] = true;
				return; // sy: skip further processing for this node
            }
        }
        */
		// sy: add
		hvac_rpc_state_t_close *rpc_state = (hvac_rpc_state_t_close *)malloc(sizeof(hvac_rpc_state_t_close));
    	rpc_state->done = false;
    	rpc_state->timeout = false;
		rpc_state->host = 0;
		hvac_client_comm_gen_close_rpc(host, fd, rpc_state);             	
	}
}

bool hvac_file_tracked(int fd)
{
	if (fd_map.empty()) { //sy: add
        return false;  
    }
	return (fd_map.find(fd) != fd_map.end());
}

const char * hvac_get_path(int fd)
{
    
    string path = "/proc/self/fd/" +  to_string(fd); 
    char filepath[256]; 
    ssize_t len = readlink(path.c_str(), filepath, sizeof(filepath)-1); 
    filepath[len] = '\0'; 

    L4C_INFO("fd on HVAC_GET_PATH: %d %s\n", fd, filepath); 
	if (fd_map.empty()) { //sy: add
        return NULL;
    }
	
	if (fd_map.find(fd) != fd_map.end())
	{
		return fd_map[fd].c_str();
	}
	return NULL;
}

bool hvac_remove_fd(int fd)
{	
	if (fd_map.empty()){ //sy: add
		return false;
	}
	hvac_remote_close(fd);	
	return fd_map.erase(fd);
}
