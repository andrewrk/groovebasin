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

#include <string.h>

#include "x11prop.h"

#include <pulsecore/macro.h>

#include <xcb/xproto.h>

#define PA_XCB_FORMAT 8

static xcb_screen_t *screen_of_display(xcb_connection_t *xcb, int screen) {
    const xcb_setup_t *s;
    xcb_screen_iterator_t iter;

    if ((s = xcb_get_setup(xcb))) {
        iter = xcb_setup_roots_iterator(s);
        for (; iter.rem; --screen, xcb_screen_next(&iter))
            if (0 == screen)
                return iter.data;
    }
    return NULL;
}

void pa_x11_set_prop(xcb_connection_t *xcb, int screen, const char *name, const char *data) {
    xcb_screen_t *xs;
    xcb_intern_atom_reply_t *reply;

    pa_assert(xcb);
    pa_assert(name);
    pa_assert(data);

    if ((xs = screen_of_display(xcb, screen))) {
        reply = xcb_intern_atom_reply(xcb,
                                      xcb_intern_atom(xcb, 0, strlen(name), name),
                                      NULL);

        if (reply) {
            xcb_change_property(xcb, XCB_PROP_MODE_REPLACE, xs->root, reply->atom,
                                XCB_ATOM_STRING, PA_XCB_FORMAT,
                                (int) strlen(data), (const void*) data);

            free(reply);
        }
    }
}

void pa_x11_del_prop(xcb_connection_t *xcb, int screen, const char *name) {
    xcb_screen_t *xs;
    xcb_intern_atom_reply_t *reply;

    pa_assert(xcb);
    pa_assert(name);

    if ((xs = screen_of_display(xcb, screen))) {
        reply = xcb_intern_atom_reply(xcb,
                                      xcb_intern_atom(xcb, 0, strlen(name), name),
                                      NULL);

        if (reply) {
            xcb_delete_property(xcb, xs->root, reply->atom);
            free(reply);
        }
    }
}

char* pa_x11_get_prop(xcb_connection_t *xcb, int screen, const char *name, char *p, size_t l) {
    char *ret = NULL;
    int len;
    xcb_get_property_cookie_t req;
    xcb_get_property_reply_t* prop = NULL;
    xcb_screen_t *xs;
    xcb_intern_atom_reply_t *reply;

    pa_assert(xcb);
    pa_assert(name);
    pa_assert(p);

    xs = screen_of_display(xcb, screen);
    /*
     * Also try and get the settings from the first screen.
     * This allows for e.g. a Media Center to run on screen 1 (e.g. HDMI) and have
     * different defaults (e.g. prefer the HDMI sink) than the primary screen 0
     * which uses the Internal Audio sink.
     */
    if (!xs && 0 != screen)
        xs = screen_of_display(xcb, 0);

    if (xs) {
        reply = xcb_intern_atom_reply(xcb,
                                      xcb_intern_atom(xcb, 0, strlen(name), name),
                                      NULL);

        if (!reply)
            goto finish;

        req = xcb_get_property(xcb, 0, xs->root, reply->atom, XCB_ATOM_STRING, 0, (uint32_t)(l-1));
        free(reply);
        prop = xcb_get_property_reply(xcb, req, NULL);

        if (!prop)
            goto finish;

        if (PA_XCB_FORMAT != prop->format)
            goto finish;

        len = xcb_get_property_value_length(prop);
        if (len < 1 || len >= (int)l)
            goto finish;

        memcpy(p, xcb_get_property_value(prop), len);
        p[len] = 0;

        ret = p;
    }

finish:

    if (prop)
        free(prop);

    return ret;
}
