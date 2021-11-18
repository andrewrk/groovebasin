#ifndef foomodulehfoo
#define foomodulehfoo

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
#include <ltdl.h>

typedef struct pa_module pa_module;

#include <pulse/proplist.h>
#include <pulsecore/dynarray.h>

#include <pulsecore/core.h>

enum {
  PA_MODULE_ERR_UNSPECIFIED = 1,
  PA_MODULE_ERR_SKIP = 2
};

struct pa_module {
    pa_core *core;
    char *name, *argument;
    uint32_t index;

    lt_dlhandle dl;

    int (*init)(pa_module*m);
    void (*done)(pa_module*m);
    int (*get_n_used)(pa_module *m);

    void *userdata;

    bool load_once:1;
    bool unload_requested:1;

    pa_proplist *proplist;
    pa_dynarray *hooks;
};

bool pa_module_exists(const char *name);

int pa_module_load(pa_module** m, pa_core *c, const char *name, const char *argument);

void pa_module_unload(pa_module *m, bool force);
void pa_module_unload_by_index(pa_core *c, uint32_t idx, bool force);

void pa_module_unload_request(pa_module *m, bool force);
void pa_module_unload_request_by_index(pa_core *c, uint32_t idx, bool force);

void pa_module_unload_all(pa_core *c);

int pa_module_get_n_used(pa_module*m);

void pa_module_update_proplist(pa_module *m, pa_update_mode_t mode, pa_proplist *p);

void pa_module_hook_connect(pa_module *m, pa_hook *hook, pa_hook_priority_t prio, pa_hook_cb_t cb, void *data);

#define PA_MODULE_AUTHOR(s)                                     \
    const char *pa__get_author(void) { return s; }              \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_MODULE_DESCRIPTION(s)                                \
    const char *pa__get_description(void) { return s; }         \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_MODULE_USAGE(s)                                      \
    const char *pa__get_usage(void) { return s; }               \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_MODULE_VERSION(s)                                    \
    const char * pa__get_version(void) { return s; }            \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_MODULE_DEPRECATED(s)                                 \
    const char * pa__get_deprecated(void) { return s; }         \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

#define PA_MODULE_LOAD_ONCE(b)                                  \
    bool pa__load_once(void) { return b; }                 \
    struct __stupid_useless_struct_to_allow_trailing_semicolon

/* Check if we're defining a module (usually defined via compiler flags) */
#ifdef PA_MODULE_NAME

/* Jump through some double-indirection hoops to get PA_MODULE_NAME substituted before the concatenation */
#define _MACRO_CONCAT1(a, b) a ## b
#define _MACRO_CONCAT(a, b) _MACRO_CONCAT1(a, b)

#define pa__init _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__init)
#define pa__done _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__done)
#define pa__get_author _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_author)
#define pa__get_description _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_description)
#define pa__get_usage _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_usage)
#define pa__get_version _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_version)
#define pa__get_deprecated _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_deprecated)
#define pa__load_once _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__load_once)
#define pa__get_n_used _MACRO_CONCAT(PA_MODULE_NAME, _LTX_pa__get_n_used)

int pa__init(pa_module*m);
void pa__done(pa_module*m);
int pa__get_n_used(pa_module*m);

const char* pa__get_author(void);
const char* pa__get_description(void);
const char* pa__get_usage(void);
const char* pa__get_version(void);
const char* pa__get_deprecated(void);
bool pa__load_once(void);
#endif /* PA_MODULE_NAME */

#endif
