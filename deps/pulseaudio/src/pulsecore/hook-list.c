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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>

#include "hook-list.h"

void pa_hook_init(pa_hook *hook, void *data) {
    pa_assert(hook);

    PA_LLIST_HEAD_INIT(pa_hook_slot, hook->slots);
    hook->n_dead = hook->n_firing = 0;
    hook->data = data;
}

static void slot_free(pa_hook *hook, pa_hook_slot *slot) {
    pa_assert(hook);
    pa_assert(slot);

    PA_LLIST_REMOVE(pa_hook_slot, hook->slots, slot);

    pa_xfree(slot);
}

void pa_hook_done(pa_hook *hook) {
    pa_assert(hook);
    pa_assert(hook->n_firing == 0);

    while (hook->slots)
        slot_free(hook, hook->slots);

    pa_hook_init(hook, NULL);
}

pa_hook_slot* pa_hook_connect(pa_hook *hook, pa_hook_priority_t prio, pa_hook_cb_t cb, void *data) {
    pa_hook_slot *slot, *where, *prev;

    pa_assert(cb);

    slot = pa_xnew(pa_hook_slot, 1);
    slot->hook = hook;
    slot->dead = false;
    slot->callback = cb;
    slot->data = data;
    slot->priority = prio;

    prev = NULL;
    for (where = hook->slots; where; where = where->next) {
        if (prio < where->priority)
            break;
        prev = where;
    }

    PA_LLIST_INSERT_AFTER(pa_hook_slot, hook->slots, prev, slot);

    return slot;
}

void pa_hook_slot_free(pa_hook_slot *slot) {
    pa_assert(slot);
    pa_assert(!slot->dead);

    if (slot->hook->n_firing > 0) {
        slot->dead = true;
        slot->hook->n_dead++;
    } else
        slot_free(slot->hook, slot);
}

pa_hook_result_t pa_hook_fire(pa_hook *hook, void *data) {
    pa_hook_slot *slot, *next;
    pa_hook_result_t result = PA_HOOK_OK;

    pa_assert(hook);

    hook->n_firing ++;

    PA_LLIST_FOREACH(slot, hook->slots) {
        if (slot->dead)
            continue;

        if ((result = slot->callback(hook->data, data, slot->data)) != PA_HOOK_OK)
            break;
    }

    hook->n_firing --;
    pa_assert(hook->n_firing >= 0);

    for (slot = hook->slots; hook->n_dead > 0 && slot; slot = next) {
        next = slot->next;

        if (slot->dead) {
            slot_free(hook, slot);
            hook->n_dead--;
        }
    }

    pa_assert(hook->n_dead == 0);

    return result;
}

bool pa_hook_is_firing(pa_hook *hook) {
    pa_assert(hook);

    return hook->n_firing > 0;
}
