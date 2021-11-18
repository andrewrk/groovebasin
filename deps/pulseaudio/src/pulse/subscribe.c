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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "subscribe.h"

void pa_command_subscribe_event(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;
    pa_subscription_event_type_t e;
    uint32_t idx;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_SUBSCRIBE_EVENT);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_context_ref(c);

    if (pa_tagstruct_getu32(t, &e) < 0 ||
        pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (c->subscribe_callback)
        c->subscribe_callback(c, e, idx, c->subscribe_userdata);

finish:
    pa_context_unref(c);
}

pa_operation* pa_context_subscribe(pa_context *c, pa_subscription_mask_t m, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SUBSCRIBE, &tag);
    pa_tagstruct_putu32(t, m);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

void pa_context_set_subscribe_callback(pa_context *c, pa_context_subscribe_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (c->state == PA_CONTEXT_TERMINATED || c->state == PA_CONTEXT_FAILED)
        return;

    c->subscribe_callback = cb;
    c->subscribe_userdata = userdata;
}
