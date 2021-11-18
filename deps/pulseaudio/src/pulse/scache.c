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

#include <stdlib.h>
#include <stdio.h>

#include <pulse/utf8.h>
#include <pulse/fork-detect.h>

#include <pulsecore/pstream-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/proplist-util.h>

#include "internal.h"
#include "scache.h"

int pa_stream_connect_upload(pa_stream *s, size_t length) {
    pa_tagstruct *t;
    uint32_t tag;
    const char *name;

    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);

    PA_CHECK_VALIDITY(s->context, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY(s->context, s->state == PA_STREAM_UNCONNECTED, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY(s->context, length > 0, PA_ERR_INVALID);
    PA_CHECK_VALIDITY(s->context, length == (size_t) (uint32_t) length, PA_ERR_INVALID);
    PA_CHECK_VALIDITY(s->context, s->context->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    if (!(name = pa_proplist_gets(s->proplist, PA_PROP_EVENT_ID)))
        name = pa_proplist_gets(s->proplist, PA_PROP_MEDIA_NAME);

    PA_CHECK_VALIDITY(s->context, name && *name && pa_utf8_valid(name), PA_ERR_INVALID);

    pa_stream_ref(s);

    s->direction = PA_STREAM_UPLOAD;
    s->flags = 0;

    t = pa_tagstruct_command(s->context, PA_COMMAND_CREATE_UPLOAD_STREAM, &tag);

    pa_tagstruct_puts(t, name);
    pa_tagstruct_put_sample_spec(t, &s->sample_spec);
    pa_tagstruct_put_channel_map(t, &s->channel_map);
    pa_tagstruct_putu32(t, (uint32_t) length);

    if (s->context->version >= 13)
        pa_tagstruct_put_proplist(t, s->proplist);

    pa_pstream_send_tagstruct(s->context->pstream, t);
    pa_pdispatch_register_reply(s->context->pdispatch, tag, DEFAULT_TIMEOUT, pa_create_stream_callback, s, NULL);

    pa_stream_set_state(s, PA_STREAM_CREATING);

    pa_stream_unref(s);
    return 0;
}

int pa_stream_finish_upload(pa_stream *s) {
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(s);
    pa_assert(PA_REFCNT_VALUE(s) >= 1);

    PA_CHECK_VALIDITY(s->context, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY(s->context, s->channel_valid, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY(s->context, s->context->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    pa_stream_ref(s);

    t = pa_tagstruct_command(s->context, PA_COMMAND_FINISH_UPLOAD_STREAM, &tag);
    pa_tagstruct_putu32(t, s->channel);
    pa_pstream_send_tagstruct(s->context->pstream, t);
    pa_pdispatch_register_reply(s->context->pdispatch, tag, DEFAULT_TIMEOUT, pa_stream_disconnect_callback, s, NULL);

    pa_stream_unref(s);
    return 0;
}

static void play_sample_ack_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int success = 1;
    uint32_t idx = PA_INVALID_INDEX;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        success = 0;
    } else if ((o->context->version >= 13 && pa_tagstruct_getu32(t, &idx) < 0) ||
               !pa_tagstruct_eof(t)) {
        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    } else if (o->context->version >= 13 && idx == PA_INVALID_INDEX)
        success = 0;

    if (o->callback) {
        pa_context_success_cb_t cb = (pa_context_success_cb_t) o->callback;
        cb(o->context, success, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

static void play_sample_with_proplist_ack_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    uint32_t idx;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        idx = PA_INVALID_INDEX;
    } else if (pa_tagstruct_getu32(t, &idx) < 0 ||
               !pa_tagstruct_eof(t)) {
        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_context_play_sample_cb_t cb = (pa_context_play_sample_cb_t) o->callback;
        cb(o->context, idx, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_context_play_sample(pa_context *c, const char *name, const char *dev, pa_volume_t volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, name && *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !dev || *dev, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    if (!dev)
        dev = c->conf->default_sink;

    t = pa_tagstruct_command(c, PA_COMMAND_PLAY_SAMPLE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, dev);

    if (!PA_VOLUME_IS_VALID(volume) && c->version < 15)
        volume = PA_VOLUME_NORM;

    pa_tagstruct_putu32(t, volume);
    pa_tagstruct_puts(t, name);

    if (c->version >= 13) {
        pa_proplist *p = pa_proplist_new();
        pa_tagstruct_put_proplist(t, p);
        pa_proplist_free(p);
    }

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, play_sample_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_context_play_sample_with_proplist(pa_context *c, const char *name, const char *dev, pa_volume_t volume, const pa_proplist *p, pa_context_play_sample_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, name && *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !dev || *dev, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 13, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    if (!dev)
        dev = c->conf->default_sink;

    t = pa_tagstruct_command(c, PA_COMMAND_PLAY_SAMPLE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, dev);

    if (!PA_VOLUME_IS_VALID(volume) && c->version < 15)
        volume = PA_VOLUME_NORM;

    pa_tagstruct_putu32(t, volume);
    pa_tagstruct_puts(t, name);

    if (p)
        pa_tagstruct_put_proplist(t, p);
    else {
        pa_proplist *empty_proplist = pa_proplist_new();
        pa_tagstruct_put_proplist(t, empty_proplist);
        pa_proplist_free(empty_proplist);
    }

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, play_sample_with_proplist_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_remove_sample(pa_context *c, const char *name, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, name && *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_REMOVE_SAMPLE, &tag);
    pa_tagstruct_puts(t, name);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}
