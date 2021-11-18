#ifndef foocoresubscribehfoo
#define foocoresubscribehfoo

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

typedef struct pa_subscription pa_subscription;
typedef struct pa_subscription_event pa_subscription_event;

#include <pulsecore/core.h>
#include <pulsecore/native-common.h>

typedef void (*pa_subscription_cb_t)(pa_core *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata);

pa_subscription* pa_subscription_new(pa_core *c, pa_subscription_mask_t m,  pa_subscription_cb_t cb, void *userdata);
void pa_subscription_free(pa_subscription*s);
void pa_subscription_free_all(pa_core *c);

void pa_subscription_post(pa_core *c, pa_subscription_event_type_t t, uint32_t idx);

#endif
