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

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#include <wincrypt.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "random.h"

static bool has_whined = false;

static const char * const devices[] = { "/dev/urandom", "/dev/random", NULL };

static int random_proper(void *ret_data, size_t length) {
#ifdef OS_IS_WIN32
    int ret = -1;

    HCRYPTPROV hCryptProv = 0;

    pa_assert(ret_data);
    pa_assert(length > 0);

    if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        if (CryptGenRandom(hCryptProv, length, ret_data))
            ret = 0;
        CryptReleaseContext(hCryptProv, 0);
    }

    return ret;

#else /* OS_IS_WIN32 */

    int fd, ret = -1;
    ssize_t r = 0;
    const char *const * device;

    pa_assert(ret_data);
    pa_assert(length > 0);

    device = devices;

    while (*device) {
        ret = 0;

        if ((fd = pa_open_cloexec(*device, O_RDONLY, 0)) >= 0) {

            if ((r = pa_loop_read(fd, ret_data, length, NULL)) < 0 || (size_t) r != length)
                ret = -1;

            pa_close(fd);
        } else
            ret = -1;

        if (ret == 0)
            break;

        device++;
    }

    return ret;
#endif /* OS_IS_WIN32 */
}

void pa_random_seed(void) {
    unsigned int seed;

    if (random_proper(&seed, sizeof(unsigned int)) < 0) {

        if (!has_whined) {
            pa_log_warn("Failed to get proper entropy. Falling back to seeding with current time.");
            has_whined = true;
        }

        seed = (unsigned int) time(NULL);
    }

    srand(seed);
}

void pa_random(void *ret_data, size_t length) {
    uint8_t *p;
    size_t l;

    pa_assert(ret_data);
    pa_assert(length > 0);

    if (random_proper(ret_data, length) >= 0)
        return;

    if (!has_whined) {
        pa_log_warn("Failed to get proper entropy. Falling back to unsecure pseudo RNG.");
        has_whined = true;
    }

    for (p = ret_data, l = length; l > 0; p++, l--)
        *p = (uint8_t) rand();
}
