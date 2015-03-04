/*
 * LCAP - Lustre Changelogs Aggregate and Publish
 *
 * Copyright (C)  2013-2015  CEA/DAM
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

#ifndef QUEUE_H
#define QUEUE_H

struct list_node {
    struct list_node    *ln_prev;
    struct list_node    *ln_next;
};

struct list {
    int                  l_count;
    struct list_node    *l_first;
    struct list_node    *l_last;
};


#define EMPTY_LIST_INITIALIZER  {.l_count = 0, .l_first = NULL, .l_last = NULL}


static inline void list_append(struct list *lst, struct list_node *n)
{
    struct list_node *_last;

    _last = lst->l_last;
    if (_last != NULL)
        _last->ln_next = n;

    n->ln_prev = _last;
    n->ln_next = NULL;

    if (lst->l_count == 0)
        lst->l_first = n;

    lst->l_last = n;
    lst->l_count++;
}

static inline void list_remove(struct list *lst, struct list_node *n)
{
    if (n->ln_prev != NULL)
        n->ln_prev->ln_next = n->ln_next;
    else
        lst->l_first = n->ln_next;

    if (n->ln_next != NULL)
        n->ln_next->ln_prev = n->ln_prev;
    else
        lst->l_last = n->ln_prev;

    n->ln_prev = NULL;
    n->ln_next = NULL;

    lst->l_count--;
}

static inline struct list_node *list_pop_head(struct list *lst)
{
    struct list_node    *n = lst->l_first;

    if (n == NULL)
        return NULL;

    lst->l_first = n->ln_next;
    if (lst->l_first != NULL)
        lst->l_first->ln_prev = NULL;

    lst->l_count--;

    if (lst->l_count == 1 || lst->l_count == 0)
        lst->l_last = lst->l_first;

    n->ln_prev = NULL;
    n->ln_next = NULL;

    return n;
}

static inline void list_insert_before(struct list *lst, struct list_node *n,
                                      struct list_node *inserted)
{
    inserted->ln_prev = n->ln_prev;
    inserted->ln_next = n;

    if (n->ln_prev != NULL)
        n->ln_prev->ln_next = inserted;
    else
        lst->l_first = inserted;

    n->ln_prev = inserted;
    lst->l_count++;
}

static inline void list_empty(struct list *lst)
{
    while (lst->l_count > 0)
        list_pop_head(lst);
}

#endif
