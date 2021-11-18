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

#include <stdlib.h>

#include <pulse/xmalloc.h>
#include <pulsecore/macro.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/flist.h>

#include "packet.h"

#define MAX_APPENDED_SIZE 128

struct pa_packet {
    PA_REFCNT_DECLARE;
    enum { PA_PACKET_APPENDED, PA_PACKET_DYNAMIC } type;
    size_t length;
    uint8_t *data;
    union {
        uint8_t appended[MAX_APPENDED_SIZE];
    } per_type;
};

PA_STATIC_FLIST_DECLARE(packets, 0, pa_xfree);

pa_packet* pa_packet_new(size_t length) {
    pa_packet *p;

    pa_assert(length > 0);

    if (!(p = pa_flist_pop(PA_STATIC_FLIST_GET(packets))))
        p = pa_xnew(pa_packet, 1);
    PA_REFCNT_INIT(p);
    p->length = length;
    if (length > MAX_APPENDED_SIZE) {
        p->data = pa_xmalloc(length);
        p->type = PA_PACKET_DYNAMIC;
    } else {
        p->data = p->per_type.appended;
        p->type = PA_PACKET_APPENDED;
    }

    return p;
}

pa_packet* pa_packet_new_data(const void* data, size_t length) {
    pa_packet *p = pa_packet_new(length);

    pa_assert(data);
    pa_assert(length > 0);

    memcpy(p->data, data, length);

    return p;
}

pa_packet* pa_packet_new_dynamic(void* data, size_t length) {
    pa_packet *p;

    pa_assert(data);
    pa_assert(length > 0);

    if (!(p = pa_flist_pop(PA_STATIC_FLIST_GET(packets))))
        p = pa_xnew(pa_packet, 1);
    PA_REFCNT_INIT(p);
    p->length = length;
    p->data = data;
    p->type = PA_PACKET_DYNAMIC;

    return p;
}

const void* pa_packet_data(pa_packet *p, size_t *l) {
    pa_assert(PA_REFCNT_VALUE(p) >= 1);
    pa_assert(p->data);
    pa_assert(l);

    *l = p->length;

    return p->data;
}

pa_packet* pa_packet_ref(pa_packet *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    PA_REFCNT_INC(p);
    return p;
}

void pa_packet_unref(pa_packet *p) {
    pa_assert(p);
    pa_assert(PA_REFCNT_VALUE(p) >= 1);

    if (PA_REFCNT_DEC(p) <= 0) {
        if (p->type == PA_PACKET_DYNAMIC)
            pa_xfree(p->data);
        if (pa_flist_push(PA_STATIC_FLIST_GET(packets), p) < 0)
            pa_xfree(p);
    }
}
