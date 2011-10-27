/*
 *  ELF and AR files processing code,
 *  symbol detection code
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

#include <errno.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <gelf.h>
#include <regex.h>

#include "symlookup.h"
#include "scanelf.h"

/* check if symbol <name> is wanted
   1 == stop search in current file
   0 == continue */
static inline void check_symbol(const char* const symbolname, const char* const filename)
{
    /* iterate through user-provided symbols */
    for (unsigned int i=0; i < symbol.size; i++)
        if (!opt.re)    //usual comparison
        {
            if (!compare_func(symbol.str[i], symbolname)) {
                do_match(i, filename, symbolname);
                if (!opt.cas)
                    break; //if ICASE, several matches are possible
            }
        } else {    //regexp
            int res_code;
            res_code = regexec(&symbol.regstr[i], symbolname, 0, NULL, 0);
            switch (res_code) {
                case REG_NOMATCH:
                    break;
                case 0:
                    do_match(i, filename, symbolname);
                    break;
                default:
                    if (opt.verb) {
                        regerror(res_code, &symbol.regstr[symbol.size], reg_error_str, reg_error_str_len);
                        error(0, errno, "warning: can't execute regular expression '%s': %s",
                              symbol.str[i], reg_error_str);
                    }
                    break;
            }
        }
}

/* parse object file, common for both elf and ar files */
/* type:
   ELF = 1;
   AR  = 0; */
static void readelf(Elf* const elf, const char* const filename, const unsigned int type)
{
    unsigned int count;
    char *name;         //symbol's name

    GElf_Ehdr ehdr;     //elf header
    Elf_Scn *section;   //elf section pointer
    GElf_Shdr shdr;     //section header
    Elf_Data *data;     //section data pointer
    GElf_Sym sym;       //symbol from obj file

    /* type-dependant vars (elf | ar) */
    Elf64_Half  e_type;
    Elf64_Word  sh_type;
    char *sh_type_str, *e_type_str;

    if (type) { //working with pure elf
        e_type = ET_DYN;
        e_type_str = "DYN";
        sh_type = SHT_DYNSYM;
        sh_type_str = "DYNSYM";
    }
    else {      //working with elf objects from ar archive
        e_type = ET_REL;
        e_type_str = "REL";
        sh_type = SHT_SYMTAB;
        sh_type_str = "SYMTAB";
    }

    if (gelf_getehdr(elf, &ehdr))
    /* check header for DYN | REL obj type */
    if (ehdr.e_type == e_type) {
        section = NULL;
        data = NULL;

        /* iterate trough elf sections, several tables are possible */
        while ((section = elf_nextscn(elf, section))) {
            if (gelf_getshdr(section, &shdr) != &shdr) {
                if (opt.verb)
                    error(0, elf_errno(), "error: can't read header for section %-4zi"
                          " in file %s", elf_ndxscn(section), filename);
                continue;
            }
            /* DYNSYM | SYMTAB section found */
            if (shdr.sh_type == sh_type) {
                count = shdr.sh_size / shdr.sh_entsize; //get number of symbols

                if ((data = elf_getdata(section, data))) {
                    // sh_info -- index of 1st non-local symbol
                    for (unsigned int i=shdr.sh_info; i < count; i++ ) {
                        if (gelf_getsym(data, i, &sym)) {
                            /* skip undefined symbols, read name of symbol */
                            if (sym.st_shndx != SHN_UNDEF) {
                                if ((name = elf_strptr(elf, shdr.sh_link, sym.st_name)))
                                    // got it!
                                    check_symbol(name, filename);
                                else {      //can't convert symbol's name
                                    if (opt.verb)
                                        error(0, elf_errno(), "error: can't read name of symbol "
                                              "%i from %s setion in %s", i, sh_type_str, filename);
                                }
                            } //symbol.sh_shndx
                        } //gelf_getsym
                        else {  //can't get symbol
                            if (opt.verb)
                                error(0, elf_errno(), "error: can't read symbol %i from "
                                      "%s setion in %s", i, sh_type_str, filename);
                        }
                    }
                }
                else {  //can't read data from section
                    if (opt.verb)
                        error(0, elf_errno(), "warning: can't read data from %s "
                              "setion in %s", sh_type_str, filename);
                }
            }
        } //end while
    }
    else {      //not DYN | REL object type
        if (opt.verb)
            error(0, 0, "%s ELF type is not %s, it is 0x%x", filename, e_type_str, ehdr.e_type);
    }
    else {      //broken header
        if (opt.verb)
            error(0, elf_errno(), "warning: can't read ELF header in %s", filename);
    }
}

/* must take name of ordinary file to access from current directory,
 * full file name from the root of traversal (in order to show it for
 * user), and last name only, it is already returned by fts,
 * so I won't waste CPU time */
void checkfile (const char* const filename,
                const char* const fullfilename,
                const char* const name)
{
    static unsigned int so, ar;
    static int fd;
    static Elf *elf, *elf_ar;   //elf, Ar object pointer
    static Elf_Kind elf_type;   //elf type enum

    /* name regular expression check */
    if (opt.file_re) {
        int res_code;
        res_code = regexec(opt.file_re, name, 0, NULL, 0);
        switch (res_code) {
            case REG_NOMATCH:
                return;
                break;
            case 0:
                break;
            default:
                if (opt.verb) {
                    regerror(res_code, opt.file_re, reg_error_str, reg_error_str_len);
                    error(0, errno, "warning: can't execute file name regular expression: %s",
                          reg_error_str);
                }
                break;
        }
    }
    /* extension check */
    if (opt.ext) {
        // select so by: ".*\.so\..*" || so = ".*\.so$"
        if ( opt.so && (strstr(name, ".so.") ||
             !strcmp(name + strlen(name)-3 ,".so")) ) {
            so = 1;
            ar = 0;
        }
        else if (opt.ar && !strcmp(name + strlen(name)-2 ,".a")) {
            so = 0;
            ar = 1;
        }
        else
            return;
    }   //skip extension test, only honour CLI options
    else {
        so = opt.so;
        ar = opt.ar;
    }

    /* preliminary reading of ELF-file */
    if ((fd = open(filename, O_RDONLY)) == -1) {
        if (opt.verb)
            error(0, errno, "warning: can't open file %s for reading", fullfilename);
        return;
    }
    // init elf object
    if (!(elf = elf_begin(fd, ELF_C_READ, NULL)) && opt.verb)
        error(0, elf_errno(), "error: elf_begin() failed for %s", fullfilename);
    //ensure that file is ELF or AR
    else {
        elf_type = elf_kind(elf);
        /* elf & requested */
        if (elf_type == ELF_K_ELF && so)
            readelf(elf, fullfilename, 1);
        /* ar & requested */
        else if (elf_type == ELF_K_AR && ar) {
            /* iterate through ar archive, elf = ar header pointer */
            Elf_Arhdr *arh;
            Elf_Cmd cmd;

            cmd = ELF_C_READ;
            while ((elf_ar = elf_begin(fd, cmd, elf))) {
                if (!(arh = elf_getarhdr(elf_ar)) )
                {
                    if (opt.verb)
                        error(0, elf_errno(), "error: can't read ar header in %s", fullfilename);
                    continue;
                }
                //omit archive symbol (/) and string (//) tables
                if (strcmp(arh->ar_name, "/") && strcmp(arh->ar_name, "//"))
                    readelf(elf_ar, fullfilename, 0);

                //at the EOF cmd will be changed to ELF_C_NULL
                cmd = elf_next(elf_ar);
                elf_end(elf_ar);
            }
        }
        else if (opt.verb >= V_VERBOSE)
            error(0, 0, "%s is not an %s file", fullfilename,
                 (ar && so) ? "so neither an ar" : (so) ? "so" : "ar" );

        // free mem & close
        elf_end(elf);
    }
    if (close(fd) == -1 && opt.verb)
        error(0, errno, "error: can't close file %s; "
                        "subsequent processing may be unreliable", fullfilename);
}

