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


#ifndef LCAPCLIENT_H
#define LCAPCLIENT_H

#include <errno.h>
#include <assert.h>

#include <lustre/lustreapi.h>

/* Map to FLAG_FOLLOW (not implemented in lustre as of now) */
#define LCAP_CL_FOLLOW  (0x01 << 0)

/* Blocking I/O */
#define LCAP_CL_BLOCK   (0x01 << 1)

/* NULL-channel, get records directly from Lustre */
#define LCAP_CL_DIRECT  (0x01 << 2)

struct lcap_cl_ctx;

struct lcap_cl_operations {
    int (*cco_start)(struct lcap_cl_ctx *ctx, int flags, const char *mdtname,
                     long long startrec);
    int (*cco_fini)(struct lcap_cl_ctx *ctx);
    int (*cco_recv)(struct lcap_cl_ctx *ctx, struct changelog_ext_rec **rec);
    int (*cco_free)(struct lcap_cl_ctx *ctx, struct changelog_ext_rec **rec);
    int (*cco_clear)(struct lcap_cl_ctx *ctx, const char *mdtname,
                     const char *id, long long endrec);
};

/* Opaque context.
 * Contains the appropriate operation vector and an instance-specific
 * data pointer */
struct lcap_cl_ctx {
    const struct lcap_cl_operations *ccc_ops;
    void                            *ccc_ptr;
};

/**
 * Signal the beginning of the read to the server.
 *
 * \param[out]  pctx        Address of a lcap_cl_ctx pointer which this
 *                          function will initialize and which is to be
 *                          passed to the other lcap_changelog_*() functions
 * \param[in]   flags       Set of LCAP_CL_* flags
 * \param[in]   mdtname     Device name from which to read records
 * \param[in]   startrec    Id of the first desired record
 *
 * \retval 0 on success
 * \retval Appropriate negative error code on failure
 */
int lcap_changelog_start(struct lcap_cl_ctx **pctx, int flags,
                         const char *mdtname, long long startrec);

/**
 * Signal the end of the read, releases associated structures.
 *
 * \param[in] ctx   The client context initialized by lcap_changelog_start
 *                  to release and invalidate
 *
 * \retval 0 on success
 * \retval Appropriate negative error code on failure
 */
static inline int lcap_changelog_fini(struct lcap_cl_ctx *ctx)
{
    assert(ctx);
    assert(ctx->ccc_ops);
    assert(ctx->ccc_ops->cco_fini);

    return ctx->ccc_ops->cco_fini(ctx);
}

/**
 * Receive a single record, to free with lcap_changelog_free() after use.
 *
 * \param[in]   ctx The client context initialized by lcap_changelog_start
 * \param[out]  rec Where to store the received record.
 *
 * \retval 0 on success
 * \retval Appropriate negative error code on failure
 */
static inline int lcap_changelog_recv(struct lcap_cl_ctx *ctx,
                                      struct changelog_ext_rec **rec)
{
    assert(ctx);
    assert(ctx->ccc_ops);
    assert(ctx->ccc_ops->cco_recv);

    return ctx->ccc_ops->cco_recv(ctx, rec);
}

/**
 * Release a record obtained from lcap_changelog_recv().
 *
 * \param[in]  ctx The client context initialized by lcap_changelog_start
 * \param[in]  rec The record to free.
 *
 * \retval 0 on success
 * \retval Appropriate negative error code on failure
 */
static inline int lcap_changelog_free(struct lcap_cl_ctx *ctx,
                                      struct changelog_ext_rec **rec)
{
    assert(ctx);
    assert(ctx->ccc_ops);
    assert(ctx->ccc_ops->cco_free);

    return ctx->ccc_ops->cco_free(ctx, rec);
}

/**
 * Acknowledge records up to a given number so that they can be cleared
 * upstream.
 *
 * \param[in]   ctx     The client context initialized by lcap_changelog_start
 * \param[in]   mdtname The device name on which to free the records
 * \param[in]   id      Changelog reader ID (such as "cl1")
 * \param[in]   endrec  Maximum record ID to acknowledge
 *
 * \retval 0 on success
 * \retval Appropriate negative error code on failure
 */
static inline int lcap_changelog_clear(struct lcap_cl_ctx *ctx,
                                       const char *mdtname, const char *id,
                                       long long endrec)
{
    assert(ctx);
    assert(ctx->ccc_ops);
    assert(ctx->ccc_ops->cco_clear);

    return ctx->ccc_ops->cco_clear(ctx, mdtname, id, endrec);
}

#endif /* LCAPCLIENT_H */
