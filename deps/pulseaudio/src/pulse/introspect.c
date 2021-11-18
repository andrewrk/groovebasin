/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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
#include <pulse/direction.h>
#include <pulse/xmalloc.h>
#include <pulse/fork-detect.h>

#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "introspect.h"

/*** Statistics ***/

static void context_stat_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    pa_stat_info i, *p = &i;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    pa_zero(i);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        p = NULL;
    } else if (pa_tagstruct_getu32(t, &i.memblock_total) < 0 ||
               pa_tagstruct_getu32(t, &i.memblock_total_size) < 0 ||
               pa_tagstruct_getu32(t, &i.memblock_allocated) < 0 ||
               pa_tagstruct_getu32(t, &i.memblock_allocated_size) < 0 ||
               pa_tagstruct_getu32(t, &i.scache_size) < 0 ||
               !pa_tagstruct_eof(t)) {
        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_stat_info_cb_t cb = (pa_stat_info_cb_t) o->callback;
        cb(o->context, p, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_stat(pa_context *c, pa_stat_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_STAT, context_stat_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Server Info ***/

static void context_get_server_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    pa_server_info i, *p = &i;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    pa_zero(i);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        p = NULL;
    } else if (pa_tagstruct_gets(t, &i.server_name) < 0 ||
               pa_tagstruct_gets(t, &i.server_version) < 0 ||
               pa_tagstruct_gets(t, &i.user_name) < 0 ||
               pa_tagstruct_gets(t, &i.host_name) < 0 ||
               pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
               pa_tagstruct_gets(t, &i.default_sink_name) < 0 ||
               pa_tagstruct_gets(t, &i.default_source_name) < 0 ||
               pa_tagstruct_getu32(t, &i.cookie) < 0 ||
               (o->context->version >= 15 &&
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0) ||
               !pa_tagstruct_eof(t)) {

        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (p && o->context->version < 15)
        pa_channel_map_init_extend(&i.channel_map, i.sample_spec.channels, PA_CHANNEL_MAP_DEFAULT);

    if (o->callback) {
        pa_server_info_cb_t cb = (pa_server_info_cb_t) o->callback;
        cb(o->context, p, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_server_info(pa_context *c, pa_server_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SERVER_INFO, context_get_server_info_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Sink Info ***/

static void context_get_sink_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int eol = 1;
    pa_sink_info i;
    uint32_t j;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    /* For safety in case someone use fail: outside the while loop below */
    pa_zero(i);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        eol = -1;
    } else {

        while (!pa_tagstruct_eof(t)) {
            bool mute;
            uint32_t flags;
            uint32_t state;
            const char *ap = NULL;

            pa_zero(i);
            i.proplist = pa_proplist_new();
            i.base_volume = PA_VOLUME_NORM;
            i.n_volume_steps = PA_VOLUME_NORM+1;
            mute = false;
            state = PA_SINK_INVALID_STATE;
            i.card = PA_INVALID_INDEX;

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_gets(t, &i.description) < 0 ||
                pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                pa_tagstruct_get_boolean(t, &mute) < 0 ||
                pa_tagstruct_getu32(t, &i.monitor_source) < 0 ||
                pa_tagstruct_gets(t, &i.monitor_source_name) < 0 ||
                pa_tagstruct_get_usec(t, &i.latency) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                pa_tagstruct_getu32(t, &flags) < 0 ||
                (o->context->version >= 13 &&
                 (pa_tagstruct_get_proplist(t, i.proplist) < 0 ||
                  pa_tagstruct_get_usec(t, &i.configured_latency) < 0)) ||
                (o->context->version >= 15 &&
                 (pa_tagstruct_get_volume(t, &i.base_volume) < 0 ||
                  pa_tagstruct_getu32(t, &state) < 0 ||
                  pa_tagstruct_getu32(t, &i.n_volume_steps) < 0 ||
                  pa_tagstruct_getu32(t, &i.card) < 0)) ||
                (o->context->version >= 16 &&
                 (pa_tagstruct_getu32(t, &i.n_ports)))) {

                goto fail;
            }

            if (o->context->version >= 16) {
                if (i.n_ports > 0) {
                    i.ports = pa_xnew(pa_sink_port_info*, i.n_ports+1);
                    i.ports[0] = pa_xnew(pa_sink_port_info, i.n_ports);

                    for (j = 0; j < i.n_ports; j++) {
                        i.ports[j] = &i.ports[0][j];

                        if (pa_tagstruct_gets(t, &i.ports[j]->name) < 0 ||
                            pa_tagstruct_gets(t, &i.ports[j]->description) < 0 ||
                            pa_tagstruct_getu32(t, &i.ports[j]->priority) < 0) {

                            goto fail;
                        }

                        i.ports[j]->available = PA_PORT_AVAILABLE_UNKNOWN;
                        if (o->context->version >= 24) {
                            uint32_t av;
                            if (pa_tagstruct_getu32(t, &av) < 0 || av > PA_PORT_AVAILABLE_YES)
                                goto fail;
                            i.ports[j]->available = av;
                        }
                        i.ports[j]->availability_group = NULL;
                        i.ports[j]->type = PA_DEVICE_PORT_TYPE_UNKNOWN;
                        if (o->context->version >= 34) {
                            if (pa_tagstruct_gets(t, &i.ports[j]->availability_group) < 0 ||
                                pa_tagstruct_getu32(t, &i.ports[j]->type) < 0)
                                goto fail;
                        }
                    }

                    i.ports[j] = NULL;
                }

                if (pa_tagstruct_gets(t, &ap) < 0)
                    goto fail;

                if (ap) {
                    for (j = 0; j < i.n_ports; j++)
                        if (pa_streq(i.ports[j]->name, ap)) {
                            i.active_port = i.ports[j];
                            break;
                        }
                }
            }

            if (o->context->version >= 21) {
                uint8_t n_formats;
                if (pa_tagstruct_getu8(t, &n_formats) < 0 || n_formats < 1)
                    goto fail;

                i.formats = pa_xnew0(pa_format_info*, n_formats);

                for (j = 0; j < n_formats; j++) {
                    i.n_formats++;
                    i.formats[j] = pa_format_info_new();

                    if (pa_tagstruct_get_format_info(t, i.formats[j]) < 0)
                        goto fail;
                }
            }

            i.mute = (int) mute;
            i.flags = (pa_sink_flags_t) flags;
            i.state = (pa_sink_state_t) state;

            if (o->callback) {
                pa_sink_info_cb_t cb = (pa_sink_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            if (i.formats) {
                for (j = 0; j < i.n_formats; j++)
                    pa_format_info_free(i.formats[j]);
                pa_xfree(i.formats);
            }
            if (i.ports) {
                pa_xfree(i.ports[0]);
                pa_xfree(i.ports);
            }
            pa_proplist_free(i.proplist);
        }
    }

    if (o->callback) {
        pa_sink_info_cb_t cb = (pa_sink_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
    return;

fail:
    pa_assert(i.proplist);

    pa_context_fail(o->context, PA_ERR_PROTOCOL);

    if (i.formats) {
        for (j = 0; j < i.n_formats; j++)
            pa_format_info_free(i.formats[j]);
        pa_xfree(i.formats);
    }
    if (i.ports) {
        pa_xfree(i.ports[0]);
        pa_xfree(i.ports);
    }
    pa_proplist_free(i.proplist);

    goto finish;
}

pa_operation* pa_context_get_sink_info_list(pa_context *c, pa_sink_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SINK_INFO_LIST, context_get_sink_info_callback, (pa_operation_cb_t) cb, userdata);
}

pa_operation* pa_context_get_sink_info_by_index(pa_context *c, uint32_t idx, pa_sink_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SINK_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_sink_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_sink_info_by_name(pa_context *c, const char *name, pa_sink_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SINK_INFO, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_sink_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_port_by_index(pa_context *c, uint32_t idx, const char*port, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 16, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_PORT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_puts(t, port);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_port_by_name(pa_context *c, const char *name, const char*port, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 16, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_PORT, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_puts(t, port);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/*** Source info ***/

static void context_get_source_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int eol = 1;
    pa_source_info i;
    uint32_t j;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    /* For safety in case someone use fail: outside the while loop below */
    pa_zero(i);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        eol = -1;
    } else {

        while (!pa_tagstruct_eof(t)) {
            bool mute;
            uint32_t flags;
            uint32_t state;
            const char *ap;

            pa_zero(i);
            i.proplist = pa_proplist_new();
            i.base_volume = PA_VOLUME_NORM;
            i.n_volume_steps = PA_VOLUME_NORM+1;
            mute = false;
            state = PA_SOURCE_INVALID_STATE;
            i.card = PA_INVALID_INDEX;

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_gets(t, &i.description) < 0 ||
                pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                pa_tagstruct_get_boolean(t, &mute) < 0 ||
                pa_tagstruct_getu32(t, &i.monitor_of_sink) < 0 ||
                pa_tagstruct_gets(t, &i.monitor_of_sink_name) < 0 ||
                pa_tagstruct_get_usec(t, &i.latency) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                pa_tagstruct_getu32(t, &flags) < 0 ||
                (o->context->version >= 13 &&
                 (pa_tagstruct_get_proplist(t, i.proplist) < 0 ||
                  pa_tagstruct_get_usec(t, &i.configured_latency) < 0)) ||
                (o->context->version >= 15 &&
                 (pa_tagstruct_get_volume(t, &i.base_volume) < 0 ||
                  pa_tagstruct_getu32(t, &state) < 0 ||
                  pa_tagstruct_getu32(t, &i.n_volume_steps) < 0 ||
                  pa_tagstruct_getu32(t, &i.card) < 0)) ||
                (o->context->version >= 16 &&
                 (pa_tagstruct_getu32(t, &i.n_ports)))) {

                goto fail;
            }

            if (o->context->version >= 16) {
                if (i.n_ports > 0) {
                    i.ports = pa_xnew(pa_source_port_info*, i.n_ports+1);
                    i.ports[0] = pa_xnew(pa_source_port_info, i.n_ports);

                    for (j = 0; j < i.n_ports; j++) {
                        i.ports[j] = &i.ports[0][j];

                        if (pa_tagstruct_gets(t, &i.ports[j]->name) < 0 ||
                            pa_tagstruct_gets(t, &i.ports[j]->description) < 0 ||
                            pa_tagstruct_getu32(t, &i.ports[j]->priority) < 0) {

                            goto fail;
                        }

                        i.ports[j]->available = PA_PORT_AVAILABLE_UNKNOWN;
                        if (o->context->version >= 24) {
                            uint32_t av;
                            if (pa_tagstruct_getu32(t, &av) < 0 || av > PA_PORT_AVAILABLE_YES)
                                goto fail;
                            i.ports[j]->available = av;
                        }
                        i.ports[j]->availability_group = NULL;
                        i.ports[j]->type = PA_DEVICE_PORT_TYPE_UNKNOWN;
                        if (o->context->version >= 34) {
                            if (pa_tagstruct_gets(t, &i.ports[j]->availability_group) < 0 ||
                                pa_tagstruct_getu32(t, &i.ports[j]->type))
                                goto fail;
                        }
                    }

                    i.ports[j] = NULL;
                }
                if (pa_tagstruct_gets(t, &ap) < 0)
                    goto fail;

                if (ap) {
                    for (j = 0; j < i.n_ports; j++)
                        if (pa_streq(i.ports[j]->name, ap)) {
                            i.active_port = i.ports[j];
                            break;
                        }
                }
            }

            if (o->context->version >= 22) {
                uint8_t n_formats;
                if (pa_tagstruct_getu8(t, &n_formats) < 0 || n_formats < 1)
                    goto fail;

                i.formats = pa_xnew0(pa_format_info*, n_formats);

                for (j = 0; j < n_formats; j++) {
                    i.n_formats++;
                    i.formats[j] = pa_format_info_new();

                    if (pa_tagstruct_get_format_info(t, i.formats[j]) < 0)
                        goto fail;
                }
            }

            i.mute = (int) mute;
            i.flags = (pa_source_flags_t) flags;
            i.state = (pa_source_state_t) state;

            if (o->callback) {
                pa_source_info_cb_t cb = (pa_source_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            if (i.formats) {
                for (j = 0; j < i.n_formats; j++)
                    pa_format_info_free(i.formats[j]);
                pa_xfree(i.formats);
            }
            if (i.ports) {
                pa_xfree(i.ports[0]);
                pa_xfree(i.ports);
            }
            pa_proplist_free(i.proplist);
        }
    }

    if (o->callback) {
        pa_source_info_cb_t cb = (pa_source_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
    return;

fail:
    pa_assert(i.proplist);

    pa_context_fail(o->context, PA_ERR_PROTOCOL);

    if (i.formats) {
        for (j = 0; j < i.n_formats; j++)
            pa_format_info_free(i.formats[j]);
        pa_xfree(i.formats);
    }
    if (i.ports) {
        pa_xfree(i.ports[0]);
        pa_xfree(i.ports);
    }
    pa_proplist_free(i.proplist);

    goto finish;
}

pa_operation* pa_context_get_source_info_list(pa_context *c, pa_source_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SOURCE_INFO_LIST, context_get_source_info_callback, (pa_operation_cb_t) cb, userdata);
}

pa_operation* pa_context_get_source_info_by_index(pa_context *c, uint32_t idx, pa_source_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SOURCE_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_source_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_source_info_by_name(pa_context *c, const char *name, pa_source_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SOURCE_INFO, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_source_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_port_by_index(pa_context *c, uint32_t idx, const char*port, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 16, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_PORT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_puts(t, port);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_port_by_name(pa_context *c, const char *name, const char*port, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 16, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_PORT, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_puts(t, port);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/*** Client info ***/

static void context_get_client_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
            pa_client_info i;

            pa_zero(i);
            i.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                (o->context->version >= 13 && pa_tagstruct_get_proplist(t, i.proplist) < 0)) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                pa_proplist_free(i.proplist);
                goto finish;
            }

            if (o->callback) {
                pa_client_info_cb_t cb = (pa_client_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            pa_proplist_free(i.proplist);
        }
    }

    if (o->callback) {
        pa_client_info_cb_t cb = (pa_client_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_client_info(pa_context *c, uint32_t idx, pa_client_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_CLIENT_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_client_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_client_info_list(pa_context *c, pa_client_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_CLIENT_INFO_LIST, context_get_client_info_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Card info ***/

static void card_info_free(pa_card_info* i) {
    if (i->proplist)
        pa_proplist_free(i->proplist);

    pa_xfree(i->profiles);

    if (i->n_profiles) {
        uint32_t j;

        for (j = 0; j < i->n_profiles; j++)
             pa_xfree(i->profiles2[j]);

        pa_xfree(i->profiles2);
    }

    if (i->ports) {
        uint32_t j;

        for (j = 0; j < i->n_ports; j++) {
            if (i->ports[j]) {
                if (i->ports[j]->profiles)
                    pa_xfree(i->ports[j]->profiles);
                if (i->ports[j]->profiles2)
                    pa_xfree(i->ports[j]->profiles2);
                if (i->ports[j]->proplist)
                    pa_proplist_free(i->ports[j]->proplist);
            }
        }

        pa_xfree(i->ports[0]);
        pa_xfree(i->ports);
    }
}

static int fill_card_port_info(pa_context *context, pa_tagstruct* t, pa_card_info* i) {
    uint32_t j, k, l;

    if (pa_tagstruct_getu32(t, &i->n_ports) < 0)
        return -PA_ERR_PROTOCOL;

    if (i->n_ports == 0) {
        i->ports = NULL;
        return 0;
    }

    i->ports = pa_xnew0(pa_card_port_info*, i->n_ports+1);
    i->ports[0] = pa_xnew0(pa_card_port_info, i->n_ports);

    for (j = 0; j < i->n_ports; j++) {
        uint8_t direction;
        uint32_t available;
        pa_card_port_info* port = i->ports[j] = &i->ports[0][j];

        port->proplist = pa_proplist_new();

        if (pa_tagstruct_gets(t, &port->name) < 0 ||
            pa_tagstruct_gets(t, &port->description) < 0 ||
            pa_tagstruct_getu32(t, &port->priority) < 0 ||
            pa_tagstruct_getu32(t, &available) < 0 ||
            pa_tagstruct_getu8(t, &direction) < 0 ||
            !pa_direction_valid(direction) ||
            pa_tagstruct_get_proplist(t, port->proplist) < 0 ||
            pa_tagstruct_getu32(t, &port->n_profiles) < 0) {

            return -PA_ERR_PROTOCOL;
        }

        if (available > PA_PORT_AVAILABLE_YES ) {
            return -PA_ERR_PROTOCOL;
        }

        port->direction = direction;
        port->available = available;

        if (port->n_profiles > 0) {
            port->profiles = pa_xnew0(pa_card_profile_info*, i->n_profiles+1);
            port->profiles2 = pa_xnew0(pa_card_profile_info2*, i->n_profiles+1);

            for (k = 0; k < port->n_profiles; k++) {
                const char* profilename;

                if (pa_tagstruct_gets(t, &profilename) < 0)
                    return -PA_ERR_PROTOCOL;

                for (l = 0; l < i->n_profiles; l++) {
                    if (pa_streq(i->profiles[l].name, profilename)) {
                        port->profiles[k] = &i->profiles[l];
                        port->profiles2[k] = i->profiles2[l];
                        break;
                    }
                }

                if (l >= i->n_profiles)
                    return -PA_ERR_PROTOCOL;
            }
        }
        if (context->version >= 27) {
            if (pa_tagstruct_gets64(t, &port->latency_offset) < 0)
                return -PA_ERR_PROTOCOL;
        } else
            port->latency_offset = 0;

        port->type = PA_DEVICE_PORT_TYPE_UNKNOWN;
        if (context->version >= 34) {
            if (pa_tagstruct_gets(t, &port->availability_group) < 0 ||
                pa_tagstruct_getu32(t, &port->type) < 0)
                return -PA_ERR_PROTOCOL;
        } else
            port->availability_group = NULL;
    }

    return 0;
}

static int fill_card_profile_info(pa_context *context, pa_tagstruct* t, pa_card_info* i) {
    uint32_t j;

    i->profiles = pa_xnew0(pa_card_profile_info, i->n_profiles+1);
    i->profiles2 = pa_xnew0(pa_card_profile_info2*, i->n_profiles+1);

    for (j = 0; j < i->n_profiles; j++) {
        if (pa_tagstruct_gets(t, &i->profiles[j].name) < 0 ||
            pa_tagstruct_gets(t, &i->profiles[j].description) < 0 ||
            pa_tagstruct_getu32(t, &i->profiles[j].n_sinks) < 0 ||
            pa_tagstruct_getu32(t, &i->profiles[j].n_sources) < 0 ||
            pa_tagstruct_getu32(t, &i->profiles[j].priority) < 0)
                return -PA_ERR_PROTOCOL;

        i->profiles2[j] = pa_xnew0(pa_card_profile_info2, 1);
        i->profiles2[j]->name = i->profiles[j].name;
        i->profiles2[j]->description = i->profiles[j].description;
        i->profiles2[j]->n_sinks = i->profiles[j].n_sinks;
        i->profiles2[j]->n_sources = i->profiles[j].n_sources;
        i->profiles2[j]->priority = i->profiles[j].priority;
        i->profiles2[j]->available = 1;

        if (context->version >= 29) {
            uint32_t av;

            if (pa_tagstruct_getu32(t, &av) < 0)
                return -PA_ERR_PROTOCOL;

            i->profiles2[j]->available = av;
        }
    }

    return 0;
}

static void context_get_card_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int eol = 1;
    pa_card_info i;

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
            uint32_t j;
            const char*ap;

            pa_zero(i);

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                pa_tagstruct_getu32(t, &i.n_profiles) < 0)
                    goto fail;

            if (i.n_profiles > 0) {
                if (fill_card_profile_info(o->context, t, &i) < 0)
                    goto fail;
            }

            i.proplist = pa_proplist_new();

            if (pa_tagstruct_gets(t, &ap) < 0 ||
                pa_tagstruct_get_proplist(t, i.proplist) < 0) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                card_info_free(&i);
                goto finish;
            }

            if (ap) {
                for (j = 0; j < i.n_profiles; j++)
                    if (pa_streq(i.profiles[j].name, ap)) {
                        i.active_profile = &i.profiles[j];
                        i.active_profile2 = i.profiles2[j];
                        break;
                    }
            }

            if (o->context->version >= 26) {
                if (fill_card_port_info(o->context, t, &i) < 0)
                    goto fail;
            }

            if (o->callback) {
                pa_card_info_cb_t cb = (pa_card_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            card_info_free(&i);
        }
    }

    if (o->callback) {
        pa_card_info_cb_t cb = (pa_card_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
    return;

fail:
    pa_context_fail(o->context, PA_ERR_PROTOCOL);
    card_info_free(&i);
    goto finish;
}

pa_operation* pa_context_get_card_info_by_index(pa_context *c, uint32_t idx, pa_card_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 15, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_CARD_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_card_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_card_info_by_name(pa_context *c, const char*name, pa_card_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 15, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_CARD_INFO, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_card_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_card_info_list(pa_context *c, pa_card_info_cb_t cb, void *userdata) {
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 15, PA_ERR_NOTSUPPORTED);

    return pa_context_send_simple_command(c, PA_COMMAND_GET_CARD_INFO_LIST, context_get_card_info_callback, (pa_operation_cb_t) cb, userdata);
}

pa_operation* pa_context_set_card_profile_by_index(pa_context *c, uint32_t idx, const char*profile, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 15, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_CARD_PROFILE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_puts(t, profile);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_card_profile_by_name(pa_context *c, const char *name, const char*profile, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 15, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_CARD_PROFILE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_puts(t, profile);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/*** Module info ***/

static void context_get_module_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
            pa_module_info i;
            bool auto_unload = false;

            pa_zero(i);
            i.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_gets(t, &i.argument) < 0 ||
                pa_tagstruct_getu32(t, &i.n_used) < 0 ||
                (o->context->version < 15 && pa_tagstruct_get_boolean(t, &auto_unload) < 0) ||
                (o->context->version >= 15 && pa_tagstruct_get_proplist(t, i.proplist) < 0)) {
                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            i.auto_unload = (int) auto_unload;

            if (o->callback) {
                pa_module_info_cb_t cb = (pa_module_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            pa_proplist_free(i.proplist);
        }
    }

    if (o->callback) {
        pa_module_info_cb_t cb = (pa_module_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_module_info(pa_context *c, uint32_t idx, pa_module_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_MODULE_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_module_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_module_info_list(pa_context *c, pa_module_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_MODULE_INFO_LIST, context_get_module_info_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Sink input info ***/

static void context_get_sink_input_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
            pa_sink_input_info i;
            bool mute = false, corked = false, has_volume = false, volume_writable = true;

            pa_zero(i);
            i.proplist = pa_proplist_new();
            i.format = pa_format_info_new();

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_getu32(t, &i.client) < 0 ||
                pa_tagstruct_getu32(t, &i.sink) < 0 ||
                pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                pa_tagstruct_get_usec(t, &i.buffer_usec) < 0 ||
                pa_tagstruct_get_usec(t, &i.sink_usec) < 0 ||
                pa_tagstruct_gets(t, &i.resample_method) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                (o->context->version >= 11 && pa_tagstruct_get_boolean(t, &mute) < 0) ||
                (o->context->version >= 13 && pa_tagstruct_get_proplist(t, i.proplist) < 0) ||
                (o->context->version >= 19 && pa_tagstruct_get_boolean(t, &corked) < 0) ||
                (o->context->version >= 20 && (pa_tagstruct_get_boolean(t, &has_volume) < 0 ||
                                               pa_tagstruct_get_boolean(t, &volume_writable) < 0)) ||
                (o->context->version >= 21 && pa_tagstruct_get_format_info(t, i.format) < 0)) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                pa_proplist_free(i.proplist);
                pa_format_info_free(i.format);
                goto finish;
            }

            i.mute = (int) mute;
            i.corked = (int) corked;
            i.has_volume = (int) has_volume;
            i.volume_writable = (int) volume_writable;

            if (o->callback) {
                pa_sink_input_info_cb_t cb = (pa_sink_input_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            pa_proplist_free(i.proplist);
            pa_format_info_free(i.format);
        }
    }

    if (o->callback) {
        pa_sink_input_info_cb_t cb = (pa_sink_input_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_sink_input_info(pa_context *c, uint32_t idx, pa_sink_input_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SINK_INPUT_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_sink_input_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_sink_input_info_list(pa_context *c, void (*cb)(pa_context *c, const pa_sink_input_info*i, int is_last, void *userdata), void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SINK_INPUT_INFO_LIST, context_get_sink_input_info_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Source output info ***/

static void context_get_source_output_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
            pa_source_output_info i;
            bool mute = false, corked = false, has_volume = false, volume_writable = true;

            pa_zero(i);
            i.proplist = pa_proplist_new();
            i.format = pa_format_info_new();

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_getu32(t, &i.owner_module) < 0 ||
                pa_tagstruct_getu32(t, &i.client) < 0 ||
                pa_tagstruct_getu32(t, &i.source) < 0 ||
                pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_get_usec(t, &i.buffer_usec) < 0 ||
                pa_tagstruct_get_usec(t, &i.source_usec) < 0 ||
                pa_tagstruct_gets(t, &i.resample_method) < 0 ||
                pa_tagstruct_gets(t, &i.driver) < 0 ||
                (o->context->version >= 13 && pa_tagstruct_get_proplist(t, i.proplist) < 0) ||
                (o->context->version >= 19 && pa_tagstruct_get_boolean(t, &corked) < 0) ||
                (o->context->version >= 22 && (pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                                               pa_tagstruct_get_boolean(t, &mute) < 0 ||
                                               pa_tagstruct_get_boolean(t, &has_volume) < 0 ||
                                               pa_tagstruct_get_boolean(t, &volume_writable) < 0 ||
                                               pa_tagstruct_get_format_info(t, i.format) < 0))) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                pa_proplist_free(i.proplist);
                pa_format_info_free(i.format);
                goto finish;
            }

            i.mute = (int) mute;
            i.corked = (int) corked;
            i.has_volume = (int) has_volume;
            i.volume_writable = (int) volume_writable;

            if (o->callback) {
                pa_source_output_info_cb_t cb = (pa_source_output_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            pa_proplist_free(i.proplist);
            pa_format_info_free(i.format);
        }
    }

    if (o->callback) {
        pa_source_output_info_cb_t cb = (pa_source_output_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_source_output_info(pa_context *c, uint32_t idx, pa_source_output_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SOURCE_OUTPUT_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_source_output_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_source_output_info_list(pa_context *c,  pa_source_output_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST, context_get_source_output_info_callback, (pa_operation_cb_t) cb, userdata);
}

/*** Volume manipulation ***/

pa_operation* pa_context_set_sink_volume_by_index(pa_context *c, uint32_t idx, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_VOLUME, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_volume_by_name(pa_context *c, const char *name, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(name);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_VOLUME, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_mute_by_index(pa_context *c, uint32_t idx, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_MUTE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_mute_by_name(pa_context *c, const char *name, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(name);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_MUTE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_input_volume(pa_context *c, uint32_t idx, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_INPUT_VOLUME, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_sink_input_mute(pa_context *c, uint32_t idx, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 11, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SINK_INPUT_MUTE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_volume_by_index(pa_context *c, uint32_t idx, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_VOLUME, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_volume_by_name(pa_context *c, const char *name, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(name);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_VOLUME, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_mute_by_index(pa_context *c, uint32_t idx, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_MUTE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_mute_by_name(pa_context *c, const char *name, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(name);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !name || *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_MUTE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_output_volume(pa_context *c, uint32_t idx, const pa_cvolume *volume, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(volume);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 22, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_cvolume_valid(volume), PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_OUTPUT_VOLUME, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_put_cvolume(t, volume);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_source_output_mute(pa_context *c, uint32_t idx, int mute, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 22, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_SOURCE_OUTPUT_MUTE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_put_boolean(t, mute);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/** Sample Cache **/

static void context_get_sample_info_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
            pa_sample_info i;
            bool lazy = false;

            pa_zero(i);
            i.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(t, &i.index) < 0 ||
                pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_get_cvolume(t, &i.volume) < 0 ||
                pa_tagstruct_get_usec(t, &i.duration) < 0 ||
                pa_tagstruct_get_sample_spec(t, &i.sample_spec) < 0 ||
                pa_tagstruct_get_channel_map(t, &i.channel_map) < 0 ||
                pa_tagstruct_getu32(t, &i.bytes) < 0 ||
                pa_tagstruct_get_boolean(t, &lazy) < 0 ||
                pa_tagstruct_gets(t, &i.filename) < 0 ||
                (o->context->version >= 13 && pa_tagstruct_get_proplist(t, i.proplist) < 0)) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            i.lazy = (int) lazy;

            if (o->callback) {
                pa_sample_info_cb_t cb = (pa_sample_info_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

            pa_proplist_free(i.proplist);
        }
    }

    if (o->callback) {
        pa_sample_info_cb_t cb = (pa_sample_info_cb_t) o->callback;
        cb(o->context, NULL, eol, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_get_sample_info_by_name(pa_context *c, const char *name, pa_sample_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, name && *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SAMPLE_INFO, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_sample_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_sample_info_by_index(pa_context *c, uint32_t idx, pa_sample_info_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(cb);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_GET_SAMPLE_INFO, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_get_sample_info_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_get_sample_info_list(pa_context *c, pa_sample_info_cb_t cb, void *userdata) {
    return pa_context_send_simple_command(c, PA_COMMAND_GET_SAMPLE_INFO_LIST, context_get_sample_info_callback, (pa_operation_cb_t) cb, userdata);
}

static pa_operation* command_kill(pa_context *c, uint32_t command, uint32_t idx, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, command, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_kill_client(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void *userdata) {
    return command_kill(c, PA_COMMAND_KILL_CLIENT, idx, cb, userdata);
}

pa_operation* pa_context_kill_sink_input(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void *userdata) {
    return command_kill(c, PA_COMMAND_KILL_SINK_INPUT, idx, cb, userdata);
}

pa_operation* pa_context_kill_source_output(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void *userdata) {
    return command_kill(c, PA_COMMAND_KILL_SOURCE_OUTPUT, idx, cb, userdata);
}

static void context_index_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
    } else if (pa_tagstruct_getu32(t, &idx) ||
               !pa_tagstruct_eof(t)) {
        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_context_index_cb_t cb = (pa_context_index_cb_t) o->callback;
        cb(o->context, idx, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_load_module(pa_context *c, const char*name, const char *argument, pa_context_index_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, name && *name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_LOAD_MODULE, &tag);
    pa_tagstruct_puts(t, name);
    pa_tagstruct_puts(t, argument);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, context_index_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_unload_module(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void *userdata) {
    return command_kill(c, PA_COMMAND_UNLOAD_MODULE, idx, cb, userdata);
}

pa_operation* pa_context_set_port_latency_offset(pa_context *c, const char *card_name, const char *port_name, int64_t offset, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, card_name && *card_name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, port_name && *port_name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 27, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SET_PORT_LATENCY_OFFSET, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, card_name);
    pa_tagstruct_puts(t, port_name);
    pa_tagstruct_puts64(t, offset);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

/*** Autoload stuff ***/

PA_WARN_REFERENCE(pa_context_get_autoload_info_by_name, "Module auto-loading no longer supported.");

pa_operation* pa_context_get_autoload_info_by_name(pa_context *c, const char *name, pa_autoload_type_t type, pa_autoload_info_cb_t cb, void *userdata) {

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

PA_WARN_REFERENCE(pa_context_get_autoload_info_by_index, "Module auto-loading no longer supported.");

pa_operation* pa_context_get_autoload_info_by_index(pa_context *c, uint32_t idx, pa_autoload_info_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

PA_WARN_REFERENCE(pa_context_get_autoload_info_list, "Module auto-loading no longer supported.");

pa_operation* pa_context_get_autoload_info_list(pa_context *c, pa_autoload_info_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

PA_WARN_REFERENCE(pa_context_add_autoload, "Module auto-loading no longer supported.");

pa_operation* pa_context_add_autoload(pa_context *c, const char *name, pa_autoload_type_t type, const char *module, const char*argument, pa_context_index_cb_t cb, void* userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

PA_WARN_REFERENCE(pa_context_remove_autoload_by_name, "Module auto-loading no longer supported.");

pa_operation* pa_context_remove_autoload_by_name(pa_context *c, const char *name, pa_autoload_type_t type, pa_context_success_cb_t cb, void* userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

PA_WARN_REFERENCE(pa_context_remove_autoload_by_index, "Module auto-loading no longer supported.");

pa_operation* pa_context_remove_autoload_by_index(pa_context *c, uint32_t idx, pa_context_success_cb_t cb, void* userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_FAIL_RETURN_NULL(c, PA_ERR_OBSOLETE);
}

pa_operation* pa_context_move_sink_input_by_name(pa_context *c, uint32_t idx, const char *sink_name, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 10, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, sink_name && *sink_name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_MOVE_SINK_INPUT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, sink_name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_move_sink_input_by_index(pa_context *c, uint32_t idx, uint32_t sink_idx, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 10, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, sink_idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_MOVE_SINK_INPUT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_putu32(t, sink_idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_move_source_output_by_name(pa_context *c, uint32_t idx, const char *source_name, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 10, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, source_name && *source_name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_MOVE_SOURCE_OUTPUT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, source_name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_move_source_output_by_index(pa_context *c, uint32_t idx, uint32_t source_idx, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 10, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, source_idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_MOVE_SOURCE_OUTPUT, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_putu32(t, source_idx);
    pa_tagstruct_puts(t, NULL);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_suspend_sink_by_name(pa_context *c, const char *sink_name, int suspend, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 11, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !sink_name || *sink_name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SUSPEND_SINK, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, sink_name);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_suspend_sink_by_index(pa_context *c, uint32_t idx, int suspend, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 11, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SUSPEND_SINK, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, idx == PA_INVALID_INDEX ? "" : NULL);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_suspend_source_by_name(pa_context *c, const char *source_name, int suspend, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 11, PA_ERR_NOTSUPPORTED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, !source_name || *source_name, PA_ERR_INVALID);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SUSPEND_SOURCE, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, source_name);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_suspend_source_by_index(pa_context *c, uint32_t idx, int suspend, pa_context_success_cb_t cb, void* userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 11, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_SUSPEND_SOURCE, &tag);
    pa_tagstruct_putu32(t, idx);
    pa_tagstruct_puts(t, idx == PA_INVALID_INDEX ? "" : NULL);
    pa_tagstruct_put_boolean(t, suspend);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}
