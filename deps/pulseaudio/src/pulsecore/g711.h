#ifndef foog711hfoo
#define foog711hfoo

/* g711.h - include for G711 u-law and a-law conversion routines
**
** Copyright (C) 2001 Chris Bagwell
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/** Copied from sox -- Lennart Poettering */

#include <inttypes.h>

#ifdef FAST_ALAW_CONVERSION
extern uint8_t _st_13linear2alaw[0x2000];
extern int16_t _st_alaw2linear16[256];
#define st_13linear2alaw(sw) (_st_13linear2alaw[(sw + 0x1000)])
#define st_alaw2linear16(uc) (_st_alaw2linear16[uc])
#else
unsigned char st_13linear2alaw(int16_t pcm_val);
int16_t st_alaw2linear16(unsigned char);
#endif

#ifdef FAST_ULAW_CONVERSION
extern uint8_t _st_14linear2ulaw[0x4000];
extern int16_t _st_ulaw2linear16[256];
#define st_14linear2ulaw(sw) (_st_14linear2ulaw[(sw + 0x2000)])
#define st_ulaw2linear16(uc) (_st_ulaw2linear16[uc])
#else
unsigned char st_14linear2ulaw(int16_t pcm_val);
int16_t st_ulaw2linear16(unsigned char);
#endif

#endif
