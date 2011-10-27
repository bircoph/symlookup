/*
 *  rpm utilities
 *  Copyright © 2007-2011 Andrew Savchenko
 *
 *  This file is part of symlookup.
 *
 *  symlookup is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation
 *
 *  symlookup is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License version 3 for more details.
 *
 *  You should have received a copy of the GNU General Public License version 3
 *  along with symlookup. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SL_RPMUTILS_H
#define SL_RPMUTILS_H

#ifdef HAVE_RPM

extern char* prevfile;
extern struct str_t *rpm_arr;

/* find rpm containing file "filename"
   memory must be allocated and freed outside this function;
   return NULL if file doesn't owned by any package */
void rpm_check(const char* const filename, struct str_t* const rpmname);

/* create printable list of rpm names
   memory must be freed by caller;
   pattern is intended for match case */
void listrpm(const char *const filename, const char *const symbolname,
             const char *const pattern);

/* init rpm */
void rpminit();

/* uninit rpm */
void rpmuninit();

#endif //HAVE_RPM

#endif /* SL_RPMUTILS_H */
