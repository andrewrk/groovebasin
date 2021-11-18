/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2004 Joe Marcus Clarke
  Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYSTEMD_DAEMON
#include <systemd/sd-daemon.h>
#endif

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/socket.h>
#include <pulsecore/arpa-inet.h>

#include "socket-util.h"

void pa_socket_peer_to_string(int fd, char *c, size_t l) {
#ifndef OS_IS_WIN32
    struct stat st;
#endif

    pa_assert(fd >= 0);
    pa_assert(c);
    pa_assert(l > 0);

#ifndef OS_IS_WIN32
    pa_assert_se(fstat(fd, &st) == 0);

    if (S_ISSOCK(st.st_mode))
#endif
    {
        union {
            struct sockaddr_storage storage;
            struct sockaddr sa;
            struct sockaddr_in in;
#ifdef HAVE_IPV6
            struct sockaddr_in6 in6;
#endif
#ifdef HAVE_SYS_UN_H
            struct sockaddr_un un;
#endif
        } sa;
        socklen_t sa_len = sizeof(sa);

        if (getpeername(fd, &sa.sa, &sa_len) >= 0) {

            if (sa.sa.sa_family == AF_INET) {
                uint32_t ip = ntohl(sa.in.sin_addr.s_addr);

                pa_snprintf(c, l, "TCP/IP client from %i.%i.%i.%i:%u",
                            ip >> 24,
                            (ip >> 16) & 0xFF,
                            (ip >> 8) & 0xFF,
                            ip & 0xFF,
                            ntohs(sa.in.sin_port));
                return;
#ifdef HAVE_IPV6
            } else if (sa.sa.sa_family == AF_INET6) {
                char buf[INET6_ADDRSTRLEN];
                const char *res;

                res = inet_ntop(AF_INET6, &sa.in6.sin6_addr, buf, sizeof(buf));
                if (res) {
                    pa_snprintf(c, l, "TCP/IP client from [%s]:%u", buf, ntohs(sa.in6.sin6_port));
                    return;
                }
#endif
#ifdef HAVE_SYS_UN_H
            } else if (sa.sa.sa_family == AF_UNIX) {
                pa_snprintf(c, l, "UNIX socket client");
                return;
#endif
            }
        }

        pa_snprintf(c, l, "Unknown network client");
        return;
    }
#ifndef OS_IS_WIN32
    else if (S_ISCHR(st.st_mode) && (fd == 0 || fd == 1)) {
        pa_snprintf(c, l, "STDIN/STDOUT client");
        return;
    }
#endif /* OS_IS_WIN32 */

    pa_snprintf(c, l, "Unknown client");
}

void pa_make_socket_low_delay(int fd) {

#ifdef SO_PRIORITY
    int priority;
    pa_assert(fd >= 0);

    priority = 6;
    if (setsockopt(fd, SOL_SOCKET, SO_PRIORITY, (const void *) &priority, sizeof(priority)) < 0)
        pa_log_warn("SO_PRIORITY failed: %s", pa_cstrerror(errno));
#endif
}

void pa_make_tcp_socket_low_delay(int fd) {
    pa_assert(fd >= 0);

    pa_make_socket_low_delay(fd);

#if defined(SOL_TCP) || defined(IPPROTO_TCP)
    {
        int on = 1;
#if defined(SOL_TCP)
        if (setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *) &on, sizeof(on)) < 0)
#else
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const void *) &on, sizeof(on)) < 0)
#endif
            pa_log_warn("TCP_NODELAY failed: %s", pa_cstrerror(errno));
    }
#endif

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || defined(IPPROTO_IP))
    {
        int tos = IPTOS_LOWDELAY;
#ifdef SOL_IP
        if (setsockopt(fd, SOL_IP, IP_TOS, (const void *) &tos, sizeof(tos)) < 0)
#else
        if (setsockopt(fd, IPPROTO_IP, IP_TOS, (const void *) &tos, sizeof(tos)) < 0)
#endif
            pa_log_warn("IP_TOS failed: %s", pa_cstrerror(errno));
    }
#endif
}

void pa_make_udp_socket_low_delay(int fd) {
    pa_assert(fd >= 0);

    pa_make_socket_low_delay(fd);

#if defined(IPTOS_LOWDELAY) && defined(IP_TOS) && (defined(SOL_IP) || defined(IPPROTO_IP))
    {
        int tos = IPTOS_LOWDELAY;
#ifdef SOL_IP
        if (setsockopt(fd, SOL_IP, IP_TOS, (const void *) &tos, sizeof(tos)) < 0)
#else
        if (setsockopt(fd, IPPROTO_IP, IP_TOS, (const void *) &tos, sizeof(tos)) < 0)
#endif
            pa_log_warn("IP_TOS failed: %s", pa_cstrerror(errno));
    }
#endif
}

int pa_socket_set_rcvbuf(int fd, size_t l) {
    int bufsz = (int) l;

    pa_assert(fd >= 0);

    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const void *) &bufsz, sizeof(bufsz)) < 0) {
        pa_log_warn("SO_RCVBUF: %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

int pa_socket_set_sndbuf(int fd, size_t l) {
    int bufsz = (int) l;

    pa_assert(fd >= 0);

    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const void *) &bufsz, sizeof(bufsz)) < 0) {
        pa_log_warn("SO_SNDBUF: %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

#ifdef HAVE_SYS_UN_H

int pa_unix_socket_is_stale(const char *fn) {
    struct sockaddr_un sa;
    int fd = -1, ret = -1;

    pa_assert(fn);

    if ((fd = pa_socket_cloexec(PF_UNIX, SOCK_STREAM, 0)) < 0) {
        pa_log("socket(): %s", pa_cstrerror(errno));
        goto finish;
    }

    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, fn, sizeof(sa.sun_path)-1);
    sa.sun_path[sizeof(sa.sun_path) - 1] = 0;

    if (connect(fd, (struct sockaddr*) &sa, sizeof(sa)) < 0) {
        if (errno == ECONNREFUSED)
            ret = 1;
    } else
        ret = 0;

finish:
    if (fd >= 0)
        pa_close(fd);

    return ret;
}

int pa_unix_socket_remove_stale(const char *fn) {
    int r;

    pa_assert(fn);

#ifdef HAVE_SYSTEMD_DAEMON
    {
        int n = sd_listen_fds(0);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (sd_is_socket_unix(SD_LISTEN_FDS_START + i, SOCK_STREAM, 1, fn, 0) > 0) {
                    /* This is a socket activated socket, therefore do not consider
                    * it stale. */
                    return 0;
                }
            }
        }
    }
#endif

    if ((r = pa_unix_socket_is_stale(fn)) < 0)
        return errno != ENOENT ? -1 : 0;

    if (!r)
        return 0;

    /* Yes, here is a race condition. But who cares? */
    if (unlink(fn) < 0)
        return -1;

    return 0;
}

#else /* HAVE_SYS_UN_H */

int pa_unix_socket_is_stale(const char *fn) {
    return -1;
}

int pa_unix_socket_remove_stale(const char *fn) {
    return -1;
}

#endif /* HAVE_SYS_UN_H */

bool pa_socket_address_is_local(const struct sockaddr *sa) {
    pa_assert(sa);

    switch (sa->sa_family) {
        case AF_UNIX:
            return true;

        case AF_INET:
            return ((const struct sockaddr_in*) sa)->sin_addr.s_addr == INADDR_LOOPBACK;

#ifdef HAVE_IPV6
        case AF_INET6:
            return memcmp(&((const struct sockaddr_in6*) sa)->sin6_addr, &in6addr_loopback, sizeof(struct in6_addr)) == 0;
#endif

        default:
            return false;
    }
}

bool pa_socket_is_local(int fd) {

    union {
        struct sockaddr_storage storage;
        struct sockaddr sa;
        struct sockaddr_in in;
#ifdef HAVE_IPV6
        struct sockaddr_in6 in6;
#endif
#ifdef HAVE_SYS_UN_H
        struct sockaddr_un un;
#endif
    } sa;
    socklen_t sa_len = sizeof(sa);

    if (getpeername(fd, &sa.sa, &sa_len) < 0)
        return false;

    return pa_socket_address_is_local(&sa.sa);
}
