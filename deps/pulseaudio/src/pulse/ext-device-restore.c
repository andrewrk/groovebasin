/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering
  Copyright 2011 Colin Guthrie

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
#include <pulse/gccmacro.h>
#include <pulse/xmalloc.h>
#include <pulse/fork-detect.h>
#include <pulse/operation.h>
#include <pulse/format.h>

#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "ext-device-restore.h"

/* Protocol extension commands */
enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_SUBSCRIBE,
    SUBCOMMAND_EVENT,
    SUBCOMMAND_READ_FORMATS_ALL,
    SUBCOMMAND_READ_FORMATS,
    SUBCOMMAND_SAVE_FORMATS
};

static void ext_device_restore_test_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
        pa_ext_device_restore_test_cb_t cb = (pa_ext_device_restore_test_cb_t) o->callback;
        cb(o->context, version, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_device_restore_test(
        pa_context *c,
        pa_ext_device_restore_test_cb_t cb,
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
    pa_tagstruct_puts(t, "module-device-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_TEST);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_device_restore_test_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_device_restore_subscribe(
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
    pa_tagstruct_puts(t, "module-device-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_SUBSCRIBE);
    pa_tagstruct_put_boolean(t, enable);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

void pa_ext_device_restore_set_subscribe_cb(
        pa_context *c,
        pa_ext_device_restore_subscribe_cb_t cb,
        void *userdata) {

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    c->ext_device_restore.callback = cb;
    c->ext_device_restore.userdata = userdata;
}

static void ext_device_restore_read_device_formats_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
        uint8_t j;

        while (!pa_tagstruct_eof(t)) {
            pa_ext_device_restore_info i;
            pa_zero(i);

            if (pa_tagstruct_getu32(t, &i.type) < 0 ||
                pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_getu8(t, &i.n_formats) < 0) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            if (PA_DEVICE_TYPE_SINK != i.type && PA_DEVICE_TYPE_SOURCE != i.type) {
                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            if (i.index == PA_INVALID_INDEX) {
                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            if (i.n_formats > 0) {
                i.formats = pa_xnew0(pa_format_info*, i.n_formats);

                for (j = 0; j < i.n_formats; j++) {

                    pa_format_info *f = i.formats[j] = pa_format_info_new();
                    if (pa_tagstruct_get_format_info(t, f) < 0) {
                        uint8_t k;

                        pa_context_fail(o->context, PA_ERR_PROTOCOL);
                        for (k = 0; k < j+1; k++)
                            pa_format_info_free(i.formats[k]);
                        pa_xfree(i.formats);
                        goto finish;
                    }
                }
            }

            if (o->callback) {
                pa_ext_device_restore_read_device_formats_cb_t cb = (pa_ext_device_restore_read_device_formats_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            for (j = 0; j < i.n_formats; j++)
                pa_format_info_free(i.formats[j]);
            pa_xfree(i.formats);
        }
    }

    if (o->callback) {
        pa_ext_device_restore_read_device_formats_cb_t cb = (pa_ext_device_restore_read_device_formats_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_device_restore_read_formats_all(
        pa_context *c,
        pa_ext_device_restore_read_device_formats_cb_t cb,
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
    pa_tagstruct_puts(t, "module-device-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_READ_FORMATS_ALL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_device_restore_read_device_formats_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_device_restore_read_formats(
        pa_context *c,
        pa_device_type_t type,
        uint32_t idx,
        pa_ext_device_restore_read_device_formats_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(idx != PA_INVALID_INDEX);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-device-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_READ_FORMATS);
    pa_tagstruct_putu32(t, type);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_device_restore_read_device_formats_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_device_restore_save_formats(
        pa_context *c,
        pa_device_type_t type,
        uint32_t idx,
        uint8_t n_formats,
        pa_format_info **formats,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;
    uint8_t j;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(idx != PA_INVALID_INDEX);
    pa_assert(n_formats > 0);
    pa_assert(formats && *formats);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-device-restore");
    pa_tagstruct_putu32(t, SUBCOMMAND_SAVE_FORMATS);

    pa_tagstruct_putu32(t, type);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_putu8(t, n_formats);
    for (j = 0; j < n_formats; j++)
        pa_tagstruct_put_format_info(t, formats[j]);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/* Command function defined in internal.h */
void pa_ext_device_restore_command(pa_context *c, uint32_t tag, pa_tagstruct *t) {
    uint32_t subcommand;
    pa_device_type_t type;
    uint32_t idx;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &subcommand) < 0 ||
        pa_tagstruct_getu32(t, &type) < 0 ||
        pa_tagstruct_getu32(t, &idx) < 0 ||
        !pa_tagstruct_eof(t)) {

        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (subcommand != SUBCOMMAND_EVENT) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (PA_DEVICE_TYPE_SINK != type && PA_DEVICE_TYPE_SOURCE != type) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (idx == PA_INVALID_INDEX) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (c->ext_device_restore.callback)
        c->ext_device_restore.callback(c, type, idx, c->ext_device_restore.userdata);
}
