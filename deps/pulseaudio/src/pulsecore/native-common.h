#ifndef foonativecommonhfoo
#define foonativecommonhfoo

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

#include <pulse/cdecl.h>
#include <pulse/def.h>

#include <pulsecore/pdispatch.h>
#include <pulsecore/pstream.h>
#include <pulsecore/tagstruct.h>

PA_C_DECL_BEGIN

enum {
    /* Generic commands */
    PA_COMMAND_ERROR,
    PA_COMMAND_TIMEOUT, /* pseudo command */
    PA_COMMAND_REPLY,

    /* CLIENT->SERVER */
    PA_COMMAND_CREATE_PLAYBACK_STREAM,        /* Payload changed in v9, v12 (0.9.0, 0.9.8) */
    PA_COMMAND_DELETE_PLAYBACK_STREAM,
    PA_COMMAND_CREATE_RECORD_STREAM,          /* Payload changed in v9, v12 (0.9.0, 0.9.8) */
    PA_COMMAND_DELETE_RECORD_STREAM,
    PA_COMMAND_EXIT,
    PA_COMMAND_AUTH,
    PA_COMMAND_SET_CLIENT_NAME,
    PA_COMMAND_LOOKUP_SINK,
    PA_COMMAND_LOOKUP_SOURCE,
    PA_COMMAND_DRAIN_PLAYBACK_STREAM,
    PA_COMMAND_STAT,
    PA_COMMAND_GET_PLAYBACK_LATENCY,
    PA_COMMAND_CREATE_UPLOAD_STREAM,
    PA_COMMAND_DELETE_UPLOAD_STREAM,
    PA_COMMAND_FINISH_UPLOAD_STREAM,
    PA_COMMAND_PLAY_SAMPLE,
    PA_COMMAND_REMOVE_SAMPLE,

    PA_COMMAND_GET_SERVER_INFO,
    PA_COMMAND_GET_SINK_INFO,
    PA_COMMAND_GET_SINK_INFO_LIST,
    PA_COMMAND_GET_SOURCE_INFO,
    PA_COMMAND_GET_SOURCE_INFO_LIST,
    PA_COMMAND_GET_MODULE_INFO,
    PA_COMMAND_GET_MODULE_INFO_LIST,
    PA_COMMAND_GET_CLIENT_INFO,
    PA_COMMAND_GET_CLIENT_INFO_LIST,
    PA_COMMAND_GET_SINK_INPUT_INFO,          /* Payload changed in v11 (0.9.7) */
    PA_COMMAND_GET_SINK_INPUT_INFO_LIST,     /* Payload changed in v11 (0.9.7) */
    PA_COMMAND_GET_SOURCE_OUTPUT_INFO,
    PA_COMMAND_GET_SOURCE_OUTPUT_INFO_LIST,
    PA_COMMAND_GET_SAMPLE_INFO,
    PA_COMMAND_GET_SAMPLE_INFO_LIST,
    PA_COMMAND_SUBSCRIBE,

    PA_COMMAND_SET_SINK_VOLUME,
    PA_COMMAND_SET_SINK_INPUT_VOLUME,
    PA_COMMAND_SET_SOURCE_VOLUME,

    PA_COMMAND_SET_SINK_MUTE,
    PA_COMMAND_SET_SOURCE_MUTE,

    PA_COMMAND_CORK_PLAYBACK_STREAM,
    PA_COMMAND_FLUSH_PLAYBACK_STREAM,
    PA_COMMAND_TRIGGER_PLAYBACK_STREAM,

    PA_COMMAND_SET_DEFAULT_SINK,
    PA_COMMAND_SET_DEFAULT_SOURCE,

    PA_COMMAND_SET_PLAYBACK_STREAM_NAME,
    PA_COMMAND_SET_RECORD_STREAM_NAME,

    PA_COMMAND_KILL_CLIENT,
    PA_COMMAND_KILL_SINK_INPUT,
    PA_COMMAND_KILL_SOURCE_OUTPUT,

    PA_COMMAND_LOAD_MODULE,
    PA_COMMAND_UNLOAD_MODULE,

    /* Obsolete */
    PA_COMMAND_ADD_AUTOLOAD___OBSOLETE,
    PA_COMMAND_REMOVE_AUTOLOAD___OBSOLETE,
    PA_COMMAND_GET_AUTOLOAD_INFO___OBSOLETE,
    PA_COMMAND_GET_AUTOLOAD_INFO_LIST___OBSOLETE,

    PA_COMMAND_GET_RECORD_LATENCY,
    PA_COMMAND_CORK_RECORD_STREAM,
    PA_COMMAND_FLUSH_RECORD_STREAM,
    PA_COMMAND_PREBUF_PLAYBACK_STREAM,

    /* SERVER->CLIENT */
    PA_COMMAND_REQUEST,
    PA_COMMAND_OVERFLOW,
    PA_COMMAND_UNDERFLOW,
    PA_COMMAND_PLAYBACK_STREAM_KILLED,
    PA_COMMAND_RECORD_STREAM_KILLED,
    PA_COMMAND_SUBSCRIBE_EVENT,

    /* A few more client->server commands */

    /* Supported since protocol v10 (0.9.5) */
    PA_COMMAND_MOVE_SINK_INPUT,
    PA_COMMAND_MOVE_SOURCE_OUTPUT,

    /* Supported since protocol v11 (0.9.7) */
    PA_COMMAND_SET_SINK_INPUT_MUTE,

    PA_COMMAND_SUSPEND_SINK,
    PA_COMMAND_SUSPEND_SOURCE,

    /* Supported since protocol v12 (0.9.8) */
    PA_COMMAND_SET_PLAYBACK_STREAM_BUFFER_ATTR,
    PA_COMMAND_SET_RECORD_STREAM_BUFFER_ATTR,

    PA_COMMAND_UPDATE_PLAYBACK_STREAM_SAMPLE_RATE,
    PA_COMMAND_UPDATE_RECORD_STREAM_SAMPLE_RATE,

    /* SERVER->CLIENT */
    PA_COMMAND_PLAYBACK_STREAM_SUSPENDED,
    PA_COMMAND_RECORD_STREAM_SUSPENDED,
    PA_COMMAND_PLAYBACK_STREAM_MOVED,
    PA_COMMAND_RECORD_STREAM_MOVED,

    /* Supported since protocol v13 (0.9.11) */
    PA_COMMAND_UPDATE_RECORD_STREAM_PROPLIST,
    PA_COMMAND_UPDATE_PLAYBACK_STREAM_PROPLIST,
    PA_COMMAND_UPDATE_CLIENT_PROPLIST,
    PA_COMMAND_REMOVE_RECORD_STREAM_PROPLIST,
    PA_COMMAND_REMOVE_PLAYBACK_STREAM_PROPLIST,
    PA_COMMAND_REMOVE_CLIENT_PROPLIST,

    /* SERVER->CLIENT */
    PA_COMMAND_STARTED,

    /* Supported since protocol v14 (0.9.12) */
    PA_COMMAND_EXTENSION,

    /* Supported since protocol v15 (0.9.15) */
    PA_COMMAND_GET_CARD_INFO,
    PA_COMMAND_GET_CARD_INFO_LIST,
    PA_COMMAND_SET_CARD_PROFILE,

    PA_COMMAND_CLIENT_EVENT,
    PA_COMMAND_PLAYBACK_STREAM_EVENT,
    PA_COMMAND_RECORD_STREAM_EVENT,

    /* SERVER->CLIENT */
    PA_COMMAND_PLAYBACK_BUFFER_ATTR_CHANGED,
    PA_COMMAND_RECORD_BUFFER_ATTR_CHANGED,

    /* Supported since protocol v16 (0.9.16) */
    PA_COMMAND_SET_SINK_PORT,
    PA_COMMAND_SET_SOURCE_PORT,

    /* Supported since protocol v22 (1.0) */
    PA_COMMAND_SET_SOURCE_OUTPUT_VOLUME,
    PA_COMMAND_SET_SOURCE_OUTPUT_MUTE,

    /* Supported since protocol v27 (3.0) */
    PA_COMMAND_SET_PORT_LATENCY_OFFSET,

    /* Supported since protocol v30 (6.0) */
    /* BOTH DIRECTIONS */
    PA_COMMAND_ENABLE_SRBCHANNEL,
    PA_COMMAND_DISABLE_SRBCHANNEL,

    /* Supported since protocol v31 (9.0)
     * BOTH DIRECTIONS */
    PA_COMMAND_REGISTER_MEMFD_SHMID,

    PA_COMMAND_MAX
};

#define PA_NATIVE_COOKIE_LENGTH 256
#define PA_NATIVE_COOKIE_FILE "cookie"
#define PA_NATIVE_COOKIE_FILE_FALLBACK ".pulse-cookie"

#define PA_NATIVE_DEFAULT_PORT 4713

#define PA_NATIVE_COOKIE_PROPERTY_NAME "protocol-native-cookie"
#define PA_NATIVE_SERVER_PROPERTY_NAME "protocol-native-server"

#define PA_NATIVE_DEFAULT_UNIX_SOCKET "native"

int pa_common_command_register_memfd_shmid(pa_pstream *p, pa_pdispatch *pd, uint32_t version,
                                           uint32_t command, pa_tagstruct *t);

PA_C_DECL_END

#endif
