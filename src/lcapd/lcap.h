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


#ifndef LCAP_H
#define LCAP_H

#include <unistd.h>
#include <limits.h>
#include <assert.h>

#include <pthread.h>

#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>

#include <zmq.h>

#include <lcap_idl.h>
#include <lcap_log.h>

#define MAX_MDT 128


struct lcap_ctx;
struct subtask_info;


/* --- GLOBAL APPLICATION CONTEXT --- */
struct lcap_cfg {
    char            *ccf_mdt[MAX_MDT];
    char            *ccf_clreader;
    unsigned int     ccf_mdtcount;

    char            *ccf_file;
    char            *ccf_loggername;

    bool             ccf_oneshot;

    int              ccf_verbosity;
    int              ccf_max_bkt;
    int              ccf_rec_batch_count;
    int              ccf_worker_count;
};

struct lcap_ctx {
    struct lcap_cfg     *cc_config;
    struct subtask_info *cc_rdr_info;
    void                *cc_zctx;   /**< 0mq context */
    void                *cc_sock;   /**< Broker socket */
    struct conn_id      *cc_rcid[MAX_MDT];  /* readers identities */
};


/* --- HELPERS AND CONVENIENCY WRAPPERS --- */

static inline const struct lcap_cfg *ctx_config(const struct lcap_ctx *ctx)
{
    assert(ctx);
    return ctx->cc_config;
}

#endif /* LCAP_H */
