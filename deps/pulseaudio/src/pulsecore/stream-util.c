/***
  This file is part of PulseAudio.

  Copyright 2013 Intel Corporation

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

#include "stream-util.h"

#include <pulse/def.h>

#include <pulsecore/core-format.h>
#include <pulsecore/macro.h>

int pa_stream_get_volume_channel_map(const pa_cvolume *volume, const pa_channel_map *original_map, const pa_format_info *format,
                                     pa_channel_map *volume_map) {
    int r;
    pa_channel_map volume_map_local;

    pa_assert(volume);
    pa_assert(format);
    pa_assert(volume_map);

    if (original_map) {
        if (volume->channels == original_map->channels) {
            *volume_map = *original_map;
            return 0;
        }

        if (volume->channels == 1) {
            pa_channel_map_init_mono(volume_map);
            return 0;
        }

        pa_log_info("Invalid stream parameters: the volume is incompatible with the channel map.");
        return -PA_ERR_INVALID;
    }

    r = pa_format_info_get_channel_map(format, &volume_map_local);
    if (r == -PA_ERR_NOENTITY) {
        if (volume->channels == 1) {
            pa_channel_map_init_mono(volume_map);
            return 0;
        }

        pa_log_info("Invalid stream parameters: multi-channel volume is set, but channel map is not.");
        return -PA_ERR_INVALID;
    }

    if (r < 0) {
        pa_log_info("Invalid channel map.");
        return -PA_ERR_INVALID;
    }

    if (volume->channels == volume_map_local.channels) {
        *volume_map = volume_map_local;
        return 0;
    }

    if (volume->channels == 1) {
        pa_channel_map_init_mono(volume_map);
        return 0;
    }

    pa_log_info("Invalid stream parameters: the volume is incompatible with the channel map.");

    return -PA_ERR_INVALID;
}
