/*
 * Path tests for kernelbase.dll
 *
 * Copyright 2017 Michael Müller
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
#include <windef.h>
#include <winbase.h>
#include <stdlib.h>
#include <winerror.h>
#include <winnls.h>
#include <pathcch.h>
#include <strsafe.h>

#include "wine/test.h"

static const struct
{
    const char *path1;
    const char *path2;
    const char *result;
    BOOL todo;
}
combine_test[] =
{
    /* nomal paths */
    {"C:\\",  "a",     "C:\\a",   FALSE},
    {"C:\\b", "..\\a", "C:\\a",   FALSE},
    {"C:",    "a",     "C:\\a",   FALSE},
    {"C:\\",  ".",     "C:\\",    TRUE},
    {"C:\\",  "..",    "C:\\",    FALSE},
    {"\\a",   "b",      "\\a\\b", FALSE},

    /* normal UNC paths */
    {"\\\\192.168.1.1\\test", "a",  "\\\\192.168.1.1\\test\\a", FALSE},
    {"\\\\192.168.1.1\\test", "..", "\\\\192.168.1.1", FALSE},

    /* NT paths */
    {"\\\\?\\C:\\", "a",  "C:\\a", TRUE},
    {"\\\\?\\C:\\", "..", "C:\\",  TRUE},

    /* NT UNC path */
    {"\\\\?\\UNC\\192.168.1.1\\test", "a",  "\\\\192.168.1.1\\test\\a", TRUE},
    {"\\\\?\\UNC\\192.168.1.1\\test", "..", "\\\\192.168.1.1", TRUE},
};

static void test_PathCchCombineEx(void)
{
    WCHAR expected[MAX_PATH] = {'C',':','\\','a',0};
    WCHAR p1[MAX_PATH] = {'C',':','\\',0};
    WCHAR p2[MAX_PATH] = {'a',0};
    WCHAR output[MAX_PATH];
    HRESULT hr;
    int i;

    hr = PathCchCombineEx(NULL, 2, p1, p2, 0);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %08x\n", hr);

    memset(output, 0xff, sizeof(output));
    hr = PathCchCombineEx(output, 0, p1, p2, 0);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %08x\n", hr);
    ok(output[0] == 0xffff, "Expected output buffer to be unchanged\n");

    memset(output, 0xff, sizeof(output));
    hr = PathCchCombineEx(output, 1, p1, p2, 0);
    ok(hr == STRSAFE_E_INSUFFICIENT_BUFFER, "Expected STRSAFE_E_INSUFFICIENT_BUFFER, got %08x\n", hr);
    ok(output[0] == 0, "Expected output buffer to contain NULL string\n");

    memset(output, 0xff, sizeof(output));
    hr = PathCchCombineEx(output, 4, p1, p2, 0);
    ok(hr == STRSAFE_E_INSUFFICIENT_BUFFER, "Expected STRSAFE_E_INSUFFICIENT_BUFFER, got %08x\n", hr);
    ok(output[0] == 0x0, "Expected output buffer to contain NULL string\n");

    memset(output, 0xff, sizeof(output));
    hr = PathCchCombineEx(output, 5, p1, p2, 0);
    ok(hr == S_OK, "Expected S_OK, got %08x\n", hr);
    ok(!lstrcmpW(output, expected),
        "Combination of %s + %s returned %s, expected %s\n",
        wine_dbgstr_w(p1), wine_dbgstr_w(p2), wine_dbgstr_w(output), wine_dbgstr_w(expected));

    for (i = 0; i < sizeof(combine_test)/sizeof(combine_test[0]); i++)
    {
        MultiByteToWideChar(CP_ACP, 0, combine_test[i].path1, -1, p1, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, combine_test[i].path2, -1, p2, MAX_PATH);
        MultiByteToWideChar(CP_ACP, 0, combine_test[i].result, -1, expected, MAX_PATH);

        hr = PathCchCombineEx(output, MAX_PATH, p1, p2, 0);
        ok(hr == S_OK, "Expected S_OK, got %08x\n", hr);
        todo_wine_if(combine_test[i].todo) ok(!lstrcmpW(output, expected),
            "Combination of %s + %s returned %s, expected %s\n",
            wine_dbgstr_w(p1), wine_dbgstr_w(p2), wine_dbgstr_w(output), wine_dbgstr_w(expected));
    }
}

START_TEST(path)
{
    test_PathCchCombineEx();
}
