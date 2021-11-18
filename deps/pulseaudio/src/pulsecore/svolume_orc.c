/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2009 Wim Taymans <wim.taymans@collabora.co.uk>
  Copyright 2010 Arun Raghavan <arun.raghavan@collabora.co.uk>

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

#include "cpu-orc.h"
#include <pulse/rtclock.h>
#include <pulsecore/sample-util.h>
#include <pulsecore/random.h>
#include <pulsecore/svolume-orc-gen.h>

pa_do_volume_func_t fallback;

static void
pa_volume_s16ne_orc(int16_t *samples, const int32_t *volumes, unsigned channels, unsigned length) {
    if (channels == 2) {
        int64_t v = (int64_t)volumes[1] << 32 | volumes[0];
        pa_volume_s16ne_orc_2ch (samples, v, ((length / (sizeof(int16_t))) / 2));
    } else if (channels == 1)
        pa_volume_s16ne_orc_1ch (samples, volumes[0], length / (sizeof(int16_t)));
    else
        fallback(samples, volumes, channels, length);
}

void pa_volume_func_init_orc(void) {
    pa_log_info("Initialising ORC optimized volume functions.");

    fallback = pa_get_volume_func(PA_SAMPLE_S16NE);
    pa_set_volume_func(PA_SAMPLE_S16NE, (pa_do_volume_func_t) pa_volume_s16ne_orc);
}
