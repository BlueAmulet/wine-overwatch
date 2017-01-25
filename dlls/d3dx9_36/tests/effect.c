/*
 * Copyright 2010 Christian Costa
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
#include <limits.h>
#include "wine/test.h"
#include "d3dx9.h"

#ifndef INFINITY
static inline float __port_infinity(void)
{
    static const unsigned __inf_bytes = 0x7f800000;
    return *(const float *)&__inf_bytes;
}
#define INFINITY __port_infinity()
#endif /* INFINITY */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*arr))

/* helper functions */
static BOOL compare_float(FLOAT f, FLOAT g, UINT ulps)
{
    INT x = *(INT *)&f;
    INT y = *(INT *)&g;

    if (x < 0)
        x = INT_MIN - x;
    if (y < 0)
        y = INT_MIN - y;

    if (abs(x - y) > ulps)
        return FALSE;

    return TRUE;
}

static inline INT get_int(D3DXPARAMETER_TYPE type, const void *data)
{
    INT i;

    switch (type)
    {
        case D3DXPT_FLOAT:
            i = *(FLOAT *)data;
            break;

        case D3DXPT_INT:
            i = *(INT *)data;
            break;

        case D3DXPT_BOOL:
            i = *(BOOL *)data;
            break;

        default:
            i = 0;
            ok(0, "Unhandled type %x.\n", type);
            break;
    }

    return i;
}

static inline float get_float(D3DXPARAMETER_TYPE type, const void *data)
{
    float f;

    switch (type)
    {
        case D3DXPT_FLOAT:
            f = *(FLOAT *)data;
            break;

        case D3DXPT_INT:
            f = *(INT *)data;
            break;

        case D3DXPT_BOOL:
            f = *(BOOL *)data;
            break;

        default:
            f = 0.0f;
            ok(0, "Unhandled type %x.\n", type);
            break;
    }

    return f;
}

static inline BOOL get_bool(const void *data)
{
    return !!*(BOOL *)data;
}

static void set_number(void *outdata, D3DXPARAMETER_TYPE outtype, const void *indata, D3DXPARAMETER_TYPE intype)
{
    switch (outtype)
    {
        case D3DXPT_FLOAT:
            *(FLOAT *)outdata = get_float(intype, indata);
            break;

        case D3DXPT_BOOL:
            *(BOOL *)outdata = get_bool(indata);
            break;

        case D3DXPT_INT:
            *(INT *)outdata = get_int(intype, indata);
            break;

        case D3DXPT_PIXELSHADER:
        case D3DXPT_VERTEXSHADER:
        case D3DXPT_TEXTURE2D:
        case D3DXPT_STRING:
            *(INT *)outdata = 0x12345678;
            break;

        default:
            ok(0, "Unhandled type %x.\n", outtype);
            *(INT *)outdata = 0;
            break;
    }
}

static const char effect_desc[] =
"Technique\n"
"{\n"
"}\n";

static void test_create_effect_and_pool(IDirect3DDevice9 *device)
{
    HRESULT hr;
    ID3DXEffect *effect;
    ID3DXBaseEffect *base;
    ULONG count;
    IDirect3DDevice9 *device2;
    ID3DXEffectStateManager *manager = (ID3DXEffectStateManager *)0xdeadbeef;
    ID3DXEffectPool *pool = (ID3DXEffectPool *)0xdeadbeef, *pool2;

    hr = D3DXCreateEffect(NULL, effect_desc, sizeof(effect_desc), NULL, NULL, 0, NULL, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffect(device, NULL, 0, NULL, NULL, 0, NULL, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3DERR_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffect(device, effect_desc, 0, NULL, NULL, 0, NULL, NULL, NULL);
    ok(hr == E_FAIL, "Got result %x, expected %x (D3DXERR_INVALIDDATA)\n", hr, E_FAIL);

    hr = D3DXCreateEffect(device, effect_desc, sizeof(effect_desc), NULL, NULL, 0, NULL, NULL, NULL);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);

    hr = D3DXCreateEffect(device, effect_desc, sizeof(effect_desc), NULL, NULL, 0, NULL, &effect, NULL);
    todo_wine ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);
    if (FAILED(hr))
    {
        skip("Failed to compile effect, skipping test.\n");
        return;
    }

    hr = effect->lpVtbl->QueryInterface(effect, &IID_ID3DXBaseEffect, (void **)&base);
    ok(hr == E_NOINTERFACE, "QueryInterface failed, got %x, expected %x (E_NOINTERFACE)\n", hr, E_NOINTERFACE);

    hr = effect->lpVtbl->GetStateManager(effect, NULL);
    ok(hr == D3DERR_INVALIDCALL, "GetStateManager failed, got %x, expected %x (D3DERR_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = effect->lpVtbl->GetStateManager(effect, &manager);
    ok(hr == D3D_OK, "GetStateManager failed, got %x, expected 0 (D3D_OK)\n", hr);
    ok(!manager, "GetStateManager failed, got %p\n", manager);

    /* this works, but it is not recommended! */
    hr = effect->lpVtbl->SetStateManager(effect, (ID3DXEffectStateManager *)device);
    ok(hr == D3D_OK, "SetStateManager failed, got %x, expected 0 (D3D_OK)\n", hr);

    hr = effect->lpVtbl->GetStateManager(effect, &manager);
    ok(hr == D3D_OK, "GetStateManager failed, got %x, expected 0 (D3D_OK)\n", hr);
    ok(manager != NULL, "GetStateManager failed\n");

    IDirect3DDevice9_AddRef(device);
    count = IDirect3DDevice9_Release(device);
    ok(count == 4, "Release failed, got %u, expected 4\n", count);

    count = IUnknown_Release(manager);
    ok(count == 3, "Release failed, got %u, expected 3\n", count);

    hr = effect->lpVtbl->SetStateManager(effect, NULL);
    ok(hr == D3D_OK, "SetStateManager failed, got %x, expected 0 (D3D_OK)\n", hr);

    hr = effect->lpVtbl->GetPool(effect, &pool);
    ok(hr == D3D_OK, "GetPool failed, got %x, expected 0 (D3D_OK)\n", hr);
    ok(!pool, "GetPool failed, got %p\n", pool);

    hr = effect->lpVtbl->GetPool(effect, NULL);
    ok(hr == D3DERR_INVALIDCALL, "GetPool failed, got %x, expected %x (D3DERR_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = effect->lpVtbl->GetDevice(effect, &device2);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);

    hr = effect->lpVtbl->GetDevice(effect, NULL);
    ok(hr == D3DERR_INVALIDCALL, "GetDevice failed, got %x, expected %x (D3DERR_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    count = IDirect3DDevice9_Release(device2);
    ok(count == 2, "Release failed, got %u, expected 2\n", count);

    count = effect->lpVtbl->Release(effect);
    ok(count == 0, "Release failed %u\n", count);

    hr = D3DXCreateEffectPool(NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectPool(&pool);
    ok(hr == S_OK, "Got result %x, expected 0 (S_OK)\n", hr);

    count = pool->lpVtbl->Release(pool);
    ok(count == 0, "Release failed %u\n", count);

    hr = D3DXCreateEffectPool(&pool);
    ok(hr == S_OK, "Got result %x, expected 0 (S_OK)\n", hr);

    hr = D3DXCreateEffect(device, effect_desc, sizeof(effect_desc), NULL, NULL, 0, pool, NULL, NULL);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);

    hr = pool->lpVtbl->QueryInterface(pool, &IID_ID3DXEffectPool, (void **)&pool2);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);
    ok(pool == pool2, "Release failed, got %p, expected %p\n", pool2, pool);

    count = pool2->lpVtbl->Release(pool2);
    ok(count == 1, "Release failed, got %u, expected 1\n", count);

    hr = IDirect3DDevice9_QueryInterface(device, &IID_IDirect3DDevice9, (void **)&device2);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);

    count = IDirect3DDevice9_Release(device2);
    ok(count == 1, "Release failed, got %u, expected 1\n", count);

    hr = D3DXCreateEffect(device, effect_desc, sizeof(effect_desc), NULL, NULL, 0, pool, &effect, NULL);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK)\n", hr);

    hr = effect->lpVtbl->GetPool(effect, &pool);
    ok(hr == D3D_OK, "GetPool failed, got %x, expected 0 (D3D_OK)\n", hr);
    ok(pool == pool2, "GetPool failed, got %p, expected %p\n", pool2, pool);

    count = pool2->lpVtbl->Release(pool2);
    ok(count == 2, "Release failed, got %u, expected 2\n", count);

    count = effect->lpVtbl->Release(effect);
    ok(count == 0, "Release failed %u\n", count);

    count = pool->lpVtbl->Release(pool);
    ok(count == 0, "Release failed %u\n", count);
}

static void test_create_effect_compiler(void)
{
    HRESULT hr;
    ID3DXEffectCompiler *compiler, *compiler2;
    ID3DXBaseEffect *base;
    IUnknown *unknown;
    ULONG count;

    hr = D3DXCreateEffectCompiler(NULL, 0, NULL, NULL, 0, &compiler, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(NULL, 0, NULL, NULL, 0, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(effect_desc, 0, NULL, NULL, 0, &compiler, NULL);
    todo_wine ok(hr == D3D_OK, "Got result %x, expected %x (D3D_OK)\n", hr, D3D_OK);
    if (FAILED(hr))
    {
        skip("D3DXCreateEffectCompiler failed, skipping test.\n");
        return;
    }

    count = compiler->lpVtbl->Release(compiler);
    ok(count == 0, "Release failed %u\n", count);

    hr = D3DXCreateEffectCompiler(effect_desc, 0, NULL, NULL, 0, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(NULL, sizeof(effect_desc), NULL, NULL, 0, &compiler, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(NULL, sizeof(effect_desc), NULL, NULL, 0, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3D_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(effect_desc, sizeof(effect_desc), NULL, NULL, 0, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "Got result %x, expected %x (D3DERR_INVALIDCALL)\n", hr, D3DERR_INVALIDCALL);

    hr = D3DXCreateEffectCompiler(effect_desc, sizeof(effect_desc), NULL, NULL, 0, &compiler, NULL);
    ok(hr == D3D_OK, "Got result %x, expected %x (D3D_OK)\n", hr, D3D_OK);

    hr = compiler->lpVtbl->QueryInterface(compiler, &IID_ID3DXBaseEffect, (void **)&base);
    ok(hr == E_NOINTERFACE, "QueryInterface failed, got %x, expected %x (E_NOINTERFACE)\n", hr, E_NOINTERFACE);

    hr = compiler->lpVtbl->QueryInterface(compiler, &IID_ID3DXEffectCompiler, (void **)&compiler2);
    ok(hr == D3D_OK, "QueryInterface failed, got %x, expected %x (D3D_OK)\n", hr, D3D_OK);

    hr = compiler->lpVtbl->QueryInterface(compiler, &IID_IUnknown, (void **)&unknown);
    ok(hr == D3D_OK, "QueryInterface failed, got %x, expected %x (D3D_OK)\n", hr, D3D_OK);

    count = unknown->lpVtbl->Release(unknown);
    ok(count == 2, "Release failed, got %u, expected %u\n", count, 2);

    count = compiler2->lpVtbl->Release(compiler2);
    ok(count == 1, "Release failed, got %u, expected %u\n", count, 1);

    count = compiler->lpVtbl->Release(compiler);
    ok(count == 0, "Release failed %u\n", count);
}

/*
 * Parameter value test
 */
struct test_effect_parameter_value_result
{
    const char *full_name;
    D3DXPARAMETER_DESC desc;
    UINT value_offset; /* start position for the value in the blob */
};

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
float f = 0.1;
float1 f1 = {1.1};
float2 f2 = {2.1, 2.2};
float3 f3 = {3.1, 3.2, 3.3};
float4 f4 = {4.1, 4.2, 4.3, 4.4};
float1x1 f11 = {11.1};
float1x2 f12 = {12.1, 12.2};
float1x3 f13 = {13.1, 13.2, 13.3};
float1x4 f14 = {14.1, 14.2, 14.3, 14.4};
float2x1 f21 = {{21.11, 21.21}};
float2x2 f22 = {{22.11, 22.21}, {22.12, 22.22}};
float2x3 f23 = {{23.11, 23.21}, {23.12, 23.22}, {23.13, 23.23}};
float2x4 f24 = {{24.11, 24.21}, {24.12, 24.22}, {24.13, 24.23}, {24.14, 24.24}};
float3x1 f31 = {{31.11, 31.21, 31.31}};
float3x2 f32 = {{32.11, 32.21, 32.31}, {32.12, 32.22, 32.32}};
float3x3 f33 = {{33.11, 33.21, 33.31}, {33.12, 33.22, 33.32},
        {33.13, 33.23, 33.33}};
float3x4 f34 = {{34.11, 34.21, 34.31}, {34.12, 34.22, 34.32},
        {34.13, 34.23, 34.33}, {34.14, 34.24, 34.34}};
float4x1 f41 = {{41.11, 41.21, 41.31, 41.41}};
float4x2 f42 = {{42.11, 42.21, 42.31, 42.41}, {42.12, 42.22, 42.32, 42.42}};
float4x3 f43 = {{43.11, 43.21, 43.31, 43.41}, {43.12, 43.22, 43.32, 43.42},
        {43.13, 43.23, 43.33, 43.43}};
float4x4 f44 = {{44.11, 44.21, 44.31, 44.41}, {44.12, 44.22, 44.32, 44.42},
        {44.13, 44.23, 44.33, 44.43}, {44.14, 44.24, 44.34, 44.44}};
float f_2[2] = {0.101, 0.102};
float1 f1_2[2] = {{1.101}, {1.102}};
float2 f2_2[2] = {{2.101, 2.201}, {2.102, 2.202}};
float3 f3_2[2] = {{3.101, 3.201, 3.301}, {3.102, 3.202, 3.302}};
float4 f4_2[2] = {{4.101, 4.201, 4.301, 4.401}, {4.102, 4.202, 4.302, 4.402}};
float1x1 f11_2[2] = {{11.101}, {11.102}};
float1x2 f12_2[2] = {{12.101, 12.201}, {12.102, 12.202}};
float1x3 f13_2[2] = {{13.101, 13.201, 13.301}, {13.102, 13.202, 13.302}};
float1x4 f14_2[2] = {{14.101, 14.201, 14.301, 14.401}, {14.102, 14.202, 14.302, 14.402}};
float2x1 f21_2[2] = {{{21.1101, 21.2101}}, {{21.1102, 21.2102}}};
float2x2 f22_2[2] = {{{22.1101, 22.2101}, {22.1201, 22.2201}}, {{22.1102, 22.2102}, {22.1202, 22.2202}}};
float2x3 f23_2[2] = {{{23.1101, 23.2101}, {23.1201, 23.2201}, {23.1301, 23.2301}}, {{23.1102, 23.2102},
        {23.1202, 23.2202}, {23.1302, 23.2302}}};
float2x4 f24_2[2] = {{{24.1101, 24.2101}, {24.1201, 24.2201}, {24.1301, 24.2301}, {24.1401, 24.2401}},
        {{24.1102, 24.2102}, {24.1202, 24.2202}, {24.1302, 24.2302}, {24.1402, 24.2402}}};
float3x1 f31_2[2] = {{{31.1101, 31.2101, 31.3101}}, {{31.1102, 31.2102, 31.3102}}};
float3x2 f32_2[2] = {{{32.1101, 32.2101, 32.3101}, {32.1201, 32.2201, 32.3201}},
        {{32.1102, 32.2102, 32.3102}, {32.1202, 32.2202, 32.3202}}};
float3x3 f33_2[2] = {{{33.1101, 33.2101, 33.3101}, {33.1201, 33.2201, 33.3201},
        {33.1301, 33.2301, 33.3301}}, {{33.1102, 33.2102, 33.3102}, {33.1202, 33.2202, 33.3202},
        {33.1302, 33.2302, 33.3302}}};
float3x4 f34_2[2] = {{{34.1101, 34.2101, 34.3101}, {34.1201, 34.2201, 34.3201},
        {34.1301, 34.2301, 34.3301}, {34.1401, 34.2401, 34.3401}}, {{34.1102, 34.2102, 34.3102},
        {34.1202, 34.2202, 34.3202}, {34.1302, 34.2302, 34.3302}, {34.1402, 34.2402, 34.3402}}};
float4x1 f41_2[2] = {{{41.1101, 41.2101, 41.3101, 41.4101}}, {{41.1102, 41.2102, 41.3102, 41.4102}}};
float4x2 f42_2[2] = {{{42.1101, 42.2101, 42.3101, 42.4101}, {42.1201, 42.2201, 42.3201, 42.4201}},
        {{42.1102, 42.2102, 42.3102, 42.4102}, {42.1202, 42.2202, 42.3202, 42.4202}}};
float4x3 f43_2[2] = {{{43.1101, 43.2101, 43.3101, 43.4101}, {43.1201, 43.2201, 43.3201, 43.4201},
        {43.1301, 43.2301, 43.3301, 43.4301}}, {{43.1102, 43.2102, 43.3102, 43.4102},
        {43.1202, 43.2202, 43.3202, 43.4202}, {43.1302, 43.2302, 43.3302, 43.4302}}};
float4x4 f44_2[2] = {{{44.1101, 44.2101, 44.3101, 44.4101}, {44.1201, 44.2201, 44.3201, 44.4201},
        {44.1301, 44.2301, 44.3301, 44.4301}, {44.1401, 44.2401, 44.3401, 44.4401}},
        {{44.1102, 44.2102, 44.3102, 44.4102}, {44.1202, 44.2202, 44.3202, 44.4202},
        {44.1302, 44.2302, 44.3302, 44.4302}, {44.1402, 44.2402, 44.3402, 44.4402}}};
technique t { pass p { } }
#endif
static const DWORD test_effect_parameter_value_blob_float[] =
{
0xfeff0901, 0x00000b80, 0x00000000, 0x00000003, 0x00000000, 0x00000024, 0x00000000, 0x00000000,
0x00000001, 0x00000001, 0x3dcccccd, 0x00000002, 0x00000066, 0x00000003, 0x00000001, 0x0000004c,
0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x3f8ccccd, 0x00000003, 0x00003166, 0x00000003,
0x00000001, 0x00000078, 0x00000000, 0x00000000, 0x00000002, 0x00000001, 0x40066666, 0x400ccccd,
0x00000003, 0x00003266, 0x00000003, 0x00000001, 0x000000a8, 0x00000000, 0x00000000, 0x00000003,
0x00000001, 0x40466666, 0x404ccccd, 0x40533333, 0x00000003, 0x00003366, 0x00000003, 0x00000001,
0x000000dc, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x40833333, 0x40866666, 0x4089999a,
0x408ccccd, 0x00000003, 0x00003466, 0x00000003, 0x00000002, 0x00000104, 0x00000000, 0x00000000,
0x00000001, 0x00000001, 0x4131999a, 0x00000004, 0x00313166, 0x00000003, 0x00000002, 0x00000130,
0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x4141999a, 0x41433333, 0x00000004, 0x00323166,
0x00000003, 0x00000002, 0x00000160, 0x00000000, 0x00000000, 0x00000001, 0x00000003, 0x4151999a,
0x41533333, 0x4154cccd, 0x00000004, 0x00333166, 0x00000003, 0x00000002, 0x00000194, 0x00000000,
0x00000000, 0x00000001, 0x00000004, 0x4161999a, 0x41633333, 0x4164cccd, 0x41666666, 0x00000004,
0x00343166, 0x00000003, 0x00000002, 0x000001c0, 0x00000000, 0x00000000, 0x00000002, 0x00000001,
0x41a8e148, 0x41a9ae14, 0x00000004, 0x00313266, 0x00000003, 0x00000002, 0x000001f4, 0x00000000,
0x00000000, 0x00000002, 0x00000002, 0x41b0e148, 0x41b1ae14, 0x41b0f5c3, 0x41b1c28f, 0x00000004,
0x00323266, 0x00000003, 0x00000002, 0x00000230, 0x00000000, 0x00000000, 0x00000002, 0x00000003,
0x41b8e148, 0x41b9ae14, 0x41b8f5c3, 0x41b9c28f, 0x41b90a3d, 0x41b9d70a, 0x00000004, 0x00333266,
0x00000003, 0x00000002, 0x00000274, 0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x41c0e148,
0x41c1ae14, 0x41c0f5c3, 0x41c1c28f, 0x41c10a3d, 0x41c1d70a, 0x41c11eb8, 0x41c1eb85, 0x00000004,
0x00343266, 0x00000003, 0x00000002, 0x000002a4, 0x00000000, 0x00000000, 0x00000003, 0x00000001,
0x41f8e148, 0x41f9ae14, 0x41fa7ae1, 0x00000004, 0x00313366, 0x00000003, 0x00000002, 0x000002e0,
0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x420070a4, 0x4200d70a, 0x42013d71, 0x42007ae1,
0x4200e148, 0x420147ae, 0x00000004, 0x00323366, 0x00000003, 0x00000002, 0x00000328, 0x00000000,
0x00000000, 0x00000003, 0x00000003, 0x420470a4, 0x4204d70a, 0x42053d71, 0x42047ae1, 0x4204e148,
0x420547ae, 0x4204851f, 0x4204eb85, 0x420551ec, 0x00000004, 0x00333366, 0x00000003, 0x00000002,
0x0000037c, 0x00000000, 0x00000000, 0x00000003, 0x00000004, 0x420870a4, 0x4208d70a, 0x42093d71,
0x42087ae1, 0x4208e148, 0x420947ae, 0x4208851f, 0x4208eb85, 0x420951ec, 0x42088f5c, 0x4208f5c3,
0x42095c29, 0x00000004, 0x00343366, 0x00000003, 0x00000002, 0x000003b0, 0x00000000, 0x00000000,
0x00000004, 0x00000001, 0x422470a4, 0x4224d70a, 0x42253d71, 0x4225a3d7, 0x00000004, 0x00313466,
0x00000003, 0x00000002, 0x000003f4, 0x00000000, 0x00000000, 0x00000004, 0x00000002, 0x422870a4,
0x4228d70a, 0x42293d71, 0x4229a3d7, 0x42287ae1, 0x4228e148, 0x422947ae, 0x4229ae14, 0x00000004,
0x00323466, 0x00000003, 0x00000002, 0x00000448, 0x00000000, 0x00000000, 0x00000004, 0x00000003,
0x422c70a4, 0x422cd70a, 0x422d3d71, 0x422da3d7, 0x422c7ae1, 0x422ce148, 0x422d47ae, 0x422dae14,
0x422c851f, 0x422ceb85, 0x422d51ec, 0x422db852, 0x00000004, 0x00333466, 0x00000003, 0x00000002,
0x000004ac, 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x423070a4, 0x4230d70a, 0x42313d71,
0x4231a3d7, 0x42307ae1, 0x4230e148, 0x423147ae, 0x4231ae14, 0x4230851f, 0x4230eb85, 0x423151ec,
0x4231b852, 0x42308f5c, 0x4230f5c3, 0x42315c29, 0x4231c28f, 0x00000004, 0x00343466, 0x00000003,
0x00000000, 0x000004d8, 0x00000000, 0x00000002, 0x00000001, 0x00000001, 0x3dced917, 0x3dd0e560,
0x00000004, 0x00325f66, 0x00000003, 0x00000001, 0x00000504, 0x00000000, 0x00000002, 0x00000001,
0x00000001, 0x3f8ced91, 0x3f8d0e56, 0x00000005, 0x325f3166, 0x00000000, 0x00000003, 0x00000001,
0x0000053c, 0x00000000, 0x00000002, 0x00000002, 0x00000001, 0x400676c9, 0x400cdd2f, 0x4006872b,
0x400ced91, 0x00000005, 0x325f3266, 0x00000000, 0x00000003, 0x00000001, 0x0000057c, 0x00000000,
0x00000002, 0x00000003, 0x00000001, 0x404676c9, 0x404cdd2f, 0x40534396, 0x4046872b, 0x404ced91,
0x405353f8, 0x00000005, 0x325f3366, 0x00000000, 0x00000003, 0x00000001, 0x000005c4, 0x00000000,
0x00000002, 0x00000004, 0x00000001, 0x40833b64, 0x40866e98, 0x4089a1cb, 0x408cd4fe, 0x40834396,
0x408676c9, 0x4089a9fc, 0x408cdd2f, 0x00000005, 0x325f3466, 0x00000000, 0x00000003, 0x00000002,
0x000005f4, 0x00000000, 0x00000002, 0x00000001, 0x00000001, 0x41319db2, 0x4131a1cb, 0x00000006,
0x5f313166, 0x00000032, 0x00000003, 0x00000002, 0x0000062c, 0x00000000, 0x00000002, 0x00000001,
0x00000002, 0x41419db2, 0x4143374c, 0x4141a1cb, 0x41433b64, 0x00000006, 0x5f323166, 0x00000032,
0x00000003, 0x00000002, 0x0000066c, 0x00000000, 0x00000002, 0x00000001, 0x00000003, 0x41519db2,
0x4153374c, 0x4154d0e5, 0x4151a1cb, 0x41533b64, 0x4154d4fe, 0x00000006, 0x5f333166, 0x00000032,
0x00000003, 0x00000002, 0x000006b4, 0x00000000, 0x00000002, 0x00000001, 0x00000004, 0x41619db2,
0x4163374c, 0x4164d0e5, 0x41666a7f, 0x4161a1cb, 0x41633b64, 0x4164d4fe, 0x41666e98, 0x00000006,
0x5f343166, 0x00000032, 0x00000003, 0x00000002, 0x000006ec, 0x00000000, 0x00000002, 0x00000002,
0x00000001, 0x41a8e17c, 0x41a9ae49, 0x41a8e1b1, 0x41a9ae7d, 0x00000006, 0x5f313266, 0x00000032,
0x00000003, 0x00000002, 0x00000734, 0x00000000, 0x00000002, 0x00000002, 0x00000002, 0x41b0e17c,
0x41b1ae49, 0x41b0f5f7, 0x41b1c2c4, 0x41b0e1b1, 0x41b1ae7d, 0x41b0f62b, 0x41b1c2f8, 0x00000006,
0x5f323266, 0x00000032, 0x00000003, 0x00000002, 0x0000078c, 0x00000000, 0x00000002, 0x00000002,
0x00000003, 0x41b8e17c, 0x41b9ae49, 0x41b8f5f7, 0x41b9c2c4, 0x41b90a72, 0x41b9d73f, 0x41b8e1b1,
0x41b9ae7d, 0x41b8f62b, 0x41b9c2f8, 0x41b90aa6, 0x41b9d773, 0x00000006, 0x5f333266, 0x00000032,
0x00000003, 0x00000002, 0x000007f4, 0x00000000, 0x00000002, 0x00000002, 0x00000004, 0x41c0e17c,
0x41c1ae49, 0x41c0f5f7, 0x41c1c2c4, 0x41c10a72, 0x41c1d73f, 0x41c11eed, 0x41c1ebba, 0x41c0e1b1,
0x41c1ae7d, 0x41c0f62b, 0x41c1c2f8, 0x41c10aa6, 0x41c1d773, 0x41c11f21, 0x41c1ebee, 0x00000006,
0x5f343266, 0x00000032, 0x00000003, 0x00000002, 0x00000834, 0x00000000, 0x00000002, 0x00000003,
0x00000001, 0x41f8e17c, 0x41f9ae49, 0x41fa7b16, 0x41f8e1b1, 0x41f9ae7d, 0x41fa7b4a, 0x00000006,
0x5f313366, 0x00000032, 0x00000003, 0x00000002, 0x0000088c, 0x00000000, 0x00000002, 0x00000003,
0x00000002, 0x420070be, 0x4200d724, 0x42013d8b, 0x42007afb, 0x4200e162, 0x420147c8, 0x420070d8,
0x4200d73f, 0x42013da5, 0x42007b16, 0x4200e17c, 0x420147e3, 0x00000006, 0x5f323366, 0x00000032,
0x00000003, 0x00000002, 0x000008fc, 0x00000000, 0x00000002, 0x00000003, 0x00000003, 0x420470be,
0x4204d724, 0x42053d8b, 0x42047afb, 0x4204e162, 0x420547c8, 0x42048539, 0x4204eb9f, 0x42055206,
0x420470d8, 0x4204d73f, 0x42053da5, 0x42047b16, 0x4204e17c, 0x420547e3, 0x42048553, 0x4204ebba,
0x42055220, 0x00000006, 0x5f333366, 0x00000032, 0x00000003, 0x00000002, 0x00000984, 0x00000000,
0x00000002, 0x00000003, 0x00000004, 0x420870be, 0x4208d724, 0x42093d8b, 0x42087afb, 0x4208e162,
0x420947c8, 0x42088539, 0x4208eb9f, 0x42095206, 0x42088f76, 0x4208f5dd, 0x42095c43, 0x420870d8,
0x4208d73f, 0x42093da5, 0x42087b16, 0x4208e17c, 0x420947e3, 0x42088553, 0x4208ebba, 0x42095220,
0x42088f91, 0x4208f5f7, 0x42095c5d, 0x00000006, 0x5f343366, 0x00000032, 0x00000003, 0x00000002,
0x000009cc, 0x00000000, 0x00000002, 0x00000004, 0x00000001, 0x422470be, 0x4224d724, 0x42253d8b,
0x4225a3f1, 0x422470d8, 0x4224d73f, 0x42253da5, 0x4225a40b, 0x00000006, 0x5f313466, 0x00000032,
0x00000003, 0x00000002, 0x00000a34, 0x00000000, 0x00000002, 0x00000004, 0x00000002, 0x422870be,
0x4228d724, 0x42293d8b, 0x4229a3f1, 0x42287afb, 0x4228e162, 0x422947c8, 0x4229ae2f, 0x422870d8,
0x4228d73f, 0x42293da5, 0x4229a40b, 0x42287b16, 0x4228e17c, 0x422947e3, 0x4229ae49, 0x00000006,
0x5f323466, 0x00000032, 0x00000003, 0x00000002, 0x00000abc, 0x00000000, 0x00000002, 0x00000004,
0x00000003, 0x422c70be, 0x422cd724, 0x422d3d8b, 0x422da3f1, 0x422c7afb, 0x422ce162, 0x422d47c8,
0x422dae2f, 0x422c8539, 0x422ceb9f, 0x422d5206, 0x422db86c, 0x422c70d8, 0x422cd73f, 0x422d3da5,
0x422da40b, 0x422c7b16, 0x422ce17c, 0x422d47e3, 0x422dae49, 0x422c8553, 0x422cebba, 0x422d5220,
0x422db886, 0x00000006, 0x5f333466, 0x00000032, 0x00000003, 0x00000002, 0x00000b64, 0x00000000,
0x00000002, 0x00000004, 0x00000004, 0x423070be, 0x4230d724, 0x42313d8b, 0x4231a3f1, 0x42307afb,
0x4230e162, 0x423147c8, 0x4231ae2f, 0x42308539, 0x4230eb9f, 0x42315206, 0x4231b86c, 0x42308f76,
0x4230f5dd, 0x42315c43, 0x4231c2aa, 0x423070d8, 0x4230d73f, 0x42313da5, 0x4231a40b, 0x42307b16,
0x4230e17c, 0x423147e3, 0x4231ae49, 0x42308553, 0x4230ebba, 0x42315220, 0x4231b886, 0x42308f91,
0x4230f5f7, 0x42315c5d, 0x4231c2c4, 0x00000006, 0x5f343466, 0x00000032, 0x00000002, 0x00000070,
0x00000002, 0x00000074, 0x0000002a, 0x00000001, 0x00000001, 0x00000001, 0x00000004, 0x00000020,
0x00000000, 0x00000000, 0x0000002c, 0x00000048, 0x00000000, 0x00000000, 0x00000054, 0x00000070,
0x00000000, 0x00000000, 0x00000080, 0x0000009c, 0x00000000, 0x00000000, 0x000000b0, 0x000000cc,
0x00000000, 0x00000000, 0x000000e4, 0x00000100, 0x00000000, 0x00000000, 0x0000010c, 0x00000128,
0x00000000, 0x00000000, 0x00000138, 0x00000154, 0x00000000, 0x00000000, 0x00000168, 0x00000184,
0x00000000, 0x00000000, 0x0000019c, 0x000001b8, 0x00000000, 0x00000000, 0x000001c8, 0x000001e4,
0x00000000, 0x00000000, 0x000001fc, 0x00000218, 0x00000000, 0x00000000, 0x00000238, 0x00000254,
0x00000000, 0x00000000, 0x0000027c, 0x00000298, 0x00000000, 0x00000000, 0x000002ac, 0x000002c8,
0x00000000, 0x00000000, 0x000002e8, 0x00000304, 0x00000000, 0x00000000, 0x00000330, 0x0000034c,
0x00000000, 0x00000000, 0x00000384, 0x000003a0, 0x00000000, 0x00000000, 0x000003b8, 0x000003d4,
0x00000000, 0x00000000, 0x000003fc, 0x00000418, 0x00000000, 0x00000000, 0x00000450, 0x0000046c,
0x00000000, 0x00000000, 0x000004b4, 0x000004d0, 0x00000000, 0x00000000, 0x000004e0, 0x000004fc,
0x00000000, 0x00000000, 0x00000510, 0x0000052c, 0x00000000, 0x00000000, 0x00000548, 0x00000564,
0x00000000, 0x00000000, 0x00000588, 0x000005a4, 0x00000000, 0x00000000, 0x000005d0, 0x000005ec,
0x00000000, 0x00000000, 0x00000600, 0x0000061c, 0x00000000, 0x00000000, 0x00000638, 0x00000654,
0x00000000, 0x00000000, 0x00000678, 0x00000694, 0x00000000, 0x00000000, 0x000006c0, 0x000006dc,
0x00000000, 0x00000000, 0x000006f8, 0x00000714, 0x00000000, 0x00000000, 0x00000740, 0x0000075c,
0x00000000, 0x00000000, 0x00000798, 0x000007b4, 0x00000000, 0x00000000, 0x00000800, 0x0000081c,
0x00000000, 0x00000000, 0x00000840, 0x0000085c, 0x00000000, 0x00000000, 0x00000898, 0x000008b4,
0x00000000, 0x00000000, 0x00000908, 0x00000924, 0x00000000, 0x00000000, 0x00000990, 0x000009ac,
0x00000000, 0x00000000, 0x000009d8, 0x000009f4, 0x00000000, 0x00000000, 0x00000a40, 0x00000a5c,
0x00000000, 0x00000000, 0x00000ac8, 0x00000ae4, 0x00000000, 0x00000000, 0x00000b78, 0x00000000,
0x00000001, 0x00000b70, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

struct test_effect_parameter_value_result test_effect_parameter_value_result_float[] =
{
    {"f",     {"f",     NULL, D3DXPC_SCALAR,      D3DXPT_FLOAT, 1, 1, 0, 0, 0, 0,   4},  10},
    {"f1",    {"f1",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 1, 0, 0, 0, 0,   4},  20},
    {"f2",    {"f2",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 2, 0, 0, 0, 0,   8},  30},
    {"f3",    {"f3",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 3, 0, 0, 0, 0,  12},  41},
    {"f4",    {"f4",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 4, 0, 0, 0, 0,  16},  53},
    {"f11",   {"f11",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 1, 0, 0, 0, 0,   4},  66},
    {"f12",   {"f12",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 2, 0, 0, 0, 0,   8},  76},
    {"f13",   {"f13",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 3, 0, 0, 0, 0,  12},  87},
    {"f14",   {"f14",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 4, 0, 0, 0, 0,  16},  99},
    {"f21",   {"f21",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 1, 0, 0, 0, 0,   8}, 112},
    {"f22",   {"f22",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 2, 0, 0, 0, 0,  16}, 123},
    {"f23",   {"f23",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 3, 0, 0, 0, 0,  24}, 136},
    {"f24",   {"f24",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 4, 0, 0, 0, 0,  32}, 151},
    {"f31",   {"f31",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 1, 0, 0, 0, 0,  12}, 168},
    {"f32",   {"f32",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 2, 0, 0, 0, 0,  24}, 180},
    {"f33",   {"f33",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 3, 0, 0, 0, 0,  36}, 195},
    {"f34",   {"f34",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 4, 0, 0, 0, 0,  48}, 213},
    {"f41",   {"f41",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 1, 0, 0, 0, 0,  16}, 234},
    {"f42",   {"f42",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 2, 0, 0, 0, 0,  32}, 247},
    {"f43",   {"f43",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 3, 0, 0, 0, 0,  48}, 264},
    {"f44",   {"f44",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 4, 0, 0, 0, 0,  64}, 285},
    {"f_2",   {"f_2",   NULL, D3DXPC_SCALAR,      D3DXPT_FLOAT, 1, 1, 2, 0, 0, 0,   8}, 310},
    {"f1_2",  {"f1_2",  NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 1, 2, 0, 0, 0,   8}, 321},
    {"f2_2",  {"f2_2",  NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 2, 2, 0, 0, 0,  16}, 333},
    {"f3_2",  {"f3_2",  NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 3, 2, 0, 0, 0,  24}, 347},
    {"f4_2",  {"f4_2",  NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 4, 2, 0, 0, 0,  32}, 363},
    {"f11_2", {"f11_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 1, 2, 0, 0, 0,   8}, 381},
    {"f12_2", {"f12_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 2, 2, 0, 0, 0,  16}, 393},
    {"f13_2", {"f13_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 3, 2, 0, 0, 0,  24}, 407},
    {"f14_2", {"f14_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 1, 4, 2, 0, 0, 0,  32}, 423},
    {"f21_2", {"f21_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 1, 2, 0, 0, 0,  16}, 441},
    {"f22_2", {"f22_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 2, 2, 0, 0, 0,  32}, 455},
    {"f23_2", {"f23_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 3, 2, 0, 0, 0,  48}, 473},
    {"f24_2", {"f24_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 2, 4, 2, 0, 0, 0,  64}, 495},
    {"f31_2", {"f31_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 1, 2, 0, 0, 0,  24}, 521},
    {"f32_2", {"f32_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 2, 2, 0, 0, 0,  48}, 537},
    {"f33_2", {"f33_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 3, 2, 0, 0, 0,  72}, 559},
    {"f34_2", {"f34_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 3, 4, 2, 0, 0, 0,  96}, 587},
    {"f41_2", {"f41_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 1, 2, 0, 0, 0,  32}, 621},
    {"f42_2", {"f42_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 2, 2, 0, 0, 0,  64}, 639},
    {"f43_2", {"f43_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 3, 2, 0, 0, 0,  96}, 665},
    {"f44_2", {"f44_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 4, 4, 2, 0, 0, 0, 128}, 699},
};

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
int i = 1;
int1 i1 = {11};
int2 i2 = {21, 22};
int3 i3 = {31, 32, 33};
int4 i4 = {41, 42, 43, 44};
int1x1 i11 = {111};
int1x2 i12 = {121, 122};
int1x3 i13 = {131, 132, 133};
int1x4 i14 = {141, 142, 143, 144};
int2x1 i21 = {{2111, 2121}};
int2x2 i22 = {{2211, 2221}, {2212, 2222}};
int2x3 i23 = {{2311, 2321}, {2312, 2322}, {2313, 2323}};
int2x4 i24 = {{2411, 2421}, {2412, 2422}, {2413, 2423}, {2414, 2424}};
int3x1 i31 = {{3111, 3121, 3131}};
int3x2 i32 = {{3211, 3221, 3231}, {3212, 3222, 3232}};
int3x3 i33 = {{3311, 3321, 3331}, {3312, 3322, 3332},
        {3313, 3323, 3333}};
int3x4 i34 = {{3411, 3421, 3431}, {3412, 3422, 3432},
        {3413, 3423, 3433}, {3414, 3424, 3434}};
int4x1 i41 = {{4111, 4121, 4131, 4141}};
int4x2 i42 = {{4211, 4221, 4231, 4241}, {4212, 4222, 4232, 4242}};
int4x3 i43 = {{4311, 4321, 4331, 4341}, {4312, 4322, 4332, 4342},
        {4313, 4323, 4333, 4343}};
int4x4 i44 = {{4411, 4421, 4431, 4441}, {4412, 4422, 4432, 4442},
        {4413, 4423, 4433, 4443}, {4414, 4424, 4434, 4444}};
int i_2[2] = {0101, 0102};
int1 i1_2[2] = {{1101}, {1102}};
int2 i2_2[2] = {{2101, 2201}, {2102, 2202}};
int3 i3_2[2] = {{3101, 3201, 3301}, {3102, 3202, 3302}};
int4 i4_2[2] = {{4101, 4201, 4301, 4401}, {4102, 4202, 4302, 4402}};
int1x1 i11_2[2] = {{11101}, {11102}};
int1x2 i12_2[2] = {{12101, 12201}, {12102, 12202}};
int1x3 i13_2[2] = {{13101, 13201, 13301}, {13102, 13202, 13302}};
int1x4 i14_2[2] = {{14101, 14201, 14301, 14401}, {14102, 14202, 14302, 14402}};
int2x1 i21_2[2] = {{{211101, 212101}}, {{211102, 212102}}};
int2x2 i22_2[2] = {{{221101, 222101}, {221201, 222201}}, {{221102, 222102}, {221202, 222202}}};
int2x3 i23_2[2] = {{{231101, 232101}, {231201, 232201}, {231301, 232301}}, {{231102, 232102},
        {231202, 232202}, {231302, 232302}}};
int2x4 i24_2[2] = {{{241101, 242101}, {241201, 242201}, {241301, 242301}, {241401, 242401}},
        {{241102, 242102}, {241202, 242202}, {241302, 242302}, {241402, 242402}}};
int3x1 i31_2[2] = {{{311101, 312101, 313101}}, {{311102, 312102, 313102}}};
int3x2 i32_2[2] = {{{321101, 322101, 323101}, {321201, 322201, 323201}},
        {{321102, 322102, 323102}, {321202, 322202, 323202}}};
int3x3 i33_2[2] = {{{331101, 332101, 333101}, {331201, 332201, 333201},
        {331301, 332301, 333301}}, {{331102, 332102, 333102}, {331202, 332202, 333202},
        {331302, 332302, 333302}}};
int3x4 i34_2[2] = {{{341101, 342101, 343101}, {341201, 342201, 343201},
        {341301, 342301, 343301}, {341401, 342401, 343401}}, {{341102, 342102, 343102},
        {341202, 342202, 343202}, {341302, 342302, 343302}, {341402, 342402, 343402}}};
int4x1 i41_2[2] = {{{411101, 412101, 413101, 414101}}, {{411102, 412102, 413102, 414102}}};
int4x2 i42_2[2] = {{{421101, 422101, 423101, 424101}, {421201, 422201, 423201, 424201}},
        {{421102, 422102, 423102, 424102}, {421202, 422202, 423202, 424202}}};
int4x3 i43_2[2] = {{{431101, 432101, 433101, 434101}, {431201, 432201, 433201, 434201},
        {431301, 432301, 433301, 434301}}, {{431102, 432102, 433102, 434102},
        {431202, 432202, 433202, 434202}, {431302, 432302, 433302, 434302}}};
int4x4 i44_2[2] = {{{441101, 442101, 443101, 444101}, {441201, 442201, 443201, 444201},
        {441301, 442301, 443301, 444301}, {441401, 442401, 443401, 444401}},
        {{441102, 442102, 443102, 444102}, {441202, 442202, 443202, 444202},
        {441302, 442302, 443302, 444302}, {441402, 442402, 443402, 444402}}};
technique t { pass p { } }
#endif
static const DWORD test_effect_parameter_value_blob_int[] =
{
0xfeff0901, 0x00000b80, 0x00000000, 0x00000002, 0x00000000, 0x00000024, 0x00000000, 0x00000000,
0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000069, 0x00000002, 0x00000001, 0x0000004c,
0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x0000000b, 0x00000003, 0x00003169, 0x00000002,
0x00000001, 0x00000078, 0x00000000, 0x00000000, 0x00000002, 0x00000001, 0x00000015, 0x00000016,
0x00000003, 0x00003269, 0x00000002, 0x00000001, 0x000000a8, 0x00000000, 0x00000000, 0x00000003,
0x00000001, 0x0000001f, 0x00000020, 0x00000021, 0x00000003, 0x00003369, 0x00000002, 0x00000001,
0x000000dc, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000029, 0x0000002a, 0x0000002b,
0x0000002c, 0x00000003, 0x00003469, 0x00000002, 0x00000002, 0x00000104, 0x00000000, 0x00000000,
0x00000001, 0x00000001, 0x0000006f, 0x00000004, 0x00313169, 0x00000002, 0x00000002, 0x00000130,
0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000079, 0x0000007a, 0x00000004, 0x00323169,
0x00000002, 0x00000002, 0x00000160, 0x00000000, 0x00000000, 0x00000001, 0x00000003, 0x00000083,
0x00000084, 0x00000085, 0x00000004, 0x00333169, 0x00000002, 0x00000002, 0x00000194, 0x00000000,
0x00000000, 0x00000001, 0x00000004, 0x0000008d, 0x0000008e, 0x0000008f, 0x00000090, 0x00000004,
0x00343169, 0x00000002, 0x00000002, 0x000001c0, 0x00000000, 0x00000000, 0x00000002, 0x00000001,
0x0000083f, 0x00000849, 0x00000004, 0x00313269, 0x00000002, 0x00000002, 0x000001f4, 0x00000000,
0x00000000, 0x00000002, 0x00000002, 0x000008a3, 0x000008ad, 0x000008a4, 0x000008ae, 0x00000004,
0x00323269, 0x00000002, 0x00000002, 0x00000230, 0x00000000, 0x00000000, 0x00000002, 0x00000003,
0x00000907, 0x00000911, 0x00000908, 0x00000912, 0x00000909, 0x00000913, 0x00000004, 0x00333269,
0x00000002, 0x00000002, 0x00000274, 0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x0000096b,
0x00000975, 0x0000096c, 0x00000976, 0x0000096d, 0x00000977, 0x0000096e, 0x00000978, 0x00000004,
0x00343269, 0x00000002, 0x00000002, 0x000002a4, 0x00000000, 0x00000000, 0x00000003, 0x00000001,
0x00000c27, 0x00000c31, 0x00000c3b, 0x00000004, 0x00313369, 0x00000002, 0x00000002, 0x000002e0,
0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000c8b, 0x00000c95, 0x00000c9f, 0x00000c8c,
0x00000c96, 0x00000ca0, 0x00000004, 0x00323369, 0x00000002, 0x00000002, 0x00000328, 0x00000000,
0x00000000, 0x00000003, 0x00000003, 0x00000cef, 0x00000cf9, 0x00000d03, 0x00000cf0, 0x00000cfa,
0x00000d04, 0x00000cf1, 0x00000cfb, 0x00000d05, 0x00000004, 0x00333369, 0x00000002, 0x00000002,
0x0000037c, 0x00000000, 0x00000000, 0x00000003, 0x00000004, 0x00000d53, 0x00000d5d, 0x00000d67,
0x00000d54, 0x00000d5e, 0x00000d68, 0x00000d55, 0x00000d5f, 0x00000d69, 0x00000d56, 0x00000d60,
0x00000d6a, 0x00000004, 0x00343369, 0x00000002, 0x00000002, 0x000003b0, 0x00000000, 0x00000000,
0x00000004, 0x00000001, 0x0000100f, 0x00001019, 0x00001023, 0x0000102d, 0x00000004, 0x00313469,
0x00000002, 0x00000002, 0x000003f4, 0x00000000, 0x00000000, 0x00000004, 0x00000002, 0x00001073,
0x0000107d, 0x00001087, 0x00001091, 0x00001074, 0x0000107e, 0x00001088, 0x00001092, 0x00000004,
0x00323469, 0x00000002, 0x00000002, 0x00000448, 0x00000000, 0x00000000, 0x00000004, 0x00000003,
0x000010d7, 0x000010e1, 0x000010eb, 0x000010f5, 0x000010d8, 0x000010e2, 0x000010ec, 0x000010f6,
0x000010d9, 0x000010e3, 0x000010ed, 0x000010f7, 0x00000004, 0x00333469, 0x00000002, 0x00000002,
0x000004ac, 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x0000113b, 0x00001145, 0x0000114f,
0x00001159, 0x0000113c, 0x00001146, 0x00001150, 0x0000115a, 0x0000113d, 0x00001147, 0x00001151,
0x0000115b, 0x0000113e, 0x00001148, 0x00001152, 0x0000115c, 0x00000004, 0x00343469, 0x00000002,
0x00000000, 0x000004d8, 0x00000000, 0x00000002, 0x00000001, 0x00000001, 0x00000041, 0x00000042,
0x00000004, 0x00325f69, 0x00000002, 0x00000001, 0x00000504, 0x00000000, 0x00000002, 0x00000001,
0x00000001, 0x0000044d, 0x0000044e, 0x00000005, 0x325f3169, 0x00000000, 0x00000002, 0x00000001,
0x0000053c, 0x00000000, 0x00000002, 0x00000002, 0x00000001, 0x00000835, 0x00000899, 0x00000836,
0x0000089a, 0x00000005, 0x325f3269, 0x00000000, 0x00000002, 0x00000001, 0x0000057c, 0x00000000,
0x00000002, 0x00000003, 0x00000001, 0x00000c1d, 0x00000c81, 0x00000ce5, 0x00000c1e, 0x00000c82,
0x00000ce6, 0x00000005, 0x325f3369, 0x00000000, 0x00000002, 0x00000001, 0x000005c4, 0x00000000,
0x00000002, 0x00000004, 0x00000001, 0x00001005, 0x00001069, 0x000010cd, 0x00001131, 0x00001006,
0x0000106a, 0x000010ce, 0x00001132, 0x00000005, 0x325f3469, 0x00000000, 0x00000002, 0x00000002,
0x000005f4, 0x00000000, 0x00000002, 0x00000001, 0x00000001, 0x00002b5d, 0x00002b5e, 0x00000006,
0x5f313169, 0x00000032, 0x00000002, 0x00000002, 0x0000062c, 0x00000000, 0x00000002, 0x00000001,
0x00000002, 0x00002f45, 0x00002fa9, 0x00002f46, 0x00002faa, 0x00000006, 0x5f323169, 0x00000032,
0x00000002, 0x00000002, 0x0000066c, 0x00000000, 0x00000002, 0x00000001, 0x00000003, 0x0000332d,
0x00003391, 0x000033f5, 0x0000332e, 0x00003392, 0x000033f6, 0x00000006, 0x5f333169, 0x00000032,
0x00000002, 0x00000002, 0x000006b4, 0x00000000, 0x00000002, 0x00000001, 0x00000004, 0x00003715,
0x00003779, 0x000037dd, 0x00003841, 0x00003716, 0x0000377a, 0x000037de, 0x00003842, 0x00000006,
0x5f343169, 0x00000032, 0x00000002, 0x00000002, 0x000006ec, 0x00000000, 0x00000002, 0x00000002,
0x00000001, 0x0003389d, 0x00033c85, 0x0003389e, 0x00033c86, 0x00000006, 0x5f313269, 0x00000032,
0x00000002, 0x00000002, 0x00000734, 0x00000000, 0x00000002, 0x00000002, 0x00000002, 0x00035fad,
0x00036395, 0x00036011, 0x000363f9, 0x00035fae, 0x00036396, 0x00036012, 0x000363fa, 0x00000006,
0x5f323269, 0x00000032, 0x00000002, 0x00000002, 0x0000078c, 0x00000000, 0x00000002, 0x00000002,
0x00000003, 0x000386bd, 0x00038aa5, 0x00038721, 0x00038b09, 0x00038785, 0x00038b6d, 0x000386be,
0x00038aa6, 0x00038722, 0x00038b0a, 0x00038786, 0x00038b6e, 0x00000006, 0x5f333269, 0x00000032,
0x00000002, 0x00000002, 0x000007f4, 0x00000000, 0x00000002, 0x00000002, 0x00000004, 0x0003adcd,
0x0003b1b5, 0x0003ae31, 0x0003b219, 0x0003ae95, 0x0003b27d, 0x0003aef9, 0x0003b2e1, 0x0003adce,
0x0003b1b6, 0x0003ae32, 0x0003b21a, 0x0003ae96, 0x0003b27e, 0x0003aefa, 0x0003b2e2, 0x00000006,
0x5f343269, 0x00000032, 0x00000002, 0x00000002, 0x00000834, 0x00000000, 0x00000002, 0x00000003,
0x00000001, 0x0004bf3d, 0x0004c325, 0x0004c70d, 0x0004bf3e, 0x0004c326, 0x0004c70e, 0x00000006,
0x5f313369, 0x00000032, 0x00000002, 0x00000002, 0x0000088c, 0x00000000, 0x00000002, 0x00000003,
0x00000002, 0x0004e64d, 0x0004ea35, 0x0004ee1d, 0x0004e6b1, 0x0004ea99, 0x0004ee81, 0x0004e64e,
0x0004ea36, 0x0004ee1e, 0x0004e6b2, 0x0004ea9a, 0x0004ee82, 0x00000006, 0x5f323369, 0x00000032,
0x00000002, 0x00000002, 0x000008fc, 0x00000000, 0x00000002, 0x00000003, 0x00000003, 0x00050d5d,
0x00051145, 0x0005152d, 0x00050dc1, 0x000511a9, 0x00051591, 0x00050e25, 0x0005120d, 0x000515f5,
0x00050d5e, 0x00051146, 0x0005152e, 0x00050dc2, 0x000511aa, 0x00051592, 0x00050e26, 0x0005120e,
0x000515f6, 0x00000006, 0x5f333369, 0x00000032, 0x00000002, 0x00000002, 0x00000984, 0x00000000,
0x00000002, 0x00000003, 0x00000004, 0x0005346d, 0x00053855, 0x00053c3d, 0x000534d1, 0x000538b9,
0x00053ca1, 0x00053535, 0x0005391d, 0x00053d05, 0x00053599, 0x00053981, 0x00053d69, 0x0005346e,
0x00053856, 0x00053c3e, 0x000534d2, 0x000538ba, 0x00053ca2, 0x00053536, 0x0005391e, 0x00053d06,
0x0005359a, 0x00053982, 0x00053d6a, 0x00000006, 0x5f343369, 0x00000032, 0x00000002, 0x00000002,
0x000009cc, 0x00000000, 0x00000002, 0x00000004, 0x00000001, 0x000645dd, 0x000649c5, 0x00064dad,
0x00065195, 0x000645de, 0x000649c6, 0x00064dae, 0x00065196, 0x00000006, 0x5f313469, 0x00000032,
0x00000002, 0x00000002, 0x00000a34, 0x00000000, 0x00000002, 0x00000004, 0x00000002, 0x00066ced,
0x000670d5, 0x000674bd, 0x000678a5, 0x00066d51, 0x00067139, 0x00067521, 0x00067909, 0x00066cee,
0x000670d6, 0x000674be, 0x000678a6, 0x00066d52, 0x0006713a, 0x00067522, 0x0006790a, 0x00000006,
0x5f323469, 0x00000032, 0x00000002, 0x00000002, 0x00000abc, 0x00000000, 0x00000002, 0x00000004,
0x00000003, 0x000693fd, 0x000697e5, 0x00069bcd, 0x00069fb5, 0x00069461, 0x00069849, 0x00069c31,
0x0006a019, 0x000694c5, 0x000698ad, 0x00069c95, 0x0006a07d, 0x000693fe, 0x000697e6, 0x00069bce,
0x00069fb6, 0x00069462, 0x0006984a, 0x00069c32, 0x0006a01a, 0x000694c6, 0x000698ae, 0x00069c96,
0x0006a07e, 0x00000006, 0x5f333469, 0x00000032, 0x00000002, 0x00000002, 0x00000b64, 0x00000000,
0x00000002, 0x00000004, 0x00000004, 0x0006bb0d, 0x0006bef5, 0x0006c2dd, 0x0006c6c5, 0x0006bb71,
0x0006bf59, 0x0006c341, 0x0006c729, 0x0006bbd5, 0x0006bfbd, 0x0006c3a5, 0x0006c78d, 0x0006bc39,
0x0006c021, 0x0006c409, 0x0006c7f1, 0x0006bb0e, 0x0006bef6, 0x0006c2de, 0x0006c6c6, 0x0006bb72,
0x0006bf5a, 0x0006c342, 0x0006c72a, 0x0006bbd6, 0x0006bfbe, 0x0006c3a6, 0x0006c78e, 0x0006bc3a,
0x0006c022, 0x0006c40a, 0x0006c7f2, 0x00000006, 0x5f343469, 0x00000032, 0x00000002, 0x00000070,
0x00000002, 0x00000074, 0x0000002a, 0x00000001, 0x00000001, 0x00000001, 0x00000004, 0x00000020,
0x00000000, 0x00000000, 0x0000002c, 0x00000048, 0x00000000, 0x00000000, 0x00000054, 0x00000070,
0x00000000, 0x00000000, 0x00000080, 0x0000009c, 0x00000000, 0x00000000, 0x000000b0, 0x000000cc,
0x00000000, 0x00000000, 0x000000e4, 0x00000100, 0x00000000, 0x00000000, 0x0000010c, 0x00000128,
0x00000000, 0x00000000, 0x00000138, 0x00000154, 0x00000000, 0x00000000, 0x00000168, 0x00000184,
0x00000000, 0x00000000, 0x0000019c, 0x000001b8, 0x00000000, 0x00000000, 0x000001c8, 0x000001e4,
0x00000000, 0x00000000, 0x000001fc, 0x00000218, 0x00000000, 0x00000000, 0x00000238, 0x00000254,
0x00000000, 0x00000000, 0x0000027c, 0x00000298, 0x00000000, 0x00000000, 0x000002ac, 0x000002c8,
0x00000000, 0x00000000, 0x000002e8, 0x00000304, 0x00000000, 0x00000000, 0x00000330, 0x0000034c,
0x00000000, 0x00000000, 0x00000384, 0x000003a0, 0x00000000, 0x00000000, 0x000003b8, 0x000003d4,
0x00000000, 0x00000000, 0x000003fc, 0x00000418, 0x00000000, 0x00000000, 0x00000450, 0x0000046c,
0x00000000, 0x00000000, 0x000004b4, 0x000004d0, 0x00000000, 0x00000000, 0x000004e0, 0x000004fc,
0x00000000, 0x00000000, 0x00000510, 0x0000052c, 0x00000000, 0x00000000, 0x00000548, 0x00000564,
0x00000000, 0x00000000, 0x00000588, 0x000005a4, 0x00000000, 0x00000000, 0x000005d0, 0x000005ec,
0x00000000, 0x00000000, 0x00000600, 0x0000061c, 0x00000000, 0x00000000, 0x00000638, 0x00000654,
0x00000000, 0x00000000, 0x00000678, 0x00000694, 0x00000000, 0x00000000, 0x000006c0, 0x000006dc,
0x00000000, 0x00000000, 0x000006f8, 0x00000714, 0x00000000, 0x00000000, 0x00000740, 0x0000075c,
0x00000000, 0x00000000, 0x00000798, 0x000007b4, 0x00000000, 0x00000000, 0x00000800, 0x0000081c,
0x00000000, 0x00000000, 0x00000840, 0x0000085c, 0x00000000, 0x00000000, 0x00000898, 0x000008b4,
0x00000000, 0x00000000, 0x00000908, 0x00000924, 0x00000000, 0x00000000, 0x00000990, 0x000009ac,
0x00000000, 0x00000000, 0x000009d8, 0x000009f4, 0x00000000, 0x00000000, 0x00000a40, 0x00000a5c,
0x00000000, 0x00000000, 0x00000ac8, 0x00000ae4, 0x00000000, 0x00000000, 0x00000b78, 0x00000000,
0x00000001, 0x00000b70, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

struct test_effect_parameter_value_result test_effect_parameter_value_result_int[] =
{
    {"i",     {"i",     NULL, D3DXPC_SCALAR,      D3DXPT_INT, 1, 1, 0, 0, 0, 0,   4},  10},
    {"i1",    {"i1",    NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 1, 0, 0, 0, 0,   4},  20},
    {"i2",    {"i2",    NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 2, 0, 0, 0, 0,   8},  30},
    {"i3",    {"i3",    NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 3, 0, 0, 0, 0,  12},  41},
    {"i4",    {"i4",    NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 4, 0, 0, 0, 0,  16},  53},
    {"i11",   {"i11",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 1, 0, 0, 0, 0,   4},  66},
    {"i12",   {"i12",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 2, 0, 0, 0, 0,   8},  76},
    {"i13",   {"i13",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 3, 0, 0, 0, 0,  12},  87},
    {"i14",   {"i14",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 4, 0, 0, 0, 0,  16},  99},
    {"i21",   {"i21",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 1, 0, 0, 0, 0,   8}, 112},
    {"i22",   {"i22",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 2, 0, 0, 0, 0,  16}, 123},
    {"i23",   {"i23",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 3, 0, 0, 0, 0,  24}, 136},
    {"i24",   {"i24",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 4, 0, 0, 0, 0,  32}, 151},
    {"i31",   {"i31",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 1, 0, 0, 0, 0,  12}, 168},
    {"i32",   {"i32",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 2, 0, 0, 0, 0,  24}, 180},
    {"i33",   {"i33",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 3, 0, 0, 0, 0,  36}, 195},
    {"i34",   {"i34",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 4, 0, 0, 0, 0,  48}, 213},
    {"i41",   {"i41",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 1, 0, 0, 0, 0,  16}, 234},
    {"i42",   {"i42",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 2, 0, 0, 0, 0,  32}, 247},
    {"i43",   {"i43",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 3, 0, 0, 0, 0,  48}, 264},
    {"i44",   {"i44",   NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 4, 0, 0, 0, 0,  64}, 285},
    {"i_2",   {"i_2",   NULL, D3DXPC_SCALAR,      D3DXPT_INT, 1, 1, 2, 0, 0, 0,   8}, 310},
    {"i1_2",  {"i1_2",  NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 1, 2, 0, 0, 0,   8}, 321},
    {"i2_2",  {"i2_2",  NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 2, 2, 0, 0, 0,  16}, 333},
    {"i3_2",  {"i3_2",  NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 3, 2, 0, 0, 0,  24}, 347},
    {"i4_2",  {"i4_2",  NULL, D3DXPC_VECTOR,      D3DXPT_INT, 1, 4, 2, 0, 0, 0,  32}, 363},
    {"i11_2", {"i11_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 1, 2, 0, 0, 0,   8}, 381},
    {"i12_2", {"i12_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 2, 2, 0, 0, 0,  16}, 393},
    {"i13_2", {"i13_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 3, 2, 0, 0, 0,  24}, 407},
    {"i14_2", {"i14_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 1, 4, 2, 0, 0, 0,  32}, 423},
    {"i21_2", {"i21_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 1, 2, 0, 0, 0,  16}, 441},
    {"i22_2", {"i22_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 2, 2, 0, 0, 0,  32}, 455},
    {"i23_2", {"i23_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 3, 2, 0, 0, 0,  48}, 473},
    {"i24_2", {"i24_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 2, 4, 2, 0, 0, 0,  64}, 495},
    {"i31_2", {"i31_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 1, 2, 0, 0, 0,  24}, 521},
    {"i32_2", {"i32_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 2, 2, 0, 0, 0,  48}, 537},
    {"i33_2", {"i33_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 3, 2, 0, 0, 0,  72}, 559},
    {"i34_2", {"i34_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 3, 4, 2, 0, 0, 0,  96}, 587},
    {"i41_2", {"i41_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 1, 2, 0, 0, 0,  32}, 621},
    {"i42_2", {"i42_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 2, 2, 0, 0, 0,  64}, 639},
    {"i43_2", {"i43_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 3, 2, 0, 0, 0,  96}, 665},
    {"i44_2", {"i44_2", NULL, D3DXPC_MATRIX_ROWS, D3DXPT_INT, 4, 4, 2, 0, 0, 0, 128}, 699},
};

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
string s = "test";
string s_2[2] = {"test1", "test2"};
texture2D tex;
Vertexshader v;
Vertexshader v_2[2];
Pixelshader p;
Pixelshader p_2[2];
technique t { pass p { } }
#endif
static const DWORD test_effect_parameter_value_blob_object[] =
{
0xfeff0901, 0x00000100, 0x00000000, 0x00000004, 0x00000004, 0x0000001c, 0x00000000, 0x00000000,
0x00000001, 0x00000002, 0x00000073, 0x00000004, 0x00000004, 0x00000040, 0x00000000, 0x00000002,
0x00000002, 0x00000003, 0x00000004, 0x00325f73, 0x00000007, 0x00000004, 0x00000060, 0x00000000,
0x00000000, 0x00000004, 0x00000004, 0x00786574, 0x00000010, 0x00000004, 0x00000080, 0x00000000,
0x00000000, 0x00000005, 0x00000002, 0x00000076, 0x00000010, 0x00000004, 0x000000a4, 0x00000000,
0x00000002, 0x00000006, 0x00000007, 0x00000004, 0x00325f76, 0x0000000f, 0x00000004, 0x000000c4,
0x00000000, 0x00000000, 0x00000008, 0x00000002, 0x00000070, 0x0000000f, 0x00000004, 0x000000e8,
0x00000000, 0x00000002, 0x00000009, 0x0000000a, 0x00000004, 0x00325f70, 0x00000002, 0x00000070,
0x00000002, 0x00000074, 0x00000007, 0x00000001, 0x00000007, 0x0000000b, 0x00000004, 0x00000018,
0x00000000, 0x00000000, 0x00000024, 0x00000038, 0x00000000, 0x00000000, 0x00000048, 0x0000005c,
0x00000000, 0x00000000, 0x00000068, 0x0000007c, 0x00000000, 0x00000000, 0x00000088, 0x0000009c,
0x00000000, 0x00000000, 0x000000ac, 0x000000c0, 0x00000000, 0x00000000, 0x000000cc, 0x000000e0,
0x00000000, 0x00000000, 0x000000f8, 0x00000000, 0x00000001, 0x000000f0, 0x00000000, 0x00000000,
0x0000000a, 0x00000000, 0x00000009, 0x00000000, 0x0000000a, 0x00000000, 0x00000008, 0x00000000,
0x00000006, 0x00000000, 0x00000007, 0x00000000, 0x00000005, 0x00000000, 0x00000004, 0x00000000,
0x00000002, 0x00000006, 0x74736574, 0x00000031, 0x00000003, 0x00000006, 0x74736574, 0x00000032,
0x00000001, 0x00000005, 0x74736574, 0x00000000,
};

struct test_effect_parameter_value_result test_effect_parameter_value_result_object[] =
{
    {"s",   {"s",   NULL, D3DXPC_OBJECT, D3DXPT_STRING,       0, 0, 0, 0, 0, 0, sizeof(void *)},     0},
    {"s_2", {"s_2", NULL, D3DXPC_OBJECT, D3DXPT_STRING,       0, 0, 2, 0, 0, 0, 2 * sizeof(void *)}, 0},
    {"tex", {"tex", NULL, D3DXPC_OBJECT, D3DXPT_TEXTURE2D,    0, 0, 0, 0, 0, 0, sizeof(void *)},     0},
    {"v",   {"v",   NULL, D3DXPC_OBJECT, D3DXPT_VERTEXSHADER, 0, 0, 0, 0, 0, 0, sizeof(void *)},     0},
    {"v_2", {"v_2", NULL, D3DXPC_OBJECT, D3DXPT_VERTEXSHADER, 0, 0, 2, 0, 0, 0, 2 * sizeof(void *)}, 0},
    {"p",   {"p",   NULL, D3DXPC_OBJECT, D3DXPT_PIXELSHADER,  0, 0, 0, 0, 0, 0, sizeof(void *)},     0},
    {"p_2", {"p_2", NULL, D3DXPC_OBJECT, D3DXPT_PIXELSHADER,  0, 0, 2, 0, 0, 0, 2 * sizeof(void *)}, 0},
};

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
float3 f3 = {-3.1, 153.2, 283.3};
float3 f3min = {-31.1, -31.2, -31.3};
float3 f3max = {320.1, 320.2, 320.3};
float4 f4 = {-4.1, 154.2, 284.3, 34.4};
float4 f4min = {-41.1, -41.2, -41.3, -41.4};
float4 f4max = {420.1, 42.20, 420.3, 420.4};
technique t { pass p { } }
#endif
static const DWORD test_effect_parameter_value_blob_special[] =
{
0xfeff0901, 0x00000150, 0x00000000, 0x00000003, 0x00000001, 0x0000002c, 0x00000000, 0x00000000,
0x00000003, 0x00000001, 0xc0466666, 0x43193333, 0x438da666, 0x00000003, 0x00003366, 0x00000003,
0x00000001, 0x0000005c, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0xc1f8cccd, 0xc1f9999a,
0xc1fa6666, 0x00000006, 0x696d3366, 0x0000006e, 0x00000003, 0x00000001, 0x00000090, 0x00000000,
0x00000000, 0x00000003, 0x00000001, 0x43a00ccd, 0x43a0199a, 0x43a02666, 0x00000006, 0x616d3366,
0x00000078, 0x00000003, 0x00000001, 0x000000c8, 0x00000000, 0x00000000, 0x00000004, 0x00000001,
0xc0833333, 0x431a3333, 0x438e2666, 0x4209999a, 0x00000003, 0x00003466, 0x00000003, 0x00000001,
0x000000fc, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0xc2246666, 0xc224cccd, 0xc2253333,
0xc225999a, 0x00000006, 0x696d3466, 0x0000006e, 0x00000003, 0x00000001, 0x00000134, 0x00000000,
0x00000000, 0x00000004, 0x00000001, 0x43d20ccd, 0x4228cccd, 0x43d22666, 0x43d23333, 0x00000006,
0x616d3466, 0x00000078, 0x00000002, 0x00000070, 0x00000002, 0x00000074, 0x00000006, 0x00000001,
0x00000001, 0x00000001, 0x00000004, 0x00000020, 0x00000000, 0x00000000, 0x00000034, 0x00000050,
0x00000000, 0x00000000, 0x00000068, 0x00000084, 0x00000000, 0x00000000, 0x0000009c, 0x000000b8,
0x00000000, 0x00000000, 0x000000d0, 0x000000ec, 0x00000000, 0x00000000, 0x00000108, 0x00000124,
0x00000000, 0x00000000, 0x00000148, 0x00000000, 0x00000001, 0x00000140, 0x00000000, 0x00000000,
0x00000000, 0x00000000,
};

struct test_effect_parameter_value_result test_effect_parameter_value_result_special[] =
{
    {"f3",    {"f3",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 3, 0, 0, 0, 0,  12},  10},
    {"f3min", {"f3min", NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 3, 0, 0, 0, 0,  12},  22},
    {"f3max", {"f3max", NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 3, 0, 0, 0, 0,  12},  35},
    {"f4",    {"f4",    NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 4, 0, 0, 0, 0,  16},  48},
    {"f4min", {"f4min", NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 4, 0, 0, 0, 0,  16},  61},
    {"f4max", {"f4max", NULL, D3DXPC_VECTOR,      D3DXPT_FLOAT, 1, 4, 0, 0, 0, 0,  16},  75},
};

#define ADD_PARAMETER_VALUE(x) {\
    test_effect_parameter_value_blob_ ## x,\
    sizeof(test_effect_parameter_value_blob_ ## x),\
    test_effect_parameter_value_result_ ## x,\
    sizeof(test_effect_parameter_value_result_ ## x)/sizeof(*test_effect_parameter_value_result_ ## x),\
}

static const struct
{
    const DWORD *blob;
    UINT blob_size;
    const struct test_effect_parameter_value_result *res;
    UINT res_count;
}
test_effect_parameter_value_data[] =
{
    ADD_PARAMETER_VALUE(float),
    ADD_PARAMETER_VALUE(int),
    ADD_PARAMETER_VALUE(object),
    ADD_PARAMETER_VALUE(special),
};

#undef ADD_PARAMETER_VALUE

/* Multiple of 16 to cover complete matrices */
#define EFFECT_PARAMETER_VALUE_ARRAY_SIZE 48
/* Constants for special INT/FLOAT conversation */
#define INT_FLOAT_MULTI 255.0f
#define INT_FLOAT_MULTI_INVERSE (1/INT_FLOAT_MULTI)

static void test_effect_parameter_value_GetValue(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    DWORD value[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    HRESULT hr;
    UINT l;

    memset(value, 0xab, sizeof(value));
    hr = effect->lpVtbl->GetValue(effect, parameter, value, res_desc->Bytes);
    if (res_desc->Class == D3DXPC_SCALAR
            || res_desc->Class == D3DXPC_VECTOR
            || res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetValue failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (l = 0; l < res_desc->Bytes / sizeof(*value); ++l)
        {
            ok(value[l] == res_value[l], "%u - %s: GetValue value[%u] failed, got %#x, expected %#x\n",
                    i, res_full_name, l, value[l], res_value[l]);
        }

        for (l = res_desc->Bytes / sizeof(*value); l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
        {
            ok(value[l] == 0xabababab, "%u - %s: GetValue value[%u] failed, got %#x, expected %#x\n",
                    i, res_full_name, l, value[l], 0xabababab);
        }
    }
    else if (res_desc->Class == D3DXPC_OBJECT)
    {
        switch (res_desc->Type)
        {
            case D3DXPT_PIXELSHADER:
            case D3DXPT_VERTEXSHADER:
            case D3DXPT_TEXTURE2D:
                ok(hr == D3D_OK, "%u - %s: GetValue failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

                for (l = 0; l < (res_desc->Elements ? res_desc->Elements : 1); ++l)
                {
                    IUnknown *unk = *((IUnknown **)value + l);
                    if (unk) IUnknown_Release(unk);
                }
                break;

            case D3DXPT_STRING:
                ok(hr == D3D_OK, "%u - %s: GetValue failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
                break;

            default:
                ok(0, "Type is %u, this should not happen!\n", res_desc->Type);
                break;
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetValue failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
        {
            ok(value[l] == 0xabababab, "%u - %s: GetValue value[%u] failed, got %#x, expected %#x\n",
                    i, res_full_name, l, value[l], 0xabababab);
        }
    }
}

static void test_effect_parameter_value_GetBool(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    BOOL bvalue = 0xabababab;
    HRESULT hr;

    hr = effect->lpVtbl->GetBool(effect, parameter, &bvalue);
    if (!res_desc->Elements && res_desc->Rows == 1 && res_desc->Columns == 1)
    {
        ok(hr == D3D_OK, "%u - %s: GetBool failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
        ok(bvalue == get_bool(res_value), "%u - %s: GetBool bvalue failed, got %#x, expected %#x\n",
                i, res_full_name, bvalue, get_bool(res_value));
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBool failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);
        ok(bvalue == 0xabababab, "%u - %s: GetBool bvalue failed, got %#x, expected %#x\n",
                i, res_full_name, bvalue, 0xabababab);
    }
}

static void test_effect_parameter_value_GetBoolArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    BOOL bavalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    HRESULT hr;
    UINT l, err = 0;

    memset(bavalue, 0xab, sizeof(bavalue));
    hr = effect->lpVtbl->GetBoolArray(effect, parameter, bavalue, res_desc->Bytes / sizeof(*bavalue));
    if (res_desc->Class == D3DXPC_SCALAR
            || res_desc->Class == D3DXPC_VECTOR
            || res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetBoolArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (l = 0; l < res_desc->Bytes / sizeof(*bavalue); ++l)
        {
            if (bavalue[l] != get_bool(&res_value[l])) ++err;
        }

        for (l = res_desc->Bytes / sizeof(*bavalue); l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
        {
            if (bavalue[l] != 0xabababab) ++err;
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBoolArray failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (bavalue[l] != 0xabababab) ++err;
    }
    ok(!err, "%u - %s: GetBoolArray failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetInt(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    INT ivalue = 0xabababab;
    HRESULT hr;

    hr = effect->lpVtbl->GetInt(effect, parameter, &ivalue);
    if (!res_desc->Elements && res_desc->Columns == 1 && res_desc->Rows == 1)
    {
        ok(hr == D3D_OK, "%u - %s: GetInt failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
        ok(ivalue == get_int(res_desc->Type, res_value), "%u - %s: GetInt ivalue failed, got %i, expected %i\n",
                i, res_full_name, ivalue, get_int(res_desc->Type, res_value));
    }
    else if(!res_desc->Elements && res_desc->Type == D3DXPT_FLOAT &&
            ((res_desc->Class == D3DXPC_VECTOR && res_desc->Columns != 2) ||
            (res_desc->Class == D3DXPC_MATRIX_ROWS && res_desc->Rows != 2 && res_desc->Columns == 1)))
    {
        INT tmp;

        ok(hr == D3D_OK, "%u - %s: GetInt failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        tmp = (INT)(min(max(0.0f, *((FLOAT *)res_value + 2)), 1.0f) * INT_FLOAT_MULTI);
        tmp += ((INT)(min(max(0.0f, *((FLOAT *)res_value + 1)), 1.0f) * INT_FLOAT_MULTI)) << 8;
        tmp += ((INT)(min(max(0.0f, *((FLOAT *)res_value + 0)), 1.0f) * INT_FLOAT_MULTI)) << 16;
        if (res_desc->Columns * res_desc->Rows > 3)
        {
            tmp += ((INT)(min(max(0.0f, *((FLOAT *)res_value + 3)), 1.0f) * INT_FLOAT_MULTI)) << 24;
        }

        ok(ivalue == tmp, "%u - %s: GetInt ivalue failed, got %x, expected %x\n",
                i, res_full_name, ivalue, tmp);
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetInt failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);
        ok(ivalue == 0xabababab, "%u - %s: GetInt ivalue failed, got %i, expected %i\n",
                i, res_full_name, ivalue, 0xabababab);
    }
}

static void test_effect_parameter_value_GetIntArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    INT iavalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    HRESULT hr;
    UINT l, err = 0;

    memset(iavalue, 0xab, sizeof(iavalue));
    hr = effect->lpVtbl->GetIntArray(effect, parameter, iavalue, res_desc->Bytes / sizeof(*iavalue));
    if (res_desc->Class == D3DXPC_SCALAR
            || res_desc->Class == D3DXPC_VECTOR
            || res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetIntArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (l = 0; l < res_desc->Bytes / sizeof(*iavalue); ++l)
        {
            if (iavalue[l] != get_int(res_desc->Type, &res_value[l])) ++err;
        }

        for (l = res_desc->Bytes / sizeof(*iavalue); l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
        {
            if (iavalue[l] != 0xabababab) ++err;
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetIntArray failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (iavalue[l] != 0xabababab) ++err;
    }
    ok(!err, "%u - %s: GetIntArray failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetFloat(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue = *(FLOAT *)&cmp;

    hr = effect->lpVtbl->GetFloat(effect, parameter, &fvalue);
    if (!res_desc->Elements && res_desc->Columns == 1 && res_desc->Rows == 1)
    {
        ok(hr == D3D_OK, "%u - %s: GetFloat failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
        ok(compare_float(fvalue, get_float(res_desc->Type, res_value), 512), "%u - %s: GetFloat fvalue failed, got %f, expected %f\n",
                i, res_full_name, fvalue, get_float(res_desc->Type, res_value));
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloat failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);
        ok(fvalue == *(FLOAT *)&cmp, "%u - %s: GetFloat fvalue failed, got %f, expected %f\n",
                i, res_full_name, fvalue, *(FLOAT *)&cmp);
    }
}

static void test_effect_parameter_value_GetFloatArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    FLOAT favalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    HRESULT hr;
    UINT l, err = 0;
    DWORD cmp = 0xabababab;

    memset(favalue, 0xab, sizeof(favalue));
    hr = effect->lpVtbl->GetFloatArray(effect, parameter, favalue, res_desc->Bytes / sizeof(*favalue));
    if (res_desc->Class == D3DXPC_SCALAR
            || res_desc->Class == D3DXPC_VECTOR
            || res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetFloatArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (l = 0; l < res_desc->Bytes / sizeof(*favalue); ++l)
        {
            if (!compare_float(favalue[l], get_float(res_desc->Type, &res_value[l]), 512)) ++err;
        }

        for (l = res_desc->Bytes / sizeof(*favalue); l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
        {
            if (favalue[l] != *(FLOAT *)&cmp) ++err;
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloatArray failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (favalue[l] != *(FLOAT *)&cmp) ++err;
    }
    ok(!err, "%u - %s: GetFloatArray failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetVector(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[4];
    UINT l, err = 0;

    memset(fvalue, 0xab, sizeof(fvalue));
    hr = effect->lpVtbl->GetVector(effect, parameter, (D3DXVECTOR4 *)&fvalue);
    if (!res_desc->Elements &&
            (res_desc->Class == D3DXPC_SCALAR || res_desc->Class == D3DXPC_VECTOR) &&
            res_desc->Type == D3DXPT_INT && res_desc->Bytes == 4)
    {
        DWORD tmp;

        ok(hr == D3D_OK, "%u - %s: GetVector failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        tmp = (DWORD)(*(fvalue + 2) * INT_FLOAT_MULTI);
        tmp += ((DWORD)(*(fvalue + 1) * INT_FLOAT_MULTI)) << 8;
        tmp += ((DWORD)(*fvalue * INT_FLOAT_MULTI)) << 16;
        tmp += ((DWORD)(*(fvalue + 3) * INT_FLOAT_MULTI)) << 24;

        if (*res_value != tmp) ++err;
    }
    else if (!res_desc->Elements && (res_desc->Class == D3DXPC_SCALAR || res_desc->Class == D3DXPC_VECTOR))
    {
        ok(hr == D3D_OK, "%u - %s: GetVector failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (l = 0; l < res_desc->Columns; ++l)
        {
            if (!compare_float(fvalue[l], get_float(res_desc->Type, &res_value[l]), 512)) ++err;
        }

        for (l = res_desc->Columns; l < 4; ++l) if (fvalue[l] != 0.0f) ++err;
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetVector failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < 4; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
    }
    ok(!err, "%u - %s: GetVector failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetVectorArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    UINT l, k, element, err = 0;

    for (element = 0; element <= res_desc->Elements + 1; ++element)
    {
        memset(fvalue, 0xab, sizeof(fvalue));
        hr = effect->lpVtbl->GetVectorArray(effect, parameter, (D3DXVECTOR4 *)&fvalue, element);
        if (!element)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetVectorArray failed, got %#x, expected %#x\n", i, res_full_name, element, hr, D3D_OK);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else if (element <= res_desc->Elements && res_desc->Class == D3DXPC_VECTOR)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetVectorArray failed, got %#x, expected %#x\n", i, res_full_name, element, hr, D3D_OK);

            for (k = 0; k < element; ++k)
            {
                for (l = 0; l < res_desc->Columns; ++l)
                {
                    if (!compare_float(fvalue[l + k * 4], get_float(res_desc->Type,
                            &res_value[l + k * res_desc->Columns]), 512))
                        ++err;
                }

                for (l = res_desc->Columns; l < 4; ++l) if (fvalue[l + k * 4] != 0.0f) ++err;
            }

            for (l = element * 4; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else
        {
            ok(hr == D3DERR_INVALIDCALL, "%u - %s[%u]: GetVectorArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3DERR_INVALIDCALL);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        ok(!err, "%u - %s[%u]: GetVectorArray failed with %u errors\n", i, res_full_name, element, err);
    }
}

static void test_effect_parameter_value_GetMatrix(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[16];
    UINT l, k, err = 0;

    memset(fvalue, 0xab, sizeof(fvalue));
    hr = effect->lpVtbl->GetMatrix(effect, parameter, (D3DXMATRIX *)&fvalue);
    if (!res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetMatrix failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (k = 0; k < 4; ++k)
        {
            for (l = 0; l < 4; ++l)
            {
                if (k < res_desc->Columns && l < res_desc->Rows)
                {
                    if (!compare_float(fvalue[l * 4 + k], get_float(res_desc->Type,
                            &res_value[l * res_desc->Columns + k]), 512))
                        ++err;
                }
                else if (fvalue[l * 4 + k] != 0.0f) ++err;
            }
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrix failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < sizeof(fvalue) / sizeof(*fvalue); ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
    }
    ok(!err, "%u - %s: GetMatrix failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetMatrixArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    UINT l, k, m, element, err = 0;

    for (element = 0; element <= res_desc->Elements + 1; ++element)
    {
        memset(fvalue, 0xab, sizeof(fvalue));
        hr = effect->lpVtbl->GetMatrixArray(effect, parameter, (D3DXMATRIX *)&fvalue, element);
        if (!element)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixArray failed, got %#x, expected %#x\n", i, res_full_name, element, hr, D3D_OK);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else if (element <= res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixArray failed, got %#x, expected %#x\n", i, res_full_name, element, hr, D3D_OK);

            for (m = 0; m < element; ++m)
            {
                for (k = 0; k < 4; ++k)
                {
                    for (l = 0; l < 4; ++l)
                    {
                        if (k < res_desc->Columns && l < res_desc->Rows)
                        {
                            if (!compare_float(fvalue[m * 16 + l * 4 + k], get_float(res_desc->Type,
                                    &res_value[m * res_desc->Columns * res_desc->Rows + l * res_desc->Columns + k]), 512))
                                ++err;
                        }
                        else if (fvalue[m * 16 + l * 4 + k] != 0.0f) ++err;
                    }
                }
            }

            for (l = element * 16; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else
        {
            ok(hr == D3DERR_INVALIDCALL, "%u - %s[%u]: GetMatrixArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3DERR_INVALIDCALL);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        ok(!err, "%u - %s[%u]: GetMatrixArray failed with %u errors\n", i, res_full_name, element, err);
    }
}

static void test_effect_parameter_value_GetMatrixPointerArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    D3DXMATRIX *matrix_pointer_array[sizeof(fvalue)/sizeof(D3DXMATRIX)];
    UINT l, k, m, element, err = 0;

    for (element = 0; element <= res_desc->Elements + 1; ++element)
    {
        memset(fvalue, 0xab, sizeof(fvalue));
        for (l = 0; l < element; ++l)
        {
            matrix_pointer_array[l] = (D3DXMATRIX *)&fvalue[l * sizeof(**matrix_pointer_array) / sizeof(FLOAT)];
        }
        hr = effect->lpVtbl->GetMatrixPointerArray(effect, parameter, matrix_pointer_array, element);
        if (!element)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else if (element <= res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (m = 0; m < element; ++m)
            {
                for (k = 0; k < 4; ++k)
                {
                    for (l = 0; l < 4; ++l)
                    {
                        if (k < res_desc->Columns && l < res_desc->Rows)
                        {
                            if (!compare_float(fvalue[m * 16 + l * 4 + k], get_float(res_desc->Type,
                                    &res_value[m * res_desc->Columns * res_desc->Rows + l * res_desc->Columns + k]), 512))
                                ++err;
                        }
                        else if (fvalue[m * 16 + l * 4 + k] != 0.0f) ++err;
                    }
                }
            }

            for (l = element * 16; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else
        {
            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;

            ok(hr == D3DERR_INVALIDCALL, "%u - %s[%u]: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3DERR_INVALIDCALL);
        }
        ok(!err, "%u - %s[%u]: GetMatrixPointerArray failed with %u errors\n", i, res_full_name, element, err);
    }
}

static void test_effect_parameter_value_GetMatrixTranspose(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[16];
    UINT l, k, err = 0;

    memset(fvalue, 0xab, sizeof(fvalue));
    hr = effect->lpVtbl->GetMatrixTranspose(effect, parameter, (D3DXMATRIX *)&fvalue);
    if (!res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        ok(hr == D3D_OK, "%u - %s: GetMatrixTranspose failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (k = 0; k < 4; ++k)
        {
            for (l = 0; l < 4; ++l)
            {
                if (k < res_desc->Columns && l < res_desc->Rows)
                {
                    if (!compare_float(fvalue[l + k * 4], get_float(res_desc->Type,
                            &res_value[l * res_desc->Columns + k]), 512))
                        ++err;
                }
                else if (fvalue[l + k * 4] != 0.0f) ++err;
            }
        }
    }
    else if (!res_desc->Elements && (res_desc->Class == D3DXPC_VECTOR || res_desc->Class == D3DXPC_SCALAR))
    {
        ok(hr == D3D_OK, "%u - %s: GetMatrixTranspose failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

        for (k = 0; k < 4; ++k)
        {
            for (l = 0; l < 4; ++l)
            {
                if (k < res_desc->Columns && l < res_desc->Rows)
                {
                    if (!compare_float(fvalue[l * 4 + k], get_float(res_desc->Type,
                            &res_value[l * res_desc->Columns + k]), 512))
                        ++err;
                }
                else if (fvalue[l * 4 + k] != 0.0f) ++err;
            }
        }
    }
    else
    {
        ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTranspose failed, got %#x, expected %#x\n",
                i, res_full_name, hr, D3DERR_INVALIDCALL);

        for (l = 0; l < sizeof(fvalue) / sizeof(*fvalue); ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
    }
    ok(!err, "%u - %s: GetMatrixTranspose failed with %u errors\n", i, res_full_name, err);
}

static void test_effect_parameter_value_GetMatrixTransposeArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    UINT l, k, m, element, err = 0;

    for (element = 0; element <= res_desc->Elements + 1; ++element)
    {
        memset(fvalue, 0xab, sizeof(fvalue));
        hr = effect->lpVtbl->GetMatrixTransposeArray(effect, parameter, (D3DXMATRIX *)&fvalue, element);
        if (!element)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else if (element <= res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (m = 0; m < element; ++m)
            {
                for (k = 0; k < 4; ++k)
                {
                    for (l = 0; l < 4; ++l)
                    {
                        if (k < res_desc->Columns && l < res_desc->Rows)
                        {
                            if (!compare_float(fvalue[m * 16 + l + k * 4], get_float(res_desc->Type,
                                    &res_value[m * res_desc->Columns * res_desc->Rows + l * res_desc->Columns + k]), 512))
                                ++err;
                        }
                        else if (fvalue[m * 16 + l + k * 4] != 0.0f) ++err;
                    }
                }
            }

            for (l = element * 16; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else
        {
            ok(hr == D3DERR_INVALIDCALL, "%u - %s[%u]: GetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3DERR_INVALIDCALL);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        ok(!err, "%u - %s[%u]: GetMatrixTransposeArray failed with %u errors\n", i, res_full_name, element, err);
    }
}

static void test_effect_parameter_value_GetMatrixTransposePointerArray(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;
    DWORD cmp = 0xabababab;
    FLOAT fvalue[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
    D3DXMATRIX *matrix_pointer_array[sizeof(fvalue)/sizeof(D3DXMATRIX)];
    UINT l, k, m, element, err = 0;

    for (element = 0; element <= res_desc->Elements + 1; ++element)
    {
        memset(fvalue, 0xab, sizeof(fvalue));
        for (l = 0; l < element; ++l)
        {
            matrix_pointer_array[l] = (D3DXMATRIX *)&fvalue[l * sizeof(**matrix_pointer_array) / sizeof(FLOAT)];
        }
        hr = effect->lpVtbl->GetMatrixTransposePointerArray(effect, parameter, matrix_pointer_array, element);
        if (!element)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else if (element <= res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
        {
            ok(hr == D3D_OK, "%u - %s[%u]: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3D_OK);

            for (m = 0; m < element; ++m)
            {
                for (k = 0; k < 4; ++k)
                {
                    for (l = 0; l < 4; ++l)
                    {
                        if (k < res_desc->Columns && l < res_desc->Rows)
                        {
                            if (!compare_float(fvalue[m * 16 + l + k * 4], get_float(res_desc->Type,
                                    &res_value[m * res_desc->Columns * res_desc->Rows + l * res_desc->Columns + k]), 512))
                                ++err;
                        }
                        else if (fvalue[m * 16 + l + k * 4] != 0.0f) ++err;
                    }
                }
            }

            for (l = element * 16; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        else
        {
            ok(hr == D3DERR_INVALIDCALL, "%u - %s[%u]: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, element, hr, D3DERR_INVALIDCALL);

            for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l) if (fvalue[l] != *(FLOAT *)&cmp) ++err;
        }
        ok(!err, "%u - %s[%u]: GetMatrixTransposePointerArray failed with %u errors\n", i, res_full_name, element, err);
    }
}

static void test_effect_parameter_value_GetTestGroup(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    test_effect_parameter_value_GetValue(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetBool(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetBoolArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetInt(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetIntArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetFloat(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetFloatArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetVector(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetVectorArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrix(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrixArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrixPointerArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrixTranspose(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrixTransposeArray(res, effect, res_value, parameter, i);
    test_effect_parameter_value_GetMatrixTransposePointerArray(res, effect, res_value, parameter, i);
}

static void test_effect_parameter_value_ResetValue(const struct test_effect_parameter_value_result *res,
        ID3DXEffect *effect, const DWORD *res_value, D3DXHANDLE parameter, UINT i)
{
    const D3DXPARAMETER_DESC *res_desc = &res->desc;
    const char *res_full_name = res->full_name;
    HRESULT hr;

    if (res_desc->Class == D3DXPC_SCALAR
            || res_desc->Class == D3DXPC_VECTOR
            || res_desc->Class == D3DXPC_MATRIX_ROWS)
    {
        hr = effect->lpVtbl->SetValue(effect, parameter, res_value, res_desc->Bytes);
        ok(hr == D3D_OK, "%u - %s: SetValue failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
    }
    else
    {
        /* nothing to do */
        switch (res_desc->Type)
        {
            case D3DXPT_PIXELSHADER:
            case D3DXPT_VERTEXSHADER:
            case D3DXPT_TEXTURE2D:
            case D3DXPT_STRING:
                break;

            default:
                ok(0, "Type is %u, this should not happen!\n", res_desc->Type);
                break;
        }
    }
}

static void test_effect_parameter_value(IDirect3DDevice9 *device)
{
    UINT i;
    UINT effect_count = sizeof(test_effect_parameter_value_data) / sizeof(*test_effect_parameter_value_data);

    for (i = 0; i < effect_count; ++i)
    {
        const struct test_effect_parameter_value_result *res = test_effect_parameter_value_data[i].res;
        UINT res_count = test_effect_parameter_value_data[i].res_count;
        const DWORD *blob = test_effect_parameter_value_data[i].blob;
        UINT blob_size = test_effect_parameter_value_data[i].blob_size;
        HRESULT hr;
        ID3DXEffect *effect;
        D3DXEFFECT_DESC edesc;
        ULONG count;
        UINT k;

        hr = D3DXCreateEffect(device, blob, blob_size, NULL, NULL, 0, NULL, &effect, NULL);
        ok(hr == D3D_OK, "%u: D3DXCreateEffect failed, got %#x, expected %#x\n", i, hr, D3D_OK);

        hr = effect->lpVtbl->GetDesc(effect, &edesc);
        ok(hr == D3D_OK, "%u: GetDesc failed, got %#x, expected %#x\n", i, hr, D3D_OK);
        ok(edesc.Parameters == res_count, "%u: Parameters failed, got %u, expected %u\n",
                i, edesc.Parameters, res_count);

        for (k = 0; k < res_count; ++k)
        {
            const D3DXPARAMETER_DESC *res_desc = &res[k].desc;
            const char *res_full_name = res[k].full_name;
            UINT res_value_offset = res[k].value_offset;
            D3DXHANDLE parameter;
            D3DXPARAMETER_DESC pdesc;
            BOOL bvalue = TRUE;
            INT ivalue = 42;
            FLOAT fvalue = 2.71828f;
            DWORD input_value[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
            DWORD expected_value[EFFECT_PARAMETER_VALUE_ARRAY_SIZE];
            UINT l, n, m, element;
            const D3DXMATRIX *matrix_pointer_array[sizeof(input_value)/sizeof(D3DXMATRIX)];

            parameter = effect->lpVtbl->GetParameterByName(effect, NULL, res_full_name);
            ok(parameter != NULL, "%u - %s: GetParameterByName failed\n", i, res_full_name);

            hr = effect->lpVtbl->GetParameterDesc(effect, parameter, &pdesc);
            ok(hr == D3D_OK, "%u - %s: GetParameterDesc failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);

            ok(res_desc->Name ? !strcmp(pdesc.Name, res_desc->Name) : !pdesc.Name,
                    "%u - %s: GetParameterDesc Name failed, got \"%s\", expected \"%s\"\n",
                    i, res_full_name, pdesc.Name, res_desc->Name);
            ok(res_desc->Semantic ? !strcmp(pdesc.Semantic, res_desc->Semantic) : !pdesc.Semantic,
                    "%u - %s: GetParameterDesc Semantic failed, got \"%s\", expected \"%s\"\n",
                    i, res_full_name, pdesc.Semantic, res_desc->Semantic);
            ok(res_desc->Class == pdesc.Class, "%u - %s: GetParameterDesc Class failed, got %#x, expected %#x\n",
                    i, res_full_name, pdesc.Class, res_desc->Class);
            ok(res_desc->Type == pdesc.Type, "%u - %s: GetParameterDesc Type failed, got %#x, expected %#x\n",
                    i, res_full_name, pdesc.Type, res_desc->Type);
            ok(res_desc->Rows == pdesc.Rows, "%u - %s: GetParameterDesc Rows failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.Rows, res_desc->Rows);
            ok(res_desc->Columns == pdesc.Columns, "%u - %s: GetParameterDesc Columns failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.Columns, res_desc->Columns);
            ok(res_desc->Elements == pdesc.Elements, "%u - %s: GetParameterDesc Elements failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.Elements, res_desc->Elements);
            ok(res_desc->Annotations == pdesc.Annotations, "%u - %s: GetParameterDesc Annotations failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.Annotations, res_desc->Annotations);
            ok(res_desc->StructMembers == pdesc.StructMembers, "%u - %s: GetParameterDesc StructMembers failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.StructMembers, res_desc->StructMembers);
            ok(res_desc->Flags == pdesc.Flags, "%u - %s: GetParameterDesc Flags failed, got %u, expected %u\n",
                    i, res_full_name, pdesc.Flags, res_desc->Flags);
            ok(res_desc->Bytes == pdesc.Bytes, "%u - %s: GetParameterDesc Bytes, got %u, expected %u\n",
                    i, res_full_name, pdesc.Bytes, res_desc->Bytes);

            /* check size */
            ok(EFFECT_PARAMETER_VALUE_ARRAY_SIZE >= res_desc->Bytes / 4 +
                    (res_desc->Elements ? res_desc->Bytes / 4 / res_desc->Elements : 0),
                    "%u - %s: Warning: Array size too small\n", i, res_full_name);

            test_effect_parameter_value_GetTestGroup(&res[k], effect, &blob[res_value_offset], parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            test_effect_parameter_value_GetTestGroup(&res[k], effect, &blob[res_value_offset], parameter, i);

            /*
             * check invalid calls
             * These will crash:
             * effect->lpVtbl->SetBoolArray(effect, parameter, NULL, res_desc->Bytes / sizeof(BOOL));
             * effect->lpVtbl->SetIntArray(effect, parameter, NULL, res_desc->Bytes / sizeof(INT));
             * effect->lpVtbl->SetFloatArray(effect, parameter, NULL, res_desc->Bytes / sizeof(FLOAT));
             * effect->lpVtbl->SetVector(effect, parameter, NULL);
             * effect->lpVtbl->SetVectorArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
             * effect->lpVtbl->SetMatrix(effect, parameter, NULL);
             * effect->lpVtbl->GetMatrix(effect, parameter, NULL);
             * effect->lpVtbl->SetMatrixArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
             * effect->lpVtbl->SetMatrixPointerArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
             * effect->lpVtbl->SetMatrixTranspose(effect, parameter, NULL);
             * effect->lpVtbl->SetMatrixTransposeArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
             * effect->lpVtbl->SetMatrixTransposePointerArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
             * effect->lpVtbl->GetValue(effect, parameter, NULL, res_desc->Bytes);
             * effect->lpVtbl->SetValue(effect, parameter, NULL, res_desc->Bytes);
             */
            hr = effect->lpVtbl->SetBool(effect, NULL, bvalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetBool failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetBool(effect, NULL, &bvalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBool failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetBool(effect, parameter, NULL);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBool failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetBoolArray(effect, NULL, (BOOL *)input_value, res_desc->Bytes / sizeof(BOOL));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetBoolArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetBoolArray(effect, NULL, (BOOL *)input_value, res_desc->Bytes / sizeof(BOOL));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBoolArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetBoolArray(effect, parameter, NULL, res_desc->Bytes / sizeof(BOOL));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetBoolArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetInt(effect, NULL, ivalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetInt failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetInt(effect, NULL, &ivalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetInt failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetInt(effect, parameter, NULL);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetInt failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetIntArray(effect, NULL, (INT *)input_value, res_desc->Bytes / sizeof(INT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetIntArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetIntArray(effect, NULL, (INT *)input_value, res_desc->Bytes / sizeof(INT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetIntArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetIntArray(effect, parameter, NULL, res_desc->Bytes / sizeof(INT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetIntArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetFloat(effect, NULL, fvalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetFloat failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetFloat(effect, NULL, &fvalue);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloat failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetFloat(effect, parameter, NULL);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloat failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetFloatArray(effect, NULL, (FLOAT *)input_value, res_desc->Bytes / sizeof(FLOAT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetFloatArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetFloatArray(effect, NULL, (FLOAT *)input_value, res_desc->Bytes / sizeof(FLOAT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloatArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetFloatArray(effect, parameter, NULL, res_desc->Bytes / sizeof(FLOAT));
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetFloatArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetVector(effect, NULL, (D3DXVECTOR4 *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetVector failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetVector(effect, NULL, (D3DXVECTOR4 *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetVector failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetVector(effect, parameter, NULL);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetVector failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetVectorArray(effect, NULL, (D3DXVECTOR4 *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetVectorArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetVectorArray(effect, NULL, (D3DXVECTOR4 *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetVectorArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetVectorArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetVectorArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrix(effect, NULL, (D3DXMATRIX *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrix failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrix(effect, NULL, (D3DXMATRIX *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrix failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixArray(effect, NULL, (D3DXMATRIX *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixArray(effect, NULL, (D3DXMATRIX *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixPointerArray(effect, NULL, matrix_pointer_array, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixPointerArray(effect, NULL, matrix_pointer_array, 0);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixPointerArray(effect, NULL, NULL, 0);
            ok(hr == D3D_OK, "%u - %s: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3D_OK);

            hr = effect->lpVtbl->GetMatrixPointerArray(effect, NULL, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixPointerArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixPointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixTranspose(effect, NULL, (D3DXMATRIX *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTranspose failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTranspose(effect, NULL, (D3DXMATRIX *)input_value);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTranspose failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTranspose(effect, parameter, NULL);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTranspose failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixTransposeArray(effect, NULL, (D3DXMATRIX *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTransposeArray(effect, NULL, (D3DXMATRIX *)input_value, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTransposeArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTransposeArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixTransposePointerArray(effect, NULL, matrix_pointer_array, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetMatrixTransposePointerArray(effect, NULL, matrix_pointer_array, 0);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTransposePointerArray(effect, NULL, NULL, 0);
            ok(hr == D3D_OK, "%u - %s: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3D_OK);

            hr = effect->lpVtbl->GetMatrixTransposePointerArray(effect, NULL, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetMatrixTransposePointerArray(effect, parameter, NULL, res_desc->Elements ? res_desc->Elements : 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetValue(effect, NULL, input_value, res_desc->Bytes);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetValue failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->SetValue(effect, parameter, input_value, res_desc->Bytes - 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetValue failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetValue(effect, NULL, input_value, res_desc->Bytes);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetValue failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            hr = effect->lpVtbl->GetValue(effect, parameter, input_value, res_desc->Bytes - 1);
            ok(hr == D3DERR_INVALIDCALL, "%u - %s: GetValue failed, got %#x, expected %#x\n",
                    i, res_full_name, hr, D3DERR_INVALIDCALL);

            test_effect_parameter_value_GetTestGroup(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetBool */
            bvalue = 5;
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetBool(effect, parameter, bvalue);
            if (!res_desc->Elements && res_desc->Rows == 1 && res_desc->Columns == 1)
            {
                bvalue = TRUE;
                set_number(expected_value, res_desc->Type, &bvalue, D3DXPT_BOOL);
                ok(hr == D3D_OK, "%u - %s: SetBool failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetBool failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetBoolArray */
            *input_value = 1;
            for (l = 1; l < res_desc->Bytes / sizeof(*input_value); ++l)
            {
                *(input_value + l) = *(input_value + l - 1) + 1;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetBoolArray(effect, parameter, (BOOL *)input_value, res_desc->Bytes / sizeof(*input_value));
            if (res_desc->Class == D3DXPC_SCALAR
                    || res_desc->Class == D3DXPC_VECTOR
                    || res_desc->Class == D3DXPC_MATRIX_ROWS)
            {
                for (l = 0; l < res_desc->Bytes / sizeof(*input_value); ++l)
                {
                    set_number(expected_value + l, res_desc->Type, input_value + l, D3DXPT_BOOL);
                }
                ok(hr == D3D_OK, "%u - %s: SetBoolArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetBoolArray failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetInt */
            ivalue = 0x1fbf02ff;
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetInt(effect, parameter, ivalue);
            if (!res_desc->Elements && res_desc->Rows == 1 && res_desc->Columns == 1)
            {
                set_number(expected_value, res_desc->Type, &ivalue, D3DXPT_INT);
                ok(hr == D3D_OK, "%u - %s: SetInt failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else if(!res_desc->Elements && res_desc->Type == D3DXPT_FLOAT &&
                    ((res_desc->Class == D3DXPC_VECTOR && res_desc->Columns != 2) ||
                    (res_desc->Class == D3DXPC_MATRIX_ROWS && res_desc->Rows != 2 && res_desc->Columns == 1)))
            {
                FLOAT tmp = ((ivalue & 0xff0000) >> 16) * INT_FLOAT_MULTI_INVERSE;
                set_number(expected_value, res_desc->Type, &tmp, D3DXPT_FLOAT);
                tmp = ((ivalue & 0xff00) >> 8) * INT_FLOAT_MULTI_INVERSE;
                set_number(expected_value + 1, res_desc->Type, &tmp, D3DXPT_FLOAT);
                tmp = (ivalue & 0xff) * INT_FLOAT_MULTI_INVERSE;
                set_number(expected_value + 2, res_desc->Type, &tmp, D3DXPT_FLOAT);
                tmp = ((ivalue & 0xff000000) >> 24) * INT_FLOAT_MULTI_INVERSE;
                set_number(expected_value + 3, res_desc->Type, &tmp, D3DXPT_FLOAT);

                ok(hr == D3D_OK, "%u - %s: SetInt failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetInt failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetIntArray */
            *input_value = 123456;
            for (l = 0; l < res_desc->Bytes / sizeof(*input_value); ++l)
            {
                *(input_value + l) = *(input_value + l - 1) + 23;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetIntArray(effect, parameter, (INT *)input_value, res_desc->Bytes / sizeof(*input_value));
            if (res_desc->Class == D3DXPC_SCALAR
                    || res_desc->Class == D3DXPC_VECTOR
                    || res_desc->Class == D3DXPC_MATRIX_ROWS)
            {
                for (l = 0; l < res_desc->Bytes / sizeof(*input_value); ++l)
                {
                    set_number(expected_value + l, res_desc->Type, input_value + l, D3DXPT_INT);
                }
                ok(hr == D3D_OK, "%u - %s: SetIntArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetIntArray failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetFloat */
            fvalue = 1.33;
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetFloat(effect, parameter, fvalue);
            if (!res_desc->Elements && res_desc->Rows == 1 && res_desc->Columns == 1)
            {
                set_number(expected_value, res_desc->Type, &fvalue, D3DXPT_FLOAT);
                ok(hr == D3D_OK, "%u - %s: SetFloat failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetFloat failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetFloatArray */
            fvalue = 1.33;
            for (l = 0; l < res_desc->Bytes / sizeof(fvalue); ++l)
            {
                *(input_value + l) = *(DWORD *)&fvalue;
                fvalue += 1.12;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetFloatArray(effect, parameter, (FLOAT *)input_value, res_desc->Bytes / sizeof(*input_value));
            if (res_desc->Class == D3DXPC_SCALAR
                    || res_desc->Class == D3DXPC_VECTOR
                    || res_desc->Class == D3DXPC_MATRIX_ROWS)
            {
                for (l = 0; l < res_desc->Bytes / sizeof(*input_value); ++l)
                {
                    set_number(expected_value + l, res_desc->Type, input_value + l, D3DXPT_FLOAT);
                }
                ok(hr == D3D_OK, "%u - %s: SetFloatArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetFloatArray failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetVector */
            fvalue = -1.33;
            for (l = 0; l < 4; ++l)
            {
                *(input_value + l) = *(DWORD *)&fvalue;
                fvalue += 1.12;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetVector(effect, parameter, (D3DXVECTOR4 *)input_value);
            if (!res_desc->Elements &&
                    (res_desc->Class == D3DXPC_SCALAR
                    || res_desc->Class == D3DXPC_VECTOR))
            {
                /* only values between 0 and INT_FLOAT_MULTI are valid */
                if (res_desc->Type == D3DXPT_INT && res_desc->Bytes == 4)
                {
                    *expected_value = (DWORD)(max(min(*(FLOAT *)(input_value + 2), 1.0f), 0.0f) * INT_FLOAT_MULTI);
                    *expected_value += ((DWORD)(max(min(*(FLOAT *)(input_value + 1), 1.0f), 0.0f) * INT_FLOAT_MULTI)) << 8;
                    *expected_value += ((DWORD)(max(min(*(FLOAT *)input_value, 1.0f), 0.0f) * INT_FLOAT_MULTI)) << 16;
                    *expected_value += ((DWORD)(max(min(*(FLOAT *)(input_value + 3), 1.0f), 0.0f) * INT_FLOAT_MULTI)) << 24;
                }
                else
                {
                    for (l = 0; l < 4; ++l)
                    {
                        set_number(expected_value + l, res_desc->Type, input_value + l, D3DXPT_FLOAT);
                    }
                }
                ok(hr == D3D_OK, "%u - %s: SetVector failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetVector failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetVectorArray */
            for (element = 0; element < res_desc->Elements + 1; ++element)
            {
                fvalue = 1.33;
                for (l = 0; l < element * 4; ++l)
                {
                    *(input_value + l) = *(DWORD *)&fvalue;
                    fvalue += 1.12;
                }
                memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
                hr = effect->lpVtbl->SetVectorArray(effect, parameter, (D3DXVECTOR4 *)input_value, element);
                if (res_desc->Elements && res_desc->Class == D3DXPC_VECTOR && element <= res_desc->Elements)
                {
                    for (m = 0; m < element; ++m)
                    {
                        for (l = 0; l < res_desc->Columns; ++l)
                        {
                            set_number(expected_value + m * res_desc->Columns + l, res_desc->Type, input_value + m * 4 + l, D3DXPT_FLOAT);
                        }
                    }
                    ok(hr == D3D_OK, "%u - %s: SetVectorArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
                }
                else
                {
                    ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetVectorArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3DERR_INVALIDCALL);
                }
                test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
                test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            }

            /* SetMatrix */
            fvalue = 1.33;
            for (l = 0; l < 16; ++l)
            {
                *(input_value + l) = *(DWORD *)&fvalue;
                fvalue += 1.12;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetMatrix(effect, parameter, (D3DXMATRIX *)input_value);
            if (!res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
            {
                for (l = 0; l < 4; ++l)
                {
                    for (m = 0; m < 4; ++m)
                    {
                        if (m < res_desc->Rows && l < res_desc->Columns)
                            set_number(expected_value + l + m * res_desc->Columns, res_desc->Type,
                                    input_value + l + m * 4, D3DXPT_FLOAT);
                    }

                }
                ok(hr == D3D_OK, "%u - %s: SetMatrix failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrix failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetMatrixArray */
            for (element = 0; element < res_desc->Elements + 1; ++element)
            {
                fvalue = 1.33;
                for (l = 0; l < element * 16; ++l)
                {
                    *(input_value + l) = *(DWORD *)&fvalue;
                    fvalue += 1.12;
                }
                memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
                hr = effect->lpVtbl->SetMatrixArray(effect, parameter, (D3DXMATRIX *)input_value, element);
                if (res_desc->Class == D3DXPC_MATRIX_ROWS && element <= res_desc->Elements)
                {
                    for (n = 0; n < element; ++n)
                    {
                        for (l = 0; l < 4; ++l)
                        {
                            for (m = 0; m < 4; ++m)
                            {
                                if (m < res_desc->Rows && l < res_desc->Columns)
                                    set_number(expected_value + l + m * res_desc->Columns + n * res_desc->Columns * res_desc->Rows,
                                            res_desc->Type, input_value + l + m * 4 + n * 16, D3DXPT_FLOAT);
                            }

                        }
                    }
                    ok(hr == D3D_OK, "%u - %s: SetMatrixArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
                }
                else
                {
                    ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3DERR_INVALIDCALL);
                }
                test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
                test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            }

            /* SetMatrixPointerArray */
            for (element = 0; element < res_desc->Elements + 1; ++element)
            {
                fvalue = 1.33;
                for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
                {
                    *(input_value + l) = *(DWORD *)&fvalue;
                    fvalue += 1.12;
                }
                memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
                for (l = 0; l < element; ++l)
                {
                    matrix_pointer_array[l] = (D3DXMATRIX *)&input_value[l * sizeof(**matrix_pointer_array) / sizeof(FLOAT)];
                }
                hr = effect->lpVtbl->SetMatrixPointerArray(effect, parameter, matrix_pointer_array, element);
                if (res_desc->Class == D3DXPC_MATRIX_ROWS && res_desc->Elements >= element)
                {
                    for (n = 0; n < element; ++n)
                    {
                        for (l = 0; l < 4; ++l)
                        {
                            for (m = 0; m < 4; ++m)
                            {
                                if (m < res_desc->Rows && l < res_desc->Columns)
                                    set_number(expected_value + l + m * res_desc->Columns + n * res_desc->Columns * res_desc->Rows,
                                            res_desc->Type, input_value + l + m * 4 + n * 16, D3DXPT_FLOAT);
                            }

                        }
                    }
                    ok(hr == D3D_OK, "%u - %s: SetMatrixPointerArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3D_OK);
                }
                else
                {
                    ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixPointerArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3DERR_INVALIDCALL);
                }
                test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
                test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            }

            /* SetMatrixTranspose */
            fvalue = 1.33;
            for (l = 0; l < 16; ++l)
            {
                *(input_value + l) = *(DWORD *)&fvalue;
                fvalue += 1.12;
            }
            memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
            hr = effect->lpVtbl->SetMatrixTranspose(effect, parameter, (D3DXMATRIX *)input_value);
            if (!res_desc->Elements && res_desc->Class == D3DXPC_MATRIX_ROWS)
            {
                for (l = 0; l < 4; ++l)
                {
                    for (m = 0; m < 4; ++m)
                    {
                        if (m < res_desc->Rows && l < res_desc->Columns)
                            set_number(expected_value + l + m * res_desc->Columns, res_desc->Type,
                                    input_value + l * 4 + m, D3DXPT_FLOAT);
                    }

                }
                ok(hr == D3D_OK, "%u - %s: SetMatrixTranspose failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
            }
            else
            {
                ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTranspose failed, got %#x, expected %#x\n",
                        i, res_full_name, hr, D3DERR_INVALIDCALL);
            }
            test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
            test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);

            /* SetMatrixTransposeArray */
            for (element = 0; element < res_desc->Elements + 1; ++element)
            {
                fvalue = 1.33;
                for (l = 0; l < element * 16; ++l)
                {
                    *(input_value + l) = *(DWORD *)&fvalue;
                    fvalue += 1.12;
                }
                memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
                hr = effect->lpVtbl->SetMatrixTransposeArray(effect, parameter, (D3DXMATRIX *)input_value, element);
                if (res_desc->Class == D3DXPC_MATRIX_ROWS && element <= res_desc->Elements)
                {
                    for (n = 0; n < element; ++n)
                    {
                        for (l = 0; l < 4; ++l)
                        {
                            for (m = 0; m < 4; ++m)
                            {
                                if (m < res_desc->Rows && l < res_desc->Columns)
                                    set_number(expected_value + l + m * res_desc->Columns + n * res_desc->Columns * res_desc->Rows,
                                            res_desc->Type, input_value + l * 4 + m + n * 16, D3DXPT_FLOAT);
                            }

                        }
                    }
                    ok(hr == D3D_OK, "%u - %s: SetMatrixTransposeArray failed, got %#x, expected %#x\n", i, res_full_name, hr, D3D_OK);
                }
                else
                {
                    ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTransposeArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3DERR_INVALIDCALL);
                }
                test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
                test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            }

            /* SetMatrixTransposePointerArray */
            for (element = 0; element < res_desc->Elements + 1; ++element)
            {
                fvalue = 1.33;
                for (l = 0; l < EFFECT_PARAMETER_VALUE_ARRAY_SIZE; ++l)
                {
                    *(input_value + l) = *(DWORD *)&fvalue;
                    fvalue += 1.12;
                }
                memcpy(expected_value, &blob[res_value_offset], res_desc->Bytes);
                for (l = 0; l < element; ++l)
                {
                    matrix_pointer_array[l] = (D3DXMATRIX *)&input_value[l * sizeof(**matrix_pointer_array) / sizeof(FLOAT)];
                }
                hr = effect->lpVtbl->SetMatrixTransposePointerArray(effect, parameter, matrix_pointer_array, element);
                if (res_desc->Class == D3DXPC_MATRIX_ROWS && res_desc->Elements >= element)
                {
                    for (n = 0; n < element; ++n)
                    {
                        for (l = 0; l < 4; ++l)
                        {
                            for (m = 0; m < 4; ++m)
                            {
                                if (m < res_desc->Rows && l < res_desc->Columns)
                                    set_number(expected_value + l + m * res_desc->Columns + n * res_desc->Columns * res_desc->Rows,
                                            res_desc->Type, input_value + l * 4 + m + n * 16, D3DXPT_FLOAT);
                            }

                        }
                    }
                    ok(hr == D3D_OK, "%u - %s: SetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3D_OK);
                }
                else
                {
                    ok(hr == D3DERR_INVALIDCALL, "%u - %s: SetMatrixTransposePointerArray failed, got %#x, expected %#x\n",
                            i, res_full_name, hr, D3DERR_INVALIDCALL);
                }
                test_effect_parameter_value_GetTestGroup(&res[k], effect, expected_value, parameter, i);
                test_effect_parameter_value_ResetValue(&res[k], effect, &blob[res_value_offset], parameter, i);
            }
        }

        count = effect->lpVtbl->Release(effect);
        ok(!count, "Release failed %u\n", count);
    }
}

static void test_effect_setvalue_object(IDirect3DDevice9 *device)
{
    ID3DXEffect *effect;
    D3DXHANDLE parameter;
    IDirect3DTexture9 *texture;
    IDirect3DTexture9 *texture_set;
    HRESULT hr;
    ULONG count;

    hr = D3DXCreateEffect(device, test_effect_parameter_value_blob_object,
            sizeof(test_effect_parameter_value_blob_object), NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "Got result %#x, expected 0 (D3D_OK).\n", hr);

    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "tex");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    texture = NULL;
    hr = D3DXCreateTexture(device, D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, 0, D3DPOOL_DEFAULT, &texture);
    ok(hr == D3D_OK, "Got result %#x, expected 0 (D3D_OK).\n", hr);
    hr = effect->lpVtbl->SetValue(effect, parameter, &texture, sizeof(texture));
    ok(hr == D3D_OK, "Got result %#x, expected 0 (D3D_OK).\n", hr);
    texture_set = NULL;
    hr = effect->lpVtbl->GetValue(effect, parameter, &texture_set, sizeof(texture_set));
    ok(hr == D3D_OK, "Got result %#x, expected 0 (D3D_OK).\n", hr);
    ok(texture == texture_set, "Texture does not match.\n");

    count = IDirect3DTexture9_Release(texture_set);
    ok(count == 2, "Got reference count %u, expected 2.\n", count);
    texture_set = NULL;
    hr = effect->lpVtbl->SetValue(effect, parameter, &texture_set, sizeof(texture_set));
    ok(hr == D3D_OK, "Got result %#x, expected 0 (D3D_OK).\n", hr);
    count = IDirect3DTexture9_Release(texture);
    ok(!count, "Got reference count %u, expected 0.\n", count);

    effect->lpVtbl->Release(effect);
}

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
float a = 2.1;
float b[1];
float c <float d = 3;>;
struct {float e;} f;
float g <float h[1] = {3};>;
struct s {float j;};
float i <s k[1] = {4};>;
technique t <s l[1] = {5};> { pass p <s m[1] = {6};> { } }
#endif
static const DWORD test_effect_variable_names_blob[] =
{
0xfeff0901, 0x0000024c, 0x00000000, 0x00000003, 0x00000000, 0x00000024, 0x00000000, 0x00000000,
0x00000001, 0x00000001, 0x40066666, 0x00000002, 0x00000061, 0x00000003, 0x00000000, 0x0000004c,
0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000000, 0x00000002, 0x00000062, 0x00000003,
0x00000000, 0x0000009c, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000, 0x40400000,
0x00000003, 0x00000000, 0x00000094, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000002,
0x00000064, 0x00000002, 0x00000063, 0x00000000, 0x00000005, 0x000000dc, 0x00000000, 0x00000000,
0x00000001, 0x00000003, 0x00000000, 0x000000e4, 0x00000000, 0x00000000, 0x00000001, 0x00000001,
0x00000000, 0x00000002, 0x00000066, 0x00000002, 0x00000065, 0x00000003, 0x00000000, 0x00000134,
0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000, 0x40400000, 0x00000003, 0x00000000,
0x0000012c, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000068, 0x00000002,
0x00000067, 0x00000003, 0x00000000, 0x000001a4, 0x00000000, 0x00000000, 0x00000001, 0x00000001,
0x00000000, 0x40800000, 0x00000000, 0x00000005, 0x00000194, 0x00000000, 0x00000001, 0x00000001,
0x00000003, 0x00000000, 0x0000019c, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000002,
0x0000006b, 0x00000002, 0x0000006a, 0x00000002, 0x00000069, 0x40a00000, 0x00000000, 0x00000005,
0x000001e4, 0x00000000, 0x00000001, 0x00000001, 0x00000003, 0x00000000, 0x000001ec, 0x00000000,
0x00000000, 0x00000001, 0x00000001, 0x00000002, 0x0000006c, 0x00000002, 0x0000006a, 0x40c00000,
0x00000000, 0x00000005, 0x0000022c, 0x00000000, 0x00000001, 0x00000001, 0x00000003, 0x00000000,
0x00000234, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000002, 0x0000006d, 0x00000002,
0x0000006a, 0x00000002, 0x00000070, 0x00000002, 0x00000074, 0x00000006, 0x00000001, 0x00000001,
0x00000001, 0x00000004, 0x00000020, 0x00000000, 0x00000000, 0x0000002c, 0x00000048, 0x00000000,
0x00000000, 0x00000054, 0x00000070, 0x00000000, 0x00000001, 0x00000078, 0x00000074, 0x000000a4,
0x000000d8, 0x00000000, 0x00000000, 0x000000ec, 0x00000108, 0x00000000, 0x00000001, 0x00000110,
0x0000010c, 0x0000013c, 0x00000158, 0x00000000, 0x00000001, 0x00000160, 0x0000015c, 0x00000244,
0x00000001, 0x00000001, 0x000001b0, 0x000001ac, 0x0000023c, 0x00000001, 0x00000000, 0x000001f8,
0x000001f4, 0x00000000, 0x00000000,
};

static void test_effect_variable_names(IDirect3DDevice9 *device)
{
    ID3DXEffect *effect;
    ULONG count;
    HRESULT hr;
    D3DXHANDLE parameter, p;

    hr = D3DXCreateEffect(device, test_effect_variable_names_blob,
            sizeof(test_effect_variable_names_blob), NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "D3DXCreateEffect failed, got %#x, expected %#x\n", hr, D3D_OK);

    /*
     * check invalid calls
     * This will crash:
     * effect->lpVtbl->GetAnnotationByName(effect, "invalid1", "invalid2");
     */
    p = effect->lpVtbl->GetParameterByName(effect, NULL, NULL);
    ok(p == NULL, "GetParameterByName failed, got %p, expected %p\n", p, NULL);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "invalid1");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "invalid1", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "invalid1", "invalid2");
    ok(p == NULL, "GetParameterByName failed, got %p, expected %p\n", p, NULL);

    /* float a; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "a");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "a", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    /* members */
    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a.");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a.", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a", ".");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a.invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a.invalid", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a", ".invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a.", "invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a", "invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    /* elements */
    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a[]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a[]", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a[0]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a[0]", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a", "[0]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterElement(effect, "a", 0);
    ok(p == NULL, "GetParameterElement failed, got %p\n", p);

    /* annotations */
    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a@");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a@", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a", "@invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a@", "invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "a@invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "a@invalid", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetAnnotationByName(effect, "a", NULL);
    ok(p == NULL, "GetAnnotationByName failed, got %p\n", p);

    p = effect->lpVtbl->GetAnnotationByName(effect, "a", "invalid");
    ok(p == NULL, "GetAnnotationByName failed, got %p\n", p);

    p = effect->lpVtbl->GetAnnotation(effect, "a", 0);
    ok(p == NULL, "GetAnnotation failed, got %p\n", p);

    /* float b[1]; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "b");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "b", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    /* elements */
    p = effect->lpVtbl->GetParameterByName(effect, NULL, "b[]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "b[0]");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "b[0]", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterElement(effect, "b", 0);
    ok(parameter == p, "GetParameterElement failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "b", "[0]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "b[1]");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterElement(effect, "b", 1);
    ok(p == NULL, "GetParameterElement failed, got %p\n", p);

    /* float c <float d = 3;>; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "c");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "c", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    /* annotations */
    p = effect->lpVtbl->GetParameterByName(effect, "c", "@d");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "c@", "d");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "c@invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "c@invalid", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetAnnotationByName(effect, "c", NULL);
    ok(p == NULL, "GetAnnotationByName failed, got %p\n", p);

    p = effect->lpVtbl->GetAnnotationByName(effect, "c", "invalid");
    ok(p == NULL, "GetAnnotationByName failed, got %p\n", p);

    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "c@d");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "c@d", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetAnnotationByName(effect, "c", "d");
    ok(parameter == p, "GetAnnotationByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetAnnotation(effect, "c", 0);
    ok(parameter == p, "GetAnnotation failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetAnnotation(effect, "c", 1);
    ok(p == NULL, "GetAnnotation failed, got %p\n", p);

    /* struct {float e;} f; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "f");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "f", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    /* members */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "f.e");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "f.e", NULL);
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "f", "e");
    ok(parameter == p, "GetParameterByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterByName(effect, "f", ".e");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "f.", "e");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, "f.invalid", NULL);
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    p = effect->lpVtbl->GetParameterByName(effect, NULL, "f.invalid");
    ok(p == NULL, "GetParameterByName failed, got %p\n", p);

    /* float g <float h[1] = {3};>; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "g@h[0]");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetAnnotationByName(effect, "g", "h[0]");
    ok(parameter == p, "GetAnnotationByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterElement(effect, "g@h", 0);
    ok(parameter == p, "GetParameterElement failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterElement(effect, effect->lpVtbl->GetAnnotation(effect, "g", 0), 0);
    ok(parameter == p, "GetParameterElement failed, got %p, expected %p\n", p, parameter);

    /* struct s {float j;}; float i <s k[1] = {4};>; */
    parameter = effect->lpVtbl->GetParameterByName(effect, NULL, "i@k[0].j");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    p = effect->lpVtbl->GetAnnotationByName(effect, "i", "k[0].j");
    ok(parameter == p, "GetAnnotationByName failed, got %p, expected %p\n", p, parameter);

    p = effect->lpVtbl->GetParameterByName(effect, effect->lpVtbl->GetParameterElement(effect, "i@k", 0), "j");
    ok(parameter == p, "GetParameterElement failed, got %p, expected %p\n", p, parameter);

    /* technique t <s l[1] = {5};> */
    parameter = effect->lpVtbl->GetAnnotationByName(effect, "t", "l[0].j");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    /* pass p <s m[1] = {6};> */
    parameter = effect->lpVtbl->GetAnnotationByName(effect, effect->lpVtbl->GetPassByName(effect, "t", "p"), "m[0].j");
    ok(parameter != NULL, "GetParameterByName failed, got %p\n", parameter);

    count = effect->lpVtbl->Release(effect);
    ok(!count, "Release failed %u\n", count);
}

static void test_effect_compilation_errors(IDirect3DDevice9 *device)
{
    ID3DXEffect *effect;
    ID3DXBuffer *compilation_errors;
    HRESULT hr;

    /* Test binary effect */
    compilation_errors = (ID3DXBuffer*)0xdeadbeef;
    hr = D3DXCreateEffect(NULL, NULL, 0, NULL, NULL, 0, NULL, NULL, &compilation_errors);
    ok(hr == D3DERR_INVALIDCALL, "D3DXCreateEffect failed, got %#x, expected %#x\n", hr, D3DERR_INVALIDCALL);
    ok(!compilation_errors, "Returned %p\n", compilation_errors);

    compilation_errors = (ID3DXBuffer*)0xdeadbeef;
    hr = D3DXCreateEffect(device, test_effect_variable_names_blob,
            sizeof(test_effect_variable_names_blob), NULL, NULL, 0, NULL, &effect, &compilation_errors);
    ok(hr == D3D_OK, "D3DXCreateEffect failed, got %#x, expected %#x\n", hr, D3D_OK);
    ok(!compilation_errors, "Returned %p\n", compilation_errors);
    effect->lpVtbl->Release(effect);
}

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
vertexshader vs_arr1[2] =
{
    asm
    {
        vs_1_1
        def c0, 1, 1, 1, 1
        mov oPos, c0
    },
    asm
    {
        vs_2_0
        def c0, 2, 2, 2, 2
        mov oPos, c0
    }
};

sampler sampler1 =
    sampler_state
    {
        MipFilter = LINEAR;
    };

float4x4 camera : VIEW = {4.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0, 0.0,0.0,0.0,6.0};
technique tech0
{
   pass p0
   {
       vertexshader = vs_arr1[1];
       VertexShaderConstant1[3] = {2,2,2,2};
       BlendOp = 2;
       AlphaOp[3] = 4;
       ZEnable = true;
       WorldTransform[1]={2.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0, 0.0,0.0,0.0,0.0, 0.0,0.0,0.0,4.0};
       ViewTransform=(camera);
       LightEnable[2] = TRUE;
       LightType[2] = POINT;
       LightPosition[2] = {4.0f, 5.0f, 6.0f};
       Sampler[1] = sampler1;
   }
}
#endif
static const DWORD test_effect_states_effect_blob[] =
{
    0xfeff0901, 0x000002e8, 0x00000000, 0x00000010, 0x00000004, 0x00000020, 0x00000000, 0x00000002,
    0x00000001, 0x00000002, 0x00000008, 0x615f7376, 0x00317272, 0x0000000a, 0x00000004, 0x00000074,
    0x00000000, 0x00000000, 0x00000002, 0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000001, 0x00000001, 0x000000ab, 0x00000100, 0x00000044, 0x00000040, 0x00000009,
    0x706d6173, 0x3172656c, 0x00000000, 0x00000003, 0x00000002, 0x000000e0, 0x000000ec, 0x00000000,
    0x00000004, 0x00000004, 0x40800000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x40c00000, 0x00000007, 0x656d6163, 0x00006172, 0x00000005, 0x57454956, 0x00000000,
    0x00000003, 0x00000010, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x40000000, 0x40000000,
    0x40000000, 0x40000000, 0x00000003, 0x00000003, 0x00000000, 0x00000000, 0x00000000, 0x00000004,
    0x00000001, 0x00000002, 0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000001, 0x00000004, 0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001,
    0x00000001, 0x40000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x40800000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000010, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000004, 0x00000001,
    0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001,
    0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x40800000,
    0x40a00000, 0x40c00000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
    0x00000001, 0x00000000, 0x0000000a, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
    0x00003070, 0x00000006, 0x68636574, 0x00000030, 0x00000003, 0x00000001, 0x00000005, 0x00000004,
    0x00000004, 0x00000018, 0x00000000, 0x00000000, 0x0000002c, 0x00000060, 0x00000000, 0x00000000,
    0x00000084, 0x000000a0, 0x00000000, 0x00000000, 0x000002dc, 0x00000000, 0x00000001, 0x000002d4,
    0x00000000, 0x0000000b, 0x00000092, 0x00000000, 0x000000fc, 0x000000f8, 0x00000098, 0x00000003,
    0x00000120, 0x00000110, 0x0000004b, 0x00000000, 0x00000140, 0x0000013c, 0x0000006b, 0x00000003,
    0x00000160, 0x0000015c, 0x00000000, 0x00000000, 0x00000180, 0x0000017c, 0x0000007d, 0x00000001,
    0x000001dc, 0x0000019c, 0x0000007c, 0x00000000, 0x00000238, 0x000001f8, 0x00000091, 0x00000002,
    0x00000258, 0x00000254, 0x00000084, 0x00000002, 0x00000278, 0x00000274, 0x00000088, 0x00000002,
    0x000002a0, 0x00000294, 0x000000b2, 0x00000001, 0x000002c0, 0x000002bc, 0x00000002, 0x00000003,
    0x00000001, 0x0000002c, 0xfffe0101, 0x00000051, 0xa00f0000, 0x3f800000, 0x3f800000, 0x3f800000,
    0x3f800000, 0x00000001, 0xc00f0000, 0xa0e40000, 0x0000ffff, 0x00000002, 0x0000002c, 0xfffe0200,
    0x05000051, 0xa00f0000, 0x40000000, 0x40000000, 0x40000000, 0x40000000, 0x02000001, 0xc00f0000,
    0xa0e40000, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x0000000a, 0x00000001, 0x00000009,
    0x706d6173, 0x3172656c, 0x00000000, 0x00000000, 0x00000000, 0xffffffff, 0x00000006, 0x00000000,
    0x0000016c, 0x46580200, 0x0030fffe, 0x42415443, 0x0000001c, 0x0000008b, 0x46580200, 0x00000001,
    0x0000001c, 0x20000100, 0x00000088, 0x00000030, 0x00000002, 0x00000004, 0x00000038, 0x00000048,
    0x656d6163, 0xab006172, 0x00030003, 0x00040004, 0x00000001, 0x00000000, 0x40800000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x40c00000, 0x4d007874, 0x6f726369,
    0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
    0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x0024fffe, 0x434c5846,
    0x00000004, 0x10000004, 0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004,
    0x00000000, 0x10000004, 0x00000001, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004,
    0x00000004, 0x10000004, 0x00000001, 0x00000000, 0x00000002, 0x00000008, 0x00000000, 0x00000004,
    0x00000008, 0x10000004, 0x00000001, 0x00000000, 0x00000002, 0x0000000c, 0x00000000, 0x00000004,
    0x0000000c, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
    0x00000001, 0x0000000b, 0x615f7376, 0x5b317272, 0x00005d31,
};
#define TEST_EFFECT_STATES_VSHADER_POS 271

static void test_effect_states(IDirect3DDevice9 *device)
{
    D3DMATRIX test_mat =
    {{{
        -1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f
    }}};
    D3DMATRIX test_mat_camera =
    {{{
        4.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 6.0f
    }}};
    D3DMATRIX test_mat_world1 =
    {{{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 4.0f
    }}};
    D3DMATRIX mat;
    HRESULT hr;
    ID3DXEffect *effect;
    UINT npasses;
    DWORD value;
    IDirect3DVertexShader9 *vshader;
    void *byte_code;
    UINT byte_code_size;
    BOOL bval;
    D3DLIGHT9 light;
    float float_data[4];

    hr = D3DXCreateEffect(device, test_effect_states_effect_blob, sizeof(test_effect_states_effect_blob),
            NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    /* State affected in passes saved/restored even if no pass
       was performed. States not present in passes are not saved &
       restored */
    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, 1);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAFUNC, 1);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = effect->lpVtbl->Begin(effect, &npasses, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(npasses == 1, "Expected 1 pass, got %u\n", npasses);

    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, 3);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHAFUNC, 2);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = effect->lpVtbl->End(effect);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_BLENDOP, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == 1, "Got result %u, expected %u.\n", value, 1);
    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_ALPHAFUNC, &value);
    ok(value == 2, "Got result %u, expected %u.\n", value, 2);

    /* Test states application in BeginPass. No states are restored
       on EndPass. */
    hr = IDirect3DDevice9_SetSamplerState(device, 1, D3DSAMP_MIPFILTER, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_GetLightEnable(device, 2, &bval);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    if (hr == D3D_OK)
        ok(!bval, "Got result %u, expected 0.\n", bval);

    hr = IDirect3DDevice9_SetTransform(device, D3DTS_WORLDMATRIX(1), &test_mat);
    hr = effect->lpVtbl->Begin(effect, &npasses, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_GetTransform(device, D3DTS_WORLDMATRIX(1), &mat);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(!memcmp(mat.m, test_mat.m, sizeof(mat)), "World matrix does not match.\n");

    hr = effect->lpVtbl->BeginPass(effect, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_GetTransform(device, D3DTS_WORLDMATRIX(1), &mat);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(!memcmp(mat.m, test_mat_world1.m, sizeof(mat)), "World matrix does not match.\n");

    hr = IDirect3DDevice9_GetTransform(device, D3DTS_VIEW, &mat);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(!memcmp(mat.m, test_mat_camera.m, sizeof(mat)), "View matrix does not match.\n");

    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_BLENDOP, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == 2, "Got result %u, expected %u\n", value, 2);

    hr = IDirect3DDevice9_GetVertexShader(device, &vshader);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(vshader != NULL, "Got NULL vshader.\n");
    if (vshader)
    {
        hr = IDirect3DVertexShader9_GetFunction(vshader, NULL, &byte_code_size);
        ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
        byte_code = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, byte_code_size);
        hr = IDirect3DVertexShader9_GetFunction(vshader, byte_code, &byte_code_size);
        ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
        ok(byte_code_size > 1, "Got unexpected byte code size %u.\n", byte_code_size);
        ok(!memcmp(byte_code, &test_effect_states_effect_blob[TEST_EFFECT_STATES_VSHADER_POS], byte_code_size),
            "Incorrect shader selected.\n");
        HeapFree(GetProcessHeap(), 0, byte_code);
        IDirect3DVertexShader9_Release(vshader);
    }

    hr = IDirect3DDevice9_GetLightEnable(device, 2, &bval);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    if (hr == D3D_OK)
        ok(bval, "Got result %u, expected TRUE.\n", bval);
    hr = IDirect3DDevice9_GetLight(device, 2, &light);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    if (hr == D3D_OK)
        ok(light.Position.x == 4.0f && light.Position.y == 5.0f && light.Position.z == 6.0f,
                "Got unexpected light position (%f, %f, %f).\n", light.Position.x, light.Position.y, light.Position.z);
    hr = IDirect3DDevice9_GetVertexShaderConstantF(device, 3, float_data, 1);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(float_data[0] == 2.0f && float_data[1] == 2.0f && float_data[2] == 2.0f && float_data[3] == 2.0f,
            "Got unexpected vertex shader floats: (%f %f %f %f).\n",
            float_data[0], float_data[1], float_data[2], float_data[3]);

    hr = effect->lpVtbl->EndPass(effect);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_BLENDOP, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == 2, "Got result %u, expected %u\n", value, 2);

    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_ZENABLE, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value, "Got result %u, expected TRUE.\n", value);

    hr = IDirect3DDevice9_GetSamplerState(device, 1, D3DSAMP_MIPFILTER, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == D3DTEXF_LINEAR, "Unexpected sampler 1 mipfilter %u.\n", value);

    hr = IDirect3DDevice9_GetTextureStageState(device, 3, D3DTSS_ALPHAOP, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == 4, "Unexpected texture stage 3 AlphaOp %u.\n", value);

    hr = effect->lpVtbl->End(effect);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_GetTransform(device, D3DTS_WORLDMATRIX(1), &mat);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(!memcmp(mat.m, test_mat.m, sizeof(mat)), "World matrix not restored.\n");

    hr = IDirect3DDevice9_GetLightEnable(device, 2, &bval);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    if (hr == D3D_OK)
        ok(!bval, "Got result %u, expected 0.\n", bval);

    /* State is not restored if effect is released without End call */
    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, 1);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = effect->lpVtbl->Begin(effect, &npasses, 0);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    hr = IDirect3DDevice9_SetRenderState(device, D3DRS_BLENDOP, 3);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);

    effect->lpVtbl->Release(effect);

    hr = IDirect3DDevice9_GetRenderState(device, D3DRS_BLENDOP, &value);
    ok(hr == D3D_OK, "Got result %x, expected 0 (D3D_OK).\n", hr);
    ok(value == 3, "Got result %u, expected %u.\n", value, 1);
}

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
float4 g_Pos1;
float4 g_Pos2;
float4 g_Selector[2] = {{0, 0, 0, 0}, {10, 10, 10, 10}};

float4 opvect1 = {0.0, -0.0, -2.2, 3.402823466e+38F};
float4 opvect2 = {1.0, 2.0, -3.0, 4.0};
float4 opvect3 = {0.0, -0.0, -2.2, 3.402823466e+38F};

float4 vect_sampler = {1, 2, 3, 4};

int4 g_iVect = {4, 3, 2, 1};

vertexshader vs_arr[3] =
{
    asm
    {
        vs_1_0
        def c0, 1, 1, 1, 1
        mov oPos, c0
    },
    asm
    {
        vs_1_1
        def c0, 2, 2, 2, 2
        mov oPos, c0
    },
    asm
    {
        vs_2_0
        def c0, 3, 3, 3, 3
        mov oPos, c0
    }
};

row_major float4x3 m4x3row = {{11, 12, 13}, {21, 22, 23}, {31, 32, 33}, {41, 42, 43}};
row_major float3x4 m3x4row = {{11, 12, 13, 14}, {21, 22, 23, 24}, {31, 32, 33, 34}};
column_major float4x3 m4x3column = {{11, 12, 13},{21, 22, 23},{31, 32, 33},{41, 42, 43}};
column_major float3x4 m3x4column = {{11, 12, 13, 14}, {21, 22, 23, 24}, {31, 32, 33, 34}};
row_major float2x2 m2x2row = {{11, 12}, {21, 22}};
column_major float2x2 m2x2column = {{11, 12}, {21, 22}};
row_major float2x3 m2x3row = {{11, 12, 13}, {21, 22, 23}};
column_major float2x3 m2x3column = {{11, 12, 13}, {21, 22, 23}};
row_major float3x2 m3x2row = {{11, 12}, {21, 22}, {31, 32}};
column_major float3x2 m3x2column = {{11, 12}, {21, 22}, {31, 32}};

row_major bool2x3 mb2x3row = {{true, false, true}, {false, true, true}};
column_major bool2x3 mb2x3column = {{true, false, true}, {false, true, true}};

struct test_struct
{
    float3 v1;
    float fv;
    float4 v2;
};

struct struct_array
{
    test_struct ts[2];
};

test_struct ts1[1] = {{{9, 10, 11}, 12, {13, 14, 15, 16}}};
test_struct ts2[2] = {{{0, 0, 0}, 0, {0, 0, 0, 0}}, {{1, 2, 3}, 4, {5, 6, 7, 8}}};
struct_array ts3 = {{{1, 2, 3}, 4, {5, 6, 7, 8}}, {{9, 10, 11}, 12, {13, 14, 15, 16}}};

float arr1[1] = {91};
float arr2[2] = {92, 93};

Texture2D tex1;
Texture2D tex2;
sampler sampler1 =
sampler_state
{
    Texture = tex1;
    MinFilter = g_iVect.y;
    MagFilter = vect_sampler.x + vect_sampler.y;
};

struct VS_OUTPUT
{
    float4 Position   : POSITION;
    float2 TextureUV  : TEXCOORD0;
    float4 Diffuse    : COLOR0;
};
VS_OUTPUT RenderSceneVS(float4 vPos : POSITION,
                        float3 vNormal : NORMAL,
                        float2 vTexCoord0 : TEXCOORD0,
                        uniform int nNumLights,
                        uniform bool bTexture)
{
    VS_OUTPUT Output;

    if (g_Selector[1].y > float4(0.5, 0.5, 0.5, 0.5).y)
        Output.Position = -g_Pos1 * 2 - float4(-4, -5, -6, -7);
    else
        Output.Position = -g_Pos2 * 3 - float4(-4, -5, -6, -7);
    Output.TextureUV = float2(0, 0);
    Output.Diffuse = 0;
    Output.Diffuse.xyz = mul(vPos, m4x3column);
    Output.Diffuse += mul(vPos, m3x4column);
    Output.Diffuse += mul(vPos, m3x4row);
    Output.Diffuse.xyz += mul(vPos, m4x3row);
    Output.Diffuse += mul(vPos, ts1[0].fv);
    Output.Diffuse += mul(vPos, ts1[0].v2);
    Output.Diffuse += mul(vPos, ts2[1].fv);
    Output.Diffuse += mul(vPos, ts2[1].v2);
    Output.Diffuse += mul(vPos, arr1[0]);
    Output.Diffuse += mul(vPos, arr2[1]);
    Output.Diffuse += mul(vPos, ts3.ts[1].fv);
    Output.Diffuse += mul(vPos, ts3.ts[1].v2);
    Output.Diffuse += tex2Dlod(sampler1, g_Pos1);
    return Output;
}

struct PS_OUTPUT
{
    float4 RGBColor : COLOR0;  /* Pixel color */
};
PS_OUTPUT RenderScenePS( VS_OUTPUT In, uniform bool2x3 mb)
{
    PS_OUTPUT Output;
    int i;

    Output.RGBColor = In.Diffuse;
    Output.RGBColor.xy += mul(In.Diffuse, m2x2row);
    Output.RGBColor.xy += mul(In.Diffuse, m2x2column);
    Output.RGBColor.xy += mul(In.Diffuse, m3x2row);
    Output.RGBColor.xy += mul(In.Diffuse, m3x2column);
    Output.RGBColor.xyz += mul(In.Diffuse, m2x3row);
    Output.RGBColor.xyz += mul(In.Diffuse, m2x3column);
    for (i = 0; i < g_iVect.x; ++i)
        Output.RGBColor.xyz += mul(In.Diffuse, m2x3column);
    if (mb[1][1])
    {
        Output.RGBColor += sin(Output.RGBColor);
        Output.RGBColor += cos(Output.RGBColor);
        Output.RGBColor.xyz += mul(Output.RGBColor, m2x3column);
        Output.RGBColor.xyz += mul(Output.RGBColor, m2x3row);
        Output.RGBColor.xy += mul(Output.RGBColor, m3x2column);
        Output.RGBColor.xy += mul(Output.RGBColor, m3x2row);
    }
    Output.RGBColor += tex2D(sampler1, In.TextureUV);
    return Output;
}

vertexshader vs_arr2[1] = {compile vs_3_0 RenderSceneVS(1, true)};
pixelshader ps_arr[1] = {compile ps_3_0 RenderScenePS(mb2x3row)};

technique tech0
{
    pass p0
    {
        VertexShader = vs_arr2[g_iVect.w - 1];
        PixelShader  = ps_arr[g_iVect.w - 1];

        LightEnable[0] = TRUE;
        LightEnable[1] = TRUE;
        LightEnable[2] = TRUE;
        LightEnable[3] = TRUE;
        LightEnable[4] = TRUE;
        LightEnable[5] = TRUE;
        LightEnable[6] = TRUE;
        LightEnable[7] = TRUE;
        LightType[0] = POINT;
        LightType[1] = POINT;
        LightType[2] = POINT;
        LightType[3] = POINT;
        LightType[4] = POINT;
        LightType[5] = POINT;
        LightType[6] = POINT;
        LightType[7] = POINT;
        LightDiffuse[0] = 1 / opvect1;
        LightDiffuse[1] = rsqrt(opvect1);
        LightDiffuse[2] = opvect1 * opvect2;
        LightDiffuse[3] = opvect1 + opvect2;
        LightDiffuse[4] = float4(opvect1 < opvect2);
        LightDiffuse[5] = float4(opvect1 >= opvect2);
        LightDiffuse[6] = -opvect1;
        LightDiffuse[7] = rcp(opvect1);

        LightAmbient[0] = frac(opvect1);
        LightAmbient[1] = min(opvect1, opvect2);
        LightAmbient[2] = max(opvect1, opvect2);
        LightAmbient[3] = sin(opvect1);
        LightAmbient[4] = cos(opvect1);
        LightAmbient[5] = 1e-2 / opvect1;
        LightAmbient[6] = float4(0, dot(opvect1, opvect2), dot(opvect2, opvect2), 0);
        LightAmbient[7] = opvect1 + 1e-12 * opvect2 - opvect3;

        LightSpecular[0] = float4(dot(opvect1.zx, opvect2.xy), dot(opvect1.zzx, opvect2.xyz),
                dot(opvect1.zzzx, opvect2.xxyy), 0);
        LightSpecular[1] = float4(opvect1[g_iVect.z], g_iVect[opvect2.y],
                g_Selector[g_iVect.w].x + g_Selector[g_iVect.w].y,
                g_Selector[g_iVect.w].x + g_Selector[g_iVect.x].y);
    }
    pass p1
    {
        VertexShader = vs_arr[g_iVect.z];
    }
}
#endif
static const DWORD test_effect_preshader_effect_blob[] =
{
    0xfeff0901, 0x00000ef4, 0x00000000, 0x00000003, 0x00000001, 0x00000030, 0x00000000, 0x00000000,
    0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000007, 0x6f505f67,
    0x00003173, 0x00000003, 0x00000001, 0x00000068, 0x00000000, 0x00000000, 0x00000004, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000007, 0x6f505f67, 0x00003273, 0x00000003,
    0x00000001, 0x000000b0, 0x00000000, 0x00000002, 0x00000004, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x41200000, 0x41200000, 0x41200000, 0x41200000, 0x0000000b, 0x65535f67,
    0x7463656c, 0x0000726f, 0x00000003, 0x00000001, 0x000000ec, 0x00000000, 0x00000000, 0x00000004,
    0x00000001, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x00000008, 0x6576706f, 0x00317463,
    0x00000003, 0x00000001, 0x00000124, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x3f800000,
    0x40000000, 0xc0400000, 0x40800000, 0x00000008, 0x6576706f, 0x00327463, 0x00000003, 0x00000001,
    0x0000015c, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x80000000, 0xc00ccccd,
    0x7f7fffff, 0x00000008, 0x6576706f, 0x00337463, 0x00000003, 0x00000001, 0x00000194, 0x00000000,
    0x00000000, 0x00000004, 0x00000001, 0x3f800000, 0x40000000, 0x40400000, 0x40800000, 0x0000000d,
    0x74636576, 0x6d61735f, 0x72656c70, 0x00000000, 0x00000002, 0x00000001, 0x000001d4, 0x00000000,
    0x00000000, 0x00000004, 0x00000001, 0x00000004, 0x00000003, 0x00000002, 0x00000001, 0x00000008,
    0x56695f67, 0x00746365, 0x00000010, 0x00000004, 0x00000200, 0x00000000, 0x00000003, 0x00000001,
    0x00000002, 0x00000003, 0x00000007, 0x615f7376, 0x00007272, 0x00000003, 0x00000002, 0x00000258,
    0x00000000, 0x00000000, 0x00000004, 0x00000003, 0x41300000, 0x41400000, 0x41500000, 0x41a80000,
    0x41b00000, 0x41b80000, 0x41f80000, 0x42000000, 0x42040000, 0x42240000, 0x42280000, 0x422c0000,
    0x00000008, 0x3378346d, 0x00776f72, 0x00000003, 0x00000002, 0x000002b0, 0x00000000, 0x00000000,
    0x00000003, 0x00000004, 0x41300000, 0x41400000, 0x41500000, 0x41600000, 0x41a80000, 0x41b00000,
    0x41b80000, 0x41c00000, 0x41f80000, 0x42000000, 0x42040000, 0x42080000, 0x00000008, 0x3478336d,
    0x00776f72, 0x00000003, 0x00000002, 0x00000308, 0x00000000, 0x00000000, 0x00000004, 0x00000003,
    0x41300000, 0x41400000, 0x41500000, 0x41a80000, 0x41b00000, 0x41b80000, 0x41f80000, 0x42000000,
    0x42040000, 0x42240000, 0x42280000, 0x422c0000, 0x0000000b, 0x3378346d, 0x756c6f63, 0x00006e6d,
    0x00000003, 0x00000002, 0x00000364, 0x00000000, 0x00000000, 0x00000003, 0x00000004, 0x41300000,
    0x41400000, 0x41500000, 0x41600000, 0x41a80000, 0x41b00000, 0x41b80000, 0x41c00000, 0x41f80000,
    0x42000000, 0x42040000, 0x42080000, 0x0000000b, 0x3478336d, 0x756c6f63, 0x00006e6d, 0x00000003,
    0x00000002, 0x000003a0, 0x00000000, 0x00000000, 0x00000002, 0x00000002, 0x41300000, 0x41400000,
    0x41a80000, 0x41b00000, 0x00000008, 0x3278326d, 0x00776f72, 0x00000003, 0x00000002, 0x000003d8,
    0x00000000, 0x00000000, 0x00000002, 0x00000002, 0x41300000, 0x41400000, 0x41a80000, 0x41b00000,
    0x0000000b, 0x3278326d, 0x756c6f63, 0x00006e6d, 0x00000003, 0x00000002, 0x0000041c, 0x00000000,
    0x00000000, 0x00000002, 0x00000003, 0x41300000, 0x41400000, 0x41500000, 0x41a80000, 0x41b00000,
    0x41b80000, 0x00000008, 0x3378326d, 0x00776f72, 0x00000003, 0x00000002, 0x0000045c, 0x00000000,
    0x00000000, 0x00000002, 0x00000003, 0x41300000, 0x41400000, 0x41500000, 0x41a80000, 0x41b00000,
    0x41b80000, 0x0000000b, 0x3378326d, 0x756c6f63, 0x00006e6d, 0x00000003, 0x00000002, 0x000004a0,
    0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x41300000, 0x41400000, 0x41a80000, 0x41b00000,
    0x41f80000, 0x42000000, 0x00000008, 0x3278336d, 0x00776f72, 0x00000003, 0x00000002, 0x000004e0,
    0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x41300000, 0x41400000, 0x41a80000, 0x41b00000,
    0x41f80000, 0x42000000, 0x0000000b, 0x3278336d, 0x756c6f63, 0x00006e6d, 0x00000001, 0x00000002,
    0x00000524, 0x00000000, 0x00000000, 0x00000002, 0x00000003, 0x00000001, 0x00000000, 0x00000001,
    0x00000000, 0x00000001, 0x00000001, 0x00000009, 0x7832626d, 0x776f7233, 0x00000000, 0x00000001,
    0x00000002, 0x00000568, 0x00000000, 0x00000000, 0x00000002, 0x00000003, 0x00000001, 0x00000000,
    0x00000001, 0x00000000, 0x00000001, 0x00000001, 0x0000000c, 0x7832626d, 0x6c6f6333, 0x006e6d75,
    0x00000000, 0x00000005, 0x00000604, 0x00000000, 0x00000001, 0x00000003, 0x00000003, 0x00000001,
    0x0000060c, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000003, 0x00000000, 0x00000614,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000003, 0x00000001, 0x0000061c, 0x00000000,
    0x00000000, 0x00000004, 0x00000001, 0x41100000, 0x41200000, 0x41300000, 0x41400000, 0x41500000,
    0x41600000, 0x41700000, 0x41800000, 0x00000004, 0x00317374, 0x00000003, 0x00003176, 0x00000003,
    0x00007666, 0x00000003, 0x00003276, 0x00000000, 0x00000005, 0x000006d0, 0x00000000, 0x00000002,
    0x00000003, 0x00000003, 0x00000001, 0x000006d8, 0x00000000, 0x00000000, 0x00000003, 0x00000001,
    0x00000003, 0x00000000, 0x000006e0, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000003,
    0x00000001, 0x000006e8, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x40000000,
    0x40400000, 0x40800000, 0x40a00000, 0x40c00000, 0x40e00000, 0x41000000, 0x00000004, 0x00327374,
    0x00000003, 0x00003176, 0x00000003, 0x00007666, 0x00000003, 0x00003276, 0x00000000, 0x00000005,
    0x000007b4, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000005, 0x000007bc, 0x00000000,
    0x00000002, 0x00000003, 0x00000003, 0x00000001, 0x000007c4, 0x00000000, 0x00000000, 0x00000003,
    0x00000001, 0x00000003, 0x00000000, 0x000007cc, 0x00000000, 0x00000000, 0x00000001, 0x00000001,
    0x00000003, 0x00000001, 0x000007d4, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x3f800000,
    0x40000000, 0x40400000, 0x40800000, 0x40a00000, 0x40c00000, 0x40e00000, 0x41000000, 0x41100000,
    0x41200000, 0x41300000, 0x41400000, 0x41500000, 0x41600000, 0x41700000, 0x41800000, 0x00000004,
    0x00337374, 0x00000003, 0x00007374, 0x00000003, 0x00003176, 0x00000003, 0x00007666, 0x00000003,
    0x00003276, 0x00000003, 0x00000000, 0x000007fc, 0x00000000, 0x00000001, 0x00000001, 0x00000001,
    0x42b60000, 0x00000005, 0x31727261, 0x00000000, 0x00000003, 0x00000000, 0x0000082c, 0x00000000,
    0x00000002, 0x00000001, 0x00000001, 0x42b80000, 0x42ba0000, 0x00000005, 0x32727261, 0x00000000,
    0x00000007, 0x00000004, 0x00000850, 0x00000000, 0x00000000, 0x00000004, 0x00000005, 0x31786574,
    0x00000000, 0x00000007, 0x00000004, 0x00000874, 0x00000000, 0x00000000, 0x00000005, 0x00000005,
    0x32786574, 0x00000000, 0x0000000a, 0x00000004, 0x00000920, 0x00000000, 0x00000000, 0x00000006,
    0x00000007, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000002,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
    0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000003, 0x000000a4, 0x00000100,
    0x00000898, 0x00000894, 0x000000aa, 0x00000100, 0x000008b0, 0x000008ac, 0x000000a9, 0x00000100,
    0x000008d0, 0x000008cc, 0x00000009, 0x706d6173, 0x3172656c, 0x00000000, 0x00000010, 0x00000004,
    0x00000948, 0x00000000, 0x00000001, 0x00000007, 0x00000008, 0x615f7376, 0x00327272, 0x0000000f,
    0x00000004, 0x0000096c, 0x00000000, 0x00000001, 0x00000008, 0x00000007, 0x615f7370, 0x00007272,
    0x00000009, 0x00000010, 0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x0000000a, 0x0000000f,
    0x00000004, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000001, 0x00000002, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002,
    0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000,
    0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
    0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002,
    0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000,
    0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003,
    0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000004, 0x00000001, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000004, 0x00000001, 0x00000003, 0x00003070, 0x0000000b, 0x00000010, 0x00000004, 0x00000000,
    0x00000000, 0x00000000, 0x00000003, 0x00003170, 0x00000006, 0x68636574, 0x00000030, 0x0000001f,
    0x00000001, 0x0000000b, 0x0000000c, 0x00000004, 0x00000020, 0x00000000, 0x00000000, 0x0000003c,
    0x00000058, 0x00000000, 0x00000000, 0x00000074, 0x00000090, 0x00000000, 0x00000000, 0x000000c0,
    0x000000dc, 0x00000000, 0x00000000, 0x000000f8, 0x00000114, 0x00000000, 0x00000000, 0x00000130,
    0x0000014c, 0x00000000, 0x00000000, 0x00000168, 0x00000184, 0x00000000, 0x00000000, 0x000001a8,
    0x000001c4, 0x00000000, 0x00000000, 0x000001e0, 0x000001f4, 0x00000000, 0x00000000, 0x0000020c,
    0x00000228, 0x00000000, 0x00000000, 0x00000264, 0x00000280, 0x00000000, 0x00000000, 0x000002bc,
    0x000002d8, 0x00000000, 0x00000000, 0x00000318, 0x00000334, 0x00000000, 0x00000000, 0x00000374,
    0x00000390, 0x00000000, 0x00000000, 0x000003ac, 0x000003c8, 0x00000000, 0x00000000, 0x000003e8,
    0x00000404, 0x00000000, 0x00000000, 0x00000428, 0x00000444, 0x00000000, 0x00000000, 0x0000046c,
    0x00000488, 0x00000000, 0x00000000, 0x000004ac, 0x000004c8, 0x00000000, 0x00000000, 0x000004f0,
    0x0000050c, 0x00000000, 0x00000000, 0x00000534, 0x00000550, 0x00000000, 0x00000000, 0x00000578,
    0x000005e4, 0x00000000, 0x00000000, 0x00000624, 0x00000690, 0x00000000, 0x00000000, 0x000006f0,
    0x00000774, 0x00000000, 0x00000000, 0x000007dc, 0x000007f8, 0x00000000, 0x00000000, 0x00000808,
    0x00000824, 0x00000000, 0x00000000, 0x00000838, 0x0000084c, 0x00000000, 0x00000000, 0x0000085c,
    0x00000870, 0x00000000, 0x00000000, 0x00000880, 0x000008ec, 0x00000000, 0x00000000, 0x00000930,
    0x00000944, 0x00000000, 0x00000000, 0x00000954, 0x00000968, 0x00000000, 0x00000000, 0x00000ee8,
    0x00000000, 0x00000002, 0x00000ec0, 0x00000000, 0x00000024, 0x00000092, 0x00000000, 0x0000097c,
    0x00000978, 0x00000093, 0x00000000, 0x00000994, 0x00000990, 0x00000091, 0x00000000, 0x000009ac,
    0x000009a8, 0x00000091, 0x00000001, 0x000009cc, 0x000009c8, 0x00000091, 0x00000002, 0x000009ec,
    0x000009e8, 0x00000091, 0x00000003, 0x00000a0c, 0x00000a08, 0x00000091, 0x00000004, 0x00000a2c,
    0x00000a28, 0x00000091, 0x00000005, 0x00000a4c, 0x00000a48, 0x00000091, 0x00000006, 0x00000a6c,
    0x00000a68, 0x00000091, 0x00000007, 0x00000a8c, 0x00000a88, 0x00000084, 0x00000000, 0x00000aac,
    0x00000aa8, 0x00000084, 0x00000001, 0x00000acc, 0x00000ac8, 0x00000084, 0x00000002, 0x00000aec,
    0x00000ae8, 0x00000084, 0x00000003, 0x00000b0c, 0x00000b08, 0x00000084, 0x00000004, 0x00000b2c,
    0x00000b28, 0x00000084, 0x00000005, 0x00000b4c, 0x00000b48, 0x00000084, 0x00000006, 0x00000b6c,
    0x00000b68, 0x00000084, 0x00000007, 0x00000b8c, 0x00000b88, 0x00000085, 0x00000000, 0x00000bb8,
    0x00000ba8, 0x00000085, 0x00000001, 0x00000be4, 0x00000bd4, 0x00000085, 0x00000002, 0x00000c10,
    0x00000c00, 0x00000085, 0x00000003, 0x00000c3c, 0x00000c2c, 0x00000085, 0x00000004, 0x00000c68,
    0x00000c58, 0x00000085, 0x00000005, 0x00000c94, 0x00000c84, 0x00000085, 0x00000006, 0x00000cc0,
    0x00000cb0, 0x00000085, 0x00000007, 0x00000cec, 0x00000cdc, 0x00000087, 0x00000000, 0x00000d18,
    0x00000d08, 0x00000087, 0x00000001, 0x00000d44, 0x00000d34, 0x00000087, 0x00000002, 0x00000d70,
    0x00000d60, 0x00000087, 0x00000003, 0x00000d9c, 0x00000d8c, 0x00000087, 0x00000004, 0x00000dc8,
    0x00000db8, 0x00000087, 0x00000005, 0x00000df4, 0x00000de4, 0x00000087, 0x00000006, 0x00000e20,
    0x00000e10, 0x00000087, 0x00000007, 0x00000e4c, 0x00000e3c, 0x00000086, 0x00000000, 0x00000e78,
    0x00000e68, 0x00000086, 0x00000001, 0x00000ea4, 0x00000e94, 0x00000ee0, 0x00000000, 0x00000001,
    0x00000092, 0x00000000, 0x00000ecc, 0x00000ec8, 0x00000007, 0x00000018, 0x00000008, 0x000007cc,
    0xffff0300, 0x00c0fffe, 0x42415443, 0x0000001c, 0x000002cb, 0xffff0300, 0x00000009, 0x0000001c,
    0x00000000, 0x000002c4, 0x000000d0, 0x00000001, 0x00000001, 0x000000d8, 0x000000e8, 0x000000f8,
    0x00080002, 0x00000002, 0x00000104, 0x00000114, 0x00000134, 0x00060002, 0x00000002, 0x0000013c,
    0x0000014c, 0x0000016c, 0x00000002, 0x00000003, 0x00000178, 0x00000188, 0x000001b8, 0x000a0002,
    0x00000002, 0x000001c0, 0x000001d0, 0x000001f0, 0x000c0002, 0x00000002, 0x000001fc, 0x0000020c,
    0x0000022c, 0x00030002, 0x00000003, 0x00000234, 0x00000244, 0x00000274, 0x00000000, 0x00000005,
    0x00000280, 0x00000290, 0x000002a8, 0x00000003, 0x00000001, 0x000002b4, 0x00000000, 0x56695f67,
    0x00746365, 0x00020001, 0x00040001, 0x00000001, 0x00000000, 0x00000004, 0x00000003, 0x00000002,
    0x00000001, 0x3278326d, 0x756c6f63, 0xab006e6d, 0x00030003, 0x00020002, 0x00000001, 0x00000000,
    0x41300000, 0x41a80000, 0x00000000, 0x00000000, 0x41400000, 0x41b00000, 0x00000000, 0x00000000,
    0x3278326d, 0x00776f72, 0x00030002, 0x00020002, 0x00000001, 0x00000000, 0x41300000, 0x41400000,
    0x00000000, 0x00000000, 0x41a80000, 0x41b00000, 0x00000000, 0x00000000, 0x3378326d, 0x756c6f63,
    0xab006e6d, 0x00030003, 0x00030002, 0x00000001, 0x00000000, 0x41300000, 0x41a80000, 0x00000000,
    0x00000000, 0x41400000, 0x41b00000, 0x00000000, 0x00000000, 0x41500000, 0x41b80000, 0x00000000,
    0x00000000, 0x3378326d, 0x00776f72, 0x00030002, 0x00030002, 0x00000001, 0x00000000, 0x41300000,
    0x41400000, 0x41500000, 0x00000000, 0x41a80000, 0x41b00000, 0x41b80000, 0x00000000, 0x3278336d,
    0x756c6f63, 0xab006e6d, 0x00030003, 0x00020003, 0x00000001, 0x00000000, 0x41300000, 0x41a80000,
    0x41f80000, 0x00000000, 0x41400000, 0x41b00000, 0x42000000, 0x00000000, 0x3278336d, 0x00776f72,
    0x00030002, 0x00020003, 0x00000001, 0x00000000, 0x41300000, 0x41400000, 0x00000000, 0x00000000,
    0x41a80000, 0x41b00000, 0x00000000, 0x00000000, 0x41f80000, 0x42000000, 0x00000000, 0x00000000,
    0x7832626d, 0x776f7233, 0xababab00, 0x00010002, 0x00030002, 0x00000001, 0x00000000, 0xffffffff,
    0x00000000, 0xffffffff, 0x00000000, 0xffffffff, 0xffffffff, 0x706d6173, 0x3172656c, 0xababab00,
    0x000c0004, 0x00010001, 0x00000001, 0x00000000, 0x335f7370, 0x4d00305f, 0x6f726369, 0x74666f73,
    0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932,
    0x332e3235, 0x00313131, 0x05000051, 0xa00f000e, 0x3d2aaaa4, 0xbf000000, 0x3f800000, 0xbe22f983,
    0x05000051, 0xa00f000f, 0x00000000, 0x3e22f983, 0x3e800000, 0xbab609ba, 0x05000051, 0xa00f0010,
    0x40c90fdb, 0xc0490fdb, 0xb4878163, 0x37cfb5a1, 0x0200001f, 0x80000005, 0x90030000, 0x0200001f,
    0x8000000a, 0x900f0001, 0x0200001f, 0x90000000, 0xa00f0800, 0x03000005, 0x80030000, 0xa0e40007,
    0x90550001, 0x04000004, 0x80030000, 0x90000001, 0xa0e40006, 0x80e40000, 0x03000002, 0x80030000,
    0x80e40000, 0x90e40001, 0x02000001, 0x80010001, 0xa000000f, 0x0400005a, 0x80010002, 0x90e40001,
    0xa0e40008, 0x80000001, 0x0400005a, 0x80020002, 0x90e40001, 0xa0e40009, 0x80000001, 0x03000002,
    0x80030000, 0x80e40000, 0x80e40002, 0x03000005, 0x800c0000, 0xa0440004, 0x90550001, 0x04000004,
    0x800c0000, 0x90000001, 0xa0440003, 0x80e40000, 0x04000004, 0x800c0000, 0x90aa0001, 0xa0440005,
    0x80e40000, 0x03000002, 0x80030000, 0x80ee0000, 0x80e40000, 0x03000008, 0x80010002, 0x90e40001,
    0xa0e4000c, 0x03000008, 0x80020002, 0x90e40001, 0xa0e4000d, 0x03000002, 0x80030000, 0x80e40000,
    0x80e40002, 0x03000005, 0x800e0001, 0xa090000b, 0x90550001, 0x04000004, 0x800e0001, 0x90000001,
    0xa090000a, 0x80e40001, 0x02000001, 0x80040000, 0x90aa0001, 0x03000002, 0x80070000, 0x80e40000,
    0x80f90001, 0x0400005a, 0x80010002, 0x90e40001, 0xa0e40000, 0x80000001, 0x0400005a, 0x80020002,
    0x90e40001, 0xa0e40001, 0x80000001, 0x0400005a, 0x80040002, 0x90e40001, 0xa0e40002, 0x80000001,
    0x03000002, 0x80070000, 0x80e40000, 0x80e40002, 0x02000001, 0x80070003, 0x80e40000, 0x01000026,
    0xf0e40000, 0x03000002, 0x80070003, 0x80e40002, 0x80e40003, 0x00000027, 0x01000028, 0xe0e40804,
    0x02000001, 0x80080003, 0x90ff0001, 0x04000004, 0x800f0000, 0x80e40003, 0xa055000f, 0xa0aa000f,
    0x02000013, 0x800f0000, 0x80e40000, 0x04000004, 0x800f0000, 0x80e40000, 0xa0000010, 0xa0550010,
    0x03000005, 0x800f0000, 0x80e40000, 0x80e40000, 0x04000004, 0x800f0002, 0x80e40000, 0xa0aa0010,
    0xa0ff0010, 0x04000004, 0x800f0002, 0x80e40000, 0x80e40002, 0xa0ff000f, 0x04000004, 0x800f0002,
    0x80e40000, 0x80e40002, 0xa000000e, 0x04000004, 0x800f0002, 0x80e40000, 0x80e40002, 0xa055000e,
    0x04000004, 0x800f0000, 0x80e40000, 0x80e40002, 0x80e40003, 0x03000002, 0x800f0000, 0x80e40000,
    0xa0aa000e, 0x04000004, 0x800f0002, 0x80e40000, 0xa1ff000e, 0xa155000e, 0x02000013, 0x800f0002,
    0x80e40002, 0x04000004, 0x800f0002, 0x80e40002, 0xa0000010, 0xa0550010, 0x03000005, 0x800f0002,
    0x80e40002, 0x80e40002, 0x04000004, 0x800f0004, 0x80e40002, 0xa0aa0010, 0xa0ff0010, 0x04000004,
    0x800f0004, 0x80e40002, 0x80e40004, 0xa0ff000f, 0x04000004, 0x800f0004, 0x80e40002, 0x80e40004,
    0xa000000e, 0x04000004, 0x800f0004, 0x80e40002, 0x80e40004, 0xa055000e, 0x04000004, 0x800f0000,
    0x80e40002, 0x80e40004, 0x80e40000, 0x03000002, 0x800f0003, 0x80e40000, 0xa0aa000e, 0x0400005a,
    0x80010000, 0x80e40003, 0xa0e40000, 0x80000001, 0x0400005a, 0x80020000, 0x80e40003, 0xa0e40001,
    0x80000001, 0x0400005a, 0x80040000, 0x80e40003, 0xa0e40002, 0x80000001, 0x03000002, 0x80070000,
    0x80e40000, 0x80e40003, 0x03000005, 0x80070001, 0x80550000, 0xa0e4000b, 0x04000004, 0x80070001,
    0x80000000, 0xa0e4000a, 0x80e40001, 0x03000002, 0x80070003, 0x80e40000, 0x80e40001, 0x03000008,
    0x80010000, 0x80e40003, 0xa0e4000c, 0x03000008, 0x80020000, 0x80e40003, 0xa0e4000d, 0x03000002,
    0x80030000, 0x80e40000, 0x80e40003, 0x03000005, 0x800c0000, 0x80550000, 0xa0440004, 0x04000004,
    0x800c0000, 0x80000000, 0xa0440003, 0x80e40000, 0x04000004, 0x800c0000, 0x80aa0003, 0xa0440005,
    0x80e40000, 0x03000002, 0x80030003, 0x80ee0000, 0x80e40000, 0x0000002a, 0x02000001, 0x80080003,
    0x90ff0001, 0x0000002b, 0x03000042, 0x800f0000, 0x90e40000, 0xa0e40800, 0x03000002, 0x800f0800,
    0x80e40000, 0x80e40003, 0x0000ffff, 0x00000007, 0x00000afc, 0xfffe0300, 0x012ffffe, 0x42415443,
    0x0000001c, 0x00000487, 0xfffe0300, 0x0000000b, 0x0000001c, 0x00000000, 0x00000480, 0x000000f8,
    0x00200002, 0x00000001, 0x00000100, 0x00000110, 0x00000120, 0x001d0002, 0x00000002, 0x00000128,
    0x00000138, 0x00000158, 0x001f0002, 0x00000001, 0x00000160, 0x00000170, 0x00000180, 0x00100002,
    0x00000004, 0x0000018c, 0x0000019c, 0x000001dc, 0x00140002, 0x00000003, 0x000001e4, 0x000001f4,
    0x00000224, 0x00170002, 0x00000003, 0x00000230, 0x00000240, 0x00000270, 0x000c0002, 0x00000004,
    0x00000278, 0x00000288, 0x000002c8, 0x00000003, 0x00000001, 0x000002d4, 0x00000000, 0x000002e4,
    0x001a0002, 0x00000003, 0x0000033c, 0x0000034c, 0x0000037c, 0x00000002, 0x00000006, 0x00000380,
    0x00000390, 0x000003f0, 0x00060002, 0x00000006, 0x00000410, 0x00000420, 0x31727261, 0xababab00,
    0x00030000, 0x00010001, 0x00000001, 0x00000000, 0x42b60000, 0x00000000, 0x00000000, 0x00000000,
    0x32727261, 0xababab00, 0x00030000, 0x00010001, 0x00000002, 0x00000000, 0x42b80000, 0x00000000,
    0x00000000, 0x00000000, 0x42ba0000, 0x00000000, 0x00000000, 0x00000000, 0x6f505f67, 0xab003173,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x3478336d, 0x756c6f63, 0xab006e6d, 0x00030003, 0x00040003, 0x00000001, 0x00000000, 0x41300000,
    0x41a80000, 0x41f80000, 0x00000000, 0x41400000, 0x41b00000, 0x42000000, 0x00000000, 0x41500000,
    0x41b80000, 0x42040000, 0x00000000, 0x41600000, 0x41c00000, 0x42080000, 0x00000000, 0x3478336d,
    0x00776f72, 0x00030002, 0x00040003, 0x00000001, 0x00000000, 0x41300000, 0x41400000, 0x41500000,
    0x41600000, 0x41a80000, 0x41b00000, 0x41b80000, 0x41c00000, 0x41f80000, 0x42000000, 0x42040000,
    0x42080000, 0x3378346d, 0x756c6f63, 0xab006e6d, 0x00030003, 0x00030004, 0x00000001, 0x00000000,
    0x41300000, 0x41a80000, 0x41f80000, 0x42240000, 0x41400000, 0x41b00000, 0x42000000, 0x42280000,
    0x41500000, 0x41b80000, 0x42040000, 0x422c0000, 0x3378346d, 0x00776f72, 0x00030002, 0x00030004,
    0x00000001, 0x00000000, 0x41300000, 0x41400000, 0x41500000, 0x00000000, 0x41a80000, 0x41b00000,
    0x41b80000, 0x00000000, 0x41f80000, 0x42000000, 0x42040000, 0x00000000, 0x42240000, 0x42280000,
    0x422c0000, 0x00000000, 0x706d6173, 0x3172656c, 0xababab00, 0x000c0004, 0x00010001, 0x00000001,
    0x00000000, 0x00317374, 0xab003176, 0x00030001, 0x00030001, 0x00000001, 0x00000000, 0xab007666,
    0x00030000, 0x00010001, 0x00000001, 0x00000000, 0xab003276, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x000002e8, 0x000002ec, 0x000002fc, 0x00000300, 0x00000310, 0x00000314, 0x00000005,
    0x00080001, 0x00030001, 0x00000324, 0x41100000, 0x41200000, 0x41300000, 0x00000000, 0x41400000,
    0x00000000, 0x00000000, 0x00000000, 0x41500000, 0x41600000, 0x41700000, 0x41800000, 0x00327374,
    0x00000005, 0x00080001, 0x00030002, 0x00000324, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x3f800000, 0x40000000, 0x40400000, 0x00000000, 0x40800000, 0x00000000, 0x00000000, 0x00000000,
    0x40a00000, 0x40c00000, 0x40e00000, 0x41000000, 0x00337374, 0xab007374, 0x00000005, 0x00080001,
    0x00030002, 0x00000324, 0x000003f4, 0x000003f8, 0x00000005, 0x00100001, 0x00010001, 0x00000408,
    0x3f800000, 0x40000000, 0x40400000, 0x00000000, 0x40800000, 0x00000000, 0x00000000, 0x00000000,
    0x40a00000, 0x40c00000, 0x40e00000, 0x41000000, 0x41100000, 0x41200000, 0x41300000, 0x00000000,
    0x41400000, 0x00000000, 0x00000000, 0x00000000, 0x41500000, 0x41600000, 0x41700000, 0x41800000,
    0x335f7376, 0x4d00305f, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461,
    0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x00ecfffe, 0x53455250,
    0x46580201, 0x0043fffe, 0x42415443, 0x0000001c, 0x000000d7, 0x46580201, 0x00000003, 0x0000001c,
    0x00000100, 0x000000d4, 0x00000058, 0x00020002, 0x00000001, 0x00000060, 0x00000070, 0x00000080,
    0x00030002, 0x00000001, 0x00000088, 0x00000070, 0x00000098, 0x00000002, 0x00000002, 0x000000a4,
    0x000000b4, 0x6f505f67, 0xab003173, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x6f505f67, 0xab003273, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x65535f67, 0x7463656c, 0xab00726f, 0x00030001, 0x00040001, 0x00000002, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x41200000, 0x41200000, 0x41200000, 0x41200000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x000cfffe, 0x49535250, 0x00000021,
    0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000001, 0x00000021, 0x00000001,
    0x00000000, 0x00000000, 0x0032fffe, 0x54494c43, 0x00000018, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x3fe00000, 0x00000000,
    0xc0000000, 0x00000000, 0xc0080000, 0x00000000, 0x00000000, 0x00000000, 0x40100000, 0x00000000,
    0x40140000, 0x00000000, 0x40180000, 0x00000000, 0x401c0000, 0x0064fffe, 0x434c5846, 0x00000009,
    0xa0500004, 0x00000002, 0x00000000, 0x00000001, 0x00000011, 0x00000000, 0x00000002, 0x00000008,
    0x00000000, 0x00000007, 0x00000000, 0x20400004, 0x00000002, 0x00000000, 0x00000007, 0x00000000,
    0x00000000, 0x00000001, 0x00000014, 0x00000000, 0x00000007, 0x00000004, 0xa0500004, 0x00000002,
    0x00000000, 0x00000001, 0x00000012, 0x00000000, 0x00000002, 0x0000000c, 0x00000000, 0x00000007,
    0x00000000, 0x20400004, 0x00000002, 0x00000000, 0x00000007, 0x00000000, 0x00000000, 0x00000001,
    0x00000014, 0x00000000, 0x00000007, 0x00000008, 0x10100004, 0x00000001, 0x00000000, 0x00000007,
    0x00000008, 0x00000000, 0x00000007, 0x00000000, 0x20400004, 0x00000002, 0x00000000, 0x00000007,
    0x00000000, 0x00000000, 0x00000007, 0x00000004, 0x00000000, 0x00000007, 0x0000000c, 0xa0200001,
    0x00000002, 0x00000000, 0x00000001, 0x00000010, 0x00000000, 0x00000002, 0x00000005, 0x00000000,
    0x00000007, 0x00000000, 0xa0500004, 0x00000002, 0x00000000, 0x00000007, 0x00000000, 0x00000000,
    0x00000007, 0x0000000c, 0x00000000, 0x00000007, 0x00000004, 0x20400004, 0x00000002, 0x00000000,
    0x00000007, 0x00000004, 0x00000000, 0x00000007, 0x00000008, 0x00000000, 0x00000004, 0x00000084,
    0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x05000051, 0xa00f0022, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x0200001f, 0x80000000, 0x900f0000, 0x0200001f, 0x90000000, 0xa00f0800, 0x0200001f,
    0x80000000, 0xe00f0000, 0x0200001f, 0x80000005, 0xe0030001, 0x0200001f, 0x8000000a, 0xe00f0002,
    0x03000009, 0x80010000, 0x90e40000, 0xa0e40017, 0x03000009, 0x80020000, 0x90e40000, 0xa0e40018,
    0x03000009, 0x80040000, 0x90e40000, 0xa0e40019, 0x03000008, 0x80010001, 0x90e40000, 0xa0e40010,
    0x03000008, 0x80020001, 0x90e40000, 0xa0e40011, 0x03000008, 0x80040001, 0x90e40000, 0xa0e40012,
    0x03000008, 0x80080001, 0x90e40000, 0xa0e40013, 0x02000001, 0x80080000, 0xa0000022, 0x03000002,
    0x800f0000, 0x80e40000, 0x80e40001, 0x03000005, 0x800f0001, 0xa0e40015, 0x90550000, 0x04000004,
    0x800f0001, 0x90000000, 0xa0e40014, 0x80e40001, 0x04000004, 0x800f0001, 0x90aa0000, 0xa0e40016,
    0x80e40001, 0x03000002, 0x800f0000, 0x80e40000, 0x80e40001, 0x03000005, 0x80070001, 0xa0e4000d,
    0x90550000, 0x04000004, 0x80070001, 0x90000000, 0xa0e4000c, 0x80e40001, 0x04000004, 0x80070001,
    0x90aa0000, 0xa0e4000e, 0x80e40001, 0x04000004, 0x80070001, 0x90ff0000, 0xa0e4000f, 0x80e40001,
    0x03000002, 0x80070000, 0x80e40000, 0x80e40001, 0x04000004, 0x800f0000, 0x90e40000, 0xa000001b,
    0x80e40000, 0x03000009, 0x80010001, 0x90e40000, 0xa0e4001c, 0x03000002, 0x800f0000, 0x80e40000,
    0x80000001, 0x04000004, 0x800f0000, 0x90e40000, 0xa0000004, 0x80e40000, 0x03000009, 0x80010001,
    0x90e40000, 0xa0e40005, 0x03000002, 0x800f0000, 0x80e40000, 0x80000001, 0x04000004, 0x800f0000,
    0x90e40000, 0xa0000020, 0x80e40000, 0x04000004, 0x800f0000, 0x90e40000, 0xa000001e, 0x80e40000,
    0x04000004, 0x800f0000, 0x90e40000, 0xa000000a, 0x80e40000, 0x03000009, 0x80010001, 0x90e40000,
    0xa0e4000b, 0x03000002, 0x800f0000, 0x80e40000, 0x80000001, 0x0300005f, 0x800f0001, 0xa0e4001f,
    0xa0e40800, 0x03000002, 0xe00f0002, 0x80e40000, 0x80e40001, 0x02000001, 0xe00f0000, 0xa0e40021,
    0x02000001, 0xe0030001, 0xa0000022, 0x0000ffff, 0x00000005, 0x00000000, 0x00000004, 0x00000000,
    0x00000001, 0x0000002c, 0xfffe0101, 0x00000051, 0xa00f0000, 0x3f800000, 0x3f800000, 0x3f800000,
    0x3f800000, 0x00000001, 0xc00f0000, 0xa0e40000, 0x0000ffff, 0x00000002, 0x0000002c, 0xfffe0101,
    0x00000051, 0xa00f0000, 0x40000000, 0x40000000, 0x40000000, 0x40000000, 0x00000001, 0xc00f0000,
    0xa0e40000, 0x0000ffff, 0x00000003, 0x0000002c, 0xfffe0200, 0x05000051, 0xa00f0000, 0x40400000,
    0x40400000, 0x40400000, 0x40400000, 0x02000001, 0xc00f0000, 0xa0e40000, 0x0000ffff, 0x00000000,
    0x00000001, 0xffffffff, 0x00000000, 0x00000002, 0x000000e8, 0x00000008, 0x615f7376, 0x00007272,
    0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c,
    0x00000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x56695f67,
    0x00746365, 0x00020001, 0x00040001, 0x00000001, 0x00000000, 0x40800000, 0x40400000, 0x40000000,
    0x3f800000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461,
    0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43,
    0x00000000, 0x000cfffe, 0x434c5846, 0x00000001, 0x10000001, 0x00000001, 0x00000000, 0x00000002,
    0x00000002, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000,
    0x00000000, 0xffffffff, 0x00000023, 0x00000000, 0x000003f0, 0x46580200, 0x0056fffe, 0x42415443,
    0x0000001c, 0x00000123, 0x46580200, 0x00000004, 0x0000001c, 0x20000100, 0x00000120, 0x0000006c,
    0x00000002, 0x00000002, 0x00000078, 0x00000088, 0x000000a8, 0x00040002, 0x00000001, 0x000000b0,
    0x000000c0, 0x000000d0, 0x00020002, 0x00000001, 0x000000d8, 0x000000e8, 0x000000f8, 0x00030002,
    0x00000001, 0x00000100, 0x00000110, 0x65535f67, 0x7463656c, 0xab00726f, 0x00030001, 0x00040001,
    0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x41200000, 0x41200000,
    0x41200000, 0x41200000, 0x56695f67, 0x00746365, 0x00020001, 0x00040001, 0x00000001, 0x00000000,
    0x40800000, 0x40400000, 0x40000000, 0x3f800000, 0x6576706f, 0x00317463, 0x00030001, 0x00040001,
    0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x004afffe, 0x54494c43, 0x00000024,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x3ff00000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x3ff00000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x3ff00000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x3ff00000,
    0x0057fffe, 0x434c5846, 0x00000007, 0x50000004, 0x00000002, 0x00000000, 0x00000002, 0x00000008,
    0x00000001, 0x00000002, 0x00000012, 0x00000001, 0x00000014, 0x00000000, 0x00000004, 0x00000000,
    0x10400001, 0x00000001, 0x00000000, 0x00000002, 0x0000000d, 0x00000000, 0x00000007, 0x00000000,
    0x10100001, 0x00000001, 0x00000000, 0x00000007, 0x00000000, 0x00000000, 0x00000007, 0x00000004,
    0xa0400001, 0x00000002, 0x00000000, 0x00000007, 0x00000004, 0x00000000, 0x00000002, 0x0000000d,
    0x00000000, 0x00000007, 0x00000000, 0x50000004, 0x00000002, 0x00000000, 0x00000002, 0x00000010,
    0x00000001, 0x00000007, 0x00000000, 0x00000001, 0x00000014, 0x00000000, 0x00000004, 0x00000001,
    0xa0400001, 0x00000002, 0x00000001, 0x00000002, 0x00000013, 0x00000002, 0x00000001, 0x00000001,
    0x00000002, 0x00000013, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000002, 0xa0400001,
    0x00000002, 0x00000001, 0x00000002, 0x00000010, 0x00000002, 0x00000001, 0x00000001, 0x00000002,
    0x00000013, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000003, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000022, 0x00000000, 0x000002cc, 0x46580200,
    0x0033fffe, 0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002, 0x0000001c, 0x20000100,
    0x00000094, 0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c, 0x0000006c, 0x00010002,
    0x00000001, 0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001,
    0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000, 0x4d007874,
    0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970,
    0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x001afffe, 0x54494c43, 0x0000000c, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0061fffe,
    0x434c5846, 0x00000006, 0xa0500001, 0x00000002, 0x00000000, 0x00000002, 0x00000002, 0x00000000,
    0x00000002, 0x00000004, 0x00000000, 0x00000007, 0x00000000, 0xa0500001, 0x00000002, 0x00000000,
    0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000005, 0x00000000, 0x00000007, 0x00000001,
    0xa0400001, 0x00000002, 0x00000000, 0x00000007, 0x00000001, 0x00000000, 0x00000007, 0x00000000,
    0x00000000, 0x00000004, 0x00000000, 0x70e00001, 0x00000006, 0x00000000, 0x00000002, 0x00000002,
    0x00000000, 0x00000002, 0x00000002, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000002,
    0x00000004, 0x00000000, 0x00000002, 0x00000005, 0x00000000, 0x00000002, 0x00000006, 0x00000000,
    0x00000004, 0x00000001, 0x70e00001, 0x00000008, 0x00000000, 0x00000002, 0x00000002, 0x00000000,
    0x00000002, 0x00000002, 0x00000000, 0x00000002, 0x00000002, 0x00000000, 0x00000002, 0x00000000,
    0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000002,
    0x00000005, 0x00000000, 0x00000002, 0x00000005, 0x00000000, 0x00000004, 0x00000002, 0x10000001,
    0x00000001, 0x00000000, 0x00000001, 0x00000008, 0x00000000, 0x00000004, 0x00000003, 0xf0f0f0f0,
    0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000021, 0x00000000, 0x00000248,
    0x46580200, 0x003efffe, 0x42415443, 0x0000001c, 0x000000c3, 0x46580200, 0x00000003, 0x0000001c,
    0x20000100, 0x000000c0, 0x00000058, 0x00000002, 0x00000001, 0x00000060, 0x00000070, 0x00000080,
    0x00010002, 0x00000001, 0x00000088, 0x00000098, 0x000000a8, 0x00020002, 0x00000001, 0x000000b0,
    0x00000070, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000, 0x6576706f, 0x00337463, 0x00030001,
    0x00040001, 0x00000001, 0x00000000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0022fffe, 0x54494c43, 0x00000010, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x812dea11, 0x3d719799, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x002dfffe, 0x434c5846, 0x00000004, 0xa0500004, 0x00000002,
    0x00000000, 0x00000001, 0x0000000c, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000007,
    0x00000000, 0x20400004, 0x00000002, 0x00000000, 0x00000007, 0x00000000, 0x00000000, 0x00000002,
    0x00000000, 0x00000000, 0x00000007, 0x00000004, 0x10100004, 0x00000001, 0x00000000, 0x00000002,
    0x00000008, 0x00000000, 0x00000007, 0x00000000, 0x20400004, 0x00000002, 0x00000000, 0x00000007,
    0x00000000, 0x00000000, 0x00000007, 0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0,
    0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000020, 0x00000000, 0x000001f0,
    0x46580200, 0x0033fffe, 0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002, 0x0000001c,
    0x20000100, 0x00000094, 0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c, 0x0000006c,
    0x00010002, 0x00000001, 0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001, 0x00040001,
    0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x001afffe, 0x54494c43, 0x0000000c,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x002afffe, 0x434c5846, 0x00000004, 0x50000004, 0x00000002, 0x00000000, 0x00000002, 0x00000000,
    0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004, 0x00000001, 0x50000004, 0x00000002,
    0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004,
    0x00000002, 0x10000001, 0x00000001, 0x00000000, 0x00000001, 0x00000008, 0x00000000, 0x00000004,
    0x00000000, 0x10000001, 0x00000001, 0x00000000, 0x00000001, 0x00000008, 0x00000000, 0x00000004,
    0x00000003, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x0000001f,
    0x00000000, 0x000001a8, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200,
    0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038,
    0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0012fffe, 0x54494c43, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x47ae147b, 0x3f847ae1, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x002ffffe, 0x434c5846, 0x00000005, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000007, 0x00000000, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000001, 0x00000000, 0x00000007, 0x00000001, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000002, 0x00000000, 0x00000007, 0x00000002, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000003, 0x00000000, 0x00000007, 0x00000003, 0xa0500004, 0x00000002,
    0x00000000, 0x00000001, 0x00000004, 0x00000000, 0x00000007, 0x00000000, 0x00000000, 0x00000004,
    0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x0000001e,
    0x00000000, 0x000000dc, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200,
    0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038,
    0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0002fffe, 0x54494c43, 0x00000000, 0x000cfffe, 0x434c5846, 0x00000001, 0x10900004, 0x00000001,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x0000001d, 0x00000000, 0x000000dc, 0x46580200,
    0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x20000100,
    0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x6576706f, 0x00317463,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000,
    0x000cfffe, 0x434c5846, 0x00000001, 0x10800004, 0x00000001, 0x00000000, 0x00000002, 0x00000000,
    0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000,
    0xffffffff, 0x0000001c, 0x00000000, 0x00000124, 0x46580200, 0x0033fffe, 0x42415443, 0x0000001c,
    0x00000097, 0x46580200, 0x00000002, 0x0000001c, 0x20000100, 0x00000094, 0x00000044, 0x00000002,
    0x00000001, 0x0000004c, 0x0000005c, 0x0000006c, 0x00010002, 0x00000001, 0x00000074, 0x00000084,
    0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x80000000,
    0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001, 0x00040001, 0x00000001, 0x00000000,
    0x3f800000, 0x40000000, 0xc0400000, 0x40800000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820,
    0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235,
    0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x000ffffe, 0x434c5846, 0x00000001, 0x20100004,
    0x00000002, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x00000000,
    0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff,
    0x0000001b, 0x00000000, 0x00000124, 0x46580200, 0x0033fffe, 0x42415443, 0x0000001c, 0x00000097,
    0x46580200, 0x00000002, 0x0000001c, 0x20000100, 0x00000094, 0x00000044, 0x00000002, 0x00000001,
    0x0000004c, 0x0000005c, 0x0000006c, 0x00010002, 0x00000001, 0x00000074, 0x00000084, 0x6576706f,
    0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd,
    0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000,
    0x40000000, 0xc0400000, 0x40800000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0002fffe, 0x54494c43, 0x00000000, 0x000ffffe, 0x434c5846, 0x00000001, 0x20000004, 0x00000002,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004,
    0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x0000001a,
    0x00000000, 0x000000dc, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200,
    0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038,
    0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0002fffe, 0x54494c43, 0x00000000, 0x000cfffe, 0x434c5846, 0x00000001, 0x10400004, 0x00000001,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000019, 0x00000000, 0x0000013c, 0x46580200,
    0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x20000100,
    0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x6576706f, 0x00317463,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000,
    0x0024fffe, 0x434c5846, 0x00000004, 0x10300001, 0x00000001, 0x00000000, 0x00000002, 0x00000000,
    0x00000000, 0x00000004, 0x00000000, 0x10300001, 0x00000001, 0x00000000, 0x00000002, 0x00000001,
    0x00000000, 0x00000004, 0x00000001, 0x10300001, 0x00000001, 0x00000000, 0x00000002, 0x00000002,
    0x00000000, 0x00000004, 0x00000002, 0x10300001, 0x00000001, 0x00000000, 0x00000002, 0x00000003,
    0x00000000, 0x00000004, 0x00000003, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000,
    0xffffffff, 0x00000018, 0x00000000, 0x000000dc, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c,
    0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030, 0x00000002,
    0x00000001, 0x00000038, 0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369, 0x74666f73,
    0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932,
    0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x000cfffe, 0x434c5846, 0x00000001,
    0x10100004, 0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000000,
    0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000017, 0x00000000,
    0x00000124, 0x46580200, 0x0033fffe, 0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002,
    0x0000001c, 0x20000100, 0x00000094, 0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c,
    0x0000006c, 0x00010002, 0x00000001, 0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001,
    0x00040001, 0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f,
    0x00327463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000,
    0x40800000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461,
    0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43,
    0x00000000, 0x000ffffe, 0x434c5846, 0x00000001, 0x20300004, 0x00000002, 0x00000000, 0x00000002,
    0x00000000, 0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0,
    0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000016, 0x00000000, 0x00000124,
    0x46580200, 0x0033fffe, 0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002, 0x0000001c,
    0x20000100, 0x00000094, 0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c, 0x0000006c,
    0x00010002, 0x00000001, 0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001, 0x00040001,
    0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000,
    0x000ffffe, 0x434c5846, 0x00000001, 0x20200004, 0x00000002, 0x00000000, 0x00000002, 0x00000000,
    0x00000000, 0x00000002, 0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000015, 0x00000000, 0x00000124, 0x46580200,
    0x0033fffe, 0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002, 0x0000001c, 0x20000100,
    0x00000094, 0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c, 0x0000006c, 0x00010002,
    0x00000001, 0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001,
    0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001,
    0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000, 0x4d007874,
    0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970,
    0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x000ffffe,
    0x434c5846, 0x00000001, 0x20400004, 0x00000002, 0x00000000, 0x00000002, 0x00000000, 0x00000000,
    0x00000002, 0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff,
    0x00000000, 0x00000000, 0xffffffff, 0x00000014, 0x00000000, 0x00000124, 0x46580200, 0x0033fffe,
    0x42415443, 0x0000001c, 0x00000097, 0x46580200, 0x00000002, 0x0000001c, 0x20000100, 0x00000094,
    0x00000044, 0x00000002, 0x00000001, 0x0000004c, 0x0000005c, 0x0000006c, 0x00010002, 0x00000001,
    0x00000074, 0x00000084, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000,
    0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x6576706f, 0x00327463, 0x00030001, 0x00040001,
    0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0xc0400000, 0x40800000, 0x4d007874, 0x6f726369,
    0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
    0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x000ffffe, 0x434c5846,
    0x00000001, 0x20500004, 0x00000002, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000002,
    0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000,
    0x00000000, 0xffffffff, 0x00000013, 0x00000000, 0x0000013c, 0x46580200, 0x0024fffe, 0x42415443,
    0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030,
    0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001,
    0x00000001, 0x00000000, 0x00000000, 0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369,
    0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
    0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000, 0x0024fffe, 0x434c5846,
    0x00000004, 0x10700001, 0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004,
    0x00000000, 0x10700001, 0x00000001, 0x00000000, 0x00000002, 0x00000001, 0x00000000, 0x00000004,
    0x00000001, 0x10700001, 0x00000001, 0x00000000, 0x00000002, 0x00000002, 0x00000000, 0x00000004,
    0x00000002, 0x10700001, 0x00000001, 0x00000000, 0x00000002, 0x00000003, 0x00000000, 0x00000004,
    0x00000003, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000012,
    0x00000000, 0x0000013c, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200,
    0x00000001, 0x0000001c, 0x20000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038,
    0x00000048, 0x6576706f, 0x00317463, 0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x00000000,
    0x80000000, 0xc00ccccd, 0x7f7fffff, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0002fffe, 0x54494c43, 0x00000000, 0x0024fffe, 0x434c5846, 0x00000004, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000001, 0x00000000, 0x00000004, 0x00000001, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000002, 0x00000000, 0x00000004, 0x00000002, 0x10300001, 0x00000001,
    0x00000000, 0x00000002, 0x00000003, 0x00000000, 0x00000004, 0x00000003, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000001, 0x00000002, 0x00000134, 0x00000008,
    0x615f7370, 0x00007272, 0x46580200, 0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200,
    0x00000001, 0x0000001c, 0x00000100, 0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038,
    0x00000048, 0x56695f67, 0x00746365, 0x00020001, 0x00040001, 0x00000001, 0x00000000, 0x40800000,
    0x40400000, 0x40000000, 0x3f800000, 0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820,
    0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131,
    0x0012fffe, 0x54494c43, 0x00000008, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xbff00000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x000ffffe, 0x434c5846, 0x00000001, 0xa0400001, 0x00000002,
    0x00000000, 0x00000002, 0x00000003, 0x00000000, 0x00000001, 0x00000004, 0x00000000, 0x00000004,
    0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0x00000000, 0x00000000, 0xffffffff, 0x00000000,
    0x00000002, 0x00000134, 0x00000008, 0x615f7376, 0x00327272, 0x46580200, 0x0024fffe, 0x42415443,
    0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x00000100, 0x00000058, 0x00000030,
    0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x56695f67, 0x00746365, 0x00020001, 0x00040001,
    0x00000001, 0x00000000, 0x40800000, 0x40400000, 0x40000000, 0x3f800000, 0x4d007874, 0x6f726369,
    0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320, 0x656c6970, 0x2e392072,
    0x392e3932, 0x332e3235, 0x00313131, 0x0012fffe, 0x54494c43, 0x00000008, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xbff00000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000ffffe, 0x434c5846,
    0x00000001, 0xa0400001, 0x00000002, 0x00000000, 0x00000002, 0x00000003, 0x00000000, 0x00000001,
    0x00000004, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0xffffffff,
    0x0000001c, 0x00000000, 0x00000002, 0x00000000, 0x000000f0, 0x46580200, 0x0026fffe, 0x42415443,
    0x0000001c, 0x00000063, 0x46580200, 0x00000001, 0x0000001c, 0x20000100, 0x00000060, 0x00000030,
    0x00000002, 0x00000001, 0x00000040, 0x00000050, 0x74636576, 0x6d61735f, 0x72656c70, 0xababab00,
    0x00030001, 0x00040001, 0x00000001, 0x00000000, 0x3f800000, 0x40000000, 0x40400000, 0x40800000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000,
    0x000ffffe, 0x434c5846, 0x00000001, 0xa0400001, 0x00000002, 0x00000000, 0x00000002, 0x00000001,
    0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f,
    0x0000ffff, 0xffffffff, 0x0000001c, 0x00000000, 0x00000001, 0x00000000, 0x000000dc, 0x46580200,
    0x0024fffe, 0x42415443, 0x0000001c, 0x0000005b, 0x46580200, 0x00000001, 0x0000001c, 0x20000100,
    0x00000058, 0x00000030, 0x00000002, 0x00000001, 0x00000038, 0x00000048, 0x56695f67, 0x00746365,
    0x00020001, 0x00040001, 0x00000001, 0x00000000, 0x40800000, 0x40400000, 0x40000000, 0x3f800000,
    0x4d007874, 0x6f726369, 0x74666f73, 0x29522820, 0x534c4820, 0x6853204c, 0x72656461, 0x6d6f4320,
    0x656c6970, 0x2e392072, 0x392e3932, 0x332e3235, 0x00313131, 0x0002fffe, 0x54494c43, 0x00000000,
    0x000cfffe, 0x434c5846, 0x00000001, 0x10000001, 0x00000001, 0x00000000, 0x00000002, 0x00000001,
    0x00000000, 0x00000004, 0x00000000, 0xf0f0f0f0, 0x0f0f0f0f, 0x0000ffff, 0xffffffff, 0x0000001c,
    0x00000000, 0x00000000, 0x00000001, 0x00000005, 0x31786574, 0x00000000,
};
#define TEST_EFFECT_PRESHADER_VSHADER_POS 2458
#define TEST_EFFECT_PRESHADER_VSHADER_LEN 13

static void test_effect_preshader(IDirect3DDevice9 *device)
{
    static const D3DXVECTOR4 test_effect_preshader_fconstsv[] =
    {
        {0.0f,   0.0f,  0.0f,  0.0f},
        {0.0f,   0.0f,  0.0f,  0.0f},
        {0.0f,   0.0f,  0.0f,  0.0f},
        {1.0f,   2.0f,  3.0f,  0.0f},
        {4.0f,   0.0f,  0.0f,  0.0f},
        {5.0f,   6.0f,  7.0f,  8.0f},
        {1.0f,   2.0f,  3.0f,  0.0f},
        {4.0f,   0.0f,  0.0f,  0.0f},
        {5.0f,   6.0f,  7.0f,  8.0f},
        {9.0f,  10.0f, 11.0f,  0.0f},
        {12.0f,  0.0f,  0.0f,  0.0f},
        {13.0f, 14.0f, 15.0f, 16.0f},
        {11.0f, 12.0f, 13.0f,  0.0f},
        {21.0f, 22.0f, 23.0f,  0.0f},
        {31.0f, 32.0f, 33.0f,  0.0f},
        {41.0f, 42.0f, 43.0f,  0.0f},
        {11.0f, 21.0f, 31.0f,  0.0f},
        {12.0f, 22.0f, 32.0f,  0.0f},
        {13.0f, 23.0f, 33.0f,  0.0f},
        {14.0f, 24.0f, 34.0f,  0.0f},
        {11.0f, 12.0f, 13.0f, 14.0f},
        {21.0f, 22.0f, 23.0f, 24.0f},
        {31.0f, 32.0f, 33.0f, 34.0f},
        {11.0f, 21.0f, 31.0f, 41.0f},
        {12.0f, 22.0f, 32.0f, 42.0f},
        {13.0f, 23.0f, 33.0f, 43.0f},
        {9.0f,  10.0f, 11.0f,  0.0f},
        {12.0f,  0.0f,  0.0f,  0.0f},
        {13.0f, 14.0f, 15.0f, 16.0f},
        {92.0f,  0.0f,  0.0f,  0.0f},
        {93.0f,  0.0f,  0.0f,  0.0f},
        {0.0f,   0.0f,  0.0f,  0.0f},
        {91.0f,  0.0f,  0.0f,  0.0f},
        {4.0f,   5.0f,  6.0f,  7.0f},
    };
    static const D3DXVECTOR4 test_effect_preshader_fconstsp[] =
    {
        {11.0f, 21.0f,  0.0f, 0.0f},
        {12.0f, 22.0f,  0.0f, 0.0f},
        {13.0f, 23.0f,  0.0f, 0.0f},
        {11.0f, 12.0f,  0.0f, 0.0f},
        {21.0f, 22.0f,  0.0f, 0.0f},
        {31.0f, 32.0f,  0.0f, 0.0f},
        {11.0f, 12.0f,  0.0f, 0.0f},
        {21.0f, 22.0f,  0.0f, 0.0f},
        {11.0f, 21.0f,  0.0f, 0.0f},
        {12.0f, 22.0f,  0.0f, 0.0f},
        {11.0f, 12.0f, 13.0f, 0.0f},
        {21.0f, 22.0f, 23.0f, 0.0f},
        {11.0f, 21.0f, 31.0f, 0.0f},
        {12.0f, 22.0f, 32.0f, 0.0f}
    };
    static const BOOL test_effect_preshader_bconsts[] =
    {
        TRUE, FALSE, TRUE, FALSE, TRUE, FALSE
    };
    static const int test_effect_preshader_iconsts[][4] =
    {
        {4, 3, 2, 1}
    };
    static const struct
    {
        const char *comment;
        BOOL todo[4];
        unsigned int result[4];
        unsigned int ulps;
    }
    test_effect_preshader_op_results[] =
    {
        {"1 / op", {FALSE, FALSE, FALSE, FALSE}, {0x7f800000, 0xff800000, 0xbee8ba2e, 0x00200000}},
        {"rsq",    {FALSE, FALSE, FALSE, FALSE}, {0x7f800000, 0x7f800000, 0x3f2c985c, 0x1f800001}, 1},
        {"mul",    {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x80000000, 0x40d33334, 0x7f800000}},
        {"add",    {FALSE, FALSE, FALSE, FALSE}, {0x3f800000, 0x40000000, 0xc0a66666, 0x7f7fffff}},
        {"lt",     {FALSE, FALSE, FALSE, FALSE}, {0x3f800000, 0x3f800000, 0x00000000, 0x00000000}},
        {"ge",     {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x00000000, 0x3f800000, 0x3f800000}},
        {"neg",    {FALSE, FALSE, FALSE, FALSE}, {0x80000000, 0x00000000, 0x400ccccd, 0xff7fffff}},
        {"rcp",    {FALSE, FALSE, FALSE, FALSE}, {0x7f800000, 0xff800000, 0xbee8ba2e, 0x00200000}},
        {"frac",   {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x00000000, 0x3f4ccccc, 0x00000000}},
        {"min",    {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x80000000, 0xc0400000, 0x40800000}},
        {"max",    {FALSE, FALSE, FALSE, FALSE}, {0x3f800000, 0x40000000, 0xc00ccccd, 0x7f7fffff}},
#if __x86_64__
        {"sin",    {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x80000000, 0xbf4ef99e, 0xbf0599b3}},
        {"cos",    {FALSE, FALSE, FALSE, FALSE}, {0x3f800000, 0x3f800000, 0xbf16a803, 0x3f5a5f96}},
#else
        {"sin",    {FALSE, FALSE, FALSE,  TRUE}, {0x00000000, 0x80000000, 0xbf4ef99e, 0x3f792dc4}},
        {"cos",    {FALSE, FALSE, FALSE,  TRUE}, {0x3f800000, 0x3f800000, 0xbf16a803, 0xbe6acefc}},
#endif
        {"den mul",{FALSE, FALSE, FALSE, FALSE}, {0x7f800000, 0xff800000, 0xbb94f209, 0x000051ec}},
        {"dot",    {FALSE, FALSE, FALSE, FALSE}, {0x00000000, 0x7f800000, 0x41f00000, 0x00000000}},
        {"prec",   {FALSE, FALSE,  TRUE, FALSE}, {0x2b8cbccc, 0x2c0cbccc, 0xac531800, 0x00000000}},
        {"dotswiz", {FALSE, FALSE, FALSE, FALSE}, {0xc00ccccd, 0xc0d33334, 0xc10ccccd, 0}},
        {"reladdr", { TRUE,  TRUE,  TRUE,  TRUE}, {0xc00ccccd, 0x40000000, 0x41a00000, 0x41500000}},
    };
#define TEST_EFFECT_PRES_NFLOATV ARRAY_SIZE(test_effect_preshader_fconstsv)
#define TEST_EFFECT_PRES_NFLOATP ARRAY_SIZE(test_effect_preshader_fconstsp)
#define TEST_EFFECT_PRES_NFLOATMAX (TEST_EFFECT_PRES_NFLOATV > TEST_EFFECT_PRES_NFLOATP ? \
            TEST_EFFECT_PRES_NFLOATV : TEST_EFFECT_PRES_NFLOATP)
#define TEST_EFFECT_PRES_NBOOL ARRAY_SIZE(test_effect_preshader_bconsts)
#define TEST_EFFECT_PRES_NINT ARRAY_SIZE(test_effect_preshader_iconsts)
#define TEST_EFFECT_PRES_NOPTESTS ARRAY_SIZE(test_effect_preshader_op_results)

    static const D3DXVECTOR4 fvect1 = {28.0f, 29.0f, 30.0f, 31.0f};
    static const D3DXVECTOR4 fvect2 = {0.0f, 0.0f, 1.0f, 0.0f};
    static const D3DXVECTOR4 fvect_empty = {-9999.0f, -9999.0f, -9999.0f, -9999.0f};
    static const int ivect_empty[4] = {-1, -1, -1, -1};
    HRESULT hr;
    ID3DXEffect *effect;
    D3DXHANDLE par;
    unsigned int npasses;
    DWORD value;
    BOOL bval;
    D3DLIGHT9 light;
    D3DXVECTOR4 fdata[TEST_EFFECT_PRES_NFLOATMAX];
    int idata[TEST_EFFECT_PRES_NINT][4];
    BOOL bdata[TEST_EFFECT_PRES_NBOOL];
    IDirect3DVertexShader9 *vshader;
    void *byte_code;
    unsigned int byte_code_size;
    unsigned int i, j;
    D3DCAPS9 caps;

    hr = IDirect3DDevice9_GetDeviceCaps(device, &caps);
    ok(SUCCEEDED(hr), "Failed to get device caps, hr %#x.\n", hr);
    if (caps.VertexShaderVersion < D3DVS_VERSION(3, 0)
            || caps.PixelShaderVersion < D3DPS_VERSION(3, 0))
    {
        skip("Test requires VS >= 3 and PS >= 3, skipping.\n");
        return;
    }

    hr = D3DXCreateEffect(device, test_effect_preshader_effect_blob, sizeof(test_effect_preshader_effect_blob),
            NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    for (i = 0; i < 256; ++i)
    {
        hr = IDirect3DDevice9_SetVertexShaderConstantF(device, i, &fvect_empty.x, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
    }
    for (i = 0; i < 224; ++i)
    {
        hr = IDirect3DDevice9_SetPixelShaderConstantF(device, i, &fvect_empty.x, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
    }
    bval = FALSE;
    for (i = 0; i < 16; ++i)
    {
        hr = IDirect3DDevice9_SetPixelShaderConstantB(device, i, &bval, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
    }
    for (i = 0; i < 16; ++i)
    {
        hr = IDirect3DDevice9_SetPixelShaderConstantI(device, i, ivect_empty, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
    }

    hr = effect->lpVtbl->Begin(effect, &npasses, 0);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    par = effect->lpVtbl->GetParameterByName(effect, NULL, "g_Pos2");
    ok(par != NULL, "GetParameterByName failed.\n");

    hr = effect->lpVtbl->SetVector(effect, par, &fvect1);
    ok(hr == D3D_OK, "SetVector failed, hr %#x.\n", hr);

    hr = effect->lpVtbl->BeginPass(effect, 0);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    hr = IDirect3DDevice9_GetVertexShaderConstantF(device, 0, &fdata[0].x, TEST_EFFECT_PRES_NFLOATV);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(!memcmp(fdata, test_effect_preshader_fconstsv, sizeof(test_effect_preshader_fconstsv)),
            "Vertex shader float constants do not match.\n");
    for (i = TEST_EFFECT_PRES_NFLOATV; i < 256; ++i)
    {
        hr = IDirect3DDevice9_GetVertexShaderConstantF(device, i, &fdata[0].x, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
        ok(!memcmp(fdata, &fvect_empty, sizeof(fvect_empty)),
                "Vertex shader float constants do not match.\n");
    }
    hr = IDirect3DDevice9_GetPixelShaderConstantF(device, 0, &fdata[0].x, TEST_EFFECT_PRES_NFLOATP);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(!memcmp(fdata, test_effect_preshader_fconstsp, sizeof(test_effect_preshader_fconstsp)),
            "Pixel shader float constants do not match.\n");
    for (i = TEST_EFFECT_PRES_NFLOATP; i < 224; ++i)
    {
        hr = IDirect3DDevice9_GetPixelShaderConstantF(device, i, &fdata[0].x, 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
        ok(!memcmp(fdata, &fvect_empty, sizeof(fvect_empty)),
                "Pixel shader float constants do not match.\n");
    }
    hr = IDirect3DDevice9_GetPixelShaderConstantI(device, 0, idata[0], TEST_EFFECT_PRES_NINT);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(!memcmp(idata, test_effect_preshader_iconsts, sizeof(test_effect_preshader_iconsts)),
            "Pixel shader integer constants do not match.\n");
    for (i = TEST_EFFECT_PRES_NINT; i < 16; ++i)
    {
        hr = IDirect3DDevice9_GetPixelShaderConstantI(device, i, idata[0], 1);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
        ok(!memcmp(idata[0], ivect_empty, sizeof(ivect_empty)),
                "Pixel shader integer constants do not match.\n");
    }

    hr = IDirect3DDevice9_GetPixelShaderConstantB(device, 0, bdata, TEST_EFFECT_PRES_NBOOL);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    for (i = 0; i < TEST_EFFECT_PRES_NBOOL; ++i)
        todo_wine_if(!bdata[i] != !test_effect_preshader_bconsts[i])
        ok(!bdata[i] == !test_effect_preshader_bconsts[i],
                "Pixel shader boolean constants do not match.\n");
    for (; i < 16; ++i)
    {
        hr = IDirect3DDevice9_GetPixelShaderConstantB(device, i, &bval, 1);
        ok(hr == D3D_OK && !bval, "Got result %#x, boolean register value %u.\n", hr, bval);
    }

    for (i = 0; i < TEST_EFFECT_PRES_NOPTESTS; ++i)
    {
        float *v;

        hr = IDirect3DDevice9_GetLight(device, i % 8, &light);
        v = i < 8 ? &light.Diffuse.r : (i < 16 ? &light.Ambient.r : &light.Specular.r);
        ok(hr == D3D_OK, "Got result %#x.\n", hr);
        for (j = 0; j < 4; ++j)
        {
            todo_wine_if(test_effect_preshader_op_results[i].todo[j])
            ok(compare_float(v[j], ((float *)test_effect_preshader_op_results[i].result)[j],
                    test_effect_preshader_op_results[i].ulps),
                    "Operation %s, component %u, expected %#x, got %#x (%g).\n",
                    test_effect_preshader_op_results[i].comment, j,
                    test_effect_preshader_op_results[i].result[j], ((unsigned int *)v)[j], v[j]);
        }
    }

    hr = IDirect3DDevice9_GetSamplerState(device, 0, D3DSAMP_MINFILTER, &value);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(value == 3, "Unexpected sampler 0 minfilter %u.\n", value);
    hr = IDirect3DDevice9_GetSamplerState(device, 0, D3DSAMP_MAGFILTER, &value);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    todo_wine ok(value == 3, "Unexpected sampler 0 magfilter %u.\n", value);
    hr = IDirect3DDevice9_GetSamplerState(device, D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MINFILTER, &value);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(value == 3, "Unexpected sampler 0 minfilter %u.\n", value);
    hr = IDirect3DDevice9_GetSamplerState(device, D3DVERTEXTEXTURESAMPLER0, D3DSAMP_MAGFILTER, &value);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    todo_wine ok(value == 3, "Unexpected sampler 0 magfilter %u.\n", value);

    hr = effect->lpVtbl->EndPass(effect);

    par = effect->lpVtbl->GetParameterByName(effect, NULL, "g_iVect");
    ok(par != NULL, "GetParameterByName failed.\n");
    hr = effect->lpVtbl->SetVector(effect, par, &fvect2);
    ok(hr == D3D_OK, "SetVector failed, hr %#x.\n", hr);
    hr = effect->lpVtbl->BeginPass(effect, 1);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    hr = IDirect3DDevice9_GetVertexShader(device, &vshader);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(!!vshader, "Got NULL vshader.\n");

    hr = IDirect3DVertexShader9_GetFunction(vshader, NULL, &byte_code_size);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    byte_code = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, byte_code_size);
    hr = IDirect3DVertexShader9_GetFunction(vshader, byte_code, &byte_code_size);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(byte_code_size > 1, "Got unexpected byte code size %u.\n", byte_code_size);
    ok(!memcmp(byte_code,
            &test_effect_preshader_effect_blob[TEST_EFFECT_PRESHADER_VSHADER_POS +
            TEST_EFFECT_PRESHADER_VSHADER_LEN], byte_code_size),
            "Incorrect shader selected.\n");
    HeapFree(GetProcessHeap(), 0, byte_code);
    IDirect3DVertexShader9_Release(vshader);

    hr = IDirect3DDevice9_SetVertexShader(device, NULL);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    hr = effect->lpVtbl->SetVector(effect, par, &fvect1);
    ok(hr == D3D_OK, "SetVector failed, hr %#x.\n", hr);
    hr = effect->lpVtbl->CommitChanges(effect);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    hr = IDirect3DDevice9_GetVertexShader(device, &vshader);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    ok(!vshader, "Incorrect shader selected.\n");

    hr = effect->lpVtbl->EndPass(effect);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    hr = effect->lpVtbl->End(effect);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    effect->lpVtbl->Release(effect);
}

struct test_preshader_op_def
{
    const char *mnem;
    unsigned int opcode;
    unsigned int args_count;
    unsigned int expected_result[4];
    D3DXVECTOR4 fvect1, fvect2;
    unsigned int ulps;
    BOOL todo[4];
};

static void test_preshader_op(IDirect3DDevice9 *device, const DWORD *sample_effect_blob,
        unsigned int sample_effect_blob_size, const struct test_preshader_op_def *test)
{
    static const struct
    {
        unsigned int pos;
        unsigned int result_index;
    }
    blob_position[] =
    {
        {0, 0},
        {4334, 0},
        {4185, 2}
    };
    DWORD *test_effect_blob;
    HRESULT hr;
    ID3DXEffect *effect;
    D3DLIGHT9 light;
    unsigned int i, passes_count;
    float *v;
    unsigned int op_pos, op_step;
    D3DXHANDLE param;

    op_step = 2 + (test->args_count + 1) * 3;
    op_pos = blob_position[test->args_count].pos;

    test_effect_blob = HeapAlloc(GetProcessHeap(), 0, sample_effect_blob_size);
    memcpy(test_effect_blob, sample_effect_blob, sample_effect_blob_size);
    for (i = 0; i < 4; ++i)
        test_effect_blob[op_pos + i * op_step] = test->opcode;

    hr = D3DXCreateEffect(device, test_effect_blob, sample_effect_blob_size,
            NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    hr = effect->lpVtbl->Begin(effect, &passes_count, 0);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    param = effect->lpVtbl->GetParameterByName(effect, NULL, "opvect1");
    ok(!!param, "GetParameterByName failed.\n");
    hr = effect->lpVtbl->SetVector(effect, param, &test->fvect1);
    ok(hr == D3D_OK, "SetVector failed, hr %#x.\n", hr);

    if (test->args_count > 1)
    {
        param = effect->lpVtbl->GetParameterByName(effect, NULL, "opvect2");
        ok(!!param, "GetParameterByName failed.\n");
        hr = effect->lpVtbl->SetVector(effect, param, &test->fvect2);
        ok(hr == D3D_OK, "SetVector failed, hr %#x.\n", hr);
    }

    hr = effect->lpVtbl->BeginPass(effect, 0);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    hr = IDirect3DDevice9_GetLight(device, blob_position[test->args_count].result_index, &light);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    v = &light.Diffuse.r;
    for (i = 0; i < 4; ++i)
        todo_wine_if(test->todo[i])
        ok(compare_float(v[i], ((float *)test->expected_result)[i], test->ulps),
                "Operation %s, component %u, expected %#x (%g), got %#x (%g).\n", test->mnem, i,
                test->expected_result[i], ((float *)test->expected_result)[i], ((unsigned int *)v)[i], v[i]);

    hr = effect->lpVtbl->EndPass(effect);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    hr = effect->lpVtbl->End(effect);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    effect->lpVtbl->Release(effect);
    HeapFree(GetProcessHeap(), 0, test_effect_blob);
}

static void test_effect_preshader_ops(IDirect3DDevice9 *device)
{
    const struct test_preshader_op_def op_tests[] =
    {
        {"exp", 0x10500001, 1, {0x3f800000, 0x3f800000, 0x3e5edc66, 0x7f800000},
                {0.0f, -0.0f, -2.2f, 3.402823466e+38f}, {1.0f, 2.0f, -3.0f, 4.0f}},
        {"0 * INF", 0x20500004, 2, {0xffc00000, 0xffc00000, 0xc0d33334, 0x7f800000},
                {0.0f, -0.0f, -2.2f, 3.402823466e+38f}, {INFINITY, INFINITY, 3.0f, 4.0f}},
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(op_tests); ++i)
        test_preshader_op(device, test_effect_preshader_effect_blob, sizeof(test_effect_preshader_effect_blob),
                &op_tests[i]);
}

static void test_isparameterused_children(ID3DXEffect *effect, D3DXHANDLE tech, D3DXHANDLE param)
{
    D3DXPARAMETER_DESC desc;
    D3DXHANDLE param_child;
    unsigned int i, child_count;
    HRESULT hr;

    hr = effect->lpVtbl->GetParameterDesc(effect, param, &desc);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);
    child_count = desc.Elements ? desc.Elements : desc.StructMembers;
    for (i = 0; i < child_count; ++i)
    {
        param_child = desc.Elements ? effect->lpVtbl->GetParameterElement(effect, param, i)
                : effect->lpVtbl->GetParameter(effect, param, i);
        ok(!!param_child, "Failed getting child parameter %s[%u].\n", desc.Name, i);
        ok(!effect->lpVtbl->IsParameterUsed(effect, param_child, tech),
                "Unexpected IsParameterUsed() result for %s[%u].\n", desc.Name, i);
        test_isparameterused_children(effect, tech, param_child);
    }
}

static void test_isparameterused_param_with_children(ID3DXEffect *effect, D3DXHANDLE tech, const char *name,
        BOOL expected_result)
{
    D3DXHANDLE param;

    param = effect->lpVtbl->GetParameterByName(effect, NULL, name);
    ok(!!param, "GetParameterByName failed for %s.\n", name);

    ok(!effect->lpVtbl->IsParameterUsed(effect, param, tech) == !expected_result,
            "Unexpected IsParameterUsed() result for %s.\n", name);

    test_isparameterused_children(effect, tech, param);
}

static void test_effect_isparameterused(IDirect3DDevice9 *device)
{
    static const struct
    {
        const char *name;
        BOOL expected_result;
    }
    check_parameters[] =
    {
        {"g_Pos1", TRUE},
        {"g_Pos2", TRUE},
        {"g_Selector", TRUE},
        {"opvect1", TRUE},
        {"opvect2", TRUE},
        {"opvect3", TRUE},
        {"arr2", TRUE},
        {"vs_arr", TRUE},
        {"g_iVect", TRUE},
        {"vect_sampler", TRUE},
        {"tex1", TRUE},
        {"tex2", FALSE},
        {"sampler1", TRUE},
        {"ts1", TRUE},
        {"ts2", TRUE},
        {"ts3", TRUE},
    };
    ID3DXEffect *effect;
    HRESULT hr;
    D3DXHANDLE tech;
    unsigned int i;

    hr = D3DXCreateEffect(device, test_effect_preshader_effect_blob, sizeof(test_effect_preshader_effect_blob),
            NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "Got result %#x.\n", hr);

    tech = effect->lpVtbl->GetTechniqueByName(effect, "tech0");
    ok(!!tech, "GetTechniqueByName failed.\n");

    for (i = 0; i < ARRAY_SIZE(check_parameters); ++i)
        test_isparameterused_param_with_children(effect, tech, check_parameters[i].name,
                check_parameters[i].expected_result);

    effect->lpVtbl->Release(effect);
}

/*
 * fxc.exe /Tfx_2_0
 */
#if 0
technique t1 { pass p { ZEnable = TRUE; } }
technique t2 { pass p { ZEnable = FALSE; } }
#endif
static const DWORD test_effect_technique_validation_blob[] =
{
0xfeff0901, 0x00000064, 0x00000000, 0x00000001, 0x00000002, 0x00000002, 0x00000000, 0x00000000,
0x00000000, 0x00000001, 0x00000001, 0x00000002, 0x00000070, 0x00000003, 0x00003174, 0x00000000,
0x00000002, 0x00000002, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x00000002,
0x00000070, 0x00000003, 0x00003274, 0x00000000, 0x00000002, 0x00000002, 0x00000001, 0x0000002c,
0x00000000, 0x00000001, 0x00000024, 0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000008,
0x00000004, 0x0000005c, 0x00000000, 0x00000001, 0x00000054, 0x00000000, 0x00000001, 0x00000000,
0x00000000, 0x00000038, 0x00000034, 0x00000000, 0x00000000
};

static void test_effect_technique_validation(IDirect3DDevice9 *device)
{
    ID3DXEffect *effect;
    ULONG count;
    D3DXHANDLE technique1, technique2, technique;
    HRESULT hr;

    hr = D3DXCreateEffect(device, test_effect_technique_validation_blob,
            sizeof(test_effect_technique_validation_blob), NULL, NULL, 0, NULL, &effect, NULL);
    ok(hr == D3D_OK, "D3DXCreateEffect failed, got %#x, expected %#x\n", hr, D3D_OK);

    technique1 = effect->lpVtbl->GetTechniqueByName(effect, "t1");
    ok(technique1 != NULL, "Failed to get technique\n");
    technique2 = effect->lpVtbl->GetTechniqueByName(effect, "t2");
    ok(technique2 != NULL, "Failed to get technique\n");

#if 0 /* This crashes on Windows */
    hr = effect->lpVtbl->FindNextValidTechnique(effect, (D3DXHANDLE)0xdeadbeef, &technique);
#endif
    hr = effect->lpVtbl->FindNextValidTechnique(effect, NULL, NULL);
    ok(hr == D3DERR_INVALIDCALL, "FindNextValidTechnique, got %#x, expected %#x\n", hr, D3DERR_INVALIDCALL);

    technique = (D3DXHANDLE)0xdeadbeef;
    hr = effect->lpVtbl->FindNextValidTechnique(effect, NULL, &technique);
    ok(hr == D3D_OK, "FindNextValidTechnique failed, got %#x, expected %#x\n", hr, D3D_OK);
    ok(technique == technique1, "Technique returned %p, expected %p\n", technique, technique1);

    technique = (D3DXHANDLE)0xdeadbeef;
    hr = effect->lpVtbl->FindNextValidTechnique(effect, technique1, &technique);
    ok(hr == D3D_OK, "FindNextValidTechnique failed, got %#x, expected %#x\n", hr, D3D_OK);
    ok(technique == technique2, "Technique returned %p, expected %p\n", technique, technique2);

    technique = (D3DXHANDLE)0xdeadbeef;
    hr = effect->lpVtbl->FindNextValidTechnique(effect, technique2, &technique);
    ok(hr == S_FALSE, "FindNextValidTechnique, got %#x, expected %#x\n", hr, S_FALSE);
    ok(technique == NULL, "Technique returned %p, expected %p\n", technique, NULL);

    count = effect->lpVtbl->Release(effect);
    ok(!count, "Release failed %u\n", count);
}

START_TEST(effect)
{
    HWND wnd;
    IDirect3D9 *d3d;
    IDirect3DDevice9 *device;
    D3DPRESENT_PARAMETERS d3dpp;
    HRESULT hr;
    ULONG count;

    if (!(wnd = CreateWindowA("static", "d3dx9_test", WS_OVERLAPPEDWINDOW, 0, 0,
            640, 480, NULL, NULL, NULL, NULL)))
    {
        skip("Couldn't create application window\n");
        return;
    }
    if (!(d3d = Direct3DCreate9(D3D_SDK_VERSION)))
    {
        skip("Couldn't create IDirect3D9 object\n");
        DestroyWindow(wnd);
        return;
    }

    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &device);
    if (FAILED(hr)) {
        skip("Failed to create IDirect3DDevice9 object %#x\n", hr);
        IDirect3D9_Release(d3d);
        DestroyWindow(wnd);
        return;
    }

    test_create_effect_and_pool(device);
    test_create_effect_compiler();
    test_effect_parameter_value(device);
    test_effect_setvalue_object(device);
    test_effect_variable_names(device);
    test_effect_compilation_errors(device);
    test_effect_states(device);
    test_effect_preshader(device);
    test_effect_preshader_ops(device);
    test_effect_isparameterused(device);
    test_effect_technique_validation(device);

    count = IDirect3DDevice9_Release(device);
    ok(count == 0, "The device was not properly freed: refcount %u\n", count);

    count = IDirect3D9_Release(d3d);
    ok(count == 0, "Release failed %u\n", count);

    DestroyWindow(wnd);
}
