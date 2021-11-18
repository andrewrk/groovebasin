#ifndef foopulsesrbchannelhfoo
#define foopulsesrbchannelhfoo

/***
  This file is part of PulseAudio.

  Copyright 2014 David Henningsson, Canonical Ltd.

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

#include <pulse/mainloop-api.h>
#include <pulsecore/fdsem.h>
#include <pulsecore/memblock.h>

/* An shm ringbuffer that is used for low overhead server-client communication.
 * Signaling is done through eventfd semaphores (pa_fdsem). */

typedef struct pa_srbchannel pa_srbchannel;

typedef struct pa_srbchannel_template {
    int readfd, writefd;
    pa_memblock *memblock;
} pa_srbchannel_template;

pa_srbchannel* pa_srbchannel_new(pa_mainloop_api *m, pa_mempool *p);
/* Note: this creates a srbchannel with swapped read and write. */
pa_srbchannel* pa_srbchannel_new_from_template(pa_mainloop_api *m, pa_srbchannel_template *t);

void pa_srbchannel_free(pa_srbchannel *sr);

void pa_srbchannel_export(pa_srbchannel *sr, pa_srbchannel_template *t);

size_t pa_srbchannel_write(pa_srbchannel *sr, const void *data, size_t l);
size_t pa_srbchannel_read(pa_srbchannel *sr, void *data, size_t l);

/* Set the callback function that is called whenever data becomes available for reading.
 * It can also be called if the output buffer was full and can now be written to.
 *
 * Return false to abort all processing (e g if the srbchannel has been freed during the callback).
 * Otherwise return true.
*/
typedef bool (*pa_srbchannel_cb_t)(pa_srbchannel *sr, void *userdata);
void pa_srbchannel_set_callback(pa_srbchannel *sr, pa_srbchannel_cb_t callback, void *userdata);

#endif
