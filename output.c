/*
 *  Results sort and output
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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <regex.h>

#include "symlookup.h"
#include "safemem.h"
#include "rpmutils.h"
#include "output.h"

const char* mtypes_str[M_TYPES]; /* match field names */
const char* const str_not_found = "No matches found.";

/* create header for sorted output */
static void construct_header()
{
    for (unsigned int i=0; i<opt.sort.cnt; i++)
    {
        fputs(mtypes_str[opt.sort.seq[i]],stdout);
        if (i != opt.sort.cnt-1)
            putchar('\t');
        else
            putchar('\n');
    }
}

/* Comparison function for ordinary matches.
   It must be called only if ordinary sort is enabled (opt.sort.cnt),
   opt.sort.seq must NOT contain mtype.rpm if (!opt.rpm) */
static int compare_matches(const void* const a, const void* const b)
{
    // statically initialize to shut up gcc
    static int result = 0;
    const char** const s1 = *((const char*** const)a);
    const char** const s2 = *((const char*** const)b);

    /* compare match results subsequently */
    for (unsigned int i=0; i < opt.sort.cnt; i++)
        /* match is an array (char**) with 3 elements,
           their order is in concordance with mtype indexes
         */
        if (( result = strcmp( s1[opt.sort.seq[i]],
                               s2[opt.sort.seq[i]] ) ))
            break;

    return result;
}

/* Comparison function for matches within fixed level.
   It must be called only from terse output subsystem,
   opt.sort.seq must NOT contain mtype.rpm if (!opt.rpm).
   External variable comparision_level is used to denote level to compare,
   as it can't be passed to the function directly */
unsigned int comparision_level;
static int compare_level(const void* const a, const void* const b)
{
    const char** const s1 = *((const char*** const)a);
    const char** const s2 = *((const char*** const)b);

    /* compare single level match results */
    return strcmp(s1[opt.sort.seq[comparision_level]],
                  s2[opt.sort.seq[comparision_level]]);
}

/* Print subtree in a terse mode:
 * at each level there're no duplicated nodes.
 * match -- match array to process
 * count -- number of elements in match array
 * start -- index of the first element to process
 * level -- highest level of a tree to process (should be valid) */
static unsigned int
terse_output(char*** const match, const unsigned int count,
             const unsigned int start, const unsigned int level)
{
    unsigned int end;
    unsigned int *type = opt.sort.seq;

    // find the end of the terse block
    for (end = start; end < count - 1; end++)
        if (strcmp(match[end][type[level-1]], match[end+1][type[level-1]]))
            break;

    for (unsigned int j = level; j < opt.sort.cnt; j++)
    {
        // all terse levels with (depth > 1) must be resorted
        if (j > level)
        {
            comparision_level = j;
            qsort(match + start, end - start + 1, sizeof(char**), compare_level);
        }

        for (unsigned int i = start; i <= end; i++)
        {
            // skip previously printed element
            if (i > start && !strcmp(match[i][type[j]], match[i-1][type[j]]))
                continue;

            for (unsigned int z=0; z < j; z++)
                putchar('\t');

            puts(match[i][type[j]]);
        }
    }

    return end;
}

/* Output sorted results.
 * match   -- match structure 
 * count   -- number of matches
 * pattern -- match pattern, only != NULL when match 
 *            search field engaged */
static void print_result(char*** const match, const unsigned int count,
                         const char* const pattern)
{
    //empty set?
    if (!count) {
        if (opt.verb)
            puts(str_not_found);
        return;
    }
    //header for match created separately
    if (opt.hdr && !opt.sort.match)
        construct_header();

    /* sort results */
    qsort(match, count, sizeof(char**), compare_matches);
    unsigned int *type = opt.sort.seq;

    /* output sorted table */
    if (opt.tbl)
    {
        for (unsigned int i=0; i < count; i++)
        {
            if (pattern) {
                fputs(pattern, stdout);
                putchar('\t');
            }
            //cycle through the fields, 
            //number depends on CLI options
            for (unsigned int j=0; j < opt.sort.cnt; j++)
            {
                fputs(match[i][type[j]], stdout);
                if (j != opt.sort.cnt-1)
                    putchar('\t');
                else
                    putchar('\n');
            }
        }
        return;
    }

    char *prev_str[M_TYPES], // array of pointers to previously printed strings
         *str;               // string for temporal reference
    // top prev str layer should be cleaned outside the main loop
    prev_str[0] = NULL;

    /* tree output */
    for (unsigned int i=0; i < count; i++)
        for (unsigned int j=0; j < opt.sort.cnt; j++)
        {
            str = match[i][type[j]];

            // skip previously printed element
            if (prev_str[j] && !strcmp(prev_str[j], str))
                continue;

            // clear previous string flag from inner layer
            if (j < opt.sort.cnt - 1)
                prev_str[j+1] = NULL;

            for (unsigned int z=0; z < j; z++)
                putchar('\t');

            puts(str);
            prev_str[j] = str; // save current string

            /* terse output section */
            if (type[j] == mtype.file && j < opt.sort.cnt - 1)
            {
                i = terse_output(match, count, i, j+1);
                break;
            }

        }
}

/* sort if required and output results */
void sort_output()
{
    if (opt.verb >= V_VERBOSE)
        puts("--> Preparing for output results");

    /* sort by match is done separately */
    if (opt.sort.match)
    {
        if (opt.hdr)
        {
            fputs("PATTERN\t\t",stdout);
            construct_header();
        }
        for (unsigned int i=0; i < symbol.size; i++) {
            if (!symbol.match_count[i] && !opt.verb)
                continue;
            if (!opt.tbl)
                printf("===> match(es) for pattern '%s':\n", symbol.str[i]);

            /* use standard output facility */
            print_result(symbol.match[i], symbol.match_count[i], symbol.str[i]);

        }
    } // opt.sort.match
    /* ordinary sort */
    else
        print_result(match_arr.match, match_arr.count, NULL);
}

/* Output unsorted results for ebuild search,
 * take care of special case when ebuild was enabled by user,
 * but disabled later due to an error. */
#ifdef HAVE_PORTAGE
void ebuild_unsorted_output()
{
    if (opt.verb >= V_VERBOSE)
        puts("--> Unsorted ebuild output");

    if (!match_arr.count) {
        if (opt.verb)
            puts(str_not_found);
        return;
    }

    for (unsigned int i=0; i < match_arr.count; i++)
    {
        fputs(match_arr.match[i][mtype.file], stdout);

        if (opt.ebuild == 1
#ifdef HAVE_RPM
            || opt.rpm    
#endif //HAVE_RPM
                )
            fputs(" (", stdout);

        if (opt.ebuild == 1) {
            fputs("ebuild: ", stdout);
            fputs(match_arr.match[i][mtype.ebuild], stdout);
        }

#ifdef HAVE_RPM
        if (opt.ebuild == 1
            && opt.rpm    
                )
            fputs(", ", stdout);

        if (opt.rpm) {
            fputs("rpm: ", stdout);
            fputs(match_arr.match[i][mtype.rpm], stdout);
        }
#endif //HAVE_RPM

        if (opt.ebuild == 1
#ifdef HAVE_RPM
            || opt.rpm    
#endif //HAVE_RPM
                )
            putchar(')');

        fputs(": ", stdout);
        puts(match_arr.match[i][mtype.symbol]);
    }
}
#endif //HAVE_PORTAGE

/* initialize output (header, formats) */
void init_output()
{
    /* init mtypes_str, required only for headers */
    if (opt.hdr)
    {
        mtypes_str[mtype.symbol]  = alloc_str("SYMBOL");
        mtypes_str[mtype.file] = alloc_str("FILE");
#ifdef HAVE_RPM
        if (opt.rpm)
            mtypes_str[mtype.rpm] = alloc_str("RPM");
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
        if (opt.ebuild)
            mtypes_str[mtype.ebuild] = alloc_str("EBUILD");
#endif //HAVE_PORTAGE
    }

    /* show unsorted output header for the first time */
    if (opt.hdr && !opt.sort.cnt)
    {
        // output as: file [ebuild] [rpm] symbol
        fputs(mtypes_str[mtype.file], stdout);
        putchar('\t');
#ifdef HAVE_PORTAGE
        if (opt.ebuild) {
            fputs(mtypes_str[mtype.ebuild], stdout);
            putchar('\t');
        }
#endif //HAVE_PORTAGE
#ifdef HAVE_RPM
        if (opt.rpm) {
            fputs(mtypes_str[mtype.rpm], stdout);
            putchar('\t');
        }
#endif //HAVE_RPM
        puts(mtypes_str[mtype.symbol]);
    }
}

