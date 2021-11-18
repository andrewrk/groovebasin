/***
  This file is part of PulseAudio.

  Copyright 2009 Ted Percival

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <errno.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#endif

#ifdef HAVE_GRP_H
#include <grp.h>
#endif

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>

#include "usergroup.h"

#ifdef HAVE_GRP_H

/* Returns a suitable starting size for a getgrnam_r() or getgrgid_r() buffer,
   plus the size of a struct group.
 */
static size_t starting_getgr_buflen(void) {
    size_t full_size;
    long n;
#ifdef _SC_GETGR_R_SIZE_MAX
    n = sysconf(_SC_GETGR_R_SIZE_MAX);
#else
    n = -1;
#endif
    if (n <= 0)
        n = 512;

    full_size = (size_t) n + sizeof(struct group);

    if (full_size < (size_t) n) /* check for integer overflow */
        return (size_t) n;

    return full_size;
}

/* Returns a suitable starting size for a getpwnam_r() or getpwuid_r() buffer,
   plus the size of a struct passwd.
 */
static size_t starting_getpw_buflen(void) {
    long n;
    size_t full_size;

#ifdef _SC_GETPW_R_SIZE_MAX
    n = sysconf(_SC_GETPW_R_SIZE_MAX);
#else
    n = -1;
#endif
    if (n <= 0)
        n = 512;

    full_size = (size_t) n + sizeof(struct passwd);

    if (full_size < (size_t) n) /* check for integer overflow */
        return (size_t) n;

    return full_size;
}

/* Given a memory allocation (*bufptr) and its length (*buflenptr),
   double the size of the allocation, updating the given buffer and length
   arguments. This function should be used in conjunction with the pa_*alloc
   and pa_xfree functions.

   Unlike realloc(), this function does *not* retain the original buffer's
   contents.

   Returns 0 on success, nonzero on error. The error cause is indicated by
   errno.
 */
static int expand_buffer_trashcontents(void **bufptr, size_t *buflenptr) {
    size_t newlen;

    if (!bufptr || !*bufptr || !buflenptr) {
        errno = EINVAL;
        return -1;
    }

    newlen = *buflenptr * 2;

    if (newlen < *buflenptr) {
        errno = EOVERFLOW;
        return -1;
    }

    /* Don't bother retaining memory contents; free & alloc anew */
    pa_xfree(*bufptr);

    *bufptr = pa_xmalloc(newlen);
    *buflenptr = newlen;

    return 0;
}

#ifdef HAVE_GETGRGID_R
/* Thread-safe getgrgid() replacement.
   Returned value should be freed using pa_getgrgid_free() when the caller is
   finished with the returned group data.

   API is the same as getgrgid(), errors are indicated by a NULL return;
   consult errno for the error cause (zero it before calling).
 */
struct group *pa_getgrgid_malloc(gid_t gid) {
    size_t buflen, getgr_buflen;
    int err;
    void *buf;
    void *getgr_buf;
    struct group *result = NULL;

    buflen = starting_getgr_buflen();
    buf = pa_xmalloc(buflen);

    getgr_buflen = buflen - sizeof(struct group);
    getgr_buf = (char *)buf + sizeof(struct group);

    while ((err = getgrgid_r(gid, (struct group *)buf, getgr_buf, getgr_buflen, &result)) == ERANGE) {
        if (expand_buffer_trashcontents(&buf, &buflen))
            break;

        getgr_buflen = buflen - sizeof(struct group);
        getgr_buf = (char *)buf + sizeof(struct group);
    }

    if (err || !result) {
        result = NULL;
        if (buf) {
            pa_xfree(buf);
            buf = NULL;
        }
    }

    pa_assert(result == buf || result == NULL);

    return result;
}

void pa_getgrgid_free(struct group *grp) {
    pa_xfree(grp);
}

#else /* !HAVE_GETGRGID_R */

struct group *pa_getgrgid_malloc(gid_t gid) {
    return getgrgid(gid);
}

void pa_getgrgid_free(struct group *grp) {
    /* nothing */
    return;
}

#endif /* !HAVE_GETGRGID_R */

#ifdef HAVE_GETGRNAM_R
/* Thread-safe getgrnam() function.
   Returned value should be freed using pa_getgrnam_free() when the caller is
   finished with the returned group data.

   API is the same as getgrnam(), errors are indicated by a NULL return;
   consult errno for the error cause (zero it before calling).
 */
struct group *pa_getgrnam_malloc(const char *name) {
    size_t buflen, getgr_buflen;
    int err;
    void *buf;
    void *getgr_buf;
    struct group *result = NULL;

    buflen = starting_getgr_buflen();
    buf = pa_xmalloc(buflen);

    getgr_buflen = buflen - sizeof(struct group);
    getgr_buf = (char *)buf + sizeof(struct group);

    while ((err = getgrnam_r(name, (struct group *)buf, getgr_buf, getgr_buflen, &result)) == ERANGE) {
        if (expand_buffer_trashcontents(&buf, &buflen))
            break;

        getgr_buflen = buflen - sizeof(struct group);
        getgr_buf = (char *)buf + sizeof(struct group);
    }

    if (err || !result) {
        result = NULL;
        if (buf) {
            pa_xfree(buf);
            buf = NULL;
        }
    }

    pa_assert(result == buf || result == NULL);

    return result;
}

void pa_getgrnam_free(struct group *group) {
    pa_xfree(group);
}

#else /* !HAVE_GETGRNAM_R */

struct group *pa_getgrnam_malloc(const char *name) {
    return getgrnam(name);
}

void pa_getgrnam_free(struct group *group) {
    /* nothing */
    return;
}

#endif /* HAVE_GETGRNAM_R */

#endif /* HAVE_GRP_H */

#ifdef HAVE_PWD_H

#ifdef HAVE_GETPWNAM_R
/* Thread-safe getpwnam() function.
   Returned value should be freed using pa_getpwnam_free() when the caller is
   finished with the returned passwd data.

   API is the same as getpwnam(), errors are indicated by a NULL return;
   consult errno for the error cause (zero it before calling).
 */
struct passwd *pa_getpwnam_malloc(const char *name) {
    size_t buflen, getpw_buflen;
    int err;
    void *buf;
    void *getpw_buf;
    struct passwd *result = NULL;

    buflen = starting_getpw_buflen();
    buf = pa_xmalloc(buflen);

    getpw_buflen = buflen - sizeof(struct passwd);
    getpw_buf = (char *)buf + sizeof(struct passwd);

    while ((err = getpwnam_r(name, (struct passwd *)buf, getpw_buf, getpw_buflen, &result)) == ERANGE) {
        if (expand_buffer_trashcontents(&buf, &buflen))
            break;

        getpw_buflen = buflen - sizeof(struct passwd);
        getpw_buf = (char *)buf + sizeof(struct passwd);
    }

    if (err || !result) {
        result = NULL;
        if (buf) {
            pa_xfree(buf);
            buf = NULL;
        }
    }

    pa_assert(result == buf || result == NULL);

    return result;
}

void pa_getpwnam_free(struct passwd *passwd) {
    pa_xfree(passwd);
}

#else /* !HAVE_GETPWNAM_R */

struct passwd *pa_getpwnam_malloc(const char *name) {
    return getpwnam(name);
}

void pa_getpwnam_free(struct passwd *passwd) {
    /* nothing */
    return;
}

#endif /* !HAVE_GETPWNAM_R */

#ifdef HAVE_GETPWUID_R
/* Thread-safe getpwuid() function.
   Returned value should be freed using pa_getpwuid_free() when the caller is
   finished with the returned group data.

   API is the same as getpwuid(), errors are indicated by a NULL return;
   consult errno for the error cause (zero it before calling).
 */
struct passwd *pa_getpwuid_malloc(uid_t uid) {
    size_t buflen, getpw_buflen;
    int err;
    void *buf;
    void *getpw_buf;
    struct passwd *result = NULL;

    buflen = starting_getpw_buflen();
    buf = pa_xmalloc(buflen);

    getpw_buflen = buflen - sizeof(struct passwd);
    getpw_buf = (char *)buf + sizeof(struct passwd);

    while ((err = getpwuid_r(uid, (struct passwd *)buf, getpw_buf, getpw_buflen, &result)) == ERANGE) {
        if (expand_buffer_trashcontents(&buf, &buflen))
            break;

        getpw_buflen = buflen - sizeof(struct passwd);
        getpw_buf = (char *)buf + sizeof(struct passwd);
    }

    if (err || !result) {
        result = NULL;
        if (buf) {
            pa_xfree(buf);
            buf = NULL;
        }
    }

    pa_assert(result == buf || result == NULL);

    return result;
}

void pa_getpwuid_free(struct passwd *passwd) {
    pa_xfree(passwd);
}

#else /* !HAVE_GETPWUID_R */

struct passwd *pa_getpwuid_malloc(uid_t uid) {
    return getpwuid(uid);
}

void pa_getpwuid_free(struct passwd *passwd) {
    /* nothing */
    return;
}

#endif /* !HAVE_GETPWUID_R */

#endif /* HAVE_PWD_H */
