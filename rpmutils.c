/*
 *  rpm utilities
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

#ifdef HAVE_RPM

#include <errno.h>
#include <error.h>
#include <string.h>

#ifndef HAVE_RPM_5
    #include <rpmlib.h>
    #include <rpmts.h>
#else
    #include <rpm4compat.h>
    #include <rpmtag.h>
    #define headerNVR(p,a,b,c) headerNEVRA(p,a,NULL,b,c,NULL)
#endif //HAVE_RPM_5
#include <rpmdb.h>

#include "symlookup.h"
#include "safemem.h"
#include "output.h"
#include "rpmutils.h"

/* rpm vars */
rpmts rpm_transaction;  //rpm transaction set
char *prevfile = NULL;
struct str_t *rpm_arr;  //matched rpms (conforms with filearr)

/* extract full rpm name from header
   memory is to be freed outside */
static inline char* get_rpmname(const Header header)
{
#define DELIM '-'
    char *full, *tmp;
    const char *name, *version, *release;
    size_t s1, s2, s3; //string lengthes

    headerNVR(header, &name, &version, &release);
    s1 = strlen(name);
    s2 = strlen(version);
    s3 = strlen(release);

    //string format "name-version-release"
    full = xmalloc(s1 + s2 + s3 + 3);
    //join strings (I hope it is faster than sprintf)
    tmp = mempcpy(full, name,    s1);
    tmp[0] = DELIM; tmp++;
    tmp = mempcpy(tmp,  version, s2);
    tmp[0] = DELIM; tmp++;
    tmp = mempcpy(tmp,  release, s3);
    tmp[0] = '\0';

    return full;
}

/* find rpm containing file "filename"
   memory must be allocated and freed outside this function;
   return NULL if file doesn't owned by any package */
void rpm_check(const char* const filename, struct str_t* const rpmname)
{
    static rpmdbMatchIterator iter;     //db iterator
    static Header header;               //header for rpm from db
    /* it is possible for file to be owned by several packages */
    rpmname->size = 0;
    rpmname->str = NULL;
    static char* name;

    if ((iter = rpmtsInitIterator(rpm_transaction, RPMTAG_BASENAMES, filename, 0)))
    {
        while ((header = rpmdbNextIterator(iter))) {
            name = get_rpmname(header);
            grow_str(rpmname, name);
            free (name);
        }

        //release iterator
        rpmdbFreeIterator(iter);
    }
    //sanity check for empty result set
    if (opt.verb && iter && !rpmname->size)
        error(0, 0, "warning: rpm query for file `%s` was returned but is empty!", filename);
}

/* create printable list of rpm names
   memory must be freed by caller */
void listrpm(const char *const filename, const char *const symbolname,
             const char *const pattern)
{
    //do not check the same file twice
    if (!prevfile || strcmp(filename, prevfile))
    {
        free_str(rpm_arr);  //&rpm_arr[0]
        free(prevfile);
        rpm_check(filename, rpm_arr);
        prevfile = alloc_str(filename);
    }

    static char *list;
    static const char* const str_no_match = "<no matches>";
    if (!rpm_arr->size)
        //table output is different a bit
        if (opt.tbl)
            if (!opt.sort.match)
                printf(outfmt, filename, str_no_match, symbolname);
            else
                printf(outfmt, pattern, filename, str_no_match, symbolname);
        else
            list = alloc_str(str_no_match);
    else {
        //new raw in table for each entry
        if (opt.tbl)
            if (!opt.sort.match)
                for (unsigned int i=0; i < rpm_arr->size; i++)
                    printf(outfmt, filename, rpm_arr->str[i], symbolname);
            else
                for (unsigned int i=0; i < rpm_arr->size; i++)
                    printf(outfmt, pattern, filename, rpm_arr->str[i], symbolname);

        else
        {
            list = alloc_str(rpm_arr->str[0]);
            //check for several matches
            if (rpm_arr->size > 1)
            {
                static size_t *len; //size of i-th matched name
                static size_t sum;  //total length
                len = xmalloc(sizeof(size_t) * rpm_arr->size);
                sum = 0;

                //get total length
                for (unsigned int i=0; i < rpm_arr->size; i++) {
                    len[i] = strlen(rpm_arr->str[i]);
                    sum += len[i];
                }
                /* result string
                   format: "name0[, name1[, name2[...]]]" */
                list = xrealloc(list, sizeof(char) * (sum + (rpm_arr->size -1) *2 +1));

                //concatenate strings
                static char *tmp;   //pointer to current position
                tmp= list + len[0];
                for (unsigned int i=1; i < rpm_arr->size; i++)
                {
                    tmp[0]=','; tmp[1]=' ';
                    tmp+=2;
                    tmp = mempcpy(tmp, rpm_arr->str[i], len[i]);
                }
                tmp[0] = '\0';
                free(len);
            }
        }
    }

    if (!opt.tbl) {
        printf("%s (rpm: %s):\t%s\n", filename, list, symbolname);
        free(list);
    }
}

/* init rpm */
void rpminit()
{
    if (!opt.verb)
        //report only fatal errors
        rpmSetVerbosity(RPMMESS_FATALERROR);
    //honor configs
    if (rpmReadConfigFiles(NULL, NULL) && opt.verb)
        error(0, errno, "warning: can't read rpm config files");

    /* init rpm db transaction set */
    if ( !(rpm_transaction = rpmtsCreate()) ) {
        if (opt.verb)
            error(0, errno, "error: cannot init rpm transaction set!\n"
                            "rpm database is probably absent or broken,\n"
                            "disabling rpm support.");
        opt.rpm = 0;
    }
    /* init rpm root if necessary */
    if (opt.rpmroot)
        rpmtsSetRootDir(rpm_transaction, opt.rpmroot);

    if (opt.verb >= V_VERBOSE)
        error(0, 0, "info: rpm api initialized.");

    //check for rpms without sorting => use storage for 1 rpm match
    if (opt.rpm && !opt.sort.cnt) {
        rpm_arr = xmalloc(sizeof(struct str_t));
        rpm_arr[0].size=0;
        rpm_arr[0].str=NULL;
    }
}

/* uninit rpm */
void rpmuninit()
{
    rpmtsFree(rpm_transaction);
    if (opt.verb >= V_VERBOSE)
        error(0, 0, "info: rpm database closed.");
}
#endif //HAVE_RPM
