/***
  This file is part of PulseAudio.

  Copyright 2009 Lennart Poettering

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

/* Shared between pacat/parec/paplay and the server */

#include <pulse/xmalloc.h>
#include <pulse/utf8.h>

#include <pulsecore/macro.h>

#include "sndfile-util.h"

int pa_sndfile_read_sample_spec(SNDFILE *sf, pa_sample_spec *ss) {
    SF_INFO sfi;
    int sf_errno;

    pa_assert(sf);
    pa_assert(ss);

    pa_zero(sfi);
    if ((sf_errno = sf_command(sf, SFC_GET_CURRENT_SF_INFO, &sfi, sizeof(sfi)))) {
        pa_log_error("sndfile: %s", sf_error_number(sf_errno));
        return -1;
    }

    switch (sfi.format & SF_FORMAT_SUBMASK) {

        case SF_FORMAT_PCM_16:
        case SF_FORMAT_PCM_U8:
        case SF_FORMAT_PCM_S8:
            ss->format = PA_SAMPLE_S16NE;
            break;

        case SF_FORMAT_PCM_24:
            ss->format = PA_SAMPLE_S24NE;
            break;

        case SF_FORMAT_PCM_32:
            ss->format = PA_SAMPLE_S32NE;
            break;

        case SF_FORMAT_ULAW:
            ss->format = PA_SAMPLE_ULAW;
            break;

        case SF_FORMAT_ALAW:
            ss->format = PA_SAMPLE_ALAW;
            break;

        case SF_FORMAT_FLOAT:
        case SF_FORMAT_DOUBLE:
        default:
            ss->format = PA_SAMPLE_FLOAT32NE;
            break;
    }

    ss->rate = (uint32_t) sfi.samplerate;
    ss->channels = (uint8_t) sfi.channels;

    if (!pa_sample_spec_valid(ss))
        return -1;

    return 0;
}

int pa_sndfile_write_sample_spec(SF_INFO *sfi, pa_sample_spec *ss) {
    pa_assert(sfi);
    pa_assert(ss);

    sfi->samplerate = (int) ss->rate;
    sfi->channels = (int) ss->channels;

    if (pa_sample_format_is_le(ss->format) > 0)
        sfi->format = SF_ENDIAN_LITTLE;
    else if (pa_sample_format_is_be(ss->format) > 0)
        sfi->format = SF_ENDIAN_BIG;

    switch (ss->format) {

        case PA_SAMPLE_U8:
            ss->format = PA_SAMPLE_S16NE;
            sfi->format = SF_FORMAT_PCM_U8;
            break;

        case PA_SAMPLE_S16LE:
        case PA_SAMPLE_S16BE:
            ss->format = PA_SAMPLE_S16NE;
            sfi->format |= SF_FORMAT_PCM_16;
            break;

        case PA_SAMPLE_S24LE:
        case PA_SAMPLE_S24BE:
            ss->format = PA_SAMPLE_S24NE;
            sfi->format |= SF_FORMAT_PCM_24;
            break;

        case PA_SAMPLE_S24_32LE:
        case PA_SAMPLE_S24_32BE:
            ss->format = PA_SAMPLE_S24_32NE;
            sfi->format |= SF_FORMAT_PCM_32;
            break;

        case PA_SAMPLE_S32LE:
        case PA_SAMPLE_S32BE:
            ss->format = PA_SAMPLE_S32NE;
            sfi->format |= SF_FORMAT_PCM_32;
            break;

        case PA_SAMPLE_ULAW:
            sfi->format = SF_FORMAT_ULAW;
            break;

        case PA_SAMPLE_ALAW:
            sfi->format = SF_FORMAT_ALAW;
            break;

        case PA_SAMPLE_FLOAT32LE:
        case PA_SAMPLE_FLOAT32BE:
        default:
            ss->format = PA_SAMPLE_FLOAT32NE;
            sfi->format |= SF_FORMAT_FLOAT;
            break;
    }

    if (!pa_sample_spec_valid(ss))
        return -1;

    return 0;
}

int pa_sndfile_read_channel_map(SNDFILE *sf, pa_channel_map *cm) {

    static const pa_channel_position_t table[] = {
        [SF_CHANNEL_MAP_MONO] =                  PA_CHANNEL_POSITION_MONO,
        [SF_CHANNEL_MAP_LEFT] =                  PA_CHANNEL_POSITION_FRONT_LEFT, /* libsndfile distinguishes left and front-left, which we don't */
        [SF_CHANNEL_MAP_RIGHT] =                 PA_CHANNEL_POSITION_FRONT_RIGHT,
        [SF_CHANNEL_MAP_CENTER] =                PA_CHANNEL_POSITION_FRONT_CENTER,
        [SF_CHANNEL_MAP_FRONT_LEFT] =            PA_CHANNEL_POSITION_FRONT_LEFT,
        [SF_CHANNEL_MAP_FRONT_RIGHT] =           PA_CHANNEL_POSITION_FRONT_RIGHT,
        [SF_CHANNEL_MAP_FRONT_CENTER] =          PA_CHANNEL_POSITION_FRONT_CENTER,
        [SF_CHANNEL_MAP_REAR_CENTER] =           PA_CHANNEL_POSITION_REAR_CENTER,
        [SF_CHANNEL_MAP_REAR_LEFT] =             PA_CHANNEL_POSITION_REAR_LEFT,
        [SF_CHANNEL_MAP_REAR_RIGHT] =            PA_CHANNEL_POSITION_REAR_RIGHT,
        [SF_CHANNEL_MAP_LFE] =                   PA_CHANNEL_POSITION_LFE,
        [SF_CHANNEL_MAP_FRONT_LEFT_OF_CENTER] =  PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        [SF_CHANNEL_MAP_FRONT_RIGHT_OF_CENTER] = PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        [SF_CHANNEL_MAP_SIDE_LEFT] =             PA_CHANNEL_POSITION_SIDE_LEFT,
        [SF_CHANNEL_MAP_SIDE_RIGHT] =            PA_CHANNEL_POSITION_SIDE_RIGHT,
        [SF_CHANNEL_MAP_TOP_CENTER] =            PA_CHANNEL_POSITION_TOP_CENTER,
        [SF_CHANNEL_MAP_TOP_FRONT_LEFT] =        PA_CHANNEL_POSITION_TOP_FRONT_LEFT,
        [SF_CHANNEL_MAP_TOP_FRONT_RIGHT] =       PA_CHANNEL_POSITION_TOP_FRONT_RIGHT,
        [SF_CHANNEL_MAP_TOP_FRONT_CENTER] =      PA_CHANNEL_POSITION_TOP_FRONT_CENTER,
        [SF_CHANNEL_MAP_TOP_REAR_LEFT] =         PA_CHANNEL_POSITION_TOP_REAR_LEFT,
        [SF_CHANNEL_MAP_TOP_REAR_RIGHT] =        PA_CHANNEL_POSITION_TOP_REAR_RIGHT,
        [SF_CHANNEL_MAP_TOP_REAR_CENTER] =       PA_CHANNEL_POSITION_TOP_REAR_CENTER
    };

    SF_INFO sfi;
    int sf_errno;
    int *channels;
    unsigned c;

    pa_assert(sf);
    pa_assert(cm);

    pa_zero(sfi);
    if ((sf_errno = sf_command(sf, SFC_GET_CURRENT_SF_INFO, &sfi, sizeof(sfi)))) {
        pa_log_error("sndfile: %s", sf_error_number(sf_errno));
        return -1;
    }

    channels = pa_xnew(int, sfi.channels);
    if (!sf_command(sf, SFC_GET_CHANNEL_MAP_INFO, channels, sizeof(channels[0]) * sfi.channels)) {
        pa_xfree(channels);
        return -1;
    }

    cm->channels = (uint8_t) sfi.channels;
    for (c = 0; c < cm->channels; c++) {
        if (channels[c] <= SF_CHANNEL_MAP_INVALID ||
            (unsigned) channels[c] >= PA_ELEMENTSOF(table)) {
            pa_xfree(channels);
            return -1;
        }

        cm->map[c] = table[channels[c]];
    }

    pa_xfree(channels);

    if (!pa_channel_map_valid(cm))
        return -1;

    return 0;
}

int pa_sndfile_write_channel_map(SNDFILE *sf, pa_channel_map *cm) {
    static const int table[PA_CHANNEL_POSITION_MAX] = {
        [PA_CHANNEL_POSITION_MONO] = SF_CHANNEL_MAP_MONO,

        [PA_CHANNEL_POSITION_FRONT_LEFT] = SF_CHANNEL_MAP_FRONT_LEFT,
        [PA_CHANNEL_POSITION_FRONT_RIGHT] = SF_CHANNEL_MAP_FRONT_RIGHT,
        [PA_CHANNEL_POSITION_FRONT_CENTER] = SF_CHANNEL_MAP_FRONT_CENTER,

        [PA_CHANNEL_POSITION_REAR_CENTER] = SF_CHANNEL_MAP_REAR_CENTER,
        [PA_CHANNEL_POSITION_REAR_LEFT] = SF_CHANNEL_MAP_REAR_LEFT,
        [PA_CHANNEL_POSITION_REAR_RIGHT] = SF_CHANNEL_MAP_REAR_RIGHT,

        [PA_CHANNEL_POSITION_LFE] = SF_CHANNEL_MAP_LFE,

        [PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER] = SF_CHANNEL_MAP_FRONT_LEFT_OF_CENTER,
        [PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER] = SF_CHANNEL_MAP_FRONT_RIGHT_OF_CENTER,

        [PA_CHANNEL_POSITION_SIDE_LEFT] = SF_CHANNEL_MAP_SIDE_LEFT,
        [PA_CHANNEL_POSITION_SIDE_RIGHT] = SF_CHANNEL_MAP_SIDE_RIGHT,

        [PA_CHANNEL_POSITION_AUX0] = -1,
        [PA_CHANNEL_POSITION_AUX1] = -1,
        [PA_CHANNEL_POSITION_AUX2] = -1,
        [PA_CHANNEL_POSITION_AUX3] = -1,
        [PA_CHANNEL_POSITION_AUX4] = -1,
        [PA_CHANNEL_POSITION_AUX5] = -1,
        [PA_CHANNEL_POSITION_AUX6] = -1,
        [PA_CHANNEL_POSITION_AUX7] = -1,
        [PA_CHANNEL_POSITION_AUX8] = -1,
        [PA_CHANNEL_POSITION_AUX9] = -1,
        [PA_CHANNEL_POSITION_AUX10] = -1,
        [PA_CHANNEL_POSITION_AUX11] = -1,
        [PA_CHANNEL_POSITION_AUX12] = -1,
        [PA_CHANNEL_POSITION_AUX13] = -1,
        [PA_CHANNEL_POSITION_AUX14] = -1,
        [PA_CHANNEL_POSITION_AUX15] = -1,
        [PA_CHANNEL_POSITION_AUX16] = -1,
        [PA_CHANNEL_POSITION_AUX17] = -1,
        [PA_CHANNEL_POSITION_AUX18] = -1,
        [PA_CHANNEL_POSITION_AUX19] = -1,
        [PA_CHANNEL_POSITION_AUX20] = -1,
        [PA_CHANNEL_POSITION_AUX21] = -1,
        [PA_CHANNEL_POSITION_AUX22] = -1,
        [PA_CHANNEL_POSITION_AUX23] = -1,
        [PA_CHANNEL_POSITION_AUX24] = -1,
        [PA_CHANNEL_POSITION_AUX25] = -1,
        [PA_CHANNEL_POSITION_AUX26] = -1,
        [PA_CHANNEL_POSITION_AUX27] = -1,
        [PA_CHANNEL_POSITION_AUX28] = -1,
        [PA_CHANNEL_POSITION_AUX29] = -1,
        [PA_CHANNEL_POSITION_AUX30] = -1,
        [PA_CHANNEL_POSITION_AUX31] = -1,

        [PA_CHANNEL_POSITION_TOP_CENTER] = SF_CHANNEL_MAP_TOP_CENTER,

        [PA_CHANNEL_POSITION_TOP_FRONT_LEFT] = SF_CHANNEL_MAP_TOP_FRONT_LEFT,
        [PA_CHANNEL_POSITION_TOP_FRONT_RIGHT] = SF_CHANNEL_MAP_TOP_FRONT_RIGHT,
        [PA_CHANNEL_POSITION_TOP_FRONT_CENTER] = SF_CHANNEL_MAP_TOP_FRONT_CENTER ,

        [PA_CHANNEL_POSITION_TOP_REAR_LEFT] = SF_CHANNEL_MAP_TOP_REAR_LEFT,
        [PA_CHANNEL_POSITION_TOP_REAR_RIGHT] = SF_CHANNEL_MAP_TOP_REAR_RIGHT,
        [PA_CHANNEL_POSITION_TOP_REAR_CENTER] = SF_CHANNEL_MAP_TOP_REAR_CENTER,
    };

    int *channels;
    unsigned c;

    pa_assert(sf);
    pa_assert(cm);

    /* Suppress channel mapping for the obvious cases */
    if (cm->channels == 1 && cm->map[0] == PA_CHANNEL_POSITION_MONO)
        return 0;

    if (cm->channels == 2 &&
        cm->map[0] == PA_CHANNEL_POSITION_FRONT_LEFT &&
        cm->map[1] == PA_CHANNEL_POSITION_FRONT_RIGHT)
        return 0;

    channels = pa_xnew(int, cm->channels);
    for (c = 0; c < cm->channels; c++) {

        if (cm->map[c] < 0 ||
            cm->map[c] >= PA_CHANNEL_POSITION_MAX ||
            table[cm->map[c]] < 0) {
            pa_xfree(channels);
            return -1;
        }

        channels[c] = table[cm->map[c]];
    }

    if (!sf_command(sf, SFC_SET_CHANNEL_MAP_INFO, channels, sizeof(channels[0]) * cm->channels)) {
        pa_xfree(channels);
        return -1;
    }

    pa_xfree(channels);
    return 0;
}

void pa_sndfile_init_proplist(SNDFILE *sf, pa_proplist *p) {

    static const char* table[] = {
        [SF_STR_TITLE] = PA_PROP_MEDIA_TITLE,
        [SF_STR_COPYRIGHT] = PA_PROP_MEDIA_COPYRIGHT,
        [SF_STR_SOFTWARE] = PA_PROP_MEDIA_SOFTWARE,
        [SF_STR_ARTIST] = PA_PROP_MEDIA_ARTIST,
        [SF_STR_COMMENT] = "media.comment",
        [SF_STR_DATE] = "media.date"
    };

    SF_INFO sfi;
    SF_FORMAT_INFO fi;
    int sf_errno;
    unsigned c;

    pa_assert(sf);
    pa_assert(p);

    for (c = 0; c < PA_ELEMENTSOF(table); c++) {
        const char *s;
        char *t;

        if (!table[c])
            continue;

        if (!(s = sf_get_string(sf, c)))
            continue;

        t = pa_utf8_filter(s);
        pa_proplist_sets(p, table[c], t);
        pa_xfree(t);
    }

    pa_zero(sfi);
    if ((sf_errno = sf_command(sf, SFC_GET_CURRENT_SF_INFO, &sfi, sizeof(sfi)))) {
        pa_log_error("sndfile: %s", sf_error_number(sf_errno));
        return;
    }

    pa_zero(fi);
    fi.format = sfi.format;
    if (sf_command(sf, SFC_GET_FORMAT_INFO, &fi, sizeof(fi)) == 0 && fi.name) {
        char *t;

        t = pa_utf8_filter(fi.name);
        pa_proplist_sets(p, "media.format", t);
        pa_xfree(t);
    }
}

pa_sndfile_readf_t pa_sndfile_readf_function(const pa_sample_spec *ss) {
    pa_assert(ss);

    switch (ss->format) {
        case PA_SAMPLE_S16NE:
            return (pa_sndfile_readf_t) sf_readf_short;

        case PA_SAMPLE_S32NE:
        case PA_SAMPLE_S24_32NE:
            return (pa_sndfile_readf_t) sf_readf_int;

        case PA_SAMPLE_FLOAT32NE:
            return (pa_sndfile_readf_t) sf_readf_float;

        case PA_SAMPLE_ULAW:
        case PA_SAMPLE_ALAW:
        case PA_SAMPLE_S24NE:
            return NULL;

        default:
            pa_assert_not_reached();
    }
}

pa_sndfile_writef_t pa_sndfile_writef_function(const pa_sample_spec *ss) {
    pa_assert(ss);

    switch (ss->format) {
        case PA_SAMPLE_S16NE:
            return (pa_sndfile_writef_t) sf_writef_short;

        case PA_SAMPLE_S32NE:
        case PA_SAMPLE_S24_32NE:
            return (pa_sndfile_writef_t) sf_writef_int;

        case PA_SAMPLE_FLOAT32NE:
            return (pa_sndfile_writef_t) sf_writef_float;

        case PA_SAMPLE_ULAW:
        case PA_SAMPLE_ALAW:
        case PA_SAMPLE_S24NE:
            return NULL;

        default:
            pa_assert_not_reached();
    }
}

int pa_sndfile_format_from_string(const char *name) {
    int i, count = 0;

    if (!name[0])
        return -1;

    pa_assert_se(sf_command(NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof(int)) == 0);

    for (i = 0; i < count; i++) {
        SF_FORMAT_INFO fi;
        pa_zero(fi);
        fi.format = i;

        pa_assert_se(sf_command(NULL, SFC_GET_FORMAT_MAJOR, &fi, sizeof(fi)) == 0);

        /* First try to match via full type string */
        if (strcasecmp(name, fi.name) == 0)
            return fi.format;

        /* Then, try to match via the full extension */
        if (strcasecmp(name, fi.extension) == 0)
            return fi.format;

        /* Then, try to match via the start of the type string */
        if (strncasecmp(name, fi.name, strlen(name)) == 0)
            return fi.format;
    }

    return -1;
}

void pa_sndfile_dump_formats(void) {
    int i, count = 0;

    pa_assert_se(sf_command(NULL, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof(int)) == 0);

    for (i = 0; i < count; i++) {
        SF_FORMAT_INFO fi;
        pa_zero(fi);
        fi.format = i;

        pa_assert_se(sf_command(NULL, SFC_GET_FORMAT_MAJOR, &fi, sizeof(fi)) == 0);
        printf("%s\t%s\n", fi.extension, fi.name);
    }
}
