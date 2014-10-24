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


#ifndef UTILS_H
#define UTILS_H

struct uri {
    char *scheme;
    char *host;
    char *path;
    int port;
};

/*
 * File handling utility: loads a text file into memory
 */
int afread(const char *path, char **dst);


/*
 * URI handling utilities
 */
void uri_init(struct uri *uri);
void uri_free(struct uri *uri);
int uri_parse(const char *uri_s, struct uri *uri);


static inline int in_range(int val, int min, int max)
{
    return ((val >= min) && (val <= max));
}

#endif /* UTILS_H */
