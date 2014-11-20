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


#ifndef MODULES_H
#define MODULES_H

#include "lcap.h"


static inline const char *cpm_name(struct lcap_ctx *ctx)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_name();
}

static inline int cpm_init(struct lcap_ctx *ctx, void **mod_private)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_init(ctx, mod_private);
}

static inline int cpm_destroy(struct lcap_ctx *ctx, void *mod_private)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_destroy(ctx, mod_private);
}

static inline int cpm_rec_enqueue(struct lcap_ctx *ctx, void *mod_private,
                                  lcap_chlg_t rec)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_rec_enqueue(ctx, mod_private, rec);
}

static inline int cpm_rec_dequeue(struct lcap_ctx *ctx, void *mod_private,
                                  lcap_chlg_t *rec)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_rec_dequeue(ctx, mod_private, rec);
}

static inline int cpm_set_ack(struct lcap_ctx *ctx, void *mod_private,
                              const struct client_id *id, const char *device,
                              long long recno)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_set_ack(ctx, mod_private, id, device, recno);
}

static inline int cpm_get_ack(struct lcap_ctx *ctx, void *mod_private,
                              const char *device, long long *recno)
{
    struct lcap_proc_module *cpm;

    cpm = &ctx->cc_module;
    return cpm->cpm_ops.cpo_get_ack(ctx, mod_private, device, recno);
}

#endif /* MODULES_H */
