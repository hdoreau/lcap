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


#ifndef LCAP_RPC_H
#define LCAP_RPC_H

#include <lcap_idl.h>
#include <zmq.h>


struct conn_id {
    size_t          ci_length;
    unsigned char   ci_data[0];
};


#define LCAP_RECV_NO_ENVELOPE   (1 << 0)
#define LCAP_RECV_NONBLOCK      (1 << 1)
struct lcapnet_request {
    int                  lr_flags;
    struct conn_id      *lr_remote;
    struct conn_id      *lr_forward;
    struct px_rpc_hdr   *lr_body;
    size_t               lr_body_len;
};

/**
 * RPC handling callback, invoked with a hint (user data) and a recomposed
 * message describing the RPC which got received.
 * All allocated buffers belong to the caller, i.e. the processing callback
 * should duplicate anything it wants to keep and must not release the messages.
 *
 * Callbacks are expected to return 0 on success and a negative error code on
 * failure.
 */
typedef int (*lcap_rpc_hdl_t)(void *, const struct lcapnet_request *);

/**
 * Read incoming RPCs and invoke the handler accordingly.
 *
 * Processing stops when no messages can be read from the socket in
 * non-blocking mode (if \a flags contains ZMQ_DONTWAIT) or if an error occurs.
 *
 * \a hint is a user private pointer to pass to the callback.
 *
 * This function returns the number of sucessfully processed RPCs (for which
 * the handler returned 0).
 */
int lcap_rpc_recv(void *zsock, int flags, lcap_rpc_hdl_t cb, void *hint);

#endif
