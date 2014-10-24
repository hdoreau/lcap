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


/* Freely adapted from Alexander Krizhanovsky (ak@natsys-lab.com) work
 * and LMAX Disruptor design documents. */

#ifndef __x86_64__
#warning "This module was developped for x86-64 architecture only."
#endif

#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <limits.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>
#include <immintrin.h>

#include "pqueue.h"

/* Make sure the queue size is a power of 2 */
#define QUEUE_SIZE_ORDER    15
#define QUEUE_SIZE          (1 << QUEUE_SIZE_ORDER)
#define QUEUE_SIZE_MASK     (QUEUE_SIZE - 1)

#ifndef min
# define min(a, b)   ((a) < (b) ? (a) : (b))
#endif

#ifndef max
# define max(a, b)   ((a) > (b) ? (a) : (b))
#endif

#define likely(x)   __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)


/* Align variables on the cache size in order to prevent false sharing */
#define __cacheline_aligned   __attribute__((aligned(64)))

/* GCC-specific memory barrier */
#define barrier()   __asm__ __volatile__("" ::: "memory")

/* Insert PAUSE instruction or sleep for a while (depending on how much
 * CPU usage we're willing to have). */
#ifdef MOD_LBL_HIGHLOAD
# define pause()     _mm_pause()
#else
# define pause()     usleep(50)
#endif


/* Per thread, incremental, monotonic identifier */
unsigned int __thread  thr_id;


struct thr_pos {
    unsigned long head;
    unsigned long tail;
};

struct pqueue {
    unsigned int      n_prod;
    unsigned int      n_cons;
    unsigned long     head __cacheline_aligned; /* first free (insert next) */
    unsigned long     tail __cacheline_aligned; /* last set (pop next) */
    unsigned long     last_head __cacheline_aligned; /* last not-processed producer's pointer */
    unsigned long     last_tail __cacheline_aligned; /* last not-processed consumer's pointer */
    struct thr_pos   *thr_pos;
    void            **slots;
};

static struct thr_pos *get_thr_pos(struct pqueue *queue)
{
    assert(get_thr_id() < max(queue->n_prod, queue->n_cons));
    return &queue->thr_pos[get_thr_id()];
}

int pqueue_init(struct pqueue **queue, unsigned int n_prod, unsigned int n_cons)
{
    struct pqueue  *q;
    unsigned int    n;
    int             rc;

    q = (struct pqueue *)malloc(sizeof(*q));
    if (q == NULL)
        return -ENOMEM;

    q->n_prod = n_prod;
    q->n_cons = n_cons;
    q->head = 0;
    q->tail = 0;
    q->last_head = 0;
    q->last_tail = 0;

    n = max(n_prod, n_cons);
    q->thr_pos = (struct thr_pos *)memalign(getpagesize(),
                                            sizeof(struct thr_pos) * n);
    if (q->thr_pos == NULL) {
        rc = -ENOMEM;
        goto err_free;
    }

    memset((char *)q->thr_pos, 0xff, sizeof(struct thr_pos) * n);

    q->slots = memalign(getpagesize(), QUEUE_SIZE * sizeof(void *));
    if (q->slots == NULL) {
        rc = -ENOMEM;
        goto err_free_thr;
    }

    *queue = q;
    return 0;

err_free_thr:
    free(q->thr_pos);

err_free:
    free(q);
    *queue = NULL;

    return rc;
}

void pqueue_destroy(struct pqueue *queue)
{
    if (queue == NULL)
        return;

    free(queue->thr_pos);
    free(queue->slots);
    free(queue);
}


void pqueue_push(struct pqueue *queue, void *ptr)
{
    /* Reserve a slot where to push. Only the increment is atomic,
     * not the assignment. We therefore need to first assign head,
     * to prevent other threads from pushing head too high.
     * --
     * It looks really weird but it _is_ valid & required! */
    get_thr_pos(queue)->head = queue->head;
    get_thr_pos(queue)->head = __sync_fetch_and_add(&queue->head, 1);

    /* Aggressively poll if there's no slot already available for us */
    while (unlikely(get_thr_pos(queue)->head >= queue->last_tail +
                                                QUEUE_SIZE)) {
        unsigned long   _min = queue->tail;
        unsigned int    i;


        for (i = 0; i < queue->n_cons; i++) {
            unsigned long tmp_t = queue->thr_pos[i].tail;

            /* The use of a memory barrier here guarantees that the tail
             * values are only fetched once, thus enforcing consistency. */
            barrier();
            if (tmp_t < _min)
                _min = tmp_t;
        }

        queue->last_tail = _min;
        if (get_thr_pos(queue)->head < queue->last_tail + QUEUE_SIZE)
            break;

        /* Wait a little... */
        pause();
    }

    /* Actually store our pointer and signal to the others that the queue
     * has grown. */
    queue->slots[get_thr_pos(queue)->head & QUEUE_SIZE_MASK] = ptr;
    get_thr_pos(queue)->head = ULONG_MAX;
}

void *pqueue_pop(struct pqueue *queue)
{
    void            *ret;

    if (queue->tail == queue->head)
        return NULL;

    get_thr_pos(queue)->tail = queue->tail;
    get_thr_pos(queue)->tail = __sync_fetch_and_add(&queue->tail, 1);

    while (unlikely(get_thr_pos(queue)->tail >= queue->last_head)) {
        unsigned long _min = queue->head;
        unsigned int  i;

        for (i = 0; i < queue->n_prod; i++) {
            unsigned long tmp_h = queue->thr_pos[i].head;

            barrier();
            if (tmp_h < _min)
                _min = tmp_h;
        }

        queue->last_head = _min;
        if (get_thr_pos(queue)->tail < queue->last_head)
            break;

        pause();
    }

    ret = queue->slots[get_thr_pos(queue)->tail & QUEUE_SIZE_MASK];
    get_thr_pos(queue)->tail = ULONG_MAX;
    return ret;
}
