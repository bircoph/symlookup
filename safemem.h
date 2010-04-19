/*
 *  general safe memory allocation routines
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

#ifndef SL_SAFEMEM_H
#define SL_SAFEMEM_H

#include <stdlib.h>
#include <error.h>
#include <string.h>

#include "symlookup.h"

//safe malloc
static inline void* xmalloc(register const size_t size)
{
    register void* value = malloc(size);
    if (!value)
        error(ERR_MEM, errno, "can't allocate %i bytes of memory!", size);
    return value;
}

//safe calloc
static inline void* xcalloc(register const size_t count, register const size_t eltsize)
{
    register void* value = calloc(count, eltsize);
    if (!value)
        error(ERR_MEM, errno, "can't allocate %i bytes of memory!", count * eltsize);
    return value;
}

//safe realloc
static inline void* xrealloc(register void* const ptr, register const size_t size)
{
    register void* value = realloc(ptr, size);
    if (!value)
        error(ERR_MEM, errno, "can't allocate %i bytes of memory!", size);
    return value;
}

//allocate new string
static char* alloc_str(register const char* const str)
{
    size_t length = strlen(str)+1;      //length of new string
    register char* val;
    //allocate memory for new string
    val = xmalloc(length);
    //put actual value
    memcpy(val, str, length);
    return val;
}

/* add new element to string array (char**) */
static inline void add_str(register char*** const arr, register const unsigned int size,
                           register const char* const str)
{
    //new element in the char* array
    *arr = xrealloc(*arr, sizeof(char*) * (size + 1));
    (*arr)[size] = alloc_str(str);
}

/* grow string array by adding new element <str> */
static inline void grow_str(register struct str_t* const arr, register const char* const str)
{
    add_str(&arr->str, arr->size, str);
    arr->size++;    //+1 element
}

/* free string array */
static inline void free_str(register struct str_t *arr)
{
    for (unsigned int i=0; i < arr->size; i++)
        free(arr->str[i]);
    free(arr->str);
}

#endif /* SL_SAFEMEM_H */
