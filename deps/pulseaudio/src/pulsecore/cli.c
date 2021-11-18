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
#include <stdlib.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/ioline.h>
#include <pulsecore/module.h>
#include <pulsecore/client.h>
#include <pulsecore/tokenizer.h>
#include <pulsecore/strbuf.h>
#include <pulsecore/cli-text.h>
#include <pulsecore/cli-command.h>
#include <pulsecore/log.h>
#include <pulsecore/macro.h>

#include "cli.h"

#define PROMPT ">>> "

struct pa_cli {
    pa_core *core;
    pa_ioline *line;

    pa_cli_eof_cb_t eof_callback;
    void *userdata;

    pa_client *client;

    bool fail, kill_requested;
    int defer_kill;

    bool interactive;
    char *last_line;
};

static void line_callback(pa_ioline *line, const char *s, void *userdata);
static void client_kill(pa_client *c);

pa_cli* pa_cli_new(pa_core *core, pa_iochannel *io, pa_module *m) {
    char cname[256];
    pa_cli *c;
    pa_client_new_data data;
    pa_client *client;

    pa_assert(io);

    pa_iochannel_socket_peer_to_string(io, cname, sizeof(cname));

    pa_client_new_data_init(&data);
    data.driver = __FILE__;
    data.module = m;
    pa_proplist_sets(data.proplist, PA_PROP_APPLICATION_NAME, cname);
    client = pa_client_new(core, &data);
    pa_client_new_data_done(&data);

    if (!client)
        return NULL;

    c = pa_xnew0(pa_cli, 1);
    c->core = core;
    c->client = client;
    pa_assert_se(c->line = pa_ioline_new(io));

    c->client->kill = client_kill;
    c->client->userdata = c;

    pa_ioline_set_callback(c->line, line_callback, c);

    return c;
}

void pa_cli_free(pa_cli *c) {
    pa_assert(c);

    pa_ioline_close(c->line);
    pa_ioline_unref(c->line);
    pa_client_free(c->client);
    pa_xfree(c->last_line);
    pa_xfree(c);
}

static void client_kill(pa_client *client) {
    pa_cli *c;

    pa_assert(client);
    pa_assert_se(c = client->userdata);

    pa_log_debug("CLI client killed.");

    if (c->defer_kill)
        c->kill_requested = true;
    else if (c->eof_callback)
        c->eof_callback(c, c->userdata);
}

static void line_callback(pa_ioline *line, const char *s, void *userdata) {
    pa_strbuf *buf;
    pa_cli *c = userdata;
    char *p;

    pa_assert(line);
    pa_assert(c);

    if (!s) {
        pa_log_debug("CLI got EOF from user.");

        if (c->eof_callback)
            c->eof_callback(c, c->userdata);

        return;
    }

    /* Magic command, like they had in AT Hayes Modems! Those were the good days! */
    if (pa_streq(s, "/"))
        s = c->last_line;
    else if (s[0]) {
        pa_xfree(c->last_line);
        c->last_line = pa_xstrdup(s);
    }

    pa_assert_se(buf = pa_strbuf_new());
    c->defer_kill++;
    if (pa_streq(s, "hello")) {
        pa_strbuf_printf(buf, "Welcome to PulseAudio %s! "
            "Use \"help\" for usage information.\n", PACKAGE_VERSION);
        c->interactive = true;
    }
    else
        pa_cli_command_execute_line(c->core, s, buf, &c->fail);
    c->defer_kill--;
    pa_ioline_puts(line, p = pa_strbuf_to_string_free(buf));
    pa_xfree(p);

    if (c->kill_requested) {
        if (c->eof_callback)
            c->eof_callback(c, c->userdata);
    } else if (c->interactive)
        pa_ioline_puts(line, PROMPT);
}

void pa_cli_set_eof_callback(pa_cli *c, pa_cli_eof_cb_t cb, void *userdata) {
    pa_assert(c);

    c->eof_callback = cb;
    c->userdata = userdata;
}

pa_module *pa_cli_get_module(pa_cli *c) {
    pa_assert(c);

    return c->client->module;
}
