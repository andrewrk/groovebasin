#ifndef foopstreamhfoo
#define foopstreamhfoo

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

#include <inttypes.h>

#include <pulse/mainloop-api.h>
#include <pulse/def.h>

#include <pulsecore/packet.h>
#include <pulsecore/memblock.h>
#include <pulsecore/iochannel.h>
#include <pulsecore/srbchannel.h>
#include <pulsecore/memchunk.h>
#include <pulsecore/creds.h>
#include <pulsecore/macro.h>

typedef struct pa_pstream pa_pstream;

typedef void (*pa_pstream_packet_cb_t)(pa_pstream *p, pa_packet *packet, pa_cmsg_ancil_data *ancil_data, void *userdata);
typedef void (*pa_pstream_memblock_cb_t)(pa_pstream *p, uint32_t channel, int64_t offset, pa_seek_mode_t seek, const pa_memchunk *chunk, void *userdata);
typedef void (*pa_pstream_notify_cb_t)(pa_pstream *p, void *userdata);
typedef void (*pa_pstream_block_id_cb_t)(pa_pstream *p, uint32_t block_id, void *userdata);

pa_pstream* pa_pstream_new(pa_mainloop_api *m, pa_iochannel *io, pa_mempool *p);

pa_pstream* pa_pstream_ref(pa_pstream*p);
void pa_pstream_unref(pa_pstream*p);

void pa_pstream_unlink(pa_pstream *p);

int pa_pstream_attach_memfd_shmid(pa_pstream *p, unsigned shm_id, int memfd_fd);

void pa_pstream_send_packet(pa_pstream*p, pa_packet *packet, pa_cmsg_ancil_data *ancil_data);
void pa_pstream_send_memblock(pa_pstream*p, uint32_t channel, int64_t offset, pa_seek_mode_t seek, const pa_memchunk *chunk);
void pa_pstream_send_release(pa_pstream *p, uint32_t block_id);
void pa_pstream_send_revoke(pa_pstream *p, uint32_t block_id);

void pa_pstream_set_receive_packet_callback(pa_pstream *p, pa_pstream_packet_cb_t cb, void *userdata);
void pa_pstream_set_receive_memblock_callback(pa_pstream *p, pa_pstream_memblock_cb_t cb, void *userdata);
void pa_pstream_set_drain_callback(pa_pstream *p, pa_pstream_notify_cb_t cb, void *userdata);
void pa_pstream_set_die_callback(pa_pstream *p, pa_pstream_notify_cb_t cb, void *userdata);
void pa_pstream_set_release_callback(pa_pstream *p, pa_pstream_block_id_cb_t cb, void *userdata);
void pa_pstream_set_revoke_callback(pa_pstream *p, pa_pstream_block_id_cb_t cb, void *userdata);

bool pa_pstream_is_pending(pa_pstream *p);

void pa_pstream_enable_shm(pa_pstream *p, bool enable);
void pa_pstream_enable_memfd(pa_pstream *p);
bool pa_pstream_get_shm(pa_pstream *p);
bool pa_pstream_get_memfd(pa_pstream *p);

/* Enables shared ringbuffer channel. Note that the srbchannel is now owned by the pstream.
   Setting srb to NULL will free any existing srbchannel. */
void pa_pstream_set_srbchannel(pa_pstream *p, pa_srbchannel *srb);

#endif
