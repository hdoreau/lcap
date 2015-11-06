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


#include "lcap_client.h"
#include <stdlib.h>


extern struct lcap_cl_operations cl_ops_null;
extern struct lcap_cl_operations cl_ops_proxy;


int lcap_changelog_start(struct lcap_cl_ctx **pctx, enum lcap_cl_flags flags,
                         const char *mdtname, long long startrec)
{
    struct lcap_cl_ctx  *ctx;
    int                  rc;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return -ENOMEM;

    if (flags & LCAP_CL_DIRECT)
        ctx->ccc_ops = &cl_ops_null;
    else
        ctx->ccc_ops = &cl_ops_proxy;

    rc = ctx->ccc_ops->cco_start(ctx, flags, mdtname, startrec);
    if (rc < 0) {
        free(ctx);
        ctx = NULL;
    }

    *pctx = ctx;
    return rc;
}
