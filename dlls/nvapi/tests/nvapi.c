/*
 * Copyright (C) 2015 Michael Müller
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
#include "wingdi.h"
#include "winuser.h"
#include "shlwapi.h"
#include "winerror.h"
#include "nvapi.h"

#include "wine/test.h"

#define NvAPI_Initialize_Offset 0x0150E828
#define NvAPI_GetDisplayDriverVersion_Offset 0xF951A4D1
#define NvAPI_unknown1_Offset 0x5786cc6e
#define NvAPI_unknown2_Offset 0x6533ea3e
#define NvAPI_unknown3_Offset 0x5380ad1a

static void* (CDECL *pnvapi_QueryInterface)(unsigned int offset);
static NvAPI_Status (CDECL *pNvAPI_Initialize)(void);
static NvAPI_Status (CDECL *pNvAPI_GetDisplayDriverVersion)(NvDisplayHandle hNvDisplay, NV_DISPLAY_DRIVER_VERSION *pVersion);
static NvAPI_Status (CDECL *pNvAPI_unknown1)(void* param0);
static NvAPI_Status (CDECL *pNvAPI_unknown2)(NvPhysicalGpuHandle gpuHandle, void *param1);
static NvAPI_Status (CDECL *pNvAPI_unknown3)(void *param0, void *param1);

static BOOL init(void)
{
    #ifdef __x86_64__
    HMODULE nvapi = LoadLibraryA("nvapi64.dll");
    #else
    HMODULE nvapi = LoadLibraryA("nvapi.dll");
    #endif

    if (!nvapi)
    {
        skip("Could not load nvapi.dll\n");
        return FALSE;
    }

    pnvapi_QueryInterface = (void *)GetProcAddress(nvapi, "nvapi_QueryInterface");
    if (!pnvapi_QueryInterface)
    {
        win_skip("Failed to get entry point for nvapi_QueryInterface.\n");
        return FALSE;
    }

    pNvAPI_Initialize = pnvapi_QueryInterface(NvAPI_Initialize_Offset);
    pNvAPI_GetDisplayDriverVersion = pnvapi_QueryInterface(NvAPI_GetDisplayDriverVersion_Offset);
    pNvAPI_unknown1 = pnvapi_QueryInterface(NvAPI_unknown1_Offset);
    pNvAPI_unknown2 = pnvapi_QueryInterface(NvAPI_unknown2_Offset);
    pNvAPI_unknown3 = pnvapi_QueryInterface(NvAPI_unknown3_Offset);

    if (!pNvAPI_Initialize)
    {
        win_skip("Failed to get entry point for NvAPI_Initialize.\n");
        return FALSE;
    }

    if (pNvAPI_Initialize())
    {
        skip("Failed to initialize nvapi.\n");
        return FALSE;
    }

    return TRUE;
}

static void test_GetDisplayDriverVersion(void)
{
    NV_DISPLAY_DRIVER_VERSION version;
    NvAPI_Status status;
    DISPLAY_DEVICEA dinfo;
    char *gpuName;

    if (!pNvAPI_GetDisplayDriverVersion)
    {
        win_skip("NvAPI_GetDisplayDriverVersion export not found.\n");
        return;
    }

    status = pNvAPI_GetDisplayDriverVersion(0, NULL);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    memset(&version, 0, sizeof(version));
    version.version = NV_DISPLAY_DRIVER_VERSION_VER;

    status = pNvAPI_GetDisplayDriverVersion((void *)0xdeadbeef, &version);
    ok(status == NVAPI_INVALID_HANDLE, "Expected status NVAPI_INVALID_HANDLE, got %d\n", status);

    memset(&dinfo, 0, sizeof(dinfo));
    dinfo.cb = sizeof(dinfo);
    ok(EnumDisplayDevicesA(NULL, 0, &dinfo, 0), "Failed to get monitor info\n");

    trace("Device name: %s\n", (char*)&dinfo.DeviceName);
    trace("Device string: %s\n", (char*)&dinfo.DeviceString);

    gpuName = (char*)&dinfo.DeviceString;
    StrTrimA(gpuName, " \t\r\n");

    if (!strncmp(gpuName, "NVIDIA ", 7))
        gpuName += 7;

    memset(&version, 0, sizeof(version));
    version.version = NV_DISPLAY_DRIVER_VERSION_VER;

    status = pNvAPI_GetDisplayDriverVersion(0, &version);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);

    trace("Driver version: %u\n", (unsigned int)version.drvVersion);
    trace("Change list num: %u\n", (unsigned int)version.bldChangeListNum);
    trace("Build string: %s\n", version.szBuildBranchString);
    trace("Adapter string: %s\n", version.szAdapterString);

    todo_wine ok(!strcmp(version.szAdapterString, gpuName), "Expected device name %s, got %s\n",
                 gpuName, version.szAdapterString);
}

static void test_unknown1(void)
{
    NV_UNKNOWN_1 test;
    NvAPI_Status status;
    int i;

    if (!pNvAPI_unknown1)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    status = pNvAPI_unknown1(NULL);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    memset(&test, 0, sizeof(test));
    test.version = NV_UNKNOWN_1_VER;
    status = pNvAPI_unknown1(&test);

    ok(!status, "Expected status NVAPI_OK, got %d\n", status);
    ok(test.gpu_count > 0, "Expected gpu_count > 0, got %d\n", test.gpu_count);

    for (i = 0; i < test.gpu_count; i++)
    {
        ok(test.gpus[i].gpuHandle != NULL, "Expected gpus[%d].gpuHandle != 0, got %p\n", i, test.gpus[i].gpuHandle);
        ok(test.gpus[i].unknown2 != 0, "Expected gpus[%d].unknown2 != 0, got %d\n", i, test.gpus[i].unknown2);
    }

    for (; i < sizeof(test.gpus) / sizeof(test.gpus[0]); i++)
    {
        ok(test.gpus[i].gpuHandle == NULL, "Expected gpus[%d].gpuHandle == NULL, got %p\n", i, test.gpus[i].gpuHandle);
        ok(test.gpus[i].unknown2 == 0, "Expected gpus[%d].unknown2 == 0, got %d\n", i, test.gpus[i].unknown2);
    }
}

static void test_unknown2(void)
{
    NV_UNKNOWN_1 test;
    NvAPI_Status status;
    NvPhysicalGpuHandle test2 = NULL;

    if (!pNvAPI_unknown1)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    if (!pNvAPI_unknown2)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    status = pNvAPI_unknown2(NULL, NULL);
    ok(status == NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE, "Expected status NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE, got %d\n", status);

    memset(&test, 0, sizeof(test));
    test.version = NV_UNKNOWN_1_VER;
    status = pNvAPI_unknown1(&test);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);

    status = pNvAPI_unknown2(test.gpus[0].gpuHandle, NULL);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    status = pNvAPI_unknown2(NULL, &test2);
    ok(status == NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE, "Expected status NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE, got %d\n", status);

    test2 = NULL;
    status = pNvAPI_unknown2((void *)0xdeadbeef, &test2);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);
    ok(test2 == (void*)0xffffffff, "Expected handle 0xffffffff, got %p\n", test2);

    test2 = NULL;
    status = pNvAPI_unknown2(test.gpus[0].gpuHandle, &test2);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);
    ok(test2 == test.gpus[0].gpuHandle, "Expected handle %p, got %p\n", test.gpus[0].gpuHandle, test2);
}

static void test_unknown3(void)
{
    NV_UNKNOWN_1 test;
    NvAPI_Status status;
    NvPhysicalGpuHandle test2 = NULL;
    NvPhysicalGpuHandle test3 = NULL;

    if (!pNvAPI_unknown1)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    if (!pNvAPI_unknown2)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    if (!pNvAPI_unknown3)
    {
        win_skip("pNvAPI_unknown1 export not found.\n");
        return;
    }

    status = pNvAPI_unknown3(NULL, NULL);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    memset(&test, 0, sizeof(test));
    test.version = NV_UNKNOWN_1_VER;
    status = pNvAPI_unknown1(&test);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);

    status = pNvAPI_unknown2(test.gpus[0].gpuHandle, &test2);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);

    status = pNvAPI_unknown3(test2, NULL);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    status = pNvAPI_unknown3(NULL, &test3);
    ok(status == NVAPI_INVALID_ARGUMENT, "Expected status NVAPI_INVALID_ARGUMENT, got %d\n", status);

    test3 = NULL;
    status = pNvAPI_unknown3((void *)0xdeadbeef, &test3);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);
    ok(test3 == (void*)0xffffffff, "Expected handle 0xffffffff, got %p\n", test3);

    test3 = NULL;
    status = pNvAPI_unknown3(test2, &test3);
    ok(!status, "Expected status NVAPI_OK, got %d\n", status);
    ok(test2 == test3, "Expected handle %p, got %p\n", test2, test3);
}

START_TEST( nvapi )
{
    if (!init())
        return;

    test_GetDisplayDriverVersion();
    test_unknown1();
    test_unknown2();
    test_unknown3();
}
