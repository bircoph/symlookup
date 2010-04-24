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

const char* const str_not_found = "No matches found.";
const char* outfmt;

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
   opt.sort.seq must NOT contain MATCH_RPM if (!opt.rpm) */
static int compare_matches(const void* const a, const void* const b)
{
    // statically initialize to shut up gcc
    static int result = 0;
    const char** const s1 = *((const char*** const)a);
    const char** const s2 = *((const char*** const)b);

    /* compare match results subsequently */
    for (unsigned int i=0; i < opt.sort.cnt; i++)
        /* match is an array (char**) with 3 elements,
           their order is in concordance with MATCH_ indexes
         */
        if (( result = strcmp( s1[opt.sort.seq[i]],
                               s2[opt.sort.seq[i]] ) ))
            break;

    return result;
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
}

/* sort if required and output results */
void sort_output()
{
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

void init_output()
{
    /* setup output format,
       useful only for non-sorted data, including match */
    if (opt.tbl) {
#ifdef HAVE_RPM
        if (opt.rpm)
            if (opt.sort.match)
                outfmt = "%s\t%s\t%s\t%s\n";
            else
                outfmt = "%s\t%s\t%s\n";
        else
#endif //HAVE_RPM
            outfmt = "%s\t%s\n";
    }
    else
        outfmt = "%s:\t%s\n";

    /* show unsorted output header for the first time */
    if (opt.hdr && !opt.sort.cnt)
    {
#ifdef HAVE_RPM
        if (opt.rpm)
            printf(outfmt, mtypes_str[MATCH_FILE], 
                    mtypes_str[MATCH_RPM], mtypes_str[MATCH_SYM]);
        else
#endif //HAVE_RPM
            printf(outfmt, mtypes_str[MATCH_FILE], 
                    mtypes_str[MATCH_SYM]);
    }
}

