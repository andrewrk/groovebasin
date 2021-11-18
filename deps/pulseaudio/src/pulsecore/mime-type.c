/***
  This file is part of PulseAudio.

  Copyright 2005-2009 Lennart Poettering

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

#include <pulse/xmalloc.h>
#include <pulsecore/core-util.h>

#include "mime-type.h"

bool pa_sample_spec_is_mime(const pa_sample_spec *ss, const pa_channel_map *cm) {

    pa_assert(pa_channel_map_compatible(cm, ss));

    switch (ss->format) {
        case PA_SAMPLE_S16BE:
        case PA_SAMPLE_S24BE:
        case PA_SAMPLE_U8:

            if (ss->rate != 8000 &&
                ss->rate != 11025 &&
                ss->rate != 16000 &&
                ss->rate != 22050 &&
                ss->rate != 24000 &&
                ss->rate != 32000 &&
                ss->rate != 44100 &&
                ss->rate != 48000)
                return false;

            if (ss->channels != 1 &&
                ss->channels != 2)
                return false;

            if ((cm->channels == 1 && cm->map[0] != PA_CHANNEL_POSITION_MONO) ||
                (cm->channels == 2 && (cm->map[0] != PA_CHANNEL_POSITION_LEFT || cm->map[1] != PA_CHANNEL_POSITION_RIGHT)))
                return false;

            return true;

        case PA_SAMPLE_ULAW:

            if (ss->rate != 8000)
                return false;

            if (ss->channels != 1)
                return false;

            if (cm->map[0] != PA_CHANNEL_POSITION_MONO)
                return false;

            return true;

        default:
            return false;
    }
}

void pa_sample_spec_mimefy(pa_sample_spec *ss, pa_channel_map *cm) {

    pa_assert(pa_channel_map_compatible(cm, ss));

    /* Turns the sample type passed in into the next 'better' one that
     * can be encoded for HTTP. If there is no 'better' one we pick
     * the 'best' one that is 'worse'. */

    if (ss->channels > 2)
        ss->channels = 2;

    if (ss->rate > 44100)
        ss->rate = 48000;
    else if (ss->rate > 32000)
        ss->rate = 44100;
    else if (ss->rate > 24000)
        ss->rate = 32000;
    else if (ss->rate > 22050)
        ss->rate = 24000;
    else if (ss->rate > 16000)
        ss->rate = 22050;
    else if (ss->rate > 11025)
        ss->rate = 16000;
    else if (ss->rate > 8000)
        ss->rate = 11025;
    else
        ss->rate = 8000;

    switch (ss->format) {
        case PA_SAMPLE_S24BE:
        case PA_SAMPLE_S24LE:
        case PA_SAMPLE_S24_32LE:
        case PA_SAMPLE_S24_32BE:
        case PA_SAMPLE_S32LE:
        case PA_SAMPLE_S32BE:
        case PA_SAMPLE_FLOAT32LE:
        case PA_SAMPLE_FLOAT32BE:
            ss->format = PA_SAMPLE_S24BE;
            break;

        case PA_SAMPLE_S16BE:
        case PA_SAMPLE_S16LE:
            ss->format = PA_SAMPLE_S16BE;
            break;

        case PA_SAMPLE_ULAW:
        case PA_SAMPLE_ALAW:

            if (ss->rate == 8000 && ss->channels == 1)
                ss->format = PA_SAMPLE_ULAW;
            else
                ss->format = PA_SAMPLE_S16BE;
            break;

        case PA_SAMPLE_U8:
            ss->format = PA_SAMPLE_U8;
            break;

        case PA_SAMPLE_MAX:
        case PA_SAMPLE_INVALID:
            pa_assert_not_reached();
    }

    pa_channel_map_init_auto(cm, ss->channels, PA_CHANNEL_MAP_DEFAULT);

    pa_assert(pa_sample_spec_is_mime(ss, cm));
}

char *pa_sample_spec_to_mime_type(const pa_sample_spec *ss, const pa_channel_map *cm) {
    pa_assert(pa_channel_map_compatible(cm, ss));
    pa_assert(pa_sample_spec_valid(ss));

    if (!pa_sample_spec_is_mime(ss, cm))
        return NULL;

    switch (ss->format) {

        case PA_SAMPLE_S16BE:
        case PA_SAMPLE_S24BE:
        case PA_SAMPLE_U8:
            /* Stupid UPnP implementations (PS3...) choke on spaces in
             * the mime type, that's why we write only ';' here,
             * instead of '; '. */
            return pa_sprintf_malloc("audio/%s;rate=%u;channels=%u",
                                     ss->format == PA_SAMPLE_S16BE ? "L16" :
                                     (ss->format == PA_SAMPLE_S24BE ? "L24" : "L8"),
                                     ss->rate, ss->channels);

        case PA_SAMPLE_ULAW:
            return pa_xstrdup("audio/basic");

        default:
            pa_assert_not_reached();
    }
}

char *pa_sample_spec_to_mime_type_mimefy(const pa_sample_spec *_ss, const pa_channel_map *_cm) {
    pa_sample_spec ss = *_ss;
    pa_channel_map cm = *_cm;

    pa_sample_spec_mimefy(&ss, &cm);

    return pa_sample_spec_to_mime_type(&ss, &cm);
}
