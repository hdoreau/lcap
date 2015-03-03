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


#ifndef LCAP_IDL_H
#define LCAP_IDL_H

#if HAVE_CONFIG_H
#include "lcap_config.h"
#endif

#include <string.h>
#include <stdint.h>

#include "queue.h"

#if !defined(container_of)
#include <stddef.h>

#define container_of(ptr, type, member) \
            ((type *)((char *)(ptr) - offsetof(type, member)))
#endif


enum rpc_op_type {
    /* Remote changelog consumers */
    RPC_OP_START        = 0,
    RPC_OP_DEQUEUE      = 1,
    RPC_OP_CLEAR        = 2,
    RPC_OP_FINI         = 3,
    
    /* Changelog reader */
    RPC_OP_ENQUEUE      = 4,

    /* Control */
    RPC_OP_ACK          = 5,
    RPC_OP_SIGNAL       = 6,

    /* Used for internal validation */
    RPC_OP_FIRST = RPC_OP_START,
    RPC_OP_LAST  = RPC_OP_SIGNAL
};


struct px_rpc_hdr {
    uint32_t    op_type;
    uint32_t    reserved;
} __attribute__((packed));

struct px_rpc_register {
    struct px_rpc_hdr   pr_hdr;
    uint32_t            pr_flags;
    uint32_t            padding;
    uint64_t            pr_start;
    uint8_t             pr_mdtname[128];
} __attribute__((packed));

struct px_rpc_clear {
    struct px_rpc_hdr   pr_hdr;
    int64_t             pr_index;
    int32_t             pr_id_len;
    char                pr_id[0];
} __attribute__((packed));

struct px_rpc_enqueue {
    struct px_rpc_hdr   pr_hdr;
    uint32_t            pr_count;
    uint8_t             pr_records[0];
} __attribute__((packed));

struct px_rpc_dequeue {
    struct px_rpc_hdr   pr_hdr;
} __attribute__((packed));

struct px_rpc_fini {
    struct px_rpc_hdr   pr_hdr;
} __attribute__((packed));

struct px_rpc_ack {
    struct px_rpc_hdr   pr_hdr;
    int32_t             pr_retcode;
} __attribute__((packed));

struct px_rpc_signal {
    struct px_rpc_hdr   pr_hdr;
    uint64_t            pr_ret;
    uint8_t             pr_mdtname[128];
} __attribute__((packed));


static inline size_t rpc_expected_length(enum rpc_op_type op)
{
    switch (op) {
        case RPC_OP_START:
            return sizeof(struct px_rpc_register);
        case RPC_OP_DEQUEUE:
            return sizeof(struct px_rpc_dequeue);
        case RPC_OP_CLEAR:
            return sizeof(struct px_rpc_clear);
        case RPC_OP_FINI:
            return sizeof(struct px_rpc_fini);
        case RPC_OP_ENQUEUE:
            return sizeof(struct px_rpc_enqueue);
        case RPC_OP_ACK:
            return sizeof(struct px_rpc_ack);
        case RPC_OP_SIGNAL:
            return sizeof(struct px_rpc_signal);
        default:
            return (size_t)-1;
    }
}

static inline const char *rpc_optype2str(enum rpc_op_type type)
{
    switch(type) {
        case RPC_OP_ENQUEUE:
            return "ENQUEUE";
        case RPC_OP_START:
            return "START";
        case RPC_OP_DEQUEUE:
            return "DEQUEUE";
        case RPC_OP_CLEAR:
            return "CLEAR";
        case RPC_OP_FINI:
            return "FINI";
        case RPC_OP_ACK:
            return "ACK";
        case RPC_OP_SIGNAL:
            return "SIGNAL";
        default:
            return "???";
    }
}


static inline const char *px_rpc_get_id(const struct px_rpc_clear *rpc)
{
    return rpc->pr_id;
}

static inline const char *px_rpc_get_mdtname(const struct px_rpc_clear *rpc)
{
    return rpc->pr_id + strlen(rpc->pr_id) + 1;
}

#endif /* LCAP_IDL_H */
