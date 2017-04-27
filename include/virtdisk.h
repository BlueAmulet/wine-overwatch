/*
 * Copyright 2017 Louis Lenders
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

#ifndef __WINE_VIRTDISK_DLL_H
#define __WINE_VIRTDISK_DLL_H

#include "windef.h"
#include "wine/debug.h"
#include "winbase.h"
#include "winnt.h"

typedef enum _GET_STORAGE_DEPENDENCY_FLAG
{
    GET_STORAGE_DEPENDENCY_FLAG_NONE         = 0x00000000,
    GET_STORAGE_DEPENDENCY_FLAG_HOST_VOLUMES = 0x00000001,
    GET_STORAGE_DEPENDENCY_FLAG_DISK_HANDLE  = 0x00000002,
} GET_STORAGE_DEPENDENCY_FLAG;

typedef enum _DEPENDENT_DISK_FLAG
{
    DEPENDENT_DISK_FLAG_NONE                 = 0x00000000,
    DEPENDENT_DISK_FLAG_MULT_BACKING_FILES   = 0x00000001,
    DEPENDENT_DISK_FLAG_FULLY_ALLOCATED      = 0x00000002,
    DEPENDENT_DISK_FLAG_READ_ONLY            = 0x00000004,
    DEPENDENT_DISK_FLAG_REMOTE               = 0x00000008,
    DEPENDENT_DISK_FLAG_SYSTEM_VOLUME        = 0x00000010,
    DEPENDENT_DISK_FLAG_SYSTEM_VOLUME_PARENT = 0x00000020,
    DEPENDENT_DISK_FLAG_REMOVABLE            = 0x00000040,
    DEPENDENT_DISK_FLAG_NO_DRIVE_LETTER      = 0x00000080,
    DEPENDENT_DISK_FLAG_PARENT               = 0x00000100,
    DEPENDENT_DISK_FLAG_NO_HOST_DISK         = 0x00000200,
    DEPENDENT_DISK_FLAG_PERMANENT_LIFETIME   = 0x00000400,
} DEPENDENT_DISK_FLAG;

typedef enum _STORAGE_DEPENDENCY_INFO_VERSION
{
    STORAGE_DEPENDENCY_INFO_VERSION_UNSPECIFIED = 0x0,
    STORAGE_DEPENDENCY_INFO_VERSION_1           = 0x1,
    STORAGE_DEPENDENCY_INFO_VERSION_2           = 0x2
} STORAGE_DEPENDENCY_INFO_VERSION;

typedef struct _VIRTUAL_STORAGE_TYPE
{
    ULONG DeviceId;
    GUID  VendorId;
} VIRTUAL_STORAGE_TYPE, *PVIRTUAL_STORAGE_TYPE;

typedef struct _STORAGE_DEPENDENCY_INFO_TYPE_1
{
    DEPENDENT_DISK_FLAG  DependencyTypeFlags;
    ULONG                ProviderSpecificFlags;
    VIRTUAL_STORAGE_TYPE VirtualStorageType;
} STORAGE_DEPENDENCY_INFO_TYPE_1, *PSTORAGE_DEPENDENCY_INFO_TYPE_1;

typedef struct _STORAGE_DEPENDENCY_INFO_TYPE_2
{
    DEPENDENT_DISK_FLAG  DependencyTypeFlags;
    ULONG                ProviderSpecificFlags;
    VIRTUAL_STORAGE_TYPE VirtualStorageType;
    ULONG                AncestorLevel;
    PWSTR                DependencyDeviceName;
    PWSTR                HostVolumeName;
    PWSTR                DependentVolumeName;
    PWSTR                DependentVolumeRelativePath;
} STORAGE_DEPENDENCY_INFO_TYPE_2, *PSTORAGE_DEPENDENCY_INFO_TYPE_2;

typedef struct _STORAGE_DEPENDENCY_INFO
{
    STORAGE_DEPENDENCY_INFO_VERSION Version;
    ULONG                           NumberEntries;
    __C89_NAMELESS union
    {
        STORAGE_DEPENDENCY_INFO_TYPE_1 Version1Entries[1];
        STORAGE_DEPENDENCY_INFO_TYPE_2 Version2Entries[1];
    } __C89_NAMELESSUNIONNAME;
} STORAGE_DEPENDENCY_INFO, *PSTORAGE_DEPENDENCY_INFO;

DWORD WINAPI GetStorageDependencyInformation(HANDLE obj, GET_STORAGE_DEPENDENCY_FLAG flags, ULONG size, STORAGE_DEPENDENCY_INFO *info, ULONG *used);

#endif	/* __WINE_VIRTDISK_DLL_H */
