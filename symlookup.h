/*
 *  symlookup main
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
extern struct str_t sp; //all search pathes (string array)

/* number of match types */
#ifdef HAVE_RPM
    #define M_TYPES 3
    //allow to reduce match "structure" if rpm search is no used
    extern unsigned int M_SAVEMEM;
#else
    #define M_TYPES 2
#endif //HAVE_RPM

/* match types */
enum match_types {
    MATCH_SYM,
    MATCH_FILE
#ifdef HAVE_RPM
   ,MATCH_RPM
#endif //HAVE_RPM
};
// printable names for match fields;
extern const char* const mtypes_str[M_TYPES];

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

/* our options, mask flags above are used; unsigned int is used for speed purposes */
struct opt_t {
    unsigned int so;    // search for *.so files
    unsigned int ar;    // search for *.ar files
    unsigned int verb;  // be verbose
    unsigned int dp;    // default search path flag
#ifdef HAVE_RPM
    unsigned int rpm;   // find rpms
#endif //HAVE_RPM
    unsigned int ext;   // perform extensions check for lib files
    unsigned int cas;   // ignore case in symbols
    unsigned int tbl;   // use table for results output
    unsigned int hdr;   // print header for the table
    int re;             // regexp options flag (extended regexps)
    int fts;            // fts() options
    struct sort_t sort; // sort params
};
extern struct opt_t opt;

/* matched symbol manipulation */
void do_match(const unsigned int i, const char* const filename,
                                    const char* const symbolname);

#endif /* SL_SYMBOL_LOOKUP_H */
