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

#include <lcap_net.h>

struct lcap_cfg;

int lcap_cfg_init(int ac, char **av, struct lcap_cfg *config);
int lcap_cfg_release(struct lcap_cfg *config);


int lcapd_process_request(void *hint, const struct lcapnet_request *req);


struct subtask_info {
    pthread_t   si_thread;
    bool        si_running;
};

struct subtask_args {
    const struct lcap_cfg   *sa_cfg;
    unsigned int             sa_idx;
};

void *reader_main(void *args);

#define READERS_URL     "inproc://lcaprdr.ipc"
#define WORKERS_URL     "inproc://lcapwrk.ipc"

#define BROKER_CONN_URL "tcp://localhost:8189"
#define BROKER_BIND_URL "tcp://*:8189"


int peer_rpc_send(void *sock, const struct conn_id *src_id,
                  const struct conn_id *dst_id, const char *msg,
                  size_t msg_len);

int ack_retcode(void *sock, const struct conn_id *src_cid,
                const struct conn_id *dst_cid, int ret);


static inline bool cid_compare(const struct conn_id *cid0,
                               const struct conn_id *cid1)
{
    if (cid0->ci_length > cid1->ci_length)
        return -1;

    if (cid0->ci_length < cid1->ci_length)
        return 1;

    return memcmp(cid0->ci_data, cid1->ci_data, cid0->ci_length);
}

#endif /* LCAPD_INTERNAL_H */
