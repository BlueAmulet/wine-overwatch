/*
 * ntoskrnl.exe testing framework
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"

#define WINE_KERNEL
#include "util.h"
#include "test.h"

extern int CDECL _vsnprintf(char *buffer, size_t count, const char *format, __ms_va_list argptr);

static void safe_vsnprintf(struct kernel_test_state *state, const char *msg, __ms_va_list args)
{
    int remaining;
    int length;

    if (state->output.overflow)
        return;

    if (state->output.offset >= sizeof(state->output.debug) - 1)
    {
        state->output.overflow = TRUE;
        return;
    }

    remaining = (sizeof(state->output.debug) - 1) - state->output.offset;
    length    = _vsnprintf(&state->output.debug[state->output.offset], remaining, msg, args);
    if (length < 0)
    {
        state->output.debug[state->output.offset] = 0;
        state->output.overflow = TRUE;
        return;
    }

    state->output.offset += length;
}

static void __cdecl safe_snprintf(struct kernel_test_state *state, const char *msg, ...)
{
    __ms_va_list valist;

    __ms_va_start(valist, msg);
    safe_vsnprintf(state, msg, valist);
    __ms_va_end(valist);
}

void winetest_set_location(struct kernel_test_state *state, const char *file, int line)
{
    state->temp.file = kernel_strrchr(file, '/');
    if (state->temp.file == NULL)
        state->temp.file = kernel_strrchr(file, '\\');
    if (state->temp.file == NULL)
        state->temp.file = file;
    else
        state->temp.file++;
    state->temp.line = line;
}

int winetest_vok(struct kernel_test_state *state, int condition, const char *msg, __ms_va_list args)
{
    if (state->temp.todo_level)
    {
        if (condition)
        {
            safe_snprintf( state, "%s:%d: Test succeeded inside todo block: ",
                           state->temp.file, state->temp.line );
            safe_vsnprintf(state, msg, args);
            state->output.todo_failures++;
            return 0;
        }
        else
        {
            if (state->input.debug_level > 0)
            {
                safe_snprintf( state, "%s:%d: Test marked todo: ",
                               state->temp.file, state->temp.line );
                safe_vsnprintf(state, msg, args);
            }
            state->output.todo_successes++;
            return 1;
        }
    }
    else
    {
        if (!condition)
        {
            safe_snprintf( state, "%s:%d: Test failed: ",
                           state->temp.file, state->temp.line );
            safe_vsnprintf(state, msg, args);
            state->output.failures++;
            return 0;
        }
        else
        {
            if (state->input.report_success)
                safe_snprintf( state, "%s:%d: Test succeeded\n",
                               state->temp.file, state->temp.line);
            state->output.successes++;
            return 1;
        }
    }
}

void __cdecl winetest_ok(struct kernel_test_state *state, int condition, const char *msg, ...)
{
    __ms_va_list valist;

    __ms_va_start(valist, msg);
    winetest_vok(state, condition, msg, valist);
    __ms_va_end(valist);
}

void __cdecl winetest_trace(struct kernel_test_state *state, const char *msg, ...)
{
    __ms_va_list valist;

    if (state->input.debug_level > 0)
    {
        safe_snprintf( state, "%s:%d: ", state->temp.file, state->temp.line );
        __ms_va_start(valist, msg);
        safe_vsnprintf(state, msg, valist);
        __ms_va_end(valist);
    }
}

void winetest_vskip(struct kernel_test_state *state, const char *msg, __ms_va_list args)
{
    safe_snprintf( state, "%s:%d: Tests skipped: ", state->temp.file, state->temp.line );
    safe_vsnprintf(state, msg, args);
    state->output.skipped++;
}

void __cdecl winetest_skip(struct kernel_test_state *state, const char *msg, ...)
{
    __ms_va_list valist;
    __ms_va_start(valist, msg);
    winetest_vskip(state, msg, valist);
    __ms_va_end(valist);
}

void __cdecl winetest_win_skip(struct kernel_test_state *state, const char *msg, ...)
{
    __ms_va_list valist;
    __ms_va_start(valist, msg);
    if (!state->input.windows)
        winetest_vskip(state, msg, valist);
    else
        winetest_vok(state, 0, msg, valist);
    __ms_va_end(valist);
}

void winetest_start_todo(struct kernel_test_state *state, int windows)
{
    if (state->input.windows == windows)
        state->temp.todo_level++;
    state->temp.todo_do_loop=1;
}

int winetest_loop_todo(struct kernel_test_state *state)
{
    int do_loop=state->temp.todo_do_loop;
    state->temp.todo_do_loop=0;
    return do_loop;
}

void winetest_end_todo(struct kernel_test_state *state, int windows)
{
    if (state->input.windows == windows)
        state->temp.todo_level--;
}
