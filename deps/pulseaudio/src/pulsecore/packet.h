#ifndef foopackethfoo
#define foopackethfoo

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

#include <sys/types.h>
#include <inttypes.h>

typedef struct pa_packet pa_packet;

/* create empty packet (either of type appended or dynamic depending
 * on length) */
pa_packet* pa_packet_new(size_t length);

/* create packet (either of type appended or dynamic depending on length)
 * and copy data */
pa_packet* pa_packet_new_data(const void* data, size_t length);

/* data must have been malloc()ed; the packet takes ownership of the memory,
 * i.e. memory is free()d with the packet */
pa_packet* pa_packet_new_dynamic(void* data, size_t length);

const void* pa_packet_data(pa_packet *p, size_t *l);

pa_packet* pa_packet_ref(pa_packet *p);
void pa_packet_unref(pa_packet *p);

#endif
