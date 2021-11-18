#ifndef foox11prophfoo
#define foox11prophfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2010 Colin Guthrie

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

#include <sys/types.h>

#include <xcb/xcb.h>

void pa_x11_set_prop(xcb_connection_t *xcb, int screen, const char *name, const char *data);
void pa_x11_del_prop(xcb_connection_t *xcb, int screen, const char *name);
char* pa_x11_get_prop(xcb_connection_t *xcb, int screen, const char *name, char *p, size_t l);

#endif
