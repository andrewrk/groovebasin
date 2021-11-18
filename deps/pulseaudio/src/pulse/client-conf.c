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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pulse/xmalloc.h>

#include <pulsecore/i18n.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-error.h>
#include <pulsecore/log.h>
#include <pulsecore/conf-parser.h>
#include <pulsecore/core-util.h>
#include <pulsecore/authkey.h>

#include "client-conf.h"

#ifdef HAVE_X11
#include <pulse/client-conf-x11.h>
#endif

#define DEFAULT_CLIENT_CONFIG_FILE PA_DEFAULT_CONFIG_DIR PA_PATH_SEP "client.conf"
#define DEFAULT_CLIENT_CONFIG_FILE_USER "client.conf"

#define ENV_CLIENT_CONFIG_FILE "PULSE_CLIENTCONFIG"
#define ENV_DEFAULT_SINK "PULSE_SINK"
#define ENV_DEFAULT_SOURCE "PULSE_SOURCE"
#define ENV_DEFAULT_SERVER "PULSE_SERVER"
#define ENV_DAEMON_BINARY "PULSE_BINARY"
#define ENV_COOKIE_FILE "PULSE_COOKIE"

static const pa_client_conf default_conf = {
    .daemon_binary = NULL,
    .extra_arguments = NULL,
    .default_sink = NULL,
    .default_source = NULL,
    .default_server = NULL,
    .default_dbus_server = NULL,
    .cookie_file_from_env = NULL,
    .cookie_from_x11_valid = false,
    .cookie_file_from_application = NULL,
    .cookie_file_from_client_conf = NULL,
    .autospawn = true,
    .disable_shm = false,
    .disable_memfd = false,
    .shm_size = 0,
    .auto_connect_localhost = false,
    .auto_connect_display = false
};

pa_client_conf *pa_client_conf_new(void) {
    pa_client_conf *c = pa_xmemdup(&default_conf, sizeof(default_conf));

    c->daemon_binary = pa_xstrdup(PA_BINARY);
    c->extra_arguments = pa_xstrdup("--log-target=syslog");

    return c;
}

void pa_client_conf_free(pa_client_conf *c) {
    pa_assert(c);
    pa_xfree(c->daemon_binary);
    pa_xfree(c->extra_arguments);
    pa_xfree(c->default_sink);
    pa_xfree(c->default_source);
    pa_xfree(c->default_server);
    pa_xfree(c->default_dbus_server);
    pa_xfree(c->cookie_file_from_env);
    pa_xfree(c->cookie_file_from_application);
    pa_xfree(c->cookie_file_from_client_conf);
    pa_xfree(c);
}

static void load_env(pa_client_conf *c) {
    char *e;

    if ((e = getenv(ENV_DEFAULT_SINK))) {
        pa_xfree(c->default_sink);
        c->default_sink = pa_xstrdup(e);
    }

    if ((e = getenv(ENV_DEFAULT_SOURCE))) {
        pa_xfree(c->default_source);
        c->default_source = pa_xstrdup(e);
    }

    if ((e = getenv(ENV_DEFAULT_SERVER))) {
        pa_xfree(c->default_server);
        c->default_server = pa_xstrdup(e);

        /* We disable autospawning automatically if a specific server was set */
        c->autospawn = false;
    }

    if ((e = getenv(ENV_DAEMON_BINARY))) {
        pa_xfree(c->daemon_binary);
        c->daemon_binary = pa_xstrdup(e);
    }

    if ((e = getenv(ENV_COOKIE_FILE)) && *e) {
        pa_xfree(c->cookie_file_from_env);
        c->cookie_file_from_env = pa_xstrdup(e);
    }
}

void pa_client_conf_load(pa_client_conf *c, bool load_from_x11, bool load_from_env) {
    FILE *f = NULL;
    char *fn = NULL;

    /* Prepare the configuration parse table */
    pa_config_item table[] = {
        { "daemon-binary",          pa_config_parse_string,   &c->daemon_binary, NULL },
        { "extra-arguments",        pa_config_parse_string,   &c->extra_arguments, NULL },
        { "default-sink",           pa_config_parse_string,   &c->default_sink, NULL },
        { "default-source",         pa_config_parse_string,   &c->default_source, NULL },
        { "default-server",         pa_config_parse_string,   &c->default_server, NULL },
        { "default-dbus-server",    pa_config_parse_string,   &c->default_dbus_server, NULL },
        { "autospawn",              pa_config_parse_bool,     &c->autospawn, NULL },
        { "cookie-file",            pa_config_parse_string,   &c->cookie_file_from_client_conf, NULL },
        { "disable-shm",            pa_config_parse_bool,     &c->disable_shm, NULL },
        { "enable-shm",             pa_config_parse_not_bool, &c->disable_shm, NULL },
        { "enable-memfd",           pa_config_parse_not_bool, &c->disable_memfd, NULL },
        { "shm-size-bytes",         pa_config_parse_size,     &c->shm_size, NULL },
        { "auto-connect-localhost", pa_config_parse_bool,     &c->auto_connect_localhost, NULL },
        { "auto-connect-display",   pa_config_parse_bool,     &c->auto_connect_display, NULL },
        { NULL,                     NULL,                     NULL, NULL },
    };

    f = pa_open_config_file(DEFAULT_CLIENT_CONFIG_FILE, DEFAULT_CLIENT_CONFIG_FILE_USER, ENV_CLIENT_CONFIG_FILE, &fn);
    if (f) {
        pa_config_parse(fn, f, table, NULL, true, NULL);
        pa_xfree(fn);
        fclose(f);
    }

    if (load_from_x11) {
#ifdef HAVE_X11
        pa_client_conf_from_x11(c);
#endif
    }

    if (load_from_env)
        load_env(c);
}

int pa_client_conf_load_cookie(pa_client_conf *c, uint8_t *cookie, size_t cookie_length) {
    int r;
    char *fallback_path;

    pa_assert(c);
    pa_assert(cookie);
    pa_assert(cookie_length > 0);

    if (c->cookie_file_from_env) {
        r = pa_authkey_load(c->cookie_file_from_env, true, cookie, cookie_length);
        if (r >= 0)
            return 0;

        pa_log_warn("Failed to load cookie from %s (configured with environment variable PULSE_COOKIE): %s",
                    c->cookie_file_from_env, pa_cstrerror(errno));
    }

    if (c->cookie_from_x11_valid) {
        if (cookie_length == sizeof(c->cookie_from_x11)) {
            memcpy(cookie, c->cookie_from_x11, cookie_length);
            return 0;
        }

        pa_log_warn("Failed to load cookie from X11 root window property PULSE_COOKIE: size mismatch.");
    }

    if (c->cookie_file_from_application) {
        r = pa_authkey_load(c->cookie_file_from_application, true, cookie, cookie_length);
        if (r >= 0)
            return 0;

        pa_log_warn("Failed to load cookie from %s (configured by the application): %s", c->cookie_file_from_application,
                    pa_cstrerror(errno));
    }

    if (c->cookie_file_from_client_conf) {
        r = pa_authkey_load(c->cookie_file_from_client_conf, true, cookie, cookie_length);
        if (r >= 0)
            return 0;

        pa_log_warn("Failed to load cookie from %s (configured in client.conf): %s", c->cookie_file_from_client_conf,
                    pa_cstrerror(errno));
    }

    r = pa_authkey_load(PA_NATIVE_COOKIE_FILE, false, cookie, cookie_length);
    if (r >= 0)
        return 0;

    if (pa_append_to_home_dir(PA_NATIVE_COOKIE_FILE_FALLBACK, &fallback_path) >= 0) {
        r = pa_authkey_load(fallback_path, false, cookie, cookie_length);
        pa_xfree(fallback_path);
        if (r >= 0)
            return 0;
    }

    r = pa_authkey_load(PA_NATIVE_COOKIE_FILE, true, cookie, cookie_length);
    if (r >= 0)
        return 0;

    pa_log("Failed to load cookie file from %s: %s", PA_NATIVE_COOKIE_FILE, pa_cstrerror(errno));
    memset(cookie, 0, cookie_length);

    return -1;
}

void pa_client_conf_set_cookie_file_from_application(pa_client_conf *c, const char *cookie_file) {
    pa_assert(c);
    pa_assert(!cookie_file || *cookie_file);

    pa_xfree(c->cookie_file_from_application);
    c->cookie_file_from_application = pa_xstrdup(cookie_file);
}
