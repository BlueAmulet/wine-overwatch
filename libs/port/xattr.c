/*
 * extended attributes functions
 *
 * Copyright 2014 Erich E. Hoover
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

#include "config.h"
#include "wine/port.h"

#if defined(HAVE_ATTR_XATTR_H)
# include <attr/xattr.h>
#endif

#include <ctype.h>
#include <errno.h>

int xattr_fget( int filedes, const char *name, void *value, size_t size )
{
#if defined(HAVE_ATTR_XATTR_H)
    return fgetxattr( filedes, name, value, size );
#else
    errno = ENOSYS;
    return -1;
#endif
}
