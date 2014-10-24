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


#define _GNU_SOURCE
#include <stdio.h>

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lcaplog.h"


struct lcap_log_rec {
    struct timeval   clr_time;  /**< Emission time of the record. */
    lcap_loglevel_t  clr_level; /**< Message criticity level . */
    const char      *clr_file;  /**< Source file origin. */
    int              clr_line;  /**< Line number in the file. */
    const char      *clr_func;  /**< Function name. */
    pid_t            clr_pid;   /**< Emitter process ID. */
    pid_t            clr_tid;   /**< Emitter thread ID. */
    char            *clr_msg;   /**< Actual message */
};


struct log_facility_operations {
    int     (*lfo_open)(void);
    void    (*lfo_log)(const struct lcap_log_rec *);
    int     (*lfo_close)(void);
};

struct log_facility {
    const char                              *lf_name;
    const struct log_facility_operations    *lf_ops;
};


lcap_loglevel_t              CurrentLogLevel;
const struct log_facility   *CurrentLogger;


static pid_t gettid(void)
{
    return syscall(SYS_gettid);
}

/*
 * === STDERR LOGGING FACILITY ===
 */
static int stderr_open(void)
{
    return 0;
}

static void stderr_log(const struct lcap_log_rec *rec)
{
    fprintf(stderr, "lcap[%u/%u] %s %s(): %s\n",
            (unsigned int)rec->clr_pid,
            (unsigned int)rec->clr_tid,
            loglevel2str(rec->clr_level),
            rec->clr_func, rec->clr_msg);
}

static int stderr_close(void)
{
    fflush(stderr);
    return 0;
}

static const struct log_facility_operations StderrLogOps = {
    .lfo_open  = stderr_open,
    .lfo_log   = stderr_log,
    .lfo_close = stderr_close,
};

static const struct log_facility StderrLogger = {
    .lf_name   = "stderr",
    .lf_ops    = &StderrLogOps,
};


/*
 * === SYSLOG LOGGING FACILITY ===
 */
static inline int lcaplevel2syslog(lcap_loglevel_t level)
{
    switch(level) {
        case LCAPLOG_ERR:
            return LOG_ERR;

        case LCAPLOG_NFO:
            return LOG_NOTICE;

        case LCAPLOG_DBG:
        case LCAPLOG_ALL:
            return LOG_DEBUG;

        default:
            /* hmm... */
            return LOG_ALERT;
    }
}

static int syslog_open(void)
{
    openlog("LCAPD", LOG_PID, LOG_USER);
    return 0;
}

static void syslog_log(const struct lcap_log_rec *rec)
{
    int priority;

    priority = lcaplevel2syslog(rec->clr_level);
    syslog(priority, "lcap[%u/%u] %s %s(): %s\n",
           (unsigned int)rec->clr_pid,
           (unsigned int)rec->clr_tid,
           loglevel2str(rec->clr_level),
           rec->clr_func, rec->clr_msg);
}

static int syslog_close(void)
{
    closelog();
    return 0;
}

static const struct log_facility_operations SyslogOps = {
    .lfo_open  = syslog_open,
    .lfo_log   = syslog_log,
    .lfo_close = syslog_close,
};

static const struct log_facility SyslogLogger = {
    .lf_name   = "syslog",
    .lf_ops    = &SyslogOps,
};


/*
 * === GENERAL LOGGING API ===
 */
int lcap_log_open(void)
{
    if (CurrentLogger == NULL)
        return -ENODEV;

    assert(CurrentLogger->lf_ops);
    assert(CurrentLogger->lf_ops->lfo_open);
    return CurrentLogger->lf_ops->lfo_open();
}

int lcap_log_close(void)
{
    if (CurrentLogger == NULL)
        return -ENODEV;

    assert(CurrentLogger->lf_ops);
    assert(CurrentLogger->lf_ops->lfo_close);
    return CurrentLogger->lf_ops->lfo_close();
}

void __lcap_log_internal(lcap_loglevel_t loglevel, const char *file, int line,
                         const char *func, const char *format, ...)
{
    struct lcap_log_rec rec;
    va_list             args;
    int                 rc;

    if (CurrentLogger == NULL)
        return;

    assert(CurrentLogger->lf_ops);
    assert(CurrentLogger->lf_ops->lfo_log);

    va_start(args, format);

    rec.clr_level = loglevel;
    rec.clr_file = file;
    rec.clr_line = line;
    rec.clr_func = func;
    rec.clr_pid  = getpid();
    rec.clr_tid  = gettid();

    gettimeofday(&rec.clr_time, NULL);

    rc = vasprintf(&rec.clr_msg, format, args);
    if (rc >= 0) {
        CurrentLogger->lf_ops->lfo_log(&rec);
        free(rec.clr_msg);
    }

    va_end(args);
}

void lcap_set_loglevel(int verbosity)
{
    switch (verbosity) {
        case 0:
            CurrentLogLevel = LCAPLOG_ERR;
            break;

        case 1:
            CurrentLogLevel = LCAPLOG_NFO;
            break;

        case 2:
            CurrentLogLevel = LCAPLOG_DBG;
            break;

        default:
            CurrentLogLevel = LCAPLOG_ALL;
            break;
    }
}

int lcap_get_loglevel(void)
{
    return CurrentLogLevel;
}


/**
 * Available logging subsystems. First is default.
 */
static const struct log_facility *AvailableLoggers[] = {
    &StderrLogger,
    &SyslogLogger,
    NULL
};

int lcap_set_logger(const char *logger_name)
{
    int i;

    if (logger_name == NULL)
        logger_name = AvailableLoggers[0]->lf_name;

    for (i = 0; AvailableLoggers[i] != NULL; i++) {
        const struct log_facility *current = AvailableLoggers[i];

        if (strcasecmp(current->lf_name, logger_name) == 0) {
            CurrentLogger = current;
            return 0;
        }
    }

    return -EINVAL;
}

