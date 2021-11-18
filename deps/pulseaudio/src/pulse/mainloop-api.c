/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <stdlib.h>

#include <pulse/xmalloc.h>

#include <pulsecore/i18n.h>
#include <pulsecore/macro.h>

#include "mainloop-api.h"

struct once_info {
    void (*callback)(pa_mainloop_api*m, void *userdata);
    void *userdata;
};

static void once_callback(pa_mainloop_api *m, pa_defer_event *e, void *userdata) {
    struct once_info *i = userdata;

    pa_assert(m);
    pa_assert(i);

    pa_assert(i->callback);
    i->callback(m, i->userdata);

    pa_assert(m->defer_free);
    m->defer_free(e);
}

static void free_callback(pa_mainloop_api *m, pa_defer_event *e, void *userdata) {
    struct once_info *i = userdata;

    pa_assert(m);
    pa_assert(i);
    pa_xfree(i);
}

void pa_mainloop_api_once(pa_mainloop_api* m, void (*callback)(pa_mainloop_api *m, void *userdata), void *userdata) {
    struct once_info *i;
    pa_defer_event *e;

    pa_assert(m);
    pa_assert(callback);

    pa_init_i18n();

    i = pa_xnew(struct once_info, 1);
    i->callback = callback;
    i->userdata = userdata;

    pa_assert(m->defer_new);
    pa_assert_se(e = m->defer_new(m, once_callback, i));
    m->defer_set_destroy(e, free_callback);
}
