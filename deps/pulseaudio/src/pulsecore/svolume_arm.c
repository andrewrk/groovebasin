/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2009 Wim Taymans <wim.taymans@collabora.co.uk>

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

#include <pulsecore/random.h>
#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>

#include "cpu-arm.h"

#include "sample-util.h"

#if defined (__arm__) && defined (HAVE_ARMV6)

#define MOD_INC() \
    " subs  r0, r6, %2              \n\t" \
    " itt cs                        \n\t" \
    " addcs r0, %1                  \n\t" \
    " movcs r6, r0                  \n\t"

static pa_do_volume_func_t _volume_ref;

static void pa_volume_s16ne_arm(int16_t *samples, const int32_t *volumes, unsigned channels, unsigned length) {
    /* Channels must be at least 4, and always a multiple of the original number.
     * This is also the max amount we overread the volume array, which should
     * have enough padding. */
    const int32_t *ve = volumes + (channels == 3 ? 6 : PA_MAX (4U, channels));
    unsigned rem = PA_ALIGN((size_t) samples) - (size_t) samples;

    /* Make sure we're word-aligned, else performance _really_ sucks */
    if (rem) {
        _volume_ref(samples, volumes, channels, rem < length ? rem : length);

        if (rem < length) {
            length -= rem;
            samples += rem / sizeof(*samples);
        } else
            return; /* we're done */
    }

    __asm__ __volatile__ (
        " mov r6, %4                      \n\t" /* r6 = volumes + rem */
        " mov %3, %3, LSR #1              \n\t" /* length /= sizeof (int16_t) */

        " cmp %3, #4                      \n\t" /* check for 4+ samples */
        " blt 2f                          \n\t"

        /* See final case for how the multiplication works */

        "1:                               \n\t"
        " ldrd r2, [r6], #8               \n\t" /* 4 samples at a time */
        " ldrd r4, [r6], #8               \n\t"
        " ldrd r0, [%0]                   \n\t"

#ifdef WORDS_BIGENDIAN
        " smulwt r2, r2, r0               \n\t"
        " smulwb r3, r3, r0               \n\t"
        " smulwt r4, r4, r1               \n\t"
        " smulwb r5, r5, r1               \n\t"
#else
        " smulwb r2, r2, r0               \n\t"
        " smulwt r3, r3, r0               \n\t"
        " smulwb r4, r4, r1               \n\t"
        " smulwt r5, r5, r1               \n\t"
#endif

        " ssat r2, #16, r2                \n\t"
        " ssat r3, #16, r3                \n\t"
        " ssat r4, #16, r4                \n\t"
        " ssat r5, #16, r5                \n\t"

#ifdef WORDS_BIGENDIAN
        " pkhbt r0, r3, r2, LSL #16       \n\t"
        " pkhbt r1, r5, r4, LSL #16       \n\t"
#else
        " pkhbt r0, r2, r3, LSL #16       \n\t"
        " pkhbt r1, r4, r5, LSL #16       \n\t"
#endif
        " strd  r0, [%0], #8              \n\t"

        MOD_INC()

        " subs %3, %3, #4                 \n\t"
        " cmp %3, #4                      \n\t"
        " bge 1b                          \n\t"

        "2:                               \n\t"
        " cmp %3, #2                      \n\t"
        " blt 3f                          \n\t"

        " ldrd r2, [r6], #8               \n\t" /* 2  samples at a time */
        " ldr  r0, [%0]                   \n\t"

#ifdef WORDS_BIGENDIAN
        " smulwt r2, r2, r0               \n\t"
        " smulwb r3, r3, r0               \n\t"
#else
        " smulwb r2, r2, r0               \n\t"
        " smulwt r3, r3, r0               \n\t"
#endif

        " ssat r2, #16, r2                \n\t"
        " ssat r3, #16, r3                \n\t"

#ifdef WORDS_BIGENDIAN
        " pkhbt r0, r3, r2, LSL #16       \n\t"
#else
        " pkhbt r0, r2, r3, LSL #16       \n\t"
#endif
        " str  r0, [%0], #4               \n\t"

        MOD_INC()

        " subs %3, %3, #2                 \n\t"

        "3:                               \n\t" /* check for odd # of samples */
        " cmp %3, #1                      \n\t"
        " bne 4f                          \n\t"

        " ldr  r0, [r6], #4               \n\t" /* r0 = volume */
        " ldrh r2, [%0]                   \n\t" /* r2 = sample */

        " smulwb r0, r0, r2               \n\t" /* r0 = (r0 * r2) >> 16 */
        " ssat r0, #16, r0                \n\t" /* r0 = PA_CLAMP(r0, 0x7FFF) */

        " strh r0, [%0], #2               \n\t" /* sample = r0 */

        "4:                               \n\t"

        : "+r" (samples), "+r" (volumes), "+r" (ve), "+r" (length)
        : "r" (volumes + ((rem / sizeof(*samples)) % channels))
        : "r6", "r5", "r4", "r3", "r2", "r1", "r0", "cc"
    );
}

#endif /* defined (__arm__) && defined (HAVE_ARMV6) */

void pa_volume_func_init_arm(pa_cpu_arm_flag_t flags) {
#if defined (__arm__) && defined (HAVE_ARMV6)
    pa_log_info("Initialising ARM optimized volume functions.");

    if (!_volume_ref)
        _volume_ref = pa_get_volume_func(PA_SAMPLE_S16NE);

    pa_set_volume_func(PA_SAMPLE_S16NE, (pa_do_volume_func_t) pa_volume_s16ne_arm);
#endif /* defined (__arm__) && defined (HAVE_ARMV6) */
}
