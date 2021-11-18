#ifndef foosocketserverhfoo
#define foosocketserverhfoo

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
#include <pulse/mainloop-api.h>
#include <pulsecore/iochannel.h>

/* It is safe to destroy the calling socket_server object from the callback */

typedef struct pa_socket_server pa_socket_server;

pa_socket_server* pa_socket_server_new_unix(pa_mainloop_api *m, const char *filename);
pa_socket_server* pa_socket_server_new_ipv4(pa_mainloop_api *m, uint32_t address, uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv4_loopback(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv4_any(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv4_string(pa_mainloop_api *m, const char *name, uint16_t port, bool fallback, const char *tcpwrap_service);
#ifdef HAVE_IPV6
pa_socket_server* pa_socket_server_new_ipv6(pa_mainloop_api *m, const uint8_t address[16], uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv6_loopback(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv6_any(pa_mainloop_api *m, uint16_t port, bool fallback, const char *tcpwrap_service);
pa_socket_server* pa_socket_server_new_ipv6_string(pa_mainloop_api *m, const char *name, uint16_t port, bool fallback, const char *tcpwrap_service);
#endif

void pa_socket_server_unref(pa_socket_server*s);
pa_socket_server* pa_socket_server_ref(pa_socket_server *s);

typedef void (*pa_socket_server_on_connection_cb_t)(pa_socket_server*s, pa_iochannel *io, void *userdata);

void pa_socket_server_set_callback(pa_socket_server*s, pa_socket_server_on_connection_cb_t connection_cb, void *userdata);

char *pa_socket_server_get_address(pa_socket_server *s, char *c, size_t l);

#endif
