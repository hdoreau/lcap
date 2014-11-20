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


/**
 * With lustre 2.7, and LU-1996 in peculiar, the changelog API only deals
 * with struct changelog_record, though binary compatibility is preserved.
 *
 * Check whether the CLF_JOBID flag exists to determine how to manipulate
 * records.
 */
#ifdef HAVE_CHANGELOG_EXT_JOBID
typedef struct changelog_rec        *lcap_chlg_t;
#else
typedef struct changelog_ext_rec    *lcap_chlg_t;
#endif



enum rpc_op_type {
    /* Changelog reader */
    RPC_OP_ENQUEUE,
    RPC_OP_DEQUEUE,

    /* Remote changelog consumers */
    RPC_OP_START,
    RPC_OP_CLEAR,
    RPC_OP_FINI,

    /* Control */
    RPC_OP_ACK
};


struct client_id {
    uint32_t    ident;
} __attribute__((packed));

struct px_rpc_hdr {
    uint32_t            op_type;
    struct client_id    cl_id;
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
    char                pr_records[0];
} __attribute__((packed));

struct px_rpc_dequeue {
    struct px_rpc_hdr   pr_hdr;
} __attribute__((packed));

struct px_rpc_ack {
    struct px_rpc_hdr   pr_hdr;
    int32_t             pr_retcode;
} __attribute__((packed));


static inline char *px_rpc_get_id(struct px_rpc_clear *rpc)
{
    return rpc->pr_id;
}

static inline char *px_rpc_get_mdtname(struct px_rpc_clear *rpc)
{
    return rpc->pr_id + strlen(rpc->pr_id) + 1;
}

#endif /* LCAP_IDL_H */
