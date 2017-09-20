/*
 * Copyright (c) 2016 Michael Müller
 * Copyright 2017 Andrey Gusev
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

#include "shlwapi.h"
#include "strsafe.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(kernelbase);

/***********************************************************************
 *           QuirkIsEnabled   (KERNELBASE.@)
 */
BOOL WINAPI QuirkIsEnabled(void *arg)
{
    FIXME("(%p): stub\n", arg);
    return FALSE;
}

/***********************************************************************
 *          QuirkIsEnabled3 (KERNELBASE.@)
 */
BOOL WINAPI QuirkIsEnabled3(void *unk1, void *unk2)
{
    FIXME("(%p, %p) stub!\n", unk1, unk2);
    return FALSE;
}

/***********************************************************************
 *          PathCchCombineEx (KERNELBASE.@)
 */
HRESULT WINAPI PathCchCombineEx(WCHAR *out, SIZE_T size, const WCHAR *path1, const WCHAR *path2, DWORD flags)
{
    WCHAR result[MAX_PATH];

    FIXME("(%p, %lu, %s, %s, %x): semi-stub\n", out, size, wine_dbgstr_w(path1), wine_dbgstr_w(path2), flags);

    if (!out || !size) return E_INVALIDARG;
    if (flags) FIXME("Flags %x not supported\n", flags);

    if (!PathCombineW(result, path1, path2))
        return E_INVALIDARG;

    if (strlenW(result) + 1 > size)
    {
        out[0] = 0;
        return STRSAFE_E_INSUFFICIENT_BUFFER;
    }

    strcpyW(out, result);
    return S_OK;
}
