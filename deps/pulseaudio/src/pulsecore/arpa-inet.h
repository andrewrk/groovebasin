#ifndef fooarpa_inethfoo
#define fooarpa_inethfoo

#if defined(HAVE_ARPA_INET_H)

#include <arpa/inet.h>

#elif defined(OS_IS_WIN32)

/* On Windows winsock2.h (here included via pulsecore/socket.h) provides most of the functionality of arpa/inet.h, except for
 * the inet_ntop and inet_pton functions, which are implemented here. */

#include <pulsecore/socket.h>

const char *inet_ntop(int af, const void *src, char *dst, socklen_t cnt);

int inet_pton(int af, const char *src, void *dst);

#endif

#endif
