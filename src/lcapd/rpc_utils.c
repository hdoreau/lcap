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


#include "lcapd_internal.h"


int peer_rpc_send(void *sock, const struct conn_id *src_id,
                  const struct conn_id *dst_id, const char *msg, size_t msg_len)
{
    int rc;

    if (src_id != NULL) {
        rc = zmq_send(sock, src_id->ci_data, src_id->ci_length, ZMQ_SNDMORE);
        if (rc < 0)
            goto err_out;

        rc = zmq_send(sock, "", 0, ZMQ_SNDMORE);
        if (rc < 0)
            goto err_out;
    }

    rc = zmq_send(sock, dst_id->ci_data, dst_id->ci_length, ZMQ_SNDMORE);
    if (rc < 0)
        goto err_out;

    rc = zmq_send(sock, "", 0, ZMQ_SNDMORE);
    if (rc < 0)
        goto err_out;

    rc = zmq_send(sock, msg, msg_len, 0);
    if (rc < 0)
        goto err_out;

    rc = 0;

err_out:
    if (rc < 0) {
        rc = -errno;
        lcap_error("Worker send error: %s", zmq_strerror(-rc));
    }
    return rc;
}

int ack_retcode(void *sock, const struct conn_id *src_id,
                const struct conn_id *dst_id, int ret)
{
    struct px_rpc_ack   rep;
    int                 rc;

    memset(&rep, 0, sizeof(rep));
    rep.pr_hdr.op_type = RPC_OP_ACK;
    rep.pr_retcode     = ret;

    rc = peer_rpc_send(sock, src_id, dst_id, (char *)&rep, sizeof(rep));
    if (rc < 0) {
        rc = -errno;
        lcap_error("Worker send error: %s", zmq_strerror(-rc));
        return rc;
    }

    lcap_debug("Message ACK'd with code %d", ret);
    return 0;
}
