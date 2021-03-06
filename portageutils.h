/*
 *  portage utilities
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

#ifndef SL_PORTAGEUTILS_H
#define SL_PORTAGEUTILS_H

#ifdef HAVE_PORTAGE
#include "symlookup.h"

extern struct str_t *ebuild_arr;

/* builds hash table for files found and searches portage db for them */
void find_ebuilds(const struct str_t *const file);

#endif //HAVE_PORTAGE

#endif /* SL_PORTAGEUTILS_H */
