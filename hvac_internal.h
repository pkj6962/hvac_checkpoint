/* HVAC_Internal .h
 * 
 * These functions are seperated out in-case someone wants to use the API
 * directly. It's probably easiest to work through the close / fclose call
 * intercept though.
 * 
 */
#ifndef __HVAC_INTERNAL_H__
#define __HVAC_INTERNAL_H__

#include <stdio.h>
#include <unistd.h>
#include <stdarg.h> /* va_list, va_start, va_arg, va_end */
#include <stdlib.h>
#include <stdint.h> /* size specified data types */
#include <string.h> 
#include <stdbool.h> 
#include <fcntl.h>
#include <sys/uio.h>
#include <sys/mman.h>


// A version string.  Currently, it just gets written to the log file.
#define HVAC_VERSION "0.0.1"

#ifdef HVAC_PRELOAD

#define REAL_DECL(func,ret,args) \
    ret (*__real_ ## func)args;

#define WRAP_DECL(__name) __name

#define MAP_OR_FAIL(func) \
    if (!(__real_ ## func)) \
{ \
    __real_ ## func = dlsym(RTLD_NEXT, #func); \
    if(!(__real_ ## func)) { \
        fprintf(stderr, "hvac failed to map symbol: %s\n", #func); \
        exit(1); \
    } \
}

#else

#define REAL_DECL(func,ret,args) \
    extern ret __real_ ## func args;

#define WRAP_DECL(__name) __wrap_ ## __name

#define MAP_OR_FAIL(func)
#endif


// using namespace std; 

/* Mapper Macro
 *
 * Statically linked: This will point to __real_close() and can 
 * should only be called from __wrap_close()
 *
 * Dynamically linked: This will become a predeclared function pointer
 * that is set in MAP_OR_FAIL. 
 *
 */


#if 0

REAL_DECL(fclose, int, (FILE *fp))
extern int WRAP_DECL(fclose)(FILE *fp);

REAL_DECL(fopen, FILE *, (const char *path, const char *mode)) 
FILE *WRAP_DECL(fopen)(const char *path, const char *mode);

REAL_DECL(pwrite, ssize_t, (int fd, const void *buf, size_t count, off_t offset))
extern ssize_t WRAP_DECL(pwrite)(int fd, const void *buf, size_t count, off_t offset);




#endif

REAL_DECL(fopen, FILE *, (const char *path, const char *mode)) 
FILE *WRAP_DECL(fopen)(const char *path, const char *mode);

REAL_DECL(fopen64, FILE *, (const char *path, const char *mode)) 
FILE *WRAP_DECL(fopen64)(const char *path, const char *mode);

REAL_DECL(pread, ssize_t, (int fd, void *buf, size_t count, off_t offset))
extern ssize_t WRAP_DECL(pread)(int fd, void *buf, size_t count, off_t offset);

REAL_DECL(readv, ssize_t, (int fd, const struct iovec *iov, int iovcnt))
extern ssize_t WRAP_DECL(readv)(int fd, const struct iovec *iov, int iovcnt);


REAL_DECL(write, ssize_t, (int fd, const void *buf, size_t count))
extern ssize_t WRAP_DECL(write)(int fd, const void *buf, size_t count);

REAL_DECL(open, int, (const char *pathname, int flags, ...))
extern int WRAP_DECL(open)(const char *pathname, int flags, ...);

REAL_DECL(open64, int, (const char *pathname, int flags, ...))
extern int WRAP_DECL(open64)(const char *pathname, int flags, ...);


REAL_DECL(read, ssize_t, (int fd, void *buf, size_t count))
extern ssize_t WRAP_DECL(read)(int fd, void *buf, size_t count);

REAL_DECL(read64, ssize_t, (int fd, void *buf, size_t count))
extern ssize_t WRAP_DECL(read64)(int fd, void *buf, size_t count);

REAL_DECL(close, int, (int fd))
extern int WRAP_DECL(close)(int fd);

REAL_DECL(lseek, off_t, (int fd, off_t offset, int whence))
extern off_t WRAP_DECL(lseek)(int fd, off_t offset, int whence);

REAL_DECL(lseek64, off64_t, (int fd, off64_t offset, int whence))
extern off64_t WRAP_DECL(lseek64)(int fd, off64_t offset, int whence);

/*
REAL_DECL(mmap, void*, (void *addr, ssize_t length, int prot, int flags, int fd, off_t offset))
extern void* WRAP_DECL(mmap)(void *addr, ssize_t length, int prot, int flags, int fd, off_t offset);
*/



#if 0
REAL_DECL(fwrite, size_t, (const void *ptr, size_t size, size_t count, FILE *stream));
size_t WRAP_DECL(fwrite)(const void *ptr, size_t size, size_t count, FILE *stream);

REAL_DECL(fsync, int, (int fd));
extern int WRAP_DECL(fsync)(int fd);

REAL_DECL(fdatasync, int, (int fd));
extern int WRAP_DECL(fdatasync)(int fd);


#endif
/* HVAC Internal API */
#ifdef __cplusplus
extern "C" bool hvac_track_file(const char* path, int flags, int fd);
extern "C" const char * hvac_get_path(int fd);
extern "C" bool  hvac_remove_fd(int fd);
extern "C" ssize_t hvac_cache_write(int fd, const void *buf, size_t count);
extern "C" ssize_t hvac_remote_read(int fd, void *buf, size_t count);
extern "C" ssize_t hvac_remote_pread(int fd, void *buf, size_t count, off_t offset);
extern "C" ssize_t hvac_remote_lseek(int fd, int offset, int whence);
extern "C" void hvac_remote_close(int fd);
extern "C" bool hvac_file_tracked(int fd);

// JH added
extern "C" int hvac_extract_rank(const char* file_path); 



#endif

extern bool hvac_track_file(const char* path, int flags, int fd);
extern const char * hvac_get_path(int fd);
// extern string hvac_get_bbpath(string path); 
extern bool  hvac_remove_fd(int fd);
extern ssize_t hvac_cache_write(int fd, const void *buf, size_t count);
extern ssize_t hvac_remote_read(int fd, void *buf, size_t count);
extern ssize_t hvac_remote_pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t hvac_remote_lseek(int fd, int offset, int whence);
extern void hvac_remote_close(int fd);
extern bool hvac_file_tracked(int fd);

// // JH added
// extern int hvac_extract_rank(const char* file_path); 


#endif
