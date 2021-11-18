/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#include <pulse/version.h>
#include <pulse/xmalloc.h>
#include <pulse/util.h>
#include <pulse/mainloop.h>
#include <pulse/timeval.h>
#include <pulse/fork-detect.h>
#include <pulse/client-conf.h>

#include <pulsecore/core-error.h>
#include <pulsecore/i18n.h>
#include <pulsecore/native-common.h>
#include <pulsecore/pdispatch.h>
#include <pulsecore/pstream.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/socket-client.h>
#include <pulsecore/pstream-util.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/socket.h>
#include <pulsecore/creds.h>
#include <pulsecore/macro.h>
#include <pulsecore/proplist-util.h>

#include "internal.h"
#include "context.h"

void pa_command_extension(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata);
static void pa_command_enable_srbchannel(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata);
static void pa_command_disable_srbchannel(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata);
static void pa_command_register_memfd_shmid(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata);

static const pa_pdispatch_cb_t command_table[PA_COMMAND_MAX] = {
    [PA_COMMAND_REQUEST] = pa_command_request,
    [PA_COMMAND_OVERFLOW] = pa_command_overflow_or_underflow,
    [PA_COMMAND_UNDERFLOW] = pa_command_overflow_or_underflow,
    [PA_COMMAND_PLAYBACK_STREAM_KILLED] = pa_command_stream_killed,
    [PA_COMMAND_RECORD_STREAM_KILLED] = pa_command_stream_killed,
    [PA_COMMAND_PLAYBACK_STREAM_MOVED] = pa_command_stream_moved,
    [PA_COMMAND_RECORD_STREAM_MOVED] = pa_command_stream_moved,
    [PA_COMMAND_PLAYBACK_STREAM_SUSPENDED] = pa_command_stream_suspended,
    [PA_COMMAND_RECORD_STREAM_SUSPENDED] = pa_command_stream_suspended,
    [PA_COMMAND_STARTED] = pa_command_stream_started,
    [PA_COMMAND_SUBSCRIBE_EVENT] = pa_command_subscribe_event,
    [PA_COMMAND_EXTENSION] = pa_command_extension,
    [PA_COMMAND_PLAYBACK_STREAM_EVENT] = pa_command_stream_event,
    [PA_COMMAND_RECORD_STREAM_EVENT] = pa_command_stream_event,
    [PA_COMMAND_CLIENT_EVENT] = pa_command_client_event,
    [PA_COMMAND_PLAYBACK_BUFFER_ATTR_CHANGED] = pa_command_stream_buffer_attr,
    [PA_COMMAND_RECORD_BUFFER_ATTR_CHANGED] = pa_command_stream_buffer_attr,
    [PA_COMMAND_ENABLE_SRBCHANNEL] = pa_command_enable_srbchannel,
    [PA_COMMAND_DISABLE_SRBCHANNEL] = pa_command_disable_srbchannel,
    [PA_COMMAND_REGISTER_MEMFD_SHMID] = pa_command_register_memfd_shmid,
};
static void context_free(pa_context *c);

#ifdef HAVE_DBUS
static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *message, void *userdata);
#endif

pa_context *pa_context_new(pa_mainloop_api *mainloop, const char *name) {
    return pa_context_new_with_proplist(mainloop, name, NULL);
}

static void reset_callbacks(pa_context *c) {
    pa_assert(c);

    c->state_callback = NULL;
    c->state_userdata = NULL;

    c->subscribe_callback = NULL;
    c->subscribe_userdata = NULL;

    c->event_callback = NULL;
    c->event_userdata = NULL;

    c->ext_device_manager.callback = NULL;
    c->ext_device_manager.userdata = NULL;

    c->ext_device_restore.callback = NULL;
    c->ext_device_restore.userdata = NULL;

    c->ext_stream_restore.callback = NULL;
    c->ext_stream_restore.userdata = NULL;
}

pa_context *pa_context_new_with_proplist(pa_mainloop_api *mainloop, const char *name, const pa_proplist *p) {
    pa_context *c;
    pa_mem_type_t type;

    pa_assert(mainloop);

    if (pa_detect_fork())
        return NULL;

    pa_init_i18n();

    c = pa_xnew0(pa_context, 1);
    PA_REFCNT_INIT(c);

    c->error = pa_xnew0(pa_context_error, 1);
    assert(c->error);

    c->proplist = p ? pa_proplist_copy(p) : pa_proplist_new();

    if (name)
        pa_proplist_sets(c->proplist, PA_PROP_APPLICATION_NAME, name);

#ifdef HAVE_DBUS
    c->system_bus = c->session_bus = NULL;
#endif
    c->mainloop = mainloop;
    c->playback_streams = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    c->record_streams = pa_hashmap_new(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func);
    c->client_index = PA_INVALID_INDEX;
    c->use_rtclock = pa_mainloop_is_our_api(mainloop);

    PA_LLIST_HEAD_INIT(pa_stream, c->streams);
    PA_LLIST_HEAD_INIT(pa_operation, c->operations);

    c->error->error = PA_OK;
    c->state = PA_CONTEXT_UNCONNECTED;

    reset_callbacks(c);

#ifndef MSG_NOSIGNAL
#ifdef SIGPIPE
    pa_check_signal_is_blocked(SIGPIPE);
#endif
#endif

    c->conf = pa_client_conf_new();
    pa_client_conf_load(c->conf, true, true);

    c->srb_template.readfd = -1;
    c->srb_template.writefd = -1;

    c->memfd_on_local = (!c->conf->disable_memfd && pa_memfd_is_locally_supported());

    type = (c->conf->disable_shm) ? PA_MEM_TYPE_PRIVATE :
           ((!c->memfd_on_local) ?
               PA_MEM_TYPE_SHARED_POSIX : PA_MEM_TYPE_SHARED_MEMFD);

    if (!(c->mempool = pa_mempool_new(type, c->conf->shm_size, true))) {

        if (!c->conf->disable_shm) {
            pa_log_warn("Failed to allocate shared memory pool. Falling back to a normal private one.");
            c->mempool = pa_mempool_new(PA_MEM_TYPE_PRIVATE, c->conf->shm_size, true);
        }

        if (!c->mempool) {
            context_free(c);
            return NULL;
        }
    }

    return c;
}

static void context_unlink(pa_context *c) {
    pa_stream *s;

    pa_assert(c);

    s = c->streams ? pa_stream_ref(c->streams) : NULL;
    while (s) {
        pa_stream *n = s->next ? pa_stream_ref(s->next) : NULL;
        pa_stream_set_state(s, c->state == PA_CONTEXT_FAILED ? PA_STREAM_FAILED : PA_STREAM_TERMINATED);
        pa_stream_unref(s);
        s = n;
    }

    while (c->operations)
        pa_operation_cancel(c->operations);

    if (c->pdispatch) {
        pa_pdispatch_unref(c->pdispatch);
        c->pdispatch = NULL;
    }

    if (c->pstream) {
        pa_pstream_unlink(c->pstream);
        pa_pstream_unref(c->pstream);
        c->pstream = NULL;
    }

    if (c->srb_template.memblock) {
        pa_memblock_unref(c->srb_template.memblock);
        c->srb_template.memblock = NULL;
    }

    if (c->client) {
        pa_socket_client_unref(c->client);
        c->client = NULL;
    }

    reset_callbacks(c);
}

static void context_free(pa_context *c) {
    pa_assert(c);

    context_unlink(c);

#ifdef HAVE_DBUS
    if (c->system_bus) {
        if (c->filter_added)
            dbus_connection_remove_filter(pa_dbus_wrap_connection_get(c->system_bus), filter_cb, c);
        pa_dbus_wrap_connection_free(c->system_bus);
    }

    if (c->session_bus) {
        if (c->filter_added)
            dbus_connection_remove_filter(pa_dbus_wrap_connection_get(c->session_bus), filter_cb, c);
        pa_dbus_wrap_connection_free(c->session_bus);
    }
#endif

    if (c->record_streams)
        pa_hashmap_free(c->record_streams);
    if (c->playback_streams)
        pa_hashmap_free(c->playback_streams);

    if (c->mempool)
        pa_mempool_unref(c->mempool);

    if (c->conf)
        pa_client_conf_free(c->conf);

    pa_strlist_free(c->server_list);

    if (c->proplist)
        pa_proplist_free(c->proplist);

    pa_xfree(c->server);
    pa_xfree(c->error);
    pa_xfree(c);
}

pa_context* pa_context_ref(pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_REFCNT_INC(c);
    return c;
}

void pa_context_unref(pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (PA_REFCNT_DEC(c) <= 0)
        context_free(c);
}

void pa_context_set_state(pa_context *c, pa_context_state_t st) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (c->state == st)
        return;

    pa_context_ref(c);

    c->state = st;

    if (c->state_callback)
        c->state_callback(c, c->state_userdata);

    if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED)
        context_unlink(c);

    pa_context_unref(c);
}

int pa_context_set_error(const pa_context *c, int error) {
    pa_assert(error >= 0);
    pa_assert(error < PA_ERR_MAX);

    if (c)
        c->error->error = error;

    return error;
}

void pa_context_fail(pa_context *c, int error) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_context_set_error(c, error);
    pa_context_set_state(c, PA_CONTEXT_FAILED);
}

static void pstream_die_callback(pa_pstream *p, void *userdata) {
    pa_context *c = userdata;

    pa_assert(p);
    pa_assert(c);

    pa_context_fail(c, PA_ERR_CONNECTIONTERMINATED);
}

static void pstream_packet_callback(pa_pstream *p, pa_packet *packet, pa_cmsg_ancil_data *ancil_data, void *userdata) {
    pa_context *c = userdata;

    pa_assert(p);
    pa_assert(packet);
    pa_assert(c);

    pa_context_ref(c);

    if (pa_pdispatch_run(c->pdispatch, packet, ancil_data, c) < 0)
        pa_context_fail(c, PA_ERR_PROTOCOL);

    pa_context_unref(c);
}

static void handle_srbchannel_memblock(pa_context *c, pa_memblock *memblock) {
    pa_srbchannel *sr;
    pa_tagstruct *t;

    pa_assert(c);

    /* Memblock sanity check */
    if (!memblock) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    } else if (pa_memblock_is_read_only(memblock)) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    } else if (pa_memblock_is_ours(memblock)) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    /* Create the srbchannel */
    c->srb_template.memblock = memblock;
    pa_memblock_ref(memblock);
    sr = pa_srbchannel_new_from_template(c->mainloop, &c->srb_template);
    if (!sr) {
        pa_log_warn("Failed to create srbchannel from template");
        c->srb_template.readfd = -1;
        c->srb_template.writefd = -1;
        pa_memblock_unref(c->srb_template.memblock);
        c->srb_template.memblock = NULL;
        return;
    }

    /* Ack the enable command */
    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, PA_COMMAND_ENABLE_SRBCHANNEL);
    pa_tagstruct_putu32(t, c->srb_setup_tag);
    pa_pstream_send_tagstruct(c->pstream, t);

    /* ...and switch over */
    pa_pstream_set_srbchannel(c->pstream, sr);
}

static void pstream_memblock_callback(pa_pstream *p, uint32_t channel, int64_t offset, pa_seek_mode_t seek, const pa_memchunk *chunk, void *userdata) {
    pa_context *c = userdata;
    pa_stream *s;

    pa_assert(p);
    pa_assert(chunk);
    pa_assert(chunk->length > 0);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_context_ref(c);

    if (c->srb_template.readfd != -1 && c->srb_template.memblock == NULL) {
        handle_srbchannel_memblock(c, chunk->memblock);
        pa_context_unref(c);
        return;
    }

    if ((s = pa_hashmap_get(c->record_streams, PA_UINT32_TO_PTR(channel)))) {

        if (chunk->memblock) {
            pa_memblockq_seek(s->record_memblockq, offset, seek, true);
            pa_memblockq_push_align(s->record_memblockq, chunk);
        } else
            pa_memblockq_seek(s->record_memblockq, offset+chunk->length, seek, true);

        if (s->read_callback) {
            size_t l;

            if ((l = pa_memblockq_get_length(s->record_memblockq)) > 0)
                s->read_callback(s, l, s->read_userdata);
        }
    }

    pa_context_unref(c);
}

int pa_context_handle_error(pa_context *c, uint32_t command, pa_tagstruct *t, bool fail) {
    uint32_t err;
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (command == PA_COMMAND_ERROR) {
        pa_assert(t);

        if (pa_tagstruct_getu32(t, &err) < 0 ||
            !pa_tagstruct_eof(t)) {
            pa_context_fail(c, PA_ERR_PROTOCOL);
            return -1;
        }

    } else if (command == PA_COMMAND_TIMEOUT)
        err = PA_ERR_TIMEOUT;
    else {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return -1;
    }

    if (err == PA_OK) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return -1;
    }

    if (err >= PA_ERR_MAX)
        err = PA_ERR_UNKNOWN;

    if (fail) {
        pa_context_fail(c, (int) err);
        return -1;
    }

    pa_context_set_error(c, (int) err);

    return 0;
}

static void setup_complete_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;

    pa_assert(pd);
    pa_assert(c);
    pa_assert(c->state == PA_CONTEXT_AUTHORIZING || c->state == PA_CONTEXT_SETTING_NAME);

    pa_context_ref(c);

    if (command != PA_COMMAND_REPLY) {
        pa_context_handle_error(c, command, t, true);
        goto finish;
    }

    switch(c->state) {
        case PA_CONTEXT_AUTHORIZING: {
            pa_tagstruct *reply;
            bool shm_on_remote = false;
            bool memfd_on_remote = false;

            if (pa_tagstruct_getu32(t, &c->version) < 0 ||
                !pa_tagstruct_eof(t)) {
                pa_context_fail(c, PA_ERR_PROTOCOL);
                goto finish;
            }

            /* Minimum supported version */
            if (c->version < 8) {
                pa_context_fail(c, PA_ERR_VERSION);
                goto finish;
            }

            /* Starting with protocol version 13 the MSB of the version
               tag reflects if shm is available for this connection or
               not. */
            if ((c->version & PA_PROTOCOL_VERSION_MASK) >= 13) {
                shm_on_remote = !!(c->version & PA_PROTOCOL_FLAG_SHM);

                /* Starting with protocol version 31, the second MSB of the version
                 * tag reflects whether memfd is supported on the other PA end. */
                if ((c->version & PA_PROTOCOL_VERSION_MASK) >= 31)
                    memfd_on_remote = !!(c->version & PA_PROTOCOL_FLAG_MEMFD);

                /* Reserve the two most-significant _bytes_ of the version tag
                 * for flags. */
                c->version &= PA_PROTOCOL_VERSION_MASK;
            }

            pa_log_debug("Protocol version: remote %u, local %u", c->version, PA_PROTOCOL_VERSION);

            /* Enable shared memory support if possible */
            if (c->do_shm)
                if (c->version < 10 || (c->version >= 13 && !shm_on_remote))
                    c->do_shm = false;

            if (c->do_shm) {

                /* Only enable SHM if both sides are owned by the same
                 * user. This is a security measure because otherwise
                 * data private to the user might leak. */

#ifdef HAVE_CREDS
                const pa_creds *creds;
                if (!(creds = pa_pdispatch_creds(pd)) || getuid() != creds->uid)
                    c->do_shm = false;
#endif
            }

            pa_log_debug("Negotiated SHM: %s", pa_yes_no(c->do_shm));
            pa_pstream_enable_shm(c->pstream, c->do_shm);

            c->shm_type = PA_MEM_TYPE_PRIVATE;
            if (c->do_shm) {
                if (c->version >= 31 && memfd_on_remote && c->memfd_on_local) {
                    const char *reason;

                    pa_pstream_enable_memfd(c->pstream);
                    if (pa_mempool_is_memfd_backed(c->mempool))
                        if (pa_pstream_register_memfd_mempool(c->pstream, c->mempool, &reason))
                            pa_log("Failed to regester memfd mempool. Reason: %s", reason);

                    /* Even if memfd pool registration fails, the negotiated SHM type
                     * shall remain memfd as both endpoints claim to support it. */
                    c->shm_type = PA_MEM_TYPE_SHARED_MEMFD;
                } else
                    c->shm_type = PA_MEM_TYPE_SHARED_POSIX;
            }

            pa_log_debug("Memfd possible: %s", pa_yes_no(c->memfd_on_local));
            pa_log_debug("Negotiated SHM type: %s", pa_mem_type_to_string(c->shm_type));

            reply = pa_tagstruct_command(c, PA_COMMAND_SET_CLIENT_NAME, &tag);

            if (c->version >= 13) {
                pa_init_proplist(c->proplist);
                pa_tagstruct_put_proplist(reply, c->proplist);
            } else
                pa_tagstruct_puts(reply, pa_proplist_gets(c->proplist, PA_PROP_APPLICATION_NAME));

            pa_pstream_send_tagstruct(c->pstream, reply);
            pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, setup_complete_callback, c, NULL);

            pa_context_set_state(c, PA_CONTEXT_SETTING_NAME);
            break;
        }

        case PA_CONTEXT_SETTING_NAME :

            if ((c->version >= 13 && (pa_tagstruct_getu32(t, &c->client_index) < 0 ||
                                      c->client_index == PA_INVALID_INDEX)) ||
                !pa_tagstruct_eof(t)) {
                pa_context_fail(c, PA_ERR_PROTOCOL);
                goto finish;
            }

            pa_context_set_state(c, PA_CONTEXT_READY);
            break;

        default:
            pa_assert_not_reached();
    }

finish:
    pa_context_unref(c);
}

static void setup_context(pa_context *c, pa_iochannel *io) {
    uint8_t cookie[PA_NATIVE_COOKIE_LENGTH];
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(io);

    pa_context_ref(c);

    pa_assert(!c->pstream);
    c->pstream = pa_pstream_new(c->mainloop, io, c->mempool);

    pa_pstream_set_die_callback(c->pstream, pstream_die_callback, c);
    pa_pstream_set_receive_packet_callback(c->pstream, pstream_packet_callback, c);
    pa_pstream_set_receive_memblock_callback(c->pstream, pstream_memblock_callback, c);

    pa_assert(!c->pdispatch);
    c->pdispatch = pa_pdispatch_new(c->mainloop, c->use_rtclock, command_table, PA_COMMAND_MAX);

    if (pa_client_conf_load_cookie(c->conf, cookie, sizeof(cookie)) < 0)
        pa_log_info("No cookie loaded. Attempting to connect without.");

    t = pa_tagstruct_command(c, PA_COMMAND_AUTH, &tag);

    c->do_shm =
        pa_mempool_is_shared(c->mempool) &&
        c->is_local;

    pa_log_debug("SHM possible: %s", pa_yes_no(c->do_shm));

    /* Starting with protocol version 13 we use the MSB of the version
     * tag for informing the other side if we could do SHM or not.
     * Starting from version 31, second MSB is used to flag memfd support. */
    pa_tagstruct_putu32(t, PA_PROTOCOL_VERSION | (c->do_shm ? PA_PROTOCOL_FLAG_SHM : 0) |
                        (c->memfd_on_local ? PA_PROTOCOL_FLAG_MEMFD: 0));
    pa_tagstruct_put_arbitrary(t, cookie, sizeof(cookie));

#ifdef HAVE_CREDS
{
    pa_creds ucred;

    if (pa_iochannel_creds_supported(io))
        pa_iochannel_creds_enable(io);

    ucred.uid = getuid();
    ucred.gid = getgid();

    pa_pstream_send_tagstruct_with_creds(c->pstream, t, &ucred);
}
#else
    pa_pstream_send_tagstruct(c->pstream, t);
#endif

    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, setup_complete_callback, c, NULL);

    pa_context_set_state(c, PA_CONTEXT_AUTHORIZING);

    pa_context_unref(c);
}

static pa_strlist *prepend_per_user(pa_strlist *l) {
    char *ufn;

    /* The per-user instance */
    if ((ufn = pa_runtime_path(PA_NATIVE_DEFAULT_UNIX_SOCKET))) {
        l = pa_strlist_prepend(l, ufn);
        pa_xfree(ufn);
    }

    return l;
}

#ifndef OS_IS_WIN32

static int context_autospawn(pa_context *c) {
    pid_t pid;
    int status, r;
    struct sigaction sa;

    pa_context_ref(c);

    if (sigaction(SIGCHLD, NULL, &sa) < 0) {
        pa_log_debug("sigaction() failed: %s", pa_cstrerror(errno));
        pa_context_fail(c, PA_ERR_INTERNAL);
        goto fail;
    }

#ifdef SA_NOCLDWAIT
    if ((sa.sa_flags & SA_NOCLDWAIT) || sa.sa_handler == SIG_IGN) {
#else
    if (sa.sa_handler == SIG_IGN) {
#endif
        pa_log_debug("Process disabled waitpid(), cannot autospawn.");
        pa_context_fail(c, PA_ERR_CONNECTIONREFUSED);
        goto fail;
    }

    pa_log_debug("Trying to autospawn...");

    if (c->spawn_api.prefork)
        c->spawn_api.prefork();

    if ((pid = fork()) < 0) {
        pa_log_error(_("fork(): %s"), pa_cstrerror(errno));
        pa_context_fail(c, PA_ERR_INTERNAL);

        if (c->spawn_api.postfork)
            c->spawn_api.postfork();

        goto fail;
    } else if (!pid) {
        /* Child */

        const char *state = NULL;
        const char * argv[32];
        unsigned n = 0;

        if (c->spawn_api.atfork)
            c->spawn_api.atfork();

        /* We leave most of the cleaning up of the process environment
         * to the executable. We only clean up the file descriptors to
         * make sure the executable can actually be loaded
         * correctly. */
        pa_close_all(-1);

        /* Setup argv */
        argv[n++] = c->conf->daemon_binary;
        argv[n++] = "--start";

        while (n < PA_ELEMENTSOF(argv)-1) {
            char *a;

            if (!(a = pa_split_spaces(c->conf->extra_arguments, &state)))
                break;

            argv[n++] = a;
        }

        argv[n++] = NULL;
        pa_assert(n <= PA_ELEMENTSOF(argv));

        execv(argv[0], (char * const *) argv);
        _exit(1);
    }

    /* Parent */

    if (c->spawn_api.postfork)
        c->spawn_api.postfork();

    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR);

    if (r < 0) {

        if (errno != ECHILD) {
            pa_log(_("waitpid(): %s"), pa_cstrerror(errno));
            pa_context_fail(c, PA_ERR_INTERNAL);
            goto fail;
        }

        /* hmm, something already reaped our child, so we assume
         * startup worked, even if we cannot know */

    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        pa_context_fail(c, PA_ERR_CONNECTIONREFUSED);
        goto fail;
    }

    pa_context_unref(c);

    return 0;

fail:

    pa_context_unref(c);

    return -1;
}

#endif /* OS_IS_WIN32 */

static void on_connection(pa_socket_client *client, pa_iochannel*io, void *userdata);

#ifdef HAVE_DBUS
static void track_pulseaudio_on_dbus(pa_context *c, DBusBusType type, pa_dbus_wrap_connection **conn) {
    DBusError error;

    pa_assert(c);
    pa_assert(conn);

    dbus_error_init(&error);

    if (!(*conn = pa_dbus_wrap_connection_new(c->mainloop, c->use_rtclock, type, &error)) || dbus_error_is_set(&error)) {
        pa_log_warn("Unable to contact DBUS: %s: %s", error.name, error.message);
        goto fail;
    }

    if (!dbus_connection_add_filter(pa_dbus_wrap_connection_get(*conn), filter_cb, c, NULL)) {
        pa_log_warn("Failed to add filter function");
        goto fail;
    }
    c->filter_added = true;

    if (pa_dbus_add_matches(
                pa_dbus_wrap_connection_get(*conn), &error,
                "type='signal',sender='" DBUS_SERVICE_DBUS "',interface='" DBUS_INTERFACE_DBUS "',member='NameOwnerChanged',arg0='org.pulseaudio.Server',arg1=''", NULL) < 0) {

        pa_log_warn("Unable to track org.pulseaudio.Server: %s: %s", error.name, error.message);
        goto fail;
    }

    return;

fail:
    if (*conn) {
        pa_dbus_wrap_connection_free(*conn);
        *conn = NULL;
    }

    dbus_error_free(&error);
}
#endif

static int try_next_connection(pa_context *c) {
    char *u = NULL;
    int r = -1;

    pa_assert(c);
    pa_assert(!c->client);

    for (;;) {
        pa_xfree(u);
        u = NULL;

        c->server_list = pa_strlist_pop(c->server_list, &u);

        if (!u) {

#ifndef OS_IS_WIN32
            if (c->do_autospawn) {

                if ((r = context_autospawn(c)) < 0)
                    goto finish;

                /* Autospawn only once */
                c->do_autospawn = false;

                /* Connect only to per-user sockets this time */
                c->server_list = prepend_per_user(c->server_list);

                /* Retry connection */
                continue;
            }
#endif

#ifdef HAVE_DBUS
            if (c->no_fail && !c->server_specified) {
                if (!c->session_bus)
                    track_pulseaudio_on_dbus(c, DBUS_BUS_SESSION, &c->session_bus);
                if (!c->system_bus)
                    track_pulseaudio_on_dbus(c, DBUS_BUS_SYSTEM, &c->system_bus);

                if (c->session_bus || c->system_bus) {
                    pa_log_debug("Waiting for PA on D-Bus...");
                    break;
                }
            } else
#endif
                pa_context_fail(c, PA_ERR_CONNECTIONREFUSED);

            goto finish;
        }

        pa_log_debug("Trying to connect to %s...", u);

        pa_xfree(c->server);
        c->server = pa_xstrdup(u);

        if (!(c->client = pa_socket_client_new_string(c->mainloop, c->use_rtclock, u, PA_NATIVE_DEFAULT_PORT)))
            continue;

        c->is_local = pa_socket_client_is_local(c->client);
        pa_socket_client_set_callback(c->client, on_connection, c);
        break;
    }

    r = 0;

finish:
    pa_xfree(u);

    return r;
}

static void on_connection(pa_socket_client *client, pa_iochannel*io, void *userdata) {
    pa_context *c = userdata;
    int saved_errno = errno;

    pa_assert(client);
    pa_assert(c);
    pa_assert(c->state == PA_CONTEXT_CONNECTING);

    pa_context_ref(c);

    pa_socket_client_unref(client);
    c->client = NULL;

    if (!io) {
        /* Try the next item in the list */
        if (saved_errno == ECONNREFUSED ||
            saved_errno == ETIMEDOUT ||
            saved_errno == EHOSTUNREACH) {
            try_next_connection(c);
            goto finish;
        }

        pa_context_fail(c, PA_ERR_CONNECTIONREFUSED);
        goto finish;
    }

    setup_context(c, io);

finish:
    pa_context_unref(c);
}

#ifdef HAVE_DBUS
static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *message, void *userdata) {
    pa_context *c = userdata;
    bool is_session;

    pa_assert(bus);
    pa_assert(message);
    pa_assert(c);

    if (c->state != PA_CONTEXT_CONNECTING)
        goto finish;

    if (!c->no_fail)
        goto finish;

    /* FIXME: We probably should check if this is actually the NameOwnerChanged we were looking for */

    is_session = c->session_bus && bus == pa_dbus_wrap_connection_get(c->session_bus);
    pa_log_debug("Rock!! PulseAudio might be back on %s bus", is_session ? "session" : "system");

    if (is_session)
        /* The user instance via PF_LOCAL */
        c->server_list = prepend_per_user(c->server_list);
    else
        /* The system wide instance via PF_LOCAL */
        c->server_list = pa_strlist_prepend(c->server_list, PA_SYSTEM_RUNTIME_PATH PA_PATH_SEP PA_NATIVE_DEFAULT_UNIX_SOCKET);

    if (!c->client)
        try_next_connection(c);

finish:
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}
#endif

int pa_context_connect(
        pa_context *c,
        const char *server,
        pa_context_flags_t flags,
        const pa_spawn_api *api) {

    int r = -1;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY(c, c->state == PA_CONTEXT_UNCONNECTED, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY(c, !(flags & ~(PA_CONTEXT_NOAUTOSPAWN|PA_CONTEXT_NOFAIL)), PA_ERR_INVALID);
    PA_CHECK_VALIDITY(c, !server || *server, PA_ERR_INVALID);

    if (server)
        c->conf->autospawn = false;
    else
        server = c->conf->default_server;

    pa_context_ref(c);

    c->no_fail = !!(flags & PA_CONTEXT_NOFAIL);
    c->server_specified = !!server;
    pa_assert(!c->server_list);

    if (server) {
        if (!(c->server_list = pa_strlist_parse(server))) {
            pa_context_fail(c, PA_ERR_INVALIDSERVER);
            goto finish;
        }

    } else {
        char *d;

        /* Prepend in reverse order */

        /* Follow the X display */
        if (c->conf->auto_connect_display) {
            if ((d = getenv("DISPLAY"))) {
                d = pa_xstrndup(d, strcspn(d, ":"));

                if (*d)
                    c->server_list = pa_strlist_prepend(c->server_list, d);

                pa_xfree(d);
            }
        }

        /* Add TCP/IP on the localhost */
        if (c->conf->auto_connect_localhost) {
            c->server_list = pa_strlist_prepend(c->server_list, "tcp6:[::1]");
            c->server_list = pa_strlist_prepend(c->server_list, "tcp4:127.0.0.1");
        }

        /* The system wide instance via PF_LOCAL */
        c->server_list = pa_strlist_prepend(c->server_list, PA_SYSTEM_RUNTIME_PATH PA_PATH_SEP PA_NATIVE_DEFAULT_UNIX_SOCKET);

        /* The user instance via PF_LOCAL */
        c->server_list = prepend_per_user(c->server_list);
    }

    /* Set up autospawning */
    if (!(flags & PA_CONTEXT_NOAUTOSPAWN) && c->conf->autospawn) {

#ifdef HAVE_GETUID
        if (getuid() == 0)
            pa_log_debug("Not doing autospawn since we are root.");
        else {
            c->do_autospawn = true;

            if (api)
                c->spawn_api = *api;
        }
#endif
    }

    pa_context_set_state(c, PA_CONTEXT_CONNECTING);
    r = try_next_connection(c);

finish:
    pa_context_unref(c);

    return r;
}

void pa_context_disconnect(pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    if (PA_CONTEXT_IS_GOOD(c->state))
        pa_context_set_state(c, PA_CONTEXT_TERMINATED);
}

pa_context_state_t pa_context_get_state(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    return c->state;
}

int pa_context_errno(const pa_context *c) {

    if (!c)
        return PA_ERR_INVALID;

    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    return c->error->error;
}

void pa_context_set_state_callback(pa_context *c, pa_context_notify_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    if (c->state == PA_CONTEXT_TERMINATED || c->state == PA_CONTEXT_FAILED)
        return;

    c->state_callback = cb;
    c->state_userdata = userdata;
}

void pa_context_set_event_callback(pa_context *c, pa_context_event_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    if (c->state == PA_CONTEXT_TERMINATED || c->state == PA_CONTEXT_FAILED)
        return;

    c->event_callback = cb;
    c->event_userdata = userdata;
}

int pa_context_is_pending(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY(c, PA_CONTEXT_IS_GOOD(c->state), PA_ERR_BADSTATE);

    return (c->pstream && pa_pstream_is_pending(c->pstream)) ||
        (c->pdispatch && pa_pdispatch_is_pending(c->pdispatch)) ||
        c->client;
}

static void set_dispatch_callbacks(pa_operation *o);

static void pdispatch_drain_callback(pa_pdispatch*pd, void *userdata) {
    set_dispatch_callbacks(userdata);
}

static void pstream_drain_callback(pa_pstream *s, void *userdata) {
    set_dispatch_callbacks(userdata);
}

static void set_dispatch_callbacks(pa_operation *o) {
    int done = 1;

    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);
    pa_assert(o->context);
    pa_assert(PA_REFCNT_VALUE(o->context) >= 1);
    pa_assert(o->context->state == PA_CONTEXT_READY);

    pa_pstream_set_drain_callback(o->context->pstream, NULL, NULL);
    pa_pdispatch_set_drain_callback(o->context->pdispatch, NULL, NULL);

    if (pa_pdispatch_is_pending(o->context->pdispatch)) {
        pa_pdispatch_set_drain_callback(o->context->pdispatch, pdispatch_drain_callback, o);
        done = 0;
    }

    if (pa_pstream_is_pending(o->context->pstream)) {
        pa_pstream_set_drain_callback(o->context->pstream, pstream_drain_callback, o);
        done = 0;
    }

    if (done) {
        if (o->callback) {
            pa_context_notify_cb_t cb = (pa_context_notify_cb_t) o->callback;
            cb(o->context, o->userdata);
        }

        pa_operation_done(o);
        pa_operation_unref(o);
    }
}

pa_operation* pa_context_drain(pa_context *c, pa_context_notify_cb_t cb, void *userdata) {
    pa_operation *o;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, pa_context_is_pending(c), PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);
    set_dispatch_callbacks(pa_operation_ref(o));

    return o;
}

void pa_context_simple_ack_callback(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    int success = 1;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, false) < 0)
            goto finish;

        success = 0;
    } else if (!pa_tagstruct_eof(t)) {
        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_context_success_cb_t cb = (pa_context_success_cb_t) o->callback;
        cb(o->context, success, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation* pa_context_send_simple_command(pa_context *c, uint32_t command, pa_pdispatch_cb_t internal_cb, pa_operation_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, cb, userdata);

    t = pa_tagstruct_command(c, command, &tag);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, internal_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_exit_daemon(pa_context *c, pa_context_success_cb_t cb, void *userdata) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    return pa_context_send_simple_command(c, PA_COMMAND_EXIT, pa_context_simple_ack_callback, (pa_operation_cb_t) cb, userdata);
}

pa_operation* pa_context_set_default_sink(pa_context *c, const char *name, pa_context_success_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);
    t = pa_tagstruct_command(c, PA_COMMAND_SET_DEFAULT_SINK, &tag);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT,  pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation* pa_context_set_default_source(pa_context *c, const char *name, pa_context_success_cb_t cb, void *userdata) {
    pa_tagstruct *t;
    pa_operation *o;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);
    t = pa_tagstruct_command(c, PA_COMMAND_SET_DEFAULT_SOURCE, &tag);
    pa_tagstruct_puts(t, name);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT,  pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

int pa_context_is_local(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_ANY(c, !pa_detect_fork(), PA_ERR_FORKED, -1);
    PA_CHECK_VALIDITY_RETURN_ANY(c, PA_CONTEXT_IS_GOOD(c->state), PA_ERR_BADSTATE, -1);

    return c->is_local;
}

pa_operation* pa_context_set_name(pa_context *c, const char *name, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(name);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);

    if (c->version >= 13) {
        pa_proplist *p = pa_proplist_new();

        pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, name);
        o = pa_context_proplist_update(c, PA_UPDATE_REPLACE, p, cb, userdata);
        pa_proplist_free(p);
    } else {
        pa_tagstruct *t;
        uint32_t tag;

        o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);
        t = pa_tagstruct_command(c, PA_COMMAND_SET_CLIENT_NAME, &tag);
        pa_tagstruct_puts(t, name);
        pa_pstream_send_tagstruct(c->pstream, t);
        pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT,  pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);
    }

    return o;
}

const char* pa_get_library_version(void) {
    return pa_get_headers_version();
}

const char* pa_context_get_server(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->server, PA_ERR_NOENTITY);

    if (*c->server == '{') {
        char *e = strchr(c->server+1, '}');
        return e ? e+1 : c->server;
    }

    return c->server;
}

uint32_t pa_context_get_protocol_version(const pa_context *c) {
    return PA_PROTOCOL_VERSION;
}

uint32_t pa_context_get_server_protocol_version(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_ANY(c, !pa_detect_fork(), PA_ERR_FORKED, PA_INVALID_INDEX);
    PA_CHECK_VALIDITY_RETURN_ANY(c, PA_CONTEXT_IS_GOOD(c->state), PA_ERR_BADSTATE, PA_INVALID_INDEX);

    return c->version;
}

pa_tagstruct *pa_tagstruct_command(pa_context *c, uint32_t command, uint32_t *tag) {
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(tag);

    t = pa_tagstruct_new();
    pa_tagstruct_putu32(t, command);
    pa_tagstruct_putu32(t, *tag = c->ctag++);

    return t;
}

uint32_t pa_context_get_index(const pa_context *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_ANY(c, !pa_detect_fork(), PA_ERR_FORKED, PA_INVALID_INDEX);
    PA_CHECK_VALIDITY_RETURN_ANY(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE, PA_INVALID_INDEX);
    PA_CHECK_VALIDITY_RETURN_ANY(c, c->version >= 13, PA_ERR_NOTSUPPORTED, PA_INVALID_INDEX);

    return c->client_index;
}

pa_operation *pa_context_proplist_update(pa_context *c, pa_update_mode_t mode, const pa_proplist *p, pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, mode == PA_UPDATE_SET || mode == PA_UPDATE_MERGE || mode == PA_UPDATE_REPLACE, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 13, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_UPDATE_CLIENT_PROPLIST, &tag);
    pa_tagstruct_putu32(t, (uint32_t) mode);
    pa_tagstruct_put_proplist(t, p);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    /* Please note that we don't update c->proplist here, because we
     * don't export that field */

    return o;
}

pa_operation *pa_context_proplist_remove(pa_context *c, const char *const keys[], pa_context_success_cb_t cb, void *userdata) {
    pa_operation *o;
    pa_tagstruct *t;
    uint32_t tag;
    const char * const *k;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, keys && keys[0], PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 13, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_REMOVE_CLIENT_PROPLIST, &tag);

    for (k = keys; *k; k++)
        pa_tagstruct_puts(t, *k);

    pa_tagstruct_puts(t, NULL);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    /* Please note that we don't update c->proplist here, because we
     * don't export that field */

    return o;
}

void pa_command_extension(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;
    uint32_t idx;
    const char *name;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_EXTENSION);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_context_ref(c);

    if (c->version < 15) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (pa_tagstruct_getu32(t, &idx) < 0 ||
        pa_tagstruct_gets(t, &name) < 0) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (pa_streq(name, "module-device-manager"))
        pa_ext_device_manager_command(c, tag, t);
    else if (pa_streq(name, "module-device-restore"))
        pa_ext_device_restore_command(c, tag, t);
    else if (pa_streq(name, "module-stream-restore"))
        pa_ext_stream_restore_command(c, tag, t);
    else
        pa_log(_("Received message for unknown extension '%s'"), name);

finish:
    pa_context_unref(c);
}

static void pa_command_enable_srbchannel(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;

#ifdef HAVE_CREDS
    pa_cmsg_ancil_data *ancil = NULL;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_ENABLE_SRBCHANNEL);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    ancil = pa_pdispatch_take_ancil_data(pd);
    if (!ancil)
        goto fail;

    /* Currently only one srb channel is supported, might change in future versions */
    if (c->srb_template.readfd != -1)
        goto fail;

    if (ancil->nfd != 2 || ancil->fds[0] == -1 || ancil->fds[1] == -1)
        goto fail;

    pa_context_ref(c);

    c->srb_template.readfd = ancil->fds[0];
    c->srb_template.writefd = ancil->fds[1];
    c->srb_setup_tag = tag;

    pa_context_unref(c);

    ancil->close_fds_on_cleanup = false;
    return;

fail:
    if (ancil)
        pa_cmsg_ancil_data_close_fds(ancil);

    pa_context_fail(c, PA_ERR_PROTOCOL);
    return;
#else
    pa_assert(c);
    pa_context_fail(c, PA_ERR_PROTOCOL);
#endif
}

static void pa_command_disable_srbchannel(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;
    pa_tagstruct *t2;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_DISABLE_SRBCHANNEL);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_pstream_set_srbchannel(c->pstream, NULL);

    c->srb_template.readfd = -1;
    c->srb_template.writefd = -1;
    if (c->srb_template.memblock) {
        pa_memblock_unref(c->srb_template.memblock);
        c->srb_template.memblock = NULL;
    }

    /* Send disable command back again */
    t2 = pa_tagstruct_new();
    pa_tagstruct_putu32(t2, PA_COMMAND_DISABLE_SRBCHANNEL);
    pa_tagstruct_putu32(t2, tag);
    pa_pstream_send_tagstruct(c->pstream, t2);
}

static void pa_command_register_memfd_shmid(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_REGISTER_MEMFD_SHMID);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_common_command_register_memfd_shmid(c->pstream, pd, c->version, command, t))
        pa_context_fail(c, PA_ERR_PROTOCOL);
}

void pa_command_client_event(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_context *c = userdata;
    pa_proplist *pl = NULL;
    const char *event;

    pa_assert(pd);
    pa_assert(command == PA_COMMAND_CLIENT_EVENT);
    pa_assert(t);
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    pa_context_ref(c);

    if (c->version < 15) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        goto finish;
    }

    pl = pa_proplist_new();

    if (pa_tagstruct_gets(t, &event) < 0 ||
        pa_tagstruct_get_proplist(t, pl) < 0 ||
        !pa_tagstruct_eof(t) || !event) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (c->event_callback)
        c->event_callback(c, event, pl, c->event_userdata);

finish:
    pa_context_unref(c);

    if (pl)
        pa_proplist_free(pl);
}

pa_time_event* pa_context_rttime_new(const pa_context *c, pa_usec_t usec, pa_time_event_cb_t cb, void *userdata) {
    struct timeval tv;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->mainloop);

    if (usec == PA_USEC_INVALID)
        return c->mainloop->time_new(c->mainloop, NULL, cb, userdata);

    pa_timeval_rtstore(&tv, usec, c->use_rtclock);

    return c->mainloop->time_new(c->mainloop, &tv, cb, userdata);
}

void pa_context_rttime_restart(const pa_context *c, pa_time_event *e, pa_usec_t usec) {
    struct timeval tv;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->mainloop);

    if (usec == PA_USEC_INVALID)
        c->mainloop->time_restart(e, NULL);
    else {
        pa_timeval_rtstore(&tv, usec, c->use_rtclock);
        c->mainloop->time_restart(e, &tv);
    }
}

size_t pa_context_get_tile_size(const pa_context *c, const pa_sample_spec *ss) {
    size_t fs, mbs;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_ANY(c, !pa_detect_fork(), PA_ERR_FORKED, (size_t) -1);
    PA_CHECK_VALIDITY_RETURN_ANY(c, !ss || pa_sample_spec_valid(ss), PA_ERR_INVALID, (size_t) -1);

    fs = ss ? pa_frame_size(ss) : 1;
    mbs = PA_ROUND_DOWN(pa_mempool_block_size_max(c->mempool), fs);
    return PA_MAX(mbs, fs);
}

int pa_context_load_cookie_from_file(pa_context *c, const char *cookie_file_path) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY(c, c->state == PA_CONTEXT_UNCONNECTED, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY(c, !cookie_file_path || *cookie_file_path, PA_ERR_INVALID);

    pa_client_conf_set_cookie_file_from_application(c->conf, cookie_file_path);

    return 0;
}
