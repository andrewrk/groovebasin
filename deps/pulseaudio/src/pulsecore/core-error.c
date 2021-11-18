/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/utf8.h>
#include <pulse/xmalloc.h>

#include <pulsecore/thread.h>
#include <pulsecore/macro.h>
#include <pulsecore/log.h>

#include "core-error.h"

PA_STATIC_TLS_DECLARE(cstrerror, pa_xfree);

const char* pa_cstrerror(int errnum) {
    const char *original = NULL;
    char *translated, *t;
    char errbuf[128];

    if (errnum < 0)
        errnum = -errnum;

    if ((t = PA_STATIC_TLS_GET(cstrerror)))
        pa_xfree(t);

#if defined(HAVE_STRERROR_R) && defined(__GLIBC__)
    original = strerror_r(errnum, errbuf, sizeof(errbuf));
#elif defined(HAVE_STRERROR_R)
    if (strerror_r(errnum, errbuf, sizeof(errbuf)) == 0) {
        errbuf[sizeof(errbuf) - 1] = 0;
        original = errbuf;
    }
#else
    /* This might not be thread safe, but we hope for the best */
    original = strerror(errnum);
#endif

    /* The second condition is a Windows-ism */
    if (!original || !strcasecmp(original, "Unknown error")) {
        pa_snprintf(errbuf, sizeof(errbuf), "Unknown error %d", errnum);
        original = errbuf;
    }

    if (!(translated = pa_locale_to_utf8(original))) {
        pa_log_warn("Unable to convert error string to locale, filtering.");
        translated = pa_utf8_filter(original);
    }

    PA_STATIC_TLS_SET(cstrerror, translated);

    return translated;
}
