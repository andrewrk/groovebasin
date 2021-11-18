#ifndef foopulsecoreparseaddrhfoo
#define foopulsecoreparseaddrhfoo

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

#include <pulsecore/macro.h>

typedef enum pa_parsed_address_type {
    PA_PARSED_ADDRESS_UNIX,
    PA_PARSED_ADDRESS_TCP4,
    PA_PARSED_ADDRESS_TCP6,
    PA_PARSED_ADDRESS_TCP_AUTO
} pa_parsed_address_type_t;

typedef struct pa_parsed_address {
    pa_parsed_address_type_t type;
    char *path_or_host;
    uint16_t port;
} pa_parsed_address;

int pa_parse_address(const char *a, pa_parsed_address *ret_p);

bool pa_is_ip_address(const char *a);

bool pa_is_ip6_address(const char *a);

#endif
