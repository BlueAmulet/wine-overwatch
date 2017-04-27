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

#define KERNEL_TESTCASE(name)                                                                   \
    static NTSTATUS test_##name(IRP *irp, IO_STACK_LOCATION *stack, ULONG_PTR *info,            \
                                struct test_##name##_state *test, struct kernel_test_state *__state); \
                                                                                                \
    static NTSTATUS __test_##name(IRP *irp, IO_STACK_LOCATION *stack, ULONG_PTR *info)          \
    {                                                                                           \
        ULONG input_length  = stack->Parameters.DeviceIoControl.InputBufferLength;              \
        ULONG output_length = stack->Parameters.DeviceIoControl.OutputBufferLength;             \
        struct kernel_test_state *state = irp->AssociatedIrp.SystemBuffer;                      \
        NTSTATUS status;                                                                        \
                                                                                                \
        if (!state)                                                                             \
            return STATUS_ACCESS_VIOLATION;                                                     \
                                                                                                \
        if (input_length < sizeof(struct test_##name##_state) ||                                \
            output_length < sizeof(struct test_##name##_state))                                 \
            return STATUS_BUFFER_TOO_SMALL;                                                     \
                                                                                                \
        kernel_memset(&state->temp,   0, sizeof(state->temp));                                  \
        kernel_memset(&state->output, 0, sizeof(state->output));                                \
        status = test_##name(irp, stack, info, irp->AssociatedIrp.SystemBuffer, state);         \
                                                                                                \
        kernel_memset(&state->temp, 0, sizeof(state->temp));                                    \
        *info = sizeof(struct test_##name##_state);                                             \
        return status;                                                                          \
    }                                                                                           \
                                                                                                \
    static NTSTATUS test_##name(IRP *irp, IO_STACK_LOCATION *stack, ULONG_PTR *info,            \
                                struct test_##name##_state *test, struct kernel_test_state *__state)

#define RUN_TESTCASE(name, irp, stack, info) \
    __test_##name(irp, stack, info)

#else

#include <stdio.h>

#define wine_run_kernel_test(device, ioctl, state, size, returned) \
    __wine_run_kernel_test(__FILE__, __LINE__, device, ioctl, state, size, returned)

static BOOL __wine_run_kernel_test(const char* file, int line, HANDLE device, DWORD ioctl,
                                   void *data, DWORD size, DWORD *returned)
{
    struct kernel_test_state *state = data;
    DWORD bytes_returned;
    BOOL res;

    if (!returned)
        returned = &bytes_returned;

    memset(state, 0, sizeof(*state));
    state->input.debug_level    = winetest_get_debug();
    state->input.report_success = winetest_get_report_success();
    state->input.windows        = !strcmp(winetest_platform, "windows");

    res = DeviceIoControl(device, ioctl, data, size, data, size, returned, NULL);

    if (returned == &bytes_returned)
        ok_(file, line)(bytes_returned == size,
            "DeviceIoControl returned %u bytes, expected %u bytes\n", bytes_returned, size);

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

    return res;
}

#endif

#endif /* _WINE_KERNEL_TEST_ */
