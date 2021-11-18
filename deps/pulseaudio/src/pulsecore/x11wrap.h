#ifndef foox11wraphfoo
#define foox11wraphfoo

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

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <pulsecore/core.h>

typedef struct pa_x11_wrapper pa_x11_wrapper;

typedef struct pa_x11_client pa_x11_client;

typedef int (*pa_x11_event_cb_t)(pa_x11_wrapper *w, XEvent *e, void *userdata);
typedef void (*pa_x11_kill_cb_t)(pa_x11_wrapper *w, void *userdata);

/* Return the X11 wrapper for this core. In case no wrapper was
    existent before, allocate a new one */
pa_x11_wrapper* pa_x11_wrapper_get(pa_core *c, const char *name);

/* Increase the wrapper's reference count by one */
pa_x11_wrapper* pa_x11_wrapper_ref(pa_x11_wrapper *w);

/* Decrease the reference counter of an X11 wrapper object */
void pa_x11_wrapper_unref(pa_x11_wrapper* w);

/* Return the X11 display object for this connection */
Display *pa_x11_wrapper_get_display(pa_x11_wrapper *w);

/* Return the XCB connection object for this connection */
xcb_connection_t *pa_x11_wrapper_get_xcb_connection(pa_x11_wrapper *w);

/* Kill the connection to the X11 display */
void pa_x11_wrapper_kill(pa_x11_wrapper *w);

/* Register an X11 client, that is called for each X11 event */
pa_x11_client* pa_x11_client_new(pa_x11_wrapper *w, pa_x11_event_cb_t event_cb, pa_x11_kill_cb_t kill_cb, void *userdata);

/* Free an X11 client object */
void pa_x11_client_free(pa_x11_client *c);

#endif
