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
    LCAPLOG_ALL,
    LCAPLOG_DBG,
    LCAPLOG_NFO,
    LCAPLOG_ERR
} lcap_loglevel_t;


extern lcap_loglevel_t CurrentLogLevel;


#define LCAP_LOG_WRAP(lvl, ...)  \
    do { \
        if ((lvl) >= CurrentLogLevel) { \
            __lcap_log_internal((lvl), __FILE__, __LINE__, __func__, \
                                __VA_ARGS__); \
        } \
    } while (0)


#define lcaplog_all(...) LCAP_LOG_WRAP(LCAPLOG_ALL, __VA_ARGS__)

#define lcaplog_dbg(...)  LCAP_LOG_WRAP(LCAPLOG_DBG, __VA_ARGS__)

#define lcaplog_nfo(...)  LCAP_LOG_WRAP(LCAPLOG_NFO, __VA_ARGS__)

#define lcaplog_err(...)  LCAP_LOG_WRAP(LCAPLOG_ERR, __VA_ARGS__)


static inline const char *loglevel2str(lcap_loglevel_t loglevel)
{
    switch (loglevel) {
        case LCAPLOG_ALL:
            return "FULL";

        case LCAPLOG_DBG:
            return "DEBUG";

        case LCAPLOG_NFO:
            return "INFO";

        case LCAPLOG_ERR:
            return "ERROR";

        default:
            return "???";
    }
}

int lcap_log_open(void);
int lcap_log_close(void);

void __lcap_log_internal(lcap_loglevel_t loglevel, const char *file, int line,
                         const char *func, const char *format, ...)
                         __attribute__((format (printf, 5, 6)));

void lcap_set_loglevel(int verbosity);
int lcap_get_loglevel(void);
int lcap_set_logger(const char *logger_name);

#endif /* LCAPLOG_H */
