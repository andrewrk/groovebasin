#ifndef foohooklistfoo
#define foohooklistfoo

/***
  This file is part of PulseAudio.

  Copyright 2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <pulsecore/llist.h>

typedef struct pa_hook_slot pa_hook_slot;
typedef struct pa_hook pa_hook;

typedef enum pa_hook_result {
    PA_HOOK_OK = 0,
    PA_HOOK_STOP = 1,
    PA_HOOK_CANCEL = -1
} pa_hook_result_t;

typedef enum pa_hook_priority {
    PA_HOOK_EARLY = -100,
    PA_HOOK_NORMAL = 0,
    PA_HOOK_LATE = 100
} pa_hook_priority_t;

typedef pa_hook_result_t (*pa_hook_cb_t)(
        void *hook_data,
        void *call_data,
        void *slot_data);

struct pa_hook_slot {
    bool dead;
    pa_hook *hook;
    pa_hook_priority_t priority;
    pa_hook_cb_t callback;
    void *data;
    PA_LLIST_FIELDS(pa_hook_slot);
};

struct pa_hook {
    PA_LLIST_HEAD(pa_hook_slot, slots);
    int n_firing, n_dead;

    void *data;
};

void pa_hook_init(pa_hook *hook, void *data);
void pa_hook_done(pa_hook *hook);

pa_hook_slot* pa_hook_connect(pa_hook *hook, pa_hook_priority_t prio, pa_hook_cb_t cb, void *data);
void pa_hook_slot_free(pa_hook_slot *slot);

pa_hook_result_t pa_hook_fire(pa_hook *hook, void *data);

bool pa_hook_is_firing(pa_hook *hook);

#endif
