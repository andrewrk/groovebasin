/***
  This file is part of PulseAudio.

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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef OS_IS_WIN32

#include <stdlib.h>
#include <stdio.h>

#include <windows.h>
#include <winsock2.h>

extern char *pa_win32_get_toplevel(HANDLE handle);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    WSADATA data;

    switch (fdwReason) {

    case DLL_PROCESS_ATTACH:
        if (!pa_win32_get_toplevel(hinstDLL))
            return FALSE;
        WSAStartup(MAKEWORD(2, 0), &data);
        break;

    case DLL_PROCESS_DETACH:
        WSACleanup();
        break;

    }
    return TRUE;
}

#endif /* OS_IS_WIN32 */
