/*****************************************************************************
 * Author:     Christopher J. Zimmer
 *             Oak Ridge National Lab
 * Date:       11/11/2020
 * Purpose:    Functions & structures for intercepting I/O calls for caching.
 *
 * Updated:    01/08/2021 - Purpose moved to HVAC modified to build without 
 *             warning under C++ : Skeleton for HVAC Built and configured
 * 
 * Copyright 2020 UT Battelle, LLC
 *
 * This work was supported by the Oak Ridge Leadership Computing Facility at
 * the Oak Ridge National Laboratory, which is managed by UT Battelle, LLC for
 * the U.S. DOE (under the contract No. DE-AC05-00OR22725).
 *
 * This file is part of the HVAC project.
 ****************************************************************************/

#include <dlfcn.h>
#include <string.h>
#include <assert.h>

#include "hvac_internal.h"
#include "hvac_logging.h"
#include "execinfo.h"

// Global symbol that will "turn off" all I/O redirection.  Set during init
// and shutdown to prevent us from getting into init loops that cause a
// segfault. (ie: fopen() calls hvac_init()) which needs to write a log
// message, so it calls fopen()...)
extern bool g_disable_redirect;

// Thread-local symbol for disabling redirects.  Used by the L4C_* macros
// to make sure I/O to our log files doesn't get redirected.
extern __thread bool tl_disable_redirect;



/* fopen wrapper */
FILE *WRAP_DECL(fopen)(const char *path, const char *mode)
{

	MAP_OR_FAIL(fopen);
	if (g_disable_redirect || tl_disable_redirect) return __real_fopen( path, mode);

	FILE *ptr = __real_fopen(path,mode);

	if (ptr != NULL)
	{
		if (hvac_track_file(path, O_RDONLY, fileno(ptr)))
		{
			L4C_INFO("FOpen: Tracking File %s",path);
		}
	}	
	
	return ptr;
}


/* fopen wrapper */
FILE *WRAP_DECL(fopen64)(const char *path, const char *mode)
{

	MAP_OR_FAIL(fopen64);
	if (g_disable_redirect || tl_disable_redirect) return __real_fopen64( path, mode);

	FILE *ptr = __real_fopen64(path,mode);

	if (ptr != NULL)
	{
		if (hvac_track_file(path, O_RDONLY, fileno(ptr)))
		{
			L4C_INFO("FOpen64: Tracking File %s",path);
		}
	}	
	
	return ptr;
}

int WRAP_DECL(open)(const char *pathname, int flags, ...)
{
	int ret = 0;
	va_list ap;
	int mode = 0;


	if (flags & O_CREAT)
	{
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

	MAP_OR_FAIL(open);
	if (g_disable_redirect || tl_disable_redirect) return __real_open(pathname, flags, mode);

	/* For now pass the open to GPFS  - I think the open is cheap
	 * possibly asychronous.
	 * If this impedes performance we can investigate a cheap way of generating
	 * an FD
	 */
	ret = __real_open(pathname, flags, mode);

	// C++ code determines whether to track
	if (ret != -1){
		if (hvac_track_file(pathname, flags, ret))
		{
			L4C_INFO("Open: Tracking File %s",pathname);
		}
	}
	
	return ret;
}

int WRAP_DECL(open64)(const char *pathname, int flags, ...)
{
	int ret = 0;
	va_list ap;
	int mode = 0;


	if (flags & O_CREAT)
	{
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}


	MAP_OR_FAIL(open64);
	if (g_disable_redirect || tl_disable_redirect) return __real_open64(pathname, flags, mode);	


	if (mode)
	{
		ret = __real_open64(pathname, flags, mode);
	}
	else
	{
		ret = __real_open64(pathname, flags);
	}


	if (ret != -1)
	{
		if (hvac_track_file(pathname, flags, ret))
		{
			L4C_INFO("Open64: Tracking file %s",pathname);
		}
	}

	
	return ret;

}



int WRAP_DECL(close)(int fd)
{
	int ret = 0;

	/* Check if hvac data has been initialized? Can we possibly hit a close call before an open call? */
	MAP_OR_FAIL(close);
	if (g_disable_redirect || tl_disable_redirect) return __real_close(fd);

	const char *path = hvac_get_path(fd);
	if (path)
	{
		L4C_INFO("Close to file %s",path);
		hvac_remove_fd(fd);
	}

	//hvac_remote_close(fd);

	/* Close the passed in file-descriptor tracked or not */
	if ((ret = __real_close(fd)) != 0)
	{
		L4C_PERROR("Error from close");
		return ret;
	}

	return ret;
}


ssize_t WRAP_DECL(read)(int fd, void *buf, size_t count)
{
	int ret = -1;
	
	//remove me
    MAP_OR_FAIL(read);	
	
    const char *path = hvac_get_path(fd);


	ret = hvac_remote_read(fd,buf,count);

	if (path)
    {
        L4C_INFO("Read to file %s of size %ld returning %ld bytes",path,count,ret);
    }
	
	if (ret == -1)
	{
		ret = __real_read(fd,buf,count);	
	}
		
    return ret;
}

ssize_t WRAP_DECL(pread)(int fd, void *buf, size_t count, off_t offset)
{
	ssize_t ret = -1;
	MAP_OR_FAIL(pread);

	const char *path = hvac_get_path(fd);
	if (path)
	{                
		L4C_INFO("pread to tracked file %s",path);
		ret = hvac_remote_pread(fd, buf, count, offset);
		ret = __real_pread(fd,buf,count,offset);
	}
	else
	{
		ret = __real_pread(fd,buf,count,offset);
	}

	return ret;
}



ssize_t WRAP_DECL(read64)(int fd, void *buf, size_t count)
{
	//remove me
	MAP_OR_FAIL(read64);


	const char *path = hvac_get_path(fd);
	if (path)
	{
		L4C_INFO("Read64 to file %s of size %ld",path,count);
	}

	return __real_read64(fd,buf,count);
}

ssize_t WRAP_DECL(write)(int fd, const void *buf, size_t count)
{
	MAP_OR_FAIL(write);
	return __real_write(fd, buf, count);

	const char *path = hvac_get_path(fd);
	if (path)
	{
		L4C_ERR("Write to file %s of size %ld",path,count);
		assert(false);
	}

	return __real_write(fd, buf, count);
}

off_t WRAP_DECL(lseek)(int fd, off_t offset, int whence)
{
	MAP_OR_FAIL(lseek);
	if (g_disable_redirect || tl_disable_redirect) return __real_lseek(fd,offset,whence);

	if (hvac_file_tracked(fd)){
		L4C_INFO("Got an LSEEK on a tracked file %d %ld\n", fd, offset);	
		return hvac_remote_lseek(fd,offset,whence);
	}
	return __real_lseek(fd, offset, whence);
}

off64_t WRAP_DECL(lseek64)(int fd, off64_t offset, int whence)
{
	MAP_OR_FAIL(lseek64);
	if (g_disable_redirect || tl_disable_redirect) return __real_lseek64(fd,offset,whence);
	if (hvac_file_tracked(fd)){
		L4C_INFO("Got an LSEEK64 on a tracked file %d %ld\n", fd, offset);	
		return hvac_remote_lseek(fd,offset,whence);
	}
	return __real_lseek64(fd, offset, whence);
}

ssize_t WRAP_DECL(readv)(int fd, const struct iovec *iov, int iovcnt)
{
	MAP_OR_FAIL(readv);
	const char *path = hvac_get_path(fd);
	if (path)
	{
		L4C_INFO("Readv to tracked file %s",path);
	}

	return __real_readv(fd, iov, iovcnt);

}
/*
   void* WRAP_DECL(mmap)(void *addr, ssize_t length, int prot, int flags, int fd, off_t offset)
   {
   MAP_OR_FAIL(mmap);
   if (path)
   {
   L4C_INFO("MMAP to tracked file %s Length %ld Offset %ld",path, length, offset);
   }

   return __real_mmap(addr,length, prot, flags, fd, offset);

   }
   */


#if 0




size_t WRAP_DECL(fwrite)(const void *ptr, size_t size, size_t count, FILE *stream)
{
	MAP_OR_FAIL(fwrite);

	return __real_fwrite(ptr,size,count,stream);
}

int WRAP_DECL(fsync)(int fd)
{
	MAP_OR_FAIL(fsync);
	if (g_disable_redirect || tl_disable_redirect) return __real_fsync(fd);

	return __real_fsync(fd);
}

int WRAP_DECL(fdatasync)(int fd)
{
	MAP_OR_FAIL(fdatasync);
	if (g_disable_redirect || tl_disable_redirect) return __real_fdatasync(fd);

	return __real_fdatasync(fd);
}

off_t WRAP_DECL(lseek)(int fd, off_t offset, int whence)
{
	MAP_OR_FAIL(lseek);
	if (g_disable_redirect || tl_disable_redirect) return __real_lseek(fd,offset,whence);
	L4C_INFO("Got a LSEEK --- Damnit\n");
	return __real_lseek(fd, offset, whence);
}

off64_t WRAP_DECL(lseek64)(int fd, off64_t offset, int whence)
{
	MAP_OR_FAIL(lseek64);
	if (g_disable_redirect || tl_disable_redirect) return __real_lseek64(fd,offset,whence);
	if (hvac_file_tracked(fd))
		L4C_INFO("Got an LSEEK64 on a tracked file %d %ld\n", fd, offset);
	return __real_lseek64(fd, offset, whence);
}

/* fopen wrapper */
FILE *WRAP_DECL(fopen)(const char *path, const char *mode)
{

	MAP_OR_FAIL(fopen);
	if (g_disable_redirect || tl_disable_redirect) return __real_fopen( path, mode);


	L4C_INFO("Intercepted Fopen %s",path);

	return __real_fopen(path, mode);
}



bool check_open_mode(const int flags, bool ignore_check)
{
	//Always back out of RDONLY
	if ((flags & O_ACCMODE) == O_WRONLY) {
		return false;
	}

	if ((flags & O_APPEND)) {
		return false;
	}
	return true;
}

/* Wrappers */
int WRAP_DECL(fclose)(FILE *fp)
{
	int ret = 0;

	/* RTLD Next fclose call */
	MAP_OR_FAIL(fclose);

	if (g_disable_redirect || tl_disable_redirect) return __real_fclose(fp);

	if ((ret = __real_fclose(fp)) != 0)
	{
		L4C_PERROR("Error from fclose");
		return ret;
	}

	return ret;
}


ssize_t WRAP_DECL(pwrite)(int fd, const void *buf, size_t count, off_t offset)
{
	MAP_OR_FAIL(pwrite);
	return __real_pwrite(fd, buf, count, offset);
}


ssize_t WRAP_DECL(pread)(int fd, void *buf, size_t count, off_t offset)
{
	MAP_OR_FAIL(pread);
	return __real_pread(fd,buf,count,offset);
}


ssize_t WRAP_DECL(write)(int fd, const void *buf, size_t count)
{
	MAP_OR_FAIL(write);
	return __real_write(fd, buf, count);

	const char *path = hvac_get_path(fd);
	if (path)
	{
		L4C_INFO("Write to file %s of size %ld",path,count);
	}

	return __real_write(fd, buf, count);
}

#endif
