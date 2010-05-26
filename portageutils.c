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
#include <stdio.h>
#include <string.h>

#include "portageutils.h"
#include "symlookup.h"
#include "safemem.h"

#define CONTENTS_NAME "/CONTENTS"
#define CONTENTS_LEN  10 // len + '\0'

/* 2D array of ebuilds corresponding to file_arr */
struct str_t *ebuild_arr;
extern struct str_t file_arr;

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

/* Process mmaped file.
 * Store "category/package" if file is found in the hash table.
 * ptr -- mmap start;
 * mbuf_end -- mmap end + 1;
 * package_name -- "category/package" without trailing '\0';
 * package_len -- length of "category/package" */
static inline void
process_list(char *ptr, const char *const mbuf_end,
             const char *const package_name, const size_t package_len)
{
    char *begin;
    ENTRY request, *result;
    struct str_t *ebuild;  // current element of ebuild array

    for (; ptr < mbuf_end; ptr++)
    {
        begin = ptr;
        // search for end of line
        for (; ptr < mbuf_end; ptr++)
            if (*ptr == '\n')
                break;

        /* minimum length for valid string of interest is 49:
         * "obj X 617ae644c40ec045954426e0702d936e 1260230791"
         * 3:1:name:1:32:1:10\n */

        // continue on bad length or header
        if (ptr-begin < 49 || strncmp(begin,"obj",3))
            continue;

        // denote end of file name
        *(ptr-44) = '\0';

        /* check match */
        request.key = begin+4;
        result = hsearch(request, FIND);
        if (!result)
            continue;

        /* add ebuild to ebuilds array by index corresponding to file array */
        ebuild = &ebuild_arr[(unsigned long)(result->data)];
        // grow ebuild array
        ebuild->str = xrealloc(ebuild->str, sizeof(char*) * (ebuild->size + 1));
        // allocate and fill new string
        ebuild->str[ebuild->size] = xmalloc(package_len + 1);
        memcpy(ebuild->str[ebuild->size], package_name, package_len);
        ebuild->str[ebuild->size][package_len] = '\0';
        // grow ebuild array
        ebuild->size++;
    }
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

    /* Initialize array for found ebuilds */
    size_t len_earr = file_arr.size * sizeof(struct str_t);
    ebuild_arr = xmalloc(len_earr);
    memset(ebuild_arr, 0, len_earr);

    if (opt.verb == V_VERBOSE)
        puts("--> Searching portage database");

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
                            mbuf = mmap(NULL, list_stat.st_size, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_POPULATE, list, 0);
                            if (mmap == MAP_FAILED)
                            {
                                if (opt.verb)
                                    error(0, errno, "error: can't mmap file %s %li bytes long!",
                                             contents, list_stat.st_size);
                            }
                            else
                            {
                                // process CONTENTS file
                                process_list(mbuf, mbuf + list_stat.st_size, 
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
