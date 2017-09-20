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

#ifndef _WINE_KERNEL_TEST_
#define _WINE_KERNEL_TEST_

struct kernel_test_state
{
    char userdata[1024];
    struct
    {
        int debug_level;
        int report_success;
        BOOL windows;
    } input;
    struct
    {
        const char *file;
        int line;
        int todo_level;
        int todo_do_loop;
    } temp;
    struct
    {
        LONG failures;
        LONG successes;
        LONG todo_failures;
        LONG todo_successes;
        LONG skipped;

        BOOL overflow;
        DWORD offset;
        char debug[4096];
    } output;
};

#define WINE_TEST_IOCTL(index) CTL_CODE(FILE_DEVICE_UNKNOWN, (index) + 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

#ifdef WINE_KERNEL

#ifdef __GNUC__
void __cdecl winetest_ok(struct kernel_test_state *state, int condition, const char *msg, ...) __attribute__((format (printf,3,4) ));
void __cdecl winetest_skip(struct kernel_test_state *state, const char *msg, ...) __attribute__((format (printf,2,3)));
void __cdecl winetest_win_skip(struct kernel_test_state *state, const char *msg, ...) __attribute__((format (printf,2,3)));
void __cdecl winetest_trace(struct kernel_test_state *state, const char *msg, ...) __attribute__((format (printf,2,3)));
#else
void __cdecl winetest_ok(struct kernel_test_state *state, int condition, const char *msg, ...);
void __cdecl winetest_skip(struct kernel_test_state *state, const char *msg, ...);
void __cdecl winetest_win_skip(struct kernel_test_state *state, const char *msg, ...);
void __cdecl winetest_trace(struct kernel_test_state *state, const char *msg, ...);
#endif /* __GNUC__ */

void winetest_set_location(struct kernel_test_state *state, const char* file, int line);
void winetest_start_todo(struct kernel_test_state *state, int windows);
int winetest_loop_todo(struct kernel_test_state *state);
void winetest_end_todo(struct kernel_test_state *state, int windows);

#define ok_(file, line)       (winetest_set_location(__state, file, line), 0) ? (void)0 : winetest_ok
#define skip_(file, line)     (winetest_set_location(__state, file, line), 0) ? (void)0 : winetest_skip
#define win_skip_(file, line) (winetest_set_location(__state, file, line), 0) ? (void)0 : winetest_win_skip
#define trace_(file, line)    (winetest_set_location(__state, file, line), 0) ? (void)0 : winetest_trace

#define _ok       ok_(__FILE__, __LINE__)
#define _skip     skip_(__FILE__, __LINE__)
#define _win_skip win_skip_(__FILE__, __LINE__)
#define _trace    trace_(__FILE__, __LINE__)

/* Variadic Macros */
#define ok(...)       _ok(__state, __VA_ARGS__)
#define skip(...)     _skip(__state, __VA_ARGS__)
#define win_skip(...) _win_skip(__state, __VA_ARGS__)
#define trace(...)    _trace(__state, __VA_ARGS__)

#define todo(windows) for (winetest_start_todo(__state, windows); \
                           winetest_loop_todo(__state); \
                           winetest_end_todo(__state, windows))
#define todo_wine      todo(0)

#define KERNEL_TESTCASE(name)                                       \
    static NTSTATUS test_##name(DEVICE_OBJECT *device, IRP *irp,    \
                                struct kernel_test_state *__state)

#else

#include <stdio.h>

#define wine_run_kernel_test(device_path, ioctl, state) \
    __wine_run_kernel_test(__FILE__, __LINE__, device_path, ioctl, state)

static void __wine_run_kernel_test(const char *file, int line, const char *device_path,
                                   DWORD ioctl, struct kernel_test_state *state)
{
    struct kernel_test_state temp_state;
    DWORD returned;
    HANDLE device;
    BOOL ret;

    device = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    ok_(file, line)(device != INVALID_HANDLE_VALUE, "CreateFileA failed with error %u\n", GetLastError());
    if (device == INVALID_HANDLE_VALUE) return;

    if (!state)
    {
        state = &temp_state;
        memset(state, 0, sizeof(*state));
    }

    state->input.debug_level    = winetest_get_debug();
    state->input.report_success = winetest_get_report_success();
    state->input.windows        = !strcmp(winetest_platform, "windows");

    ret = DeviceIoControl(device, ioctl, state, sizeof(*state), state, sizeof(*state), &returned, NULL);
    ok_(file, line)(ret, "DeviceIoControl failed with error %u\n", GetLastError());
    ok_(file, line)(returned == sizeof(*state), "DeviceIoControl returned %u bytes\n", returned);

    if (state->output.offset >= sizeof(state->output.debug))
        state->output.offset = sizeof(state->output.debug) - 1;

    if (state->output.offset)
    {
        state->output.debug[state->output.offset] = 0;
        fprintf(stdout, "%s", state->output.debug);
        if (state->output.debug[state->output.offset - 1] != '\n')
            fprintf(stdout, "<line truncated>\n");
    }

    if (state->output.overflow)
        skip_(file, line)("Buffer was too small, kernel debug output is truncated!\n");

    winetest_add_failures(state->output.failures);
    winetest_add_successes(state->output.successes);
    winetest_add_todo_failures(state->output.todo_failures);
    winetest_add_todo_successes(state->output.todo_successes);
    winetest_add_skipped(state->output.skipped);

    CloseHandle(device);
}

#endif

#endif /* _WINE_KERNEL_TEST_ */
