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

#include "config.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "virtdisk.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(virtdisk);

/*****************************************************
 *    DllMain (VIRTDISK.@)
 */
BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, void *reserved)
{
    TRACE("(%p, %d, %p)\n", hinst, reason, reserved);

    switch (reason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinst);
            break;
    }

    return TRUE;
}

DWORD WINAPI GetStorageDependencyInformation(HANDLE obj, GET_STORAGE_DEPENDENCY_FLAG flags, ULONG size, STORAGE_DEPENDENCY_INFO *info, ULONG *used)
{
    FIXME("(%p, 0x%x, %u, %p, %p): stub\n", obj, flags, size, info, used);

    if (used) *used = sizeof(STORAGE_DEPENDENCY_INFO);

    if (!info || !size)
        return ERROR_SUCCESS;

    if (size < sizeof(STORAGE_DEPENDENCY_INFO))
        return ERROR_INSUFFICIENT_BUFFER;

    info->NumberEntries = 0;

    return ERROR_SUCCESS;
}
