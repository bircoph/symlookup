/*
 *  Results sort and output
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

#ifndef SL_OUTPUT_H
#define SL_OUTPUT_H

extern const char* const str_not_found;

/* sort if required and output results */
void sort_output();

/* output unsorted results for ebuild search */
void ebuild_unsorted_output();

/* initialize output (header, formats) */
void init_output();

#endif /* SL_OUTPUT_H */
