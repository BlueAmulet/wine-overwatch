/*
 * Copyright 2014 Martin Storsjo
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

#include "objbase.h"
#include "initguid.h"
#define COBJMACROS
#include "roapi.h"
#include "winstring.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(combase);

static const char *debugstr_hstring(HSTRING hstr)
{
    const WCHAR *str;
    UINT32 len;
    if (hstr && !((ULONG_PTR)hstr >> 16)) return "(invalid)";
    str = WindowsGetStringRawBuffer(hstr, &len);
    return wine_dbgstr_wn(str, len);
}

static HRESULT get_library_for_classid(const WCHAR *classid, WCHAR **out)
{
    static const WCHAR classkeyW[] = {'S','o','f','t','w','a','r','e','\\',
                                      'M','i','c','r','o','s','o','f','t','\\',
                                      'W','i','n','d','o','w','s','R','u','n','t','i','m','e','\\',
                                      'A','c','t','i','v','a','t','a','b','l','e','C','l','a','s','s','I','d',0};
    static const WCHAR dllpathW[] = {'D','l','l','P','a','t','h',0};
    HKEY hkey_root, hkey_class;
    DWORD type, size;
    HRESULT hr;
    WCHAR *buf = NULL;

    *out = NULL;

    /* load class registry key */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, classkeyW, 0, KEY_READ, &hkey_root))
        return REGDB_E_READREGDB;
    if (RegOpenKeyExW(hkey_root, classid, 0, KEY_READ, &hkey_class))
    {
        WARN("Class %s not found in registry\n", debugstr_w(classid));
        RegCloseKey(hkey_root);
        return REGDB_E_CLASSNOTREG;
    }
    RegCloseKey(hkey_root);

    /* load (and expand) DllPath registry value */
    if (RegQueryValueExW(hkey_class, dllpathW, NULL, &type, NULL, &size))
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ)
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (!(buf = HeapAlloc(GetProcessHeap(), 0, size)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (RegQueryValueExW(hkey_class, dllpathW, NULL, NULL, (BYTE *)buf, &size))
    {
        hr = REGDB_E_READREGDB;
        goto done;
    }
    if (type == REG_EXPAND_SZ)
    {
        WCHAR *expanded;
        DWORD len = ExpandEnvironmentStringsW(buf, NULL, 0);
        if (!(expanded = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR))))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        ExpandEnvironmentStringsW(buf, expanded, len);
        HeapFree(GetProcessHeap(), 0, buf);
        buf = expanded;
    }

    *out = buf;
    return S_OK;

done:
    HeapFree(GetProcessHeap(), 0, buf);
    RegCloseKey(hkey_class);
    return hr;
}

/***********************************************************************
 *      RoInitialize (combase.@)
 */
HRESULT WINAPI RoInitialize(RO_INIT_TYPE type)
{
    switch (type) {
    case RO_INIT_SINGLETHREADED:
        return CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    default:
        FIXME("type %d\n", type);
    case RO_INIT_MULTITHREADED:
        return CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }
}

/***********************************************************************
 *      RoUninitialize (combase.@)
 */
void WINAPI RoUninitialize(void)
{
    CoUninitialize();
}

/***********************************************************************
 *      RoGetActivationFactory (combase.@)
 */
HRESULT WINAPI RoGetActivationFactory(HSTRING classid, REFIID iid, void **class_factory)
{
    PFNGETACTIVATIONFACTORY pDllGetActivationFactory;
    IActivationFactory *factory;
    WCHAR *library;
    HMODULE module;
    HRESULT hr;

    FIXME("(%s, %s, %p): semi-stub\n", debugstr_hstring(classid), debugstr_guid(iid), class_factory);

    if (!iid || !class_factory)
        return E_INVALIDARG;

    hr = get_library_for_classid(WindowsGetStringRawBuffer(classid, NULL), &library);
    if (FAILED(hr))
    {
        ERR("Failed to find library for %s\n", debugstr_hstring(classid));
        return hr;
    }

    if (!(module = LoadLibraryW(library)))
    {
        ERR("Failed to load module %s\n", debugstr_w(library));
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }

    if (!(pDllGetActivationFactory = (void *)GetProcAddress(module, "DllGetActivationFactory")))
    {
        ERR("Module %s does not implement DllGetActivationFactory\n", debugstr_w(library));
        hr = E_FAIL;
        goto done;
    }

    TRACE("Found library %s for class %s\n", debugstr_w(library), debugstr_hstring(classid));

    hr = pDllGetActivationFactory(classid, &factory);
    if (SUCCEEDED(hr))
    {
        hr = IActivationFactory_QueryInterface(factory, iid, class_factory);
        if (SUCCEEDED(hr))
        {
            TRACE("Created interface %p\n", *class_factory);
            module = NULL;
        }
        IActivationFactory_Release(factory);
    }

done:
    HeapFree(GetProcessHeap(), 0, library);
    if (module) FreeLibrary(module);
    return hr;
}

/***********************************************************************
 *      RoActivateInstance (combase.@)
 */
HRESULT WINAPI RoActivateInstance(HSTRING classid, IInspectable **instance)
{
    IActivationFactory *factory;
    HRESULT hr;

    FIXME("(%s, %p): semi-stub\n", debugstr_hstring(classid), instance);

    hr = RoGetActivationFactory(classid, &IID_IActivationFactory, (void **)&factory);
    if (SUCCEEDED(hr))
    {
        hr = IActivationFactory_ActivateInstance(factory, instance);
        IActivationFactory_Release(factory);
    }

    return hr;
}

/***********************************************************************
 *      RoGetApartmentIdentifier (combase.@)
 */
HRESULT WINAPI RoGetApartmentIdentifier(UINT64 *identifier)
{
    FIXME("(%p): stub\n", identifier);

    if (!identifier)
        return E_INVALIDARG;

    *identifier = 0xdeadbeef;
    return S_OK;
}

/***********************************************************************
 *      RoRegisterForApartmentShutdown (combase.@)
 */
HRESULT WINAPI RoRegisterForApartmentShutdown(IApartmentShutdown *callback,
        UINT64 *identifier, APARTMENT_SHUTDOWN_REGISTRATION_COOKIE *cookie)
{
    HRESULT hr;

    FIXME("(%p, %p, %p): stub\n", callback, identifier, cookie);

    hr = RoGetApartmentIdentifier(identifier);
    if (FAILED(hr))
        return hr;

    *cookie = (void *)0xcafecafe;
    return S_OK;
}

/***********************************************************************
 *      RoGetServerActivatableClasses (combase.@)
 */
HRESULT WINAPI RoGetServerActivatableClasses(HSTRING name, HSTRING **classes, DWORD *count)
{
    FIXME("(%s, %p, %p): stub\n", debugstr_hstring(name), classes, count);

    *count = 0;
    return S_OK;
}

/***********************************************************************
 *      RoRegisterActivationFactories (combase.@)
 */
HRESULT WINAPI RoRegisterActivationFactories(HSTRING *classes, PFNGETACTIVATIONFACTORY *callbacks,
                                             UINT32 count, RO_REGISTRATION_COOKIE *cookie)
{
    UINT32 i;

    FIXME("(%p, %p, %d, %p): stub\n", classes, callbacks, count, cookie);

    for (i = 0; i < count; i++)
        FIXME("  %s\n", debugstr_hstring(classes[i]));

    return S_OK;
}

/***********************************************************************
 *      CleanupTlsOleState (combase.@)
 */
void WINAPI CleanupTlsOleState(void *unknown)
{
    FIXME("(%p): stub\n", unknown);
}
