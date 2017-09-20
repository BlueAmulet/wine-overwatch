/*
 * Copyright 2002 Mike McCormack for CodeWeavers
 * Copyright 2005 Juan Lang
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
#include <stdio.h>

#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "winreg.h"
#include "winuser.h"
#include "i_cryptasn1tls.h"
#include "crypt32_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(crypt);

static HCRYPTPROV hDefProv;
HINSTANCE hInstance;

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD fdwReason, PVOID pvReserved)
{
    switch (fdwReason)
    {
        case DLL_PROCESS_ATTACH:
            hInstance = hInst;
            DisableThreadLibraryCalls(hInst);
            init_empty_store();
            crypt_oid_init();
            break;
        case DLL_PROCESS_DETACH:
            if (pvReserved) break;
            crypt_oid_free();
            crypt_sip_free();
            default_chain_engine_free();
            if (hDefProv) CryptReleaseContext(hDefProv, 0);
            break;
    }
    return TRUE;
}

HCRYPTPROV CRYPT_GetDefaultProvider(void)
{
    if (!hDefProv)
    {
        HCRYPTPROV prov;

        if (!CryptAcquireContextW(&prov, NULL, MS_ENH_RSA_AES_PROV_W,
         PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
            return hDefProv;
        InterlockedCompareExchangePointer((PVOID *)&hDefProv, (PVOID)prov,
         NULL);
        if (hDefProv != prov)
            CryptReleaseContext(prov, 0);
    }
    return hDefProv;
}

typedef void * HLRUCACHE;

/* this function is called by Internet Explorer when it is about to verify a
 * downloaded component.  The first parameter appears to be a pointer to an
 * unknown type, native fails unless it points to a buffer of at least 20 bytes.
 * The second parameter appears to be an out parameter, whatever it's set to is
 * passed (by cryptnet.dll) to I_CryptFlushLruCache.
 */
BOOL WINAPI I_CryptCreateLruCache(void *unknown, HLRUCACHE *out)
{
    FIXME("(%p, %p): stub!\n", unknown, out);
    *out = (void *)0xbaadf00d;
    return TRUE;
}

BOOL WINAPI I_CryptFindLruEntry(DWORD unk0, DWORD unk1)
{
    FIXME("(%08x, %08x): stub!\n", unk0, unk1);
    return FALSE;
}

BOOL WINAPI I_CryptFindLruEntryData(DWORD unk0, DWORD unk1, DWORD unk2)
{
    FIXME("(%08x, %08x, %08x): stub!\n", unk0, unk1, unk2);
    return FALSE;
}

BOOL WINAPI I_CryptCreateLruEntry(HLRUCACHE h, DWORD unk0, DWORD unk1)
{
    FIXME("(%p, %08x, %08x): stub!\n", h, unk0, unk1);
    return FALSE;
}

DWORD WINAPI I_CryptFlushLruCache(HLRUCACHE h, DWORD unk0, DWORD unk1)
{
    FIXME("(%p, %08x, %08x): stub!\n", h, unk0, unk1);
    return 0;
}

HLRUCACHE WINAPI I_CryptFreeLruCache(HLRUCACHE h, DWORD unk0, DWORD unk1)
{
    FIXME("(%p, %08x, %08x): stub!\n", h, unk0, unk1);
    return h;
}

LPVOID WINAPI CryptMemAlloc(ULONG cbSize)
{
    return HeapAlloc(GetProcessHeap(), 0, cbSize);
}

LPVOID WINAPI CryptMemRealloc(LPVOID pv, ULONG cbSize)
{
    return HeapReAlloc(GetProcessHeap(), 0, pv, cbSize);
}

VOID WINAPI CryptMemFree(LPVOID pv)
{
    HeapFree(GetProcessHeap(), 0, pv);
}

DWORD WINAPI I_CryptAllocTls(void)
{
    return TlsAlloc();
}

LPVOID WINAPI I_CryptDetachTls(DWORD dwTlsIndex)
{
    LPVOID ret;

    ret = TlsGetValue(dwTlsIndex);
    TlsSetValue(dwTlsIndex, NULL);
    return ret;
}

LPVOID WINAPI I_CryptGetTls(DWORD dwTlsIndex)
{
    return TlsGetValue(dwTlsIndex);
}

BOOL WINAPI I_CryptSetTls(DWORD dwTlsIndex, LPVOID lpTlsValue)
{
    return TlsSetValue(dwTlsIndex, lpTlsValue);
}

BOOL WINAPI I_CryptFreeTls(DWORD dwTlsIndex, DWORD unknown)
{
    BOOL ret;

    TRACE("(%d, %d)\n", dwTlsIndex, unknown);

    ret = TlsFree(dwTlsIndex);
    if (!ret) SetLastError( E_INVALIDARG );
    return ret;
}

BOOL WINAPI I_CryptGetOssGlobal(DWORD x)
{
    FIXME("%08x\n", x);
    return FALSE;
}

HCRYPTPROV WINAPI I_CryptGetDefaultCryptProv(DWORD reserved)
{
    HCRYPTPROV ret;

    TRACE("(%08x)\n", reserved);

    if (reserved)
    {
        SetLastError(E_INVALIDARG);
        return 0;
    }
    ret = CRYPT_GetDefaultProvider();
    CryptContextAddRef(ret, NULL, 0);
    return ret;
}

BOOL WINAPI I_CryptReadTrustedPublisherDWORDValueFromRegistry(LPCWSTR name,
 DWORD *value)
{
    static const WCHAR safer[] = { 
     'S','o','f','t','w','a','r','e','\\','P','o','l','i','c','i','e','s','\\',
     'M','i','c','r','o','s','o','f','t','\\','S','y','s','t','e','m',
     'C','e','r','t','i','f','i','c','a','t','e','s','\\',
     'T','r','u','s','t','e','d','P','u','b','l','i','s','h','e','r','\\',
     'S','a','f','e','r',0 };
    HKEY key;
    LONG rc;
    BOOL ret = FALSE;

    TRACE("(%s, %p)\n", debugstr_w(name), value);

    *value = 0;
    rc = RegCreateKeyW(HKEY_LOCAL_MACHINE, safer, &key);
    if (rc == ERROR_SUCCESS)
    {
        DWORD size = sizeof(DWORD);

        if (!RegQueryValueExW(key, name, NULL, NULL, (LPBYTE)value, &size))
            ret = TRUE;
        RegCloseKey(key);
    }
    return ret;
}

DWORD WINAPI I_CryptInstallOssGlobal(DWORD x, DWORD y, DWORD z)
{
    static int ret = 8;
    ret++;
    FIXME("%08x %08x %08x, return value %d\n", x, y, z,ret);
    return ret;
}

BOOL WINAPI I_CryptInstallAsn1Module(ASN1module_t x, DWORD y, void* z)
{
    FIXME("(%p %08x %p): stub\n", x, y, z);
    return TRUE;
}

BOOL WINAPI I_CryptUninstallAsn1Module(HCRYPTASN1MODULE x)
{
    FIXME("(%08x): stub\n", x);
    return TRUE;
}

ASN1decoding_t WINAPI I_CryptGetAsn1Decoder(HCRYPTASN1MODULE x)
{
    FIXME("(%08x): stub\n", x);
    return NULL;
}

ASN1encoding_t WINAPI I_CryptGetAsn1Encoder(HCRYPTASN1MODULE x)
{
    FIXME("(%08x): stub\n", x);
    return NULL;
}

BOOL WINAPI CryptProtectMemory(void *data, DWORD len, DWORD flags)
{
    FIXME("(%p %u %08x): stub\n", data, len, flags);
    return TRUE;
}

BOOL WINAPI CryptUnprotectMemory(void *data, DWORD len, DWORD flags)
{
    static int fixme_once;
    if (!fixme_once++) FIXME("(%p %u %08x): stub\n", data, len, flags);
    return TRUE;
}
