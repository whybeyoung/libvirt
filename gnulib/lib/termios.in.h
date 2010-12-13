/* -*- buffer-read-only: t -*- vi: set ro: */
/* DO NOT EDIT! GENERATED AUTOMATICALLY! */
/* Substitute for and wrapper around <termios.h>.
   Copyright (C) 2010 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef _GL_TERMIOS_H

#if __GNUC__ >= 3
@PRAGMA_SYSTEM_HEADER@
#endif
@PRAGMA_COLUMNS@

/* The include_next requires a split double-inclusion guard.  */
#if @HAVE_TERMIOS_H@
# @INCLUDE_NEXT@ @NEXT_TERMIOS_H@
#endif

#ifndef _GL_TERMIOS_H
#define _GL_TERMIOS_H

#if @GNULIB_TCGETSID@
/* Get pid_t.  */
# include <sys/types.h>
#endif

/* The definitions of _GL_FUNCDECL_RPL etc. are copied here.  */

/* The definition of _GL_WARN_ON_USE is copied here.  */


/* Declare overridden functions.  */

#if @GNULIB_TCGETSID@
/* Return the session ID of the controlling terminal of the current process.
   The argument is a descriptor if this controlling terminal.
   Return -1, with errno set, upon failure.  errno = ENOSYS means that the
   function is unsupported.  */
# if !@HAVE_TCGETSID@
_GL_FUNCDECL_SYS (tcgetsid, pid_t, (int fd));
# endif
_GL_CXXALIAS_SYS (tcgetsid, pid_t, (int fd));
_GL_CXXALIASWARN (tcgetsid);
#elif defined GNULIB_POSIXCHECK
# undef tcgetsid
# if HAVE_RAW_DECL_TCGETSID
_GL_WARN_ON_USE (tcgetsid, "tcgetsid is not portable - "
                 "use gnulib module tcgetsid for portability");
# endif
#endif


#endif /* _GL_TERMIOS_H */
#endif /* _GL_TERMIOS_H */