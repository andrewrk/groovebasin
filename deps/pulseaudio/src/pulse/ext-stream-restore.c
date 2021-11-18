/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#include <pulse/context.h>
#include <pulse/fork-detect.h>
#include <pulse/operation.h>

#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "ext-stream-restore.h"

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_READ,
    SUBCOMMAND_WRITE,
    SUBCOMMAND_DELETE,
    SUBCOMMAND_SUBSCRIBE,
    SUBCOMMAND_EVENT
};

static void ext_stream_restore_test_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    uint32_t version = PA_INVALID_INDEX;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

    } else if (pa_tagstruct_getu32(t, &version) < 0 ||
               !pa_tagstruct_eof(t)) {

        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_ext_stream_restore_test_cb_t cb = (pa_ext_stream_restore_test_cb_t) o->callback;
        cb(o->context, version, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_stream_restore_test(
        pa_context *c,
        pa_ext_stream_restore_test_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-stream-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_TEST);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_stream_restore_test_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

static void ext_stream_restore_read_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int eol = 1;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        eol = -1;
    } else {

        while (!pa_tagstruct_eof(t)) {
            pa_ext_stream_restore_info i;
            bool mute = false;

            memset(&i, 0, sizeof(i));

            if (pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                pa_tagstruct_gets(t, &i.device) < 0 ||
                pa_tagstruct_get_boolean(t, &mute) < 0) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            i.mute = (int) mute;

            if (o->callback) {
                pa_ext_stream_restore_read_cb_t cb = (pa_ext_stream_restore_read_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }
        }
    }

    if (o->callback) {
        pa_ext_stream_restore_read_cb_t cb = (pa_ext_stream_restore_read_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_stream_restore_read(
        pa_context *c,
        pa_ext_stream_restore_read_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-stream-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_READ);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_stream_restore_read_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_stream_restore_write(
        pa_context *c,
        pa_update_mode_t mode,
        const pa_ext_stream_restore_info data[],
        unsigned n,
        int apply_immediately,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o = NULL;
    pa_tagstruct *t = NULL;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(mode == PA_UPDATE_MERGE || mode == PA_UPDATE_REPLACE || mode == PA_UPDATE_SET);
    pa_assert(data);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-stream-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_WRITE);

    pa_tagstruct_putu32(t, mode);
    pa_tagstruct_put_boolean(t, apply_immediately);

    for (; n > 0; n--, data++) {
        if (!data->name || !*data->name)
            goto fail;

        pa_tagstruct_puts(t, data->name);

        if (data->volume.channels > 0 &&
            !pa_cvolume_compatible_with_channel_map(&data->volume, &data->channel_map))
            goto fail;

        pa_tagstruct_put_channel_map(t, &data->channel_map);
        pa_tagstruct_put_cvolume(t, &data->volume);
        pa_tagstruct_puts(t, data->device);
        pa_tagstruct_put_boolean(t, data->mute);
    }

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;

fail:
    pa_operation_cancel(o);
    pa_operation_unref(o);

    pa_tagstruct_free(t);

    pa_context_set_error(c, PA_ERR_INVALID);
    return NULL;
}

pa_operation *pa_ext_stream_restore_delete(
        pa_context *c,
        const char *const s[],
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o = NULL;
    pa_tagstruct *t = NULL;
    const char *const *k;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(s);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-stream-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_DELETE);

    for (k = s; *k; k++) {
        if (!*k || !**k)
            goto fail;

        pa_tagstruct_puts(t, *k);
    }

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;

fail:
    pa_operation_cancel(o);
    pa_operation_unref(o);

    pa_tagstruct_free(t);

    pa_context_set_error(c, PA_ERR_INVALID);
    return NULL;
}

pa_operation *pa_ext_stream_restore_subscribe(
        pa_context *c,
        int enable,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-stream-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_SUBSCRIBE);
    pa_tagstruct_put_boolean(t, enable);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

void pa_ext_stream_restore_set_subscribe_cb(
        pa_context *c,
        pa_ext_stream_restore_subscribe_cb_t cb,
        void *userdata) {

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    c->ext_stream_restore.callback = cb;
    c->ext_stream_restore.userdata = userdata;
}

void pa_ext_stream_restore_command(pa_context *c, uint32_t tag, pa_tagstruct *t) {
    uint32_t subcommand;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &subcommand) < 0 ||
        !pa_tagstruct_eof(t)) {

        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (subcommand != SUBCOMMAND_EVENT) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (c->ext_stream_restore.callback)
        c->ext_stream_restore.callback(c, c->ext_stream_restore.userdata);
}
