/*****************************************************************************
 * Author:     Ross G. Miller
 *             Oak Ridge National Lab
 * Date:       04/06/20
 * Purpose:    Functions & structures for logging 
 *
 * Updated:    11/16/20 - Purpose moved to HVAC modified to build without 
 *             warning under C++
 * 
 * Copyright 2020 UT Battelle, LLC
 *
 * This work was supported by the Oak Ridge Leadership Computing Facility at
 * the Oak Ridge National Laboratory, which is managed by UT Battelle, LLC for
 * the U.S. DOE (under the contract No. DE-AC05-00OR22725).
 *
 * This file is part of the hvac project.
 ****************************************************************************/

#ifndef _HVAC_LOGGING_H_
#define _HVAC_LOGGING_H_

#include <stdbool.h>
#include <errno.h>

#include "log4c.h"

    // Sets up the default parameters for log4c
#ifdef __cplusplus
    extern "C" void hvac_init_logging();
    extern "C" void log_preformatter_internal( unsigned priority, const char* filename, unsigned linenum, const char *format_str, ...);
#endif
    // Cleanly shut down the logging system (close files, etc...)
    void hvac_finalize_logging();

// Logging parameters are organized into hierarchical categories.
// We're not currently making use of the hierarchy, but the category
// name does get displayed on each line of the log file.
// The defines we check for are set in the CMakeLists file and passed
// in on the compiler command line
#ifdef HVAC_CLIENT
#define L4C_CAT_NAME "HVAC_CLIENT"

// Parameters for where & how the log files are written
// The current dir is probably an acceptable fallback for the intercept
// library, but we'll check for an ENV variable anyway
#define L4C_LOG_DIR (char *)"."
#define L4C_LOG_NAME_PREFIX (char *)"hvac_intercept_log"
#elif defined HVAC_SERVER
#define L4C_CAT_NAME "hvac_server"

// /tmp is a lousy location for these log files and probably only useful
// for the hvac developers.  That said, it's the best I can think of
// right now.  It's going to be imperative that users set an environment
// variable for the log file location.
#define L4C_LOG_DIR "."
#define L4C_LOG_NAME_PREFIX "hvac_server_log"
#else
// In case we somehow forgot to define the correct macro
#error Neither HVAC_CLIENT nor HVAC_SERVER was defined! Check your build scripts!
#define L4C_CAT_NAME "hvac_UNKNOWN"
    // NOTE:  L4C_CAT_NAME is defined here only to keep various code editors and linters
    // from complaining about it not being defined.  Visual Studio Code in particular
    // is bad about this.
#endif

// Name of an environment variable the user can define to adjust verbosity
// 0 is the least verbose (LOG4C_PRIORITY_FATAL) and 800 is the most verbose
// (LOG4C_PRIORITY_TRACE)
#define HVAC_LOG_LEVEL "HVAC_LOG_LEVEL"

// Name of an environment variable the user can define to set the location
// of the log files.
#define HVAC_LOG_DIR "HVAC_LOG_DIR"

// Name of an environment variable the user can define to set the first part
// of the filename for the log files.  (The actual file names will have the
// process ID and a sequence number appended to the prefix.)
#define HVAC_LOG_PREFIX "HVAC_LOG_PREFIX"

// Some macros to make logging a little simpler
#define L4C_LOG_AT_PRIORITY(log_priority, format_str, ...) \
    log_preformatter_internal(log_priority, __FILE__, __LINE__, format_str, ##__VA_ARGS__)

#define L4C_FATAL(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_FATAL, format_str, ##__VA_ARGS__)
#define L4C_ALERT(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_ALERT, format_str, ##__VA_ARGS__)
#define L4C_CRIT(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_CRIT, format_str, ##__VA_ARGS__)
#define L4C_ERR(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_ERROR, format_str, ##__VA_ARGS__)
#define L4C_WARN(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_WARN, format_str, ##__VA_ARGS__)
#define L4C_NOTICE(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_NOTICE, format_str, ##__VA_ARGS__)
#define L4C_INFO(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_INFO, format_str, ##__VA_ARGS__)
#define L4C_DEBUG(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_DEBUG, format_str, ##__VA_ARGS__)
#define L4C_TRACE(format_str, ...) L4C_LOG_AT_PRIORITY(LOG4C_PRIORITY_TRACE, format_str, ##__VA_ARGS__)

// A macro that has semantics similar to perror(), but outputs to the logging
// system instead of to stderr.
// Note that, unlike perror(), this macro isn't smart enough to not print the
// colon and space in cases where msg is empty.
#define L4C_PERROR(msg) \
    L4C_ERR(msg ": %s", strerror(errno))

    // Thread-local symbol for disabling redirects.  Used by the L4C_* macros
    // to make sure I/O to our log files doesn't get redirected.
    extern __thread bool tl_disable_redirect;

    // "Private" helper func for the L4C_* macros above. It's expected to only be
    // called by the L4C_LOG_AT_PRIORITY macro.
    void log_preformatter_internal(unsigned priority, const char *filename, unsigned linenum,
                                   const char *format_str, ...);
#endif /* _HVAC_LOGGING_H_ */
