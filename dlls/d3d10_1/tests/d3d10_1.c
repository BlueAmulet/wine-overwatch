/*
 * Copyright 2008 Henri Verbeet for CodeWeavers
 * Copyright 2015 Józef Kucia for CodeWeavers
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

#define COBJMACROS
#include "initguid.h"
#include "d3d11.h"
#include "wine/test.h"

static const D3D10_FEATURE_LEVEL1 d3d10_feature_levels[] =
{
    D3D10_FEATURE_LEVEL_10_1,
    D3D10_FEATURE_LEVEL_10_0,
    D3D10_FEATURE_LEVEL_9_3,
    D3D10_FEATURE_LEVEL_9_2,
    D3D10_FEATURE_LEVEL_9_1
};

static ULONG get_refcount(IUnknown *iface)
{
    IUnknown_AddRef(iface);
    return IUnknown_Release(iface);
}

struct device_desc
{
    D3D10_FEATURE_LEVEL1 feature_level;
    UINT flags;
};

static ID3D10Device1 *create_device(const struct device_desc *desc)
{
    D3D10_FEATURE_LEVEL1 feature_level = D3D10_FEATURE_LEVEL_10_1;
    ID3D10Device1 *device;
    UINT flags = 0;

    if (desc)
    {
        feature_level = desc->feature_level;
        flags = desc->flags;
    }

    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE,
            NULL, flags, feature_level, D3D10_1_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_WARP,
            NULL, flags, feature_level, D3D10_1_SDK_VERSION, &device)))
        return device;
    if (SUCCEEDED(D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_REFERENCE,
            NULL, flags, feature_level, D3D10_1_SDK_VERSION, &device)))
        return device;

    return NULL;
}

static void test_create_device(void)
{
    D3D10_FEATURE_LEVEL1 feature_level, supported_feature_level;
    DXGI_SWAP_CHAIN_DESC swapchain_desc, obtained_desc;
    IDXGISwapChain *swapchain;
    ID3D10Device1 *device;
    unsigned int i;
    ULONG refcount;
    HWND window;
    HRESULT hr;

    for (i = 0; i < sizeof(d3d10_feature_levels) / sizeof(*d3d10_feature_levels); ++i)
    {
        if (SUCCEEDED(hr = D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                d3d10_feature_levels[i], D3D10_1_SDK_VERSION, &device)))
        {
            supported_feature_level = d3d10_feature_levels[i];
            break;
        }
    }

    if (FAILED(hr))
    {
        skip("Failed to create HAL device.\n");
        return;
    }

    feature_level = ID3D10Device1_GetFeatureLevel(device);
    ok(feature_level == supported_feature_level, "Got feature level %#x, expected %#x.\n",
            feature_level, supported_feature_level);

    ID3D10Device1_Release(device);

    hr = D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    device = (ID3D10Device1 *)0xdeadbeef;
    hr = D3D10CreateDevice1(NULL, 0xffffffff, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &device);
    todo_wine ok(hr == E_INVALIDARG, "D3D10CreateDevice1 returned %#x.\n", hr);
    ok(!device, "Got unexpected device pointer %p.\n", device);

    device = (ID3D10Device1 *)0xdeadbeef;
    hr = D3D10CreateDevice1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            0, D3D10_1_SDK_VERSION, &device);
    ok(hr == E_INVALIDARG, "D3D10CreateDevice1 returned %#x.\n", hr);
    ok(!device, "Got unexpected device pointer %p.\n", device);

    window = CreateWindowA("static", "d3d10_1_test", 0, 0, 0, 0, 0, 0, 0, 0, 0);

    swapchain_desc.BufferDesc.Width = 800;
    swapchain_desc.BufferDesc.Height = 600;
    swapchain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swapchain_desc.BufferDesc.RefreshRate.Denominator = 60;
    swapchain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapchain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapchain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;
    swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapchain_desc.BufferCount = 1;
    swapchain_desc.OutputWindow = window;
    swapchain_desc.Windowed = TRUE;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapchain_desc.Flags = 0;

    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, &swapchain, &device);
    ok(SUCCEEDED(hr), "D3D10CreateDeviceAndSwapChain1 failed %#x.\n", hr);

    memset(&obtained_desc, 0, sizeof(obtained_desc));
    hr = IDXGISwapChain_GetDesc(swapchain, &obtained_desc);
    ok(SUCCEEDED(hr), "GetDesc failed %#x.\n", hr);
    ok(obtained_desc.BufferDesc.Width == swapchain_desc.BufferDesc.Width,
            "Got unexpected BufferDesc.Width %u.\n", obtained_desc.BufferDesc.Width);
    ok(obtained_desc.BufferDesc.Height == swapchain_desc.BufferDesc.Height,
            "Got unexpected BufferDesc.Height %u.\n", obtained_desc.BufferDesc.Height);
    todo_wine ok(obtained_desc.BufferDesc.RefreshRate.Numerator == swapchain_desc.BufferDesc.RefreshRate.Numerator,
            "Got unexpected BufferDesc.RefreshRate.Numerator %u.\n",
            obtained_desc.BufferDesc.RefreshRate.Numerator);
    todo_wine ok(obtained_desc.BufferDesc.RefreshRate.Denominator == swapchain_desc.BufferDesc.RefreshRate.Denominator,
            "Got unexpected BufferDesc.RefreshRate.Denominator %u.\n",
            obtained_desc.BufferDesc.RefreshRate.Denominator);
    ok(obtained_desc.BufferDesc.Format == swapchain_desc.BufferDesc.Format,
            "Got unexpected BufferDesc.Format %#x.\n", obtained_desc.BufferDesc.Format);
    ok(obtained_desc.BufferDesc.ScanlineOrdering == swapchain_desc.BufferDesc.ScanlineOrdering,
            "Got unexpected BufferDesc.ScanlineOrdering %#x.\n", obtained_desc.BufferDesc.ScanlineOrdering);
    ok(obtained_desc.BufferDesc.Scaling == swapchain_desc.BufferDesc.Scaling,
            "Got unexpected BufferDesc.Scaling %#x.\n", obtained_desc.BufferDesc.Scaling);
    ok(obtained_desc.SampleDesc.Count == swapchain_desc.SampleDesc.Count,
            "Got unexpected SampleDesc.Count %u.\n", obtained_desc.SampleDesc.Count);
    ok(obtained_desc.SampleDesc.Quality == swapchain_desc.SampleDesc.Quality,
            "Got unexpected SampleDesc.Quality %u.\n", obtained_desc.SampleDesc.Quality);
    todo_wine ok(obtained_desc.BufferUsage == swapchain_desc.BufferUsage,
            "Got unexpected BufferUsage %#x.\n", obtained_desc.BufferUsage);
    ok(obtained_desc.BufferCount == swapchain_desc.BufferCount,
            "Got unexpected BufferCount %u.\n", obtained_desc.BufferCount);
    ok(obtained_desc.OutputWindow == swapchain_desc.OutputWindow,
            "Got unexpected OutputWindow %p.\n", obtained_desc.OutputWindow);
    ok(obtained_desc.Windowed == swapchain_desc.Windowed,
            "Got unexpected Windowed %#x.\n", obtained_desc.Windowed);
    ok(obtained_desc.SwapEffect == swapchain_desc.SwapEffect,
            "Got unexpected SwapEffect %#x.\n", obtained_desc.SwapEffect);
    ok(obtained_desc.Flags == swapchain_desc.Flags,
            "Got unexpected Flags %#x.\n", obtained_desc.Flags);

    refcount = IDXGISwapChain_Release(swapchain);
    ok(!refcount, "Swapchain has %u references left.\n", refcount);

    feature_level = ID3D10Device1_GetFeatureLevel(device);
    ok(feature_level == supported_feature_level, "Got feature level %#x, expected %#x.\n",
            feature_level, supported_feature_level);

    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, NULL, NULL, &device);
    ok(SUCCEEDED(hr), "D3D10CreateDeviceAndSwapChain1 failed %#x.\n", hr);
    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, NULL, NULL, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, NULL, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    swapchain = (IDXGISwapChain *)0xdeadbeef;
    device = (ID3D10Device1 *)0xdeadbeef;
    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            0, D3D10_1_SDK_VERSION, &swapchain_desc, &swapchain, &device);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(!swapchain, "Got unexpected swapchain pointer %p.\n", swapchain);
    ok(!device, "Got unexpected device pointer %p.\n", device);

    swapchain = (IDXGISwapChain *)0xdeadbeef;
    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, &swapchain, NULL);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);
    ok(!swapchain, "Got unexpected swapchain pointer %p.\n", swapchain);

    swapchain_desc.OutputWindow = NULL;
    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, NULL, &device);
    ok(SUCCEEDED(hr), "D3D10CreateDeviceAndSwapChain1 failed %#x.\n", hr);
    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);

    swapchain = (IDXGISwapChain *)0xdeadbeef;
    device = (ID3D10Device1 *)0xdeadbeef;
    swapchain_desc.OutputWindow = NULL;
    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, &swapchain, &device);
    todo_wine ok(hr == DXGI_ERROR_INVALID_CALL, "D3D10CreateDeviceAndSwapChain1 returned %#x.\n", hr);
    if (SUCCEEDED(hr))
    {
        refcount = IDXGISwapChain_Release(swapchain);
        ok(!refcount, "Swapchain has %u references left.\n", refcount);
        refcount = ID3D10Device1_Release(device);
        ok(!refcount, "Device has %u references left.\n", refcount);
    }
    else
    {
        ok(!swapchain, "Got unexpected swapchain pointer %p.\n", swapchain);
        ok(!device, "Got unexpected device pointer %p.\n", device);
    }

    swapchain = (IDXGISwapChain *)0xdeadbeef;
    device = (ID3D10Device1 *)0xdeadbeef;
    swapchain_desc.OutputWindow = window;
    swapchain_desc.BufferDesc.Format = DXGI_FORMAT_BC5_UNORM;
    hr = D3D10CreateDeviceAndSwapChain1(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
            supported_feature_level, D3D10_1_SDK_VERSION, &swapchain_desc, &swapchain, &device);
    ok(hr == E_INVALIDARG, "D3D10CreateDeviceAndSwapChain1 returned %#x.\n", hr);
    ok(!swapchain, "Got unexpected swapchain pointer %p.\n", swapchain);
    ok(!device, "Got unexpected device pointer %p.\n", device);

    DestroyWindow(window);
}

static void test_device_interfaces(void)
{
    IDXGIAdapter *dxgi_adapter;
    IDXGIDevice *dxgi_device;
    ID3D10Device1 *device;
    IUnknown *iface;
    ULONG refcount;
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < sizeof(d3d10_feature_levels) / sizeof(*d3d10_feature_levels); ++i)
    {
        struct device_desc device_desc;

        device_desc.feature_level = d3d10_feature_levels[i];
        device_desc.flags = 0;

        if (!(device = create_device(&device_desc)))
        {
            skip("Failed to create device for feature level %#x.\n", d3d10_feature_levels[i]);
            continue;
        }

        hr = ID3D10Device1_QueryInterface(device, &IID_IUnknown, (void **)&iface);
        ok(SUCCEEDED(hr), "Device should implement IUnknown interface, hr %#x.\n", hr);
        IUnknown_Release(iface);

        hr = ID3D10Device1_QueryInterface(device, &IID_IDXGIObject, (void **)&iface);
        ok(SUCCEEDED(hr), "Device should implement IDXGIObject interface, hr %#x.\n", hr);
        IUnknown_Release(iface);

        hr = ID3D10Device1_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
        ok(SUCCEEDED(hr), "Device should implement IDXGIDevice.\n");
        hr = IDXGIDevice_GetParent(dxgi_device, &IID_IDXGIAdapter, (void **)&dxgi_adapter);
        ok(SUCCEEDED(hr), "Device parent should implement IDXGIAdapter.\n");
        hr = IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory, (void **)&iface);
        ok(SUCCEEDED(hr), "Adapter parent should implement IDXGIFactory.\n");
        IUnknown_Release(iface);
        IDXGIAdapter_Release(dxgi_adapter);
        hr = IDXGIDevice_GetParent(dxgi_device, &IID_IDXGIAdapter1, (void **)&dxgi_adapter);
        ok(SUCCEEDED(hr), "Device parent should implement IDXGIAdapter1.\n");
        hr = IDXGIAdapter_GetParent(dxgi_adapter, &IID_IDXGIFactory1, (void **)&iface);
        ok(hr == E_NOINTERFACE, "Adapter parent should not implement IDXGIFactory1.\n");
        IDXGIAdapter_Release(dxgi_adapter);
        IDXGIDevice_Release(dxgi_device);

        hr = ID3D10Device1_QueryInterface(device, &IID_IDXGIDevice1, (void **)&iface);
        ok(SUCCEEDED(hr), "Device should implement IDXGIDevice1.\n");
        IUnknown_Release(iface);

        hr = ID3D10Device1_QueryInterface(device, &IID_ID3D10Multithread, (void **)&iface);
        ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
                "Device should implement ID3D10Multithread interface, hr %#x.\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(iface);

        hr = ID3D10Device1_QueryInterface(device, &IID_ID3D10InfoQueue, (void **)&iface);
        ok(hr == E_NOINTERFACE, "Found ID3D10InfoQueue interface in non-debug mode, hr %#x.\n", hr);

        hr = ID3D10Device1_QueryInterface(device, &IID_ID3D10Device, (void **)&iface);
        ok(SUCCEEDED(hr), "Device should implement ID3D10Device interface, hr %#x.\n", hr);
        IUnknown_Release(iface);

        hr = ID3D10Device1_QueryInterface(device, &IID_ID3D11Device, (void **)&iface);
        ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
                "Device should implement ID3D11Device interface, hr %#x.\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(iface);

        refcount = ID3D10Device1_Release(device);
        ok(!refcount, "Device has %u references left.\n", refcount);
    }

    for (i = 0; i < sizeof(d3d10_feature_levels) / sizeof(*d3d10_feature_levels); ++i)
    {
        struct device_desc device_desc;

        device_desc.feature_level = d3d10_feature_levels[i];
        device_desc.flags = D3D10_CREATE_DEVICE_DEBUG;
        if (!(device = create_device(&device_desc)))
        {
            skip("Failed to create device for feature level %#x.\n", d3d10_feature_levels[i]);
            continue;
        }

        hr = ID3D10Device1_QueryInterface(device, &IID_ID3D10InfoQueue, (void **)&iface);
        todo_wine ok(hr == S_OK, "Device should implement ID3D10InfoQueue interface, hr %#x.\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(iface);

        refcount = ID3D10Device1_Release(device);
        ok(!refcount, "Device has %u references left.\n", refcount);
    }
}

static void test_create_shader_resource_view(void)
{
    D3D10_SHADER_RESOURCE_VIEW_DESC1 srv_desc;
    D3D10_TEXTURE2D_DESC texture_desc;
    ULONG refcount, expected_refcount;
    ID3D10ShaderResourceView1 *srview;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Texture2D *texture;
    ID3D10Device *tmp_device;
    ID3D10Device1 *device;
    ID3D10Buffer *buffer;
    IUnknown *iface;
    HRESULT hr;

    if (!(device = create_device(NULL)))
    {
        skip("Failed to create device.\n");
        return;
    }

    buffer_desc.ByteWidth = 1024;
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    hr = ID3D10Device1_CreateBuffer(device, &buffer_desc, NULL, &buffer);
    ok(SUCCEEDED(hr), "Failed to create a buffer, hr %#x\n", hr);

    hr = ID3D10Device1_CreateShaderResourceView1(device, (ID3D10Resource *)buffer, NULL, &srview);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    srv_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    srv_desc.ViewDimension = D3D10_1_SRV_DIMENSION_BUFFER;
    U(srv_desc).Buffer.ElementOffset = 0;
    U(srv_desc).Buffer.ElementWidth = 64;

    hr = ID3D10Device1_CreateShaderResourceView1(device, NULL, &srv_desc, &srview);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device1_CreateShaderResourceView1(device, (ID3D10Resource *)buffer, &srv_desc, &srview);
    ok(SUCCEEDED(hr), "Failed to create a shader resource view, hr %#x\n", hr);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp_device = NULL;
    expected_refcount = refcount + 1;
    ID3D10ShaderResourceView1_GetDevice(srview, &tmp_device);
    ok(tmp_device == (ID3D10Device *)device, "Got unexpected device %p, expected %p.\n", tmp_device, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp_device);

    hr = ID3D10ShaderResourceView1_QueryInterface(srview, &IID_ID3D10ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr), "Shader resource view should implement ID3D10ShaderResourceView.\n");
    IUnknown_Release(iface);
    hr = ID3D10ShaderResourceView1_QueryInterface(srview, &IID_ID3D11ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D11ShaderResourceView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10ShaderResourceView1_Release(srview);
    ID3D10Buffer_Release(buffer);

    texture_desc.Width = 512;
    texture_desc.Height = 512;
    texture_desc.MipLevels = 0;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = 0;

    hr = ID3D10Device1_CreateTexture2D(device, &texture_desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create a 2d texture, hr %#x\n", hr);

    hr = ID3D10Device1_CreateShaderResourceView1(device, (ID3D10Resource *)texture, NULL, &srview);
    ok(SUCCEEDED(hr), "Failed to create a shader resource view, hr %#x\n", hr);

    ID3D10ShaderResourceView1_GetDesc1(srview, &srv_desc);
    ok(srv_desc.Format == texture_desc.Format, "Got unexpected format %#x.\n", srv_desc.Format);
    ok(srv_desc.ViewDimension == D3D10_1_SRV_DIMENSION_TEXTURE2D,
            "Got unexpected view dimension %#x.\n", srv_desc.ViewDimension);
    ok(U(srv_desc).Texture2D.MostDetailedMip == 0, "Got unexpected MostDetailedMip %u.\n",
            U(srv_desc).Texture2D.MostDetailedMip);
    ok(U(srv_desc).Texture2D.MipLevels == 10, "Got unexpected MipLevels %u.\n", U(srv_desc).Texture2D.MipLevels);

    hr = ID3D10ShaderResourceView1_QueryInterface(srview, &IID_ID3D10ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr), "Shader resource view should implement ID3D10ShaderResourceView.\n");
    IUnknown_Release(iface);
    hr = ID3D10ShaderResourceView1_QueryInterface(srview, &IID_ID3D11ShaderResourceView, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Shader resource view should implement ID3D11ShaderResourceView.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    ID3D10ShaderResourceView1_Release(srview);
    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_create_blend_state(void)
{
    static const D3D10_BLEND_DESC1 desc_conversion_tests[] =
    {
        {
            FALSE, FALSE,
            {
                {
                    FALSE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD
                },
            },
        },
        {
            FALSE, TRUE,
            {
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    FALSE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_RED
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    FALSE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_GREEN
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
            },
        },
        {
            FALSE, TRUE,
            {
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_SUBTRACT,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ZERO, D3D10_BLEND_ONE, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ZERO, D3D10_BLEND_ONE, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ONE, D3D10_BLEND_OP_MAX,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    TRUE, D3D10_BLEND_ONE, D3D10_BLEND_ONE, D3D10_BLEND_OP_MIN,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    FALSE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
                {
                    FALSE, D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD,
                    D3D10_BLEND_ONE, D3D10_BLEND_ZERO, D3D10_BLEND_OP_ADD, D3D10_COLOR_WRITE_ENABLE_ALL
                },
            },
        },
    };

    ID3D10BlendState1 *blend_state1, *blend_state2;
    D3D10_BLEND_DESC1 desc, obtained_desc;
    ID3D10BlendState *d3d10_blend_state;
    D3D10_BLEND_DESC d3d10_blend_desc;
    ULONG refcount, expected_refcount;
    ID3D10Device1 *device;
    ID3D10Device *tmp;
    unsigned int i, j;
    IUnknown *iface;
    HRESULT hr;

    if (!(device = create_device(NULL)))
    {
        skip("Failed to create device.\n");
        return;
    }

    hr = ID3D10Device1_CreateBlendState1(device, NULL, &blend_state1);
    ok(hr == E_INVALIDARG, "Got unexpected hr %#x.\n", hr);

    memset(&desc, 0, sizeof(desc));
    desc.AlphaToCoverageEnable = FALSE;
    desc.IndependentBlendEnable = FALSE;
    desc.RenderTarget[0].BlendEnable = FALSE;
    desc.RenderTarget[0].SrcBlend = D3D10_BLEND_ONE;
    desc.RenderTarget[0].DestBlend = D3D10_BLEND_ZERO;
    desc.RenderTarget[0].BlendOp = D3D10_BLEND_OP_ADD;
    desc.RenderTarget[0].SrcBlendAlpha = D3D10_BLEND_ONE;
    desc.RenderTarget[0].DestBlendAlpha = D3D10_BLEND_ZERO;
    desc.RenderTarget[0].BlendOpAlpha = D3D10_BLEND_OP_ADD;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D10_COLOR_WRITE_ENABLE_ALL;

    expected_refcount = get_refcount((IUnknown *)device) + 1;
    hr = ID3D10Device1_CreateBlendState1(device, &desc, &blend_state1);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);
    hr = ID3D10Device1_CreateBlendState1(device, &desc, &blend_state2);
    ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);
    ok(blend_state1 == blend_state2, "Got different blend state objects.\n");
    refcount = get_refcount((IUnknown *)device);
    ok(refcount >= expected_refcount, "Got unexpected refcount %u, expected >= %u.\n", refcount, expected_refcount);
    tmp = NULL;
    expected_refcount = refcount + 1;
    ID3D10BlendState1_GetDevice(blend_state1, &tmp);
    ok(tmp == (ID3D10Device *)device, "Got unexpected device %p, expected %p.\n", tmp, device);
    refcount = get_refcount((IUnknown *)device);
    ok(refcount == expected_refcount, "Got unexpected refcount %u, expected %u.\n", refcount, expected_refcount);
    ID3D10Device_Release(tmp);

    ID3D10BlendState1_GetDesc1(blend_state1, &obtained_desc);
    ok(obtained_desc.AlphaToCoverageEnable == FALSE, "Got unexpected alpha to coverage enable %#x.\n",
            obtained_desc.AlphaToCoverageEnable);
    ok(obtained_desc.IndependentBlendEnable == FALSE, "Got unexpected independent blend enable %#x.\n",
            obtained_desc.IndependentBlendEnable);
    for (i = 0; i < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
    {
        ok(obtained_desc.RenderTarget[i].BlendEnable == FALSE,
                "Got unexpected blend enable %#x for render target %u.\n",
                obtained_desc.RenderTarget[i].BlendEnable, i);
        ok(obtained_desc.RenderTarget[i].SrcBlend == D3D10_BLEND_ONE,
                "Got unexpected src blend %u for render target %u.\n",
                obtained_desc.RenderTarget[i].SrcBlend, i);
        ok(obtained_desc.RenderTarget[i].DestBlend == D3D10_BLEND_ZERO,
                "Got unexpected dest blend %u for render target %u.\n",
                obtained_desc.RenderTarget[i].DestBlend, i);
        ok(obtained_desc.RenderTarget[i].BlendOp == D3D10_BLEND_OP_ADD,
                "Got unexpected blend op %u for render target %u.\n",
                obtained_desc.RenderTarget[i].BlendOp, i);
        ok(obtained_desc.RenderTarget[i].SrcBlendAlpha == D3D10_BLEND_ONE,
                "Got unexpected src blend alpha %u for render target %u.\n",
                obtained_desc.RenderTarget[i].SrcBlendAlpha, i);
        ok(obtained_desc.RenderTarget[i].DestBlendAlpha == D3D10_BLEND_ZERO,
                "Got unexpected dest blend alpha %u for render target %u.\n",
                obtained_desc.RenderTarget[i].DestBlendAlpha, i);
        ok(obtained_desc.RenderTarget[i].BlendOpAlpha == D3D10_BLEND_OP_ADD,
                "Got unexpected blend op alpha %u for render target %u.\n",
                obtained_desc.RenderTarget[i].BlendOpAlpha, i);
        ok(obtained_desc.RenderTarget[i].RenderTargetWriteMask == D3D10_COLOR_WRITE_ENABLE_ALL,
                "Got unexpected render target write mask %#x for render target %u.\n",
                obtained_desc.RenderTarget[0].RenderTargetWriteMask, i);
    }

    hr = ID3D10BlendState1_QueryInterface(blend_state1, &IID_ID3D10BlendState, (void **)&iface);
    ok(SUCCEEDED(hr), "Blend state should implement ID3D10BlendState.\n");
    IUnknown_Release(iface);
    hr = ID3D10BlendState1_QueryInterface(blend_state1, &IID_ID3D11BlendState, (void **)&iface);
    ok(SUCCEEDED(hr) || broken(hr == E_NOINTERFACE) /* Not available on all Windows versions. */,
            "Blend state should implement ID3D11BlendState.\n");
    if (SUCCEEDED(hr)) IUnknown_Release(iface);

    refcount = ID3D10BlendState1_Release(blend_state1);
    ok(refcount == 1, "Got unexpected refcount %u.\n", refcount);
    refcount = ID3D10BlendState1_Release(blend_state2);
    ok(!refcount, "Blend state has %u references left.\n", refcount);

    for (i = 0; i < sizeof(desc_conversion_tests) / sizeof(*desc_conversion_tests); ++i)
    {
        const D3D10_BLEND_DESC1 *current_desc = &desc_conversion_tests[i];

        hr = ID3D10Device1_CreateBlendState1(device, current_desc, &blend_state1);
        ok(SUCCEEDED(hr), "Failed to create blend state, hr %#x.\n", hr);

        hr = ID3D10BlendState1_QueryInterface(blend_state1, &IID_ID3D10BlendState, (void **)&d3d10_blend_state);
        ok(SUCCEEDED(hr), "Blend state should implement ID3D10BlendState.\n");

        ID3D10BlendState_GetDesc(d3d10_blend_state, &d3d10_blend_desc);
        ok(d3d10_blend_desc.AlphaToCoverageEnable == current_desc->AlphaToCoverageEnable,
                "Got unexpected alpha to coverage enable %#x for test %u.\n",
                d3d10_blend_desc.AlphaToCoverageEnable, i);
        ok(d3d10_blend_desc.SrcBlend == current_desc->RenderTarget[0].SrcBlend,
                "Got unexpected src blend %u for test %u.\n", d3d10_blend_desc.SrcBlend, i);
        ok(d3d10_blend_desc.DestBlend == current_desc->RenderTarget[0].DestBlend,
                "Got unexpected dest blend %u for test %u.\n", d3d10_blend_desc.DestBlend, i);
        ok(d3d10_blend_desc.BlendOp == current_desc->RenderTarget[0].BlendOp,
                "Got unexpected blend op %u for test %u.\n", d3d10_blend_desc.BlendOp, i);
        ok(d3d10_blend_desc.SrcBlendAlpha == current_desc->RenderTarget[0].SrcBlendAlpha,
                "Got unexpected src blend alpha %u for test %u.\n", d3d10_blend_desc.SrcBlendAlpha, i);
        ok(d3d10_blend_desc.DestBlendAlpha == current_desc->RenderTarget[0].DestBlendAlpha,
                "Got unexpected dest blend alpha %u for test %u.\n", d3d10_blend_desc.DestBlendAlpha, i);
        ok(d3d10_blend_desc.BlendOpAlpha == current_desc->RenderTarget[0].BlendOpAlpha,
                "Got unexpected blend op alpha %u for test %u.\n", d3d10_blend_desc.BlendOpAlpha, i);
        for (j = 0; j < D3D10_SIMULTANEOUS_RENDER_TARGET_COUNT; j++)
        {
            unsigned int k = current_desc->IndependentBlendEnable ? j : 0;
            ok(d3d10_blend_desc.BlendEnable[j] == current_desc->RenderTarget[k].BlendEnable,
                    "Got unexpected blend enable %#x for test %u, render target %u.\n",
                    d3d10_blend_desc.BlendEnable[j], i, j);
            ok(d3d10_blend_desc.RenderTargetWriteMask[j] == current_desc->RenderTarget[k].RenderTargetWriteMask,
                    "Got unexpected render target write mask %#x for test %u, render target %u.\n",
                    d3d10_blend_desc.RenderTargetWriteMask[j], i, j);
        }

        ID3D10BlendState_Release(d3d10_blend_state);

        refcount = ID3D10BlendState1_Release(blend_state1);
        ok(!refcount, "Got unexpected refcount %u.\n", refcount);
    }

    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

static void test_getdc(void)
{
    struct device_desc device_desc;
    D3D10_TEXTURE2D_DESC desc;
    ID3D10Texture2D *texture;
    IDXGISurface1 *surface1;
    ID3D10Device1 *device;
    ULONG refcount;
    HRESULT hr;
    HDC dc;

    device_desc.feature_level = D3D10_FEATURE_LEVEL_10_1;
    device_desc.flags = D3D10_CREATE_DEVICE_BGRA_SUPPORT;
    if (!(device = create_device(&device_desc)))
    {
        skip("Failed to create device.\n");
        return;
    }

    /* Without D3D10_RESOURCE_MISC_GDI_COMPATIBLE. */
    desc.Width = 512;
    desc.Height = 512;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_RENDER_TARGET;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    hr = ID3D10Device1_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface1, (void**)&surface1);
    ok(SUCCEEDED(hr), "Failed to get IDXGISurface1 interface, hr %#x.\n", hr);

    hr = IDXGISurface1_GetDC(surface1, FALSE, &dc);
    todo_wine ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    IDXGISurface1_Release(surface1);
    ID3D10Texture2D_Release(texture);

    desc.MiscFlags = D3D10_RESOURCE_MISC_GDI_COMPATIBLE;
    hr = ID3D10Device1_CreateTexture2D(device, &desc, NULL, &texture);
    ok(SUCCEEDED(hr), "Failed to create texture, hr %#x.\n", hr);

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface1, (void**)&surface1);
    ok(SUCCEEDED(hr), "Failed to get IDXGISurface1 interface, hr %#x.\n", hr);

    hr = IDXGISurface1_ReleaseDC(surface1, NULL);
    todo_wine ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    hr = IDXGISurface1_GetDC(surface1, FALSE, &dc);
    ok(SUCCEEDED(hr), "Failed to get DC, hr %#x.\n", hr);

    /* One more time. */
    dc = (HDC)0xdeadbeef;
    hr = IDXGISurface1_GetDC(surface1, FALSE, &dc);
    todo_wine ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);
    ok(dc == (HDC)0xdeadbeef, "Got unexpected dc %p.\n", dc);

    hr = IDXGISurface1_ReleaseDC(surface1, NULL);
    ok(SUCCEEDED(hr), "Failed to release DC, hr %#x.\n", hr);

    hr = IDXGISurface1_ReleaseDC(surface1, NULL);
    todo_wine ok(hr == DXGI_ERROR_INVALID_CALL, "Got unexpected hr %#x.\n", hr);

    IDXGISurface1_Release(surface1);
    ID3D10Texture2D_Release(texture);

    refcount = ID3D10Device1_Release(device);
    ok(!refcount, "Device has %u references left.\n", refcount);
}

START_TEST(d3d10_1)
{
    test_create_device();
    test_device_interfaces();
    test_create_shader_resource_view();
    test_create_blend_state();
    test_getdc();
}
