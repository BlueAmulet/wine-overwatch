/*
 * ntoskrnl.exe testing framework
 *
 * Copyright 2015 Sebastian Lackner
 * Copyright 2015 Michael Müller
 * Copyright 2015 Christian Costa
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

#include "test.h"

#define WINE_IOCTL_PsGetCurrentProcessId           WINE_TEST_IOCTL(0)
#define WINE_IOCTL_PsGetCurrentThread              WINE_TEST_IOCTL(1)
#define WINE_IOCTL_NtBuildNumber                   WINE_TEST_IOCTL(2)
#define WINE_IOCTL_ExInitializeNPagedLookasideList WINE_TEST_IOCTL(3)
#define WINE_IOCTL_LdrModules                      WINE_TEST_IOCTL(4)

struct test_PsGetCurrentProcessId
{
    DWORD pid;
};
