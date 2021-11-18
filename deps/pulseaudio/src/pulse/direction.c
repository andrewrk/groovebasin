/***
  This file is part of PulseAudio.

  Copyright 2014 Intel Corporation

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

#include "direction.h"

#include <pulsecore/i18n.h>

int pa_direction_valid(pa_direction_t direction) {
    if (direction != PA_DIRECTION_INPUT
            && direction != PA_DIRECTION_OUTPUT
            && direction != (PA_DIRECTION_INPUT | PA_DIRECTION_OUTPUT))
        return 0;

    return 1;
}

const char *pa_direction_to_string(pa_direction_t direction) {
    pa_init_i18n();

    if (direction == PA_DIRECTION_INPUT)
        return _("input");
    if (direction == PA_DIRECTION_OUTPUT)
        return _("output");
    if (direction == (PA_DIRECTION_INPUT | PA_DIRECTION_OUTPUT))
        return _("bidirectional");

    return _("invalid");
}
