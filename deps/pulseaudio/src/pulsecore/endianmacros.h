#ifndef fooendianmacroshfoo
#define fooendianmacroshfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

#include <inttypes.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif

#ifdef HAVE_BYTESWAP_H
#define PA_INT16_SWAP(x) ((int16_t) bswap_16((uint16_t) x))
#define PA_UINT16_SWAP(x) ((uint16_t) bswap_16((uint16_t) x))
#define PA_INT32_SWAP(x) ((int32_t) bswap_32((uint32_t) x))
#define PA_UINT32_SWAP(x) ((uint32_t) bswap_32((uint32_t) x))
#else
#define PA_INT16_SWAP(x) ( (int16_t) ( ((uint16_t) (x) >> 8) | ((uint16_t) (x) << 8) ) )
#define PA_UINT16_SWAP(x) ( (uint16_t) ( ((uint16_t) (x) >> 8) | ((uint16_t) (x) << 8) ) )
#define PA_INT32_SWAP(x) ( (int32_t) ( ((uint32_t) (x) >> 24) | ((uint32_t) (x) << 24) | (((uint32_t) (x) & 0xFF00) << 8) | ((((uint32_t) (x)) >> 8) & 0xFF00) ) )
#define PA_UINT32_SWAP(x) ( (uint32_t) ( ((uint32_t) (x) >> 24) | ((uint32_t) (x) << 24) | (((uint32_t) (x) & 0xFF00) << 8) | ((((uint32_t) (x)) >> 8) & 0xFF00) ) )
#endif

static inline uint32_t PA_READ24BE(const uint8_t *p) {
    return
        ((uint32_t) p[0] << 16) |
        ((uint32_t) p[1] << 8) |
        ((uint32_t) p[2]);
}

static inline uint32_t PA_READ24LE(const uint8_t *p) {
    return
        ((uint32_t) p[2] << 16) |
        ((uint32_t) p[1] << 8) |
        ((uint32_t) p[0]);
}

static inline void PA_WRITE24BE(uint8_t *p, uint32_t u) {
    p[0] = (uint8_t) (u >> 16);
    p[1] = (uint8_t) (u >> 8);
    p[2] = (uint8_t) u;
}

static inline void PA_WRITE24LE(uint8_t *p, uint32_t u) {
    p[2] = (uint8_t) (u >> 16);
    p[1] = (uint8_t) (u >> 8);
    p[0] = (uint8_t) u;
}

static inline float PA_READ_FLOAT32RE(const void *p) {
    union {
        float f;
        uint32_t u;
    } t;

    t.u = PA_UINT32_SWAP(*(uint32_t *) p);
    return t.f;
}

static inline void PA_WRITE_FLOAT32RE(void *p, float x) {
    union {
        float f;
        uint32_t u;
    } t;

    t.f = x;
    *(uint32_t *) p = PA_UINT32_SWAP(t.u);
}

#define PA_MAYBE_INT16_SWAP(c,x) ((c) ? PA_INT16_SWAP(x) : (x))
#define PA_MAYBE_UINT16_SWAP(c,x) ((c) ? PA_UINT16_SWAP(x) : (x))

#define PA_MAYBE_INT32_SWAP(c,x) ((c) ? PA_INT32_SWAP(x) : (x))
#define PA_MAYBE_UINT32_SWAP(c,x) ((c) ? PA_UINT32_SWAP(x) : (x))

#ifdef WORDS_BIGENDIAN
 #define PA_INT16_FROM_LE(x) PA_INT16_SWAP(x)
 #define PA_INT16_FROM_BE(x) ((int16_t)(x))

 #define PA_INT16_TO_LE(x) PA_INT16_SWAP(x)
 #define PA_INT16_TO_BE(x) ((int16_t)(x))

 #define PA_UINT16_FROM_LE(x) PA_UINT16_SWAP(x)
 #define PA_UINT16_FROM_BE(x) ((uint16_t)(x))

 #define PA_UINT16_TO_LE(x) PA_UINT16_SWAP(x)
 #define PA_UINT16_TO_BE(x) ((uint16_t)(x))

 #define PA_INT32_FROM_LE(x) PA_INT32_SWAP(x)
 #define PA_INT32_FROM_BE(x) ((int32_t)(x))

 #define PA_INT32_TO_LE(x) PA_INT32_SWAP(x)
 #define PA_INT32_TO_BE(x) ((int32_t)(x))

 #define PA_UINT32_FROM_LE(x) PA_UINT32_SWAP(x)
 #define PA_UINT32_FROM_BE(x) ((uint32_t)(x))

 #define PA_UINT32_TO_LE(x) PA_UINT32_SWAP(x)
 #define PA_UINT32_TO_BE(x) ((uint32_t)(x))

 #define PA_READ24NE(x) PA_READ24BE(x)
 #define PA_WRITE24NE(x,y) PA_WRITE24BE((x),(y))

 #define PA_READ24RE(x) PA_READ24LE(x)
 #define PA_WRITE24RE(x,y) PA_WRITE24LE((x),(y))
#else
 #define PA_INT16_FROM_LE(x) ((int16_t)(x))
 #define PA_INT16_FROM_BE(x) PA_INT16_SWAP(x)

 #define PA_INT16_TO_LE(x) ((int16_t)(x))
 #define PA_INT16_TO_BE(x) PA_INT16_SWAP(x)

 #define PA_UINT16_FROM_LE(x) ((uint16_t)(x))
 #define PA_UINT16_FROM_BE(x) PA_UINT16_SWAP(x)

 #define PA_UINT16_TO_LE(x) ((uint16_t)(x))
 #define PA_UINT16_TO_BE(x) PA_UINT16_SWAP(x)

 #define PA_INT32_FROM_LE(x) ((int32_t)(x))
 #define PA_INT32_FROM_BE(x) PA_INT32_SWAP(x)

 #define PA_INT32_TO_LE(x) ((int32_t)(x))
 #define PA_INT32_TO_BE(x) PA_INT32_SWAP(x)

 #define PA_UINT32_FROM_LE(x) ((uint32_t)(x))
 #define PA_UINT32_FROM_BE(x) PA_UINT32_SWAP(x)

 #define PA_UINT32_TO_LE(x) ((uint32_t)(x))
 #define PA_UINT32_TO_BE(x) PA_UINT32_SWAP(x)

 #define PA_READ24NE(x) PA_READ24LE(x)
 #define PA_WRITE24NE(x,y) PA_WRITE24LE((x),(y))

 #define PA_READ24RE(x) PA_READ24BE(x)
 #define PA_WRITE24RE(x,y) PA_WRITE24BE((x),(y))
#endif

#endif
