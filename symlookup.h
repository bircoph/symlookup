/*
 *  symlookup main
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

#ifndef SL_SYMBOL_LOOKUP_H
#define SL_SYMBOL_LOOKUP_H

#include <regex.h>

//define error codes (0=normal exit, obvious, not defined)
#define ERR_PARSE 1
#define ERR_IO    2
#define ERR_MEM   3
#define ERR_ELF   4
#define ERR_FTS   5

extern const size_t reg_error_str_len;
extern char *reg_error_str;

//use different compare function depend upon case sensetivity
extern int (*compare_func) (const char*, const char*);

/* structure for string array */
struct str_t {
    unsigned int size; //number of elements
    char **str;
};

/* number of match types */
#if (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
    #if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
        #define M_TYPES 4
    #else
        #define M_TYPES 3
    #endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
/* memory to be saved in match structure when non-full query is requested */
extern unsigned int M_SAVEMEM;
#else
    #define M_TYPES 2
extern const unsigned int M_SAVEMEM;
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))

/* match types */
struct mtype_t {
    unsigned int symbol;
    unsigned int file;
#ifdef HAVE_RPM
    unsigned int rpm;
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
    unsigned int ebuild;
#endif //HAVE_PORTAGE
};
extern struct mtype_t mtype;

/* array for matches ((char*)[3]):
   user-provided symbols
   matched files
   matched rpm */

/* structure for all symbol names */
struct sym_arr {
    unsigned int size;          //number of elements
    unsigned int *match_count;  //number of matches for each symbol
    char **str;                 //user-provided symbols or regexps
    regex_t *regstr;            //regexps for symbols
    char ****match;             //matched symbols array
};
extern struct sym_arr symbol;

/* structure for match array */
struct match_arr_t {
    unsigned int count; //number of matches
    char ***match;
};
extern struct match_arr_t match_arr;

/* subsequent sort comparision (ssc) options */
struct sort_t {
    unsigned int cnt;           //number of ssc, zero stands for disabled sort
    unsigned int seq[M_TYPES];  //type(s) of ssc
    unsigned int match;         //sort independently for different matches
};

enum verbose_t {
    V_QUIET,
    V_NORMAL,
    V_VERBOSE
};

/* our options, mask flags above are used; unsigned int is used for speed purposes */
struct opt_t {
    unsigned int so;    // search for *.so files
    unsigned int ar;    // search for *.ar files
    unsigned int dp;    // default search path flag
    unsigned int ext;   // perform extensions check for lib files
    unsigned int cas;   // ignore case in symbols
    unsigned int tbl;   // use table for results output
    unsigned int hdr;   // print header for the table
#ifdef HAVE_RPM
    unsigned int rpm;   // find rpms
    char* rpmroot;      // rpm root directory
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
    unsigned int ebuild;// find ebuilds (2 stands for disabled due to init error)
    char* portageDB;    // path to portage database
#endif //HAVE_PORTAGE
    enum verbose_t verb;// verbosity level
    int re;             // regexp options flag (extended regexps)
    int fts;            // fts() options
    struct sort_t sort; // sort params
    regex_t *file_re;   // library file name regular expression 
};
extern struct opt_t opt;

/* matched symbol manipulation */
void do_match(const unsigned int i, const char* const filename,
                                    const char* const symbolname);

#endif /* SL_SYMBOL_LOOKUP_H */
