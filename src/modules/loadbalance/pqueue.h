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


#ifndef PQUEUE_H
#define PQUEUE_H

extern unsigned int __thread  thr_id;

static inline unsigned int get_thr_id(void)
{
    return thr_id;
}

static inline void set_thr_id(unsigned int id)
{
    thr_id = id;
}

struct pqueue;


int pqueue_init(struct pqueue **queue, unsigned int n_prod,
                unsigned int n_cons);
void pqueue_destroy(struct pqueue *queue);
void pqueue_push(struct pqueue *queue, void *ptr);
void *pqueue_pop(struct pqueue *queue);

#endif /* PQUEUE_H */
