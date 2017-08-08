/*
 * Copyright (C) 2017 Michael Müller
 *
 * Dll for testing custom msi actions.
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

#define _WIN32_MSI 300
#include <stdio.h>
#include <stdlib.h>

#include <windows.h>
#include <msiquery.h>
#include <msidefs.h>
#include <msi.h>

static const char *pipename = "\\\\.\\pipe\\wine_custom_action";
HANDLE pipe_handle;

static void send_msg(const char *type, const char *file, int line, const char *msg)
{
    DWORD written = 0;
    char buf[512];

    sprintf(buf, "%s:%s:%d:%s", type, file, line, msg);
    WriteFile(pipe_handle, buf, strlen(buf)+1, &written, NULL);
}

static inline void pipe_trace(const char *file, int line, const char *msg, ...)
{
    va_list valist;
    char buf[512];

    va_start(valist, msg);
    vsprintf(buf, msg, valist);
    va_end(valist);

    send_msg("TRACE", file, line, buf);
}

static void pipe_ok(int cnd, const char *file, int line, const char *msg, ...)
{
   va_list valist;
   char buf[512];

    va_start(valist, msg);
    vsprintf(buf, msg, valist);
    va_end(valist);

    send_msg(cnd ? "OK" : "FAIL", file, line, buf);
}

#define trace(msg, ...) pipe_trace((cnd), __FILE__, __LINE__, msg, __VA_ARGS__)
#define ok(cnd, msg, ...) pipe_ok((cnd), __FILE__, __LINE__, msg, __VA_ARGS__)

static UINT connect_named_pipe(void)
{
    BOOL res;

    res = WaitNamedPipeA(pipename, NMPWAIT_USE_DEFAULT_WAIT);
    if(!res) return ERROR_BROKEN_PIPE;

    pipe_handle = CreateFileA(pipename, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (pipe_handle == INVALID_HANDLE_VALUE) return ERROR_BROKEN_PIPE;

    return ERROR_SUCCESS;
}

UINT WINAPI testfunc1(MSIHANDLE handle)
{
    MSIDBSTATE state;
    UINT res;

    res = connect_named_pipe();
    if (res) return res;

    state = MsiGetDatabaseState(handle);
    ok(state == MSIDBSTATE_ERROR, "Expected MSIDBSTATE_ERROR, got %d\n", state);

    CloseHandle(pipe_handle);
    return ERROR_SUCCESS;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
