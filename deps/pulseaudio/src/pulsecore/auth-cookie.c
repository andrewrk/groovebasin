/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#include <sys/types.h>

#include <pulse/xmalloc.h>

#include <pulsecore/refcnt.h>
#include <pulsecore/macro.h>
#include <pulsecore/core-util.h>
#include <pulsecore/shared.h>
#include <pulsecore/authkey.h>

#include "auth-cookie.h"

struct pa_auth_cookie {
    PA_REFCNT_DECLARE;
    pa_core *core;
    char *name;
    size_t size;
};

pa_auth_cookie* pa_auth_cookie_get(pa_core *core, const char *cn, bool create, size_t size) {
    pa_auth_cookie *c;
    char *t;

    pa_assert(core);
    pa_assert(size > 0);

    t = pa_sprintf_malloc("auth-cookie%s%s", cn ? "@" : "", cn ? cn : "");

    if ((c = pa_shared_get(core, t))) {

        pa_xfree(t);

        if (c->size != size)
            return NULL;

        return pa_auth_cookie_ref(c);
    }

    c = pa_xmalloc(PA_ALIGN(sizeof(pa_auth_cookie)) + size);
    PA_REFCNT_INIT(c);
    c->core = core;
    c->name = t;
    c->size = size;

    pa_assert_se(pa_shared_set(core, t, c) >= 0);

    if (pa_authkey_load(cn, create, (uint8_t*) c + PA_ALIGN(sizeof(pa_auth_cookie)), size) < 0) {
        pa_auth_cookie_unref(c);
        return NULL;
    }

    return c;
}

pa_auth_cookie *pa_auth_cookie_create(pa_core *core, const void *data, size_t size) {
    pa_auth_cookie *c;
    char *t;

    pa_assert(core);
    pa_assert(data);
    pa_assert(size > 0);

    t = pa_xstrdup("auth-cookie");

    if ((c = pa_shared_get(core, t))) {

        pa_xfree(t);

        if (c->size != size)
            return NULL;

        return pa_auth_cookie_ref(c);
    }

    c = pa_xmalloc(PA_ALIGN(sizeof(pa_auth_cookie)) + size);
    PA_REFCNT_INIT(c);
    c->core = core;
    c->name = t;
    c->size = size;

    pa_assert_se(pa_shared_set(core, t, c) >= 0);

    memcpy((uint8_t *) c + PA_ALIGN(sizeof(pa_auth_cookie)), data, size);

    return c;
}

pa_auth_cookie* pa_auth_cookie_ref(pa_auth_cookie *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_REFCNT_INC(c);

    return c;
}

void pa_auth_cookie_unref(pa_auth_cookie *c) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (PA_REFCNT_DEC(c) > 0)
        return;

    pa_assert_se(pa_shared_remove(c->core, c->name) >= 0);

    pa_xfree(c->name);
    pa_xfree(c);
}

const uint8_t* pa_auth_cookie_read(pa_auth_cookie *c, size_t size) {
    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(c->size == size);

    return (const uint8_t*) c + PA_ALIGN(sizeof(pa_auth_cookie));
}
