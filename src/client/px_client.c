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


#include <lcap_client.h>

#include <stdlib.h>
#include <zmq.h>

#define DEFAULT_CACHE_SIZE  256


struct px_zmq_data {
    void                     *zmq_ctx;  /**< 0MQ context */
    void                     *zmq_srv;  /**< Socket to server */
    void                     *rec_buff; /**< RPC buffer containing records */
    struct changelog_rec    **records;  /**< Undelivered (cached) records */
    long long                 rec_nxt;  /**< Next record to read */
    long long                 rec_cnt;  /**< High watermark */
    int                       rec_mdt_len;
    char                      rec_mdt[128];
};

static int pzd_destroy(struct px_zmq_data *pzd)
{
    if (pzd->zmq_srv != NULL)
        zmq_close(pzd->zmq_srv);

    if (pzd->zmq_ctx != NULL)
        zmq_ctx_destroy(pzd->zmq_ctx);

    free(pzd->rec_buff);
    free(pzd->records);
    memset(pzd, 0, sizeof(*pzd));
    return 0;
}

static int pzd_cache_grow(struct px_zmq_data *pzd, size_t newsize)
{
    newsize *= sizeof(struct changelog_rec *);
    pzd->records = (struct changelog_rec **)realloc(pzd->records, newsize);
    if (pzd->records == NULL)
        return -ENOMEM;

    return 0;
}

static int pzd_init(struct px_zmq_data *pzd, const char *mdtname)
{
    int rc;

    pzd->zmq_ctx = zmq_ctx_new();
    if (pzd->zmq_ctx == NULL) {
        rc = -errno;
        goto err_cleanup;
    }

    pzd->zmq_srv = zmq_socket(pzd->zmq_ctx, ZMQ_REQ);
    if (pzd->zmq_srv == NULL) {
        rc = -errno;
        goto err_cleanup;
    }

    pzd->rec_cnt  = 0;
    pzd->rec_nxt  = 0;
    pzd->rec_buff = NULL;
    pzd->records  = NULL;

    pzd->rec_mdt_len = strlen(mdtname);
    if (pzd->rec_mdt_len > sizeof(pzd->rec_mdt)) {
        rc = -EINVAL;
        goto err_cleanup;
    }

    strcpy(pzd->rec_mdt, mdtname);

    rc = pzd_cache_grow(pzd, DEFAULT_CACHE_SIZE);
    if (rc < 0)
        goto err_cleanup;

err_cleanup:
    if (rc)
        pzd_destroy(pzd);

    return rc;
}

static const char *px_rec_uri(void)
{
    return "tcp://localhost:8189";
    //return secure_getenv(LCAP_REC_URI);
}

static int cl_start_pack(struct px_rpc_register *msg, int flags,
                         const char *mdtname, long long startrec)
{
    memset(msg, 0, sizeof(*msg));
    msg->pr_hdr.op_type = RPC_OP_START;
    msg->pr_start = startrec;
    msg->pr_flags = flags;
    strncpy((char *)msg->pr_mdtname, mdtname, sizeof(msg->pr_mdtname));
    return 0;
}

static int cl_dequeue_pack(struct px_rpc_dequeue *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->pr_hdr.op_type = RPC_OP_DEQUEUE;
    return 0;
}

static int cl_clear_pack(struct px_rpc_clear *msg, const char *mdtname,
                         const char *id, long long endrec)
{
    size_t id_len;
    size_t dev_len;

    msg->pr_hdr.op_type = RPC_OP_CLEAR;
    msg->pr_index = endrec;

    id_len = strlen(id);
    dev_len = strlen(mdtname);

    memcpy(msg->pr_id, id, id_len);
    memcpy(msg->pr_id + id_len + 1, mdtname, dev_len);
    msg->pr_id_len = id_len + dev_len + 2;
    return 0;
}

/**
 * Send a request to the server. The request is composed of two top frames:
 * A first one identifying the targetted MDT, a second one with the actual RPC
 * body.
 * The ZMQ_REQ socket we use will add another (envelope) one, but that's server
 * business...
 */
static int px_rpc_send(struct px_zmq_data *pzd, char *rpc, size_t rpc_size)
{
    int rc;

    rc = zmq_send(pzd->zmq_srv, pzd->rec_mdt, pzd->rec_mdt_len, ZMQ_SNDMORE);
    if (rc < 0)
        return -errno;

    rc = zmq_send(pzd->zmq_srv, rpc, rpc_size, 0);
    if (rc < 0)
        return -errno;

    return 0;
}

static int cl_rep_recv(struct px_zmq_data *pzd, char *buff, size_t bufflen)
{
    int     rc;
    int     rcvd = 0;
    int     more = 0;
    size_t  more_len = sizeof(more);

    do {
        rc = zmq_recv(pzd->zmq_srv, buff + rcvd, bufflen - rcvd, 0);
        if (rc < 0)
            return -errno;

        if (rc > bufflen - rcvd)
            return -EOVERFLOW;

        rcvd += rc;

        rc = zmq_getsockopt(pzd->zmq_srv, ZMQ_RCVMORE, &more, &more_len);
        if (rc < 0)
            return -errno;
    } while (more);

    return rcvd;
}

static int cl_ack_retcode(struct px_zmq_data *pzd)
{
    struct px_rpc_ack   rep_ack;
    int                 rc;

    rc = cl_rep_recv(pzd, (char *)&rep_ack, sizeof(rep_ack));
    if (rc < 0)
        return rc;

    if (rc < sizeof(rep_ack))
        return -EINVAL;

    return rep_ack.pr_retcode;
}

static int px_changelog_start(struct lcap_cl_ctx *ctx, enum lcap_cl_flags flags,
                              const char *mdtname, long long startrec)
{
    struct px_zmq_data      *pzd;
    struct px_rpc_register   reg;
    struct px_rpc_ack        ack;
    int                      rc = 0;

    pzd = calloc(1, sizeof(*pzd));
    if (pzd == NULL) {
        rc = -ENOMEM;
        goto out;
    }

    rc = pzd_init(pzd, mdtname);
    if (rc)
        goto out;

    ctx->ccc_ptr = (void *)pzd;

    rc = zmq_connect(pzd->zmq_srv, px_rec_uri());
    if (rc < 0) {
        rc = -errno;
        goto out_initialized;
    }

    rc = cl_start_pack(&reg, flags, mdtname, startrec);
    if (rc < 0)
        goto out_initialized;

    rc = px_rpc_send(pzd, (char *)&reg, sizeof(reg));
    if (rc < 0)
        goto out_initialized;

    rc = zmq_recv(pzd->zmq_srv, &ack, sizeof(ack), 0);
    if (rc < 0) {
        rc = -errno;
        goto out_initialized;
    }

    if (rc < sizeof(ack)) {
        rc = -EINVAL;
        goto out_initialized;
    }

    return ack.pr_retcode;

out_initialized:
    pzd_destroy(pzd);

out:
    free(pzd);
    ctx->ccc_ptr = NULL;

    return rc;
}

static int px_changelog_fini(struct lcap_cl_ctx *ctx)
{
    struct px_zmq_data  *pzd = (struct px_zmq_data *)ctx->ccc_ptr;
    struct px_rpc_fini   rpc;
    int                  rc;

    if (ctx == NULL)
        return -EINVAL;

    memset(&rpc, 0, sizeof(rpc));
    rpc.pr_hdr.op_type = RPC_OP_FINI;

    rc = px_rpc_send(pzd, (char *)&rpc, sizeof(rpc));
    if (rc < 0)
        return rc;

    rc = cl_ack_retcode(pzd);
    if (rc < 0)
        return rc;

    pzd_destroy((struct px_zmq_data *)ctx->ccc_ptr);
    return rc;
}

static inline struct changelog_rec * 
changelog_rec_next(struct changelog_rec *rec)
{
    return (struct changelog_rec *)(changelog_rec_name(rec) + rec->cr_namelen);
}

#define RECV_BUFFER_LENGTH  8 * 1024 * 1024
static int px_dequeue_records(struct px_zmq_data *pzd)
{
    char                    *buff;
    struct px_rpc_dequeue    rpc;
    struct px_rpc_hdr       *rep_hdr;
    int                      rc;
    int                      rcvd = 0;

    rc = cl_dequeue_pack(&rpc);
    if (rc < 0)
        return rc;

    rc = px_rpc_send(pzd, (char *)&rpc, sizeof(rpc));
    if (rc < 0)
        return rc;

    buff = (char *)malloc(RECV_BUFFER_LENGTH);
    if (buff == NULL)
        return -ENOMEM;

    rc = cl_rep_recv(pzd, buff, RECV_BUFFER_LENGTH);
    if (rc < 0)
        goto out_free;

    rcvd = rc;
    rc = 0;

    rep_hdr = (struct px_rpc_hdr *)buff;
    if (rcvd < sizeof(*rep_hdr)) {
        rc = -EINVAL;
        goto out_free;
    }

    switch (rep_hdr->op_type) {
        case RPC_OP_ACK: {
            struct px_rpc_ack   *rep_ack;

            rep_ack = (struct px_rpc_ack *)buff;
            if (rcvd < sizeof(*rep_ack)) {
                rc = -EINVAL;
                goto out_free;
            }
            rc = rep_ack->pr_retcode;
            break;
        }

        case RPC_OP_ENQUEUE: {
            struct px_rpc_enqueue   *rep_enq;
            struct changelog_rec    *rec_iter;
            int                      i;

            rep_enq = (struct px_rpc_enqueue *)buff;
            if (rcvd < (sizeof(*rep_enq) +
                        sizeof(*rec_iter))) {
                rc = -EINVAL;
                goto out_free;
            }

            if (rep_enq->pr_count > pzd->rec_cnt) {
                rc = pzd_cache_grow(pzd, rep_enq->pr_count);
                if (rc < 0)
                    goto out_free;
            }

            rec_iter = (struct changelog_rec *)rep_enq->pr_records;
            for (i = 0; i < rep_enq->pr_count; i++) {
                pzd->records[i] = (struct changelog_rec *)rec_iter;
                rec_iter = changelog_rec_next(rec_iter);
                /* XXX check offset validity */
            }
            pzd->rec_nxt  = 0;
            pzd->rec_cnt  = i;
            pzd->rec_buff = buff;
            rc = 0;
            break;
        }

        default:
            rc = -EPROTO;
            goto out_free;
    }

out_free:
    if (rc)
        free(buff);

    return rc;
}

static int px_changelog_recv(struct lcap_cl_ctx *ctx,
                             struct changelog_rec **rec)
{
    struct px_zmq_data  *pzd = (struct px_zmq_data *)ctx->ccc_ptr;
    int                  rc;

    if (pzd->rec_nxt == pzd->rec_cnt) {
        rc = px_dequeue_records(pzd);
        if (rc != 0)
            return rc; /* <0 or >0 are both possible */
    }

    *rec = pzd->records[pzd->rec_nxt++];
    return 0;
}


static int px_changelog_free(struct lcap_cl_ctx *ctx,
                             struct changelog_rec **rec)
{
    struct px_zmq_data  *pzd = (struct px_zmq_data *)ctx->ccc_ptr;

    if (pzd->rec_nxt == pzd->rec_cnt) {
        free(pzd->rec_buff);
        pzd->rec_buff = NULL;
    }
    *rec = NULL;
    return 0;
}

static int px_changelog_clear(struct lcap_cl_ctx *ctx, const char *mdtname,
                              const char *id, long long endrec)
{
    struct px_zmq_data  *pzd = (struct px_zmq_data *)ctx->ccc_ptr;
    struct px_rpc_clear *rpc;
    size_t               id_len;
    size_t               name_len;
    size_t               rpc_len;
    int                  rc;

    if (pzd->rec_nxt < pzd->rec_cnt)
        return 0;

    id_len = strlen(id);
    name_len = strlen(mdtname);

    rpc_len = sizeof(*rpc) + id_len + name_len + 2;
    rpc = (struct px_rpc_clear *)calloc(1, rpc_len);
    if (rpc == NULL)
        return -ENOMEM;

    rc = cl_clear_pack(rpc, mdtname, id, endrec);
    if (rc)
        goto out_free;

    rc = px_rpc_send(pzd, (char *)rpc, rpc_len);
    if (rc < 0)
        goto out_free;

    rc = cl_ack_retcode(pzd);

out_free:
    free(rpc);

    return rc;
}

struct lcap_cl_operations cl_ops_proxy = {
    .cco_start  = px_changelog_start,
    .cco_fini   = px_changelog_fini,
    .cco_recv   = px_changelog_recv,
    .cco_free   = px_changelog_free,
    .cco_clear  = px_changelog_clear
};
