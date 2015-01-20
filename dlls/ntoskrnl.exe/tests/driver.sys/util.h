/*
 * ntoskrnl.exe testing framework
 *
 * Copyright 2015 Sebastian Lackner
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stddef.h>

static inline const char* kernel_strrchr(const char *str, int character)
{
    const char *ret = NULL;
    for (; *str; str++)
    {
        if (*str == character)
            ret = str;
    }
    return ret;
}

static inline void* kernel_memcpy(void *destination, const void *source, size_t num)
{
    const char *src = source;
    char *dst = destination;
    while (num--) *dst++ = *src++;
    return destination;
}

static inline void* kernel_memset(void *ptr, int value, size_t num)
{
    char *dst = ptr;
    while (num--) *dst++ = value;
    return ptr;
}
