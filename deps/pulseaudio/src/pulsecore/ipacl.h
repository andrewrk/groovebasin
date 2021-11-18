#ifndef foopulsecoreipaclhfoo
#define foopulsecoreipaclhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

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

typedef struct pa_ip_acl pa_ip_acl;

pa_ip_acl* pa_ip_acl_new(const char *s);
void pa_ip_acl_free(pa_ip_acl *acl);
int pa_ip_acl_check(pa_ip_acl *acl, int fd);

#endif
