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


#ifndef LCAPD_INTERNAL_H
#define LCAPD_INTERNAL_H

#include <unistd.h>
#include <limits.h>
#include <assert.h>

#include <pthread.h>

#include <lustre/lustreapi.h>
#include <lustre/lustre_user.h>

#include <zmq.h>

#include <lcap_idl.h>
#include <lcap_log.h>
#include <lcap_net.h>

#define MAX_MDT 128

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
    void                *cc_zctx;
    void                *cc_sock;
    struct conn_id      *cc_rcid[MAX_MDT];  /* readers identities */
};


static inline const struct lcap_cfg *ctx_config(const struct lcap_ctx *ctx)
{
    assert(ctx);
    return ctx->cc_config;
}


int lcap_cfg_init(int ac, char **av, struct lcap_cfg *config);
int lcap_cfg_release(struct lcap_cfg *config);


int lcapd_process_request(void *hint, const struct lcapnet_request *req);


struct subtask_info {
    pthread_t   si_thread;
    bool        si_running;
};

struct subtask_args {
    const struct lcap_cfg   *sa_cfg;
    unsigned int             sa_idx;
};

void *reader_main(void *args);

#define READERS_URL     "inproc://lcaprdr.ipc"
#define CLG_ACK_URL     "inproc://lcapack.ipc"
#define WORKERS_URL     "inproc://lcapwrk.ipc"

#define BROKER_CONN_URL "tcp://localhost:8189"
#define BROKER_BIND_URL "tcp://*:8189"


int peer_rpc_send(void *sock, const struct conn_id *src_id,
                  const struct conn_id *dst_id, const char *msg,
                  size_t msg_len);

int ack_retcode(void *sock, const struct conn_id *src_cid,
                const struct conn_id *dst_cid, int ret);


static inline bool cid_compare(const struct conn_id *cid0,
                               const struct conn_id *cid1)
{
    if (cid0->ci_length > cid1->ci_length)
        return -1;

    if (cid0->ci_length < cid1->ci_length)
        return 1;

    return memcmp(cid0->ci_data, cid1->ci_data, cid0->ci_length);
}

#endif /* LCAPD_INTERNAL_H */
