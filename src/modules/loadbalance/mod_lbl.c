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
#include <assert.h>
#include <stdbool.h>

#include "../../lcapd/lcap.h"
#include "pqueue.h"

#ifndef max
# define max(a, b)    ((a) > (b) ? (a) : (b))
#endif

/* Module name, exposed to the server */
#define MODNAME_LBL "load balancer"


struct ack_state {
    const char  *as_device;
    long long    as_record;
};


/* MPMC queue of all entries. */
static struct pqueue    *entries;
/* Current ACK state for each MDT we're reading from */
static struct ack_state *ack_array;
/* Module usage reference counter */
static unsigned int module_users;


const char *lcap_module_name(void)
{
    return MODNAME_LBL;
}

static inline unsigned int module_ref_inc(void)
{
    return __sync_add_and_fetch(&module_users, 1);
}

static inline unsigned int module_ref_dec(void)
{
    return __sync_sub_and_fetch(&module_users, 1);
}

static int shared_structures_init(struct lcap_ctx *ctx)
{
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    int                      i;
    int                      rc;

    ack_array = (struct ack_state *)calloc(cfg->ccf_mdtcount,
                                           sizeof(struct ack_state));
    if (ack_array == NULL)
        return -ENOMEM;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        ack_array[i].as_device = cfg->ccf_mdt[i];
        ack_array[i].as_record = -1LL;
    }

    /* There is only one producer per MDT */
    rc = pqueue_init(&entries, cfg->ccf_mdtcount, cfg->ccf_worker_count);
    return rc;
}

int lcap_module_init(struct lcap_ctx *ctx, void **mdata)
{
    unsigned int    user = module_ref_inc();
    int             rc;

    set_thr_id((user - 1) % max(ctx->cc_config->ccf_mdtcount,
                                ctx->cc_config->ccf_worker_count));

    lcap_info("Initializing module %s", MODNAME_LBL);

    if (user == 1) {
        /* First thread is responsible for initializing shared data structures */
        rc = shared_structures_init(ctx);
        if (rc < 0) {
            lcap_error("Failed to initialize shared structures: %s", strerror(-rc));
            return rc;
        }
    }
    return 0;
}

int lcap_module_destroy(struct lcap_ctx *ctx, void *mdata)
{
    if (module_ref_dec() == 0) {
        /* Last user is responsible for cleaning up */
        if (entries != NULL) {
            pqueue_destroy(entries);
            entries = NULL;
        }
        if (ack_array != NULL) {
            free(ack_array);
            ack_array = NULL;
        }
    }
    return 0;
}

int lcap_module_rec_enqueue(struct lcap_ctx *ctx, void *mdata, lcap_chlg_t rec)
{
    lcap_debug("Enqueuing record #%llu", rec->cr_index);
    pqueue_push(entries, rec);
    return 0;
}

int lcap_module_rec_dequeue(struct lcap_ctx *ctx, void *mdata, lcap_chlg_t *rec)
{
    *rec = pqueue_pop(entries);
    if (*rec == NULL)
        return -EAGAIN;

    lcap_debug("Dequeued record #%llu", (*rec)->cr_index);
    return 0;
}

int lcap_module_set_ack(struct lcap_ctx *ctx, void *mdata,
                        const struct client_id *id, const char *device,
                        long long recno)
{
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    int                      i;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        if (strcmp(ack_array[i].as_device, device) == 0) {
            ack_array[i].as_record = recno;
            return 0;
        }
    }
    return -ENODEV;
}

int lcap_module_get_ack(struct lcap_ctx *ctx, void *mdata, const char *device,
                        long long *recno)
{
    const struct lcap_cfg   *cfg = ctx_config(ctx);
    int                      i;

    for (i = 0; i < cfg->ccf_mdtcount; i++) {
        if (strcmp(ack_array[i].as_device, device) == 0) {
            *recno = ack_array[i].as_record;
            return 0;
        }
    }
    return -ENODEV;
}
