/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
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
#include "wine/port.h"

#include "dxgi_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

static void dxgi_mode_from_wined3d(DXGI_MODE_DESC *mode, const struct wined3d_display_mode *wined3d_mode)
{
    mode->Width = wined3d_mode->width;
    mode->Height = wined3d_mode->height;
    mode->RefreshRate.Numerator = wined3d_mode->refresh_rate;
    mode->RefreshRate.Denominator = 1;
    mode->Format = dxgi_format_from_wined3dformat(wined3d_mode->format_id);
    mode->ScanlineOrdering = wined3d_mode->scanline_ordering;
    mode->Scaling = DXGI_MODE_SCALING_UNSPECIFIED; /* FIXME */
}

static inline struct dxgi_output *impl_from_IDXGIOutput(IDXGIOutput *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_output, IDXGIOutput_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_QueryInterface(IDXGIOutput *iface, REFIID riid, void **object)
{
    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_IDXGIOutput)
            || IsEqualGUID(riid, &IID_IDXGIObject)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(riid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dxgi_output_AddRef(IDXGIOutput *iface)
{
    struct dxgi_output *This = impl_from_IDXGIOutput(iface);
    ULONG refcount = InterlockedIncrement(&This->refcount);

    TRACE("%p increasing refcount to %u.\n", This, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_output_Release(IDXGIOutput *iface)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);
    ULONG refcount = InterlockedDecrement(&output->refcount);

    TRACE("%p decreasing refcount to %u.\n", output, refcount);

    if (!refcount)
    {
        wined3d_private_store_cleanup(&output->private_store);
        IDXGIAdapter1_Release(&output->adapter->IDXGIAdapter1_iface);
        HeapFree(GetProcessHeap(), 0, output);
    }

    return refcount;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_SetPrivateData(IDXGIOutput *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&output->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetPrivateDataInterface(IDXGIOutput *iface,
        REFGUID guid, const IUnknown *object)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&output->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetPrivateData(IDXGIOutput *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&output->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetParent(IDXGIOutput *iface,
        REFIID riid, void **parent)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);

    TRACE("iface %p, riid %s, parent %p.\n", iface, debugstr_guid(riid), parent);

    return IDXGIAdapter1_QueryInterface(&output->adapter->IDXGIAdapter1_iface, riid, parent);
}

/* IDXGIOutput methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDesc(IDXGIOutput *iface, DXGI_OUTPUT_DESC *desc)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);
    struct wined3d_output_desc wined3d_desc;
    HRESULT hr;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return E_INVALIDARG;

    wined3d_mutex_lock();
    hr = wined3d_get_output_desc(output->adapter->factory->wined3d,
            output->adapter->ordinal, &wined3d_desc);
    wined3d_mutex_unlock();

    if (FAILED(hr))
    {
        WARN("Failed to get output desc, hr %#x.\n", hr);
        return hr;
    }

    memcpy(desc->DeviceName, wined3d_desc.device_name, sizeof(desc->DeviceName));
    desc->DesktopCoordinates = wined3d_desc.desktop_rect;
    desc->AttachedToDesktop = wined3d_desc.attached_to_desktop;
    desc->Rotation = wined3d_desc.rotation;
    desc->Monitor = wined3d_desc.monitor;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplayModeList(IDXGIOutput *iface,
        DXGI_FORMAT format, UINT flags, UINT *mode_count, DXGI_MODE_DESC *desc)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);
    enum wined3d_format_id wined3d_format;
    unsigned int i, max_count;
    struct wined3d *wined3d;

    FIXME("iface %p, format %s, flags %#x, mode_count %p, desc %p partial stub!\n",
            iface, debug_dxgi_format(format), flags, mode_count, desc);

    if (!mode_count)
        return DXGI_ERROR_INVALID_CALL;

    if (format == DXGI_FORMAT_UNKNOWN)
    {
        *mode_count = 0;
        return S_OK;
    }

    wined3d = output->adapter->factory->wined3d;
    wined3d_format = wined3dformat_from_dxgi_format(format);

    wined3d_mutex_lock();
    max_count = wined3d_get_adapter_mode_count(wined3d, output->adapter->ordinal,
            wined3d_format, WINED3D_SCANLINE_ORDERING_UNKNOWN);

    if (!desc)
    {
        wined3d_mutex_unlock();
        *mode_count = max_count;
        return S_OK;
    }

    if (max_count > *mode_count)
    {
        wined3d_mutex_unlock();
        return DXGI_ERROR_MORE_DATA;
    }

    *mode_count = max_count;

    for (i = 0; i < *mode_count; ++i)
    {
        struct wined3d_display_mode mode;
        HRESULT hr;

        hr = wined3d_enum_adapter_modes(wined3d, output->adapter->ordinal, wined3d_format,
                WINED3D_SCANLINE_ORDERING_UNKNOWN, i, &mode);
        if (FAILED(hr))
        {
            WARN("EnumAdapterModes failed, hr %#x.\n", hr);
            wined3d_mutex_unlock();
            return hr;
        }

        dxgi_mode_from_wined3d(&desc[i], &mode);
    }
    wined3d_mutex_unlock();

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_FindClosestMatchingMode(IDXGIOutput *iface,
        const DXGI_MODE_DESC *mode, DXGI_MODE_DESC *closest_match, IUnknown *device)
{
    struct dxgi_output *output = impl_from_IDXGIOutput(iface);
    struct wined3d_display_mode wined3d_mode;
    struct dxgi_adapter *adapter;
    struct wined3d *wined3d;
    HRESULT hr;

    TRACE("iface %p, mode %p, closest_match %p, device %p.\n", iface, mode, closest_match, device);

    if ((!mode->Width && mode->Height) || (mode->Width && !mode->Height))
        return DXGI_ERROR_INVALID_CALL;

    if (mode->Format == DXGI_FORMAT_UNKNOWN && !device)
        return DXGI_ERROR_INVALID_CALL;

    TRACE("Mode: %s.\n", debug_dxgi_mode(mode));
    if (mode->Format == DXGI_FORMAT_UNKNOWN)
    {
        FIXME("Matching formats to device not implemented.\n");
        return E_NOTIMPL;
    }

    adapter = output->adapter;
    wined3d = adapter->factory->wined3d;

    wined3d_mutex_lock();
    wined3d_display_mode_from_dxgi(&wined3d_mode, mode);
    hr = wined3d_find_closest_matching_adapter_mode(wined3d, adapter->ordinal, &wined3d_mode);
    wined3d_mutex_unlock();

    if (SUCCEEDED(hr))
    {
        dxgi_mode_from_wined3d(closest_match, &wined3d_mode);
        TRACE("Returning %s.\n", debug_dxgi_mode(closest_match));
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_WaitForVBlank(IDXGIOutput *iface)
{
    static BOOL once = FALSE;

    if (!once++)
        FIXME("iface %p stub!\n", iface);
    else
        TRACE("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_TakeOwnership(IDXGIOutput *iface, IUnknown *device, BOOL exclusive)
{
    FIXME("iface %p, device %p, exclusive %d stub!\n", iface, device, exclusive);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE dxgi_output_ReleaseOwnership(IDXGIOutput *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetGammaControlCapabilities(IDXGIOutput *iface,
        DXGI_GAMMA_CONTROL_CAPABILITIES *gamma_caps)
{
    int i;

    TRACE("iface %p, gamma_caps %p.\n", iface, gamma_caps);

    if (!gamma_caps)
        return E_INVALIDARG;

    gamma_caps->ScaleAndOffsetSupported = FALSE;
    gamma_caps->MaxConvertedValue = 1.0;
    gamma_caps->MinConvertedValue = 0.0;
    gamma_caps->NumGammaControlPoints = 256;

    for (i = 0; i < 256; i++)
        gamma_caps->ControlPointPositions[i] = i / 255.0f;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetGammaControl(IDXGIOutput *iface,
        const DXGI_GAMMA_CONTROL *gamma_control)
{
    struct wined3d_gamma_ramp ramp;
    HDC dc;
    int i;

    TRACE("iface %p, gamma_control %p.\n", iface, gamma_control);

    if (!gamma_control)
        return E_INVALIDARG;

    for (i = 0; i < 256; i++)
    {
        ramp.red[i] = gamma_control->GammaCurve[i].Red * 65535;
        ramp.green[i] = gamma_control->GammaCurve[i].Green * 65535;
        ramp.blue[i] = gamma_control->GammaCurve[i].Blue * 65535;
    }

    /* we can not use wined3d_swapchain_set_gamma_ramp here, because outputs don't have
     * references to swapchains and the output handling of dxgi is far from complete yet */
    dc = GetDC(0);
    SetDeviceGammaRamp(dc, &ramp);
    ReleaseDC(0, dc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetGammaControl(IDXGIOutput *iface, DXGI_GAMMA_CONTROL *gamma_control)
{
    struct wined3d_gamma_ramp ramp;
    HDC dc;
    int i;

    TRACE("iface %p, gamma_control %p.\n", iface, gamma_control);

    /* We can not use wined3d_swapchain_get_gamma_ramp here, because outputs don't have
     * references to swapchains and the output handling of dxgi is far from complete yet */
    dc = GetDC(0);
    GetDeviceGammaRamp(dc, &ramp);
    ReleaseDC(0, dc);

    gamma_control->Scale.Red    = 0.0f;
    gamma_control->Scale.Green  = 0.0f;
    gamma_control->Scale.Blue   = 0.0f;
    gamma_control->Offset.Red   = 0.0f;
    gamma_control->Offset.Green = 0.0f;
    gamma_control->Offset.Blue  = 0.0f;

    for (i = 0; i < 256; i++)
    {
        gamma_control->GammaCurve[i].Red = ramp.red[i] / 65535.0f;
        gamma_control->GammaCurve[i].Green = ramp.green[i] / 65535.0f;
        gamma_control->GammaCurve[i].Blue = ramp.blue[i] / 65535.0f;
    }

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetDisplaySurface(IDXGIOutput *iface, IDXGISurface *surface)
{
    FIXME("iface %p, surface %p stub!\n", iface, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplaySurfaceData(IDXGIOutput *iface, IDXGISurface *surface)
{
    FIXME("iface %p, surface %p stub!\n", iface, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetFrameStatistics(IDXGIOutput *iface, DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

static const struct IDXGIOutputVtbl dxgi_output_vtbl =
{
    dxgi_output_QueryInterface,
    dxgi_output_AddRef,
    dxgi_output_Release,
    /* IDXGIObject methods */
    dxgi_output_SetPrivateData,
    dxgi_output_SetPrivateDataInterface,
    dxgi_output_GetPrivateData,
    dxgi_output_GetParent,
    /* IDXGIOutput methods */
    dxgi_output_GetDesc,
    dxgi_output_GetDisplayModeList,
    dxgi_output_FindClosestMatchingMode,
    dxgi_output_WaitForVBlank,
    dxgi_output_TakeOwnership,
    dxgi_output_ReleaseOwnership,
    dxgi_output_GetGammaControlCapabilities,
    dxgi_output_SetGammaControl,
    dxgi_output_GetGammaControl,
    dxgi_output_SetDisplaySurface,
    dxgi_output_GetDisplaySurfaceData,
    dxgi_output_GetFrameStatistics,
};

static void dxgi_output_init(struct dxgi_output *output, struct dxgi_adapter *adapter)
{
    output->IDXGIOutput_iface.lpVtbl = &dxgi_output_vtbl;
    output->refcount = 1;
    wined3d_private_store_init(&output->private_store);
    output->adapter = adapter;
    IDXGIAdapter1_AddRef(&output->adapter->IDXGIAdapter1_iface);
}

HRESULT dxgi_output_create(struct dxgi_adapter *adapter, struct dxgi_output **output)
{
    if (!(*output = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(**output))))
        return E_OUTOFMEMORY;

    dxgi_output_init(*output, adapter);
    return S_OK;
}
