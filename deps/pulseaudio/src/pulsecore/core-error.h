#ifndef foocoreerrorhfoo
#define foocoreerrorhfoo

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

#include <pulse/cdecl.h>

/** \file
 * Error management */

PA_C_DECL_BEGIN

/** A wrapper around the standard strerror() function that converts the
 * string to UTF-8. The function is thread safe but the returned string is
 * only guaranteed to exist until the thread exits or pa_cstrerror() is
 * called again from the same thread. */
const char* pa_cstrerror(int errnum);

PA_C_DECL_END

#endif
