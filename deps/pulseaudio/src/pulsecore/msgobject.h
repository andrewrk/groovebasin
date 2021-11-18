#ifndef foopulsemsgobjecthfoo
#define foopulsemsgobjecthfoo

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

#include <sys/types.h>

#include <pulsecore/macro.h>
#include <pulsecore/object.h>
#include <pulsecore/memchunk.h>

typedef struct pa_msgobject pa_msgobject;

struct pa_msgobject {
    pa_object parent;
    int (*process_msg)(pa_msgobject *o, int code, void *userdata, int64_t offset, pa_memchunk *chunk);
};

pa_msgobject *pa_msgobject_new_internal(size_t size, const char *type_id, bool (*check_type)(const char *type_name));

#define pa_msgobject_new(type) ((type*) pa_msgobject_new_internal(sizeof(type), type##_type_id, type##_check_type))
#define pa_msgobject_free ((void (*) (pa_msgobject* o)) pa_object_free)

#define PA_MSGOBJECT(o) pa_msgobject_cast(o)

PA_DECLARE_PUBLIC_CLASS(pa_msgobject);

#endif
