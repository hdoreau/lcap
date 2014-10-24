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


#ifndef LCAPD_INTERNAL_H
#define LCAPD_INTERNAL_H

struct lcap_cfg;
struct lcap_ctx;

int lcap_cfg_init(int ac, char **av, struct lcap_cfg *config);
int lcap_cfg_release(struct lcap_cfg *config);

int lcap_module_load_external(struct lcap_ctx *ctx, const char *modname);
int lcap_module_unload_external(struct lcap_ctx *ctx);

struct wrk_args {
    struct lcap_ctx *wa_ctx;
    int              wa_idx;
};

void *worker_main(void *args);
void *reader_main(void *args);

#define WORKERS_URL     "inproc://lcapwrk.ipc"
#define SERVER_CONN_URL "tcp://localhost:8189"
#define SERVER_BIND_URL "tcp://*:8189"

#endif /* LCAPD_INTERNAL_H */

