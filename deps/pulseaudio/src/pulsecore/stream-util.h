#ifndef foostreamutilhfoo
#define foostreamutilhfoo

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

#include <pulse/format.h>
#include <pulse/volume.h>

/* This is a helper function that is called from pa_sink_input_new() and
 * pa_source_output_new(). The job of this function is to figure out what
 * channel map should be used for interpreting the volume that was set for the
 * stream. The channel map that the client intended for the volume may be
 * different than the final stream channel map, because the client may want the
 * server to decide the stream channel map.
 *
 * volume is the volume for which the channel map should be figured out.
 *
 * original_map is the channel map that is set in the new data struct's
 * channel_map field. If the channel map hasn't been set in the new data, then
 * original_map should be NULL.
 *
 * format is the negotiated format for the stream. It's used as a fallback if
 * original_map is not available.
 *
 * On success, the result is saved in volume_map. It's possible that this
 * function fails to figure out the right channel map for the volume, in which
 * case a negative error code is returned. */
int pa_stream_get_volume_channel_map(const pa_cvolume *volume, const pa_channel_map *original_map, const pa_format_info *format,
                                     pa_channel_map *volume_map);

#endif
