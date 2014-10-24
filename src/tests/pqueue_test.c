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
#include <sys/time.h>

#include <modules/loadbalance/pqueue.h>


#define QUEUE_SIZE_ORDER    15
#define QUEUE_SIZE          (1 << QUEUE_SIZE_ORDER)

#define N           (QUEUE_SIZE * 1024)
#define CONSUMERS   2
#define PRODUCERS   2


typedef unsigned char   q_type_t;

#define X_EMPTY     0    /* Skipped by producers */
#define X_MISSED    255  /* Missed by consumers */

static q_type_t x[N * PRODUCERS];

static int n;

struct wrk_ctx {
    struct pqueue   *queue;
    unsigned int     idx;
};

void *prod_main(void *args)
{
    struct wrk_ctx  *ctx = (struct wrk_ctx *)args;
    unsigned int     i;

    set_thr_id(ctx->idx);
    for (i = get_thr_id(); i < N * PRODUCERS; i += PRODUCERS) {
        x[i] = X_MISSED;
        pqueue_push(ctx->queue, x + i);
    }
    return NULL;
}

void *cons_main(void *args)
{
    struct wrk_ctx  *ctx = (struct wrk_ctx *)args;
    unsigned int     i;

    set_thr_id(ctx->idx);
    while (__sync_fetch_and_add(&n, 1) < N * PRODUCERS) {
        q_type_t *v = pqueue_pop(ctx->queue);

        assert(v);
        assert(*v == X_MISSED);
        *v = (q_type_t)(get_thr_id() + 1);
    }
    return NULL;
}

static inline unsigned long tv_to_ms(const struct timeval *tv)
{
    return ((unsigned long)tv->tv_sec * 1000000 + tv->tv_usec) / 1000;
}

int main(int ac, char **av)
{
    struct pqueue   *q;
    pthread_t        thr[PRODUCERS + CONSUMERS];
    struct wrk_ctx   ctx[max(PRODUCERS, CONSUMERS)];
    struct timeval   tv0, tv1;
    int              i, res = 0;

    memset(x, X_EMPTY, N * sizeof(q_type_t) * PRODUCERS);
    res = pqueue_init(&q, PRODUCERS, CONSUMERS);
    if (res < 0) {
        printf("Cannot initialize queue: %s\n", strerror(-res));
        return 1;
    }

    gettimeofday(&tv0, NULL);
    for (i = 0; i < PRODUCERS; i++) {
        ctx[i].queue = q;
        ctx[i].idx   = i;
        pthread_create(&thr[i], NULL, prod_main, &ctx[i]);
    }

    usleep(10 * 1000);

    for (i = 0; i < CONSUMERS; i++) {
        ctx[i].queue = q;
        ctx[i].idx   = i;
        pthread_create(&thr[PRODUCERS + i], NULL, cons_main, &ctx[i]);
    }

    for (i = 0; i < PRODUCERS + CONSUMERS; i++)
        pthread_join(thr[i], NULL);

    gettimeofday(&tv1, NULL);
    printf("Execution: %d operations in %llums\n", n, tv_to_ms(&tv1) - tv_to_ms(&tv0));

    for (i = 0; i < N * PRODUCERS; i++) {
        if (x[i] == X_EMPTY) {
            printf("empty: %d\n", i);
            res = 1;
            break;
        } else if (x[i] == X_MISSED) {
            printf("missed: %d\n", i);
            res = 2;
            break;
        }
    }

    printf("%s\n", res ? "FAILED" : "PASSED");

    pqueue_destroy(q);

    return res;
}
