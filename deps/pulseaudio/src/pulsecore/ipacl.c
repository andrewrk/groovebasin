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

#include <sys/types.h>
#include <sys/types.h>
#include <string.h>

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/llist.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/socket.h>
#include <pulsecore/arpa-inet.h>

#include "ipacl.h"

struct acl_entry {
    PA_LLIST_FIELDS(struct acl_entry);
    int family;
    struct in_addr address_ipv4;
#ifdef HAVE_IPV6
    struct in6_addr address_ipv6;
#endif
    int bits;
};

struct pa_ip_acl {
    PA_LLIST_HEAD(struct acl_entry, entries);
};

pa_ip_acl* pa_ip_acl_new(const char *s) {
    const char *state = NULL;
    char *a;
    pa_ip_acl *acl;

    pa_assert(s);

    acl = pa_xnew(pa_ip_acl, 1);
    PA_LLIST_HEAD_INIT(struct acl_entry, acl->entries);

    while ((a = pa_split(s, ";", &state))) {
        char *slash;
        struct acl_entry e, *n;
        uint32_t bits;

        if ((slash = strchr(a, '/'))) {
            *slash = 0;
            slash++;
            if (pa_atou(slash, &bits) < 0) {
                pa_log_warn("Failed to parse number of bits: %s", slash);
                goto fail;
            }
        } else
            bits = (uint32_t) -1;

        if (inet_pton(AF_INET, a, &e.address_ipv4) > 0) {

            e.bits = bits == (uint32_t) -1 ? 32 : (int) bits;

            if (e.bits > 32) {
                pa_log_warn("Number of bits out of range: %i", e.bits);
                goto fail;
            }

            e.family = AF_INET;

            if (e.bits < 32 && (uint32_t) (ntohl(e.address_ipv4.s_addr) << e.bits) != 0)
                pa_log_warn("Host part of ACL entry '%s/%u' is not zero!", a, e.bits);

#ifdef HAVE_IPV6
        } else if (inet_pton(AF_INET6, a, &e.address_ipv6) > 0) {

            e.bits = bits == (uint32_t) -1 ? 128 : (int) bits;

            if (e.bits > 128) {
                pa_log_warn("Number of bits out of range: %i", e.bits);
                goto fail;
            }
            e.family = AF_INET6;

            if (e.bits < 128) {
                int t = 0, i;

                for (i = 0, bits = (uint32_t) e.bits; i < 16; i++) {

                    if (bits >= 8)
                        bits -= 8;
                    else {
                        if ((uint8_t) ((e.address_ipv6.s6_addr[i]) << bits) != 0) {
                            t = 1;
                            break;
                        }
                        bits = 0;
                    }
                }

                if (t)
                    pa_log_warn("Host part of ACL entry '%s/%u' is not zero!", a, e.bits);
            }
#endif

        } else {
            pa_log_warn("Failed to parse address: %s", a);
            goto fail;
        }

        n = pa_xmemdup(&e, sizeof(struct acl_entry));
        PA_LLIST_PREPEND(struct acl_entry, acl->entries, n);

        pa_xfree(a);
    }

    return acl;

fail:
    pa_xfree(a);
    pa_ip_acl_free(acl);

    return NULL;
}

void pa_ip_acl_free(pa_ip_acl *acl) {
    pa_assert(acl);

    while (acl->entries) {
        struct acl_entry *e = acl->entries;
        PA_LLIST_REMOVE(struct acl_entry, acl->entries, e);
        pa_xfree(e);
    }

    pa_xfree(acl);
}

int pa_ip_acl_check(pa_ip_acl *acl, int fd) {
    struct sockaddr_storage sa;
    struct acl_entry *e;
    socklen_t salen;

    pa_assert(acl);
    pa_assert(fd >= 0);

    salen = sizeof(sa);
    if (getpeername(fd, (struct sockaddr*) &sa, &salen) < 0)
        return -1;

#ifdef HAVE_IPV6
    if (sa.ss_family != AF_INET && sa.ss_family != AF_INET6)
#else
    if (sa.ss_family != AF_INET)
#endif
        return -1;

    if (sa.ss_family == AF_INET && salen != sizeof(struct sockaddr_in))
        return -1;

#ifdef HAVE_IPV6
    if (sa.ss_family == AF_INET6 && salen != sizeof(struct sockaddr_in6))
        return -1;
#endif

    for (e = acl->entries; e; e = e->next) {

        if (e->family != sa.ss_family)
            continue;

        if (e->family == AF_INET) {
            struct sockaddr_in *sai = (struct sockaddr_in*) &sa;

            if (e->bits == 0 || /* this needs special handling because >> takes the right-hand side modulo 32 */
                (ntohl(sai->sin_addr.s_addr ^ e->address_ipv4.s_addr) >> (32 - e->bits)) == 0)
                return 1;
#ifdef HAVE_IPV6
        } else if (e->family == AF_INET6) {
            int i, bits;
            struct sockaddr_in6 *sai = (struct sockaddr_in6*) &sa;

            if (e->bits == 128)
                return memcmp(&sai->sin6_addr, &e->address_ipv6, 16) == 0;

            if (e->bits == 0)
                return 1;

            for (i = 0, bits = e->bits; i < 16; i++) {

                if (bits >= 8) {
                    if (sai->sin6_addr.s6_addr[i] != e->address_ipv6.s6_addr[i])
                        break;

                    bits -= 8;
                } else {
                    if ((sai->sin6_addr.s6_addr[i] ^ e->address_ipv6.s6_addr[i]) >> (8 - bits) != 0)
                        break;

                    bits = 0;
                }

                if (bits == 0)
                    return 1;
            }
#endif
        }
    }

    return 0;
}
