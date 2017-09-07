/*
 * ntoskrnl.exe testing framework
 *
 * Copyright 2015 Sebastian Lackner
 * Copyright 2015 Michael Müller
 * Copyright 2015 Christian Costa
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

#define NONAMELESSUNION
#define NONAMELESSSTRUCT

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/ntddk.h"
#include "ddk/wdm.h"

#define WINE_KERNEL
#include "util.h"
#include "test.h"
#include "driver.h"

extern PVOID WINAPI MmGetSystemRoutineAddress(PUNICODE_STRING);

const WCHAR driver_device[] = {'\\','D','e','v','i','c','e',
                               '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};
const WCHAR driver_link[] = {'\\','D','o','s','D','e','v','i','c','e','s',
                             '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};

static LDR_MODULE *ldr_module;

static void *get_system_routine(const char *name)
{
    UNICODE_STRING name_u;
    ANSI_STRING name_a;
    NTSTATUS status;
    void *ret;

    RtlInitAnsiString(&name_a, name);
    status = RtlAnsiStringToUnicodeString(&name_u, &name_a, TRUE);
    if (status) return NULL;

    ret = MmGetSystemRoutineAddress(&name_u);
    RtlFreeUnicodeString(&name_u);
    return ret;
}

/* In each kernel testcase the following variables are available:
 *
 *   device     - DEVICE_OBJECT used for ioctl
 *   irp        - IRP pointer passed to ioctl
 *   __state    - used internally for test macros
 */

KERNEL_TESTCASE(PsGetCurrentProcessId)
{
    struct test_PsGetCurrentProcessId *test = (void *)&__state->userdata;
    test->pid = (DWORD)(ULONG_PTR)PsGetCurrentProcessId();
    ok(test->pid, "Expected processid to be non zero\n");
    return STATUS_SUCCESS;
}

KERNEL_TESTCASE(PsGetCurrentThread)
{
    PETHREAD thread = PsGetCurrentThread();
    todo_wine ok(thread != NULL, "Expected thread to be non-NULL\n");
    return STATUS_SUCCESS;
}

KERNEL_TESTCASE(NtBuildNumber)
{
    USHORT *pNtBuildNumber;
    ULONG build;

    if (!(pNtBuildNumber = get_system_routine("NtBuildNumber")))
    {
        win_skip("Could not get pointer to NtBuildNumber\n");
        return STATUS_SUCCESS;
    }

    PsGetVersion(NULL, NULL, &build, NULL);
    ok(*pNtBuildNumber == build, "Expected build number %u, got %u\n", build, *pNtBuildNumber);
    return STATUS_SUCCESS;
}

KERNEL_TESTCASE(ExInitializeNPagedLookasideList)
{
    NPAGED_LOOKASIDE_LIST list;
    ULONG tag = 0x454e4957; /* WINE */

    ExInitializeNPagedLookasideList(&list, NULL, NULL, POOL_NX_ALLOCATION, LOOKASIDE_MINIMUM_BLOCK_SIZE, tag, 0);
    ok(list.L.Depth == 4, "Expected 4 got %u\n", list.L.Depth);
    ok(list.L.MaximumDepth == 256, "Expected 256 got %u\n", list.L.MaximumDepth);
    ok(list.L.TotalAllocates == 0, "Expected 0 got %u\n", list.L.TotalAllocates);
    ok(list.L.u2.AllocateMisses == 0, "Expected 0 got %u\n", list.L.u2.AllocateMisses);
    ok(list.L.TotalFrees == 0, "Expected 0 got %u\n", list.L.TotalFrees);
    ok(list.L.u3.FreeMisses == 0, "Expected 0 got %u\n", list.L.u3.FreeMisses);
    ok(list.L.Type == (NonPagedPool|POOL_NX_ALLOCATION),
       "Expected NonPagedPool|POOL_NX_ALLOCATION got %u\n", list.L.Type);
    ok(list.L.Tag == tag, "Expected %x got %x\n", tag, list.L.Tag);
    ok(list.L.Size == LOOKASIDE_MINIMUM_BLOCK_SIZE,
       "Expected %u got %u\n", LOOKASIDE_MINIMUM_BLOCK_SIZE, list.L.Size);
    ok(list.L.LastTotalAllocates == 0,"Expected 0 got %u\n", list.L.LastTotalAllocates);
    ok(list.L.u6.LastAllocateMisses == 0,"Expected 0 got %u\n", list.L.u6.LastAllocateMisses);
    ExDeleteNPagedLookasideList(&list);

    list.L.Depth = 0;
    ExInitializeNPagedLookasideList(&list, NULL, NULL, 0, LOOKASIDE_MINIMUM_BLOCK_SIZE, tag, 20);
    ok(list.L.Depth == 4, "Expected 4 got %u\n", list.L.Depth);
    ExDeleteNPagedLookasideList(&list);

    return STATUS_SUCCESS;
}

static BOOL equal_string(ANSI_STRING *str1, const char *str2)
{
    if (str1->Length != kernel_strlen(str2)) return FALSE;
    return !kernel_strncmp(str1->Buffer, str2, str1->Length);
}

KERNEL_TESTCASE(LdrModules)
{
    BOOL win32k = FALSE, dxgkrnl = FALSE, dxgmms1 = FALSE;
    LIST_ENTRY *start, *entry;
    ANSI_STRING name_a;
    LDR_MODULE *mod;
    NTSTATUS status;

    /* Try to find start of the InLoadOrderModuleList list */
    for (start = ldr_module->InLoadOrderModuleList.Flink; ; start = start->Flink)
    {
        mod = CONTAINING_RECORD(start, LDR_MODULE, InLoadOrderModuleList);

        if (!MmIsAddressValid(&mod->BaseAddress) || !mod->BaseAddress) break;
        if (!MmIsAddressValid(&mod->LoadCount) || !mod->LoadCount) break;
        if (!MmIsAddressValid(&mod->SizeOfImage) || !mod->SizeOfImage) break;
        if (!MmIsAddressValid(&mod->EntryPoint) || mod->EntryPoint < mod->BaseAddress ||
            (DWORD_PTR)mod->EntryPoint > (DWORD_PTR)mod->BaseAddress + mod->SizeOfImage) break;
    }

    for (entry = start->Flink; entry != start; entry = entry->Flink)
    {
        mod = CONTAINING_RECORD(entry, LDR_MODULE, InLoadOrderModuleList);

        status = RtlUnicodeStringToAnsiString(&name_a, &mod->BaseDllName, TRUE);
        ok(!status, "RtlUnicodeStringToAnsiString failed with %08x\n", status);
        if (status) continue;

        if (entry == start->Flink)
        {
            ok(equal_string(&name_a, "ntoskrnl.exe"),
               "Expected ntoskrnl.exe, got %.*s\n", name_a.Length, name_a.Buffer);
        }

        if (equal_string(&name_a, "win32k.sys"))  win32k  = TRUE;
        if (equal_string(&name_a, "dxgkrnl.sys")) dxgkrnl = TRUE;
        if (equal_string(&name_a, "dxgmms1.sys")) dxgmms1 = TRUE;

        RtlFreeAnsiString(&name_a);
    }

    ok(win32k,  "Failed to find win32k.sys\n");
    ok(dxgkrnl, "Failed to find dxgkrnl.sys\n");
    ok(dxgmms1, "Failed to find dxgmms1.sys\n");

    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_Create(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.u.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_IoControl(DEVICE_OBJECT *device, IRP *irp)
{
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    struct kernel_test_state *state = irp->AssociatedIrp.SystemBuffer;
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    ULONG_PTR information = 0;

    if (!state)
    {
        status = STATUS_ACCESS_VIOLATION;
        goto done;
    }

    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*state) ||
        stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*state))
    {
        status = STATUS_BUFFER_TOO_SMALL;
        goto done;
    }

    kernel_memset(&state->temp, 0, sizeof(state->temp));
    kernel_memset(&state->output, 0, sizeof(state->output));

#define DECLARE_TEST(name) \
    case WINE_IOCTL_##name: status = test_##name(device, irp, state); break;

    switch (stack->Parameters.DeviceIoControl.IoControlCode)
    {
        DECLARE_TEST(PsGetCurrentProcessId);
        DECLARE_TEST(PsGetCurrentThread);
        DECLARE_TEST(NtBuildNumber);
        DECLARE_TEST(ExInitializeNPagedLookasideList);
        DECLARE_TEST(LdrModules);

        default:
            break;
    }

#undef DECLARE_TEST

    kernel_memset(&state->temp, 0, sizeof(state->temp));
    if (status == STATUS_SUCCESS) information = sizeof(*state);

done:
    irp->IoStatus.u.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS WINAPI driver_Close(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.u.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static VOID WINAPI driver_Unload(DRIVER_OBJECT *driver)
{
    UNICODE_STRING linkW;

    DbgPrint("unloading driver\n");

    RtlInitUnicodeString(&linkW, driver_link);
    IoDeleteSymbolicLink(&linkW);

    IoDeleteDevice(driver->DeviceObject);
}

NTSTATUS WINAPI DriverEntry(DRIVER_OBJECT *driver, PUNICODE_STRING registry)
{
    UNICODE_STRING nameW, linkW;
    DEVICE_OBJECT *device;
    NTSTATUS status;

    DbgPrint("loading driver\n");

    ldr_module = (LDR_MODULE*)driver->DriverSection;

    /* Allow unloading of the driver */
    driver->DriverUnload = driver_Unload;

    /* Set driver functions */
    driver->MajorFunction[IRP_MJ_CREATE]            = driver_Create;
    driver->MajorFunction[IRP_MJ_DEVICE_CONTROL]    = driver_IoControl;
    driver->MajorFunction[IRP_MJ_CLOSE]             = driver_Close;

    RtlInitUnicodeString(&nameW, driver_device);
    RtlInitUnicodeString(&linkW, driver_link);

    if (!(status = IoCreateDevice(driver, 0, &nameW, FILE_DEVICE_UNKNOWN,
                                  FILE_DEVICE_SECURE_OPEN, FALSE, &device)))
        status = IoCreateSymbolicLink(&linkW, &nameW);

    return status;
}
