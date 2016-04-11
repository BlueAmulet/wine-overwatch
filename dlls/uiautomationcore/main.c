/*
 * uiautomationcore API
 *
 * Copyright 2016 Michael Müller
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
#include "wine/debug.h"

#define COBJMACROS
#include "uiautomationcoreapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(uiautomation);

static HRESULT WINAPI dummy_QueryInterface(IUnknown *iface, REFIID iid, void **ppv)
{
    TRACE("(%p, %s, %p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (!IsEqualIID(&IID_IUnknown, iid))
    {
        FIXME("Unknown interface: %s\n", debugstr_guid(iid));
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    *ppv = iface;
    IUnknown_AddRef((IUnknown *)*ppv);
    return S_OK;
}

static ULONG WINAPI dummy_AddRef(IUnknown *iface)
{
    FIXME("(%p): stub\n", iface);
    return 1;
}

static ULONG WINAPI dummy_Release(IUnknown *iface)
{
    FIXME("(%p): stub\n", iface);
    return 1;
}

static const IUnknownVtbl dummy_Vtbl =
{
    dummy_QueryInterface,
    dummy_AddRef,
    dummy_Release,
};

static IUnknown dummy = { &dummy_Vtbl };

int WINAPI UiaLookupId(AutomationIdentifierType type, const GUID *guid)
{
    FIXME("(%d, %s)\n", type, debugstr_guid(guid));
    return 1;
}

HRESULT WINAPI UiaGetReservedMixedAttributeValue(IUnknown **value)
{
    FIXME("(%p): stub\n", value);

    *value = &dummy;
    return S_OK;
}

HRESULT WINAPI UiaGetReservedNotSupportedValue(IUnknown **value)
{
    FIXME("(%p): stub\n", value);

    *value = &dummy;
    return S_OK;
}

LRESULT WINAPI UiaReturnRawElementProvider(HWND hwnd, WPARAM wparam, LPARAM lparam,
                                           IRawElementProviderSimple *provider)
{
    FIXME("(%p, %lx, %lx, %p): stub\n", hwnd, wparam, lparam, provider);
    return 0;
}


BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    TRACE("(%p, %u, %p)\n", instance, reason, reserved);

    switch (reason)
    {
        case DLL_WINE_PREATTACH:
            return FALSE;    /* prefer native version */
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            break;
    }

    return TRUE;
}
