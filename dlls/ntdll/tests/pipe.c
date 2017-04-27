/* Unit test suite for Ntdll NamedPipe API functions
 *
 * Copyright 2011 Bernhard Loos
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

#include <stdio.h>
#include <stdarg.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "winnls.h"
#include "wine/test.h"
#include "winternl.h"
#include "winioctl.h"

#ifndef __WINE_WINTERNL_H

typedef struct {
  ULONG ReadMode;
  ULONG CompletionMode;
} FILE_PIPE_INFORMATION;

typedef struct {
  ULONG NamedPipeType;
  ULONG NamedPipeConfiguration;
  ULONG MaximumInstances;
  ULONG CurrentInstances;
  ULONG InboundQuota;
  ULONG ReadDataAvailable;
  ULONG OutboundQuota;
  ULONG WriteQuotaAvailable;
  ULONG NamedPipeState;
  ULONG NamedPipeEnd;
} FILE_PIPE_LOCAL_INFORMATION;

#ifndef FILE_SYNCHRONOUS_IO_ALERT
#define FILE_SYNCHRONOUS_IO_ALERT 0x10
#endif

#ifndef FILE_SYNCHRONOUS_IO_NONALERT
#define FILE_SYNCHRONOUS_IO_NONALERT 0x20
#endif

#ifndef FSCTL_PIPE_LISTEN
#define FSCTL_PIPE_LISTEN CTL_CODE(FILE_DEVICE_NAMED_PIPE, 2, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#endif

static NTSTATUS (WINAPI *pNtFsControlFile) (HANDLE handle, HANDLE event, PIO_APC_ROUTINE apc, PVOID apc_context, PIO_STATUS_BLOCK io, ULONG code, PVOID in_buffer, ULONG in_size, PVOID out_buffer, ULONG out_size);
static NTSTATUS (WINAPI *pNtCreateNamedPipeFile) (PHANDLE handle, ULONG access,
                                        POBJECT_ATTRIBUTES attr, PIO_STATUS_BLOCK iosb,
                                        ULONG sharing, ULONG dispo, ULONG options,
                                        ULONG pipe_type, ULONG read_mode,
                                        ULONG completion_mode, ULONG max_inst,
                                        ULONG inbound_quota, ULONG outbound_quota,
                                        PLARGE_INTEGER timeout);
static NTSTATUS (WINAPI *pNtQueryInformationFile) (IN HANDLE FileHandle, OUT PIO_STATUS_BLOCK IoStatusBlock, OUT PVOID FileInformation, IN ULONG Length, IN FILE_INFORMATION_CLASS FileInformationClass);
static NTSTATUS (WINAPI *pNtSetInformationFile) (HANDLE handle, PIO_STATUS_BLOCK io, PVOID ptr, ULONG len, FILE_INFORMATION_CLASS class);
static NTSTATUS (WINAPI *pNtCancelIoFile) (HANDLE hFile, PIO_STATUS_BLOCK io_status);
static NTSTATUS (WINAPI *pNtCancelIoFileEx) (HANDLE hFile, IO_STATUS_BLOCK *iosb, IO_STATUS_BLOCK *io_status);
static void (WINAPI *pRtlInitUnicodeString) (PUNICODE_STRING target, PCWSTR source);

static HANDLE (WINAPI *pOpenThread)(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwThreadId);
static DWORD (WINAPI *pQueueUserAPC)(PAPCFUNC pfnAPC, HANDLE hThread, ULONG_PTR dwData);


static BOOL init_func_ptrs(void)
{
    HMODULE module = GetModuleHandleA("ntdll.dll");

#define loadfunc(name)  if (!(p##name = (void *)GetProcAddress(module, #name))) { \
                            trace("GetProcAddress(%s) failed\n", #name); \
                            return FALSE; \
                        }

    loadfunc(NtFsControlFile)
    loadfunc(NtCreateNamedPipeFile)
    loadfunc(NtQueryInformationFile)
    loadfunc(NtSetInformationFile)
    loadfunc(NtCancelIoFile)
    loadfunc(NtCancelIoFileEx)
    loadfunc(RtlInitUnicodeString)

    /* not fatal */
    module = GetModuleHandleA("kernel32.dll");
    pOpenThread = (void *)GetProcAddress(module, "OpenThread");
    pQueueUserAPC = (void *)GetProcAddress(module, "QueueUserAPC");
    return TRUE;
}

static inline BOOL is_signaled( HANDLE obj )
{
    return WaitForSingleObject( obj, 0 ) == WAIT_OBJECT_0;
}

static const WCHAR testpipe[] = { '\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', '\\',
                                  't', 'e', 's', 't', 'p', 'i', 'p', 'e', 0 };
static const WCHAR testpipe_nt[] = { '\\', '?', '?', '\\', 'p', 'i', 'p', 'e', '\\',
                                     't', 'e', 's', 't', 'p', 'i', 'p', 'e', 0 };

static NTSTATUS create_pipe(PHANDLE handle, ULONG sharing, ULONG options)
{
    IO_STATUS_BLOCK iosb;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING name;
    LARGE_INTEGER timeout;
    NTSTATUS res;

    pRtlInitUnicodeString(&name, testpipe_nt);

    attr.Length                   = sizeof(attr);
    attr.RootDirectory            = 0;
    attr.ObjectName               = &name;
    attr.Attributes               = 0x40; /*case insensitive */
    attr.SecurityDescriptor       = NULL;
    attr.SecurityQualityOfService = NULL;

    timeout.QuadPart = -100000000;

    res = pNtCreateNamedPipeFile(handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb, sharing,  2 /*FILE_CREATE*/,
                                 options, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    return res;
}

static BOOL ioapc_called;
static void CALLBACK ioapc(void *arg, PIO_STATUS_BLOCK io, ULONG reserved)
{
    ioapc_called = TRUE;
}

static NTSTATUS listen_pipe(HANDLE hPipe, HANDLE hEvent, PIO_STATUS_BLOCK iosb, BOOL use_apc)
{
    int dummy;

    ioapc_called = FALSE;

    return pNtFsControlFile(hPipe, hEvent, use_apc ? &ioapc: NULL, use_apc ? &dummy: NULL, iosb, FSCTL_PIPE_LISTEN, 0, 0, 0, 0);
}

static void test_create_invalid(void)
{
    IO_STATUS_BLOCK iosb;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING name;
    LARGE_INTEGER timeout;
    NTSTATUS res;
    HANDLE handle, handle2;
    FILE_PIPE_LOCAL_INFORMATION info;

    pRtlInitUnicodeString(&name, testpipe_nt);

    attr.Length                   = sizeof(attr);
    attr.RootDirectory            = 0;
    attr.ObjectName               = &name;
    attr.Attributes               = 0x40; /*case insensitive */
    attr.SecurityDescriptor       = NULL;
    attr.SecurityQualityOfService = NULL;

    timeout.QuadPart = -100000000;

/* create a pipe with FILE_OVERWRITE */
    res = pNtCreateNamedPipeFile(&handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb, FILE_SHARE_READ, 4 /*FILE_OVERWRITE*/,
                                 0, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    todo_wine ok(res == STATUS_INVALID_PARAMETER, "NtCreateNamedPipeFile returned %x\n", res);
    if (!res)
        CloseHandle(handle);

/* create a pipe with FILE_OVERWRITE_IF */
    res = pNtCreateNamedPipeFile(&handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb, FILE_SHARE_READ, 5 /*FILE_OVERWRITE_IF*/,
                                 0, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    todo_wine ok(res == STATUS_INVALID_PARAMETER, "NtCreateNamedPipeFile returned %x\n", res);
    if (!res)
        CloseHandle(handle);

/* create a pipe with sharing = 0 */
    res = pNtCreateNamedPipeFile(&handle, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb, 0, 2 /*FILE_CREATE*/,
                                 0, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    ok(res == STATUS_INVALID_PARAMETER, "NtCreateNamedPipeFile returned %x\n", res);
    if (!res)
        CloseHandle(handle);

/* create a pipe without r/w access */
    res = pNtCreateNamedPipeFile(&handle, SYNCHRONIZE, &attr, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 2 /*FILE_CREATE*/,
                                 0, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    res = pNtQueryInformationFile(handle, &iosb, &info, sizeof(info), (FILE_INFORMATION_CLASS)24);
    ok(res == STATUS_ACCESS_DENIED, "NtQueryInformationFile returned %x\n", res);

/* test FILE_CREATE creation disposition */
    res = pNtCreateNamedPipeFile(&handle2, SYNCHRONIZE, &attr, &iosb, FILE_SHARE_READ | FILE_SHARE_WRITE, 2 /*FILE_CREATE*/,
                                 0, 1, 0, 0, 0xFFFFFFFF, 500, 500, &timeout);
    todo_wine ok(res == STATUS_ACCESS_DENIED, "NtCreateNamedPipeFile returned %x\n", res);
    if (!res)
        CloseHandle(handle2);

    CloseHandle(handle);
}

static void test_create(void)
{
    HANDLE hserver;
    NTSTATUS res;
    int j, k;
    FILE_PIPE_LOCAL_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    HANDLE hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    static const DWORD access[] = { 0, GENERIC_READ, GENERIC_WRITE, GENERIC_READ | GENERIC_WRITE};
    static const DWORD sharing[] =    { FILE_SHARE_READ, FILE_SHARE_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE };
    static const DWORD pipe_config[]= {               1,                0,                                  2 };

    for (j = 0; j < sizeof(sharing) / sizeof(DWORD); j++) {
        for (k = 0; k < sizeof(access) / sizeof(DWORD); k++) {
            HANDLE hclient;
            BOOL should_succeed = TRUE;

            res  = create_pipe(&hserver, sharing[j], 0);
            if (res) {
                ok(0, "NtCreateNamedPipeFile returned %x, sharing: %x\n", res, sharing[j]);
                continue;
            }

            res = listen_pipe(hserver, hEvent, &iosb, FALSE);
            ok(res == STATUS_PENDING, "NtFsControlFile returned %x\n", res);

            res = pNtQueryInformationFile(hserver, &iosb, &info, sizeof(info), (FILE_INFORMATION_CLASS)24);
            ok(!res, "NtQueryInformationFile for server returned %x, sharing: %x\n", res, sharing[j]);
            ok(info.NamedPipeConfiguration == pipe_config[j], "wrong duplex status for pipe: %d, expected %d\n",
               info.NamedPipeConfiguration, pipe_config[j]);

            hclient = CreateFileW(testpipe, access[k], 0, 0, OPEN_EXISTING, 0, 0);
            if (hclient != INVALID_HANDLE_VALUE) {
                res = pNtQueryInformationFile(hclient, &iosb, &info, sizeof(info), (FILE_INFORMATION_CLASS)24);
                ok(!res, "NtQueryInformationFile for client returned %x, access: %x, sharing: %x\n",
                   res, access[k], sharing[j]);
                ok(info.NamedPipeConfiguration == pipe_config[j], "wrong duplex status for pipe: %d, expected %d\n",
                   info.NamedPipeConfiguration, pipe_config[j]);
                CloseHandle(hclient);
            }

            if (access[k] & GENERIC_WRITE)
                should_succeed &= !!(sharing[j] & FILE_SHARE_WRITE);
            if (access[k] & GENERIC_READ)
                should_succeed &= !!(sharing[j] & FILE_SHARE_READ);

            if (should_succeed)
                ok(hclient != INVALID_HANDLE_VALUE, "CreateFile failed for sharing %x, access: %x, GetLastError: %d\n",
                   sharing[j], access[k], GetLastError());
            else
                ok(hclient == INVALID_HANDLE_VALUE, "CreateFile succeeded for sharing %x, access: %x\n", sharing[j], access[k]);

            CloseHandle(hserver);
        }
    }
    CloseHandle(hEvent);
}

static void test_overlapped(void)
{
    IO_STATUS_BLOCK iosb;
    HANDLE hEvent;
    HANDLE hPipe;
    HANDLE hClient;
    NTSTATUS res;

    hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(hEvent != INVALID_HANDLE_VALUE, "can't create event, GetLastError: %x\n", GetLastError());

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, 0 /* OVERLAPPED */);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    memset(&iosb, 0x55, sizeof(iosb));
    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    ok(res == STATUS_PENDING, "NtFsControlFile returned %x\n", res);
    ok(U(iosb).Status == 0x55555555, "iosb.Status got changed to %x\n", U(iosb).Status);

    hClient = CreateFileW(testpipe, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    ok(hClient != INVALID_HANDLE_VALUE, "can't open pipe, GetLastError: %x\n", GetLastError());

    ok(U(iosb).Status == 0, "Wrong iostatus %x\n", U(iosb).Status);
    ok(WaitForSingleObject(hEvent, 0) == 0, "hEvent not signaled\n");

    ok(!ioapc_called, "IOAPC ran too early\n");

    SleepEx(0, TRUE); /* alertable wait state */

    ok(ioapc_called, "IOAPC didn't run\n");

    CloseHandle(hPipe);
    CloseHandle(hClient);

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, 0 /* OVERLAPPED */);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    hClient = CreateFileW(testpipe, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    ok(hClient != INVALID_HANDLE_VALUE || broken(GetLastError() == ERROR_PIPE_BUSY) /* > Win 8 */,
       "can't open pipe, GetLastError: %x\n", GetLastError());

    if (hClient != INVALID_HANDLE_VALUE)
    {
        SetEvent(hEvent);
        memset(&iosb, 0x55, sizeof(iosb));
        res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
        ok(res == STATUS_PIPE_CONNECTED, "NtFsControlFile returned %x\n", res);
        ok(U(iosb).Status == 0x55555555, "iosb.Status got changed to %x\n", U(iosb).Status);
        ok(!is_signaled(hEvent), "hEvent not signaled\n");

        CloseHandle(hClient);
    }

    CloseHandle(hPipe);
    CloseHandle(hEvent);
}

static void test_completion(void)
{
    static const char buf[] = "testdata";
    FILE_IO_COMPLETION_NOTIFICATION_INFORMATION info;
    HANDLE port, pipe, client;
    IO_STATUS_BLOCK iosb;
    OVERLAPPED ov, *pov;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    DWORD num_bytes;
    ULONG_PTR key;
    DWORD dwret;
    BOOL ret;

    memset(&ov, 0, sizeof(ov));
    ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    ok(ov.hEvent != INVALID_HANDLE_VALUE, "CreateEvent failed, error %u\n", GetLastError());

    status = create_pipe(&pipe, FILE_SHARE_READ | FILE_SHARE_WRITE, 0 /* OVERLAPPED */);
    ok(!status, "NtCreateNamedPipeFile returned %x\n", status);
    status = listen_pipe(pipe, ov.hEvent, &iosb, FALSE);
    ok(status == STATUS_PENDING, "NtFsControlFile returned %x\n", status);

    client = CreateFileW(testpipe, GENERIC_READ | GENERIC_WRITE, 0, 0,
                         OPEN_EXISTING, FILE_FLAG_OVERLAPPED, 0);
    ok(client != INVALID_HANDLE_VALUE, "CreateFile failed, error %u\n", GetLastError());
    dwret = WaitForSingleObject(ov.hEvent, 0);
    ok(dwret == WAIT_OBJECT_0, "expected WAIT_OBJECT_0, got %u\n", dwret);

    port = CreateIoCompletionPort(client, NULL, 0xdeadbeef, 0);
    ok(port != NULL, "CreateIoCompletionPort failed, error %u\n", GetLastError());

    ret = WriteFile(client, buf, sizeof(buf), &num_bytes, &ov);
    ok(ret, "WriteFile failed, error %u\n", GetLastError());
    ok(num_bytes == sizeof(buf), "expected sizeof(buf), got %u\n", num_bytes);

    key = 0;
    pov = NULL;
    ret = GetQueuedCompletionStatus(port, &num_bytes, &key, &pov, 1000);
    ok(ret, "GetQueuedCompletionStatus failed, error %u\n", GetLastError());
    ok(key == 0xdeadbeef, "expected 0xdeadbeef, got %lx\n", key);
    ok(pov == &ov, "expected %p, got %p\n", &ov, pov);

    info.Flags = FILE_SKIP_COMPLETION_PORT_ON_SUCCESS;
    status = pNtSetInformationFile(client, &io, &info, sizeof(info), FileIoCompletionNotificationInformation);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS, got %08x\n", status);

    info.Flags = 0;
    status = pNtQueryInformationFile(client, &io, &info, sizeof(info), FileIoCompletionNotificationInformation);
    ok(status == STATUS_SUCCESS, "expected STATUS_SUCCESS, got %08x\n", status);
    ok((info.Flags & FILE_SKIP_COMPLETION_PORT_ON_SUCCESS) != 0, "got %08x\n", info.Flags);

    ret = WriteFile(client, buf, sizeof(buf), &num_bytes, &ov);
    ok(ret, "WriteFile failed, error %u\n", GetLastError());
    ok(num_bytes == sizeof(buf), "expected sizeof(buf), got %u\n", num_bytes);

    pov = (void *)0xdeadbeef;
    ret = GetQueuedCompletionStatus(port, &num_bytes, &key, &pov, 1000);
    ok(!ret, "GetQueuedCompletionStatus succeeded\n");
    ok(pov == NULL, "expected NULL, got %p\n", pov);

    CloseHandle(ov.hEvent);
    CloseHandle(client);
    CloseHandle(pipe);
    CloseHandle(port);
}

static BOOL userapc_called;
static void CALLBACK userapc(ULONG_PTR dwParam)
{
    userapc_called = TRUE;
}

static BOOL open_succeeded;
static DWORD WINAPI thread(PVOID main_thread)
{
    HANDLE h;

    Sleep(400);

    if (main_thread) {
        DWORD ret;
        userapc_called = FALSE;
        ret = pQueueUserAPC(&userapc, main_thread, 0);
        ok(ret, "can't queue user apc, GetLastError: %x\n", GetLastError());
        CloseHandle(main_thread);
    }

    Sleep(400);

    h = CreateFileW(testpipe, GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);

    if (h != INVALID_HANDLE_VALUE) {
        open_succeeded = TRUE;
        Sleep(100);
        CloseHandle(h);
    } else
        open_succeeded = FALSE;

    return 0;
}

static void test_alertable(void)
{
    IO_STATUS_BLOCK iosb;
    HANDLE hEvent;
    HANDLE hPipe;
    NTSTATUS res;
    HANDLE hThread;
    DWORD ret;

    memset(&iosb, 0x55, sizeof(iosb));

    hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(hEvent != INVALID_HANDLE_VALUE, "can't create event, GetLastError: %x\n", GetLastError());

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_ALERT);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

/* queue an user apc before calling listen */
    userapc_called = FALSE;
    ret = pQueueUserAPC(&userapc, GetCurrentThread(), 0);
    ok(ret, "can't queue user apc, GetLastError: %x\n", GetLastError());

    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    todo_wine ok(res == STATUS_CANCELLED, "NtFsControlFile returned %x\n", res);

    todo_wine ok(userapc_called, "user apc didn't run\n");
    ok(U(iosb).Status == 0x55555555, "iosb.Status got changed to %x\n", U(iosb).Status);
    todo_wine ok(WaitForSingleObjectEx(hEvent, 0, TRUE) == WAIT_TIMEOUT, "hEvent signaled\n");
    ok(!ioapc_called, "IOAPC ran\n");

/* queue an user apc from a different thread */
    hThread = CreateThread(NULL, 0, &thread, pOpenThread(MAXIMUM_ALLOWED, FALSE, GetCurrentThreadId()), 0, 0);
    ok(hThread != INVALID_HANDLE_VALUE, "can't create thread, GetLastError: %x\n", GetLastError());

    /* wine_todo: the earlier NtFsControlFile call gets cancelled after the pipe gets set into listen state
                  instead of before, so this NtFsControlFile will fail STATUS_INVALID_HANDLE */
    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    todo_wine ok(res == STATUS_CANCELLED, "NtFsControlFile returned %x\n", res);

    ok(userapc_called, "user apc didn't run\n");
    ok(U(iosb).Status == 0x55555555, "iosb.Status got changed to %x\n", U(iosb).Status);
    ok(WaitForSingleObjectEx(hEvent, 0, TRUE) == WAIT_TIMEOUT, "hEvent signaled\n");
    ok(!ioapc_called, "IOAPC ran\n");

    WaitForSingleObject(hThread, INFINITE);

    SleepEx(0, TRUE); /* get rid of the userapc, if NtFsControlFile failed */

    ok(open_succeeded, "couldn't open client side pipe\n");

    CloseHandle(hThread);
    DisconnectNamedPipe(hPipe);

/* finally try without an apc */
    hThread = CreateThread(NULL, 0, &thread, 0, 0, 0);
    ok(hThread != INVALID_HANDLE_VALUE, "can't create thread, GetLastError: %x\n", GetLastError());

    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    todo_wine ok(!res, "NtFsControlFile returned %x\n", res);

    ok(open_succeeded, "couldn't open client side pipe\n");
    ok(!U(iosb).Status, "Wrong iostatus %x\n", U(iosb).Status);
    todo_wine ok(WaitForSingleObject(hEvent, 0) == 0, "hEvent not signaled\n");

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(hEvent);
    CloseHandle(hPipe);
}

static void test_nonalertable(void)
{
    IO_STATUS_BLOCK iosb;
    HANDLE hEvent;
    HANDLE hPipe;
    NTSTATUS res;
    HANDLE hThread;
    DWORD ret;

    memset(&iosb, 0x55, sizeof(iosb));

    hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(hEvent != INVALID_HANDLE_VALUE, "can't create event, GetLastError: %x\n", GetLastError());

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    hThread = CreateThread(NULL, 0, &thread, 0, 0, 0);
    ok(hThread != INVALID_HANDLE_VALUE, "can't create thread, GetLastError: %x\n", GetLastError());

    userapc_called = FALSE;
    ret = pQueueUserAPC(&userapc, GetCurrentThread(), 0);
    ok(ret, "can't queue user apc, GetLastError: %x\n", GetLastError());

    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    todo_wine ok(!res, "NtFsControlFile returned %x\n", res);

    ok(open_succeeded, "couldn't open client side pipe\n");
    todo_wine ok(!U(iosb).Status, "Wrong iostatus %x\n", U(iosb).Status);
    todo_wine ok(WaitForSingleObject(hEvent, 0) == 0, "hEvent not signaled\n");

    ok(!ioapc_called, "IOAPC ran too early\n");
    ok(!userapc_called, "user apc ran too early\n");

    SleepEx(0, TRUE); /* alertable wait state */

    ok(ioapc_called, "IOAPC didn't run\n");
    ok(userapc_called, "user apc didn't run\n");

    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CloseHandle(hEvent);
    CloseHandle(hPipe);
}

static void test_cancelio(void)
{
    IO_STATUS_BLOCK iosb;
    IO_STATUS_BLOCK cancel_sb;
    HANDLE hEvent;
    HANDLE hPipe;
    NTSTATUS res;

    hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(hEvent != INVALID_HANDLE_VALUE, "can't create event, GetLastError: %x\n", GetLastError());

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, 0 /* OVERLAPPED */);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    memset(&iosb, 0x55, sizeof(iosb));

    res = listen_pipe(hPipe, hEvent, &iosb, TRUE);
    ok(res == STATUS_PENDING, "NtFsControlFile returned %x\n", res);

    res = pNtCancelIoFile(hPipe, &cancel_sb);
    ok(!res, "NtCancelIoFile returned %x\n", res);

    ok(U(iosb).Status == STATUS_CANCELLED, "Wrong iostatus %x\n", U(iosb).Status);
    ok(WaitForSingleObject(hEvent, 0) == 0, "hEvent not signaled\n");

    ok(!ioapc_called, "IOAPC ran too early\n");

    SleepEx(0, TRUE); /* alertable wait state */

    ok(ioapc_called, "IOAPC didn't run\n");

    CloseHandle(hPipe);

    res = create_pipe(&hPipe, FILE_SHARE_READ | FILE_SHARE_WRITE, 0 /* OVERLAPPED */);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    memset(&iosb, 0x55, sizeof(iosb));
    res = listen_pipe(hPipe, hEvent, &iosb, FALSE);
    ok(res == STATUS_PENDING, "NtFsControlFile returned %x\n", res);

    res = pNtCancelIoFileEx(hPipe, &iosb, &cancel_sb);
    ok(!res, "NtCancelIoFileEx returned %x\n", res);

    ok(U(iosb).Status == STATUS_CANCELLED, "Wrong iostatus %x\n", U(iosb).Status);
    ok(WaitForSingleObject(hEvent, 0) == 0, "hEvent not signaled\n");

    CloseHandle(hEvent);
    CloseHandle(hPipe);
}

static void _check_pipe_handle_state(int line, HANDLE handle, ULONG read, ULONG completion)
{
    IO_STATUS_BLOCK iosb;
    FILE_PIPE_INFORMATION fpi;
    NTSTATUS res;
    if (handle != INVALID_HANDLE_VALUE)
    {
        memset(&fpi, 0x55, sizeof(fpi));
        res = pNtQueryInformationFile(handle, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok_(__FILE__, line)(!res, "NtQueryInformationFile returned %x\n", res);
        ok_(__FILE__, line)(fpi.ReadMode == read, "Unexpected ReadMode, expected %x, got %x\n",
                            read, fpi.ReadMode);
        ok_(__FILE__, line)(fpi.CompletionMode == completion, "Unexpected CompletionMode, expected %x, got %x\n",
                            completion, fpi.CompletionMode);
    }
}
#define check_pipe_handle_state(handle, r, c) _check_pipe_handle_state(__LINE__, handle, r, c)

static void test_filepipeinfo(void)
{
    IO_STATUS_BLOCK iosb;
    OBJECT_ATTRIBUTES attr;
    UNICODE_STRING name;
    LARGE_INTEGER timeout;
    HANDLE hServer, hClient;
    FILE_PIPE_INFORMATION fpi;
    NTSTATUS res;

    pRtlInitUnicodeString(&name, testpipe_nt);

    attr.Length                   = sizeof(attr);
    attr.RootDirectory            = 0;
    attr.ObjectName               = &name;
    attr.Attributes               = 0x40; /* case insensitive */
    attr.SecurityDescriptor       = NULL;
    attr.SecurityQualityOfService = NULL;

    timeout.QuadPart = -100000000;

    /* test with INVALID_HANDLE_VALUE */
    res = pNtQueryInformationFile(INVALID_HANDLE_VALUE, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(res == STATUS_OBJECT_TYPE_MISMATCH, "NtQueryInformationFile returned %x\n", res);

    fpi.ReadMode = 0;
    fpi.CompletionMode = 0;
    res = pNtSetInformationFile(INVALID_HANDLE_VALUE, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(res == STATUS_OBJECT_TYPE_MISMATCH, "NtSetInformationFile returned %x\n", res);

    /* server end with read-only attributes */
    res = pNtCreateNamedPipeFile(&hServer, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,  2 /* FILE_CREATE */,
                                 0, 0, 0, 1, 0xFFFFFFFF, 500, 500, &timeout);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    check_pipe_handle_state(hServer, 0, 1);

    hClient = CreateFileW(testpipe, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    ok(hClient != INVALID_HANDLE_VALUE || broken(GetLastError() == ERROR_PIPE_BUSY) /* > Win 8 */,
       "can't open pipe, GetLastError: %x\n", GetLastError());

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 0, 0);

    fpi.ReadMode = 0;
    fpi.CompletionMode = 0;
    res = pNtSetInformationFile(hServer, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(res == STATUS_ACCESS_DENIED, "NtSetInformationFile returned %x\n", res);

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 0, 0);

    fpi.ReadMode = 1; /* invalid on a byte stream pipe */
    fpi.CompletionMode = 1;
    res = pNtSetInformationFile(hServer, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(res == STATUS_ACCESS_DENIED, "NtSetInformationFile returned %x\n", res);

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 0, 0);

    if (hClient != INVALID_HANDLE_VALUE)
    {
        fpi.ReadMode = 1; /* invalid on a byte stream pipe */
        fpi.CompletionMode = 1;
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(res == STATUS_INVALID_PARAMETER, "NtSetInformationFile returned %x\n", res);
    }

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 0, 0);

    if (hClient != INVALID_HANDLE_VALUE)
    {
        fpi.ReadMode = 0;
        fpi.CompletionMode = 1;
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(!res, "NtSetInformationFile returned %x\n", res);
    }

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 0, 1);

    if (hClient != INVALID_HANDLE_VALUE)
    {
        fpi.ReadMode = 0;
        fpi.CompletionMode = 2; /* not in range 0-1 */
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(res == STATUS_INVALID_PARAMETER || broken(!res) /* < Vista */, "NtSetInformationFile returned %x\n", res);

        fpi.ReadMode = 2; /* not in range 0-1 */
        fpi.CompletionMode = 0;
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(res == STATUS_INVALID_PARAMETER || broken(!res) /* < Vista */, "NtSetInformationFile returned %x\n", res);
    }

    CloseHandle(hClient);

    check_pipe_handle_state(hServer, 0, 1);

    fpi.ReadMode = 0;
    fpi.CompletionMode = 0;
    res = pNtSetInformationFile(hServer, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(res == STATUS_ACCESS_DENIED, "NtSetInformationFile returned %x\n", res);

    CloseHandle(hServer);

    /* message mode server with read/write attributes */
    res = pNtCreateNamedPipeFile(&hServer, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &attr, &iosb,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,  2 /* FILE_CREATE */,
                                 0, 1, 1, 0, 0xFFFFFFFF, 500, 500, &timeout);
    ok(!res, "NtCreateNamedPipeFile returned %x\n", res);

    check_pipe_handle_state(hServer, 1, 0);

    hClient = CreateFileW(testpipe, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    ok(hClient != INVALID_HANDLE_VALUE || broken(GetLastError() == ERROR_PIPE_BUSY) /* > Win 8 */,
       "can't open pipe, GetLastError: %x\n", GetLastError());

    check_pipe_handle_state(hServer, 1, 0);
    check_pipe_handle_state(hClient, 0, 0);

    if (hClient != INVALID_HANDLE_VALUE)
    {
        fpi.ReadMode = 1;
        fpi.CompletionMode = 1;
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(!res, "NtSetInformationFile returned %x\n", res);
    }

    check_pipe_handle_state(hServer, 1, 0);
    check_pipe_handle_state(hClient, 1, 1);

    fpi.ReadMode = 0;
    fpi.CompletionMode = 1;
    res = pNtSetInformationFile(hServer, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(!res, "NtSetInformationFile returned %x\n", res);

    check_pipe_handle_state(hServer, 0, 1);
    check_pipe_handle_state(hClient, 1, 1);

    if (hClient != INVALID_HANDLE_VALUE)
    {
        fpi.ReadMode = 0;
        fpi.CompletionMode = 2; /* not in range 0-1 */
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(res == STATUS_INVALID_PARAMETER || broken(!res) /* < Vista */, "NtSetInformationFile returned %x\n", res);

        fpi.ReadMode = 2; /* not in range 0-1 */
        fpi.CompletionMode = 0;
        res = pNtSetInformationFile(hClient, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
        ok(res == STATUS_INVALID_PARAMETER || broken(!res) /* < Vista */, "NtSetInformationFile returned %x\n", res);
    }

    CloseHandle(hClient);

    check_pipe_handle_state(hServer, 0, 1);

    fpi.ReadMode = 1;
    fpi.CompletionMode = 0;
    res = pNtSetInformationFile(hServer, &iosb, &fpi, sizeof(fpi), (FILE_INFORMATION_CLASS)23);
    ok(!res, "NtSetInformationFile returned %x\n", res);

    check_pipe_handle_state(hServer, 1, 0);

    CloseHandle(hServer);
}

static void WINAPI apc( void *arg, IO_STATUS_BLOCK *iosb, ULONG reserved )
{
    int *count = arg;
    (*count)++;
    ok( !reserved, "reserved is not 0: %x\n", reserved );
}

#define PIPENAME "\\\\.\\pipe\\ntdll_tests_pipe.c"

static BOOL create_pipe_pair( HANDLE *read, HANDLE *write, ULONG flags, ULONG type, ULONG size )
{
    const BOOL server_reader = flags & PIPE_ACCESS_INBOUND;
    HANDLE client, server;

    server = CreateNamedPipeA(PIPENAME, flags, PIPE_WAIT | type,
                              1, size, size, NMPWAIT_USE_DEFAULT_WAIT, NULL);
    ok(server != INVALID_HANDLE_VALUE, "CreateNamedPipe failed\n");

    client = CreateFileA(PIPENAME, server_reader ? GENERIC_WRITE : GENERIC_READ | FILE_WRITE_ATTRIBUTES, 0,
                         NULL, OPEN_EXISTING, flags & FILE_FLAG_OVERLAPPED, 0);
    ok(client != INVALID_HANDLE_VALUE, "CreateFile failed (%d)\n", GetLastError());

    if(server_reader)
    {
        *read = server;
        *write = client;
    }
    else
    {
        if(type & PIPE_READMODE_MESSAGE)
        {
            DWORD read_mode = PIPE_READMODE_MESSAGE;
            ok(SetNamedPipeHandleState(client, &read_mode, NULL, NULL), "Change mode\n");
        }

        *read = client;
        *write = server;
    }
    return TRUE;
}

static void read_pipe_test(ULONG pipe_flags, ULONG pipe_type)
{
    IO_STATUS_BLOCK iosb, iosb2;
    HANDLE handle, read, write;
    HANDLE event = CreateEventA( NULL, TRUE, FALSE, NULL );
    int apc_count = 0;
    char buffer[128];
    DWORD written;
    BOOL ret;
    NTSTATUS status;

    if (!create_pipe_pair( &read, &write, FILE_FLAG_OVERLAPPED | pipe_flags, pipe_type, 4096 )) return;

    /* try read with no data */
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    ok( is_signaled( read ), "read handle is not signaled\n" );
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( read ), "read handle is signaled\n" );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    ret = WriteFile( write, buffer, 1, &written, NULL );
    ok(ret && written == 1, "WriteFile error %d\n", GetLastError());
    /* iosb updated here by async i/o */
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( !is_signaled( read ), "read handle is signaled\n" );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    apc_count = 0;
    SleepEx( 1, FALSE ); /* non-alertable sleep */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc not called\n" );

    /* with no event, the pipe handle itself gets signaled */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    ok( !is_signaled( read ), "read handle is signaled\n" );
    status = NtReadFile( read, 0, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( read ), "read handle is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    ret = WriteFile( write, buffer, 1, &written, NULL );
    ok(ret && written == 1, "WriteFile error %d\n", GetLastError());
    /* iosb updated here by async i/o */
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( read ), "read handle is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    apc_count = 0;
    SleepEx( 1, FALSE ); /* non-alertable sleep */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc not called\n" );

    /* now read with data ready */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    ResetEvent( event );
    ret = WriteFile( write, buffer, 1, &written, NULL );
    ok(ret && written == 1, "WriteFile error %d\n", GetLastError());
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_SUCCESS, "wrong status %x\n", status );
    ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, FALSE ); /* non-alertable sleep */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc not called\n" );

    /* now partial read with data ready */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    ResetEvent( event );
    ret = WriteFile( write, buffer, 2, &written, NULL );
    ok(ret && written == 2, "WriteFile error %d\n", GetLastError());
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    if (pipe_type & PIPE_READMODE_MESSAGE)
    {
        ok( status == STATUS_BUFFER_OVERFLOW, "wrong status %x\n", status );
        ok( U(iosb).Status == STATUS_BUFFER_OVERFLOW, "wrong status %x\n", U(iosb).Status );
    }
    else
    {
        ok( status == STATUS_SUCCESS, "wrong status %x\n", status );
        ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    }
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, FALSE ); /* non-alertable sleep */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc not called\n" );
    apc_count = 0;
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_SUCCESS, "wrong status %x\n", status );
    ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, FALSE ); /* non-alertable sleep */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc not called\n" );

    /* try read with no data */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    ok( is_signaled( event ), "event is not signaled\n" ); /* check that read resets the event */
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    ret = WriteFile( write, buffer, 1, &written, NULL );
    ok(ret && written == 1, "WriteFile error %d\n", GetLastError());
    /* partial read is good enough */
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( U(iosb).Status == 0, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 1, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc was not called\n" );

    /* read from disconnected pipe */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    CloseHandle( write );
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_PIPE_BROKEN, "wrong status %x\n", status );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( !apc_count, "apc was called\n" );
    CloseHandle( read );

    /* read from disconnected pipe, with invalid event handle */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    status = NtReadFile( read, (HANDLE)0xdeadbeef, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_INVALID_HANDLE, "wrong status %x\n", status );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( !apc_count, "apc was called\n" );
    CloseHandle( read );

    /* read from closed handle */
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    SetEvent( event );
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 1, NULL, NULL );
    ok( status == STATUS_INVALID_HANDLE, "wrong status %x\n", status );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );  /* not reset on invalid handle */
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( !apc_count, "apc was called\n" );

    /* disconnect while async read is in progress */
    if (!create_pipe_pair( &read, &write, FILE_FLAG_OVERLAPPED | pipe_flags, pipe_type, 4096 )) return;
    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    CloseHandle( write );
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    todo_wine_if(!(pipe_type & PIPE_TYPE_MESSAGE) && (pipe_flags & PIPE_ACCESS_OUTBOUND))
    ok( U(iosb).Status == STATUS_PIPE_BROKEN, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc was not called\n" );
    CloseHandle( read );

    if (!create_pipe_pair( &read, &write, FILE_FLAG_OVERLAPPED | pipe_flags, pipe_type, 4096 )) return;
    ret = DuplicateHandle(GetCurrentProcess(), read, GetCurrentProcess(), &handle, 0, TRUE, DUPLICATE_SAME_ACCESS);
    ok(ret, "Failed to duplicate handle: %d\n", GetLastError());

    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    status = NtReadFile( handle, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    /* Cancel by other handle */
    status = pNtCancelIoFile( read, &iosb2 );
    ok(status == STATUS_SUCCESS, "failed to cancel by different handle: %x\n", status);
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    ok( U(iosb).Status == STATUS_CANCELLED, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc was not called\n" );

    apc_count = 0;
    U(iosb).Status = 0xdeadbabe;
    iosb.Information = 0xdeadbeef;
    status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
    ok( status == STATUS_PENDING, "wrong status %x\n", status );
    ok( !is_signaled( event ), "event is signaled\n" );
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    ok( !apc_count, "apc was called\n" );
    /* Close queued handle */
    CloseHandle( read );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
    status = pNtCancelIoFile( read, &iosb2 );
    ok(status == STATUS_INVALID_HANDLE, "cancelled by closed handle?\n");
    status = pNtCancelIoFile( handle, &iosb2 );
    ok(status == STATUS_SUCCESS, "failed to cancel: %x\n", status);
    Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
    ok( U(iosb).Status == STATUS_CANCELLED, "wrong status %x\n", U(iosb).Status );
    ok( iosb.Information == 0, "wrong info %lu\n", iosb.Information );
    ok( is_signaled( event ), "event is not signaled\n" );
    ok( !apc_count, "apc was called\n" );
    SleepEx( 1, TRUE ); /* alertable sleep */
    ok( apc_count == 1, "apc was not called\n" );
    CloseHandle( handle );
    CloseHandle( write );

    if (pNtCancelIoFileEx)
    {
        /* Basic Cancel Ex */
        if (!create_pipe_pair( &read, &write, FILE_FLAG_OVERLAPPED | pipe_flags, pipe_type, 4096 )) return;

        apc_count = 0;
        U(iosb).Status = 0xdeadbabe;
        iosb.Information = 0xdeadbeef;
        status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
        ok( status == STATUS_PENDING, "wrong status %x\n", status );
        ok( !is_signaled( event ), "event is signaled\n" );
        ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
        ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
        ok( !apc_count, "apc was called\n" );
        status = pNtCancelIoFileEx( read, &iosb, &iosb2 );
        ok(status == STATUS_SUCCESS, "Failed to cancel I/O\n");
        Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
        ok( U(iosb).Status == STATUS_CANCELLED, "wrong status %x\n", U(iosb).Status );
        ok( iosb.Information == 0, "wrong info %lu\n", iosb.Information );
        ok( is_signaled( event ), "event is not signaled\n" );
        ok( !apc_count, "apc was called\n" );
        SleepEx( 1, TRUE ); /* alertable sleep */
        ok( apc_count == 1, "apc was not called\n" );

        /* Duplicate iosb */
        apc_count = 0;
        U(iosb).Status = 0xdeadbabe;
        iosb.Information = 0xdeadbeef;
        status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
        ok( status == STATUS_PENDING, "wrong status %x\n", status );
        ok( !is_signaled( event ), "event is signaled\n" );
        ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
        ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
        ok( !apc_count, "apc was called\n" );
        status = NtReadFile( read, event, apc, &apc_count, &iosb, buffer, 2, NULL, NULL );
        ok( status == STATUS_PENDING, "wrong status %x\n", status );
        ok( !is_signaled( event ), "event is signaled\n" );
        ok( U(iosb).Status == 0xdeadbabe, "wrong status %x\n", U(iosb).Status );
        ok( iosb.Information == 0xdeadbeef, "wrong info %lu\n", iosb.Information );
        ok( !apc_count, "apc was called\n" );
        status = pNtCancelIoFileEx( read, &iosb, &iosb2 );
        ok(status == STATUS_SUCCESS, "Failed to cancel I/O\n");
        Sleep(1);  /* FIXME: needed for wine to run the i/o apc  */
        ok( U(iosb).Status == STATUS_CANCELLED, "wrong status %x\n", U(iosb).Status );
        ok( iosb.Information == 0, "wrong info %lu\n", iosb.Information );
        ok( is_signaled( event ), "event is not signaled\n" );
        ok( !apc_count, "apc was called\n" );
        SleepEx( 1, TRUE ); /* alertable sleep */
        ok( apc_count == 2, "apc was not called\n" );

        CloseHandle( read );
        CloseHandle( write );
    }

    CloseHandle(event);
}

START_TEST(pipe)
{
    if (!init_func_ptrs())
        return;

    trace("starting invalid create tests\n");
    test_create_invalid();

    trace("starting create tests\n");
    test_create();

    trace("starting overlapped tests\n");
    test_overlapped();

    trace("starting completion tests\n");
    test_completion();

    trace("starting FILE_PIPE_INFORMATION tests\n");
    test_filepipeinfo();

    if (!pOpenThread || !pQueueUserAPC)
        return;

    trace("starting alertable tests\n");
    test_alertable();

    trace("starting nonalertable tests\n");
    test_nonalertable();

    trace("starting cancelio tests\n");
    test_cancelio();

    trace("starting byte read in byte mode client -> server\n");
    read_pipe_test(PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE);
    trace("starting byte read in message mode client -> server\n");
    read_pipe_test(PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE);
    trace("starting message read in message mode client -> server\n");
    read_pipe_test(PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE);
    trace("starting byte read in byte mode server -> client\n");
    read_pipe_test(PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE);
    trace("starting byte read in message mode server -> client\n");
    read_pipe_test(PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE);
    trace("starting message read in message mode server -> client\n");
    read_pipe_test(PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE);
}
