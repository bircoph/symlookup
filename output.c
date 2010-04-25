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

/* Comparison function for matches within fixed level.
   It must be called only from terse output subsystem,
   opt.sort.seq must NOT contain MATCH_RPM if (!opt.rpm).
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
        {
            end++;
            break;
        }

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
            if (type[j] == MATCH_FILE && j < opt.sort.cnt - 1)
            {
                i = terse_output(match, count, i, j+1);
                break;
            }

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

