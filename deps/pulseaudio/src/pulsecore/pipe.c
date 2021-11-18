/***
  This file is part of PulseAudio.

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

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/types.h>

#include <pulsecore/socket.h>
#include <pulsecore/core-util.h>

#include "pipe.h"

#ifndef HAVE_PIPE

static int set_block(int fd, int blocking) {
#ifdef O_NONBLOCK

    int v;

    pa_assert(fd >= 0);

    if ((v = fcntl(fd, F_GETFL)) < 0)
        return -1;

    if (blocking)
        v &= ~O_NONBLOCK;
    else
        v |= O_NONBLOCK;

    if (fcntl(fd, F_SETFL, v) < 0)
        return -1;

    return 0;

#elif defined(OS_IS_WIN32)

    u_long arg;

    arg = !blocking;

    if (ioctlsocket(fd, FIONBIO, &arg) < 0)
        return -1;

    return 0;

#else

    return -1;

#endif
}

int pipe(int filedes[2]) {
    int listener;
    struct sockaddr_in addr, peer;
    socklen_t len;

    listener = -1;
    filedes[0] = -1;
    filedes[1] = -1;

    listener = socket(PF_INET, SOCK_STREAM, 0);
    if (listener < 0)
        goto error;

    filedes[0] = socket(PF_INET, SOCK_STREAM, 0);
    if (filedes[0] < 0)
        goto error;

    filedes[1] = socket(PF_INET, SOCK_STREAM, 0);
    if (filedes[1] < 0)
        goto error;

    /* Make non-blocking so that connect() won't block */
    if (set_block(filedes[0], 0) < 0)
        goto error;

    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        goto error;

    if (listen(listener, 1) < 0)
        goto error;

    len = sizeof(addr);
    if (getsockname(listener, (struct sockaddr*)&addr, &len) < 0)
        goto error;

    if (connect(filedes[0], (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef OS_IS_WIN32
        if (WSAGetLastError() != EWOULDBLOCK)
#else
        if (errno != EINPROGRESS)
#endif
            goto error;
    }

    len = sizeof(peer);
    filedes[1] = accept(listener, (struct sockaddr*)&peer, &len);
    if (filedes[1] < 0)
        goto error;

    /* Restore blocking */
    if (set_block(filedes[0], 1) < 0)
        goto error;

    len = sizeof(addr);
    if (getsockname(filedes[0], (struct sockaddr*)&addr, &len) < 0)
        goto error;

    /* Check that someone else didn't steal the connection */
    if ((addr.sin_port != peer.sin_port) || (addr.sin_addr.s_addr != peer.sin_addr.s_addr))
        goto error;

    pa_close(listener);

    return 0;

error:
        if (listener >= 0)
                pa_close(listener);
        if (filedes[0] >= 0)
                pa_close(filedes[0]);
        if (filedes[1] >= 0)
                pa_close(filedes[1]);

        return -1;
}

#endif /* HAVE_PIPE */
