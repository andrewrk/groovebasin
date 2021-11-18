#ifndef foopulsei18nhfoo
#define foopulsei18nhfoo

/***
  This file is part of PulseAudio.

  Copyright 2008 Lennart Poettering

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

#include <pulse/cdecl.h>

PA_C_DECL_BEGIN

#ifdef ENABLE_NLS

#if !defined(GETTEXT_PACKAGE)
#error "Something is very wrong here, config.h needs to be included first"
#endif

#include <libintl.h>

#define _(String) dgettext(GETTEXT_PACKAGE, String)
#ifdef gettext_noop
#define N_(String) gettext_noop(String)
#else
#define N_(String) (String)
#endif

#else /* NLS is disabled */

#define _(String) (String)
#define N_(String) (String)
#define textdomain(String) (String)
#define gettext(String) (String)
#define dgettext(Domain,String) (String)
#define dcgettext(Domain,String,Type) (String)
#define ngettext(SingularString,PluralString,N) (PluralString)
#define dngettext(Domain,SingularString,PluralString,N) (PluralString)
#define dcngettext(Domain,SingularString,PluralString,N,Type) (PluralString)
#define bindtextdomain(Domain,Directory) (Domain)
#define bind_textdomain_codeset(Domain,Codeset) (Codeset)

#endif /* ENABLE_NLS */

void pa_init_i18n(void);

PA_C_DECL_END

#endif
