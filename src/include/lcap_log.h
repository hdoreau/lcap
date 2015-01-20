/*
 * LCAP - Lustre Changelogs Aggregate and Publish
 *
 * Copyright (C)  2013-2014  CEA/DAM
 * Henri DOREAU <henri.doreau@cea.fr>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


#ifndef LCAPLOG_H
#define LCAPLOG_H

#include <sys/time.h>
#include <assert.h>


/* Logging-related data structures */

typedef enum {
    LCAPLOG_DBG = 1,
    LCAPLOG_VRB = 2,
    LCAPLOG_NFO = 3,
    LCAPLOG_ERR = 4,

    /* Special values */
    LCAPLOG_OFF = 5,
    LCAPLOG_MIN = LCAPLOG_DBG,
} lcap_loglevel_t;


/**
 * Global process loglevel
 */
extern lcap_loglevel_t CurrentLogLevel;

/**
 * Generate a log record, according to the current log level.
 * Not for direct use. Use the lcap_{debug,verb,info,error}
 * macros below.
 */
#define __LCAP_LOG_WRAP(lvl, ...)  \
    do { \
        if ((lvl) >= CurrentLogLevel) { \
            __lcap_log_internal((lvl), __FILE__, __LINE__, __func__, \
                                __VA_ARGS__); \
        } \
    } while (0)


/* Internal function, not for direct use */
void __lcap_log_internal(lcap_loglevel_t loglevel, const char *file, int line,
                         const char *func, const char *format, ...)
                         __attribute__((format (printf, 5, 6)));


/* Public macros, for actual use in code */
#define lcap_debug(...) __LCAP_LOG_WRAP(LCAPLOG_DBG, __VA_ARGS__)

#define lcap_verb(...)  __LCAP_LOG_WRAP(LCAPLOG_VRB, __VA_ARGS__)

#define lcap_info(...)  __LCAP_LOG_WRAP(LCAPLOG_NFO, __VA_ARGS__)

#define lcap_error(...) __LCAP_LOG_WRAP(LCAPLOG_ERR, __VA_ARGS__)


static inline const char *loglevel2str(lcap_loglevel_t loglevel)
{
    switch (loglevel) {
        case LCAPLOG_DBG:
            return "DEBUG";

        case LCAPLOG_VRB:
            return "VERBOSE";

        case LCAPLOG_NFO:
            return "INFO";

        case LCAPLOG_ERR:
            return "ERROR";

        default:
            return "???";
    }
}

/**
 * Initialize the logging infrastructure. This should be invoked after a
 * logging infrastructure has been set (see lcap_set_logger).
 *
 * Returns 0 on success and a negative error code on failure.
 */
int lcap_log_open(void);

/**
 * Release resources were taken in lcap_log_open, if any.
 *
 * Returns 0 on success and a negative error code on failure.
 */
int lcap_log_close(void);

/**
 * Adjust current log level.
 */
void lcap_set_loglevel(int verbosity);

/**
 * Get current log level.
 */
int lcap_get_loglevel(void);

/**
 * Register a new logger. Valid ones include "stderr" and "syslog".
 */
int lcap_set_logger(const char *logger_name);

#endif /* LCAPLOG_H */
