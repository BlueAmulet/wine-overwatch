/*
 * Copyright (C) 2014-2015 Michael Müller
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

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "cuda.h"

#include "wine/test.h"

static CUresult (WINAPI *pcuInit)(unsigned int);
static CUresult (WINAPI *pcuGetExportTable)(const void**, const CUuuid*);

static const CUuuid UUID_TlsNotifyInterface = {{0x19, 0x5B, 0xCB, 0xF4, 0xD6, 0x7D, 0x02, 0x4A,
                                                0xAC, 0xC5, 0x1D, 0x29, 0xCE, 0xA6, 0x31, 0xAE}};

struct TlsNotifyInterface_table
{
    int size;
    CUresult (WINAPI *Set)(void **handle, void *callback, void *data);
    CUresult (WINAPI *Remove)(void *handle, void *param1);
};

static BOOL init(void)
{
    HMODULE nvcuda = LoadLibraryA("nvcuda.dll");
    if (!nvcuda)
    {
        skip("Could not load nvcuda.dll\n");
        return FALSE;
    }

    pcuInit           = (void *)GetProcAddress(nvcuda, "cuInit");
    pcuGetExportTable = (void *)GetProcAddress(nvcuda, "cuGetExportTable");

    if (!pcuInit)
    {
        win_skip("Failed to get entry point for cuInit.\n");
        return FALSE;
    }

    if (pcuInit(0))
    {
        skip("Failed to initialize cuda.\n");
        return FALSE;
    }

    return TRUE;
}

struct tls_test_data
{
    int count;
    DWORD reason;
    DWORD threadid;
};

static void CDECL tls_callback_test(DWORD reason, void *data)
{
    struct tls_test_data *test_data = data;
    trace("reason: %d, data: %p\n", reason, data);

    test_data->count++;
    test_data->reason = reason;
    test_data->threadid = GetCurrentThreadId();
}

static DWORD WINAPI test_thread(LPVOID data)
{
    struct tls_test_data *test_data = data;
    ok(test_data->count == 0, "Expected 0 callback executions, got %d\n", test_data->count);

    /* do nothing */
    return 0;
}

static void test_TlsNotifyInterface(void)
{
    const struct TlsNotifyInterface_table *iface;
    struct tls_test_data test_data;
    DWORD threadid, thread_res;
    void *handle, *handle2;
    HANDLE thread;
    CUresult res;

    if (!pcuGetExportTable)
    {
        win_skip("cuGetExportTable export not found.\n");
        return;
    }

    if (pcuGetExportTable((const void **)&iface, &UUID_TlsNotifyInterface))
    {
        win_skip("TlsNotifyInterface not supported.\n");
        return;
    }

    ok(iface->size == sizeof(struct TlsNotifyInterface_table),
       "Size mismatch for TlsNotifyInterface, expected %u, got %u\n",
       (unsigned int)sizeof(struct TlsNotifyInterface_table), iface->size);

    /* Test adding and removing a TLS callback */
    memset(&test_data, 0x00, sizeof(test_data));
    res = iface->Set(&handle, &tls_callback_test, &test_data);
    ok(!res, "Failed to set TLS callback, got error %d\n", res);
    res = iface->Remove(handle, NULL);
    ok(!res, "Failed to remove TLS callback, got error %d\n", res);
    ok(test_data.count == 0, "Expected 0 callback execution, got %d\n", test_data.count);

    /* Test TLS callback with a thread */
    memset(&test_data, 0x00, sizeof(test_data));
    res = iface->Set(&handle, &tls_callback_test, &test_data);
    ok(!res, "Failed to set TLS callback, got error %d\n", res);
    thread = CreateThread(NULL, 0, test_thread, &test_data, 0, &threadid);
    ok(thread != NULL, "Failed to create Thread, error: %d\n", GetLastError());
    thread_res = WaitForSingleObject(thread, 2000);
    ok(thread_res == WAIT_OBJECT_0, "Waiting for thread failed: %d\n", thread_res);
    ok(test_data.count == 1, "Expected 1 callback execution, got %d\n", test_data.count);
    ok(test_data.reason == 0, "Expected reason 0, got %d\n", test_data.reason);
    ok(test_data.threadid == threadid, "Expected thread id %d, got %d\n", threadid, test_data.threadid);
    res = iface->Remove(handle, NULL);
    ok(!res, "Failed to remove TLS callback, got error %d\n", res);

    /* Test adding the same callback twice */
    res = iface->Set(&handle, &tls_callback_test, &test_data);
    ok(!res, "Failed to set first TLS callback, got error %d\n", res);
    res = iface->Set(&handle2, &tls_callback_test, &test_data);
    ok(!res, "Failed to set second TLS callback, got error %d\n", res);
    ok(handle != handle2, "expected handle and handle2 to be different\n");
    res = iface->Remove(handle, NULL);
    ok(!res, "Failed to remove first TLS callback, got error %d\n", res);
    res = iface->Remove(handle2, NULL);
    ok(!res, "Failed to remove second TLS callback, got error %d\n", res);

    /* removing an invalid callback handle causes a crash on Windows */
    if (0)
    {
        res = iface->Remove(handle, NULL);
        ok(0, "Failed to remove TLS callback, got error %d\n", res);
        res = iface->Remove(NULL, NULL);
        ok(0, "Failed to remove TLS callback, got error %d\n", res);
    }
}

START_TEST( nvcuda )
{
    if (!init())
        return;

    test_TlsNotifyInterface();
}
