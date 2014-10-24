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
#include <ctype.h>

#include "utils.h"


static inline void *zalloc(size_t len)
{
    return calloc(len, 1);
}

static int fsize(FILE *fstream, size_t *flen)
{
    int rc;

    rc = fseek(fstream, 0L, SEEK_END);
    if (rc)
        return -errno;

    *flen = ftell(fstream);

    rewind(fstream);
    return 0;
}

int afread(const char *path, char **dst)
{
    int rc;
    FILE *fstream;
    size_t flen = 0;

    fstream = fopen(path, "r");
    if (fstream == NULL) {
        rc = -errno;
        goto err_cleanup;
    }

    rc = fsize(fstream, &flen);
    if (rc)
        goto err_cleanup;

    *dst = zalloc(flen + 1);
    if (*dst == NULL) {
        rc = -errno;
        goto err_cleanup;
    }

    if (flen != fread(*dst, 1, flen, fstream)) {
        rc = -EIO;
        goto err_cleanup;
    }

    /* Zero-terminate the buffer, just in case */
    (*dst)[flen] = '\0';

    rc = 0;

err_cleanup:
    if (fstream)
        fclose(fstream);

    if (rc) {
        if (*dst)
            free(*dst);
        *dst = NULL;
    }

    return rc;
}


void uri_init(struct uri *uri)
{
    uri->scheme = NULL;
    uri->host   = NULL;
    uri->path   = NULL;
    uri->port   = -1;
}

void uri_free(struct uri *uri)
{
    if (!uri)
        return;

    if (uri->scheme) {
       free(uri->scheme);
       uri->scheme = NULL;
    }

    if (uri->host) {
        free(uri->host);
        uri->host = NULL;
    }

    if (uri->path) {
        free(uri->path);
        uri->path = NULL;
    }

    uri->port = -1;
}

static long parse_long(const char *s, char **tail)
{
    if (!isdigit((int)(unsigned char)*s)) {
        *tail = (char *)s;
        return 0;
    }

    return strtol(s, tail, 10);
}

static int hex_digit_value(char digit)
{
    const char *DIGITS = "0123456789abcdef";
    const char *p;

    if ((unsigned char) digit == '\0')
        return -1;

    p = strchr(DIGITS, tolower((int) (unsigned char) digit));
    if (p == NULL)
        return -1;

    return p - DIGITS;
}

static int lowercase(char *s)
{
    char *p;

    for (p = s; *p != '\0'; p++)
        *p = tolower((int) (unsigned char) *p);

    return p - s;
}

static char *mkstr(const char *start, const char *stop)
{
    char *s;
    size_t len = stop - start;

    if (len < 0)
        return NULL;

    s = zalloc(len + 1);
    if (!s)
        return NULL;

    memcpy(s, start, len); /* zalloc() guarantees zero termination */
    return s;
}

/* In-place percent decoding. */
static int percent_decode(char *s)
{
    char *p, *q;

    /* Skip to the first '%'. If there are no percent escapes, this lets us
     * return without doing any copying. */
    q = s;
    while (*q != '\0' && *q != '%')
        q++;

    p = q;
    while (*q != '\0') {
        if (*q == '%') {
            int c, d;

            q++;
            c = hex_digit_value(*q);
            if (c == -1)
                return -1;
            q++;
            d = hex_digit_value(*q);
            if (d == -1)
                return -1;

            *p++ = c * 16 + d;
            q++;
        } else {
            *p++ = *q++;
        }
    }
    *p = '\0';

    return p - s;
}

/* Locale-insensitive versions of isalpha and isdigit */
static inline int is_alpha_char(int c)
{
    return (in_range(c, 'a', 'z') || in_range(c, 'A', 'Z'));
}

static inline int is_digit_char(int c)
{
    return in_range(c, '0', '9');
}

/* Parse the authority part of a URI. userinfo (user name and password) are not
 * supported and will cause an error if present. See RFC 3986, section 3.2.
 * Returns NULL on error.
 */
static int uri_parse_authority(struct uri *uri, const char *authority)
{
    const char *portsep;
    const char *host_start, *host_end;
    char *tail;

    /* We do not support "user:pass@" userinfo. The proxy has no use for it. */
    if (strchr(authority, '@') != NULL)
        return -EINVAL;

    /* Find the beginning and end of the host. */
    host_start = authority;
    if (*host_start == '[') {
        /* IPv6 address in brackets. */
        host_start++;
        host_end = strchr(host_start, ']');
        if (host_end == NULL)
            return -EINVAL;
        portsep = host_end + 1;
        if (!(*portsep == ':' || *portsep == '\0'))
            return -EINVAL;
    } else {
        portsep = strrchr(authority, ':');
        if (portsep == NULL)
            portsep = strchr(authority, '\0');
        host_end = portsep;
    }

    /* Get the port number. */
    if (*portsep == ':' && *(portsep + 1) != '\0') {
        long n;

        errno = 0;
        n = parse_long(portsep + 1, &tail);
        if (errno != 0
            || *tail != '\0'
            || tail == portsep + 1
            || !in_range(n, 1, 65535))
            return -EINVAL;
        uri->port = n;
    } else {
        uri->port = -1;
    }

    /* Get the host. */
    uri->host = mkstr(host_start, host_end);
    if (percent_decode(uri->host) < 0) {
        free(uri->host);
        uri->host = NULL;
        return -EINVAL;
    }

    return 0;
}

int uri_parse(const char *uri_s, struct uri *uri)
{
    const char *p, *q;

    uri_init(uri);

    /* Scheme, section 3.1. */
    p = uri_s;
    if (!is_alpha_char(*p))
        goto fail;

    q = p;
    while (is_alpha_char(*q)
           || is_digit_char(*q)
           || *q == '+'
           || *q == '-'
           || *q == '.')
        q++;

    if (*q != ':')
        goto fail;

    uri->scheme = mkstr(p, q);
    lowercase(uri->scheme);

    /* Authority, section 3.2. */
    p = q + 1;
    if (*p == '/' && *(p + 1) == '/') {
        char *authority = NULL;

        p += 2;
        q = p;
        while (!(*q == '/' || *q == '?' || *q == '#' || *q == '\0'))
            q++;

        authority = mkstr(p, q);
        if (uri_parse_authority(uri, authority)) {
            free(authority);
            goto fail;
        }
        free(authority);
        p = q;
    }

    q = strchr(p, '\0');
    uri->path = mkstr(p, q);

    return 0;

fail:
    uri_free(uri);
    return -EINVAL;
}

