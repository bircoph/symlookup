/*
 *  Results sort and output
 *  Copyright © 2007-2009 Andrew Savchenko
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
   It must be called only if ordinary sort is enabled (opt.sort.enabled),
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

#if 0
/* these functions are required only in 3-field sort,
 * so they are useless if rpm support is disabled */
#ifdef HAVE_RPM
/* compare strings, wrapper to strcmp */
static int compare_strings(register const void* const a, register const void* const b)
{
    return strcmp(*(const char** const)a, *(const char** const)b);
}

// though this function doesn't refer to rpm directly,
// it is used only in rpm output routine

/* print leaf of MATCH_FILE sort tree
   returns new pointer to cache */
static const char**
print_leaf(const char **cache,
           unsigned int *cache_size,
           char ***const match,
           const unsigned int i, 
           const unsigned int mark,
           const unsigned int type_idx,
           const char *const format)
{
    const char* prev;
    unsigned int current_size;

    //realloc memory only if cache size is not enough
    current_size = sizeof(char*) * (i-mark);
    if (current_size > *cache_size)
    {
        while (current_size > *cache_size)
            *cache_size <<= 1;
        cache = xrealloc(cache, *cache_size);
    }

    // select matches to sort, save them in cache
    for (unsigned int j=mark; j < i; j++)
        cache[j-mark] = match[j][opt.sort.seq[type_idx]];
    // sort in order to remove dups later
    qsort(cache, i-mark, sizeof(char*), compare_strings);

    printf(format, cache[0]);
    prev = cache[0];
    for (unsigned int j=0; j < i-mark; j++)
        //do not duplicate fields
        if (strcmp(cache[j], prev)) {
            printf(format, cache[j]);
            prev = cache[j];
        }
    return cache;
}
#endif //HAVE_RPM

/* oh, yeah! finally print sort result 
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

    unsigned int *type = opt.sort.seq;
    char *prev[2];
#ifdef HAVE_RPM
    unsigned int type_cnt;

    //in MATCH_FILE case subsequent fields will be sorted later
    //save orig value
    if (opt.rpm && type[0] == MATCH_FILE)
    {
        type_cnt = opt.sort.cnt;
        opt.sort.cnt = 1;
    }
#endif //HAVE_RPM

    /* sort results */
    qsort(match, count, sizeof(char**), compare_matches);

#ifdef HAVE_RPM
    //restore orig value
    if (opt.rpm && type[0] == MATCH_FILE)
        opt.sort.cnt = type_cnt;
#endif //HAVE_RPM

#ifdef HAVE_RPM
    if (opt.rpm)
    {
        /* MATCH_FILE special branching:
           rpms and symbols are both belong
           to master file equally, not hierarchically */
        if (type[0] == MATCH_FILE)
        {
            printf("%s:\n", match[0][type[0]]);
            unsigned int mark = 0;
            /* sort cache */
            unsigned int cache_size = sizeof(char*) * 8;
            const char **cache = xmalloc(cache_size);

            //iterate through each field of each match
            for (unsigned int i=1; i < count; i++)
                //match next different first field
                if (strcmp(match[i][type[0]], match[mark][type[0]]))
                {
                    cache = print_leaf(cache, &cache_size, match, i, mark, 1, "\t%s\n");
                    cache = print_leaf(cache, &cache_size, match, i, mark, 2, "\t\t%s\n");
                    printf("%s:\n", match[i][type[0]]);
                    mark = i;
                }

            cache = print_leaf(cache, &cache_size, match, count, mark, 1, "\t%s\n");
            cache = print_leaf(cache, &cache_size, match, count, mark, 2, "\t\t%s\n");
            free(cache);
        }
        else 
            if (opt.tbl)
                for (unsigned int i=0; i < count; i++)
                {
                    if (pattern)
                        printf("%s\t", pattern);
                    printf(outfmt, match[0][type[0]],
                                       match[0][type[1]], match[0][type[2]]);
                }
            else
            {
                printf("%s:\n\t%s:\n\t\t%s\n", match[0][type[0]],
                                               match[0][type[1]], match[0][type[2]]);
                prev[0] = match[0][type[0]];
                prev[1] = match[0][type[1]];
                //iterate through each field of each match
                for (unsigned int i=1; i < count; i++)
                {
                    //do not duplicate first field
                    if (strcmp(match[i][type[0]], prev[0])) {
                        printf("%s:\n\t%s:\n", match[i][type[0]], match[i][type[1]]);
                        prev[0] = match[i][type[0]];
                        prev[1] = match[i][type[1]];
                    } else
                    //do not duplicate second field
                    if (strcmp(match[i][type[1]], prev[1])) {
                        printf("\t%s:\n", match[i][type[1]]);
                        prev[1] = match[i][type[1]];
                    }
                    printf("\t\t%s\n", match[i][type[2]]);
                }
            }
        }   //opt.rpm
    else 
#endif //HAVE_RPM

        if (opt.tbl)
            for (unsigned int i=0; i < count; i++)
            {
                if (pattern)
                    printf("%s\t", pattern);
                printf(outfmt, match[0][type[0]], match[0][type[1]]);
            }
        else
        {
            printf("%s:\n\t%s\n", match[0][type[0]], match[0][type[1]]);
            prev[0] = match[0][type[0]];
            //iterate through each field of each match
            for (unsigned int i=1; i < count; i++)
            {
                //do not duplicate first field
                if (strcmp(match[i][type[0]], prev[0])) {
                    printf("%s:\n", match[i][type[0]]);
                    prev[0] = match[i][type[0]];
                }
                printf("\t%s\n", match[i][type[1]]);
            }
        }
}
#endif

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
            fputs("PATTERN\t",stdout);
            construct_header();
        }
        for (unsigned int i=0; i < symbol.size; i++) {
            if (!symbol.match_count[i] && !opt.verb)
                continue;
            if (!opt.tbl)
                printf("===> match(es) for pattern '%s':\n", symbol.str[i]);
            /* print unsorted resusts */
            if (!opt.sort.cnt)
                if (opt.verb && !symbol.match_count[i])
                    if (!opt.tbl)
                        puts(str_not_found);
                    else
                        printf("%s\t%s\n", symbol.str[i], str_not_found);
                else
#ifdef HAVE_RPM
                    if (opt.rpm)
                        for (unsigned int j=0; j < symbol.match_count[i]; j++)
                            listrpm(symbol.match[i][j][MATCH_FILE], symbol.match[i][j][MATCH_SYM],
                                    symbol.str[i]);
                    else
#endif //HAVE_RPM
                        for (unsigned int j=0; j < symbol.match_count[i]; j++)
                        {
                            if (opt.tbl) {
                                fputs(symbol.str[i],stdout);
                                putchar('\t');
                            }

                            printf(outfmt, symbol.match[i][j][MATCH_FILE],
                                                 symbol.match[i][j][MATCH_SYM]);
                        }
            else
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
    if (opt.hdr && !opt.sort.enabled)
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

