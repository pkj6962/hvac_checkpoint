#include <pthread.h>
#include <queue>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "hvac_logging.h"
#include "hvac_internal.h"
#include "hvac_write_data_mover.h"

static bool stop_write_worker_thread = false;
static pthread_t write_worker_thread;
static std::queue<ClientTask> write_task_queue;
static pthread_mutex_t write_task_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t write_task_queue_cv = PTHREAD_COND_INITIALIZER;

std::map<int, int> write_fd_redir_map;
static std::map<int, int> write_read_fd_map;

static void* worker_thread_function(void* arg)
{
    L4C_INFO("worker thread starts working");

    while (true)
    {
        pthread_mutex_lock(&write_task_queue_mutex);
        
        while (write_task_queue.empty() && !stop_write_worker_thread)
        {
            pthread_cond_wait(&write_task_queue_cv, &write_task_queue_mutex);
        }

        if (stop_write_worker_thread && write_task_queue.empty())
        {
            pthread_mutex_unlock(&write_task_queue_mutex);
            break;
        }

        ClientTask task = std::move(write_task_queue.front());
        write_task_queue.pop();
        pthread_mutex_unlock(&write_task_queue_mutex);

        // L4C_INFO("A0: worker thread: processing write task");
        switch (task.type)
        {
        case TaskType::OPEN:
            write_fd_redir_map[task.local_fd] = task.remote_fd;
            L4C_INFO("OPEN: %d - %d mapped", task.local_fd, task.remote_fd);
            break;

        case TaskType::WRITE:
        {
            // L4C_INFO("A1: worker thread: processing write task");
            auto l_read_fd = write_read_fd_map.find(task.local_fd);
            int local_read_fd;

            if (l_read_fd == write_read_fd_map.end()) 
            {
                local_read_fd = open(task.dram_file_path.c_str(), O_RDONLY); 
                write_read_fd_map[task.local_fd] = local_read_fd;
            }
            else
            {
                local_read_fd = l_read_fd->second;
            }
            if (local_read_fd == -1)
            {
                // TODO: Add error logging
                L4C_INFO("Open error on write_data_mover: %d %d %s\n", local_read_fd, errno, task.dram_file_path.c_str()); 
                break;
            }

            // L4C_INFO("A2: worker thread: processing write task");
            std::vector<char> temp_buffer(task.count); 
            ssize_t bytes_read = read(local_read_fd, temp_buffer.data(), task.count);

            if (bytes_read < 0)
            {
                // TODO: Add error logging
                break;
            }

            if (bytes_read == 0)
            {
                break;
            }
            // L4C_INFO("A3: worker thread: processing write task");
            ssize_t result = hvac_cache_write(task.local_fd, task.path_hash, temp_buffer.data(), bytes_read);
            if (result < 0)
            {
                // TODO: add error handling and logging
                break;
            }
            // L4C_INFO("A4: worker thread: processing write task");
            break;
        }
        case TaskType::CLOSE:
        {
            L4C_INFO("CLOSE TASK IS BEING HANDLED");
            auto l_read_fd = write_read_fd_map.find(task.local_fd); 
            if (l_read_fd != write_read_fd_map.end())
                close(l_read_fd->second);
            // It is done in close_gen_rpc 
            // write_fd_redir_map.erase(task.local_fd); 
            hvac_remote_close(task.local_fd); 

            break;
        }
        }
    }
    L4C_INFO("worker thread stops working");
    return nullptr;
}

void enqueue_write_task(std::string file_path, int path_hash, int local_fd, size_t count)
{
    ClientTask task;
    task.type = TaskType::WRITE;
    task.local_fd = local_fd;
    task.dram_file_path = file_path;
    task.count = count;
    task.path_hash = path_hash;

    pthread_mutex_lock(&write_task_queue_mutex);
    write_task_queue.push(std::move(task));
    pthread_mutex_unlock(&write_task_queue_mutex);

    // L4C_INFO("enqueue_write_task is called %d", write_task_queue.size());
    pthread_cond_signal(&write_task_queue_cv);
}

void enqueue_open_task(int local_fd, int remote_fd)
{
    ClientTask task;
    task.type = TaskType::OPEN;
    task.local_fd = local_fd;
    task.remote_fd = remote_fd;

    pthread_mutex_lock(&write_task_queue_mutex);
    write_task_queue.push(std::move(task));
    pthread_mutex_unlock(&write_task_queue_mutex);

    pthread_cond_signal(&write_task_queue_cv);
}

void enqueue_close_task(int local_fd)
{
    ClientTask task;
    task.type = TaskType::CLOSE;
    task.local_fd = local_fd;

    pthread_mutex_lock(&write_task_queue_mutex);
    write_task_queue.push(std::move(task));
    pthread_mutex_unlock(&write_task_queue_mutex);

    pthread_cond_signal(&write_task_queue_cv);
}

void start_background_worker()
{
    stop_write_worker_thread = false;
    pthread_create(&write_worker_thread, nullptr, worker_thread_function, nullptr);
}

void stop_background_worker()
{
    pthread_mutex_lock(&write_task_queue_mutex);
    stop_write_worker_thread = true;
    pthread_cond_broadcast(&write_task_queue_cv);
    pthread_mutex_unlock(&write_task_queue_mutex);

    pthread_join(write_worker_thread, nullptr);
}



/*
#include <thread>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "hvac_logging.h"
#include "hvac_internal.h"
#include "hvac_write_data_mover.h"

static bool stop_write_worker_thread = false;
static std::thread write_worker_thread;
static std::queue<ClientTask> write_task_queue; // 백그라운드 쓰레드 및 메인 클라이언트 루틴 간 공유자료구조
static std::mutex write_task_queue_mutex;
static std::condition_variable write_task_queue_cv;
// static std::map<int, int> write_fd_redir_map;
std::map<int, int> write_fd_redir_map;
static std::map<int, std::string> write_fd_map; // Not used 
static std::map<int, int> write_read_fd_map; // Global FD -> DRAM FD


static void worker_thread_function()
{
    L4C_INFO("worker thread starts working");
    while (true)
    {
        L4C_INFO("Z0");
        std::unique_lock<std::mutex> lock(write_task_queue_mutex);
        L4C_INFO("Z1");
        
        write_task_queue_cv.wait(lock, []
                                 { return !write_task_queue.empty() || stop_write_worker_thread; });
        L4C_INFO("Z2");
        if (stop_write_worker_thread && write_task_queue.empty())
        {
            break;
        }
        L4C_INFO("Z3");
        
        // if (write_task_queue.empty())
        //     continue; 
        ClientTask task = std::move(write_task_queue.front());
        L4C_INFO("Z4");
        write_task_queue.pop();
        lock.unlock();
        L4C_INFO("A0: worker thread: processing write task");
        switch (task.type)
        {
        case TaskType::OPEN:
        {
            write_fd_redir_map[task.local_fd] = task.remote_fd; // 글로벌 fd와 Remote Fd 매핑
            break;
        }
        case TaskType::WRITE:
        {
            L4C_INFO("A1: worker thread: processing write task");
            auto l_read_fd = write_read_fd_map.find(task.local_fd); // Global FD로부터 DRAM FD 추출
            int local_read_fd; // DRAMFS 파일 
            if (l_read_fd == write_read_fd_map.end()) // 첫번째 파일 쓰기 요청 
            {
                local_read_fd = open(task.dram_file_path.c_str(), O_RDONLY); 
                write_read_fd_map[task.local_fd] = local_read_fd; // GlobalFD와 DRAM FD 매핑 
            }

            else
                local_read_fd = l_read_fd->second; 

            if (local_read_fd == -1)
            {
                // TODO: Add error logging
                break;
            }
            L4C_INFO("A2: worker thread: processing write task");
            std::vector<char> temp_buffer(task.count); // 최적화 가능 여부 검토 필요
            ssize_t bytes_read = read(local_read_fd, temp_buffer.data(), task.count); // DRAM 파일 읽기 

            if (bytes_read < 0)
            {
                // TODO: Add error logging
                break;
            }
            if (bytes_read == 0)
            {
                // Unlikely to happen -- end of file
                break;
            }
            L4C_INFO("A3: worker thread: processing write task");
            ssize_t result = hvac_cache_write(task.local_fd, task.path_hash, temp_buffer.data(), bytes_read); // DRAM 파일 데이터를 Remote NVMe로 복사 
            if (result < 0)
            {
                // TODO: add error handling and logging
                break;
            }
            L4C_INFO("A4: worker thread: processing write task");
            break;
        }
        case TaskType::CLOSE:
        {
            auto l_read_fd = write_read_fd_map.find(task.local_fd); 
            if (l_read_fd != write_read_fd_map.end())
                close(l_read_fd->second); // DRAM 파일 닫기
            write_fd_redir_map.erase(task.local_fd);
            break;
        }
        }
    }
}

void enqueue_write_task(std::string file_path, int path_hash, int local_fd, size_t count)
{
    ClientTask task;
    task.type = TaskType::WRITE;
    task.local_fd = local_fd;
    task.dram_file_path = file_path;
    task.count = count;
    task.path_hash = path_hash;
    {
        std::lock_guard<std::mutex> lock(write_task_queue_mutex);
        write_task_queue.push(std::move(task));
    }
    L4C_INFO("enqueue_write_task is called %d", write_task_queue.size());
    write_task_queue_cv.notify_one();
}

void enqueue_open_task(int local_fd, int remote_fd)
{
    ClientTask task;
    task.type = TaskType::OPEN;
    task.local_fd = local_fd;
    task.remote_fd = remote_fd;
    {
        std::lock_guard<std::mutex> lock(write_task_queue_mutex);
        write_task_queue.push(std::move(task));
    }
    write_task_queue_cv.notify_one();
}

void enqueue_close_task(int local_fd)
{
    ClientTask task;
    task.type = TaskType::CLOSE;
    task.local_fd = local_fd;
    {
        std::lock_guard<std::mutex> lock(write_task_queue_mutex);
        write_task_queue.push(std::move(task));
    }
    write_task_queue_cv.notify_one();
}

void start_background_worker()
{
    stop_write_worker_thread = false;
    write_worker_thread = std::thread(worker_thread_function);
    // L4C_INFO("background thread starts working");

}

void stop_background_worker()
{
    std::unique_lock<std::mutex> lock(write_task_queue_mutex);
    stop_write_worker_thread = true;
    write_task_queue_cv.notify_all();
    write_worker_thread.join();
}
*/