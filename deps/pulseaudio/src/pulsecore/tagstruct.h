#ifndef footagstructhfoo
#define footagstructhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.
***/

#include <inttypes.h>
#include <sys/types.h>
#include <sys/time.h>

#include <pulse/sample.h>
#include <pulse/format.h>
#include <pulse/channelmap.h>
#include <pulse/volume.h>
#include <pulse/proplist.h>

#include <pulsecore/macro.h>

typedef struct pa_tagstruct pa_tagstruct;

/* Due to a stupid design flaw, proplists may only be at the END of a
 * packet or not before a STRING! Don't forget that! We can't really
 * fix this without breaking compat. */

enum {
    PA_TAG_INVALID = 0,
    PA_TAG_STRING = 't',
    PA_TAG_STRING_NULL = 'N',
    PA_TAG_U32 = 'L',
    PA_TAG_U8 = 'B',
    PA_TAG_U64 = 'R',
    PA_TAG_S64 = 'r',
    PA_TAG_SAMPLE_SPEC = 'a',
    PA_TAG_ARBITRARY = 'x',
    PA_TAG_BOOLEAN_TRUE = '1',
    PA_TAG_BOOLEAN_FALSE = '0',
    PA_TAG_BOOLEAN = PA_TAG_BOOLEAN_TRUE,
    PA_TAG_TIMEVAL = 'T',
    PA_TAG_USEC = 'U'  /* 64bit unsigned */,
    PA_TAG_CHANNEL_MAP = 'm',
    PA_TAG_CVOLUME = 'v',
    PA_TAG_PROPLIST = 'P',
    PA_TAG_VOLUME = 'V',
    PA_TAG_FORMAT_INFO = 'f',
};

pa_tagstruct *pa_tagstruct_new(void);
pa_tagstruct *pa_tagstruct_new_fixed(const uint8_t* data, size_t length);
void pa_tagstruct_free(pa_tagstruct*t);

int pa_tagstruct_eof(pa_tagstruct*t);
const uint8_t* pa_tagstruct_data(pa_tagstruct*t, size_t *l);

void pa_tagstruct_put(pa_tagstruct *t, ...);

void pa_tagstruct_puts(pa_tagstruct*t, const char *s);
void pa_tagstruct_putu8(pa_tagstruct*t, uint8_t c);
void pa_tagstruct_putu32(pa_tagstruct*t, uint32_t i);
void pa_tagstruct_putu64(pa_tagstruct*t, uint64_t i);
void pa_tagstruct_puts64(pa_tagstruct*t, int64_t i);
void pa_tagstruct_put_sample_spec(pa_tagstruct *t, const pa_sample_spec *ss);
void pa_tagstruct_put_arbitrary(pa_tagstruct*t, const void *p, size_t length);
void pa_tagstruct_put_boolean(pa_tagstruct*t, bool b);
void pa_tagstruct_put_timeval(pa_tagstruct*t, const struct timeval *tv);
void pa_tagstruct_put_usec(pa_tagstruct*t, pa_usec_t u);
void pa_tagstruct_put_channel_map(pa_tagstruct *t, const pa_channel_map *map);
void pa_tagstruct_put_cvolume(pa_tagstruct *t, const pa_cvolume *cvolume);
void pa_tagstruct_put_proplist(pa_tagstruct *t, const pa_proplist *p);
void pa_tagstruct_put_volume(pa_tagstruct *t, pa_volume_t volume);
void pa_tagstruct_put_format_info(pa_tagstruct *t, const pa_format_info *f);

int pa_tagstruct_get(pa_tagstruct *t, ...);

int pa_tagstruct_gets(pa_tagstruct*t, const char **s);
int pa_tagstruct_getu8(pa_tagstruct*t, uint8_t *c);
int pa_tagstruct_getu32(pa_tagstruct*t, uint32_t *i);
int pa_tagstruct_getu64(pa_tagstruct*t, uint64_t *i);
int pa_tagstruct_gets64(pa_tagstruct*t, int64_t *i);
int pa_tagstruct_get_sample_spec(pa_tagstruct *t, pa_sample_spec *ss);
int pa_tagstruct_get_arbitrary(pa_tagstruct *t, const void **p, size_t length);
int pa_tagstruct_get_boolean(pa_tagstruct *t, bool *b);
int pa_tagstruct_get_timeval(pa_tagstruct*t, struct timeval *tv);
int pa_tagstruct_get_usec(pa_tagstruct*t, pa_usec_t *u);
int pa_tagstruct_get_channel_map(pa_tagstruct *t, pa_channel_map *map);
int pa_tagstruct_get_cvolume(pa_tagstruct *t, pa_cvolume *v);
int pa_tagstruct_get_proplist(pa_tagstruct *t, pa_proplist *p);
int pa_tagstruct_get_volume(pa_tagstruct *t, pa_volume_t *v);
int pa_tagstruct_get_format_info(pa_tagstruct *t, pa_format_info *f);

#endif
