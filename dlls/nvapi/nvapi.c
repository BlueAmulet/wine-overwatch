/*
 * Copyright (C) 2015 Michael Müller
 * Copyright (C) 2015 Sebastian Lackner
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

#include "config.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/debug.h"
#include "wine/list.h"
#include "nvapi.h"
#include "d3d9.h"

WINE_DEFAULT_DEBUG_CHANNEL(nvapi);

#define FAKE_PHYSICAL_GPU ((NvPhysicalGpuHandle)0xdead0001)
#define FAKE_DISPLAY ((NvDisplayHandle)0xdead0002)
#define FAKE_LOGICAL_GPU ((NvLogicalGpuHandle)0xdead0003)

#if defined(__i386__) || defined(__x86_64__)

static NvAPI_Status CDECL unimplemented_stub(unsigned int offset)
{
    FIXME("function 0x%x is unimplemented!\n", offset);
    return NVAPI_ERROR;
}

#ifdef __i386__

#include "pshpack1.h"
struct thunk
{
    unsigned char  push_ebp;
    unsigned short mov_esp_ebp;
    unsigned char  sub_0x08_esp[3];
    unsigned char  mov_dword_esp[3];
    unsigned int   offset;
    unsigned char  mov_eax;
    void           *stub;
    unsigned short call_eax;
    unsigned char  leave;
    unsigned char  ret;
};
#include "poppack.h"

static void* prepare_thunk(struct thunk *thunk, unsigned int offset)
{
    thunk->push_ebp         = 0x55;
    thunk->mov_esp_ebp      = 0xE589;
    thunk->sub_0x08_esp[0]  = 0x83;
    thunk->sub_0x08_esp[1]  = 0xEC;
    thunk->sub_0x08_esp[2]  = 0x08;
    thunk->mov_dword_esp[0] = 0xC7;
    thunk->mov_dword_esp[1] = 0x04;
    thunk->mov_dword_esp[2] = 0x24;
    thunk->offset           = offset;
    thunk->mov_eax          = 0xB8;
    thunk->stub             = &unimplemented_stub;
    thunk->call_eax         = 0xD0FF;
    thunk->leave            = 0xC9;
    thunk->ret              = 0xC3;
    return thunk;
}

#else

#include "pshpack1.h"
struct thunk
{
    unsigned short mov_rcx;
    unsigned int   offset;
    unsigned int   zero;
    unsigned short mov_rax;
    void           *stub;
    unsigned short jmp_rax;
};
#include "poppack.h"

static void* prepare_thunk(struct thunk *thunk, unsigned int offset)
{
    thunk->mov_rcx           = 0xB948;
    thunk->offset            = offset;
    thunk->zero              = 0;
    thunk->mov_rax           = 0xB848;
    thunk->stub              = &unimplemented_stub;
    thunk->jmp_rax           = 0xE0FF;
    return thunk;
}

#endif

struct thunk_entry
{
    struct list entry;
    int num_thunks;
    struct thunk thunks[0];
};

static struct list unimplemented_thunks = LIST_INIT( unimplemented_thunks );
static SYSTEM_BASIC_INFORMATION system_info;

static RTL_CRITICAL_SECTION unimplemented_thunk_section;
static RTL_CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &unimplemented_thunk_section,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": unimplemented_thunk_section") }
};
static RTL_CRITICAL_SECTION unimplemented_thunk_section = { &critsect_debug, -1, 0, 0, 0, 0 };

static void* lookup_thunk_function(unsigned int offset)
{
    struct list *ptr;
    unsigned int i;

    /* check for existing thunk */
    LIST_FOR_EACH( ptr, &unimplemented_thunks )
    {
        struct thunk_entry *entry = LIST_ENTRY( ptr, struct thunk_entry, entry );
        for (i = 0; i < entry->num_thunks; i++)
            if (entry->thunks[i].offset == offset)
                return &entry->thunks[i];
    }

    return NULL;
}

static void* allocate_thunk_function(unsigned int offset)
{
    struct thunk_entry *entry;
    struct list *ptr;

    /* append after last existing thunk if possible */
    if ((ptr = list_tail( &unimplemented_thunks )))
    {
        entry = LIST_ENTRY( ptr, struct thunk_entry, entry );
        if (FIELD_OFFSET( struct thunk_entry, thunks[entry->num_thunks + 1] ) <= system_info.PageSize)
            return prepare_thunk( &entry->thunks[entry->num_thunks++], offset );
    }

    /* allocate a new block */
    entry = VirtualAlloc( NULL, system_info.PageSize, MEM_COMMIT | MEM_RESERVE,
                          PAGE_EXECUTE_READWRITE );
    if (entry)
    {
        list_add_tail( &unimplemented_thunks, &entry->entry );
        entry->num_thunks = 1;
        return prepare_thunk( &entry->thunks[0], offset );
    }

    return NULL;
}

static void* get_thunk_function(unsigned int offset)
{
    void *ret;
    TRACE("(%x)\n", offset);

    EnterCriticalSection( &unimplemented_thunk_section );
    ret = lookup_thunk_function( offset );
    if (!ret) ret = allocate_thunk_function( offset );
    LeaveCriticalSection( &unimplemented_thunk_section );
    return ret;
}

static void init_thunks(void)
{
    NtQuerySystemInformation( SystemBasicInformation, &system_info, sizeof(system_info), NULL );
    /* we assume here that system_info.PageSize will always be great enough to hold at least one thunk */
}

static void free_thunks(void)
{
    struct list *ptr, *ptr2;
    EnterCriticalSection( &unimplemented_thunk_section );
    LIST_FOR_EACH_SAFE( ptr, ptr2, &unimplemented_thunks )
    {
        struct thunk_entry *entry = LIST_ENTRY( ptr, struct thunk_entry, entry );
        list_remove( ptr );
        VirtualFree( entry, 0, MEM_RELEASE );
    }
    LeaveCriticalSection( &unimplemented_thunk_section );
}

#else

static NvAPI_Status CDECL unimplemented_stub()
{
    FIXME("function is unimplemented!\n");
    return NVAPI_ERROR;
}

static void* get_thunk_function(unsigned int offset)
{
    TRACE("(%x)\n", offset);
    return &unimplemented_stub;
}

static void init_thunks(void)
{
    /* unimplemented */
}

static void free_thunks(void)
{
    /* unimplemented */
}

#endif


static NvAPI_Status CDECL NvAPI_Initialize(void)
{
    TRACE("()\n");
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Unknown1(NV_UNKNOWN_1 *param)
{
    TRACE("(%p)\n", param);

    if (!param)
        return NVAPI_INVALID_ARGUMENT;

    if (param->version != NV_UNKNOWN_1_VER)
        return NVAPI_INCOMPATIBLE_STRUCT_VERSION;

    param->gpu_count = 1;
    param->gpus[0].gpuHandle = FAKE_PHYSICAL_GPU;
    param->gpus[0].unknown2 = 11;

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Unknown2(NvPhysicalGpuHandle gpuHandle, NvPhysicalGpuHandle *retHandle)
{
    TRACE("(%p, %p)\n", gpuHandle, retHandle);

    if (!gpuHandle)
        return NVAPI_EXPECTED_PHYSICAL_GPU_HANDLE;

    if (!retHandle)
        return NVAPI_INVALID_ARGUMENT;

    if (gpuHandle == FAKE_PHYSICAL_GPU)
        *retHandle = (void *)gpuHandle;
    else
    {
        FIXME("invalid handle: %p\n", gpuHandle);
        *retHandle = (void*)0xffffffff;
    }

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Unknown3(NvPhysicalGpuHandle gpuHandle, NvPhysicalGpuHandle *retHandle)
{
    TRACE("(%p, %p)\n", gpuHandle, retHandle);

    if (!gpuHandle || !retHandle)
        return NVAPI_INVALID_ARGUMENT;

    if (gpuHandle == FAKE_PHYSICAL_GPU)
        *retHandle = (void *)gpuHandle;
    else
    {
        FIXME("invalid handle: %p\n", gpuHandle);
        *retHandle = (void *)0xffffffff;
    }

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_GetDisplayDriverVersion(NvDisplayHandle hNvDisplay, NV_DISPLAY_DRIVER_VERSION *pVersion)
{
    NvAPI_ShortString build_str = {'r','3','3','7','_','0','0','-','1','8','9',0};
    NvAPI_ShortString adapter = {'G','e','F','o','r','c','e',' ','9','9','9',' ','G','T','X', 0};
    /* TODO: find a good way to get the graphic card name, EnumDisplayDevices is useless in Wine */
    /* For now we return the non existing GeForce 999 GTX as graphic card name */

    TRACE("(%p, %p)\n", hNvDisplay, pVersion);

    if (hNvDisplay && hNvDisplay != FAKE_DISPLAY)
    {
        FIXME("invalid display handle: %p\n", hNvDisplay);
        return NVAPI_INVALID_HANDLE;
    }

    if (!pVersion)
        return NVAPI_INVALID_ARGUMENT;

    pVersion->drvVersion = 33788;
    pVersion->bldChangeListNum = 0;
    memcpy(pVersion->szBuildBranchString, build_str, sizeof(build_str));
    memcpy(pVersion->szAdapterString, adapter, sizeof(adapter));

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_GetAssociatedNvidiaDisplayHandle(const char *szDisplayName, NvDisplayHandle *pNvDispHandle)
{
    TRACE("(%s, %p)\n", szDisplayName, pNvDispHandle);

    *pNvDispHandle = FAKE_DISPLAY;
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_GetPhysicalGPUsFromDisplay(NvDisplayHandle hNvDisp, NvPhysicalGpuHandle nvGPUHandle[NVAPI_MAX_PHYSICAL_GPUS], NvU32 *pGpuCount)
{
    TRACE("(%p, %p, %p)\n", hNvDisp, nvGPUHandle, pGpuCount);

    nvGPUHandle[0] = FAKE_PHYSICAL_GPU;
    *pGpuCount = 1;

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_Disable(void)
{
    TRACE("()\n");
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_IsEnabled(NvU8 *pIsStereoEnabled)
{
    TRACE("(%p)\n", pIsStereoEnabled);

    *pIsStereoEnabled = 0;
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_CreateHandleFromIUnknown(void *pDevice, StereoHandle *pStereoHandle)
{
    TRACE("(%p, %p)\n", pDevice, pStereoHandle);
    return NVAPI_STEREO_NOT_INITIALIZED;
}

static NvAPI_Status CDECL NvAPI_Stereo_DestroyHandle(StereoHandle stereoHandle)
{
    TRACE("(%p)\n", stereoHandle);
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_Activate(StereoHandle stereoHandle)
{
    TRACE("(%p)\n", stereoHandle);
    return NVAPI_STEREO_NOT_INITIALIZED;
}

static NvAPI_Status CDECL NvAPI_Stereo_Deactivate(StereoHandle stereoHandle)
{
    TRACE("(%p)\n", stereoHandle);
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_IsActivated(StereoHandle stereoHandle, NvU8 *pIsStereoOn)
{
    TRACE("(%p, %p)\n", stereoHandle, pIsStereoOn);

    *pIsStereoOn = 0;
    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_Stereo_GetSeparation(StereoHandle stereoHandle, float *pSeparationPercentage)
{
    TRACE("(%p, %p)\n", stereoHandle, pSeparationPercentage);
    return NVAPI_STEREO_NOT_INITIALIZED;
}

static NvAPI_Status CDECL NvAPI_Stereo_SetSeparation(StereoHandle stereoHandle, float newSeparationPercentage)
{
    TRACE("(%p, %f)\n", stereoHandle, newSeparationPercentage);
    return NVAPI_STEREO_NOT_INITIALIZED;
}

static NvAPI_Status CDECL NvAPI_Stereo_Enable(void)
{
    TRACE("()\n");
    return NVAPI_STEREO_NOT_INITIALIZED;
}

static NvAPI_Status CDECL NvAPI_D3D9_StretchRectEx(IDirect3DDevice9 *pDevice, IDirect3DResource9 *pSourceResource,
                                                   const RECT *pSourceRect, IDirect3DResource9 *pDestResource,
                                                   const RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
    TRACE("(%p, %p, %p, %p, %p, %d)\n", pDevice, pSourceResource, pSourceRect, pDestResource, pDestRect, Filter);
    return NVAPI_UNREGISTERED_RESOURCE;
}

static NvAPI_Status CDECL NvAPI_EnumLogicalGPUs(NvLogicalGpuHandle gpuHandle[NVAPI_MAX_LOGICAL_GPUS], NvU32 *count)
{
    TRACE("(%p, %p)\n", gpuHandle, count);

    if (!gpuHandle)
        return NVAPI_INVALID_ARGUMENT;

    if (!count)
        return NVAPI_INVALID_POINTER;

    gpuHandle[0] = FAKE_LOGICAL_GPU;
    *count = 1;

    return NVAPI_OK;
}

static NvAPI_Status CDECL NvAPI_EnumLogicalGPUs_unknown(NvLogicalGpuHandle gpuHandle[NVAPI_MAX_LOGICAL_GPUS], NvU32 *count)
{
    TRACE("(%p, %p)\n", gpuHandle, count);
    return NvAPI_EnumLogicalGPUs(gpuHandle, count);
}

void* CDECL nvapi_QueryInterface(unsigned int offset)
{
    static const struct
    {
        unsigned int offset;
        void *function;
    }
    function_list[] =
    {
        {0x0150E828, NvAPI_Initialize},
        {0xF951A4D1, NvAPI_GetDisplayDriverVersion},
        {0x5786cc6e, NvAPI_Unknown1},
        {0x6533ea3e, NvAPI_Unknown2},
        {0x5380ad1a, NvAPI_Unknown3},
        {0x35c29134, NvAPI_GetAssociatedNvidiaDisplayHandle},
        {0x34ef9506, NvAPI_GetPhysicalGPUsFromDisplay},
        {0x2ec50c2b, NvAPI_Stereo_Disable},
        {0x348ff8e1, NvAPI_Stereo_IsEnabled},
        {0xac7e37f4, NvAPI_Stereo_CreateHandleFromIUnknown},
        {0x3a153134, NvAPI_Stereo_DestroyHandle},
        {0xf6a1ad68, NvAPI_Stereo_Activate},
        {0x2d68de96, NvAPI_Stereo_Deactivate},
        {0x1fb0bc30, NvAPI_Stereo_IsActivated},
        {0x451f2134, NvAPI_Stereo_GetSeparation},
        {0x5c069fa3, NvAPI_Stereo_SetSeparation},
        {0x239c4545, NvAPI_Stereo_Enable},
        {0xaeaecd41, NvAPI_D3D9_StretchRectEx},
        {0x48b3ea59, NvAPI_EnumLogicalGPUs},
        {0xfb9bc2ab, NvAPI_EnumLogicalGPUs_unknown}
    };
    unsigned int i;
    TRACE("(%x)\n", offset);

    for (i = 0; i < sizeof(function_list) / sizeof(function_list[0]); i++)
    {
        if (function_list[i].offset == offset)
            return function_list[i].function;
    }

    return get_thunk_function(offset);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    TRACE("(%p, %u, %p)\n", instance, reason, reserved);
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            init_thunks();
            break;
        case DLL_PROCESS_DETACH:
            free_thunks();
            break;
    }

    return TRUE;
}
