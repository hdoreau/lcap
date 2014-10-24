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


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "lcap.h"
#include "modules.h"
#include "lcapd_internal.h"

#include <lcap_idl.h>

/**
 * Number of seconds to wait between two retries at the end of the
 * changelog records stream.
 */
#define EOF_RETRY_DELAY 1


extern int TerminateSig;

struct reader_env {
    unsigned int        re_index;   /**< Reader index */
    unsigned int        re_ack_idx; /**< Last acknowledged record */
    unsigned long long  re_total;   /**< Number of received records */
    unsigned long long  re_srec;    /**< Start record */
    struct timeval      re_start;   /**< Start time */
    struct lcap_ctx    *re_ctx;     /**< Global lcapd context */
    void               *re_mptr;    /**< Module private pointer */
};


static inline const char *reader_device(const struct reader_env *env)
{
    return ctx_config(env->re_ctx)->ccf_mdt[env->re_index];
}

static int __ack_records(struct reader_env *env, long long ack_idx)
{
    const struct lcap_cfg   *cfg = ctx_config(env->re_ctx);
    const char              *device = reader_device(env);
    int                      rc;

    lcaplog_nfo("Acknowledging records to %lld to %s", ack_idx, device);

    rc = llapi_changelog_clear(device, cfg->ccf_clreader, ack_idx);
    if (rc)
        lcaplog_err("llapi-changelog_clear(%s, %s, %lld): %s", device,
                    cfg->ccf_clreader, ack_idx, strerror(-rc));
    return rc;
}

static int changelog_reader_ack(struct reader_env *env)
{
    long long   ack_idx;
    int         rc;

    rc = cpm_get_ack(env->re_ctx, env->re_mptr, reader_device(env), &ack_idx);
    if (rc == 0 && ack_idx > env->re_ack_idx) {
        rc = __ack_records(env, ack_idx);
        if (rc) {
            lcaplog_err("Acknowledging records to lustre: %s",
                        zmq_strerror(-rc));
            return rc;
        }
        env->re_ack_idx = ack_idx;
    } else if (rc < 0 && rc != -EAGAIN) {
        lcaplog_err("Getting current clear index: %s", strerror(-rc));
        return rc;
    }

    return 0;
}

static int changelog_reader_init(struct lcap_ctx *ctx, unsigned int idx,
                                 struct reader_env *env)
{
    int rc;

    /* Initialize stats */
    env->re_index = idx;
    env->re_ctx = ctx;
    env->re_ack_idx = 0ULL;
    env->re_total = 0ULL;
    env->re_srec = 0ULL;
    gettimeofday(&env->re_start, NULL);

    rc = cpm_init(ctx, &env->re_mptr);
    if (rc < 0) {
        lcaplog_err("Cannot initialize module: %s", strerror(-rc));
        return rc;
    }

    lcaplog_dbg("Ready to enqueue changelog records");
    return 0;
}

static int changelog_reader_print_stats(struct reader_env *env)
{
    const char      *device = reader_device(env);
    struct timeval   end;
    unsigned int     duration;
    double           rate;

    gettimeofday(&end, NULL);

    duration = end.tv_sec - env->re_start.tv_sec;
    duration *= 1000;
    duration += (end.tv_usec / 1000) - (env->re_start.tv_usec / 1000);

    if (duration == 0)
        rate = 0.0;
    else
        rate = (double)env->re_total / duration;

    lcaplog_nfo("%llu records processed from %s (%d/s)",
                env->re_total, device, (int)(rate * 1000));
    return 0;
}

static int changelog_reader_release(struct reader_env *env)
{
    int rc;

    rc = cpm_destroy(env->re_ctx, env->re_mptr);
    if (rc < 0)
        return rc;

    rc = changelog_reader_print_stats(env);
    if (rc < 0)
        return rc;

    return 0;
}

static int changelog_reader_loop(struct lcap_ctx *ctx, struct reader_env *env)
{
    void                        *clprivate;
    const char                  *device = reader_device(env);
    struct changelog_ext_rec    *rec;
    struct px_rpc_enqueue        hdr;
    int                          flags = CHANGELOG_FLAG_BLOCK;
    int                          rc;

    hdr.pr_hdr.op_type = RPC_OP_ENQUEUE;
    hdr.pr_count       = 1;

    rc = llapi_changelog_start(&clprivate, flags, device, env->re_srec);
    if (rc) {
        lcaplog_err("Cannot start changelog on %s: %s", device, strerror(-rc));
        return rc;
    }

    while ((rc = llapi_changelog_recv(clprivate, &rec)) == 0) {
        if (TerminateSig)
            break;

        env->re_total++;

        if (rec->cr_index <= env->re_srec)
            continue;

        rc = cpm_rec_enqueue(env->re_ctx, env->re_mptr, rec);
        if (rc < 0)
            break;

        env->re_srec = rec->cr_index;

        rc = changelog_reader_ack(env);
        if (rc < 0)
            break;
    }

    /* EOF */
    if (rc == 0 || rc == 1 || rc == -EAGAIN || rc == -EPROTO)
        rc = changelog_reader_ack(env);

    llapi_changelog_fini(&clprivate);
    return rc;
}

static inline bool reader_running(const struct subtask_args *sa)
{
    return sa->sa_ctx->cc_rdr_info[sa->sa_idx].si_running;
}

void *reader_main(void *args)
{
    struct subtask_args     *sa = (struct subtask_args *)args;
    struct lcap_ctx         *ctx = sa->sa_ctx;
    const struct lcap_cfg   *config = ctx_config(ctx);
    struct reader_env        env;
    int                      reader_idx = sa->sa_idx;
    int                      rc;

    rc = changelog_reader_init(ctx, reader_idx, &env);
    if (rc)
        return NULL;

    while (reader_running(sa)) {
        rc = changelog_reader_loop(ctx, &env);
        if (rc)
            break;

        if (config->ccf_oneshot)
            break;

        lcaplog_all("...");
        sleep(EOF_RETRY_DELAY);
    }

    lcaplog_all("ChangeLog reader %d stopping with rc=%d", reader_idx, rc);

    changelog_reader_release(&env);
    free(args);
    return NULL;
}
