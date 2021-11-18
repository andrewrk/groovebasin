#ifndef foopulsecoresockethfoo
#define foopulsecoresockethfoo

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_WINSOCK2_H
#include <winsock2.h>
#include "winerrno.h"

typedef long suseconds_t;

#endif

#ifdef HAVE_WS2TCPIP_H
#include <ws2tcpip.h>
#endif

#endif
