//Starting to use CPP functionality


#include <map>
#include <string>
#include <filesystem>
#include <iostream>
#include <assert.h>

#include "hvac_internal.h"
#include "hvac_logging.h"
#include "hvac_comm.h"


#define HVAC_CLIENT 1
__thread bool tl_disable_redirect = false;
bool g_disable_redirect = true;
bool g_hvac_initialized = false;
bool g_hvac_comm_initialized = false;
bool g_mercury_init=false;


uint32_t g_hvac_server_count = 0;
char *hvac_data_dir = NULL;

pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;

std::map<int,std::string> fd_map;
std::map<int, int > fd_redir_map;

/* Devise a way to safely call this and initialize early */
static void __attribute__((constructor)) hvac_client_init()
{	
    pthread_mutex_lock(&init_mutex);
    if (g_hvac_initialized){
        pthread_mutex_unlock(&init_mutex);
        return;
    }
    hvac_init_logging();

    char * rank_str = getenv("PMI_RANK"); 
    int client_rank = atoi(rank_str); 
	L4C_INFO("client rank: %d", client_rank);


    char * hvac_data_dir_c = getenv("HVAC_DATA_DIR");

    if (getenv("HVAC_SERVER_COUNT") != NULL)
    {
        g_hvac_server_count = atoi(getenv("HVAC_SERVER_COUNT"));
	    L4C_INFO("hvac_server_count: %d", g_hvac_server_count);
    }
    else
    {        
        L4C_FATAL("Please set enviroment variable HVAC_SERVER_COUNT\n");
        exit(-1);
    }


    if (hvac_data_dir_c != NULL)
    {
		hvac_data_dir = (char *)malloc(strlen(hvac_data_dir_c) + 1);
		snprintf(hvac_data_dir, strlen(hvac_data_dir_c) + 1, "%s", hvac_data_dir_c);
    }
    

    g_hvac_initialized = true;
    pthread_mutex_unlock(&init_mutex);
    
    g_disable_redirect = false;
}

static void __attribute((destructor)) hvac_client_shutdown()
{
    hvac_shutdown_comm();
}

// It is only called on OPEN wrappers.
// FD is stored in FD_MAP when the file PATH is in HVAC_DATA_DIR.
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


	// Send RPC to tell server to open file 
	if (tracked){
		if (!g_mercury_init){

            char * rank_str = getenv("PMI_RANK"); 
            if (rank_str == NULL)
                L4C_INFO("Rank Before init: NULL");
            else
            {
                int client_rank = atoi(rank_str); 
                L4C_INFO("Rank Before init: %d", client_rank);
            }
			hvac_init_comm(false);	

            rank_str = getenv("PMI_RANK"); 
            if (rank_str == NULL)
                L4C_INFO("Rank after init: NULL");
            else
            {
                int client_rank = atoi(rank_str); 
                L4C_INFO("Rank after init: %d", client_rank);
            }




			/* I think I only need to do this once */
			hvac_client_comm_register_rpc();
			g_mercury_init = true;
		}
		
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		L4C_INFO("Remote open - Host %d", host);
		hvac_client_comm_gen_open_rpc(host, fd_map[fd], fd);
		hvac_client_block();
	}


	return tracked;
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
	if (hvac_file_tracked(fd)){
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		L4C_INFO("Remote read - Host %d", host);		
		hvac_client_comm_gen_read_rpc(host, fd, buf, count, -1);
		bytes_read = hvac_read_block();   		
		return bytes_read;
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
	if (hvac_file_tracked(fd)){
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
		L4C_INFO("Remote pread - Host %d", host);		
		hvac_client_comm_gen_read_rpc(host, fd, buf, count, offset);
		bytes_read = hvac_read_block();   	
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
		int host = std::hash<std::string>{}(fd_map[fd]) % g_hvac_server_count;	
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
		hvac_client_comm_gen_close_rpc(host, fd);             	
	}
}

bool hvac_file_tracked(int fd)
{
	return (fd_map.find(fd) != fd_map.end());
}

const char * hvac_get_path(int fd)
{	
	if (fd_map.find(fd) != fd_map.end())
	{
		return fd_map[fd].c_str();
	}
	return NULL;
}

bool hvac_remove_fd(int fd)
{
	hvac_remote_close(fd);	
	return fd_map.erase(fd);
}
