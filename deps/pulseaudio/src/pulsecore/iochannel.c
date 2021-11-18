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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/core-error.h>
#include <pulsecore/core-util.h>
#include <pulsecore/socket.h>
#include <pulsecore/socket-util.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "iochannel.h"

struct pa_iochannel {
    int ifd, ofd;
    int ifd_type, ofd_type;
    pa_mainloop_api* mainloop;

    pa_iochannel_cb_t callback;
    void*userdata;

    bool readable:1;
    bool writable:1;
    bool hungup:1;
    bool no_close:1;

    pa_io_event* input_event, *output_event;
};

static void callback(pa_mainloop_api* m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata);

static void delete_events(pa_iochannel *io) {
    pa_assert(io);

    if (io->input_event)
        io->mainloop->io_free(io->input_event);

    if (io->output_event && io->output_event != io->input_event)
        io->mainloop->io_free(io->output_event);

    io->input_event = io->output_event = NULL;
}

static void enable_events(pa_iochannel *io) {
    pa_assert(io);

    if (io->hungup) {
        delete_events(io);
        return;
    }

    if (io->ifd == io->ofd && io->ifd >= 0) {
        pa_io_event_flags_t f = PA_IO_EVENT_NULL;

        if (!io->readable)
            f |= PA_IO_EVENT_INPUT;
        if (!io->writable)
            f |= PA_IO_EVENT_OUTPUT;

        pa_assert(io->input_event == io->output_event);

        if (f != PA_IO_EVENT_NULL) {
            if (io->input_event)
                io->mainloop->io_enable(io->input_event, f);
            else
                io->input_event = io->output_event = io->mainloop->io_new(io->mainloop, io->ifd, f, callback, io);
        } else
            delete_events(io);

    } else {

        if (io->ifd >= 0) {
            if (!io->readable) {
                if (io->input_event)
                    io->mainloop->io_enable(io->input_event, PA_IO_EVENT_INPUT);
                else
                    io->input_event = io->mainloop->io_new(io->mainloop, io->ifd, PA_IO_EVENT_INPUT, callback, io);
            } else if (io->input_event) {
                io->mainloop->io_free(io->input_event);
                io->input_event = NULL;
            }
        }

        if (io->ofd >= 0) {
            if (!io->writable) {
                if (io->output_event)
                    io->mainloop->io_enable(io->output_event, PA_IO_EVENT_OUTPUT);
                else
                    io->output_event = io->mainloop->io_new(io->mainloop, io->ofd, PA_IO_EVENT_OUTPUT, callback, io);
            } else if (io->output_event) {
                io->mainloop->io_free(io->output_event);
                io->output_event = NULL;
            }
        }
    }
}

static void callback(pa_mainloop_api* m, pa_io_event *e, int fd, pa_io_event_flags_t f, void *userdata) {
    pa_iochannel *io = userdata;
    bool changed = false;

    pa_assert(m);
    pa_assert(e);
    pa_assert(fd >= 0);
    pa_assert(userdata);

    if ((f & (PA_IO_EVENT_HANGUP|PA_IO_EVENT_ERROR)) && !io->hungup) {
        io->hungup = true;
        changed = true;
    }

    if ((f & PA_IO_EVENT_INPUT) && !io->readable) {
        io->readable = true;
        changed = true;
        pa_assert(e == io->input_event);
    }

    if ((f & PA_IO_EVENT_OUTPUT) && !io->writable) {
        io->writable = true;
        changed = true;
        pa_assert(e == io->output_event);
    }

    if (changed) {
        enable_events(io);

        if (io->callback)
            io->callback(io, io->userdata);
    }
}

pa_iochannel* pa_iochannel_new(pa_mainloop_api*m, int ifd, int ofd) {
    pa_iochannel *io;

    pa_assert(m);
    pa_assert(ifd >= 0 || ofd >= 0);

    io = pa_xnew0(pa_iochannel, 1);
    io->ifd = ifd;
    io->ofd = ofd;
    io->mainloop = m;

    if (io->ifd >= 0)
        pa_make_fd_nonblock(io->ifd);

    if (io->ofd >= 0 && io->ofd != io->ifd)
        pa_make_fd_nonblock(io->ofd);

    enable_events(io);
    return io;
}

void pa_iochannel_free(pa_iochannel*io) {
    pa_assert(io);

    delete_events(io);

    if (!io->no_close) {
        if (io->ifd >= 0)
            pa_close(io->ifd);
        if (io->ofd >= 0 && io->ofd != io->ifd)
            pa_close(io->ofd);
    }

    pa_xfree(io);
}

bool pa_iochannel_is_readable(pa_iochannel*io) {
    pa_assert(io);

    return io->readable || io->hungup;
}

bool pa_iochannel_is_writable(pa_iochannel*io) {
    pa_assert(io);

    return io->writable && !io->hungup;
}

bool pa_iochannel_is_hungup(pa_iochannel*io) {
    pa_assert(io);

    return io->hungup;
}

ssize_t pa_iochannel_write(pa_iochannel*io, const void*data, size_t l) {
    ssize_t r;

    pa_assert(io);
    pa_assert(data);
    pa_assert(l);
    pa_assert(io->ofd >= 0);

    r = pa_write(io->ofd, data, l, &io->ofd_type);

    if ((size_t) r == l)
        return r; /* Fast path - we almost always successfully write everything */

    if (r < 0) {
        if (errno == EINTR || errno == EAGAIN)
            r = 0;
        else
            return r;
    }

    /* Partial write - let's get a notification when we can write more */
    io->writable = io->hungup = false;
    enable_events(io);

    return r;
}

ssize_t pa_iochannel_read(pa_iochannel*io, void*data, size_t l) {
    ssize_t r;

    pa_assert(io);
    pa_assert(data);
    pa_assert(io->ifd >= 0);

    if ((r = pa_read(io->ifd, data, l, &io->ifd_type)) >= 0) {

        /* We also reset the hangup flag here to ensure that another
         * IO callback is triggered so that we will again call into
         * user code */
        io->readable = io->hungup = false;
        enable_events(io);
    }

    return r;
}

#ifdef HAVE_CREDS

bool pa_iochannel_creds_supported(pa_iochannel *io) {
    struct {
        struct sockaddr sa;
#ifdef HAVE_SYS_UN_H
        struct sockaddr_un un;
#endif
        struct sockaddr_storage storage;
    } sa;

    socklen_t l;

    pa_assert(io);
    pa_assert(io->ifd >= 0);
    pa_assert(io->ofd == io->ifd);

    l = sizeof(sa);
    if (getsockname(io->ifd, &sa.sa, &l) < 0)
        return false;

    return sa.sa.sa_family == AF_UNIX;
}

int pa_iochannel_creds_enable(pa_iochannel *io) {
    int t = 1;

    pa_assert(io);
    pa_assert(io->ifd >= 0);

    if (setsockopt(io->ifd, SOL_SOCKET, SO_PASSCRED, &t, sizeof(t)) < 0) {
        pa_log_error("setsockopt(SOL_SOCKET, SO_PASSCRED): %s", pa_cstrerror(errno));
        return -1;
    }

    return 0;
}

ssize_t pa_iochannel_write_with_creds(pa_iochannel*io, const void*data, size_t l, const pa_creds *ucred) {
    ssize_t r;
    struct msghdr mh;
    struct iovec iov;
    union {
        struct cmsghdr hdr;
        uint8_t data[CMSG_SPACE(sizeof(struct ucred))];
    } cmsg;
    struct ucred *u;

    pa_assert(io);
    pa_assert(data);
    pa_assert(l);
    pa_assert(io->ofd >= 0);

    pa_zero(iov);
    iov.iov_base = (void*) data;
    iov.iov_len = l;

    pa_zero(cmsg);
    cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(struct ucred));
    cmsg.hdr.cmsg_level = SOL_SOCKET;
    cmsg.hdr.cmsg_type = SCM_CREDENTIALS;

    u = (struct ucred*) CMSG_DATA(&cmsg.hdr);

    u->pid = getpid();
    if (ucred) {
        u->uid = ucred->uid;
        u->gid = ucred->gid;
    } else {
        u->uid = getuid();
        u->gid = getgid();
    }

    pa_zero(mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = &cmsg;
    mh.msg_controllen = sizeof(cmsg);

    if ((r = sendmsg(io->ofd, &mh, MSG_NOSIGNAL)) >= 0) {
        io->writable = io->hungup = false;
        enable_events(io);
    }

    return r;
}

/* For more details on FD passing, check the cmsg(3) manpage
 * and IETF RFC #2292: "Advanced Sockets API for IPv6" */
ssize_t pa_iochannel_write_with_fds(pa_iochannel*io, const void*data, size_t l, int nfd, const int *fds) {
    ssize_t r;
    int *msgdata;
    struct msghdr mh;
    struct iovec iov;
    union {
        struct cmsghdr hdr;
        uint8_t data[CMSG_SPACE(sizeof(int) * MAX_ANCIL_DATA_FDS)];
    } cmsg;

    pa_assert(io);
    pa_assert(data);
    pa_assert(l);
    pa_assert(io->ofd >= 0);
    pa_assert(fds);
    pa_assert(nfd > 0);
    pa_assert(nfd <= MAX_ANCIL_DATA_FDS);

    pa_zero(iov);
    iov.iov_base = (void*) data;
    iov.iov_len = l;

    pa_zero(cmsg);
    cmsg.hdr.cmsg_level = SOL_SOCKET;
    cmsg.hdr.cmsg_type = SCM_RIGHTS;

    msgdata = (int*) CMSG_DATA(&cmsg.hdr);
    memcpy(msgdata, fds, nfd * sizeof(int));
    cmsg.hdr.cmsg_len = CMSG_LEN(sizeof(int) * nfd);

    pa_zero(mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = &cmsg;

    /* If we followed the example on the cmsg man page, we'd use
     * sizeof(cmsg.data) here, but if nfd < MAX_ANCIL_DATA_FDS, then the data
     * buffer is larger than needed, and the kernel doesn't like it if we set
     * msg_controllen to a larger than necessary value. The commit message for
     * commit 451d1d6762 contains a longer explanation. */
    mh.msg_controllen = CMSG_SPACE(sizeof(int) * nfd);

    if ((r = sendmsg(io->ofd, &mh, MSG_NOSIGNAL)) >= 0) {
        io->writable = io->hungup = false;
        enable_events(io);
    }
    return r;
}

ssize_t pa_iochannel_read_with_ancil_data(pa_iochannel*io, void*data, size_t l, pa_cmsg_ancil_data *ancil_data) {
    ssize_t r;
    struct msghdr mh;
    struct iovec iov;
    union {
        struct cmsghdr hdr;
        uint8_t data[CMSG_SPACE(sizeof(struct ucred)) + CMSG_SPACE(sizeof(int) * MAX_ANCIL_DATA_FDS)];
    } cmsg;

    pa_assert(io);
    pa_assert(data);
    pa_assert(l);
    pa_assert(io->ifd >= 0);
    pa_assert(ancil_data);

    if (io->ifd_type > 0) {
        ancil_data->creds_valid = false;
        ancil_data->nfd = 0;
        return pa_iochannel_read(io, data, l);
    }

    iov.iov_base = data;
    iov.iov_len = l;

    pa_zero(mh);
    mh.msg_iov = &iov;
    mh.msg_iovlen = 1;
    mh.msg_control = &cmsg;
    mh.msg_controllen = sizeof(cmsg);

    if ((r = recvmsg(io->ifd, &mh, 0)) >= 0) {
        struct cmsghdr *cmh;

        ancil_data->creds_valid = false;
        ancil_data->nfd = 0;

        for (cmh = CMSG_FIRSTHDR(&mh); cmh; cmh = CMSG_NXTHDR(&mh, cmh)) {

            if (cmh->cmsg_level != SOL_SOCKET)
                continue;

            if (cmh->cmsg_type == SCM_CREDENTIALS) {
                struct ucred u;
                pa_assert(cmh->cmsg_len == CMSG_LEN(sizeof(struct ucred)));
                memcpy(&u, CMSG_DATA(cmh), sizeof(struct ucred));

                ancil_data->creds.gid = u.gid;
                ancil_data->creds.uid = u.uid;
                ancil_data->creds_valid = true;
            }
            else if (cmh->cmsg_type == SCM_RIGHTS) {
                int nfd = (cmh->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                if (nfd > MAX_ANCIL_DATA_FDS) {
                    int i;
                    pa_log("Trying to receive too many file descriptors!");
                    for (i = 0; i < nfd; i++)
                        pa_close(((int*) CMSG_DATA(cmh))[i]);
                    continue;
                }
                memcpy(ancil_data->fds, CMSG_DATA(cmh), nfd * sizeof(int));
                ancil_data->nfd = nfd;
                ancil_data->close_fds_on_cleanup = true;
            }
        }

        io->readable = io->hungup = false;
        enable_events(io);
    }

    if (r == -1 && errno == ENOTSOCK) {
        io->ifd_type = 1;
        return pa_iochannel_read_with_ancil_data(io, data, l, ancil_data);
    }

    return r;
}

#endif /* HAVE_CREDS */

void pa_iochannel_set_callback(pa_iochannel*io, pa_iochannel_cb_t _callback, void *userdata) {
    pa_assert(io);

    io->callback = _callback;
    io->userdata = userdata;
}

void pa_iochannel_set_noclose(pa_iochannel*io, bool b) {
    pa_assert(io);

    io->no_close = b;
}

void pa_iochannel_socket_peer_to_string(pa_iochannel*io, char*s, size_t l) {
    pa_assert(io);
    pa_assert(s);
    pa_assert(l);

    pa_socket_peer_to_string(io->ifd, s, l);
}

int pa_iochannel_socket_set_rcvbuf(pa_iochannel *io, size_t l) {
    pa_assert(io);

    return pa_socket_set_rcvbuf(io->ifd, l);
}

int pa_iochannel_socket_set_sndbuf(pa_iochannel *io, size_t l) {
    pa_assert(io);

    return pa_socket_set_sndbuf(io->ofd, l);
}

pa_mainloop_api* pa_iochannel_get_mainloop_api(pa_iochannel *io) {
    pa_assert(io);

    return io->mainloop;
}

int pa_iochannel_get_recv_fd(pa_iochannel *io) {
    pa_assert(io);

    return io->ifd;
}

int pa_iochannel_get_send_fd(pa_iochannel *io) {
    pa_assert(io);

    return io->ofd;
}

bool pa_iochannel_socket_is_local(pa_iochannel *io) {
    pa_assert(io);

    if (pa_socket_is_local(io->ifd))
        return true;

    if (io->ifd != io->ofd)
        if (pa_socket_is_local(io->ofd))
            return true;

    return false;
}
