#ifndef foousergrouphfoo
#define foousergrouphfoo

/***
  This file is part of PulseAudio.

  Copyright 2009 Ted Percival

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

#include <sys/types.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#ifdef HAVE_GRP_H

struct group *pa_getgrgid_malloc(gid_t gid);
void pa_getgrgid_free(struct group *grp);

struct group *pa_getgrnam_malloc(const char *name);
void pa_getgrnam_free(struct group *group);

#endif /* HAVE_GRP_H */

#ifdef HAVE_PWD_H

struct passwd *pa_getpwuid_malloc(uid_t uid);
void pa_getpwuid_free(struct passwd *passwd);

struct passwd *pa_getpwnam_malloc(const char *name);
void pa_getpwnam_free(struct passwd *passwd);

#endif /* HAVE_PWD_H */

#endif /* foousergrouphfoo */
