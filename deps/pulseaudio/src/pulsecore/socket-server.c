/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
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
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#ifndef SUN_LEN
#define SUN_LEN(ptr) \
    ((size_t)(((struct sockaddr_un *) 0)->sun_path) + strlen((ptr)->sun_path))
#endif
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_LIBWRAP
#include <tcpd.h>

/* Solaris requires that the allow_severity and deny_severity variables be
 * defined in the client program. */
#ifdef __sun
#include <syslog.h>
int allow_severity = LOG_INFO;
int deny_severity = LOG_WARNING;
#endif

#endif /* HAVE_LIBWRAP */

#ifdef HAVE_SYSTEMD_DAEMON
#include <systemd/sd-daemon.h>
#endif

#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/socket.h>
#include <pulsecore/socket-util.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-error.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/arpa-inet.h>

#include "socket-server.h"

struct pa_socket_server {
    PA_REFCNT_DECLARE;
    int fd;
    char *filename;
    bool activated;
    char *tcpwrap_service;

    pa_socket_server_on_connection_cb_t on_connection;
    void *userdata;

    pa_io_event *io_event;
    pa_mainloop_api *mainloop;
    enum {
        SOCKET_SERVER_IPV4,
        SOCKET_SERVER_UNIX,
        SOCKET_SERVER_IPV6
    } type;
};

static void callback(pa_mainloop_api *mainloop, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_socket_server *s = userdata;
    pa_iochannel *io;
    int nfd;

    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);
    pa_assert(s->mainloop == mainloop);
    pa_assert(s->io_event == e);
    pa_assert(e);
    pa_assert(fd >= 0);
    pa_assert(fd == s->fd);

    pa_socket_server_ref(s);

    if ((nfd = pa_accept_cloexec(fd, NULL, NULL)) < 0) {
        pa_log("accept(): %s", pa_cstrerror(errno));
        goto finish;
    }

    if (!s->on_connection) {
        pa_close(nfd);
        goto finish;
    }

#ifdef HAVE_LIBWRAP

    if (s->tcpwrap_service) {
        struct request_info req;

        request_init(&req, RQ_DAEMON, s->tcpwrap_service, RQ_FILE, nfd, NULL);
        fromhost(&req);
        if (!hosts_access(&req)) {
            pa_log_warn("TCP connection refused by tcpwrap.");
            pa_close(nfd);
            goto finish;
        }

        pa_log_info("TCP connection accepted by tcpwrap.");
    }
#endif

    /* There should be a check for socket type here */
    if (s->type == SOCKET_SERVER_IPV4)
        pa_make_tcp_socket_low_delay(nfd);
    else
        pa_make_socket_low_delay(nfd);

    pa_assert_se(io = pa_iochannel_new(s->mainloop, nfd, nfd));
    s->on_connection(s, io, s->userdata);

finish:
    pa_socket_server_unref(s);
}

static pa_socket_server* socket_server_new(pa_mainloop_api *m, int fd) {
    pa_socket_server *s;

    pa_assert(m);
    pa_assert(fd >= 0);

    s = pa_xnew0(pa_socket_server, 1);
    PA_REFCNT_INIT(s);
    s->fd = fd;
    s->mainloop = m;

    pa_assert_se(s->io_event = m->io_new(m, fd, PA_IO_EVENT_INPUT, callback, s));

    return s;
}

pa_socket_server* pa_socket_server_ref(pa_socket_server *s) {
    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);

    PA_REFCNT_INC(s);
    return s;
}

#ifdef HAVE_SYS_UN_H

pa_socket_server* pa_socket_server_new_unix(pa_mainloop_api *m, const char *filename) {
    int fd = -1;
    bool activated = false;
    struct sockaddr_un sa;
    pa_socket_server *s;

    pa_assert(m);
    pa_assert(filename);

#ifdef HAVE_SYSTEMD_DAEMON
    {
        int n = sd_listen_fds(0);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (sd_is_socket_unix(SD_LISTEN_FDS_START + i, SOCK_STREAM, 1, filename, 0) > 0) {
                    fd = SD_LISTEN_FDS_START + i;
                    activated = true;
                    pa_log_info("Found socket activation socket for '%s' \\o/", filename);
                    break;
                }
            }
        }
    }
#endif

    if (fd < 0) {
        if ((fd = pa_socket_cloexec(PF_UNIX, SOCK_STREAM, 0)) < 0) {
            pa_log("socket(PF_UNIX): %s", pa_cstrerror(errno));
            goto fail;
        }

        memset(&sa, 0, sizeof(sa));
        sa.sun_family = AF_UNIX;
        pa_strlcpy(sa.sun_path, filename, sizeof(sa.sun_path));

        pa_make_socket_low_delay(fd);

        if (bind(fd, (struct sockaddr*) &sa, (socklen_t) SUN_LEN(&sa)) < 0) {
            pa_log("bind(): %s", pa_cstrerror(errno));
            goto fail;
        }

        /* Allow access from all clients. Sockets like this one should
        * always be put inside a directory with proper access rights,
        * because not all OS check the access rights on the socket
        * inodes. */
        chmod(filename, 0777);

        if (listen(fd, 5) < 0) {
            pa_log("listen(): %s", pa_cstrerror(errno));
            goto fail;
        }
    }

    pa_assert_se(s = socket_server_new(m, fd));

    s->filename = pa_xstrdup(filename);
    s->type = SOCKET_SERVER_UNIX;
    s->activated = activated;

    return s;

fail:
    if (fd >= 0)
        pa_close(fd);

    return NULL;
}

#else /* HAVE_SYS_UN_H */

pa_socket_server* pa_socket_server_new_unix(pa_mainloop_api *m, const char *filename) {
    return NULL;
}

#endif /* HAVE_SYS_UN_H */

pa_socket_server* pa_socket_server_new_ipv4(pa_mainloop_api *m, uint32_t address, uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_socket_server *ss;
    int fd = -1;
    bool activated = false;
    struct sockaddr_in sa;
    int on = 1;

    pa_assert(m);
    pa_assert(port);

#ifdef HAVE_SYSTEMD_DAEMON
    {
        int n = sd_listen_fds(0);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (sd_is_socket_inet(SD_LISTEN_FDS_START + i, AF_INET, SOCK_STREAM, 1, port) > 0) {
                    fd = SD_LISTEN_FDS_START + i;
                    activated = true;
                    pa_log_info("Found socket activation socket for ipv4 in port '%d' \\o/", port);
                    break;
                }
            }
        }
    }
#endif

    if (fd < 0) {
        if ((fd = pa_socket_cloexec(PF_INET, SOCK_STREAM, 0)) < 0) {
            pa_log("socket(PF_INET): %s", pa_cstrerror(errno));
            goto fail;
        }

#ifdef SO_REUSEADDR
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &on, sizeof(on)) < 0)
            pa_log("setsockopt(): %s", pa_cstrerror(errno));
#endif

        pa_make_tcp_socket_low_delay(fd);

        memset(&sa, 0, sizeof(sa));
        sa.sin_family = AF_INET;
        sa.sin_port = htons(port);
        sa.sin_addr.s_addr = htonl(address);

        if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {

            if (errno == EADDRINUSE && fallback) {
                sa.sin_port = 0;

                if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
                    pa_log("bind(): %s", pa_cstrerror(errno));
                    goto fail;
                }
            } else {
                pa_log("bind(): %s", pa_cstrerror(errno));
                goto fail;
            }
        }

        if (listen(fd, 5) < 0) {
            pa_log("listen(): %s", pa_cstrerror(errno));
            goto fail;
        }
    }

    pa_assert_se(ss = socket_server_new(m, fd));

    ss->type = SOCKET_SERVER_IPV4;
    ss->tcpwrap_service = pa_xstrdup(tcpwrap_service);
    ss->activated = activated;

    return ss;

fail:
    if (fd >= 0)
        pa_close(fd);

    return NULL;
}

#ifdef HAVE_IPV6
pa_socket_server* pa_socket_server_new_ipv6(pa_mainloop_api *m, const uint8_t address[16], uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_socket_server *ss;
    int fd = -1;
    bool activated = false;
    struct sockaddr_in6 sa;
    int on;

    pa_assert(m);
    pa_assert(port > 0);

#ifdef HAVE_SYSTEMD_DAEMON
    {
        int n = sd_listen_fds(0);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                if (sd_is_socket_inet(SD_LISTEN_FDS_START + i, AF_INET6, SOCK_STREAM, 1, port) > 0) {
                    fd = SD_LISTEN_FDS_START + i;
                    activated = true;
                    pa_log_info("Found socket activation socket for ipv6 in port '%d' \\o/", port);
                    break;
                }
            }
        }
    }
#endif

    if (fd < 0) {
        if ((fd = pa_socket_cloexec(PF_INET6, SOCK_STREAM, 0)) < 0) {
            pa_log("socket(PF_INET6): %s", pa_cstrerror(errno));
            goto fail;
        }

#ifdef IPV6_V6ONLY
        on = 1;
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (const void *) &on, sizeof(on)) < 0)
            pa_log("setsockopt(IPPROTO_IPV6, IPV6_V6ONLY): %s", pa_cstrerror(errno));
#endif

#ifdef SO_REUSEADDR
        on = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *) &on, sizeof(on)) < 0)
            pa_log("setsockopt(SOL_SOCKET, SO_REUSEADDR, 1): %s", pa_cstrerror(errno));
#endif

        pa_make_tcp_socket_low_delay(fd);

        memset(&sa, 0, sizeof(sa));
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(port);
        memcpy(sa.sin6_addr.s6_addr, address, 16);

        if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {

            if (errno == EADDRINUSE && fallback) {
                sa.sin6_port = 0;

                if (bind(fd, (struct sockaddr *) &sa, sizeof(sa)) < 0) {
                    pa_log("bind(): %s", pa_cstrerror(errno));
                    goto fail;
                }
            } else {
                pa_log("bind(): %s", pa_cstrerror(errno));
                goto fail;
            }
        }

        if (listen(fd, 5) < 0) {
            pa_log("listen(): %s", pa_cstrerror(errno));
            goto fail;
        }
    }

    pa_assert_se(ss = socket_server_new(m, fd));

    ss->type = SOCKET_SERVER_IPV6;
    ss->tcpwrap_service = pa_xstrdup(tcpwrap_service);
    ss->activated = activated;

    return ss;

fail:
    if (fd >= 0)
        pa_close(fd);

    return NULL;
}
#endif

pa_socket_server* pa_socket_server_new_ipv4_loopback(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_assert(m);
    pa_assert(port > 0);

    return pa_socket_server_new_ipv4(m, INADDR_LOOPBACK, port, fallback, tcpwrap_service);
}

#ifdef HAVE_IPV6
pa_socket_server* pa_socket_server_new_ipv6_loopback(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_assert(m);
    pa_assert(port > 0);

    return pa_socket_server_new_ipv6(m, in6addr_loopback.s6_addr, port, fallback, tcpwrap_service);
}
#endif

pa_socket_server* pa_socket_server_new_ipv4_any(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_assert(m);
    pa_assert(port > 0);

    return pa_socket_server_new_ipv4(m, INADDR_ANY, port, fallback, tcpwrap_service);
}

#ifdef HAVE_IPV6
pa_socket_server* pa_socket_server_new_ipv6_any(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service) {
    pa_assert(m);
    pa_assert(port > 0);

    return pa_socket_server_new_ipv6(m, in6addr_any.s6_addr, port, fallback, tcpwrap_service);
}
#endif

pa_socket_server* pa_socket_server_new_ipv4_string(pa_mainloop_api *m, const char *name, uint16_t port, bool fallback, const char *tcpwrap_service) {
    struct in_addr ipv4;

    pa_assert(m);
    pa_assert(name);
    pa_assert(port > 0);

    if (inet_pton(AF_INET, name, &ipv4) > 0)
        return pa_socket_server_new_ipv4(m, ntohl(ipv4.s_addr), port, fallback, tcpwrap_service);

    return NULL;
}

#ifdef HAVE_IPV6
pa_socket_server* pa_socket_server_new_ipv6_string(pa_mainloop_api *m, const char *name, uint16_t port, bool fallback, const char *tcpwrap_service) {
    struct in6_addr ipv6;

    pa_assert(m);
    pa_assert(name);
    pa_assert(port > 0);

    if (inet_pton(AF_INET6, name, &ipv6) > 0)
        return pa_socket_server_new_ipv6(m, ipv6.s6_addr, port, fallback, tcpwrap_service);

    return NULL;
}
#endif

static void socket_server_free(pa_socket_server*s) {
    pa_assert(s);

    if (!s->activated && s->filename)
        unlink(s->filename);
    pa_xfree(s->filename);

    pa_close(s->fd);

    pa_xfree(s->tcpwrap_service);

    s->mainloop->io_free(s->io_event);
    pa_xfree(s);
}

void pa_socket_server_unref(pa_socket_server *s) {
    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);

    if (PA_REFCNT_DEC(s) <= 0)
        socket_server_free(s);
}

void pa_socket_server_set_callback(pa_socket_server*s, pa_socket_server_on_connection_cb_t on_connection, void *userdata) {
    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);

    s->on_connection = on_connection;
    s->userdata = userdata;
}

char *pa_socket_server_get_address(pa_socket_server *s, char *c, size_t l) {
    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);
    pa_assert(c);
    pa_assert(l > 0);

    switch (s->type) {
#ifdef HAVE_IPV6
        case SOCKET_SERVER_IPV6: {
            struct sockaddr_in6 sa;
            socklen_t sa_len = sizeof(sa);

            if (getsockname(s->fd, (struct sockaddr*) &sa, &sa_len) < 0) {
                pa_log("getsockname(): %s", pa_cstrerror(errno));
                return NULL;
            }

            if (memcmp(&in6addr_any, &sa.sin6_addr, sizeof(in6addr_any)) == 0) {
                char fqdn[256];
                if (!pa_get_fqdn(fqdn, sizeof(fqdn)))
                    return NULL;

                pa_snprintf(c, l, "tcp6:%s:%u", fqdn, (unsigned) ntohs(sa.sin6_port));

            } else if (memcmp(&in6addr_loopback, &sa.sin6_addr, sizeof(in6addr_loopback)) == 0) {
                char *id;

                if (!(id = pa_machine_id()))
                    return NULL;

                pa_snprintf(c, l, "{%s}tcp6:localhost:%u", id, (unsigned) ntohs(sa.sin6_port));
                pa_xfree(id);
            } else {
                char ip[INET6_ADDRSTRLEN];

                if (!inet_ntop(AF_INET6, &sa.sin6_addr, ip, sizeof(ip))) {
                    pa_log("inet_ntop(): %s", pa_cstrerror(errno));
                    return NULL;
                }

                pa_snprintf(c, l, "tcp6:[%s]:%u", ip, (unsigned) ntohs(sa.sin6_port));
            }

            return c;
        }
#endif

        case SOCKET_SERVER_IPV4: {
            struct sockaddr_in sa;
            socklen_t sa_len = sizeof(sa);

            if (getsockname(s->fd, (struct sockaddr*) &sa, &sa_len) < 0) {
                pa_log("getsockname(): %s", pa_cstrerror(errno));
                return NULL;
            }

            if (sa.sin_addr.s_addr == INADDR_ANY) {
                char fqdn[256];
                if (!pa_get_fqdn(fqdn, sizeof(fqdn)))
                    return NULL;

                pa_snprintf(c, l, "tcp:%s:%u", fqdn, (unsigned) ntohs(sa.sin_port));
            } else if (sa.sin_addr.s_addr == INADDR_LOOPBACK) {
                char *id;

                if (!(id = pa_machine_id()))
                    return NULL;

                pa_snprintf(c, l, "{%s}tcp:localhost:%u", id, (unsigned) ntohs(sa.sin_port));
                pa_xfree(id);
            } else {
                char ip[INET_ADDRSTRLEN];

                if (!inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip))) {
                    pa_log("inet_ntop(): %s", pa_cstrerror(errno));
                    return NULL;
                }

                pa_snprintf(c, l, "tcp:[%s]:%u", ip, (unsigned) ntohs(sa.sin_port));
            }

            return c;
        }

        case SOCKET_SERVER_UNIX: {
            char *id;

            if (!s->filename)
                return NULL;

            if (!(id = pa_machine_id()))
                return NULL;

            pa_snprintf(c, l, "{%s}unix:%s", id, s->filename);
            pa_xfree(id);
            return c;
        }

        default:
            return NULL;
    }
}
