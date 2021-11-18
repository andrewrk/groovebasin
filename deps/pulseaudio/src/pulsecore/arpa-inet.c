/***
  This file is part of PulseAudio.

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

#if !defined(HAVE_ARPA_INET_H) && defined(OS_IS_WIN32)

#include <errno.h>

#include <pulsecore/macro.h>
#include <pulsecore/socket.h>
#include <pulsecore/core-util.h>

#include "arpa-inet.h"

const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt) {
    struct in_addr *in = (struct in_addr*)src;
#ifdef HAVE_IPV6
    struct in6_addr *in6 = (struct in6_addr*)src;
#endif

    pa_assert(src);
    pa_assert(dst);

    switch (af) {
    case AF_INET:
        pa_snprintf(dst, cnt, "%d.%d.%d.%d",
#ifdef WORDS_BIGENDIAN
            (int)(in->s_addr >> 24) & 0xff,
            (int)(in->s_addr >> 16) & 0xff,
            (int)(in->s_addr >>  8) & 0xff,
            (int)(in->s_addr >>  0) & 0xff);
#else
            (int)(in->s_addr >>  0) & 0xff,
            (int)(in->s_addr >>  8) & 0xff,
            (int)(in->s_addr >> 16) & 0xff,
            (int)(in->s_addr >> 24) & 0xff);
#endif
        break;
#ifdef HAVE_IPV6
    case AF_INET6:
        pa_snprintf(dst, cnt, "%x:%x:%x:%x:%x:%x:%x:%x",
            in6->s6_addr[ 0] << 8 | in6->s6_addr[ 1],
            in6->s6_addr[ 2] << 8 | in6->s6_addr[ 3],
            in6->s6_addr[ 4] << 8 | in6->s6_addr[ 5],
            in6->s6_addr[ 6] << 8 | in6->s6_addr[ 7],
            in6->s6_addr[ 8] << 8 | in6->s6_addr[ 9],
            in6->s6_addr[10] << 8 | in6->s6_addr[11],
            in6->s6_addr[12] << 8 | in6->s6_addr[13],
            in6->s6_addr[14] << 8 | in6->s6_addr[15]);
        break;
#endif
    default:
        errno = EAFNOSUPPORT;
        return NULL;
    }

    return dst;
}

int inet_pton(int af, const char *src, void *dst) {
    struct in_addr *in = (struct in_addr*)dst;
#ifdef HAVE_IPV6
    struct in6_addr *in6 = (struct in6_addr*)dst;
#endif

    pa_assert(src);
    pa_assert(dst);

    switch (af) {
    case AF_INET:
        in->s_addr = inet_addr(src);
        if (in->s_addr == INADDR_NONE)
            return 0;
        break;
#ifdef HAVE_IPV6
    case AF_INET6:
        /* FIXME */
#endif
    default:
        errno = EAFNOSUPPORT;
        return -1;
    }

    return 1;
}

#endif
