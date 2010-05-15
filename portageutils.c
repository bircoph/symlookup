/*
 *  portage utilities
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

#ifdef HAVE_PORTAGE

#include <search.h>
#include <error.h>
#include <errno.h>

#include "portageutils.h"
#include "symlookup.h"

// returns 0 on failure, 1 otherwise
static inline int
hash_init(const size_t size)
{
    size_t ret = 1;
    ret = hcreate(size * 5/4 + 1);

    if (!ret)
    {
        if (opt.verb)
            error(0, errno, "error: cannot init file hash table!\n"
                            "Disabling ebuild support.");

        /* remove ebuild from the sort sequence */
        unsigned int found = 0;
        for (unsigned int i = 0; i<opt.sort.cnt; i++)
        {
            if (!found)
            {
                if (opt.sort.seq[i] == mtype.ebuild)
                    found=1;
                continue;
            }
            opt.sort.seq[i-1] = opt.sort.seq[i];
        }
        opt.sort.cnt--;
        // ebuild is the last type now, so no need to reduce match
        // type values of other fields
    }
    return ret;
}

static inline void
hash_files(const struct str_t *const file)
{
}

/* builds hash table for files found and searches portage db for them */
void find_ebuilds(const struct str_t *const file)
{
    // nothing to do on empty list or failed hcreate
    if (!file->size || !hash_init(file->size))
        return;

    hash_files(file);
}

#endif //HAVE_PORTAGE
