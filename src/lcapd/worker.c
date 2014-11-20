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
#include <signal.h>
#include <time.h>

#include "lcap.h"
#include "modules.h"
#include "lcapd_internal.h"

#include <lcap_idl.h>

#define RECVBUF_SIZE    4096

/**
 * Signal handling flags.
 */
extern int TerminateSig;


struct wrk_env {
    struct lcap_ctx *we_ctx;
    void            *we_sock;
    void            *we_mptr;
};

static int rpc_handle_dequeue(struct wrk_env *env, struct px_rpc_dequeue *rpc,
                              size_t msg_size)
{
    if (msg_size < sizeof(*rpc))
        return -EINVAL;

    return 0;
}

static int rpc_handle_start(struct wrk_env *env, const char *msg,
                            size_t msg_size)
{
    struct lcap_ctx         *ctx = env->we_ctx;
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    struct px_rpc_register  *rpc = (struct px_rpc_register *)msg;
    int                      i;

    if (msg_size < sizeof(*rpc))
        return -EINVAL;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        if (strcmp((const char *)rpc->pr_mdtname, cfg->ccf_mdt[i]) == 0)
            return 0;
    }
    lcaplog_err("Received RPC for unknown device: %s", rpc->pr_mdtname);
    return -ENODEV;
}

static int rpc_handle_clear(struct wrk_env *env, const char *msg,
                            size_t msg_size)
{
    const char          *dev;
    struct lcap_ctx     *ctx = env->we_ctx;
    struct px_rpc_clear *rpc = (struct px_rpc_clear *)msg;
    struct client_id    *cl_id = &rpc->pr_hdr.cl_id;

    if (msg_size < sizeof(*rpc))
        return -EINVAL;

    /* Variable field length sanity */
    if (rpc->pr_id_len > msg_size - sizeof(*rpc))
        return -EINVAL;

    dev = px_rpc_get_mdtname(rpc);

    lcaplog_all("Client #%d ack %lld on device %s", cl_id->ident,
                (long long)rpc->pr_index, dev);

    return cpm_set_ack(ctx, env->we_mptr, cl_id, dev, (long long)rpc->pr_index);
}

static int ack_retcode(struct wrk_env *env, int retcode)
{
    struct px_rpc_ack   rep;
    int                 rc;

    memset(&rep, 0, sizeof(rep));
    rep.pr_hdr.op_type = RPC_OP_ACK;
    rep.pr_retcode     = retcode;

    rc = zmq_send(env->we_sock, (void *)&rep, sizeof(rep), 0);
    if (rc < 0) {
        rc = -errno;
        lcaplog_err("Worker SND error: %s (%d)", zmq_strerror(-rc), -rc);
        return rc;
    }

    lcaplog_all("ACKed with code %d", retcode);
    return 0;
}

static int ack_send_records(struct wrk_env *env)
{
    const struct lcap_cfg   *cfg = ctx_config(env->we_ctx);
    struct px_rpc_enqueue    reply;
    lcap_chlg_t             *rec;
    int                      i, j;
    int                      rc;

    rec = (lcap_chlg_t *)calloc(cfg->ccf_rec_batch_count, sizeof(rec));
    if (rec == NULL) {
        rc = -ENOMEM;
        lcaplog_err("Cannot allocate memory for record batch: %s", strerror(-rc));
        return ack_retcode(env, rc);
    }

    for (i = 0; i < cfg->ccf_rec_batch_count; i++) {
        rc = cpm_rec_dequeue(env->we_ctx, env->we_mptr, &rec[i]);
        if (rc < 0 && i == 0) {
            /* We can't get anything, notify the client */
            free(rec);
            return ack_retcode(env, rc == -EAGAIN ? 1 : rc);
        } else if (rc < 0) {
            /* OK, let's send what we already have */
            break;
        }
    }

    memset(&reply, 0, sizeof(reply));
    reply.pr_hdr.op_type = RPC_OP_ENQUEUE;
    reply.pr_count = i;

    rc = zmq_send(env->we_sock, &reply, sizeof(reply), ZMQ_SNDMORE);
    if (rc < 0) {
        rc = -errno;
        lcaplog_err("Cannot send records back: %s", zmq_strerror(-rc));
        return rc;
    }

    for (j = 0; j < i; j++) {
        lcap_chlg_t record = rec[j];

        rc = zmq_send(env->we_sock,
                      record,
                      sizeof(*record) + record->cr_namelen,
                      j < i - 1 ? ZMQ_SNDMORE : 0);
        if (rc < 0) {
            rc = -errno;
            lcaplog_err("Cannot send records back: %s", zmq_strerror(-rc));
            break;
        }
    }

    lcaplog_all("Returned %d records (#%llu -> #%llu)", j, rec[0]->cr_index,
                rec[j - 1]->cr_index);

    for (j = 0; j < i; j++)
        llapi_changelog_free(&rec[j]);
    free(rec);

    return 0;
}

static int rpc_handler(struct wrk_env *env, const char *msg, size_t msg_size)
{
    struct px_rpc_hdr   *msg_hdr = (struct px_rpc_hdr *)msg;
    int                  rc;

    if (msg_size < sizeof(*msg_hdr))
        return ack_retcode(env, -EINVAL);

    switch (msg_hdr->op_type) {
        case RPC_OP_DEQUEUE:
            lcaplog_all("Received RPC_OP_DEQUEUE request");
            rc = rpc_handle_dequeue(env, (struct px_rpc_dequeue *)msg, msg_size);
            rc = ack_send_records(env);
            break;

        case RPC_OP_START:
            lcaplog_all("Received RPC_OP_START request");
            rc = rpc_handle_start(env, msg, msg_size);
            rc = ack_retcode(env, rc);
            break;

        case RPC_OP_CLEAR:
            lcaplog_all("Received RPC_OP_CLEAR request");
            rc = rpc_handle_clear(env, msg, msg_size);
            rc = ack_retcode(env, rc);
            break;

        case RPC_OP_FINI:
            lcaplog_all("Received RPC_OP_FINI request");
            rc = ack_retcode(env, 0);
            break;

        default:
            lcaplog_nfo("Invalid RPC opcode %d, ignoring", msg_hdr->op_type);
            rc = ack_retcode(env, -EPROTO);
    }

    return rc;
}

static inline bool worker_running(const struct subtask_args *sa)
{
    return sa->sa_ctx->cc_wrk_info[sa->sa_idx].si_running;
}

void *worker_main(void *args)
{
    struct subtask_args *sa = (struct subtask_args *)args;
    struct lcap_ctx     *ctx = sa->sa_ctx;
    struct wrk_env       env;
    void                *zctx = ctx->cc_zctx;
    void                *zrcv = NULL;
    char                *buff;
    int                  rc;

    env.we_ctx  = ctx;
    env.we_mptr = NULL;

    rc = cpm_init(ctx, &env.we_mptr);
    if (rc) {
        lcaplog_err("Cannot initialize module: %s", zmq_strerror(-rc));
        return NULL;
    }

    env.we_sock = zrcv = zmq_socket(zctx, ZMQ_REP);
    if (zrcv == NULL) {
        rc = -errno;
        lcaplog_err("Opening worker socket: %s", zmq_strerror(-rc));
        goto out;
    }
    
    rc = zmq_connect(zrcv, WORKERS_URL);
    if (rc) {
        rc = -errno;
        lcaplog_err("Opening worker socket: %s", zmq_strerror(-rc));
        goto out;
    }

    buff = (char *)malloc(RECVBUF_SIZE);
    if (buff == NULL) {
        rc = -ENOMEM;
        lcaplog_err("Cannot allocate memory");
        goto out_free;
    }

    while (worker_running(sa)) {
        int     rcvd = 0;
        int     more = 0;
        size_t  more_len = sizeof(more);

        do {
            rc = zmq_recv(zrcv, buff + rcvd, RECVBUF_SIZE - rcvd, 0);
            if (rc < 0) {
                rc = -errno;
                lcaplog_err("Worker RCV error: %s", zmq_strerror(-rc));
                break;
            }

            rcvd += rc;

            rc = zmq_getsockopt(zrcv, ZMQ_RCVMORE, &more, &more_len);
            if (rc < 0) {
                rc = -errno;
                lcaplog_err("Worker GETSOCKOPT error: %s", zmq_strerror(-rc));
                break;
            }
        } while (more);

        rc = rpc_handler(&env, buff, rcvd);
        if (rc < 0)
            lcaplog_nfo("Received invalid RPC: %s", strerror(-rc));
    }

out_free:
    free(buff);

out:
    if (zrcv)
        zmq_close(zrcv);

    rc = cpm_destroy(ctx, env.we_mptr);
    if (rc)
        lcaplog_err("Cannot release module properly: %s", strerror(-rc));

    return NULL;
}

