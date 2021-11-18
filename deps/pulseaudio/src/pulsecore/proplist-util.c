/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#include <string.h>
#include <locale.h>

#ifdef ENABLE_NLS
#include <libintl.h>
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#elif !HAVE_DECL_ENVIRON
extern char **environ;
#endif

#include <pulse/gccmacro.h>
#include <pulse/proplist.h>
#include <pulse/utf8.h>
#include <pulse/xmalloc.h>
#include <pulse/util.h>

#include <pulsecore/core-util.h>

#if defined(HAVE_GLIB) && defined(PA_GCC_WEAKREF)
#include <glib.h>
static G_CONST_RETURN gchar* _g_get_application_name(void) PA_GCC_WEAKREF(g_get_application_name);
#endif

#if defined(HAVE_GTK) && defined(PA_GCC_WEAKREF)
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
static G_CONST_RETURN gchar* _gtk_window_get_default_icon_name(void) PA_GCC_WEAKREF(gtk_window_get_default_icon_name);
static Display *_gdk_display PA_GCC_WEAKREF(gdk_display);
#endif

#include "proplist-util.h"

static void add_glib_properties(pa_proplist *p) {

#if defined(HAVE_GLIB) && defined(PA_GCC_WEAKREF)

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_NAME))
        if (_g_get_application_name) {
            const gchar *t;

            /* We ignore the tiny race condition here. */

            if ((t = _g_get_application_name()))
                pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, t);
        }

#endif
}

static void add_gtk_properties(pa_proplist *p) {

#if defined(HAVE_GTK) && defined(PA_GCC_WEAKREF)

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_ICON_NAME))
        if (_gtk_window_get_default_icon_name) {
            const gchar *t;

            /* We ignore the tiny race condition here. */

            if ((t = _gtk_window_get_default_icon_name()))
                pa_proplist_sets(p, PA_PROP_APPLICATION_ICON_NAME, t);
        }

    if (!pa_proplist_contains(p, PA_PROP_WINDOW_X11_DISPLAY))
        if (&_gdk_display && _gdk_display) {
            const char *t;

            /* We ignore the tiny race condition here. */

            if ((t = DisplayString(_gdk_display)))
                pa_proplist_sets(p, PA_PROP_WINDOW_X11_DISPLAY, t);
        }

#endif
}

void pa_init_proplist(pa_proplist *p) {
    char **e;
    const char *pp;

    pa_assert(p);

    if (environ) {

        /* Some applications seem to reset environ to NULL for various
         * reasons, hence we need to check for this explicitly. See
         * rhbz #473080 */

        for (e = environ; *e; e++) {

            if (pa_startswith(*e, "PULSE_PROP_")) {
                size_t kl, skip;
                char *k;
                bool override;

                if (pa_startswith(*e, "PULSE_PROP_OVERRIDE_")) {
                    skip = 20;
                    override = true;
                } else {
                    skip = 11;
                    override = false;
                }

                kl = strcspn(*e+skip, "=");

                if ((*e)[skip+kl] != '=')
                    continue;

                k = pa_xstrndup(*e+skip, kl);

                if (!pa_streq(k, "OVERRIDE"))
                    if (override || !pa_proplist_contains(p, k))
                        pa_proplist_sets(p, k, *e+skip+kl+1);
                pa_xfree(k);
            }
        }
    }

    if ((pp = getenv("PULSE_PROP"))) {
        pa_proplist *t;

        if ((t = pa_proplist_from_string(pp))) {
            pa_proplist_update(p, PA_UPDATE_MERGE, t);
            pa_proplist_free(t);
        }
    }

    if ((pp = getenv("PULSE_PROP_OVERRIDE"))) {
        pa_proplist *t;

        if ((t = pa_proplist_from_string(pp))) {
            pa_proplist_update(p, PA_UPDATE_REPLACE, t);
            pa_proplist_free(t);
        }
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_ID)) {
        char t[32];
        pa_snprintf(t, sizeof(t), "%lu", (unsigned long) getpid());
        pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_ID, t);
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_USER)) {
        char *u;

        if ((u = pa_get_user_name_malloc())) {
            pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_USER, u);
            pa_xfree(u);
        }
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_HOST)) {
        char *h;

        if ((h = pa_get_host_name_malloc())) {
            pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_HOST, h);
            pa_xfree(h);
        }
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_BINARY)) {
        char *t;

        if ((t = pa_get_binary_name_malloc())) {
            char *c = pa_utf8_filter(t);
            pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_BINARY, c);
            pa_xfree(t);
            pa_xfree(c);
        }
    }

    add_glib_properties(p);
    add_gtk_properties(p);

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_NAME)) {
        const char *t;

        if ((t = pa_proplist_gets(p, PA_PROP_APPLICATION_PROCESS_BINARY)))
            pa_proplist_sets(p, PA_PROP_APPLICATION_NAME, t);
    }

#ifdef ENABLE_NLS
    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_LANGUAGE)) {
        const char *l;

        if ((l = setlocale(LC_MESSAGES, NULL)))
            pa_proplist_sets(p, PA_PROP_APPLICATION_LANGUAGE, l);
    }
#endif

    if (!pa_proplist_contains(p, PA_PROP_WINDOW_X11_DISPLAY)) {
        const char *t;

        if ((t = getenv("DISPLAY"))) {
            char *c = pa_utf8_filter(t);
            pa_proplist_sets(p, PA_PROP_WINDOW_X11_DISPLAY, c);
            pa_xfree(c);
        }
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_MACHINE_ID)) {
        char *m;

        if ((m = pa_machine_id())) {
            pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_MACHINE_ID, m);
            pa_xfree(m);
        }
    }

    if (!pa_proplist_contains(p, PA_PROP_APPLICATION_PROCESS_SESSION_ID)) {
        char *s;

        if ((s = pa_session_id())) {
            pa_proplist_sets(p, PA_PROP_APPLICATION_PROCESS_SESSION_ID, s);
            pa_xfree(s);
        }
    }
}

char *pa_proplist_get_stream_group(pa_proplist *p, const char *prefix, const char *cache) {
    const char *r;
    char *t;

    if (!p)
        return NULL;

    if (cache && (r = pa_proplist_gets(p, cache)))
        return pa_xstrdup(r);

    if (!prefix)
        prefix = "stream";

    if ((r = pa_proplist_gets(p, PA_PROP_MEDIA_ROLE)))
        t = pa_sprintf_malloc("%s-by-media-role:%s", prefix, r);
    else if ((r = pa_proplist_gets(p, PA_PROP_APPLICATION_ID)))
        t = pa_sprintf_malloc("%s-by-application-id:%s", prefix, r);
    else if ((r = pa_proplist_gets(p, PA_PROP_APPLICATION_NAME)))
        t = pa_sprintf_malloc("%s-by-application-name:%s", prefix, r);
    else if ((r = pa_proplist_gets(p, PA_PROP_MEDIA_NAME)))
        t = pa_sprintf_malloc("%s-by-media-name:%s", prefix, r);
    else
        t = pa_sprintf_malloc("%s-fallback:%s", prefix, r);

    if (cache)
        pa_proplist_sets(p, cache, t);

    return t;
}
