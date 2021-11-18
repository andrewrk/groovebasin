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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/native-common.h>
#include <pulsecore/llist.h>
#include <pulsecore/log.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/flist.h>
#include <pulsecore/core-rtclock.h>

#include "pdispatch.h"

/* #define DEBUG_OPCODES */

#ifdef DEBUG_OPCODES

static const char *command_names[PA_COMMAND_MAX] = {
    /* Generic commands */
    [PA_COMMAND_ERROR] = "ERROR",
    [PA_COMMAND_TIMEOUT] = "TIMEOUT",
    [PA_COMMAND_REPLY] = "REPLY",

    /* CLIENT->SERVER */
    [PA_COMMAND_CREATE_PLAYBACK_STREAM] = "CREATE_PLAYBACK_STREAM",
    [PA_COMMAND_DELETE_PLAYBACK_STREAM] = "DELETE_PLAYBACK_STREAM",
    [PA_COMMAND_CREATE_RECORD_STREAM] = "CREATE_RECORD_STREAM",
    [PA_COMMAND_DELETE_RECORD_STREAM] = "DELETE_RECORD_STREAM",
    [PA_COMMAND_AUTH] = "AUTH",
    [PA_COMMAND_EXIT] = "EXIT",
    [PA_COMMAND_SET_CLIENT_NAME] = "SET_CLIENT_NAME",
    [PA_COMMAND_LOOKUP_SINK] = "LOOKUP_SINK",
    [PA_COMMAND_LOOKUP_SOURCE] = "LOOKUP_SOURCE",
    [PA_COMMAND_DRAIN_PLAYBACK_STREAM] = "DRAIN_PLAYBACK_STREAM",
    [PA_COMMAND_STAT] = "STAT",
    [PA_COMMAND_GET_PLAYBACK_LATENCY] = "GET_PLAYBACK_LATENCY",
    [PA_COMMAND_CREATE_UPLOAD_STREAM] = "CREATE_UPLOAD_STREAM",
    [PA_COMMAND_DELETE_UPLOAD_STREAM] = "DELETE_UPLOAD_STREAM",
    [PA_COMMAND_FINISH_UPLOAD_STREAM] = "FINISH_UPLOAD_STREAM",
    [PA_COMMAND_PLAY_SAMPLE] = "PLAY_SAMPLE",
    [PA_COMMAND_REMOVE_SAMPLE] = "REMOVE_SAMPLE",

    [PA_COMMAND_GET_SERVER_INFO] = "GET_SERVER_INFO",
    [PA_COMMAND_GET_SINK_INFO] = "GET_SINK_INFO",
    [PA_COMMAND_GET_SINK_INFO_LIST] = "GET_SINK_INFO_LIST",
    [PA_COMMAND_GET_SOURCE_INFO] = "GET_SOURCE_INFO",
    [PA_COMMAND_GET_SOURCE_INFO_LIST] = "GET_SOURCE_INFO_LIST",
    [PA_COMMAND_GET_MODULE_INFO] = "GET_MODULE_INFO",
    [PA_COMMAND_GET_MODULE_INFO_LIST] = "GET_MODULE_INFO_LIST",
    [PA_COMMAND_GET_CLIENT_INFO] = "GET_CLIENT_INFO",
    [PA_COMMAND_GET_CLIENT_INFO_LIST] = "GET_CLIENT_INFO_LIST",
    [PA_COMMAND_GET_SAMPLE_INFO] = "GET_SAMPLE_INFO",
    [PA_COMMAND_GET_SAMPLE_INFO_LIST] = "GET_SAMPLE_INFO_LIST",
    [PA_COMMAND_GET_SINK_INPUT_INFO] = "GET_SINK_INPUT_INFO",
    [PA_COMMAND_GET_SINK_INPUT_INFO_LIST] = "GET_SINK_INPUT_INFO_LIST",
    [PA_COMMAND_GET_SOURCE_OUTPUT_INFO] = "GET_SOURCE_OUTPUT_INFO",
    [PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST] = "GET_SOURCE_OUTPUT_INFO_LIST",
    [PA_COMMAND_SUBSCRIBE] = "SUBSCRIBE",

    [PA_COMMAND_SET_SINK_VOLUME] = "SET_SINK_VOLUME",
    [PA_COMMAND_SET_SINK_INPUT_VOLUME] = "SET_SINK_INPUT_VOLUME",
    [PA_COMMAND_SET_SOURCE_VOLUME] = "SET_SOURCE_VOLUME",

    [PA_COMMAND_SET_SINK_MUTE] = "SET_SINK_MUTE",
    [PA_COMMAND_SET_SOURCE_MUTE] = "SET_SOURCE_MUTE",

    [PA_COMMAND_TRIGGER_PLAYBACK_STREAM] = "TRIGGER_PLAYBACK_STREAM",
    [PA_COMMAND_FLUSH_PLAYBACK_STREAM] = "FLUSH_PLAYBACK_STREAM",
    [PA_COMMAND_CORK_PLAYBACK_STREAM] = "CORK_PLAYBACK_STREAM",

    [PA_COMMAND_SET_DEFAULT_SINK] = "SET_DEFAULT_SINK",
    [PA_COMMAND_SET_DEFAULT_SOURCE] = "SET_DEFAULT_SOURCE",

    [PA_COMMAND_SET_PLAYBACK_STREAM_NAME] = "SET_PLAYBACK_STREAM_NAME",
    [PA_COMMAND_SET_RECORD_STREAM_NAME] = "SET_RECORD_STREAM_NAME",

    [PA_COMMAND_KILL_CLIENT] = "KILL_CLIENT",
    [PA_COMMAND_KILL_SINK_INPUT] = "KILL_SINK_INPUT",
    [PA_COMMAND_KILL_SOURCE_OUTPUT] = "KILL_SOURCE_OUTPUT",

    [PA_COMMAND_LOAD_MODULE] = "LOAD_MODULE",
    [PA_COMMAND_UNLOAD_MODULE] = "UNLOAD_MODULE",

    [PA_COMMAND_ADD_AUTOLOAD___OBSOLETE] = "ADD_AUTOLOAD (obsolete)",
    [PA_COMMAND_REMOVE_AUTOLOAD___OBSOLETE] = "REMOVE_AUTOLOAD (obsolete)",
    [PA_COMMAND_GET_AUTOLOAD_INFO___OBSOLETE] = "GET_AUTOLOAD_INFO (obsolete)",
    [PA_COMMAND_GET_AUTOLOAD_INFO_LIST___OBSOLETE] = "GET_AUTOLOAD_INFO_LIST (obsolete)",

    [PA_COMMAND_GET_RECORD_LATENCY] = "GET_RECORD_LATENCY",
    [PA_COMMAND_CORK_RECORD_STREAM] = "CORK_RECORD_STREAM",
    [PA_COMMAND_FLUSH_RECORD_STREAM] = "FLUSH_RECORD_STREAM",
    [PA_COMMAND_PREBUF_PLAYBACK_STREAM] = "PREBUF_PLAYBACK_STREAM",

    /* SERVER->CLIENT */
    [PA_COMMAND_REQUEST] = "REQUEST",
    [PA_COMMAND_OVERFLOW] = "OVERFLOW",
    [PA_COMMAND_UNDERFLOW] = "UNDERFLOW",
    [PA_COMMAND_PLAYBACK_STREAM_KILLED] = "PLAYBACK_STREAM_KILLED",
    [PA_COMMAND_RECORD_STREAM_KILLED] = "RECORD_STREAM_KILLED",
    [PA_COMMAND_SUBSCRIBE_EVENT] = "SUBSCRIBE_EVENT",

    /* A few more client->server commands */

    /* Supported since protocol v10 (0.9.5) */
    [PA_COMMAND_MOVE_SINK_INPUT] = "MOVE_SINK_INPUT",
    [PA_COMMAND_MOVE_SOURCE_OUTPUT] = "MOVE_SOURCE_OUTPUT",

    /* Supported since protocol v11 (0.9.7) */
    [PA_COMMAND_SET_SINK_INPUT_MUTE] = "SET_SINK_INPUT_MUTE",

    [PA_COMMAND_SUSPEND_SINK] = "SUSPEND_SINK",
    [PA_COMMAND_SUSPEND_SOURCE] = "SUSPEND_SOURCE",

    /* Supported since protocol v12 (0.9.8) */
    [PA_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR] = "SET_PLAYBACK_STREAM_BUFFER_ATTR",
    [PA_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR] = "SET_RECORD_STREAM_BUFFER_ATTR",

    [PA_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE] = "UPDATE_PLAYBACK_STREAM_SAMPLE_RATE",
    [PA_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE] = "UPDATE_RECORD_STREAM_SAMPLE_RATE",

    /* SERVER->CLIENT */
    [PA_COMMAND_PLAYBACK_STREAM_SUSPENDED] = "PLAYBACK_STREAM_SUSPENDED",
    [PA_COMMAND_RECORD_STREAM_SUSPENDED] = "RECORD_STREAM_SUSPENDED",
    [PA_COMMAND_PLAYBACK_STREAM_MOVED] = "PLAYBACK_STREAM_MOVED",
    [PA_COMMAND_RECORD_STREAM_MOVED] = "RECORD_STREAM_MOVED",

    /* Supported since protocol v13 (0.9.11) */
    [PA_COMMAND_UPDATE_RECORD_STREAM_PROPLIST] = "UPDATE_RECORD_STREAM_PROPLIST",
    [PA_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST] = "UPDATE_PLAYBACK_STREAM_PROPLIST",
    [PA_COMMAND_UPDATE_CLIENT_PROPLIST] = "UPDATE_CLIENT_PROPLIST",
    [PA_COMMAND_REMOVE_RECORD_STREAM_PROPLIST] = "REMOVE_RECORD_STREAM_PROPLIST",
    [PA_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST] = "REMOVE_PLAYBACK_STREAM_PROPLIST",
    [PA_COMMAND_REMOVE_CLIENT_PROPLIST] = "REMOVE_CLIENT_PROPLIST",

    /* SERVER->CLIENT */
    [PA_COMMAND_STARTED] = "STARTED",

    /* Supported since protocol v14 (0.9.12) */
    [PA_COMMAND_EXTENSION] = "EXTENSION",

    /* Supported since protocol v15 (0.9.15) */
    [PA_COMMAND_GET_CARD_INFO] = "GET_CARD_INFO",
    [PA_COMMAND_GET_CARD_INFO_LIST] = "GET_CARD_INFO_LIST",
    [PA_COMMAND_SET_CARD_PROFILE] = "SET_CARD_PROFILE",

    [PA_COMMAND_CLIENT_EVENT] = "CLIENT_EVENT",
    [PA_COMMAND_PLAYBACK_STREAM_EVENT] = "PLAYBACK_STREAM_EVENT",
    [PA_COMMAND_RECORD_STREAM_EVENT] = "RECORD_STREAM_EVENT",

    /* SERVER->CLIENT */
    [PA_COMMAND_PLAYBACK_BUFFER_ATTR_CHANGED] = "PLAYBACK_BUFFER_ATTR_CHANGED",
    [PA_COMMAND_RECORD_BUFFER_ATTR_CHANGED] = "RECORD_BUFFER_ATTR_CHANGED",

    /* Supported since protocol v16 (0.9.16) */
    [PA_COMMAND_SET_SINK_PORT] = "SET_SINK_PORT",
    [PA_COMMAND_SET_SOURCE_PORT] = "SET_SOURCE_PORT",

    /* Supported since protocol v22 (1.0) */
    [PA_COMMAND_SET_SOURCE_OUTPUT_VOLUME] = "SET_SOURCE_OUTPUT_VOLUME",
    [PA_COMMAND_SET_SOURCE_OUTPUT_MUTE] = "SET_SOURCE_OUTPUT_MUTE",

    /* Supported since protocol v27 (3.0) */
    [PA_COMMAND_SET_PORT_LATENCY_OFFSET] = "SET_PORT_LATENCY_OFFSET",

    /* Supported since protocol v30 (6.0) */
    /* BOTH DIRECTIONS */
    [PA_COMMAND_ENABLE_SRBCHANNEL] = "ENABLE_SRBCHANNEL",
    [PA_COMMAND_DISABLE_SRBCHANNEL] = "DISABLE_SRBCHANNEL",

    /* Supported since protocol v31 (9.0) */
    /* BOTH DIRECTIONS */
    [PA_COMMAND_REGISTER_MEMFD_SHMID] = "REGISTER_MEMFD_SHMID",
};

#endif

PA_STATIC_FLIST_DECLARE(reply_infos, 0, pa_xfree);

struct reply_info {
    pa_pdispatch *pdispatch;
    PA_LLIST_FIELDS(struct reply_info);
    pa_pdispatch_cb_t callback;
    void *userdata;
    pa_free_cb_t free_cb;
    uint32_t tag;
    pa_time_event *time_event;
};

struct pa_pdispatch {
    PA_REFCNT_DECLARE;
    pa_mainloop_api *mainloop;
    const pa_pdispatch_cb_t *callback_table;
    unsigned n_commands;
    PA_LLIST_HEAD(struct reply_info, replies);
    pa_pdispatch_drain_cb_t drain_callback;
    void *drain_userdata;
    pa_cmsg_ancil_data *ancil_data;
    bool use_rtclock;
};

static void reply_info_free(struct reply_info *r) {
    pa_assert(r);
    pa_assert(r->pdispatch);
    pa_assert(r->pdispatch->mainloop);

    if (r->time_event)
        r->pdispatch->mainloop->time_free(r->time_event);

    PA_LLIST_REMOVE(struct reply_info, r->pdispatch->replies, r);

    if (pa_flist_push(PA_STATIC_FLIST_GET(reply_infos), r) < 0)
        pa_xfree(r);
}

pa_pdispatch* pa_pdispatch_new(pa_mainloop_api *mainloop, bool use_rtclock, const pa_pdispatch_cb_t *table, unsigned entries) {
    pa_pdispatch *pd;

    pa_assert(mainloop);
    pa_assert((entries && table) || (!entries && !table));

    pd = pa_xnew0(pa_pdispatch, 1);
    PA_REFCNT_INIT(pd);
    pd->mainloop = mainloop;
    pd->callback_table = table;
    pd->n_commands = entries;
    PA_LLIST_HEAD_INIT(struct reply_info, pd->replies);
    pd->use_rtclock = use_rtclock;

    return pd;
}

static void pdispatch_free(pa_pdispatch *pd) {
    pa_assert(pd);

    while (pd->replies) {
        if (pd->replies->free_cb)
            pd->replies->free_cb(pd->replies->userdata);

        reply_info_free(pd->replies);
    }

    pa_xfree(pd);
}

static void run_action(pa_pdispatch *pd, struct reply_info *r, uint32_t command, pa_tagstruct *ts) {
    pa_pdispatch_cb_t callback;
    void *userdata;
    uint32_t tag;
    pa_assert(r);

    pa_pdispatch_ref(pd);

    callback = r->callback;
    userdata = r->userdata;
    tag = r->tag;

    reply_info_free(r);

    callback(pd, command, tag, ts, userdata);

    if (pd->drain_callback && !pa_pdispatch_is_pending(pd))
        pd->drain_callback(pd, pd->drain_userdata);

    pa_pdispatch_unref(pd);
}

int pa_pdispatch_run(pa_pdispatch *pd, pa_packet *packet, pa_cmsg_ancil_data *ancil_data, void *userdata) {
    uint32_t tag, command;
    pa_tagstruct *ts = NULL;
    int ret = -1;
    const void *pdata;
    size_t plen;

    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);
    pa_assert(packet);

    pa_pdispatch_ref(pd);

    pdata = pa_packet_data(packet, &plen);
    if (plen <= 8)
        goto finish;

    ts = pa_tagstruct_new_fixed(pdata, plen);

    if (pa_tagstruct_getu32(ts, &command) < 0 ||
        pa_tagstruct_getu32(ts, &tag) < 0)
        goto finish;

#ifdef DEBUG_OPCODES
{
    char t[256];
    char const *p = NULL;

    if (command >= PA_COMMAND_MAX || !(p = command_names[command]))
        pa_snprintf((char*) (p = t), sizeof(t), "%u", command);

    pa_log("[%p] Received opcode <%s>", pd, p);
}
#endif

    pd->ancil_data = ancil_data;

    if (command == PA_COMMAND_ERROR || command == PA_COMMAND_REPLY) {
        struct reply_info *r;

        PA_LLIST_FOREACH(r, pd->replies)
            if (r->tag == tag)
                break;

        if (r)
            run_action(pd, r, command, ts);

    } else if (pd->callback_table && (command < pd->n_commands) && pd->callback_table[command]) {
        const pa_pdispatch_cb_t *cb = pd->callback_table+command;

        (*cb)(pd, command, tag, ts, userdata);
    } else {
        pa_log("Received unsupported command %u", command);
        goto finish;
    }

    ret = 0;

finish:
    pd->ancil_data = NULL;

    if (ts)
        pa_tagstruct_free(ts);

    pa_pdispatch_unref(pd);

    return ret;
}

static void timeout_callback(pa_mainloop_api*m, pa_time_event*e, const struct timeval *t, void *userdata) {
    struct reply_info*r = userdata;

    pa_assert(r);
    pa_assert(r->time_event == e);
    pa_assert(r->pdispatch);
    pa_assert(r->pdispatch->mainloop == m);
    pa_assert(r->callback);

    run_action(r->pdispatch, r, PA_COMMAND_TIMEOUT, NULL);
}

void pa_pdispatch_register_reply(pa_pdispatch *pd, uint32_t tag, int timeout, pa_pdispatch_cb_t cb, void *userdata, pa_free_cb_t free_cb) {
    struct reply_info *r;
    struct timeval tv;

    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);
    pa_assert(cb);

    if (!(r = pa_flist_pop(PA_STATIC_FLIST_GET(reply_infos))))
        r = pa_xnew(struct reply_info, 1);

    r->pdispatch = pd;
    r->callback = cb;
    r->userdata = userdata;
    r->free_cb = free_cb;
    r->tag = tag;

    pa_assert_se(r->time_event = pd->mainloop->time_new(pd->mainloop,
                                                        pa_timeval_rtstore(&tv, pa_rtclock_now() + timeout * PA_USEC_PER_SEC, pd->use_rtclock),
                                                        timeout_callback, r));

    PA_LLIST_PREPEND(struct reply_info, pd->replies, r);
}

int pa_pdispatch_is_pending(pa_pdispatch *pd) {
    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    return !!pd->replies;
}

void pa_pdispatch_set_drain_callback(pa_pdispatch *pd, pa_pdispatch_drain_cb_t cb, void *userdata) {
    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);
    pa_assert(!cb || pa_pdispatch_is_pending(pd));

    pd->drain_callback = cb;
    pd->drain_userdata = userdata;
}

void pa_pdispatch_unregister_reply(pa_pdispatch *pd, void *userdata) {
    struct reply_info *r, *n;

    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    PA_LLIST_FOREACH_SAFE(r, n, pd->replies)
        if (r->userdata == userdata)
            reply_info_free(r);
}

void pa_pdispatch_unref(pa_pdispatch *pd) {
    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    if (PA_REFCNT_DEC(pd) <= 0)
        pdispatch_free(pd);
}

pa_pdispatch* pa_pdispatch_ref(pa_pdispatch *pd) {
    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    PA_REFCNT_INC(pd);
    return pd;
}

#ifdef HAVE_CREDS

const pa_creds * pa_pdispatch_creds(pa_pdispatch *pd) {
    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    if (pd->ancil_data && pd->ancil_data->creds_valid)
         return &pd->ancil_data->creds;
    return NULL;
}

/* Should be called only once during the dispatcher lifetime
 *
 * If the returned ancillary data contains any fds, caller maintains sole
 * responsibility of closing them down using pa_cmsg_ancil_data_close_fds() */
pa_cmsg_ancil_data *pa_pdispatch_take_ancil_data(pa_pdispatch *pd) {
    pa_cmsg_ancil_data *ancil;

    pa_assert(pd);
    pa_assert(PA_REFCNT_VALUE(pd) >= 1);

    ancil = pd->ancil_data;

    /* iochannel guarantees us that nfd will always be capped */
    if (ancil)
        pa_assert(ancil->nfd <= MAX_ANCIL_DATA_FDS);

    pd->ancil_data = NULL;
    return ancil;
}

#endif
