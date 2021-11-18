/***
    This file is part of PulseAudio.

    Copyright 2004-2006 Lennart Poettering
    Copyright 2006-2007 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

/* #undef HAVE_LIBASYNCNS */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_LIBASYNCNS
#include <asyncns.h>
#endif

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/socket.h>
#include <pulsecore/socket-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/socket-util.h>
#include <pulsecore/log.h>
#include <pulsecore/parseaddr.h>
#include <pulsecore/macro.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/arpa-inet.h>

#include "socket-client.h"

#define CONNECT_TIMEOUT 5

struct pa_socket_client {
    PA_REFCNT_DECLARE;
    int fd;

    pa_mainloop_api *mainloop;
    pa_io_event *io_event;
    pa_time_event *timeout_event;
    pa_defer_event *defer_event;

    pa_socket_client_cb_t callback;
    void *userdata;

    bool local;

#ifdef HAVE_LIBASYNCNS
    asyncns_t *asyncns;
    asyncns_query_t * asyncns_query;
    pa_io_event *asyncns_io_event;
#endif
};

static pa_socket_client* socket_client_new(pa_mainloop_api *m) {
    pa_socket_client *c;
    pa_assert(m);

    c = pa_xnew0(pa_socket_client, 1);
    PA_REFCNT_INIT(c);
    c->mainloop = m;
    c->fd = -1;

    return c;
}

static void free_events(pa_socket_client *c) {
    pa_assert(c);

    if (c->io_event) {
        c->mainloop->io_free(c->io_event);
        c->io_event = NULL;
    }

    if (c->timeout_event) {
        c->mainloop->time_free(c->timeout_event);
        c->timeout_event = NULL;
    }

    if (c->defer_event) {
        c->mainloop->defer_free(c->defer_event);
        c->defer_event = NULL;
    }
}

static void do_call(pa_socket_client *c) {
    pa_iochannel *io = NULL;
    int error;
    socklen_t lerror;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->callback);

    pa_socket_client_ref(c);

    if (c->fd < 0)
        goto finish;

    lerror = sizeof(error);
    if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, (void*)&error, &lerror) < 0) {
        pa_log("getsockopt(): %s", pa_cstrerror(errno));
        goto finish;
    }

    if (lerror != sizeof(error)) {
        pa_log("getsockopt() returned invalid size.");
        goto finish;
    }

    if (error != 0) {
        pa_log_debug("connect(): %s", pa_cstrerror(error));
        errno = error;
        goto finish;
    }

    io = pa_iochannel_new(c->mainloop, c->fd, c->fd);

finish:
    if (!io && c->fd >= 0)
        pa_close(c->fd);
    c->fd = -1;

    free_events(c);

    c->callback(c, io, c->userdata);

    pa_socket_client_unref(c);
}

static void connect_defer_cb(pa_mainloop_api *m, pa_defer_event *e, void *userdata) {
    pa_socket_client *c = userdata;

    pa_assert(m);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->defer_event == e);

    do_call(c);
}

static void connect_io_cb(pa_mainloop_api*m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_socket_client *c = userdata;

    pa_assert(m);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->io_event == e);
    pa_assert(fd >= 0);

    do_call(c);
}

static int do_connect(pa_socket_client *c, const struct sockaddr *sa, socklen_t len) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(sa);
    pa_assert(len > 0);

    pa_make_fd_nonblock(c->fd);

    if (connect(c->fd, sa, len) < 0) {
#ifdef OS_IS_WIN32
        if (WSAGetLastError() != EWOULDBLOCK) {
            pa_log_debug("connect(): %d", WSAGetLastError());
#else
        if (errno != EINPROGRESS) {
            pa_log_debug("connect(): %s (%d)", pa_cstrerror(errno), errno);
#endif
            return -1;
        }

        c->io_event = c->mainloop->io_new(c->mainloop, c->fd, PA_IO_EVENT_OUTPUT, connect_io_cb, c);
    } else
        c->defer_event = c->mainloop->defer_new(c->mainloop, connect_defer_cb, c);

    return 0;
}

pa_socket_client* pa_socket_client_new_ipv4(pa_mainloop_api *m, uint32_t address, uint16_t port) {
    struct sockaddr_in sa;

    pa_assert(m);
    pa_assert(port > 0);

    pa_zero(sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(address);

    return pa_socket_client_new_sockaddr(m, (struct sockaddr*) &sa, sizeof(sa));
}

pa_socket_client* pa_socket_client_new_unix(pa_mainloop_api *m, const char *filename) {
#ifdef HAVE_SYS_UN_H
    struct sockaddr_un sa;

    pa_assert(m);
    pa_assert(filename);

    pa_zero(sa);
    sa.sun_family = AF_UNIX;
    pa_strlcpy(sa.sun_path, filename, sizeof(sa.sun_path));

    return pa_socket_client_new_sockaddr(m, (struct sockaddr*) &sa, sizeof(sa));
#else /* HAVE_SYS_UN_H */

    return NULL;
#endif /* HAVE_SYS_UN_H */
}

static int sockaddr_prepare(pa_socket_client *c, const struct sockaddr *sa, size_t salen) {
    pa_assert(c);
    pa_assert(sa);
    pa_assert(salen);

    c->local = pa_socket_address_is_local(sa);

    if ((c->fd = pa_socket_cloexec(sa->sa_family, SOCK_STREAM, 0)) < 0) {
        pa_log("socket(): %s", pa_cstrerror(errno));
        return -1;
    }

#ifdef HAVE_IPV6
    if (sa->sa_family == AF_INET || sa->sa_family == AF_INET6)
#else
    if (sa->sa_family == AF_INET)
#endif
        pa_make_tcp_socket_low_delay(c->fd);
    else
        pa_make_socket_low_delay(c->fd);

    if (do_connect(c, sa, (socklen_t) salen) < 0)
        return -1;

    return 0;
}

pa_socket_client* pa_socket_client_new_sockaddr(pa_mainloop_api *m, const struct sockaddr *sa, size_t salen) {
    pa_socket_client *c;

    pa_assert(m);
    pa_assert(sa);
    pa_assert(salen > 0);

    c = socket_client_new(m);

    if (sockaddr_prepare(c, sa, salen) < 0)
        goto fail;

    return c;

fail:
    pa_socket_client_unref(c);
    return NULL;
}

static void socket_client_free(pa_socket_client *c) {
    pa_assert(c);
    pa_assert(c->mainloop);

    free_events(c);

    if (c->fd >= 0)
        pa_close(c->fd);

#ifdef HAVE_LIBASYNCNS
    if (c->asyncns_query)
        asyncns_cancel(c->asyncns, c->asyncns_query);
    if (c->asyncns)
        asyncns_free(c->asyncns);
    if (c->asyncns_io_event)
        c->mainloop->io_free(c->asyncns_io_event);
#endif

    pa_xfree(c);
}

void pa_socket_client_unref(pa_socket_client *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (PA_REFCNT_DEC(c) <= 0)
        socket_client_free(c);
}

pa_socket_client* pa_socket_client_ref(pa_socket_client *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_REFCNT_INC(c);
    return c;
}

void pa_socket_client_set_callback(pa_socket_client *c, pa_socket_client_cb_t on_connection, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    c->callback = on_connection;
    c->userdata = userdata;
}

pa_socket_client* pa_socket_client_new_ipv6(pa_mainloop_api *m, uint8_t address[16], uint16_t port) {
#ifdef HAVE_IPV6
    struct sockaddr_in6 sa;

    pa_assert(m);
    pa_assert(address);
    pa_assert(port > 0);

    pa_zero(sa);
    sa.sin6_family = AF_INET6;
    sa.sin6_port = htons(port);
    memcpy(&sa.sin6_addr, address, sizeof(sa.sin6_addr));

    return pa_socket_client_new_sockaddr(m, (struct sockaddr*) &sa, sizeof(sa));

#else
    return NULL;
#endif
}

#ifdef HAVE_LIBASYNCNS

static void asyncns_cb(pa_mainloop_api*m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_socket_client *c = userdata;
    struct addrinfo *res = NULL;
    int ret;

    pa_assert(m);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->asyncns_io_event == e);
    pa_assert(fd >= 0);

    if (asyncns_wait(c->asyncns, 0) < 0)
        goto fail;

    if (!asyncns_isdone(c->asyncns, c->asyncns_query))
        return;

    ret = asyncns_getaddrinfo_done(c->asyncns, c->asyncns_query, &res);
    c->asyncns_query = NULL;

    if (ret != 0 || !res)
        goto fail;

    if (res->ai_addr)
        if (sockaddr_prepare(c, res->ai_addr, res->ai_addrlen) < 0)
            goto fail;

    asyncns_freeaddrinfo(res);

    m->io_free(c->asyncns_io_event);
    c->asyncns_io_event = NULL;
    return;

fail:
    m->io_free(c->asyncns_io_event);
    c->asyncns_io_event = NULL;

    errno = EHOSTUNREACH;
    do_call(c);
    return;

}

#endif

static void timeout_cb(pa_mainloop_api *m, pa_time_event *e, const struct timeval *t, void *userdata) {
    pa_socket_client *c = userdata;

    pa_assert(m);
    pa_assert(e);
    pa_assert(c);

    if (c->fd >= 0) {
        pa_close(c->fd);
        c->fd = -1;
    }

    errno = ETIMEDOUT;
    do_call(c);
}

static void start_timeout(pa_socket_client *c, bool use_rtclock) {
    struct timeval tv;

    pa_assert(c);
    pa_assert(!c->timeout_event);

    c->timeout_event = c->mainloop->time_new(c->mainloop, pa_timeval_rtstore(&tv, pa_rtclock_now() + CONNECT_TIMEOUT * PA_USEC_PER_SEC, use_rtclock), timeout_cb, c);
}

pa_socket_client* pa_socket_client_new_string(pa_mainloop_api *m, bool use_rtclock, const char*name, uint16_t default_port) {
    pa_socket_client *c = NULL;
    pa_parsed_address a;
    char *name_buf;

    pa_assert(m);
    pa_assert(name);

    a.path_or_host = NULL;

    if (pa_is_ip6_address(name)) {
        size_t len = strlen(name);
        name_buf = pa_xmalloc(len + 3);
        memcpy(name_buf + 1, name, len);
        name_buf[0] = '[';
        name_buf[len + 1] = ']';
        name_buf[len + 2] = '\0';
    } else {
        name_buf = pa_xstrdup(name);
    }

    if (pa_parse_address(name_buf, &a) < 0) {
        pa_log_warn("parsing address failed: %s", name_buf);
        goto finish;
    }

    if (!a.port)
        a.port = default_port;

    switch (a.type) {
        case PA_PARSED_ADDRESS_UNIX:
            if ((c = pa_socket_client_new_unix(m, a.path_or_host)))
                start_timeout(c, use_rtclock);
            break;

        case PA_PARSED_ADDRESS_TCP4:  /* Fallthrough */
        case PA_PARSED_ADDRESS_TCP6:  /* Fallthrough */
        case PA_PARSED_ADDRESS_TCP_AUTO: {
            struct addrinfo hints;
            char port[12];

            pa_snprintf(port, sizeof(port), "%u", (unsigned) a.port);

            pa_zero(hints);
            if (a.type == PA_PARSED_ADDRESS_TCP4)
                hints.ai_family = PF_INET;
#ifdef HAVE_IPV6
            else if (a.type == PA_PARSED_ADDRESS_TCP6)
                hints.ai_family = PF_INET6;
#endif
            else
                hints.ai_family = PF_UNSPEC;

            hints.ai_socktype = SOCK_STREAM;

#if defined(HAVE_LIBASYNCNS)
            {
                asyncns_t *asyncns;

                if (!(asyncns = asyncns_new(1)))
                    goto finish;

                c = socket_client_new(m);
                c->asyncns = asyncns;
                c->asyncns_io_event = m->io_new(m, asyncns_fd(c->asyncns), PA_IO_EVENT_INPUT, asyncns_cb, c);
                pa_assert_se(c->asyncns_query = asyncns_getaddrinfo(c->asyncns, a.path_or_host, port, &hints));
                start_timeout(c, use_rtclock);
            }
#elif defined(HAVE_GETADDRINFO)
            {
                int ret;
                struct addrinfo *res = NULL;

                ret = getaddrinfo(a.path_or_host, port, &hints, &res);

                if (ret < 0 || !res)
                    goto finish;

                if (res->ai_addr) {
                    if ((c = pa_socket_client_new_sockaddr(m, res->ai_addr, res->ai_addrlen)))
                        start_timeout(c, use_rtclock);
                }

                freeaddrinfo(res);
            }
#else
            {
                struct hostent *host = NULL;
                struct sockaddr_in s;

#ifdef HAVE_IPV6
                /* FIXME: PF_INET6 support */
                if (hints.ai_family == PF_INET6) {
                    pa_log_error("IPv6 is not supported on Windows");
                    goto finish;
                }
#endif

                host = gethostbyname(a.path_or_host);
                if (!host) {
                    unsigned int addr = inet_addr(a.path_or_host);
                    if (addr != INADDR_NONE)
                        host = gethostbyaddr((char*)&addr, 4, AF_INET);
                }

                if (!host)
                    goto finish;

                pa_zero(s);
                s.sin_family = AF_INET;
                memcpy(&s.sin_addr, host->h_addr, sizeof(struct in_addr));
                s.sin_port = htons(a.port);

                if ((c = pa_socket_client_new_sockaddr(m, (struct sockaddr*)&s, sizeof(s))))
                    start_timeout(c, use_rtclock);
            }
#endif /* HAVE_LIBASYNCNS */
        }
    }

finish:
    pa_xfree(name_buf);
    pa_xfree(a.path_or_host);
    return c;

}

/* Return non-zero when the target sockaddr is considered
   local. "local" means UNIX socket or TCP socket on localhost. Other
   local IP addresses are not considered local. */
bool pa_socket_client_is_local(pa_socket_client *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    return c->local;
}
