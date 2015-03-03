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

#include "lcapd_internal.h"

#include <lcap_idl.h>

/**
 * Number of seconds to wait between two retries at the end of the
 * changelog records stream.
 */
#define EOF_RETRY_DELAY 1


extern int TerminateSig;


struct lcap_rec_bucket {
    unsigned long long       lrb_index;
    struct list_node         lrb_node;
    int                      lrb_rec_count; /**< Number of records */
    size_t                   lrb_size;      /**< Aggregated record size */
    struct changelog_rec    *lrb_records[]; /**< Pointers to the records */
};

struct reader_stats {
    struct timeval  rs_start_time;  /**< Start time */
    long            rs_rec_read;    /**< Number of read records */
    long            rs_rec_sent;    /**< Number of sent records */
};

struct client_state {
    long long                cs_start;  /**< Client start record number */
    struct list_node         cs_node;   /**< List node in env::re_peers */
    struct lcap_rec_bucket  *cs_bucket; /**< Currently processed bucket */
    struct conn_id          *cs_ident;  /**< Variable length, keep last */
};

struct reader_env {
    const struct lcap_cfg   *re_cfg;     /**< Global configuration (shared) */
    void                    *re_clpriv;  /**< LLAPI private changelog info */
    void                    *re_zctx;    /**< Local ZMQ context */
    void                    *re_sock;    /**< Records publication socket */
    struct conn_id          *re_ident;   /**< This reader connection identity */
    struct reader_stats      re_stats;   /**< Reader statistics/metrics */
    int                      re_index;   /**< Reader index (one per MDT) */
    long long                re_srec;    /**< Next start index */
    long                     re_rec_cnt; /**< Total count of records */
    struct list              re_buckets; /**< Linked list of buckets */
    struct list              re_peers;   /**< Linked list of client states */
};


/**
 * Copy and return a connection ID.
 * Return NULL if memory could not be allocated.
 */
static struct conn_id *conn_id_dup(const struct conn_id *cid)
{
    struct conn_id *dup;

    dup = malloc(sizeof(*dup) + cid->ci_length);
    if (dup != NULL) {
        dup->ci_length = cid->ci_length;
        memcpy(dup->ci_data, cid->ci_data, cid->ci_length);
    }

    return dup;
}

/**
 * Indicate whether the reader as described by \a env is full or still has
 * available slots to store records. */
static inline bool changelog_reader_full(const struct reader_env *env)
{
    const struct lcap_cfg   *cfg = env->re_cfg;

    return env->re_rec_cnt >= cfg->ccf_rec_batch_count * cfg->ccf_max_bkt;
}

/**
 * Return the ASCIIZ string naming the MDT device this reader (as described by
 * \a env) is attached to.
 */
static inline const char *reader_device(const struct reader_env *env)
{
    return env->re_cfg->ccf_mdt[env->re_index];
}

/**
 * Readers are named after the MDT device they are attached to. Fill and store
 * a connection_id structure accordingly. This is used for identify ourselves
 * to the broker.
 */
static int build_reader_identity(struct reader_env *env)
{
    const char  *dev = reader_device(env);
    size_t       dev_len = strlen(dev);

    env->re_ident = malloc(sizeof(*env->re_ident) + dev_len);
    if (env->re_ident == NULL)
        return -ENOMEM;

    memcpy(env->re_ident->ci_data, dev, dev_len);
    env->re_ident->ci_length = dev_len;
    return 0;
}

/**
 * Associate an identity to the reader described by \a env.
 * An identity is a unique identifier used to route messages to the
 * other components appropriately.
 */
static int changelog_reader_set_identity(struct reader_env *env)
{
    int rc;

    rc = build_reader_identity(env);
    if (rc < 0)
        return rc;

    rc = zmq_setsockopt(env->re_sock, ZMQ_IDENTITY, env->re_ident->ci_data,
                        env->re_ident->ci_length);
    if (rc < 0)
        return -errno;

    return 0;
}

/**
 * Try to initialize a changelog reader thread.
 * A reader is given an index by the main lcapd process, which indicates
 * which MDT to follow from the ones specified in the configuration.
 */
static int changelog_reader_init(const struct lcap_cfg *cfg, unsigned int idx,
                                 struct reader_env *env)
{
    int rc;

    memset(env, 0, sizeof(*env));
    env->re_cfg   = cfg;
    env->re_index = idx;
    gettimeofday(&env->re_stats.rs_start_time, NULL);

    env->re_zctx = zmq_ctx_new();
    if (env->re_zctx == NULL) {
        rc = -errno;
        lcap_error("Cannot initialize ZMQ context: %s", zmq_strerror(-rc));
        return rc;
    }

    env->re_sock = zmq_socket(env->re_zctx, ZMQ_DEALER);
    if (env->re_sock == NULL) {
        rc = -errno;
        lcap_error("Cannot open changelog publication socket: %s",
                   zmq_strerror(-rc));
        return rc;
    }

    rc = changelog_reader_set_identity(env);
    if (rc < 0) {
        lcap_error("Cannot set identity to '%s': %s",
                   (char *)env->re_ident->ci_data, zmq_strerror(-rc));
        return rc;
    }

    rc = zmq_connect(env->re_sock, BROKER_CONN_URL);
    if (rc < 0) {
        rc = -errno;
        lcap_error("Cannot connect to %s: %s", BROKER_CONN_URL,
                   zmq_strerror(-rc));
        return rc;
    }

    lcap_verb("Ready to distribute records to %s", BROKER_CONN_URL);
    return 0;
}

/**
 * Poke the broker, and give him information about the health of the current
 * reader as described by \env.
 */
static int changelog_reader_signal(struct reader_env *env, int errcode)
{
    struct px_rpc_signal    rpc;
    int                     rc;

    memset(&rpc, 0, sizeof(rpc));
    rpc.pr_hdr.op_type = RPC_OP_SIGNAL;
    rpc.pr_ret         = (uint64_t)errcode;

    strcpy((char *)rpc.pr_mdtname, reader_device(env));

    rc = zmq_send(env->re_sock, "", 0, ZMQ_SNDMORE);
    if (rc < 0) {
        rc = -errno;
        lcap_error("Cannot send signal RPC: %s", zmq_strerror(-rc));
        return rc;
    }

    rc = zmq_send(env->re_sock, (void *)rpc.pr_mdtname,
                  strlen((char *)rpc.pr_mdtname), ZMQ_SNDMORE);
    if (rc < 0) {
        rc = -errno;
        lcap_error("Cannot send signal RPC: %s", zmq_strerror(-rc));
        return rc;
    }

    rc = zmq_send(env->re_sock, (void *)&rpc, sizeof(rpc), 0);
    if (rc < 0) {
        rc = -errno;
        lcap_error("Cannot send signal RPC: %s", zmq_strerror(-rc));
        return rc;
    }

    return 0;
}

/**
 * Display information gathered during operation times.
 */
static int changelog_reader_print_stats(struct reader_env *env)
{
    struct reader_stats *rstats = &env->re_stats;
    const char          *device = reader_device(env);
    struct timeval       end;
    int                  duration;
    double               processing_rate;

    gettimeofday(&end, NULL);

    duration = end.tv_sec - rstats->rs_start_time.tv_sec;
    duration *= 1000;
    duration += (end.tv_usec / 1000) - (rstats->rs_start_time.tv_usec / 1000);

    if (duration == 0)
        processing_rate = 0.0;
    else
        processing_rate = (double)rstats->rs_rec_read / duration;

    lcap_info("%ld records processed from %s (%d/s)", rstats->rs_rec_read,
              device, (int)(processing_rate * 1000));
    return 0;
}

/**
 * Release resources associated to the current changelog reader as described by
 * \a env. Print out statistics eventually.
 */
static int changelog_reader_release(struct reader_env *env)
{
    int rc;

    if (env->re_sock != NULL) {
        zmq_close(env->re_sock);
        env->re_sock = NULL;
    }

    if (env->re_zctx != NULL) {
        zmq_ctx_destroy(env->re_zctx);
        env->re_zctx = NULL;
    }

    rc = changelog_reader_print_stats(env);
    if (rc < 0)
        return rc;

    return 0;
}

/**
 * Extract the next bucket of records to be served from \a env.
 * This function returns NULL if no bucket was available.
 */
static inline struct lcap_rec_bucket *rec_bucket_pop(struct reader_env *env)
{
    struct list_node        *n = list_pop_head(&env->re_buckets);
    struct lcap_rec_bucket  *bkt;

    if (n == NULL)
        return NULL;

    bkt = container_of(n, struct lcap_rec_bucket, lrb_node);
    env->re_rec_cnt -= bkt->lrb_rec_count;

    return bkt;
}

/**
 * Get a pointer (without unlinking it from \a env) to the last record bucket.
 */
static inline struct lcap_rec_bucket *rec_bucket_get(struct reader_env *env)
{
    struct list_node *n = env->re_buckets.l_last;

    if (n == NULL)
        return NULL;

    return container_of(n, struct lcap_rec_bucket, lrb_node);
}

/**
 * Allocate and insert a new, empty, bucket to \a env.
 */
static int rec_bucket_add(struct reader_env *env, int slot_cnt)
{
    struct lcap_rec_bucket  *bkt;
    size_t                   bkt_sz = sizeof(*bkt) +
                                      slot_cnt * sizeof(struct changelog_rec *);

    bkt = (struct lcap_rec_bucket *)calloc(1, bkt_sz);
    if (bkt == NULL)
        return -ENOMEM;

    list_append(&env->re_buckets, &bkt->lrb_node);
    return 0;
}

/**
 * Release a bucket and all records it contains.
 */
static void rec_bucket_destroy(struct lcap_rec_bucket *bkt)
{
    int i;

    for (i = 0; i < bkt->lrb_rec_count; i++)
        llapi_changelog_free(&bkt->lrb_records[i]);

    free(bkt);
}

/**
 * Insert a new changelog_record into the reader's cache.
 */
static int changelog_reader_rec_store(struct reader_env *env,
                                      struct changelog_rec *rec)
{
    struct lcap_rec_bucket  *current = rec_bucket_get(env);
    int                      batch_size;
    int                      idx;
    int                      rc;

    batch_size = env->re_cfg->ccf_rec_batch_count;
    if (current == NULL || current->lrb_rec_count == batch_size) {
        rc = rec_bucket_add(env, batch_size);
        if (rc)
            return rc;
        current = rec_bucket_get(env);
    }

    assert(current != NULL);
    assert(current->lrb_rec_count < batch_size);

    idx = current->lrb_rec_count++;
    current->lrb_records[idx] = rec;
    current->lrb_size += changelog_rec_size(rec) + rec->cr_namelen;
    lcap_debug("Inserted record #%llu into current bucket at %d",
               rec->cr_index, idx);
    return 0;
}

/**
 * Get the client descriptor for the peer identified by \a cid. For this
 * function to succeed and not return NULL, the corresponding peer must
 * have registered itself using RPC_OP_START already.
 *
 * XXX This O(N) complexity is miserable here.
 */
static struct client_state *client_state_get(struct reader_env *env,
                                             const struct conn_id *cid)
{
    struct list_node    *lnode;
    struct conn_id      *cid_curr;
    struct client_state *cs;

    for (lnode = env->re_peers.l_first; lnode != NULL; lnode = lnode->ln_next) {
        cs = container_of(lnode, struct client_state, cs_node);
        cid_curr = cs->cs_ident;

        if (cid_curr->ci_length != cid->ci_length)
            continue;

        if (memcmp(cid_curr->ci_data, cid->ci_data, cid->ci_length) == 0)
            return cs;
    }

    return NULL;
}

/**
 * Free resources associated to a client state. It is assumed that the structure
 * has already been unlinked from list.
 */
static void client_state_release(struct client_state *cs)
{
    free(cs->cs_ident);
    free(cs);
}

/**
 * Process START message from client. Registration consists in creating a new
 * client state structure and replying OK.
 */
static int reader_handle_start(struct reader_env *env,
                               const struct lcapnet_request *req)
{
    struct px_rpc_register  *rpc = (struct px_rpc_register *)req->lr_body;
    struct client_state     *cs;
    int                      rc;

    if (req->lr_body_len < sizeof(*rpc)) {
        lcap_error("Truncated START RPC of size %zd", req->lr_body_len);
        return -EINVAL;
    }

    cs = client_state_get(env, req->lr_forward);
    if (cs != NULL) {
        lcap_info("Received START RPC for already registered client");
        return -EALREADY;
    }

    cs = calloc(1, sizeof(*cs));
    if (cs == NULL) {
        rc = -ENOMEM;
        lcap_error("Cannot initialize client context: %s", strerror(-rc));
        return rc;
    }

    cs->cs_start = rpc->pr_start;
    cs->cs_ident = conn_id_dup(req->lr_forward);
    if (cs->cs_ident == NULL) {
        free(cs);
        rc = -ENOMEM;
        lcap_error("Cannot populate client context: %s", strerror(-rc));
        return rc;
    }

    list_append(&env->re_peers, &cs->cs_node);

    rc = ack_retcode(env->re_sock, NULL, req->lr_forward, 0);
    if (rc < 0) {
        lcap_error("Cannot ACK: %s", zmq_strerror(-rc));
        return rc;
    }

    return 0;
}

/**
 * Pack and deliver a RPC_OP_ENQUEUE message to a client.
 * This consists in giving this client temporary ownership of a bucket of
 * records, and sending them.
 */
static int enqueue_rec(struct reader_env *env, struct client_state *cs,
                       const struct lcapnet_request *req)
{
    struct px_rpc_enqueue   *rpc;
    size_t                   rpc_size;
    uint8_t                 *rpc_next_rec;
    int                      i;
    int                      rc;

    rpc_size = sizeof(*rpc) + cs->cs_bucket->lrb_size;
    rpc = calloc(1, rpc_size);
    if (rpc == NULL)
        return -ENOMEM;

    rpc->pr_hdr.op_type = RPC_OP_ENQUEUE;
    rpc->pr_count       = cs->cs_bucket->lrb_rec_count;

    rpc_next_rec = rpc->pr_records;
    for (i = 0; i < rpc->pr_count; i++) {
        struct changelog_rec    *rec = cs->cs_bucket->lrb_records[i];
        size_t                   copy_len = changelog_rec_size(rec) +
                                            rec->cr_namelen;

        memcpy(rpc_next_rec, rec, copy_len);
        rpc_next_rec += copy_len;
    }

    rc = peer_rpc_send(env->re_sock, NULL, req->lr_forward, (const char *)rpc,
                       rpc_size);
    free(rpc);
    return rc;
}

/**
 * Process a request for records from client. If valid, and if records are
 * currently available, they will be delivered immediately.
 */
static int reader_handle_dequeue(struct reader_env *env,
                                 const struct lcapnet_request *req)
{
    struct px_rpc_dequeue   *rpc = (struct px_rpc_dequeue *)req->lr_body;
    struct client_state     *cs;
    struct lcap_rec_bucket  *bkt;

    if (req->lr_body_len < sizeof(*rpc)) {
        lcap_error("Truncated DEQUEUE RPC, ignoring");
        return -EPROTO;
    }

    cs = client_state_get(env, req->lr_forward);
    if (cs == NULL) {
        lcap_info("Out of context CLEAR RPC, ignoring");
        return -EPROTO;
    }

    if (cs->cs_bucket != NULL) {
        lcap_info("Client did not acknowledge bucket #%llu",
                  cs->cs_bucket->lrb_index);
        return -EPROTO;
    }

    bkt = rec_bucket_pop(env);
    if (bkt == NULL)
        return 1;   /* EOF */

    /* From now on, this bucket belongs to the corresponding client,
     * until ack or timeout occurs */
    cs->cs_bucket = bkt;

    return enqueue_rec(env, cs, req); /* There you go! */
}

/**
 * Process RPC_OP_CLEAR request.
 * XXX This definitely needs more love.
 */
static int reader_handle_clear(struct reader_env *env,
                               const struct lcapnet_request *req)
{
    struct px_rpc_clear *rpc = (struct px_rpc_clear *)req->lr_body;
    struct client_state *cs;
    //const char          *dev = reader_device(env);
    //int                  rc;

    if (req->lr_body_len < sizeof(*rpc)) {
        lcap_error("Truncated CLEAR RPC of size %zd", req->lr_body_len);
        return -EPROTO;
    }

    cs = client_state_get(env, req->lr_forward);
    if (cs == NULL) {
        lcap_info("Out of context CLEAR RPC, ignoring");
        return -EPROTO;
    }

    if (cs->cs_bucket == NULL) {
        lcap_info("No bucket associated to context, nothing to clear");
        return 0;   /* Fine... */
    }

    rec_bucket_destroy(cs->cs_bucket);
    cs->cs_bucket = NULL;

#if 0
    rc = llapi_changelog_clear(dev, "cl1", (long long)rpc->pr_index);
    if (rc < 0) {
        lcap_error("Cannot clear changelog records "
                    "(device='%s', reader='%s', rec=%lld): %s",
                    dev, "cl1", (long long)rpc->pr_index, strerror(-rc));
        return rc;
    }
#endif

    return ack_retcode(env->re_sock, NULL, req->lr_forward, 0);
}

/**
 * Handle RPC_OP_FINI (deregistration) message. Release and forget all resources
 * associated to a client.
 */
static int reader_handle_fini(struct reader_env *env,
                              const struct lcapnet_request *req)
{
    struct px_rpc_fini *rpc = (struct px_rpc_fini *)req->lr_body;
    struct client_state *cs;

    if (req->lr_body_len < sizeof(*rpc)) {
        lcap_error("Truncated FINI RPC of size %zd", req->lr_body_len);
        return -EPROTO;
    }

    cs = client_state_get(env, req->lr_forward);
    if (cs == NULL) {
        lcap_info("Out of context FINI RPC, ignoring");
        return -EPROTO;
    }

    list_remove(&env->re_peers, &cs->cs_node);
    client_state_release(cs);

    return ack_retcode(env->re_sock, NULL, req->lr_forward, 0);
}

/**
 * Array of RPC handler for the LCAPD reader.
 */
int (*reader_rpc_handle[])(struct reader_env *,
                           const struct lcapnet_request *) = {
    [RPC_OP_START]      = reader_handle_start,
    [RPC_OP_DEQUEUE]    = reader_handle_dequeue,
    [RPC_OP_CLEAR]      = reader_handle_clear,
    [RPC_OP_FINI]       = reader_handle_fini,
    [RPC_OP_ENQUEUE]    = NULL,
    [RPC_OP_ACK]        = NULL,
    [RPC_OP_SIGNAL]     = NULL,
};


static inline int rpc_handle_one(struct reader_env *env,
                                 enum rpc_op_type op_type,
                                 const struct lcapnet_request *req)
{
    if (reader_rpc_handle[op_type] == NULL) {
        lcap_error("Received unexpected message (code=%d)", op_type);
        return -EPROTO;
    }

    return reader_rpc_handle[op_type](env, req);
}

/**
 * MDT Reader main RPC handing callback.
 */
static int changelog_reader_rpc_hdl(void *hint,
                                    const struct lcapnet_request *req)
{
    struct reader_env       *env = (struct reader_env *)hint;
    const struct px_rpc_hdr *hdr = req->lr_body;
    size_t                   hlen = req->lr_body_len;
    int                      rc;

    if (hlen < sizeof(*hdr)) {
        rc = -EPROTO;
        lcap_error("Received truncated/invalid RPC of size: %zu", hlen);
        goto out_reply;
    }

    if (hdr->op_type < RPC_OP_FIRST || hdr->op_type > RPC_OP_LAST) {
        rc = -EINVAL;
        lcap_error("Received RPC with invalid opcode: %d\n", hdr->op_type);
        goto out_reply;
    }

    rc = rpc_handle_one(env, hdr->op_type, req);

out_reply:
    lcap_verb("Received %s RPC [rc=%d | %s]", rpc_optype2str(hdr->op_type),
              rc, zmq_strerror(-rc));

    /* Error or DEQUEUE EOF */
    if (rc < 0 || rc == 1)
        rc = ack_retcode(env->re_sock, NULL, req->lr_forward, rc);

    return rc;
}

/**
 * Process messages from broker/clients
 */
static int changelog_reader_serve(struct reader_env *env)
{
    int             rc;
    int             timeout = env->re_clpriv ? 50 : 1000;
    zmq_pollitem_t  itm[] = {{env->re_sock, 0, ZMQ_POLLIN, 0}};

    rc = zmq_poll(itm, 1, timeout);
    if (rc <= 0) {
        //lcap_debug("Nothing received (%s)", zmq_strerror(rc));
        return rc;
    }

    assert(itm[0].revents & ZMQ_POLLIN);

    rc = lcap_rpc_recv(env->re_sock, LCAP_RECV_NONBLOCK | LCAP_RECV_NO_ENVELOPE,
                       changelog_reader_rpc_hdl, env);
    if (rc < 0)
        return rc;

    lcap_info("Processed %d client RPCs", rc);
    return 0;
}

/**
 * Enqueue records using the (unfortunately) blocking LLAPI interface.
 */
static int changelog_reader_enqueue(struct reader_env *env)
{
    const char              *device = reader_device(env);
    int                      flags  = CHANGELOG_FLAG_JOBID;
    int                      batch_count = 0;
    int                      batch_size;
    struct changelog_rec    *rec;
    int                      rc;

    if (changelog_reader_full(env))
        return 0;

    batch_size = env->re_cfg->ccf_rec_batch_count;

    if (env->re_clpriv == NULL) {
        rc = llapi_changelog_start(&env->re_clpriv, flags, device,
                                   env->re_srec);
        if (rc) {
            lcap_error("Cannot start changelog on %s: %s", device,
                        strerror(-rc));
            return rc;
        }
    }

    while ((rc = llapi_changelog_recv(env->re_clpriv, &rec)) == 0) {
        if (TerminateSig)
            break;

        if (rec->cr_index < env->re_srec)
            continue;

        rc = changelog_reader_rec_store(env, rec);
        if (rc)
            break;

        env->re_srec = rec->cr_index + 1;
        env->re_stats.rs_rec_read++;
        env->re_rec_cnt++;
        batch_count++;

        if (batch_count > batch_size || changelog_reader_full(env))
            break;
    }

    /* EOF: llapi_changelog_start() needed on next iteration. */
    if (rc == 1 || rc == -EAGAIN || rc == -EPROTO) {
        llapi_changelog_fini(&env->re_clpriv);
        env->re_clpriv = NULL;
    }

    return rc;
}

/**
 * Changelog reader entry point.
 * Initialize context accordingly, then switch between remote messages
 * processing and local caching of changelog records. Both operations cannot
 * (easily) be put in a single event loop because of the LLAPI changelog
 * interface being blocking...
 */
void *reader_main(void *args)
{
    struct subtask_args *sa = (struct subtask_args *)args;
    struct reader_env    env;
    int                  rc;

    rc = changelog_reader_init(sa->sa_cfg, sa->sa_idx, &env);
    if (rc)
        goto out;

    rc = changelog_reader_signal(&env, 0);
    if (rc)
        goto out;

    while (!TerminateSig) {
        /* Enqueue records, if possible */
        rc = changelog_reader_enqueue(&env);
        if (rc < 0)
            break;

        /* Serve workers for 50ms (or 1s if EOF was reached) */
        rc = changelog_reader_serve(&env);
        if (rc < 0)
            break;
    }

out:
    lcap_debug("ChangeLog reader #%d stopping with rc=%d: %s",
               sa->sa_idx, rc, zmq_strerror(-rc));

    /* try to warn the broker if possible */
    if (rc)
        changelog_reader_signal(&env, rc);

    changelog_reader_release(&env);
    free(args);
    return NULL;
}
