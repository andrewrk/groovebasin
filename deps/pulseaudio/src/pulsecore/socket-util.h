#ifndef foosocketutilhfoo
#define foosocketutilhfoo

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

#include <sys/types.h>

#include <pulsecore/socket.h>
#include <pulsecore/macro.h>

void pa_socket_peer_to_string(int fd, char *c, size_t l);

void pa_make_socket_low_delay(int fd);
void pa_make_tcp_socket_low_delay(int fd);
void pa_make_udp_socket_low_delay(int fd);

int pa_socket_set_sndbuf(int fd, size_t l);
int pa_socket_set_rcvbuf(int fd, size_t l);

int pa_unix_socket_is_stale(const char *fn);
int pa_unix_socket_remove_stale(const char *fn);

bool pa_socket_address_is_local(const struct sockaddr *sa);
bool pa_socket_is_local(int fd);

#endif
