/*
 * Unit tests for dxva2 functions
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

#include <string.h>
#include <stdio.h>
#include "wine/test.h"
#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#define COBJMACROS
#define INITGUID
#include "d3d9.h"
#include "dxva2api.h"
#include "dxva.h"

DEFINE_GUID(GUID_NULL,0,0,0,0,0,0,0,0,0,0,0);

struct decoder_name
{
    const GUID *guid;
    const char *name;
};

struct decoder_name decoder_names[] =
{
    /* H.264 */
    {&DXVA2_ModeH264_A, "H.264 MoComp"},
    {&DXVA2_ModeH264_B, "H.264 MoComp, FGT"},
    {&DXVA2_ModeH264_C, "H.264 IDCT"},
    {&DXVA2_ModeH264_D, "H.264 IDCT, FGT"},
    {&DXVA2_ModeH264_E, "H.264 VLD, no FGT"},
    {&DXVA2_ModeH264_F, "H.264 VLD, FGT"},

    /* MPEG 2 */
    {&DXVA2_ModeMPEG2_IDCT, "MPEG-2 IDCT"},
    {&DXVA2_ModeMPEG2_MoComp, "MPEG-2 MoComp"},
    {&DXVA2_ModeMPEG2_VLD, "MPEG-2 VLD"},

    /* VC-1 */
    {&DXVA2_ModeVC1_A, "VC-1 post processing"},
    {&DXVA2_ModeVC1_B, "VC-1 MoComp"},
    {&DXVA2_ModeVC1_C, "VC-1 IDCT"},
    {&DXVA2_ModeVC1_D, "VC-1 VLD"},

    /* WMV8 */
    {&DXVA2_ModeWMV8_A, "WMV 8 post processing"},
    {&DXVA2_ModeWMV8_B, "WMV 8 MoComp"},

    /* WMV9 */
    {&DXVA2_ModeWMV9_A, "WMV 9 post processing"},
    {&DXVA2_ModeWMV9_B, "WMV 9 MoComp"},
    {&DXVA2_ModeWMV9_C, "WMV 9 IDCT"},
};

struct decoder_test
{
    const GUID *guid;
    UINT width;
    UINT height;
    UINT surface_count;
    BOOL supported;
};

struct decoder_test decoder_tests[] =
{
    {&DXVA2_ModeMPEG2_VLD, 720, 576, 3, FALSE},
    {&DXVA2_ModeH264_E, 720, 576, 17, FALSE},
};

static const char* convert_decoderguid_to_str(const GUID *guid)
{
    unsigned int i;

    for (i = 0; i < sizeof(decoder_names) / sizeof(decoder_names[0]); i++)
    {
        if (IsEqualGUID(guid, decoder_names[i].guid))
            return decoder_names[i].name;
    }

    return wine_dbgstr_guid(guid);
}

HRESULT (WINAPI* pDXVA2CreateVideoService)(IDirect3DDevice9 *device, REFIID riid, void **ppv);

static BOOL init_dxva2(void)
{
    HMODULE dxva2 = LoadLibraryA("dxva2.dll");
    if (!dxva2)
    {
        win_skip("dxva2.dll not available\n");
        return FALSE;
    }

    pDXVA2CreateVideoService = (void *)GetProcAddress(dxva2, "DXVA2CreateVideoService");
    return TRUE;
}

static IDirect3DDevice9 *create_device(IDirect3D9 *d3d9, HWND focus_window)
{
    D3DPRESENT_PARAMETERS present_parameters = {0};
    IDirect3DDevice9 *device;
    DWORD behavior_flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;

    present_parameters.BackBufferWidth = 640;
    present_parameters.BackBufferHeight = 480;
    present_parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    present_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    present_parameters.hDeviceWindow = focus_window;
    present_parameters.Windowed = TRUE;
    present_parameters.EnableAutoDepthStencil = FALSE;

    if (SUCCEEDED(IDirect3D9_CreateDevice(d3d9, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, focus_window,
            behavior_flags, &present_parameters, &device)))
        return device;

    return NULL;
}

static BOOL create_video_service(HWND focus_window, REFIID riid, void **service)
{
    IDirect3DDevice9 *device;
    IDirect3D9 *d3d9;
    HRESULT hr;

    d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d9) return FALSE;

    device = create_device(d3d9, focus_window);
    IDirect3D9_Release(d3d9);
    if(!device)
        return FALSE;

    hr = pDXVA2CreateVideoService(device, riid, service);
    IDirect3DDevice9_Release(device);
    if (hr) return FALSE;

    return TRUE;
}

static inline void test_buffer(IDirectXVideoDecoder *decoder, UINT type, const char *name)
{
    void *buffer;
    UINT size;
    HRESULT hr;

    hr = IDirectXVideoDecoder_GetBuffer(decoder, type, &buffer, &size);
    ok(!hr, "Failed to get buffer %s: %x\n", name, hr);
    if (!hr) IDirectXVideoDecoder_ReleaseBuffer(decoder, type);
}

static void test_decoding(IDirectXVideoDecoder *decoder, REFGUID const guid, IDirect3DSurface9 **surfaces)
{
    HRESULT hr;

    hr = IDirectXVideoDecoder_BeginFrame(decoder, surfaces[0], NULL);
    ok(!hr, "Failed to begin frame for: %s\n", convert_decoderguid_to_str(guid));
    if (hr) return;

    test_buffer(decoder, DXVA2_PictureParametersBufferType, "DXVA2_PictureParametersBuffer");
    test_buffer(decoder, DXVA2_InverseQuantizationMatrixBufferType, "DXVA2_InverseQuantizationMatrixBuffer");
    test_buffer(decoder, DXVA2_SliceControlBufferType, "DXVA2_SliceControlBuffer");
    test_buffer(decoder, DXVA2_BitStreamDateBufferType, "DXVA2_BitStreamDateBuffer");

    IDirectXVideoDecoder_EndFrame(decoder, NULL);
}

static void test_decoder_resolution(IDirectXVideoDecoderService *service, REFGUID const guid, D3DFORMAT format,
                                    UINT width, UINT height, UINT surface_count)
{
    HRESULT hr;
    DXVA2_VideoDesc desc;
    UINT count;
    DXVA2_ConfigPictureDecode *configs;
    DXVA2_ConfigPictureDecode config;
    IDirect3DSurface9 **surfaces;
    IDirectXVideoDecoder *decoder;
    unsigned int i;

    trace("Analysing buffer sizes for: %s (%u x %u)\n", convert_decoderguid_to_str(guid), width, height);

    memset(&desc, 0, sizeof(desc));

    desc.SampleWidth = width;
    desc.SampleHeight = height;
    desc.Format = format;
    desc.InputSampleFreq.Numerator = 25;
    desc.InputSampleFreq.Denominator = 1;
    desc.OutputFrameFreq.Numerator = 25;
    desc.OutputFrameFreq.Denominator = 1;

    if (0) /* crashes on windows */
    {
        hr = IDirectXVideoDecoderService_GetDecoderConfigurations(service, guid, &desc, NULL, NULL, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);

        hr = IDirectXVideoDecoderService_GetDecoderConfigurations(service, guid, &desc, NULL, &count, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);

        hr = IDirectXVideoDecoderService_GetDecoderConfigurations(service, guid, &desc, NULL, NULL, &configs);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);
    }

    hr = IDirectXVideoDecoderService_GetDecoderConfigurations(service, &GUID_NULL, &desc, NULL, &count, &configs);
    ok(hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %x\n", hr);

    hr = IDirectXVideoDecoderService_GetDecoderConfigurations(service, guid, &desc, NULL, &count, &configs);
    ok(!hr, "Failed to get decoder configuration for: %s\n", convert_decoderguid_to_str(guid));
    if (hr) return;

    if (!count)
    {
        skip("Decoder for %s does not support a single decoder configuration? Skipping test\n",
             convert_decoderguid_to_str(guid));
        return;
    }

    memcpy(&config, &configs[0], sizeof(config));
    CoTaskMemFree(configs);

    surfaces = (void*)HeapAlloc(GetProcessHeap(), 0, surface_count * sizeof(IDirect3DSurface9));
    if (!surfaces)
    {
        skip("Failed to allocate memory for surfaces\n");
        return;
    }

    hr = IDirectXVideoDecoderService_CreateSurface(service, width, height, surface_count-1, format, D3DPOOL_DEFAULT, 0,
                                                        DXVA2_VideoDecoderRenderTarget, surfaces, NULL);
    ok(!hr, "Failed to create surfaces: %x\n", hr);
    if (hr)
    {
        HeapFree(GetProcessHeap(), 0, surfaces);
        return;
    }

    hr = IDirectXVideoDecoderService_CreateVideoDecoder(service, guid, &desc, &configs[0], surfaces, surface_count, &decoder);
    ok(!hr, "Failed to create decoder for %s: %x\n", convert_decoderguid_to_str(guid), hr);
    if (!hr)
    {
        test_decoding(decoder, guid, surfaces);
        IDirectXVideoDecoder_Release(decoder);
    }

    for (i = 0; i < surface_count; i++)
        IDirect3DSurface9_Release(surfaces[i]);

    HeapFree(GetProcessHeap(), 0, surfaces);
}

static void test_decoder(IDirectXVideoDecoderService *service, REFGUID const guid, UINT width, UINT height,
                         UINT surface_count)
{
    HRESULT hr;
    D3DFORMAT *formats;
    D3DFORMAT test_format;
    UINT count;

    if (0) /* crashes on windows */
    {
        hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(service, guid, NULL, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);

        hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(service, guid, &count, NULL);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);

        hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(service, guid, NULL, &formats);
        ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %x\n", hr);
    }

    hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(service, &GUID_NULL, &count, &formats);
    ok(hr == D3DERR_INVALIDCALL, "Expected D3DERR_INVALIDCALL, got %x\n", hr);

    hr = IDirectXVideoDecoderService_GetDecoderRenderTargets(service, guid, &count, &formats);
    ok(!hr, "Failed to get formats for codec %s\n", convert_decoderguid_to_str(guid));
    if (hr) return;

    if (!count)
    {
        skip("Decoder for %s does not support a single output format? Skipping test\n",
             convert_decoderguid_to_str(guid));
        return;
    }

    test_format = formats[0];
    CoTaskMemFree(formats);

    test_decoder_resolution(service, guid, test_format, width, height, surface_count);
}

static void test_decoder_service(HWND focus_window)
{
    IDirectXVideoDecoderService *service;
    unsigned int i, j;
    HRESULT hr;
    UINT count;
    GUID *guids;

    if (!create_video_service(focus_window, &IID_IDirectXVideoDecoderService, (void**)&service))
    {
        skip("Failed to create video decoder service, skipping decoder service tests\n");
        return;
    }

    hr = IDirectXVideoDecoderService_GetDecoderDeviceGuids(service, &count, &guids);
    if (hr)
    {
        skip("Failed to get decoder guids: %x\n", hr);
        return;
    }

    for (i = 0; i < count; i++)
    {
        trace("supported codec: %s\n", convert_decoderguid_to_str(&guids[i]));

        for (j = 0; j < sizeof(decoder_tests) / sizeof(decoder_tests[0]); j++)
        {
            if (IsEqualGUID(&guids[i], decoder_tests[j].guid))
                decoder_tests[j].supported = TRUE;
        }
    }

    CoTaskMemFree(guids);

    for (i = 0; i < sizeof(decoder_tests) / sizeof(decoder_tests[0]); i++)
    {
        if (!decoder_tests[i].supported)
        {
            skip("Decoder %s not supported, skipping test\n", convert_decoderguid_to_str(decoder_tests[i].guid));
            continue;
        }

        test_decoder(service, decoder_tests[i].guid, decoder_tests[i].width,
                     decoder_tests[i].height, decoder_tests[i].surface_count);
    }

    IDirectXVideoDecoderService_Release(service);
}

START_TEST(dxva2)
{
    HWND window;

    if (!init_dxva2())
        return;

    window = CreateWindowA("static", "dxva2_test", WS_OVERLAPPEDWINDOW,
                           0, 0, 640, 480, NULL, NULL, NULL, NULL);

    ok(window != NULL, "couldn't create window\n");
    if (!window) return;

    test_decoder_service(window);
}
