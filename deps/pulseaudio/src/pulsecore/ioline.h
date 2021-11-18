#ifndef fooiolinehfoo
#define fooiolinehfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <pulse/gccmacro.h>

#include <pulsecore/iochannel.h>

/* An ioline wraps an iochannel for line based communication. A
 * callback function is called whenever a new line has been received
 * from the client */

typedef struct pa_ioline pa_ioline;

typedef void (*pa_ioline_cb_t)(pa_ioline*io, const char *s, void *userdata);
typedef void (*pa_ioline_drain_cb_t)(pa_ioline *io, void *userdata);

pa_ioline* pa_ioline_new(pa_iochannel *io);
void pa_ioline_unref(pa_ioline *l);
pa_ioline* pa_ioline_ref(pa_ioline *l);
void pa_ioline_close(pa_ioline *l);

/* Write a string to the channel */
void pa_ioline_puts(pa_ioline *s, const char *c);

/* Write a string to the channel */
void pa_ioline_printf(pa_ioline *s, const char *format, ...) PA_GCC_PRINTF_ATTR(2,3);

/* Set the callback function that is called for every received line */
void pa_ioline_set_callback(pa_ioline*io, pa_ioline_cb_t callback, void *userdata);

/* Set the callback function that is called when everything has been written */
void pa_ioline_set_drain_callback(pa_ioline*io, pa_ioline_drain_cb_t callback, void *userdata);

/* Make sure to close the ioline object as soon as the send buffer is emptied */
void pa_ioline_defer_close(pa_ioline *io);

/* Returns true when everything was written */
bool pa_ioline_is_drained(pa_ioline *io);

/* Detaches from the iochannel and returns it. Data that has already
 * been read will not be available in the detached iochannel */
pa_iochannel* pa_ioline_detach_iochannel(pa_ioline *l);

#endif
