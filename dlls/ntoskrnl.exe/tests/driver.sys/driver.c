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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"

#include "driver.h"

const WCHAR driver_device[] = {'\\','D','e','v','i','c','e',
                               '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};
const WCHAR driver_link[] = {'\\','D','o','s','D','e','v','i','c','e','s',
                             '\\','W','i','n','e','T','e','s','t','D','r','i','v','e','r',0};


static NTSTATUS test_basic_ioctl(IRP *irp, IO_STACK_LOCATION *stack, ULONG_PTR *info)
{
    const char str[] = "Wine is not an emulator";
    ULONG length = stack->Parameters.DeviceIoControl.OutputBufferLength;
    char *buffer = irp->AssociatedIrp.SystemBuffer;
    int i;

    if (!buffer)
        return STATUS_ACCESS_VIOLATION;

    if (length < sizeof(str)-1)
        return STATUS_BUFFER_TOO_SMALL;

    for (i = 0; i < sizeof(str)-1; i++)
        buffer[i] = str[i];

    *info = sizeof(str)-1;
    return STATUS_SUCCESS;
}


static NTSTATUS WINAPI driver_Create(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static NTSTATUS WINAPI driver_IoControl(DEVICE_OBJECT *device, IRP *irp)
{
    IO_STACK_LOCATION *stack = IoGetCurrentIrpStackLocation(irp);
    NTSTATUS status = STATUS_NOT_SUPPORTED;
    ULONG_PTR information = 0;

    switch (stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_WINETEST_BASIC_IOCTL:
            status = test_basic_ioctl(irp, stack, &information);
            break;

        default:
            break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = information;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

static NTSTATUS WINAPI driver_Close(DEVICE_OBJECT *device, IRP *irp)
{
    irp->IoStatus.Status = STATUS_SUCCESS;
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
