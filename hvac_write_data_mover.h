#ifndef WRITE_WORKER_H
#define WRITE_WORKER_H

#include <string>
#include <sys/types.h>
#include <cstddef>

enum class TaskType
{
    OPEN,
    WRITE,
    // LSEEK,
    CLOSE
};

struct ClientTask
{
    TaskType type;
    int remote_fd;
    int local_fd;
    std::string dram_file_path;
    int path_hash;
    size_t count;
};

void enqueue_write_task(std::string file_path, int path_hash, int local_fd, size_t count);

void enqueue_open_task(int local_fd, int remote_fd);

void enqueue_close_task(int local_fd);

void start_background_worker();

void stop_background_worker();

#endif
