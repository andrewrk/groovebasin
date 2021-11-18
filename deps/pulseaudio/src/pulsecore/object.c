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

#include "object.h"

const char pa_object_type_id[] = "pa_object";

pa_object *pa_object_new_internal(size_t size, const char *type_id, bool (*check_type)(const char *type_id)) {
    pa_object *o;

    pa_assert(size > sizeof(pa_object));
    pa_assert(type_id);

    if (!check_type)
        check_type = pa_object_check_type;

    pa_assert(check_type(type_id));
    pa_assert(check_type(pa_object_type_id));

    o = pa_xmalloc0(size);
    PA_REFCNT_INIT(o);
    o->type_id = type_id;
    o->free = pa_object_free;
    o->check_type = check_type;

    return o;
}

pa_object *pa_object_ref(pa_object *o) {
    pa_object_assert_ref(o);

    PA_REFCNT_INC(o);
    return o;
}

void pa_object_unref(pa_object *o) {
    pa_object_assert_ref(o);

    if (PA_REFCNT_DEC(o) <= 0) {
        pa_assert(o->free);
        o->free(o);
    }
}

bool pa_object_check_type(const char *type_id) {
    pa_assert(type_id);

    return type_id == pa_object_type_id;
}
