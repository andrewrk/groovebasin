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

#include "msgobject.h"

PA_DEFINE_PUBLIC_CLASS(pa_msgobject, pa_object);

pa_msgobject *pa_msgobject_new_internal(size_t size, const char *type_id, bool (*check_type)(const char *type_name)) {
    pa_msgobject *o;

    pa_assert(size >= sizeof(pa_msgobject));
    pa_assert(type_id);

    if (!check_type)
        check_type = pa_msgobject_check_type;

    pa_assert(check_type(type_id));
    pa_assert(check_type(pa_object_type_id));
    pa_assert(check_type(pa_msgobject_type_id));

    o = PA_MSGOBJECT(pa_object_new_internal(size, type_id, check_type));
    o->process_msg = NULL;
    return o;
}
