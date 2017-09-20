/*
 * runas.exe implementation
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

#include <windows.h>
#include <wine/unicode.h>
#include <wine/debug.h>

#include "runas.h"

WINE_DEFAULT_DEBUG_CHANNEL(runas);

extern HANDLE CDECL __wine_create_default_token(BOOL admin);

struct cmdinfo
{
    WCHAR *program;
    DWORD trustlevel;
};

static void output_writeconsole(const WCHAR *str, DWORD wlen)
{
    DWORD count, ret;

    ret = WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), str, wlen, &count, NULL);
    if (!ret)
    {
        DWORD len;
        char  *msgA;

        /* On Windows WriteConsoleW() fails if the output is redirected. So fall
         * back to WriteFile(), assuming the console encoding is still the right
         * one in that case.
         */
        len = WideCharToMultiByte(GetConsoleOutputCP(), 0, str, wlen, NULL, 0, NULL, NULL);
        msgA = HeapAlloc(GetProcessHeap(), 0, len * sizeof(char));
        if (!msgA) return;

        WideCharToMultiByte(GetConsoleOutputCP(), 0, str, wlen, msgA, len, NULL, NULL);
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msgA, len, &count, FALSE);
        HeapFree(GetProcessHeap(), 0, msgA);
    }
}

static void output_formatstring(const WCHAR *fmt, __ms_va_list va_args)
{
    WCHAR *str;
    DWORD len;

    SetLastError(NO_ERROR);
    len = FormatMessageW(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_ALLOCATE_BUFFER,
                         fmt, 0, 0, (WCHAR *)&str, 0, &va_args);
    if (len == 0 && GetLastError() != NO_ERROR)
    {
        WINE_FIXME("Could not format string: le=%u, fmt=%s\n", GetLastError(), wine_dbgstr_w(fmt));
        return;
    }
    output_writeconsole(str, len);
    LocalFree(str);
}

static void __cdecl output_message(unsigned int id, ...)
{
    WCHAR fmt[1024];
    __ms_va_list va_args;

    if (!LoadStringW(GetModuleHandleW(NULL), id, fmt, sizeof(fmt)/sizeof(WCHAR)))
    {
        WINE_FIXME("LoadString failed with %d\n", GetLastError());
        return;
    }
    __ms_va_start(va_args, id);
    output_formatstring(fmt, va_args);
    __ms_va_end(va_args);
}

static void show_usage(void)
{
    output_message(STRING_USAGE);
}

static void show_trustlevels(void)
{
    output_message(STRING_TRUSTLEVELS);
    ExitProcess(0);
}

static WCHAR *starts_with(WCHAR *str, const WCHAR *start)
{
    DWORD start_len = strlenW(start);
    if (strlenW(str) < start_len)
        return NULL;
    if (strncmpW(str, start, start_len))
        return NULL;
    return str + start_len;
}

static BOOL parse_command_line(int argc, WCHAR *argv[], struct cmdinfo *cmd)
{
    static const WCHAR showtrustlevelsW[] = {'/','s','h','o','w','t','r','u','s','t','l','e','v','e','l','s',0};
    static const WCHAR trustlevelW[] = {'/','t','r','u','s','t','l','e','v','e','l',':',0};
    int i;

    memset(cmd, 0, sizeof(*cmd));

    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '/')
        {
            WCHAR *arg;

            if ((arg = starts_with(argv[i], trustlevelW)))
                cmd->trustlevel = strtoulW(arg, NULL, 0);
            else if (!strcmpW(argv[i], showtrustlevelsW))
                show_trustlevels();
            else
                WINE_FIXME("Ignoring parameter %s\n", wine_dbgstr_w(argv[i]));
        }
        else if (argv[i][0])
        {
            if (cmd->program) return FALSE;
            cmd->program = argv[i];
        }
    }

    return TRUE;
}

static BOOL start_process(struct cmdinfo *cmd)
{
    PROCESS_INFORMATION info;
    STARTUPINFOW startup;
    HANDLE token = NULL;
    BOOL ret;

    if (cmd->trustlevel == 0x20000)
    {
        if (!(token = __wine_create_default_token(FALSE)))
            ERR("Failed to create limited token\n");
    }

    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    ret = CreateProcessAsUserW(token, NULL, cmd->program, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
    if (ret)
    {
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
    else
    {
        DWORD error = GetLastError();
        WCHAR *str;

        if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                           FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, 0, (WCHAR *)&str, 0, NULL))
        {
            output_message(STRING_START_ERROR, cmd->program, error, str);
            LocalFree(str);
        }
        else
            WINE_FIXME("Failed to format error: %u\n", error);
    }

    if (token) CloseHandle(token);
    return ret;
}

int wmain(int argc, WCHAR *argv[])
{
    struct cmdinfo cmd;

    if (argc <= 1)
    {
        show_usage();
        return 0;
    }

    if (!parse_command_line(argc, argv, &cmd))
    {
        show_usage();
        return 1;
    }

    if (!cmd.program)
    {
        show_usage();
        return 1;
    }

    if (cmd.trustlevel && cmd.trustlevel != 0x20000)
    {
        output_message(STRING_UNHANDLED_TRUSTLEVEL, cmd.trustlevel);
        return 1;
    }

    return start_process(&cmd);
}
