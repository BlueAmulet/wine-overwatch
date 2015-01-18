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

#include "windows.h"
#include "winsvc.h"
#include "winioctl.h"
#include "wine/test.h"

#include "driver.sys/driver.h"

static const char driver_name[] = "WineTestDriver";
static const char device_path[] = "\\\\.\\WineTestDriver";

/* extracts a driver from a resource to a filename */
static BOOL extract_resource(const char *name, const char *filename)
{
    DWORD size, written = 0;
    HGLOBAL reshandle;
    HRSRC resinfo;
    HANDLE file;
    char *data;

    resinfo = FindResourceA(NULL, name, (LPCSTR)RT_RCDATA);
    if (!resinfo)
        return FALSE;

    reshandle = LoadResource(NULL, resinfo);
    if (!reshandle)
        return FALSE;

    data = LockResource(reshandle);
    if (!data)
        return FALSE;

    size = SizeofResource(NULL, resinfo);
    if (!size)
        return FALSE;

    file = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    while (size)
    {
        if (!WriteFile(file, data, size, &written, NULL))
        {
            CloseHandle(file);
            return FALSE;
        }

        data += written;
        size -= written;
    }

    CloseHandle(file);
    return TRUE;
}

static void wait_driver(SC_HANDLE service, SERVICE_STATUS *status)
{
    for (;;)
    {
        Sleep(100);

        if (!QueryServiceStatus(service, status))
        {
            ok(0, "QueryServiceStatus failed with %x\n", GetLastError());
            status->dwCurrentState = SERVICE_STOPPED;
            return;
        }

        if (status->dwCurrentState != SERVICE_STOP_PENDING &&
            status->dwCurrentState != SERVICE_START_PENDING)
            return;
    }
}

/* unload a driver and delete the temporary file */
static void unload_driver(SC_HANDLE service, const char *filename)
{
    SERVICE_STATUS status;

    if (service)
    {
        /* Wine specific hack - when the test is the first application
         * in a wineprefix, then the driver will need some time to start up,
         * before we can stop them again. */
        wait_driver(service, &status);

        ControlService(service, SERVICE_CONTROL_STOP, &status);
        wait_driver(service, &status);
        ok(status.dwCurrentState == SERVICE_STOPPED,
           "expected SERVICE_STOPPED, got %d\n", status.dwCurrentState);

        DeleteService(service);
        CloseServiceHandle(service);
    }
    if (filename)
    {
        ok(DeleteFileA(filename),
           "DeleteFileA failed with %x\n", GetLastError());
    }
}

/* load a driver and return the service handle */
static SC_HANDLE load_driver(char *filename)
{
    SC_HANDLE manager, service;
    SERVICE_STATUS status;
    char temp[MAX_PATH];

    manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!manager)
        return FALSE;

    /* before we start with the actual tests, make sure to terminate
     * any old wine test drivers. */
    service = OpenServiceA(manager, driver_name, SERVICE_ALL_ACCESS);
    if (service) unload_driver(service, NULL);

    /* extract the new driver which is embedded into a resource */
    GetTempPathA(sizeof(temp), temp);
    GetTempFileNameA(temp, "drv", 0, filename);

    if (!extract_resource("driver.sys", filename))
    {
        ok(0, "Failed to extract driver resource\n");
        goto err;
    }

    trace("Trying to load driver %s\n", filename);

    /* load the new driver */
    service = CreateServiceA(manager, driver_name, driver_name,
                             SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
                             SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                             filename, NULL, NULL, NULL, NULL, NULL);
    if (!service)
    {
        ok(0, "CreateServiceA failed with %x\n", GetLastError());
        goto err;
    }

    ok(StartServiceA(service, 0, NULL),
       "StartServiceA failed with %x\n", GetLastError());

    CloseServiceHandle(manager);

    /* wait for the service to start up properly */
    wait_driver(service, &status);
    ok(status.dwCurrentState == SERVICE_RUNNING,
       "expected SERVICE_RUNNING, got %d\n", status.dwCurrentState);

    if (status.dwCurrentState != SERVICE_RUNNING)
    {
        unload_driver(service, filename);
        return NULL;
    }

    return service;

err:
    CloseServiceHandle(manager);
    unload_driver(NULL, filename);
    return NULL;
}

static void test_basic_ioctl(void)
{
    const char str[] = "Wine is not an emulator";
    DWORD bytes_returned;
    char buf[32];
    HANDLE file;
    BOOL res;

    file = CreateFileA(device_path, GENERIC_READ | GENERIC_WRITE,
                       0, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        ok(0, "Connecting to driver failed with %x\n", GetLastError());
        return;
    }

    res = DeviceIoControl(file, IOCTL_WINETEST_BASIC_IOCTL, NULL, 0, buf,
                          sizeof(buf), &bytes_returned, NULL);
    ok(res, "DeviceIoControl failed with %x\n", GetLastError());
    ok(bytes_returned == sizeof(str)-1, "Unexpected number of bytes\n");
    ok(!memcmp(buf, str, sizeof(str)-1), "Unexpected response data\n");

    CloseHandle(file);
}

START_TEST(ntoskrnl)
{
    char filename[MAX_PATH];
    SC_HANDLE service;

    if (!(service = load_driver(filename)))
    {
        win_skip("Failed to load driver, skipping tests.\n");
        return;
    }

    test_basic_ioctl();

    unload_driver(service, filename);
}
