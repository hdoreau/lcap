/*
 * LCAP - Lustre Changelogs Aggregate and Publish
 *
 * Copyright (C)  2013-2015  CEA/DAM
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
#include <malloc.h>
#include <signal.h>
#include <time.h>

#include "lcapd_internal.h"


#define abs(x)  ((x) < 0 ? -(x) : (x))

/**
 * Signal handling flags.
 */
extern int TerminateSig;


static int changelog_reader_register(struct lcap_ctx *ctx, const char *mdt,
                                     const struct conn_id *cid)
{
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    struct conn_id          *cid_dup;
    int                      i;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        if (strcmp(mdt, cfg->ccf_mdt[i]) == 0) {
            cid_dup = (struct conn_id *)malloc(sizeof(*cid) + cid->ci_length);
            if (cid_dup == NULL)
                return -ENOMEM;
            memcpy(cid_dup, cid, sizeof(*cid) + cid->ci_length);
            ctx->cc_rcid[i] = cid_dup;
            lcap_debug("Registered changelog reader for device '%s' at #%d",
                        cfg->ccf_mdt[i], i);
            return 0;
        }
    }
    lcap_error("Received unexpected registration RPC for MDT %s", mdt);
    return -EINVAL;
}

static int changelog_reader_deregister(struct lcap_ctx *ctx,
                                       const struct conn_id *cid)
{
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    int                      i;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        if (ctx->cc_rcid[i] != NULL &&
            cid_compare(ctx->cc_rcid[i], cid) == 0) {
            free(ctx->cc_rcid[i]);
            ctx->cc_rcid[i] = NULL;
            lcap_debug("Deregistered changelog reader #%d", i);
            break;
        }
    }
    return 0;
}

static int broker_reader_send(struct lcap_ctx *ctx,
                              const struct lcapnet_request *req)
{
    enum rpc_op_type    op_type = req->lr_body->op_type;
    size_t              expected_len = rpc_expected_length(op_type);

    if (req->lr_body_len < expected_len)
        return -EINVAL;

    lcap_debug("Forwarding %s message to reader '%.*s'",
               rpc_optype2str(op_type),
               (int)req->lr_forward->ci_length,
               (const char *)req->lr_forward->ci_data);

    return peer_rpc_send(ctx->cc_sock, req->lr_forward, req->lr_remote,
                         (const char *)req->lr_body, req->lr_body_len);
}

static int broker_client_send(struct lcap_ctx *ctx,
                              const struct lcapnet_request *req)
{
    return peer_rpc_send(ctx->cc_sock, NULL, req->lr_forward,
                         (const char *)req->lr_body, req->lr_body_len);
}

static int broker_handle_signal(struct lcap_ctx *ctx,
                                const struct lcapnet_request *req)
{
    const struct px_rpc_signal  *rpc;
    int                          rc = 0;

    if (req->lr_body_len < sizeof(*rpc))
        return -EINVAL;

    rpc = (const struct px_rpc_signal *)req->lr_body;
    if (rpc->pr_ret == 0) {
        rc = changelog_reader_register(ctx, (const char *)rpc->pr_mdtname,
                                       req->lr_remote);
    } else {
        changelog_reader_deregister(ctx, req->lr_remote);
        lcap_error("Reader started but failed with error: %s",
                   zmq_strerror(-(int)rpc->pr_ret));
    }

    return rc;
}

/**
 * Array of RPC handler for the LCAPD broker.
 */
int (*broker_rpc_handle[])(struct lcap_ctx *,
                           const struct lcapnet_request *) = {
    [RPC_OP_START]      = broker_reader_send,
    [RPC_OP_DEQUEUE]    = broker_reader_send,
    [RPC_OP_CLEAR]      = broker_reader_send,
    [RPC_OP_FINI]       = broker_reader_send,
    [RPC_OP_ENQUEUE]    = broker_client_send,
    [RPC_OP_ACK]        = broker_client_send,
    [RPC_OP_SIGNAL]     = broker_handle_signal
};

static inline int rpc_handle_one(struct lcap_ctx *ctx, enum rpc_op_type op_type,
                                 const struct lcapnet_request *req)
{
    if (broker_rpc_handle[op_type] == NULL) {
        lcap_error("Received unexpected message (code=%d)", op_type);
        return -EPROTO;
    }

    return broker_rpc_handle[op_type](ctx, req);
}

int lcapd_process_request(void *hint, const struct lcapnet_request *req)
{
    struct lcap_ctx     *ctx = (struct lcap_ctx *)hint;
    struct px_rpc_hdr   *hdr = req->lr_body;
    size_t               msg_len = req->lr_body_len;
    int                  rc = 0;

    if (msg_len < sizeof(*hdr)) {
        rc = -EPROTO;
        lcap_error("Received truncated/invalid RPC of size: %zu", msg_len);
        goto out_reply;
    }

    if (hdr->op_type < RPC_OP_FIRST || hdr->op_type > RPC_OP_LAST) {
        rc = -EINVAL;
        lcap_error("Received RPC with invalid opcode: %d\n", hdr->op_type);
        goto out_reply;
    }

    rc = rpc_handle_one(ctx, hdr->op_type, req);

out_reply:
    lcap_debug("Received %s RPC [rc=%d | %s]", rpc_optype2str(hdr->op_type),
               rc, zmq_strerror(-rc));

    if (rc < 0)
        rc = ack_retcode(ctx->cc_sock, NULL, req->lr_remote, rc);

    return rc;
}
