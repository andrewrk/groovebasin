#ifndef foopulseclienthfoo
#define foopulseclienthfoo

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

#include <inttypes.h>

#include <pulsecore/typedefs.h>
#include <pulse/proplist.h>
#include <pulsecore/core.h>
#include <pulsecore/module.h>

/* Every connection to the server should have a pa_client
 * attached. That way the user may generate a listing of all connected
 * clients easily and kill them if they want.*/

struct pa_client {
    uint32_t index;
    pa_core *core;

    pa_proplist *proplist;
    pa_module *module;
    char *driver;

    pa_idxset *sink_inputs;
    pa_idxset *source_outputs;

    void *userdata;

    void (*kill)(pa_client *c);

    void (*send_event)(pa_client *c, const char *name, pa_proplist *data);
};

typedef struct pa_client_new_data {
    pa_proplist *proplist;
    const char *driver;
    pa_module *module;
} pa_client_new_data;

pa_client_new_data *pa_client_new_data_init(pa_client_new_data *data);
void pa_client_new_data_done(pa_client_new_data *data);

pa_client *pa_client_new(pa_core *c, pa_client_new_data *data);

/* This function should be called only by the code that created the client */
void pa_client_free(pa_client *c);

/* Code that didn't create the client should call this function to
 * request destruction of the client */
void pa_client_kill(pa_client *c);

/* Rename the client */
void pa_client_set_name(pa_client *c, const char *name);

void pa_client_update_proplist(pa_client *c, pa_update_mode_t mode, pa_proplist *p);

void pa_client_send_event(pa_client *c, const char *event, pa_proplist *data);

typedef struct pa_client_send_event_hook_data {
    pa_client *client;
    const char *event;
    pa_proplist *data;
} pa_client_send_event_hook_data;

#endif
