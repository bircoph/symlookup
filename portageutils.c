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
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "portageutils.h"
#include "symlookup.h"
#include "safemem.h"

#define CONTENTS_NAME "/CONTENTS"
#define CONTENTS_LEN  10 // len + '\0'

/* disables ebuild support */
static void ebuild_disable(void)
{
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

/* Selects only categories which are directories
 * and do not begin with '.' */
static int filter_directory(const struct dirent *const dir)
{
    if (dir->d_type == DT_DIR && dir->d_name[0] != '.')
        return 1;
    else
        return 0;
}

/* Process mmaped file */
static inline void
process_list(const char *const mbuf, const off_t mbuf_len,
             const char *const package_name, const size_t package_len)
{
}

/* builds hash table for files found and searches portage db for them */
void find_ebuilds(const struct str_t *const file)
{
    // nothing to do on empty list
    if (!file->size)
        return;

    /* initialize hash */
    if (!hcreate(file->size * 4/3 + 1))
    {
        if (opt.verb)
            error(0, errno, "error: cannot init file hash table! "
                            "Disabling ebuild support.");
        ebuild_disable();
        return;
    }

    /* initialize portage root DB dir entry */
    struct dirent **category;
    int categories;

    /* open portage root DB dir */
    categories = scandir(opt.portageDB, &category, filter_directory, 0);
    if (categories == -1 || chdir(opt.portageDB) == -1)
    {
        if (opt.verb)
            error(0, errno, "error: cannot open portage root directory %s. "
                            "Disabling ebuild support.", opt.portageDB);
        ebuild_disable();
        // additional cleanups an this stage
        if (categories >= 0)
        {
            for (int i=0; i<categories; i++)
                free(category[i]);
            free(category);
        }
        hdestroy();
        return;
    }

    /* hash found files */
    ENTRY entry;
    for (unsigned long int i=0; i<file->size; i++)
    {
        entry.key = file->str[i];
        entry.data = (void*)i;
        // no errors should be here
        hsearch(entry, ENTER);
    }

    /***** portage DB tree loop data declarations *****/
    struct dirent **package;   // for packages in each category
    int    packages;           // number of packages in category

    char  *contents,           // full name of CONTENTS file
          *tmpstr;             // temporary pointer for string operations
    size_t contents_len = 128, // max pool for length of CONTENTS full file name
           new_len,            // length of new CONTENTS file name
           category_len,       // category name length
           package_len;        // package name length

    int    list;               // file descriptor for listing file
    struct stat list_stat;     // to fstat() listing file
    char  *mbuf;               // mmap buffer

    contents = xmalloc(contents_len);

    /* loop through portage categories */
    for (int i=0; i<categories; i++)
    {
        packages = scandir(category[i]->d_name, &package, filter_directory, 0);
        if (packages == -1)
        {
            if (opt.verb)
                error(0, errno, "error: cannot open portage category %s.",
                         category[i]->d_name);
        }
        else
        {
            category_len = _D_EXACT_NAMLEN(category[i]);

            /* loop through packages/CONTENTS */
            for (int j=0; j<packages; j++)
            {
                package_len = _D_EXACT_NAMLEN(package[j]);
                // new len = category + '/' + package + "/CONTENTS" + '\0'
                new_len = category_len + 1 + package_len + CONTENTS_LEN;
                if (new_len > contents_len)
                    contents = xrealloc(contents, contents_len * 2);

                // construct file name
                memcpy(contents, category[i]->d_name, category_len);
                tmpstr = contents + category_len;
                *tmpstr++ = '/';
                memcpy(tmpstr, package[j]->d_name, package_len);
                tmpstr += package_len;
                memcpy(tmpstr, CONTENTS_NAME, CONTENTS_LEN);

                /* open file */
                list = open(contents, 0, O_RDONLY);
                if (list != -1)
                {
                    // stat to obtain size
                    if (fstat(list, &list_stat))
                    {
                        if (opt.verb)
                            error(0, errno, "error: can't stat file %s, "
                                            "check your portage DB!", contents);
                    }
                    else
                    {
                        // zero size is normal for at least virtual packages
                        if (list_stat.st_size > 0)
                        {
                            /* mmap now */
                            mbuf = mmap(NULL, list_stat.st_size, PROT_READ,
                                        MAP_PRIVATE | MAP_POPULATE | MAP_NORESERVE,
                                        list, 0);
                            if (mmap == MAP_FAILED)
                            {
                                if (opt.verb)
                                    error(0, errno, "error: can't mmap file %s %li bytes long!",
                                             contents, list_stat.st_size);
                            }
                            else
                            {
                                // process CONTENTS file
                                process_list(mbuf, list_stat.st_size, 
                                             contents, category_len + 1 + package_len);
                                if (munmap(mbuf, list_stat.st_size) && opt.verb)
                                    error(0, errno, "warning: can't unmap file %s %li bytes long!",
                                             contents, list_stat.st_size);
                            }

                        }
                    }
                    // check if file is closed correctly
                    if (close(list) && opt.verb)
                            error(0, errno, "warning: can't close file %s", contents);
                }
                else
                {
                    if (opt.verb)
                        error(0, errno, "error: can't open file %s for reading, "
                                        "check your portage DB!", contents);
                }

                // clean memory
                free(package[j]);
            }
            // clean memory
            free(package);
        }
        // clean memory
        free(category[i]);
    }

    // clean memory
    free(contents);
    free(category);
    hdestroy();
}

#endif //HAVE_PORTAGE
