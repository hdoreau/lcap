/*
 * LCAP - Lustre Changelogs Aggregate and Publish
 *
 * Copyright (C)  2015  CEA/DAM
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
#include <stdbool.h>

#include <zmq.h>

#include <lcap_net.h>
#include <lcap_idl.h>
#include <lcap_log.h>


static struct conn_id *connection_id_new(void *bytes, size_t len)
{
    struct conn_id  *cid;

    cid = (struct conn_id *)malloc(sizeof(*cid) + len);
    if (cid != NULL) {
        cid->ci_length = len;
        memcpy(cid->ci_data, bytes, len);
    }

    return cid;
}

static int lcapnet_req_init(struct lcapnet_request **req)
{
    *req = calloc(1, sizeof(struct lcapnet_request));
    if (*req == NULL)
        return -ENOMEM;

    return 0;
}

static void lcapnet_req_release(struct lcapnet_request *req)
{
    if (req == NULL)
        return;

    free(req->lr_remote);
    free(req->lr_forward);
    free(req->lr_body);
}

static int lcapnet_req_update(struct lcapnet_request *req, zmq_msg_t *zmsg)
{
    size_t   frame_len = zmq_msg_size(zmsg);
    void    *frame_bytes = zmq_msg_data(zmsg);

    /* Ignore delimiters */
    if (frame_len == 0)
        return 0;

    if (req->lr_remote == NULL && !req->lr_flags & LCAP_RECV_NO_ENVELOPE) {
        req->lr_remote = connection_id_new(frame_bytes, frame_len);
        return req->lr_remote ? 0 : -ENOMEM;
    }

    if (req->lr_forward == NULL) {
        req->lr_forward = connection_id_new(frame_bytes, frame_len);
        return req->lr_forward ? 0 : -ENOMEM;
    }

    req->lr_body = realloc(req->lr_body, req->lr_body_len + frame_len);
    if (req->lr_body == NULL)
        return -ENOMEM;

    memcpy((char *)req->lr_body + req->lr_body_len, frame_bytes, frame_len);
    req->lr_body_len += frame_len;
    return 0;
}

static int lcap_rpc_recv_once(void *zsock, int flags, lcap_rpc_hdl_t cb,
                              void *hint)
{
    struct lcapnet_request  *req;
    int                      zmq_flags = 0;
    int                      rc;

    rc = lcapnet_req_init(&req);
    if (rc < 0) {
        lcap_error("Cannot initialize lcapnet descriptor: %s", strerror(-rc));
        return rc;
    }

    if (flags & LCAP_RECV_NO_ENVELOPE)
        req->lr_flags |= LCAP_RECV_NO_ENVELOPE;

    if (flags & LCAP_RECV_NONBLOCK)
        zmq_flags |= ZMQ_DONTWAIT;

    for (;;) {
        struct zmq_msg_t    zmsg;
        bool                stop = false;

        rc = zmq_msg_init(&zmsg);
        if (rc < 0) {
            rc = -errno;
            lcap_error("Cannot initialize zmsg: %s", zmq_strerror(-rc));
            break;
        }

        rc = zmq_msg_recv(&zmsg, zsock, flags);
        if (rc < 0) {
            rc = -errno;
            if (rc != -EAGAIN && rc != -EINTR)
                lcap_error("Error receiving frame: %s", zmq_strerror(-rc));
            goto out_loop;
        }

        rc = lcapnet_req_update(req, &zmsg);
        if (rc < 0)
            goto out_loop;

        stop = !zmq_msg_more(&zmsg);

out_loop:
        zmq_msg_close(&zmsg);
        if (stop || rc)
            break;
    }

    if (rc >= 0 && req->lr_body_len > 0) {
        rc = cb(hint, req);
        if (rc < 0)
            lcap_info("Could not fully process RPC: %s", strerror(-rc));
    }

    lcapnet_req_release(req);
    return rc;
}

int lcap_rpc_recv(void *zsock, int flags, lcap_rpc_hdl_t cb, void *hint)
{
    int rpc_processed = 0;
    int rc;

    for (;;) {
        rc = lcap_rpc_recv_once(zsock, flags, cb, hint);
        if (rc < 0)
            break;

        rpc_processed++;
    }

    return rpc_processed;
}
