/*
 *  CLI parser and program configurer
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
 *
 *  ld_so_conf-related code is based on GNU Linker (ld) code, file
 *  emultempl/elf32.em, written by Steve Chamberlain <sac@cygnus.com>.
 *  See AUTHORS in the source tree for details.
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <fts.h>
#include <string.h>
#include <getopt.h>
#include <regex.h>
#include <glob.h>

#include "symlookup.h"
#include "safemem.h"
#include "parser.h"
#include "version.h"
#include "rpmutils.h"

extern struct str_t sp; //all search pathes (string array)

/* buffer size for reading lines */
size_t line_buf = 512;
// strtok stuff
char *tail = NULL;       //for strtok fields
// field list storage
char *field_list = NULL;
// for file name regular expression
char *filename_regexp = NULL;
// flag for file name regular expression case
unsigned int filename_case = 0;
// set for already added fields
unsigned int field_set = 0;

/* grow symbol array by adding new element <str> */
static void grow_sym(const char* const str)
{
    add_str(&symbol.str, symbol.size, str);

    /* allocate memory for regular expression and create it */
    if (opt.re) {
        symbol.regstr = xrealloc(symbol.regstr, sizeof(regex_t) * (symbol.size + 1));
        int err_code;
        //compile regexp
        if (( err_code = regcomp(&symbol.regstr[symbol.size], str, opt.re) ))
        {
            regerror(err_code, &symbol.regstr[symbol.size], reg_error_str, reg_error_str_len);
            error(ERR_PARSE, errno, "failed to compile regular expression '%s': %s",
                  str, reg_error_str);
        }
    }

    ++symbol.size;      //+1 element
}

/********************************************************************
 *                          SORTING UTILS                           *
 * * * * * * * * * * * * * * * * ** * * * * * * * * * * * * * * * * *
 * Ideas behind sort sequence:                                      *
 * opt.sort.seq[]={field1, field2, field3,...}                      *
 *                                                                  *
 * 1. Table output is straightforward:                              *
 * [ header ]                                                       *
 * field1 field2 field3 ...                                         *
 * New raw for each unique combination.                             *
 * Terse variation is inapplicable.                                 *
 *                                                                  *
 * 2. Tree output.                                                  *
 * 2.1. Normal form (non-terse).                                    *
 * field1                                                           *
 *     field11                                                      *
 *         field111                                                 *
 *         field112                                                 *
 *     field12                                                      *
 *         field121                                                 *
 *         field122 (== e.g., field111)                             *
 * So this is just tab-shifted tree. Duplications in parent leafs   *
 * are possible in general.                                         *
 * 2.2. Terse form. Only valid after mtype.file field.              *
 * field1                                                           *
 *     field11                                                      *
 *     field12                                                      *
 *         field111                                                 *
 *         field112                                                 *
 *         field121                                                 *
 * Fields from the same terse group are indeed equal leafs of the   *
 * tree, so there are no summary duplications, fields are           *
 * compacted.                                                       *
 ********************************************************************/

/* check for duplication of ordinary field
   without special requirements
   1 == stop checking
   0 == continue */
static int check_field_ordinary_dup(const char* const str, const char* const field,
                                    const unsigned int type)
{
    if (!strcmp(str,field)) {
        //check for duplicate
        if (field_set & (1U << type))
            error(ERR_PARSE, 0, "parse error: each sort field may arrear only once,\n"
                                "'%s' entry duplicated", str);
        opt.sort.seq[opt.sort.cnt++] = type;
        // setup the flag for current field;
        field_set |= 1U << type;
        return 1;
    }
    return 0;
}

/* find last unset field */
static unsigned int find_last_field(const unsigned int limit)
{
    for (unsigned int i=0; i<limit; i++)
        if (!(field_set & (1U << i)))
            return i;
}

/* build sort sequence and dependencies from CLI argument */
static inline void construct_sort_sequence()
{
    // reset sort counter, it was set just as a bool flag before
    opt.sort.cnt = 0;
    // field list may be empty, then defaults should be used
    if (field_list)
    {
        if ((tail = strtok(field_list, ",")))
        {
            do  //parse different fields
            {
                if (!strcmp(tail,"match")) 
                {
                    if (!opt.cas && !opt.re)
                    {
                        if (opt.verb)
                            error(0, 0, "parse error: 'match' sort field is useless without -i or -r,\n"
                                        "entry ignored");
                        continue;
                    }

                    if (opt.sort.cnt)
                        error(ERR_PARSE, 0, "parse error: 'match' sort field must be first in list");
                    if (opt.sort.match)
                        error(ERR_PARSE, 0, "parse error: each sort field may arrear only once,"
                                            "'match' entry duplicated");
                    //option seems to be ok
                    opt.sort.match = 1;
                    continue;
                }
                if (check_field_ordinary_dup(tail, "symbol", mtype.symbol))
                    continue;
                if (check_field_ordinary_dup(tail, "file", mtype.file))
                    continue;
#ifdef HAVE_RPM
                //rpm special casing
                if (!strcmp(tail,"rpm"))
                {
                    if (!opt.rpm)
                    {
                        if (opt.verb)
                            error(0, 0, "parse error: 'rpm' field is set, but -R is not set, entry ignored");
                        continue;
                    }
                    if (check_field_ordinary_dup(tail, "rpm", mtype.rpm))
                        continue;
                }
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
                //rpm special casing
                if (!strcmp(tail,"ebuild"))
                {
                    if (!opt.ebuild)
                    {
                        if (opt.verb)
                            error(0, 0, "parse error: 'ebuild' field is set, but -E is not set, entry ignored");
                        continue;
                    }
                    if (check_field_ordinary_dup(tail, "ebuild", mtype.ebuild))
                        continue;
                }
#endif //HAVE_PORTAGE
                error(ERR_PARSE, 0, "parse error: unknown sort suboption '%s'",tail);
            }
            while ((tail = strtok(NULL, ",")));
        }
        free(field_list);
    }

    /**********************************************************
     * Autocompletion policy:    ** +RPM, +EBUILD:            *
     *---------------------------**---------------------------*
     * -RPM, -EBUILD:            ** 0: f e r s                *
     *---------------------------**                           *
     * 0: f s                    ** 1: F e r s                *
     * 1: single choise possible **    R e f s                *
     *---------------------------**    E r f s                *
     * +RPM,-EBUILD/-RPM+EBUILD: **    S f e r                *
     *---------------------------**                           *
     * 0: f    r(e) s            ** 2: F E r s                *
     * 1: F    r(e) s            **    F R e s                *
     *    R(E) f    s            **    F S e r                *
     *    S    f    r(e)         **    R E f s                *
     * 2: single choise possible **    R F e s                *
     *---------------------------**    R S f e                *
     * Notes:                     *    E R f s                *
     * -capital is user field;    *    E F r s                *
     * -lowcase is autocomletion; *    E S f r                *
     * -signs:                    *    S F r e                *
     *   f: file                  *    S R e f                *
     *   e: ebuild                *    S E r f                *
     *   r: rpm                   *                           *
     *   s: symbol                * 3: single choise possible *
     **********************************************************/
    switch (opt.sort.cnt)
    {
        case 0:
            opt.sort.seq[0]=mtype.file;

#if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
            if (opt.rpm && opt.ebuild) {
                opt.sort.seq[1]=mtype.ebuild;
                opt.sort.seq[2]=mtype.rpm;
                opt.sort.seq[3]=mtype.symbol;
            }
            else
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
#ifdef HAVE_RPM
            if (opt.rpm) {
                opt.sort.seq[1]=mtype.rpm;
                opt.sort.seq[2]=mtype.symbol;
            }
            else
#endif //HAVE_PORTAGE
#ifdef HAVE_PORTAGE
            if (opt.ebuild) {
                opt.sort.seq[1]=mtype.ebuild;
                opt.sort.seq[2]=mtype.symbol;
            }
            else
#endif //HAVE_PORTAGE
                opt.sort.seq[1]=mtype.symbol;
            break;
/******************************************************/
        case 1:
#if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
            if (opt.rpm && opt.ebuild)
            {
                if (opt.sort.seq[0] == mtype.file)
                {
                    opt.sort.seq[1]=mtype.ebuild;
                    opt.sort.seq[2]=mtype.rpm;
                    opt.sort.seq[3]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.rpm)
                {
                    opt.sort.seq[1]=mtype.ebuild;
                    opt.sort.seq[2]=mtype.file;
                    opt.sort.seq[3]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.ebuild)
                {
                    opt.sort.seq[1]=mtype.rpm;
                    opt.sort.seq[2]=mtype.file;
                    opt.sort.seq[3]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.symbol)
                {
                    opt.sort.seq[1]=mtype.file;
                    opt.sort.seq[2]=mtype.ebuild;
                    opt.sort.seq[3]=mtype.rpm;
                }
            }
            else
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
#ifdef HAVE_RPM
            if (opt.rpm)
            {
                if (opt.sort.seq[0] == mtype.file)
                {
                    opt.sort.seq[1]=mtype.rpm;
                    opt.sort.seq[2]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.rpm)
                {
                    opt.sort.seq[1]=mtype.file;
                    opt.sort.seq[2]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.symbol)
                {
                    opt.sort.seq[1]=mtype.file;
                    opt.sort.seq[2]=mtype.rpm;
                }
            }
            else
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
            if (opt.ebuild)
            {
                if (opt.sort.seq[0] == mtype.file)
                {
                    opt.sort.seq[1]=mtype.ebuild;
                    opt.sort.seq[2]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.ebuild)
                {
                    opt.sort.seq[1]=mtype.file;
                    opt.sort.seq[2]=mtype.symbol;
                }
                else
                if (opt.sort.seq[0] == mtype.symbol)
                {
                    opt.sort.seq[1]=mtype.file;
                    opt.sort.seq[2]=mtype.ebuild;
                }
            }
            else
#endif //HAVE_PORTAGE
                opt.sort.seq[1]=find_last_field(2);
            break;
/******************************************************/
#if (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
        case 2:
    #if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
            if (opt.rpm && opt.ebuild)
            {
                if (opt.sort.seq[0] == mtype.file)
                {
                    if (opt.sort.seq[1] == mtype.ebuild)
                    {
                        opt.sort.seq[2]=mtype.rpm;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.rpm)
                    {
                        opt.sort.seq[2]=mtype.ebuild;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.symbol)
                    {
                        opt.sort.seq[2]=mtype.ebuild;
                        opt.sort.seq[3]=mtype.rpm;
                    }
                }
                else
                if (opt.sort.seq[0] == mtype.rpm)
                {
                    if (opt.sort.seq[1] == mtype.ebuild)
                    {
                        opt.sort.seq[2]=mtype.file;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.file)
                    {
                        opt.sort.seq[2]=mtype.ebuild;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.symbol)
                    {
                        opt.sort.seq[2]=mtype.file;
                        opt.sort.seq[3]=mtype.ebuild;
                    }
                }
                else
                if (opt.sort.seq[0] == mtype.ebuild)
                {
                    if (opt.sort.seq[1] == mtype.rpm)
                    {
                        opt.sort.seq[2]=mtype.file;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.file)
                    {
                        opt.sort.seq[2]=mtype.rpm;
                        opt.sort.seq[3]=mtype.symbol;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.symbol)
                    {
                        opt.sort.seq[2]=mtype.file;
                        opt.sort.seq[3]=mtype.rpm;
                    }
                }
                else
                if (opt.sort.seq[0] == mtype.symbol)
                {
                    if (opt.sort.seq[1] == mtype.file)
                    {
                        opt.sort.seq[2]=mtype.rpm;
                        opt.sort.seq[3]=mtype.ebuild;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.rpm)
                    {
                        opt.sort.seq[2]=mtype.ebuild;
                        opt.sort.seq[3]=mtype.file;
                    }
                    else
                    if (opt.sort.seq[1] == mtype.ebuild)
                    {
                        opt.sort.seq[2]=mtype.rpm;
                        opt.sort.seq[3]=mtype.file;
                    }
                }
            } //(opt.rpm && opt.ebuild)
            else
            if (opt.rpm || opt.ebuild)
    #elif (defined HAVE_RPM)
            if (opt.rpm)
    #else
            if (opt.ebuild)
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
                opt.sort.seq[2]=find_last_field(3);
            break;
#endif //(defined(HAVE_RPM) || defined(HAVE_PORTAGE))
/******************************************************/
#if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
        case 3:
            if (opt.rpm && opt.ebuild)
                opt.sort.seq[3]=find_last_field(4);
            break;
#endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
    }

    // at the end (opt.sort.cnt == number_of_elements_to_sort)
    opt.sort.cnt = M_SAVEMEM;
}

/****************************************************************
 *                      SEARCH PATH UTILS                       *
 ****************************************************************/

/* parse string with paths in format:
 * PATH1:PATH2:... */
static void parse_path_str(char* const str)
{
    if ((tail = strtok(str, ":")))
        do  //parse different pathes
            grow_str(&sp, tail);
        while ((tail = strtok(NULL, ":")));
}

static void parse_ld_so_conf (const char* const);
/* parse include command in ld.so.conf */
static void parse_ld_so_conf_include (const char* const filename, const char* pattern)
{
    char *newp = NULL;
    glob_t gl;

    //complement pattern path if original one is relative
    //using basename of filename
    if (pattern[0] != '/')
    {
        char *p = strrchr (filename, '/');
        size_t patlen = strlen (pattern) + 1;

        newp = xmalloc (p - filename + 1 + patlen);
        memcpy (newp, filename, p - filename + 1);
        memcpy (newp + (p - filename + 1), pattern, patlen);
        pattern = newp;
    }

    switch (glob (pattern, GLOB_NOSORT, NULL, &gl)) {
        case 0:
            for (unsigned int i = 0; i < gl.gl_pathc; i++)
                parse_ld_so_conf (gl.gl_pathv[i]);
            globfree (&gl);
            break;
        case GLOB_NOMATCH:
            if (opt.verb)
                error(0, errno, "warning: no matches for glob '%s' from file '%s'",
                                pattern, filename);
            break;
        case GLOB_NOSPACE:
            if (opt.verb)
                error(0, errno, "error: can't perform glob for a pattern '%s' "
                                "due to memory allocation error.\n"
                                "Search path may be incomplete.", pattern);
            break;
        case GLOB_ABORTED:
            if (opt.verb)
                error(0, errno, "error: can't perform opendir for a pattern '%s'.\n"
                                "Search path may be incomplete.", pattern);
            break;
    }

    if (newp)
        free (newp);
}

/* parse ld *.conf file */
static void parse_ld_so_conf (const char* const filename)
{
    FILE *fd = fopen (filename, "r");
    char *line,     // buffer for line
         *p,        // temporary pointer into the line
         *tok_buf;  // buffer for reentrant strtok_r()
    if (!fd) {
        if (opt.verb)
            error(0, errno, "error: can't open ld config file %s for reading;\n"
                            "check your ld configuration!", filename);
        return;
    }

    line = xmalloc(line_buf);
    while (getline(&line, &line_buf, fd) != -1)
    {
        /* Because the file format does not know any form of quoting we
           can search forward for the next '#' character and if found
           make it terminating the line.
           Also \n should be replaced */
        p = line;
        while (*p && *p != '\n' && *p != '#')
            p++;
        if (p)
            *p = '\0';

        /* Remove leading whitespace.  NUL is no whitespace character.  */
        if (!(tail = strtok_r(line, " \f\r\t\v", &tok_buf)))
            continue;

        //check for "include" keyword
        if (!strcmp(tail, "include"))
            while ((tail = strtok_r(NULL, " \t", &tok_buf)))
                parse_ld_so_conf_include (filename, tail);
        else {
            //allow '=' character in the same way as ld
            if (!strcmp(tail,"="))
                tail = strtok_r(NULL, " \f\r\t\v", &tok_buf);
            grow_str(&sp, tail);
        }
    }

    free (line);
    if (fclose(fd) && opt.verb)
        error(0, errno, "error: can't close file %s;\n"
                        "/etc/ld.so.conf parsing may be unreliable", filename);
    return;
}

/* setup default search path in a way similar to ld(1) */
static inline void set_default_path()
{
    char *str,
         *dt = NULL;    //flag for DT_RUNPATH presence
    //environment
    if ((str = getenv("LD_RUN_PATH")))
        parse_path_str(str);
    if ((str = getenv("LD_LIBRARY_PATH")))
        parse_path_str(str);
    if ((str = getenv("DT_RUNPATH"))) {
        parse_path_str(str);
        dt = str;
    }
    // ld skips DT_RPATH if DT_RUNPATH exists, so we do
    if (!dt && (str = getenv("DT_RPATH")))
        parse_path_str(str);
    //default dirs
    grow_str(&sp, "/lib");
    grow_str(&sp, "/usr/lib");

    /* process /etc/ld.so.conf */
    parse_ld_so_conf("/etc/ld.so.conf");
}

/****************************************************************
 *                      MAIN PARSE SYSTEM                       *
 ****************************************************************/


/* Initialize package-dependent stuff */
#if (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
static inline void init_packages()
{
#ifdef HAVE_RPM
    if (!opt.rpm && opt.rpmroot)
    {
        if (opt.verb)
            error(0,0,"parse warning: --rpm-root is specified, but --rpm is not.\n"
                      "Ignoring --rpm-root option.");
        free(opt.rpmroot);
    }

    /* init rpm support,
     * on failure opt.rpm will become unset */
    if (opt.rpm)
        rpminit();

    /* check for M_SAVEMEM */
    if (opt.rpm)
        M_SAVEMEM++;
#endif //HAVE_RPM

#ifdef HAVE_PORTAGE
    if (!opt.ebuild && opt.portageDB)
    {
        if (opt.verb)
            error(0,0,"parse warning: --portage-db is specified, but --ebuild is not.\n"
                      "Ignoring --portageDB option.");
        free(opt.portageDB);
    }
    else // user didn't define portage db path
    if (opt.ebuild && !opt.portageDB)
        opt.portageDB = alloc_str("/var/db/pkg");

    /* check for M_SAVEMEM */
    if (opt.ebuild)
        M_SAVEMEM++;
#endif //HAVE_PORTAGE

    /* Initialize type fields */
#ifdef HAVE_RPM
    if (opt.rpm)
        mtype.rpm = 2;
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
    #ifdef HAVE_RPM
    if (opt.rpm && opt.ebuild)
        mtype.ebuild = 3;
    else
    #endif //HAVE_RPM
        mtype.ebuild = 2;
#endif //HAVE_PORTAGE
}
#endif //(defined(HAVE_RPM) || defined(HAVE_PORTAGE))

void parse(const int argc, char* const argv[])
{
    const struct option long_opt[]= {
        {"path",                required_argument, NULL,'p'},
        {"quiet",               no_argument,       NULL,'q'},
        {"ar",                  no_argument,       NULL,'a'},
        {"ar-only",             no_argument,       NULL,'A'},
        {"follow",              no_argument,       NULL,'s'},
        {"xdev",                no_argument,       NULL,'d'},
        {"noext",               no_argument,       NULL,'X'},
        {"regexp",              no_argument,       NULL,'r'},
        {"ignorecase",          no_argument,       NULL,'i'},
        {"filename-regexp",     required_argument, NULL,'F'},
        {"filename-ignorecase", no_argument,       NULL,'I'},
#ifdef HAVE_RPM
        {"rpm",                 no_argument,       NULL,'R'},
        {"rpm-root",            required_argument, NULL,'z'},
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
        {"ebuild",              no_argument,       NULL,'E'},
        {"portage-db",          required_argument, NULL,'Z'},
#endif //HAVE_PORTAGE
        {"sort",                optional_argument, NULL,'S'},
        {"table",               no_argument,       NULL,'t'},
        {"header",              no_argument,       NULL,'H'},
        {"help",                no_argument,       NULL,'h'},
        {"verbose",             no_argument,       NULL,'v'},
        {"version",             no_argument,       NULL,'V'},
        {0,0,0,0}
    };
    int c;
    unsigned int opt_a=0, opt_A=0, // collision options variables
                 opt_q=0, opt_v=0; //

    do  /* reading options */
    {
        c = getopt_long(argc, argv, "p:aAsdXriF:I"
#ifdef HAVE_RPM
                                    "R"
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
                                    "E"
#endif //HAVE_PORTAGE
                                    "S::tHqvhV", long_opt, NULL);
        switch (c) {
            case 'h':
                printf(
            "Usage: %s [options] [symbols]\n"
            "       %s [-h | --help]\n"
            "       %s [-v | --version]\n\n"
            "Searches for dynamic (by default) or static libs, where given symbols\n"
            "are defined."
#if (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
            " Optionally it can find "
    #if (defined(HAVE_RPM) && defined(HAVE_PORTAGE))
            "rpms or ebuilds"
    #elif (defined(HAVE_RPM))
            "rpms"
    #else
            "ebuilds"
    #endif //(defined(HAVE_RPM) && defined(HAVE_PORTAGE))
            " containing these libs."
#endif //(defined(HAVE_RPM) || defined(HAVE_PORTAGE))
            "\n"
            "If no symbols are defined at command line, standard input is used.\n\n"
            "Options:\n"
            "    -p, --path <PATH1:PATH2:...>    set path(s) for library search,\n"
            "                                    defaults are defined in a way, similar\n"
            "                                    to ld(1) program\n"
            "    -a, --ar                        search also in ar(1) archives\n"
            "    -A, --ar-only                   search ONLY in ar(1) archives\n"
            "    -s, --follow                    follow symbolic links, often it is\n"
            "                                    just wasting of time\n"
            "    -d, --xdev                      stay on the same physical device\n"
            "                                    as root search directory\n"
            "    -X, --noext                     do not perform extension check for libs\n"
            "    -r, --regexp                    treat given symbols as extended\n"
            "                                    regular expressions\n"
            "    -i, --ignorecase                ignore case in symbols\n"
            "    -F, --filename-regexp           select only file names satisfying given\n"
            "                                    regular expression\n"
            "    -I, --filename-ignorecase       ignore case in filename reg. expression\n"
#ifdef HAVE_RPM
            "    -R, --rpm                       find rpms, containing target libs\n"
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
            "    -E, --ebuild                    find ebuilds, containing target libs\n"
#endif //HAVE_PORTAGE
            "    -S, --sort [field,...]          sort search results; you may arrange\n"
            "                                    collation subsequence order, look at\n"
            "                                    Sorting below\n"
            "    -t, --table                     use table for results output\n"
            "    -H, --header                    show header for table results output\n"
            "    -q, --quiet                     show fatal errors only\n"
            "    -v, --verbose                   be more verbose\n"
            "    -h, --help                      show this help message\n"
            "    -V, --version                   show version\n"
#ifdef HAVE_RPM
            "    --rpm-root <PATH>               path to root rpm directory (when chrooting)\n"
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
            "    --portage-db <PATH>             path to portage data base\n"
#endif //HAVE_PORTAGE
            "Note: if both -a and -A are specified, the last one will take an effect;\n"
            "      the same is for -q and -v options.\n\n"
            "Sorting:\n"
            "    You can collate search results by different fields sequentially.\n"
            "    Fields must be separated by comma, no spaces are allowed;\n"
            "    they must immediately follow short option and long one\n"
            "    after '=' sign or space.\n"
            "    Valid values are:\n"
            "        match      sort results separately for each symbol (or pattern)\n"
            "                   provided by user; if neither -i nor -r is provided\n"
            "                   this option is useless and will be ignored; it must be\n"
            "                   the first sort field if provided\n"
            "        symbol     sort by symbol name (case sensitive)\n"
            "        file       sort by file name\n"
#ifdef HAVE_RPM
            "        rpm        sort by rpm containing matched file;\n"
            "                   it is useless if -R is unspecified.\n"
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
            "        ebuild     sort by ebuild containing matched file;\n"
            "                   it is useless if -E is unspecified.\n"
#endif //HAVE_PORTAGE
            "    Default sequence is 'file"
#ifdef HAVE_PORTAGE
            "(,ebuild)"
#endif //HAVE_PORTAGE
#ifdef HAVE_RPM
            "(,rpm)"
#endif //HAVE_RPM
            ",symbol'.\n\n"
            "Error codes:\n"
            "       0 - normal exit\n"
            "       %i - parse error\n"
            "       %i - i/o error\n"
            "       %i - memory allocation error\n"
            "       %i - error in libelf\n"
            "       %i - fts function error\n"
                ,argv[0], argv[0], argv[0], ERR_PARSE, ERR_IO, ERR_MEM, ERR_ELF, ERR_FTS);
                exit(0);
            case 'V':
                puts(
                    "symlookup ver. " VERSION "\n"
                    "license: GNU GPLv.3\n"
#ifdef HAVE_RPM
                    "rpm support: yes "
    #ifdef HAVE_RPM_5
                    "(using rpm v5)\n"
    #else
                    "(using rpm v4)\n"
    #endif //HAVE_RPM_5
#else
                    "rpm support: no\n"
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
                    "portage support: yes"
#else
                    "portage support: no"
#endif //HAVE_PORTAGE
                );
                exit(0);
            case 'p':
                parse_path_str(optarg);
                break;
            case 'q':
                opt.verb = V_QUIET;   //not verbose (quiet)
                opt_q = 1;
                break;
            case 'v':
                opt.verb = V_VERBOSE; //most verbose
                opt_v = 1;
                break;
            case 'a':
                opt.ar = 1;
                opt.so = 1;
                opt_a = 1;
                break;
            case 'A':
                opt.ar = 1;
                opt.so = 0;
                opt_A = 1;
                break;
            case 's':
                opt.fts &= ~FTS_PHYSICAL;
                opt.fts |= FTS_LOGICAL;
                break;
            case 'd':
                opt.fts |= FTS_XDEV;
                break;
            case 'X':
                opt.ext = 0;
                break;
            case 'r':
                //regcomp flags
                opt.re = REG_EXTENDED | REG_NOSUB;
                break;
            case 'i':
                opt.cas = 1;
                compare_func = strcasecmp;
                break;
            case 'F':
                if (filename_regexp)
                    free(filename_regexp);
                filename_regexp = alloc_str(optarg);
                break;
            case 'I':
                filename_case = 1;
                break;
#ifdef HAVE_RPM
            case 'R':
                opt.rpm = 1;
                break;
            case 'z':
                if (opt.rpmroot)
                    free (opt.rpmroot);
                // copy path safely
                opt.rpmroot = alloc_str(optarg);
                break;
#endif //HAVE_RPM
#ifdef HAVE_PORTAGE
            case 'E':
                opt.ebuild = 1;
                break;
            case 'Z':
                if (opt.portageDB)
                    free (opt.portageDB);
                // copy path safely
                opt.portageDB = alloc_str(optarg);
                break;
#endif //HAVE_PORTAGE
            case 'S':
                opt.sort.cnt = 1;
                if (optarg) {
                    // -S is specified more than once...
                    if (field_list) {
                        free(field_list);
                        if (opt.verb)
                            error(0,0,"warning: sort list is redefined, the latest one will take an effect");
                    }
                    field_list = alloc_str(optarg);
                }
                break;
            case 't':
                opt.tbl = 1;
                break;
            case 'H':
                opt.hdr = 1;
                break;
            case '?':
                exit(ERR_PARSE);        /* stop program if have incorrect params */
        }
    }
    while (c!=-1);

    // check for option collisions
    if (opt_q && opt_v)
        error(0, 0, "parse warning: both -q and -v options are specified, "
                    "the last one will take an effect: -%c", (opt.verb) ? 'v':'q');
    if (opt.verb && opt_a && opt_A)
        error(0, 0, "parse warning: both -a and -A options are specified, "
                    "the last one will take an effect: -%c", (opt.so) ? 'a':'A');
    if (opt.verb && filename_case && !filename_regexp)
        error(0, 0, "parse warning: option -I is specified, but -F is not, "
                    "-I option will be ignored");

    // warn if header is requested but table is not
    if (opt.hdr && !opt.tbl) {
        if (opt.verb)
            error(0,0,"parse warning: table header is requested but table output is not\n"
                      "(-H is specified without -t), ignoring -H option");
        opt.hdr=0;
    }

    // user didn't define search paths
    if (!sp.size)
        set_default_path();
    else
        opt.dp = 0;

    // last element in search path must be NULL,
    // this is fts_open() requirement;
    // thus grow_str is unusable: length(NULL) => segfault
    sp.str = xrealloc(sp.str, sizeof(char*) * ++sp.size);
    sp.str[sp.size-1] = NULL;

    // init regexp errmsg buffer
    if (opt.re || filename_regexp)
        reg_error_str = xmalloc(reg_error_str_len);
    if (opt.re && opt.cas)
        opt.re |= REG_ICASE;

    /* Compile regexp for file name */
    if (filename_regexp) {
        int err_code,
            flags = REG_EXTENDED | REG_NOSUB;
        if (filename_case)
            flags |= REG_ICASE;

        // allocate regexp
        opt.file_re = xmalloc(sizeof(regex_t));
        //compile regexp
        if (( err_code = regcomp(opt.file_re, filename_regexp, flags) ))
        {
            regerror(err_code, opt.file_re, reg_error_str, reg_error_str_len);
            error(ERR_PARSE, errno, "failed to compile file name regular expression '%s': %s",
                  filename_regexp, reg_error_str);
        }
    }

    /* read from stdin if no symbols are specified */
    if (optind == argc) {
        char *line = xmalloc(line_buf);
        /* read whole line from stdin */
        while (getline(&line, &line_buf, stdin) != -1)
        {   // -1 means both EOF and read error
            if (errno)
                error(ERR_IO, errno, "i/o error: can't read list of symbols from stdin");
            /* parse line */
            if ((tail = strtok(line, " \t\r\n\f\v")))
                do
                    //parse different symbols
                    grow_sym(tail);
                while ((tail = strtok(NULL, " \t\r\n\f\v")));
        }
        free(line);
    }
    else /* get symbols from command line */
        while (optind < argc)
            grow_sym(argv[optind++]);

#if (defined(HAVE_RPM) || defined(HAVE_PORTAGE))
    init_packages();
#endif //(defined(HAVE_RPM) || defined(HAVE_PORTAGE))

    /* parse sort suboptions, this can't be done in the main switch
       due to dependance on other suboptions */
    if (opt.sort.cnt)
        construct_sort_sequence();
}
