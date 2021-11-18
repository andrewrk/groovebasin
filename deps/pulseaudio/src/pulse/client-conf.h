#ifndef fooclientconfhfoo
#define fooclientconfhfoo

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

#include <pulsecore/macro.h>
#include <pulsecore/native-common.h>

/* A structure containing configuration data for PulseAudio clients. */

typedef struct pa_client_conf {
    char *daemon_binary;
    char *extra_arguments;
    char *default_sink;
    char *default_source;
    char *default_server;
    char *default_dbus_server;
    char *cookie_file_from_env;
    uint8_t cookie_from_x11[PA_NATIVE_COOKIE_LENGTH];
    bool cookie_from_x11_valid;
    char *cookie_file_from_application;
    char *cookie_file_from_client_conf;
    bool autospawn, disable_shm, disable_memfd, auto_connect_localhost, auto_connect_display;
    size_t shm_size;
} pa_client_conf;

/* Create a new configuration data object and reset it to defaults */
pa_client_conf *pa_client_conf_new(void);
void pa_client_conf_free(pa_client_conf *c);

/* Load the configuration data from the client configuration file and
 * optionally from X11 and/or environment variables, overwriting the current
 * settings in *c. */
void pa_client_conf_load(pa_client_conf *c, bool load_from_x11, bool load_from_env);

/* Load the cookie from the cookie sources specified in the configuration, or
 * if nothing is specified or none of the sources work, load the cookie from
 * the default source. If the default source doesn't work either, this function
 * returns a negative value and initializes the cookie to all-zeroes. */
int pa_client_conf_load_cookie(pa_client_conf *c, uint8_t *cookie, size_t cookie_length);

void pa_client_conf_set_cookie_file_from_application(pa_client_conf *c, const char *cookie_file);

#endif
