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


static int flags_translate(enum lcap_cl_flags lcap_flags)
{
    int lu_flags = 0;

    if (lcap_flags & LCAP_CL_FOLLOW)
        lu_flags |= CHANGELOG_FLAG_FOLLOW;

    if (lcap_flags & LCAP_CL_BLOCK)
        lu_flags |= CHANGELOG_FLAG_BLOCK;

    if (lcap_flags & LCAP_CL_JOBID)
        lu_flags |= CHANGELOG_FLAG_JOBID;

    return lu_flags;
}

static int lu_changelog_start(struct lcap_cl_ctx *ctx, enum lcap_cl_flags flags,
                              const char *mdtname, long long startrec)
{
    int lu_flags;
    
    lu_flags = flags_translate(flags);
    return llapi_changelog_start(&ctx->ccc_ptr, lu_flags, mdtname, startrec);
}

static int lu_changelog_fini(struct lcap_cl_ctx *ctx)
{
    return llapi_changelog_fini(&ctx->ccc_ptr);
}

static int lu_changelog_recv(struct lcap_cl_ctx *ctx,
                             struct changelog_rec **rec)
{
    return llapi_changelog_recv(ctx->ccc_ptr, rec);
}

static int lu_changelog_free(struct lcap_cl_ctx *ctx,
                             struct changelog_rec **rec)
{
    return llapi_changelog_free(rec);
}

static int lu_changelog_clear(struct lcap_cl_ctx *ctx, const char *mdtname,
                              const char *id, long long endrec)
{
    return llapi_changelog_clear(mdtname, id, endrec);
}

struct lcap_cl_operations cl_ops_null = {
    .cco_start  = lu_changelog_start,
    .cco_fini   = lu_changelog_fini,
    .cco_recv   = lu_changelog_recv,
    .cco_free   = lu_changelog_free,
    .cco_clear  = lu_changelog_clear
};
