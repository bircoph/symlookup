/*
 *  ELF and AR files processing code,
 *  symbol detection code
 *  Copyright © 2007-2010 Andrew Savchenko
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

#ifndef SL_SCANELF_H
#define SL_SCANELF_H

/* must take name of ordinary file to access from current directory,
 * full file name from the root of traversal (in order to show it for
 * user), and last name only, it is already returned by fts,
 * so I won't waste CPU time */
void checkfile (const char* const filename,
                const char* const fullfilename,
                const char* const name);

#endif /* SL_SCANELF_H */
