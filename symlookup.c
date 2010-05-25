/*
 *  symlookup main
 *  Copyright © 2007-2010 Andrew Savchenko
 *  Project created 04.03.2007 16:50:05 MSK (GMT+3)
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <fts.h>
#include <string.h>
#include <gelf.h>
#include <regex.h>
#include <search.h>
#include <sys/stat.h>
#include <unistd.h>

#include "symlookup.h"
#include "parser.h"
#include "safemem.h"
#include "scanelf.h"
#include "rpmutils.h"
#include "output.h"
#include "portageutils.h"

const size_t reg_error_str_len = 512;
char *reg_error_str = NULL;
//flag for found matches in the case of no sort
unsigned int matches_found = 0;

//use different compare function depend upon case sensitivity
int (*compare_func) (const char*, const char*) = strcmp;

/* structure for string array */
struct str_t
    sp       = {0, NULL}, //all search pathes (string array)
    file_arr = {0, NULL}; //matched files

/* array for matches ((char*)[4]):
   user-provided symbols
   matched files
   matched rpm
   matched ebuild */

/* match array */
struct match_arr_t match_arr = {0, NULL};

/* structure for all symbol names */
struct sym_arr symbol = {
    .size = 0,
    .match_count = NULL,
    .str    = NULL,
    .regstr = NULL,
    .match  = NULL
};

/* our options, mask flags above are used;
   unsigned int is used for speed purposes
   setup defaults */
struct opt_t opt = {
    .so   = 1,
    .ar   = 0,
    .dp   = 1,
    .ext  = 1,
    .cas  = 0,
    .tbl  = 0,
    .hdr  = 0,
#ifdef HAVE_RPM
    .rpm  = 0,
    .rpmroot = NULL,
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
    .ebuild    = 0,
    .portageDB = NULL,
#endif //HAVE_PORTAGE
    .verb = V_NORMAL,
    .re   = 0,
    .fts  = FTS_PHYSICAL,
    { /* sort */
        .cnt     = 0,
        .seq     = {0,0
#ifdef HAVE_RPM
                   ,0
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
                   ,0
#endif //HAVE_PORTAGE
                   },
        .match   = 0
    }
};
/* decrease M_SAVEMEM by a number of types we can save
 * in the best case*/
#if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
    unsigned int M_SAVEMEM = M_TYPES - 2;
#elif (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
    unsigned int M_SAVEMEM = M_TYPES - 1;
#else
    const unsigned int M_SAVEMEM = M_TYPES;
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))

/* match types */
struct mtype_t mtype = {
    .symbol = 0,
    .file = 1
};

/* free path array */
static inline void free_unused()
{
#ifdef HAVE_RPM
    //free rpm strings
    if (opt.sort.cnt)
        free(prevfile);

    /* we don't need rpm storage anymore if
       we do not use sorting */
    if (opt.rpm && !opt.sort.cnt
#ifdef HAVE_PORTAGE
            // do not free rpm array if ebuild search is engaged,
            // because immediate output is not possible in this case
            && !opt.ebuild
#endif //HAVE_PORTAGE
            ) {
        free_str(rpm_arr);
        free(rpm_arr);
    }
#endif //HAVE_RPM
    free_str(&sp);
}

/* free compiled regexp array */
static inline void free_regstr()
{
    for (unsigned int i=0; i < symbol.size; i++)
        regfree(&symbol.regstr[i]);
    free(symbol.regstr);
}

/* matched symbol manipulation */
void do_match(const unsigned int i, const char* const filename,
                                    const char* const symbolname)
{
    //don't sort => print immediately
    if (!opt.sort.cnt)
#ifdef HAVE_PORTAGE
    // due to ebuild database structure it is too expensive
    // to query ebuilds on the fly
    if (!opt.ebuild)
#endif //HAVE_PORTAGE
    {
#ifdef HAVE_RPM
        if (opt.rpm) //engage rpm support
            listrpm(filename, symbolname, NULL);
        else
#endif //HAVE_RPM
            printf(outfmt, filename, symbolname);
        matches_found = 1;
        return;
    }
    /* add new match to match_arr */
    match_arr.match = xrealloc(match_arr.match,
                               sizeof(char**) * (match_arr.count + 1));
    //allocate memory for match data
    match_arr.match[match_arr.count] = xmalloc(sizeof(char*) * M_SAVEMEM);

    static char **match;
    match = match_arr.match[match_arr.count];

    if (opt.re || opt.cas)
        match[mtype.symbol] = alloc_str(symbolname);
    else
        // In the case of exact, case insensitive match
        // we do not need to allocate new string, since
        // matched symbolname is by definition equal to
        // the user-requested symbol string.
        match[mtype.symbol] = symbol.str[i];

    /* It is possible to save some memory for multiple
       single file matches by means of small memory overhead
       for single ones and some additional CPU usage.
       Though, the most critical (at least for the first run)
       is i/o bandwidth, so I decided to do this:
       file name can be remembered only once %-) */

    if (!file_arr.size ||
        strcmp(file_arr.str[file_arr.size-1], filename)) {
        //^ reject to collate when no files are recorded
        //add new filename:
        grow_str(&file_arr, filename);

        //the same separate allocation as for filename,
        //but str_t itself
#ifdef HAVE_RPM
        if (opt.rpm) {
            rpm_arr = xrealloc(rpm_arr, sizeof(struct str_t) * file_arr.size);
            rpm_check(filename, &rpm_arr[file_arr.size-1]);
        }
#endif //HAVE_RPM
    }

    match[mtype.file] = file_arr.str[file_arr.size-1];

#ifdef HAVE_RPM
    static char *const str_rpmnf = "<rpm not found>";
    if (opt.rpm) {
        static struct str_t *rpmname;
        rpmname = &rpm_arr[file_arr.size-1];
        //str_rpmnf <=> no match message
        match[mtype.rpm] = (rpmname->size)? rpmname->str[0] : str_rpmnf;

        /* unroll several rpm matches to independent match results,
           so configurable sort can be done easly */
        if (rpmname->size > 1) {
            /* add new matches to match_arr */
            match_arr.match = xrealloc(match_arr.match,
                                       sizeof(char**) * (match_arr.count + rpmname->size));

            for (unsigned int j = 1; j < rpmname->size; j++) {
                match_arr.count++;
                //allocate memory for match data
                match_arr.match[match_arr.count] = xmalloc(sizeof(char*) * M_SAVEMEM);

                match_arr.match[match_arr.count][mtype.symbol]  = match[mtype.symbol];
                match_arr.match[match_arr.count][mtype.file] = match[mtype.file];
                match_arr.match[match_arr.count][mtype.rpm]  = rpmname->str[j];
            }
        }
    }
#endif //HAVE_RPM

    /* add link to new match to symbol array;
       required only if we want to remember
       relationship with original pattern */
    if (opt.sort.match)
    {
        symbol.match[i] = xrealloc(symbol.match[i],
                                   sizeof(char***) * (symbol.match_count[i] + 1));
        symbol.match[i][symbol.match_count[i]] = match;
        symbol.match_count[i]++;

#ifdef HAVE_RPM
        //multiple rpm match leads to several formal matches
        if (opt.rpm)
        {
            static unsigned int rpmcount;
            rpmcount = rpm_arr[file_arr.size-1].size;
            if (rpmcount > 1)
            {
                symbol.match[i] = xrealloc(symbol.match[i],
                                           sizeof(char***) * (symbol.match_count[i] + rpmcount));
                //remember additional matches downwards
                for (unsigned int j = 1; j < rpmcount; j++)
                {
                    symbol.match[i][symbol.match_count[i]] = match_arr.match[match_arr.count - j];
                    symbol.match_count[i]++;
                }
            }
        }
#endif //HAVE_RPM
    }
    match_arr.count++;
}

/* comparison function for uniq_id tree */
int compare_id(register const void* const a, register const void* const b)
{
    const unsigned long long x = *(const unsigned long long* const)a;
    const unsigned long long y = *(const unsigned long long* const)b;
    return (x > y) - (x < y);
}

/* free memory for uniq_id tree element */
void free_id(register void *const id)
{
    free(id);
}

/* Scan file tree using fts.

   We must ensure that physical files are not duplicate,
   since fts doesn't do this completely.
   Unique file id is device_id in the high 32-bit word 
   and inode_id in the low 32-bit word.
   So, unique file id is unsigned long long. 
   They are stored using balanced binary beta-tree */
static inline void fts_scan()
{
    FTS *ftsp;      //pointer to fts directory hierarchy
    FTSENT *entry;  //fts entry which depict file

    // fts_open return value isn't defined in case of errors,
    // we must check by errno 8-/
    errno=0;
    // create fts hierarchy
    ftsp = fts_open(sp.str, opt.fts, NULL);
    if (errno) {
        //save errno
        int err = errno;
        error(0,0,"fatal: invalid search path detected");
        if (opt.dp)
            error(0,0,"Check your system ld configuration at /etc/ld.so.conf and/or\n"
                      "$LD_RUN_PATH, $LD_LIBRARY_PATH, $DT_RUNPATH or $DT_RPATH environmental variables.");
        else
            error(0,0,"Check search path command line option (-p).");

        error(0,0,"Current search path array:");
        //the last element is NULL
        for (int i=0; i < sp.size-1; i++)
            error(0,0,"path[%i]='%s'",i,sp.str[i]);

        error(ERR_FTS, err, "fatal: cannot initialize file search hierarchy");
    }
    /* binary tree data */
    void *tree_root = NULL;         // pointer to id tree root pointer
    unsigned long long *file_id;    // uniq id of physical file
    struct stat *statp;             // stat structure of file in question
    void *leaf;                     // leaf in the tree
    unsigned int need_alloc = 1;    // flag for allocation memory required;

    if (opt.verb == V_VERBOSE)
        puts("--> Iterating search tree");

    /* iterate through file hierarchy */
    while ((entry = fts_read(ftsp)))
    {   //check for error conditions
        if (opt.verb)
            switch (entry->fts_info) {
                case FTS_DC:
                    error(0,0,"warning: directory '%s' causes a cycle in the file system tree",
                          entry->fts_path);
                    continue;
                    break;
                case FTS_DNR:
                    error(0,errno,"warning: directory '%s' cannot be read", entry->fts_path);
                    continue;
                    break;
                case FTS_ERR:
                    error(0,errno,"warning: error accessing file '%s'", entry->fts_path);
                    continue;
                    break;
                case FTS_NS:
                    error(0,errno,"warning: cannot stat file '%s'", entry->fts_path);
                    continue;
                    break;
                case FTS_SLNONE:
                    error(0,0,"warning: file '%s' is a stale symbolic link", entry->fts_path);
                    continue;
                    break;
            }
        //process only regular files
        if (entry->fts_info == FTS_F)
        {
            //create unique id
            statp = entry->fts_statp;
            //allocate memory only if previous id was put in the tree
            if (need_alloc)
                file_id = xmalloc(sizeof(unsigned long long));
            *file_id = (unsigned long long)(statp->st_dev) << 32 | statp->st_ino;

            //check new file
            need_alloc=1;
            if (!(leaf = tsearch(file_id, &tree_root, compare_id))) {
                if (opt.verb)
                    error(0,errno,"error: not enough memory for file id tree, "
                                  "search results may be duplicated");
                need_alloc=0;
            } else
            //skip already checked file
            if (*(unsigned long long**)leaf != file_id) {
                need_alloc=0;
                continue;
            }
            checkfile(entry->fts_accpath, entry->fts_path, entry->fts_name);
        }
    }
    // fts_read() sets errno to 0 explicitly if all was ok
    if (errno && opt.verb)
        error(0, errno, "warning: fts hierarchy scan was ended abnormally,\n"
                        "search results may be incomplete");
    //free field_id pointer if it isn't in the tree
    if (!need_alloc)
        free(file_id);
    //free search tree
    tdestroy(tree_root, free_id);

    if (fts_close(ftsp) == -1 && opt.verb)
        error(0, errno, "warning: can't close fts file hierarchy stream\n"
                        "and restore working directory");
}

int main(const int argc, char *const argv[])
{
    /* parse args & preinit some vars */
    parse(argc, argv);

    /* accomplish symbol base init only if sort requested */
    if (opt.sort.match) {
        symbol.match = xcalloc (symbol.size, sizeof(char****));
        symbol.match_count = xcalloc (symbol.size, sizeof(int));
    }

    /* init libelf */
    if (elf_version(EV_CURRENT) == EV_NONE)
        error(ERR_ELF, elf_errno(), "fatal: cannot initialize libelf");

    /* prepare output */
    init_output();

    /* scan file hierarchy */
    fts_scan();

    /* free unneeded memory */
    free_unused();
    if (opt.re) {
        free(reg_error_str);
        free_regstr();
    }

#ifdef HAVE_PORTAGE
    /* search for ebuilds owning files in questions */
    if (opt.ebuild)
        find_ebuilds(&file_arr);
#endif //HAVE_PORTAGE

    /* sort if required and output results */
    if (opt.sort.cnt)
        sort_output();
    else if (opt.verb && !matches_found)
        puts(str_not_found);

#ifdef HAVE_RPM
    /* uninit rpm */
    if (opt.rpm)
        rpmuninit();
#endif //HAVE_RPM
}

