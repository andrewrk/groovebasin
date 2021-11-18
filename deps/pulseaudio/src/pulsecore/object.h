#ifndef foopulseobjecthfoo
#define foopulseobjecthfoo

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

#include <pulse/xmalloc.h>
#include <pulsecore/refcnt.h>
#include <pulsecore/macro.h>

typedef struct pa_object pa_object;

struct pa_object {
    PA_REFCNT_DECLARE;
    const char *type_id;
    void (*free)(pa_object *o);
    bool (*check_type)(const char *type_name);
};

pa_object *pa_object_new_internal(size_t size, const char *type_id, bool (*check_type)(const char *type_id));
#define pa_object_new(type) ((type*) pa_object_new_internal(sizeof(type), type##_type_id, type##_check_type))

#define pa_object_free ((void (*) (pa_object* _obj)) pa_xfree)

bool pa_object_check_type(const char *type_id);

extern const char pa_object_type_id[];

static inline bool pa_object_isinstance(void *o) {
    pa_object *obj = (pa_object*) o;
    return obj ? obj->check_type(pa_object_type_id) : true;
}

pa_object *pa_object_ref(pa_object *o);
void pa_object_unref(pa_object *o);

static inline int pa_object_refcnt(pa_object *o) {
    return o ? PA_REFCNT_VALUE(o) : 0;
}

static inline pa_object* pa_object_cast(void *o) {
    pa_object *obj = (pa_object*) o;
    pa_assert(!obj || obj->check_type(pa_object_type_id));
    return obj;
}

#define pa_object_assert_ref(o) pa_assert(pa_object_refcnt(o) > 0)

#define PA_OBJECT(o) pa_object_cast(o)

#define PA_DECLARE_CLASS_COMMON(c)                                      \
    static inline bool c##_isinstance(void *o) {                        \
        pa_object *obj = (pa_object*) o;                                \
        return obj ? obj->check_type(c##_type_id) : true;               \
    }                                                                   \
    static inline c* c##_cast(void *o) {                                \
        pa_assert(c##_isinstance(o));                                   \
        return (c*) o;                                                  \
    }                                                                   \
    static inline c* c##_ref(c *o) {                                    \
        return (c *) ((void *) pa_object_ref(PA_OBJECT(o)));            \
    }                                                                   \
    static inline void c##_unref(c* o) {                                \
        pa_object_unref(PA_OBJECT(o));                                  \
    }                                                                   \
    static inline int c##_refcnt(c* o) {                                \
        return pa_object_refcnt(PA_OBJECT(o));                          \
    }                                                                   \
    static inline void c##_assert_ref(c *o) {                           \
        pa_object_assert_ref(PA_OBJECT(o));                             \
    }                                                                   \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_DECLARE_PUBLIC_CLASS(c)                                      \
    extern const char c##_type_id[];                                    \
    PA_DECLARE_CLASS_COMMON(c);                                         \
    bool c##_check_type(const char *type_id)

#define PA_DEFINE_PUBLIC_CLASS(c, parent)                               \
    const char c##_type_id[] = #c;                                      \
    bool c##_check_type(const char *type_id) {                          \
        if (type_id == c##_type_id)                                     \
            return true;                                                \
        return parent##_check_type(type_id);                            \
    }                                                                   \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_DEFINE_PRIVATE_CLASS(c, parent)                              \
    static const char c##_type_id[] = #c;                               \
    PA_DECLARE_CLASS_COMMON(c);                                         \
    static bool c##_check_type(const char *type_id) {                   \
        if (type_id == c##_type_id)                                     \
            return true;                                                \
        return parent##_check_type(type_id);                            \
    }                                                                   \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#endif
