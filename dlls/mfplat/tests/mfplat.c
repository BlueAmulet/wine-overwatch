/*
 * Unit test suite for mfplat.
 *
 * Copyright 2015 Michael Müller
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

#include "wine/test.h"

DEFINE_GUID(MFT_CATEGORY_OTHER, 0x90175d57,0xb7ea,0x4901,0xae,0xb3,0x93,0x3a,0x87,0x47,0x75,0x6f);

DEFINE_GUID(DUMMY_CLSID, 0x12345678,0x1234,0x1234,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19);
DEFINE_GUID(DUMMY_GUID1, 0x12345678,0x1234,0x1234,0x21,0x21,0x21,0x21,0x21,0x21,0x21,0x21);
DEFINE_GUID(DUMMY_GUID2, 0x12345678,0x1234,0x1234,0x22,0x22,0x22,0x22,0x22,0x22,0x22,0x22);
DEFINE_GUID(DUMMY_GUID3, 0x12345678,0x1234,0x1234,0x33,0x33,0x33,0x33,0x33,0x33,0x33,0x33);

static HRESULT (WINAPI* pMFTEnum)(GUID, UINT32, MFT_REGISTER_TYPE_INFO *, MFT_REGISTER_TYPE_INFO *,
                                  IMFAttributes *, CLSID**, UINT32*);
static HRESULT (WINAPI* pMFTRegister)(CLSID, GUID, LPWSTR, UINT32, UINT32, MFT_REGISTER_TYPE_INFO *,
                                      UINT32, MFT_REGISTER_TYPE_INFO *, void *);
static HRESULT (WINAPI* pMFTUnregister)(CLSID);

static BOOL check_clsid(CLSID *clsids, UINT32 count)
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (IsEqualGUID(&clsids[i], &DUMMY_CLSID))
            return TRUE;
    }
    return FALSE;
}

static void test_register(void)
{
    static WCHAR name[] = {'W','i','n','e',' ','t','e','s','t',0};
    MFT_REGISTER_TYPE_INFO input;
    MFT_REGISTER_TYPE_INFO output;
    CLSID *clsids;
    UINT32 count;
    HRESULT ret;

    memcpy(&input.guidMajorType, &DUMMY_GUID1, sizeof(GUID));
    memcpy(&input.guidMajorType, &DUMMY_GUID2, sizeof(GUID));
    memcpy(&output.guidSubtype, &DUMMY_GUID1, sizeof(GUID));
    memcpy(&output.guidSubtype, &DUMMY_GUID3, sizeof(GUID));

    ret = pMFTRegister(DUMMY_CLSID, MFT_CATEGORY_OTHER, name, 0, 1, &input, 1, &output, NULL);
    ok(!ret, "Failed to register dummy filter: %x\n", ret);
    if (ret) return;

    count = 0;
    clsids = NULL;
    ret = pMFTEnum(MFT_CATEGORY_OTHER, 0, NULL, NULL, NULL, &clsids, &count);
    ok(!ret, "Failed to enumerate filters: %x\n", ret);
    ok(count > 0, "Expected count > 0\n");
    ok(clsids != NULL, "Expected clsids != NULL\n");
    ok(check_clsid(clsids, count), "Filter was not part of enumeration\n");
    CoTaskMemFree(clsids);

    count = 0;
    clsids = NULL;
    ret = pMFTEnum(MFT_CATEGORY_OTHER, 0, &input, NULL, NULL, &clsids, &count);
    ok(!ret, "Failed to enumerate filters: %x\n", ret);
    ok(count > 0, "Expected count > 0\n");
    ok(clsids != NULL, "Expected clsids != NULL\n");
    ok(check_clsid(clsids, count), "Filter was not part of enumeration\n");
    CoTaskMemFree(clsids);

    count = 0;
    clsids = NULL;
    ret = pMFTEnum(MFT_CATEGORY_OTHER, 0, NULL, &output, NULL, &clsids, &count);
    ok(!ret, "Failed to enumerate filters: %x\n", ret);
    ok(count > 0, "Expected count > 0\n");
    ok(clsids != NULL, "Expected clsids != NULL\n");
    ok(check_clsid(clsids, count), "Filter was not part of enumeration\n");
    CoTaskMemFree(clsids);

    count = 0;
    clsids = NULL;
    ret = pMFTEnum(MFT_CATEGORY_OTHER, 0, &input, &output, NULL, &clsids, &count);
    ok(!ret, "Failed to enumerate filters: %x\n", ret);
    ok(count > 0, "Expected count > 0\n");
    ok(clsids != NULL, "Expected clsids != NULL\n");
    ok(check_clsid(clsids, count), "Filter was not part of enumeration\n");
    CoTaskMemFree(clsids);

    /* exchange input and output */
    count = 0;
    clsids = NULL;
    ret = pMFTEnum(MFT_CATEGORY_OTHER, 0, &output, &input, NULL, &clsids, &count);
    ok(!ret, "Failed to enumerate filters: %x\n", ret);
    ok(!count, "Expected count == 0\n");
    ok(clsids == NULL, "Expected clsids == NULL\n");

    pMFTUnregister(DUMMY_CLSID);
}

BOOL init_function_ptrs(void)
{
    HMODULE mfplat = LoadLibraryA("mfplat.dll");
    if (!mfplat)
    {
        win_skip("Could not load mfplat.dll\n");
        return FALSE;
    }

    #define LOAD_FUNCPTR(f) p##f = (void*)GetProcAddress(mfplat, #f)
    LOAD_FUNCPTR(MFTEnum);
    LOAD_FUNCPTR(MFTRegister);
    LOAD_FUNCPTR(MFTUnregister);
    #undef LOAD_FUNCPTR

    return TRUE;
}

START_TEST(mfplat)
{
    if (!init_function_ptrs())
        return;

    CoInitialize(NULL);
    test_register();
}
