/*
 *
 * Copyright 2014 Austin English
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

#include <stdarg.h>
#include <string.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"

#include "initguid.h"
#include "mfapi.h"
#include "mferror.h"

#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfplat);

static WCHAR transform_keyW[] = {'S','o','f','t','w','a','r','e','\\','C','l','a','s','s','e','s',
                                 '\\','M','e','d','i','a','F','o','u','n','d','a','t','i','o','n','\\',
                                 'T','r','a','n','s','f','o','r','m','s',0};
static WCHAR categories_keyW[] = {'S','o','f','t','w','a','r','e','\\','C','l','a','s','s','e','s',
                                 '\\','M','e','d','i','a','F','o','u','n','d','a','t','i','o','n','\\',
                                 'T','r','a','n','s','f','o','r','m','s','\\',
                                 'C','a','t','e','g','o','r','i','e','s',0};
static WCHAR inputtypesW[]  = {'I','n','p','u','t','T','y','p','e','s',0};
static WCHAR outputtypesW[] = {'O','u','t','p','u','t','T','y','p','e','s',0};
static const WCHAR szGUIDFmt[] =
{
    '%','0','8','x','-','%','0','4','x','-','%','0','4','x','-','%','0',
    '2','x','%','0','2','x','-','%','0','2','x','%','0','2','x','%','0','2',
    'x','%','0','2','x','%','0','2','x','%','0','2','x',0
};

static const BYTE guid_conv_table[256] =
{
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x00 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x20 */
    0,   1,   2,   3,   4,   5,   6, 7, 8, 9, 0, 0, 0, 0, 0, 0, /* 0x30 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    0,   0,   0,   0,   0,   0,   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
    0, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf                             /* 0x60 */
};

static LPWSTR GUIDToString(LPWSTR lpwstr, REFGUID lpcguid)
{
    wsprintfW(lpwstr, szGUIDFmt, lpcguid->Data1, lpcguid->Data2,
        lpcguid->Data3, lpcguid->Data4[0], lpcguid->Data4[1],
        lpcguid->Data4[2], lpcguid->Data4[3], lpcguid->Data4[4],
        lpcguid->Data4[5], lpcguid->Data4[6], lpcguid->Data4[7]);

    return lpwstr;
}

static inline BOOL is_valid_hex(WCHAR c)
{
    if (!(((c >= '0') && (c <= '9'))  ||
          ((c >= 'a') && (c <= 'f'))  ||
          ((c >= 'A') && (c <= 'F'))))
        return FALSE;
    return TRUE;
}

static BOOL GUIDFromString(LPCWSTR s, GUID *id)
{
    int i;

    /* in form XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX */

    id->Data1 = 0;
    for (i = 0; i < 8; i++)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data1 = (id->Data1 << 4) | guid_conv_table[s[i]];
    }
    if (s[8]!='-') return FALSE;

    id->Data2 = 0;
    for (i = 9; i < 13; i++)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data2 = (id->Data2 << 4) | guid_conv_table[s[i]];
    }
    if (s[13]!='-') return FALSE;

    id->Data3 = 0;
    for (i = 14; i < 18; i++)
    {
        if (!is_valid_hex(s[i])) return FALSE;
        id->Data3 = (id->Data3 << 4) | guid_conv_table[s[i]];
    }
    if (s[18]!='-') return FALSE;

    for (i = 19; i < 36; i+=2)
    {
        if (i == 23)
        {
            if (s[i]!='-') return FALSE;
            i++;
        }
        if (!is_valid_hex(s[i]) || !is_valid_hex(s[i+1])) return FALSE;
        id->Data4[(i-19)/2] = guid_conv_table[s[i]] << 4 | guid_conv_table[s[i+1]];
    }

    if (!s[37]) return TRUE;
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
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

static HRESULT register_transform(CLSID *clsid, WCHAR *name,
                                  UINT32 cinput, MFT_REGISTER_TYPE_INFO *input_types,
                                  UINT32 coutput, MFT_REGISTER_TYPE_INFO *output_types)
{
    HKEY htransform, hclsid = 0;
    WCHAR buffer[64];
    GUID *types;
    DWORD size;
    LONG ret;
    UINT32 i;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, transform_keyW, &htransform))
        return E_FAIL;

    GUIDToString(buffer, clsid);
    ret = RegCreateKeyW(htransform, buffer, &hclsid);
    RegCloseKey(htransform);
    if (ret) return E_FAIL;

    size = (strlenW(name) + 1) * sizeof(WCHAR);
    if (RegSetValueExW(hclsid, NULL, 0, REG_SZ, (BYTE *)name, size))
        goto err;

    if (cinput)
    {
        size = 2 * cinput * sizeof(GUID);
        types = HeapAlloc(GetProcessHeap(), 0, size);
        if (!types) goto err;

        for (i = 0; i < cinput; i++)
        {
            memcpy(&types[2 * i],     &input_types[i].guidMajorType, sizeof(GUID));
            memcpy(&types[2 * i + 1], &input_types[i].guidSubtype,   sizeof(GUID));
        }

        ret = RegSetValueExW(hclsid, inputtypesW, 0, REG_BINARY, (BYTE *)types, size);
        HeapFree(GetProcessHeap(), 0, types);
        if (ret) goto err;
    }

    if (coutput)
    {
        size = 2 * coutput * sizeof(GUID);
        types = HeapAlloc(GetProcessHeap(), 0, size);
        if (!types) goto err;

        for (i = 0; i < coutput; i++)
        {
            memcpy(&types[2 * i],     &output_types[i].guidMajorType, sizeof(GUID));
            memcpy(&types[2 * i + 1], &output_types[i].guidSubtype,   sizeof(GUID));
        }

        ret = RegSetValueExW(hclsid, outputtypesW, 0, REG_BINARY, (BYTE *)types, size);
        HeapFree(GetProcessHeap(), 0, types);
        if (ret) goto err;
    }

    RegCloseKey(hclsid);
    return S_OK;

err:
    RegCloseKey(hclsid);
    return E_FAIL;
}

static HRESULT register_category(CLSID *clsid, GUID *category)
{
    HKEY hcategory, htmp1, htmp2;
    WCHAR buffer[64];
    DWORD ret;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, categories_keyW, &hcategory))
        return E_FAIL;

    GUIDToString(buffer, category);
    ret = RegCreateKeyW(hcategory, buffer, &htmp1);
    RegCloseKey(hcategory);
    if (ret) return E_FAIL;

    GUIDToString(buffer, clsid);
    ret = RegCreateKeyW(htmp1, buffer, &htmp2);
    RegCloseKey(htmp1);
    if (ret) return E_FAIL;

    RegCloseKey(htmp2);
    return S_OK;
}

/***********************************************************************
 *      MFTRegister (mfplat.@)
 */
HRESULT WINAPI MFTRegister(CLSID clsid, GUID category, LPWSTR name, UINT32 flags, UINT32 cinput,
                           MFT_REGISTER_TYPE_INFO *input_types, UINT32 coutput,
                           MFT_REGISTER_TYPE_INFO *output_types, void *attributes)
{
    HRESULT hr;

    FIXME("(%s, %s, %s, %x, %u, %p, %u, %p, %p)\n", debugstr_guid(&clsid), debugstr_guid(&category),
                                                    debugstr_w(name), flags, cinput, input_types,
                                                    coutput, output_types, attributes);

    if (attributes)
        FIXME("attributes not yet supported.\n");

    if (flags)
        FIXME("flags not yet supported.\n");

    hr = register_transform(&clsid, name, cinput, input_types, coutput, output_types);

    if (SUCCEEDED(hr))
        hr = register_category(&clsid, &category);

    return hr;
}

static BOOL match_type(WCHAR *clsid_str, WCHAR *type_str, MFT_REGISTER_TYPE_INFO *type)
{
    HKEY htransform, hfilter;
    DWORD reg_type, size;
    LONG ret = FALSE;
    GUID *guids = NULL;
    int i;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, transform_keyW, &htransform))
        return FALSE;

    if (RegOpenKeyW(htransform, clsid_str, &hfilter))
    {
        RegCloseKey(htransform);
        return FALSE;
    }

    if (RegQueryValueExW(hfilter, type_str, NULL, &reg_type, NULL, &size) != ERROR_SUCCESS)
        goto out;

    if (reg_type != REG_BINARY)
        goto out;

    if (!size || size % (sizeof(GUID) * 2) != 0)
        goto out;

    guids = HeapAlloc(GetProcessHeap(), 0, size);
    if (!guids)
        goto out;

    if (RegQueryValueExW(hfilter, type_str, NULL, &reg_type, (LPBYTE)guids, &size) != ERROR_SUCCESS)
        goto out;

    for (i = 0; i < size / sizeof(GUID); i += 2)
    {
        if (!memcmp(&guids[i], &type->guidMajorType, sizeof(GUID)) &&
            !memcmp(&guids[i+1], &type->guidSubtype, sizeof(GUID)))
        {
            ret = TRUE;
            break;
        }
    }

out:
    HeapFree(GetProcessHeap(), 0, guids);
    RegCloseKey(hfilter);
    RegCloseKey(htransform);
    return ret;
}

/***********************************************************************
 *      MFTEnum (mfplat.@)
 */
HRESULT WINAPI MFTEnum(GUID category, UINT32 flags, MFT_REGISTER_TYPE_INFO *input_type,
                       MFT_REGISTER_TYPE_INFO *output_type, IMFAttributes *attributes,
                       CLSID **pclsids, UINT32 *pcount)
{
    WCHAR buffer[64], clsid_str[MAX_PATH];
    HKEY hcategory, hlist;
    DWORD index = 0;
    DWORD size = MAX_PATH;
    CLSID *clsids = NULL;
    UINT32 count = 0;
    LONG ret;

    FIXME("(%s, %x, %p, %p, %p, %p, %p)\n", debugstr_guid(&category), flags, input_type,
                                            output_type, attributes, pclsids, pcount);

    if (!pclsids || !pcount)
        return E_INVALIDARG;

    if (RegOpenKeyW(HKEY_LOCAL_MACHINE, categories_keyW, &hcategory))
        return E_FAIL;

    GUIDToString(buffer, &category);

    ret = RegOpenKeyW(hcategory, buffer, &hlist);
    RegCloseKey(hcategory);
    if (ret) return E_FAIL;

    while (RegEnumKeyExW(hlist, index, clsid_str, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
        GUID clsid;
        PVOID tmp;

        if (!GUIDFromString(clsid_str, &clsid))
            goto next;

        if (output_type && !match_type(clsid_str, outputtypesW, output_type))
            goto next;

        if (input_type && !match_type(clsid_str, inputtypesW, input_type))
            goto next;

        tmp = CoTaskMemRealloc(clsids, (count + 1) * sizeof(GUID));
        if (!tmp)
        {
            CoTaskMemFree(clsids);
            RegCloseKey(hlist);
            return E_OUTOFMEMORY;
        }

        clsids = tmp;
        clsids[count++] = clsid;

    next:
        size = MAX_PATH;
        index++;
    }

    *pclsids = clsids;
    *pcount = count;

    RegCloseKey(hlist);
    return S_OK;
}

/***********************************************************************
 *      MFTUnregister (mfplat.@)
 */
HRESULT WINAPI MFTUnregister(CLSID clsid)
{
    WCHAR buffer[64], category[MAX_PATH];
    HKEY htransform, hcategory, htmp;
    DWORD size = MAX_PATH;
    DWORD index = 0;

    FIXME("(%s)\n", debugstr_guid(&clsid));

    GUIDToString(buffer, &clsid);

    if (!RegOpenKeyW(HKEY_LOCAL_MACHINE, transform_keyW, &htransform))
    {
        RegDeleteKeyW(htransform, buffer);
        RegCloseKey(htransform);
    }

    if (!RegOpenKeyW(HKEY_LOCAL_MACHINE, categories_keyW, &hcategory))
    {
        while (RegEnumKeyExW(hcategory, index, category, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            if (!RegOpenKeyW(hcategory, category, &htmp))
            {
                RegDeleteKeyW(htmp, buffer);
                RegCloseKey(htmp);
            }
            size = MAX_PATH;
            index++;
        }
        RegCloseKey(hcategory);
    }

    return S_OK;
}

/***********************************************************************
 *      MFStartup (mfplat.@)
 */
HRESULT WINAPI MFStartup(ULONG version, DWORD flags)
{
    FIXME("(%u, %u): stub\n", version, flags);
    return MF_E_BAD_STARTUP_VERSION;
}

/***********************************************************************
 *      MFShutdown (mfplat.@)
 */
HRESULT WINAPI MFShutdown(void)
{
    FIXME("(): stub\n");
    return S_OK;
}

static HRESULT WINAPI MFPluginControl_QueryInterface(IMFPluginControl *iface, REFIID riid, void **ppv)
{
    if(IsEqualGUID(riid, &IID_IUnknown)) {
        TRACE("(IID_IUnknown %p)\n", ppv);
        *ppv = iface;
    }else if(IsEqualGUID(riid, &IID_IMFPluginControl)) {
        TRACE("(IID_IMFPluginControl %p)\n", ppv);
        *ppv = iface;
    }else {
        FIXME("(%s %p)\n", debugstr_guid(riid), ppv);
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI MFPluginControl_AddRef(IMFPluginControl *iface)
{
    TRACE("\n");
    return 2;
}

static ULONG WINAPI MFPluginControl_Release(IMFPluginControl *iface)
{
    TRACE("\n");
    return 1;
}

static HRESULT WINAPI MFPluginControl_GetPreferredClsid(IMFPluginControl *iface, DWORD plugin_type,
        const WCHAR *selector, CLSID *clsid)
{
    FIXME("(%d %s %p)\n", plugin_type, debugstr_w(selector), clsid);
    return E_NOTIMPL;
}

static HRESULT WINAPI MFPluginControl_GetPreferredClsidByIndex(IMFPluginControl *iface, DWORD plugin_type,
        DWORD index, WCHAR **selector, CLSID *clsid)
{
    FIXME("(%d %d %p %p)\n", plugin_type, index, selector, clsid);
    return E_NOTIMPL;
}

static HRESULT WINAPI MFPluginControl_SetPreferredClsid(IMFPluginControl *iface, DWORD plugin_type,
        const WCHAR *selector, const CLSID *clsid)
{
    FIXME("(%d %s %s)\n", plugin_type, debugstr_w(selector), debugstr_guid(clsid));
    return E_NOTIMPL;
}

static HRESULT WINAPI MFPluginControl_IsDisabled(IMFPluginControl *iface, DWORD plugin_type, REFCLSID clsid)
{
    FIXME("(%d %s)\n", plugin_type, debugstr_guid(clsid));
    return E_NOTIMPL;
}

static HRESULT WINAPI MFPluginControl_GetDisabledByIndex(IMFPluginControl *iface, DWORD plugin_type, DWORD index, CLSID *clsid)
{
    FIXME("(%d %d %p)\n", plugin_type, index, clsid);
    return E_NOTIMPL;
}

static HRESULT WINAPI MFPluginControl_SetDisabled(IMFPluginControl *iface, DWORD plugin_type, REFCLSID clsid, BOOL disabled)
{
    FIXME("(%d %s %x)\n", plugin_type, debugstr_guid(clsid), disabled);
    return E_NOTIMPL;
}

static const IMFPluginControlVtbl MFPluginControlVtbl = {
    MFPluginControl_QueryInterface,
    MFPluginControl_AddRef,
    MFPluginControl_Release,
    MFPluginControl_GetPreferredClsid,
    MFPluginControl_GetPreferredClsidByIndex,
    MFPluginControl_SetPreferredClsid,
    MFPluginControl_IsDisabled,
    MFPluginControl_GetDisabledByIndex,
    MFPluginControl_SetDisabled
};

static IMFPluginControl plugin_control = { &MFPluginControlVtbl };

/***********************************************************************
 *      MFGetPluginControl (mfplat.@)
 */
HRESULT WINAPI MFGetPluginControl(IMFPluginControl **ret)
{
    TRACE("(%p)\n", ret);

    *ret = &plugin_control;
    return S_OK;
}
