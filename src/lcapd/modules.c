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
#include <dlfcn.h>

#include "lcap.h"
#include "modules.h"

static int __mod_symbol(struct lcap_ctx *ctx, void *dlhandle,
                        const char *symname, void **sym)
{
    char *errstr;

    *sym = dlsym(dlhandle, symname);
    errstr = dlerror();
    if (errstr) {
        lcap_error("Can't load symbol %s: %s", symname, errstr);
        *sym = NULL;
        return -EINVAL;
    }
    return 0;
}

static int __module_load(struct lcap_ctx *ctx, void *dlhandle,
                         const char *modname)
{
    int rc;
    struct lcap_proc_module *mod = &ctx->cc_module;

    mod->cpm_name = strdup(modname);
    mod->cpm_dlh  = dlhandle;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_name",
                      (void **)&mod->cpm_ops.cpo_name);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_init",
                      (void **)&mod->cpm_ops.cpo_init);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_destroy",
                      (void **)&mod->cpm_ops.cpo_destroy);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_rec_enqueue",
                      (void **)&mod->cpm_ops.cpo_rec_enqueue);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_rec_dequeue",
                      (void **)&mod->cpm_ops.cpo_rec_dequeue);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_set_ack",
                      (void **)&mod->cpm_ops.cpo_set_ack);
    if (rc)
        goto out;

    rc = __mod_symbol(ctx, dlhandle, "lcap_module_get_ack",
                      (void **)&mod->cpm_ops.cpo_get_ack);
    if (rc)
        goto out;

    ctx->cc_loaded = true;

out:
    return rc;
}

int lcap_module_load_external(struct lcap_ctx *ctx)
{
    const char  *modname = ctx->cc_config->ccf_module;
    void        *dlhandle;
    int          rc;

    lcap_verb("Loading module from %s", modname);

    if (ctx->cc_loaded) {
        lcap_error("Module already loaded");
        return -EALREADY;
    }

    dlhandle = dlopen(modname, RTLD_NOW);
    if (!dlhandle) {
        lcap_error("dlopen() failure: %s", dlerror());
        return -EINVAL;
    }

    rc = __module_load(ctx, dlhandle, modname);
    if (rc)
        goto err_cleanup;

    lcap_info("Module %s loaded from %s", ctx->cc_module.cpm_name, modname);
    ctx->cc_loaded = true;

    return 0;

err_cleanup:
    dlclose(dlhandle);
    return rc;
}

int lcap_module_unload_external(struct lcap_ctx *ctx)
{
    const char *modname = ctx->cc_config->ccf_module;

    if (!ctx->cc_loaded) {
        lcap_debug("No module loaded");
        return 0;
    }

    lcap_verb("Unloading module %s", modname);

    dlclose(ctx->cc_module.cpm_dlh);
    memset(&ctx->cc_module, 0x00, sizeof(ctx->cc_module));

    lcap_debug("Module %s successfully freed", modname);
    ctx->cc_loaded = false;

    return 0;
}
