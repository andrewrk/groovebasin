#  This file is part of PulseAudio.
#
#  Copyright 2010 Lennart Poettering
#  Copyright 2010 Wim Taymans <wim.taymans@collabora.co.uk>
#  Copyright 2010 Arun Raghavan <arun.raghavan@collabora.co.uk>
#
#  PulseAudio is free software; you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as published
#  by the Free Software Foundation; either version 2.1 of the License,
#  or (at your option) any later version.
#
#  PulseAudio is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
#  General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public License
#  along with PulseAudio; if not, see <http://www.gnu.org/licenses/>.

# S16NE 1- and 2-channel volume scaling work as follows:
#
#     params: samples s (signed 16-bit), volume v (signed 32-bit < 2^31)
#
#                  32           16                 0 (type of operation)
#         sample =               |      sample     | (signed)
#              s = |      0      |      sample     | (unsigned)
#
#     if (sample < 0)
#          signc = |      0      |      0xffff     | (unsigned)
#     else
#          signc = |      0      |        0        | (unsigned)
#
#     if (sample < 0)
#             ml = |      0      | -((s*vl) >> 16) | (unsigned)
#     else
#             ml = |      0      |   (s*vl) >> 16  | (unsigned)
#
#             vh =               |      v >> 16    | (signed, but sign bit is always zero
#                                                     since PA_VOLUME_MAX is 0x0fffffff)
#             mh = |         (s * vh) >> 16        | (signed)
#             ml = |           ml + mh             | (signed)
#         sample =               |    (ml >> 16)   | (signed, saturated)

.function pa_volume_s16ne_orc_1ch
.dest 2 samples int16_t
.param 4 vols int32_t
.temp 4 v
.temp 2 vh
.temp 4 s
.temp 4 mh
.temp 4 ml
.temp 4 signc

loadpl v, vols
convuwl s, samples
x2 cmpgtsw signc, 0, s
x2 andw signc, signc, v
x2 mulhuw ml, s, v
subl ml, ml, signc
convhlw vh, v
mulswl mh, samples, vh
addl ml, ml, mh
convssslw samples, ml

.function pa_volume_s16ne_orc_2ch
.dest 4 samples int16_t
.longparam 8 vols
.temp 8 v
.temp 4 vh
.temp 8 s
.temp 8 mh
.temp 8 ml
.temp 8 signc

loadpq v, vols
x2 convuwl s, samples
x4 cmpgtsw signc, 0, s
x4 andw signc, signc, v
x4 mulhuw ml, s, v
x2 subl ml, ml, signc
x2 convhlw vh, v
x2 mulswl mh, samples, vh
x2 addl ml, ml, mh
x2 convssslw samples, ml
