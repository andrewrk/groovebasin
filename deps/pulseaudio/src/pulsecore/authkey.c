/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <pulse/util.h>
#include <pulse/xmalloc.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/random.h>
#include <pulsecore/macro.h>

#include "authkey.h"

/* Generate a new authentication key, store it in file fd and return it in *data  */
static int generate(int fd, void *ret_data, size_t length) {
    ssize_t r;

    pa_assert(fd >= 0);
    pa_assert(ret_data);
    pa_assert(length > 0);

    pa_random(ret_data, length);

    lseek(fd, (off_t) 0, SEEK_SET);
    if (ftruncate(fd, (off_t) 0) < 0) {
        pa_log("Failed to truncate cookie file: %s", pa_cstrerror(errno));
        return -1;
    }

    if ((r = pa_loop_write(fd, ret_data, length, NULL)) < 0 || (size_t) r != length) {
        pa_log("Failed to write cookie file: %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

#ifndef O_BINARY
#define O_BINARY 0
#endif

/* Load an authentication cookie from file fn and store it in data. If
 * the cookie file doesn't exist, create it */
static int load(const char *fn, bool create, void *data, size_t length) {
    int fd = -1;
    int writable = 1;
    int unlock = 0, ret = -1;
    ssize_t r;

    pa_assert(fn);
    pa_assert(data);
    pa_assert(length > 0);

    if (create)
        pa_make_secure_parent_dir(fn, pa_in_system_mode() ? 0755U : 0700U, -1, -1, false);

    if ((fd = pa_open_cloexec(fn, (create ? O_RDWR|O_CREAT : O_RDONLY)|O_BINARY, S_IRUSR|S_IWUSR)) < 0) {

        if (!create || errno != EACCES || (fd = open(fn, O_RDONLY|O_BINARY)) < 0) {
            pa_log_warn("Failed to open cookie file '%s': %s", fn, pa_cstrerror(errno));
            goto finish;
        } else
            writable = 0;
    }

    unlock = pa_lock_fd(fd, 1) >= 0;

    if ((r = pa_loop_read(fd, data, length, NULL)) < 0) {
        pa_log("Failed to read cookie file '%s': %s", fn, pa_cstrerror(errno));
        goto finish;
    }

    if ((size_t) r != length) {
        pa_log_debug("Got %d bytes from cookie file '%s', expected %d", (int) r, fn, (int) length);

        if (!writable) {
            pa_log_warn("Unable to write cookie to read-only file");
            goto finish;
        }

        if (generate(fd, data, length) < 0)
            goto finish;
    }

    ret = 0;

finish:

    if (fd >= 0) {

        if (unlock)
            pa_lock_fd(fd, 0);

        if (pa_close(fd) < 0) {
            pa_log_warn("Failed to close cookie file: %s", pa_cstrerror(errno));
            ret = -1;
        }
    }

    return ret;
}

/* If the specified file path starts with / return it, otherwise
 * return path prepended with the config home directory. */
static int normalize_path(const char *fn, char **_r) {
    pa_assert(fn);
    pa_assert(_r);

    if (!pa_is_path_absolute(fn))
        return pa_append_to_config_home_dir(fn, _r);

    *_r = pa_xstrdup(fn);
    return 0;
}

int pa_authkey_load(const char *fn, bool create, void *data, size_t length) {
    char *p;
    int ret;

    pa_assert(fn);
    pa_assert(data);
    pa_assert(length > 0);

    if ((ret = normalize_path(fn, &p)) < 0)
        return ret;

    if ((ret = load(p, create, data, length)) < 0)
        pa_log_warn("Failed to load authentication key '%s': %s", p, (ret < 0) ? pa_cstrerror(errno) : "File corrupt");

    pa_xfree(p);

    return ret;
}

/* Store the specified cookie in the specified cookie file */
int pa_authkey_save(const char *fn, const void *data, size_t length) {
    int fd = -1;
    int unlock = 0, ret;
    ssize_t r;
    char *p;

    pa_assert(fn);
    pa_assert(data);
    pa_assert(length > 0);

    if ((ret = normalize_path(fn, &p)) < 0)
        return ret;

    if ((fd = pa_open_cloexec(p, O_RDWR|O_CREAT, S_IRUSR|S_IWUSR)) < 0) {
        pa_log_warn("Failed to open cookie file '%s': %s", fn, pa_cstrerror(errno));
        ret = -1;
        goto finish;
    }

    unlock = pa_lock_fd(fd, 1) >= 0;

    if ((r = pa_loop_write(fd, data, length, NULL)) < 0 || (size_t) r != length) {
        pa_log("Failed to read cookie file '%s': %s", fn, pa_cstrerror(errno));
        ret = -1;
        goto finish;
    }

finish:

    if (fd >= 0) {

        if (unlock)
            pa_lock_fd(fd, 0);

        if (pa_close(fd) < 0) {
            pa_log_warn("Failed to close cookie file: %s", pa_cstrerror(errno));
            ret = -1;
        }
    }

    pa_xfree(p);

    return ret;
}
