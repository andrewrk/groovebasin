/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/native-common.h>
#include <pulsecore/pstream.h>
#include <pulsecore/refcnt.h>
#include <pulse/xmalloc.h>

#include "pstream-util.h"

static void pa_pstream_send_tagstruct_with_ancil_data(pa_pstream *p, pa_tagstruct *t, pa_cmsg_ancil_data *ancil_data) {
    size_t length;
    const uint8_t *data;
    pa_packet *packet;

    pa_assert(p);
    pa_assert(t);

    pa_assert_se(data = pa_tagstruct_data(t, &length));
    pa_assert_se(packet = pa_packet_new_data(data, length));
    pa_tagstruct_free(t);

    pa_pstream_send_packet(p, packet, ancil_data);
    pa_packet_unref(packet);
}

#ifdef HAVE_CREDS

void pa_pstream_send_tagstruct_with_creds(pa_pstream *p, pa_tagstruct *t, const pa_creds *creds) {
    if (creds) {
        pa_cmsg_ancil_data a;

        a.nfd = 0;
        a.creds_valid = true;
        a.creds = *creds;
        pa_pstream_send_tagstruct_with_ancil_data(p, t, &a);
    }
    else
        pa_pstream_send_tagstruct_with_ancil_data(p, t, NULL);
}

/* @close_fds: If set then the pstreams code, after invoking a sendmsg(),
 * will close all passed fds.
 *
 * Such fds cannot be closed here as this might lead to freeing them
 * before they're actually passed to the other end. The internally-used
 * pa_pstream_send_packet() does not do any actual writes and just
 * defers write events over the pstream. */
void pa_pstream_send_tagstruct_with_fds(pa_pstream *p, pa_tagstruct *t, int nfd, const int *fds,
                                        bool close_fds) {
    if (nfd > 0) {
        pa_cmsg_ancil_data a;

        a.nfd = nfd;
        a.creds_valid = false;
        a.close_fds_on_cleanup = close_fds;
        pa_assert(nfd <= MAX_ANCIL_DATA_FDS);
        memcpy(a.fds, fds, sizeof(int) * nfd);
        pa_pstream_send_tagstruct_with_ancil_data(p, t, &a);
    }
    else
        pa_pstream_send_tagstruct_with_ancil_data(p, t, NULL);
}

#else

void pa_pstream_send_tagstruct_with_creds(pa_pstream *p, pa_tagstruct *t, const pa_creds *creds) {
    pa_pstream_send_tagstruct_with_ancil_data(p, t, NULL);
}

void PA_GCC_NORETURN pa_pstream_send_tagstruct_with_fds(pa_pstream *p, pa_tagstruct *t, int nfd, const int *fds,
                                                        bool close_fds) {
    pa_assert_not_reached();
}

#endif

void pa_pstream_send_error(pa_pstream *p, uint32_t tag, uint32_t error) {
    pa_tagstruct *t;

    pa_assert_se(t = pa_tagstruct_new());
    pa_tagstruct_putu32(t, PA_COMMAND_ERROR);
    pa_tagstruct_putu32(t, tag);
    pa_tagstruct_putu32(t, error);
    pa_pstream_send_tagstruct(p, t);
}

void pa_pstream_send_simple_ack(pa_pstream *p, uint32_t tag) {
    pa_tagstruct *t;

    pa_assert_se(t = pa_tagstruct_new());
    pa_tagstruct_putu32(t, PA_COMMAND_REPLY);
    pa_tagstruct_putu32(t, tag);
    pa_pstream_send_tagstruct(p, t);
}

/* Before sending blocks from a memfd-backed pool over the pipe, we
 * must call this method first.
 *
 * This is needed to transfer memfd blocks without passing their fd
 * every time, thus minimizing overhead and avoiding fd leaks.
 *
 * On registration a packet is sent with the memfd fd as ancil data;
 * such packet has an ID that uniquely identifies the pool's memfd
 * region. Upon arrival the other end creates a permanent mapping
 * between that ID and the passed memfd memory area.
 *
 * By doing so, we won't need to reference the pool's memfd fd any
 * further - just its ID. Both endpoints can then close their fds. */
int pa_pstream_register_memfd_mempool(pa_pstream *p, pa_mempool *pool, const char **fail_reason) {
#if defined(HAVE_CREDS) && defined(HAVE_MEMFD)
    unsigned shm_id;
    int memfd_fd, ret = -1;
    pa_tagstruct *t;
    bool per_client_mempool;

    pa_assert(p);
    pa_assert(fail_reason);

    *fail_reason = NULL;
    per_client_mempool = pa_mempool_is_per_client(pool);

    pa_pstream_ref(p);

    if (!pa_mempool_is_shared(pool)) {
        *fail_reason = "mempool is not shared";
        goto finish;
    }

    if (!pa_mempool_is_memfd_backed(pool)) {
        *fail_reason = "mempool is not memfd-backed";
        goto finish;
    }

    if (pa_mempool_get_shm_id(pool, &shm_id)) {
        *fail_reason = "could not extract pool SHM ID";
        goto finish;
    }

    if (!pa_pstream_get_memfd(p)) {
        *fail_reason = "pipe does not support memfd transport";
        goto finish;
    }

    memfd_fd = (per_client_mempool) ? pa_mempool_take_memfd_fd(pool) :
                                      pa_mempool_get_memfd_fd(pool);

    /* Note! For per-client mempools we've taken ownership of the memfd
     * fd, and we're thus the sole code path responsible for closing it.
     * In case of any failure, it MUST be closed. */

    if (pa_pstream_attach_memfd_shmid(p, shm_id, memfd_fd)) {
        *fail_reason = "could not attach memfd SHM ID to pipe";

        if (per_client_mempool)
            pa_assert_se(pa_close(memfd_fd) == 0);
        goto finish;
    }

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_REGISTER_MEMFD_SHMID);
    pa_tagstruct_putu32(t, (uint32_t) -1); /* tag */
    pa_tagstruct_putu32(t, shm_id);
    pa_pstream_send_tagstruct_with_fds(p, t, 1, &memfd_fd, per_client_mempool);

    ret = 0;
finish:
    pa_pstream_unref(p);
    return ret;

#else
    pa_assert(fail_reason);
    *fail_reason = "memfd support not compiled in";
    return -1;
#endif
}
