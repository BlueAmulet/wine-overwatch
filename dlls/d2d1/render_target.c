/*
 * Copyright 2014 Henri Verbeet for CodeWeavers
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

#include "d2d1_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

#define INITIAL_CLIP_STACK_SIZE 4

static const D2D1_MATRIX_3X2_F identity =
{
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
};

struct d2d_draw_text_layout_ctx
{
    ID2D1Brush *brush;
    D2D1_DRAW_TEXT_OPTIONS options;
};

static ID2D1Brush *d2d_draw_get_text_brush(struct d2d_draw_text_layout_ctx *context, IUnknown *effect)
{
    ID2D1Brush *brush = NULL;

    if (effect && SUCCEEDED(IUnknown_QueryInterface(effect, &IID_ID2D1Brush, (void**)&brush)))
        return brush;

    ID2D1Brush_AddRef(context->brush);
    return context->brush;
}

static void d2d_rect_intersect(D2D1_RECT_F *dst, const D2D1_RECT_F *src)
{
    if (src->left > dst->left)
        dst->left = src->left;
    if (src->top > dst->top)
        dst->top = src->top;
    if (src->right < dst->right)
        dst->right = src->right;
    if (src->bottom < dst->bottom)
        dst->bottom = src->bottom;
}

static void d2d_rect_set(D2D1_RECT_F *dst, float left, float top, float right, float bottom)
{
    dst->left = left;
    dst->top = top;
    dst->right = right;
    dst->bottom = bottom;
}

static void d2d_size_set(D2D1_SIZE_U *dst, float width, float height)
{
    dst->width = width;
    dst->height = height;
}

static BOOL d2d_clip_stack_init(struct d2d_clip_stack *stack)
{
    if (!(stack->stack = HeapAlloc(GetProcessHeap(), 0, INITIAL_CLIP_STACK_SIZE * sizeof(*stack->stack))))
        return FALSE;

    stack->size = INITIAL_CLIP_STACK_SIZE;
    stack->count = 0;

    return TRUE;
}

static void d2d_clip_stack_cleanup(struct d2d_clip_stack *stack)
{
    HeapFree(GetProcessHeap(), 0, stack->stack);
}

static BOOL d2d_clip_stack_push(struct d2d_clip_stack *stack, const D2D1_RECT_F *rect)
{
    D2D1_RECT_F r;

    if (stack->count == stack->size)
    {
        D2D1_RECT_F *new_stack;
        unsigned int new_size;

        if (stack->size > UINT_MAX / 2)
            return FALSE;

        new_size = stack->size * 2;
        if (!(new_stack = HeapReAlloc(GetProcessHeap(), 0, stack->stack, new_size * sizeof(*stack->stack))))
            return FALSE;

        stack->stack = new_stack;
        stack->size = new_size;
    }

    r = *rect;
    if (stack->count)
        d2d_rect_intersect(&r, &stack->stack[stack->count - 1]);
    stack->stack[stack->count++] = r;

    return TRUE;
}

static void d2d_clip_stack_pop(struct d2d_clip_stack *stack)
{
    if (!stack->count)
        return;
    --stack->count;
}

static void d2d_rt_draw(struct d2d_d3d_render_target *render_target, enum d2d_shape_type shape_type,
        ID3D10Buffer *ib, unsigned int index_count, ID3D10Buffer *vb, unsigned int vb_stride,
        ID3D10Buffer *vs_cb, ID3D10Buffer *ps_cb, struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    struct d2d_shape_resources *shape_resources = &render_target->shape_resources[shape_type];
    ID3D10Device *device = render_target->device;
    D3D10_RECT scissor_rect;
    unsigned int offset;
    D3D10_VIEWPORT vp;
    HRESULT hr;

    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = render_target->pixel_size.width;
    vp.Height = render_target->pixel_size.height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    if (FAILED(hr = render_target->stateblock->lpVtbl->Capture(render_target->stateblock)))
    {
        WARN("Failed to capture stateblock, hr %#x.\n", hr);
        return;
    }

    ID3D10Device_ClearState(device);

    ID3D10Device_IASetInputLayout(device, shape_resources->il);
    ID3D10Device_IASetPrimitiveTopology(device, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D10Device_IASetIndexBuffer(device, ib, DXGI_FORMAT_R16_UINT, 0);
    offset = 0;
    ID3D10Device_IASetVertexBuffers(device, 0, 1, &vb, &vb_stride, &offset);
    ID3D10Device_VSSetConstantBuffers(device, 0, 1, &vs_cb);
    ID3D10Device_VSSetShader(device, shape_resources->vs);
    ID3D10Device_PSSetConstantBuffers(device, 0, 1, &ps_cb);
    ID3D10Device_RSSetViewports(device, 1, &vp);
    if (render_target->clip_stack.count)
    {
        const D2D1_RECT_F *clip_rect;

        clip_rect = &render_target->clip_stack.stack[render_target->clip_stack.count - 1];
        scissor_rect.left = ceilf(clip_rect->left - 0.5f);
        scissor_rect.top = ceilf(clip_rect->top - 0.5f);
        scissor_rect.right = ceilf(clip_rect->right - 0.5f);
        scissor_rect.bottom = ceilf(clip_rect->bottom - 0.5f);
    }
    else
    {
        scissor_rect.left = 0.0f;
        scissor_rect.top = 0.0f;
        scissor_rect.right = render_target->pixel_size.width;
        scissor_rect.bottom = render_target->pixel_size.height;
    }
    ID3D10Device_RSSetScissorRects(device, 1, &scissor_rect);
    ID3D10Device_RSSetState(device, render_target->rs);
    ID3D10Device_OMSetRenderTargets(device, 1, &render_target->view, NULL);
    if (brush)
        d2d_brush_bind_resources(brush, opacity_brush, render_target, shape_type);
    else
        ID3D10Device_PSSetShader(device, shape_resources->ps[D2D_BRUSH_TYPE_SOLID][D2D_BRUSH_TYPE_COUNT]);

    if (ib)
        ID3D10Device_DrawIndexed(device, index_count, 0, 0);
    else
        ID3D10Device_Draw(device, index_count, 0);

    if (FAILED(hr = render_target->stateblock->lpVtbl->Apply(render_target->stateblock)))
        WARN("Failed to apply stateblock, hr %#x.\n", hr);
}

static inline struct d2d_d3d_render_target *impl_from_ID2D1RenderTarget(ID2D1RenderTarget *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_d3d_render_target, ID2D1RenderTarget_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_QueryInterface(ID2D1RenderTarget *iface, REFIID iid, void **out)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1RenderTarget)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1RenderTarget_AddRef(iface);
        *out = iface;
        return S_OK;
    }
    else if (IsEqualGUID(iid, &IID_ID2D1GdiInteropRenderTarget))
    {
        ID2D1GdiInteropRenderTarget_AddRef(&render_target->ID2D1GdiInteropRenderTarget_iface);
        *out = &render_target->ID2D1GdiInteropRenderTarget_iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_d3d_render_target_AddRef(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ULONG refcount = InterlockedIncrement(&render_target->refcount);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_d3d_render_target_Release(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ULONG refcount = InterlockedDecrement(&render_target->refcount);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
    {
        unsigned int i, j, k;

        d2d_clip_stack_cleanup(&render_target->clip_stack);
        IDWriteRenderingParams_Release(render_target->default_text_rendering_params);
        if (render_target->text_rendering_params)
            IDWriteRenderingParams_Release(render_target->text_rendering_params);
        ID3D10BlendState_Release(render_target->bs);
        ID3D10RasterizerState_Release(render_target->rs);
        ID3D10Buffer_Release(render_target->vb);
        ID3D10Buffer_Release(render_target->ib);
        for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
        {
            for (j = 0; j < D2D_BRUSH_TYPE_COUNT; ++j)
            {
                for (k = 0; k < D2D_BRUSH_TYPE_COUNT + 1; ++k)
                {
                    if (render_target->shape_resources[i].ps[j][k])
                        ID3D10PixelShader_Release(render_target->shape_resources[i].ps[j][k]);
                }
            }
            ID3D10VertexShader_Release(render_target->shape_resources[i].vs);
            ID3D10InputLayout_Release(render_target->shape_resources[i].il);
        }
        render_target->stateblock->lpVtbl->Release(render_target->stateblock);
        ID3D10RenderTargetView_Release(render_target->view);
        ID3D10Device_Release(render_target->device);
        ID2D1Factory_Release(render_target->factory);
        HeapFree(GetProcessHeap(), 0, render_target);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_GetFactory(ID2D1RenderTarget *iface, ID2D1Factory **factory)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    *factory = render_target->factory;
    ID2D1Factory_AddRef(*factory);
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateBitmap(ID2D1RenderTarget *iface,
        D2D1_SIZE_U size, const void *src_data, UINT32 pitch, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, size {%u, %u}, src_data %p, pitch %u, desc %p, bitmap %p.\n",
            iface, size.width, size.height, src_data, pitch, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create(render_target->factory, render_target->device, size, src_data, pitch, desc, &object)))
        *bitmap = &object->ID2D1Bitmap_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateBitmapFromWicBitmap(ID2D1RenderTarget *iface,
        IWICBitmapSource *bitmap_source, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, bitmap_source %p, desc %p, bitmap %p.\n",
            iface, bitmap_source, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create_from_wic_bitmap(render_target->factory, render_target->device, bitmap_source,
            desc, &object)))
        *bitmap = &object->ID2D1Bitmap_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateSharedBitmap(ID2D1RenderTarget *iface,
        REFIID iid, void *data, const D2D1_BITMAP_PROPERTIES *desc, ID2D1Bitmap **bitmap)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_bitmap *object;
    HRESULT hr;

    TRACE("iface %p, iid %s, data %p, desc %p, bitmap %p.\n",
            iface, debugstr_guid(iid), data, desc, bitmap);

    if (SUCCEEDED(hr = d2d_bitmap_create_shared(iface, render_target->device, iid, data, desc, &object)))
        *bitmap = &object->ID2D1Bitmap_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateBitmapBrush(ID2D1RenderTarget *iface,
        ID2D1Bitmap *bitmap, const D2D1_BITMAP_BRUSH_PROPERTIES *bitmap_brush_desc,
        const D2D1_BRUSH_PROPERTIES *brush_desc, ID2D1BitmapBrush **brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, bitmap_brush_desc %p, brush_desc %p, brush %p.\n",
            iface, bitmap, bitmap_brush_desc, brush_desc, brush);

    if (SUCCEEDED(hr = d2d_bitmap_brush_create(render_target->factory, bitmap, bitmap_brush_desc, brush_desc, &object)))
        *brush = (ID2D1BitmapBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateSolidColorBrush(ID2D1RenderTarget *iface,
        const D2D1_COLOR_F *color, const D2D1_BRUSH_PROPERTIES *desc, ID2D1SolidColorBrush **brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, color %p, desc %p, brush %p.\n", iface, color, desc, brush);

    if (SUCCEEDED(hr = d2d_solid_color_brush_create(render_target->factory, color, desc, &object)))
        *brush = (ID2D1SolidColorBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateGradientStopCollection(ID2D1RenderTarget *iface,
        const D2D1_GRADIENT_STOP *stops, UINT32 stop_count, D2D1_GAMMA gamma, D2D1_EXTEND_MODE extend_mode,
        ID2D1GradientStopCollection **gradient)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_gradient *object;
    HRESULT hr;

    TRACE("iface %p, stops %p, stop_count %u, gamma %#x, extend_mode %#x, gradient %p.\n",
            iface, stops, stop_count, gamma, extend_mode, gradient);

    if (SUCCEEDED(hr = d2d_gradient_create(render_target->factory, stops, stop_count, gamma, extend_mode, &object)))
        *gradient = &object->ID2D1GradientStopCollection_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateLinearGradientBrush(ID2D1RenderTarget *iface,
        const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1LinearGradientBrush **brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_brush *object;
    HRESULT hr;

    TRACE("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p.\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    if (SUCCEEDED(hr = d2d_linear_gradient_brush_create(render_target->factory, gradient_brush_desc, brush_desc,
        gradient, &object)))
        *brush = (ID2D1LinearGradientBrush *)&object->ID2D1Brush_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateRadialGradientBrush(ID2D1RenderTarget *iface,
        const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES *gradient_brush_desc, const D2D1_BRUSH_PROPERTIES *brush_desc,
        ID2D1GradientStopCollection *gradient, ID2D1RadialGradientBrush **brush)
{
    FIXME("iface %p, gradient_brush_desc %p, brush_desc %p, gradient %p, brush %p stub!\n",
            iface, gradient_brush_desc, brush_desc, gradient, brush);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateCompatibleRenderTarget(ID2D1RenderTarget *iface,
        const D2D1_SIZE_F *size, const D2D1_SIZE_U *pixel_size, const D2D1_PIXEL_FORMAT *format,
        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS options, ID2D1BitmapRenderTarget **rt)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_bitmap_render_target *object;
    HRESULT hr;

    TRACE("iface %p, size %p, pixel_size %p, format %p, options %#x, render_target %p.\n",
            iface, size, pixel_size, format, options, rt);

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_bitmap_render_target_init(object, render_target, size, pixel_size,
            format, options)))
    {
        WARN("Failed to initialize render target, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *rt = &object->ID2D1BitmapRenderTarget_iface;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateLayer(ID2D1RenderTarget *iface,
        const D2D1_SIZE_F *size, ID2D1Layer **layer)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_layer *object;
    HRESULT hr;

    TRACE("iface %p, size %p, layer %p.\n", iface, size, layer);

    if (SUCCEEDED(hr = d2d_layer_create(render_target->factory, size, &object)))
        *layer = &object->ID2D1Layer_iface;

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_CreateMesh(ID2D1RenderTarget *iface, ID2D1Mesh **mesh)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_mesh *object;
    HRESULT hr;

    TRACE("iface %p, mesh %p.\n", iface, mesh);

    if (SUCCEEDED(hr = d2d_mesh_create(render_target->factory, &object)))
        *mesh = &object->ID2D1Mesh_iface;

    return hr;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawLine(ID2D1RenderTarget *iface,
        D2D1_POINT_2F p0, D2D1_POINT_2F p1, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    TRACE("iface %p, p0 {%.8e, %.8e}, p1 {%.8e, %.8e}, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, p0.x, p0.y, p1.x, p1.y, brush, stroke_width, stroke_style);

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(render_target->factory, &geometry)))
    {
        WARN("Failed to create path geometry, %#x.\n", hr);
        return;
    }

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        WARN("Open() failed, %#x.\n", hr);
        ID2D1PathGeometry_Release(geometry);
        return;
    }

    ID2D1GeometrySink_BeginFigure(sink, p0, D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddLine(sink, p1);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        WARN("Close() failed, %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);

    ID2D1RenderTarget_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1PathGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawRectangle(ID2D1RenderTarget *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, debug_d2d_rect_f(rect), brush, stroke_width, stroke_style);

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillRectangle(ID2D1RenderTarget *iface,
        const D2D1_RECT_F *rect, ID2D1Brush *brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1RectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %s, brush %p.\n", iface, debug_d2d_rect_f(rect), brush);

    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawRoundedRectangle(ID2D1RenderTarget *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, rect, brush, stroke_width, stroke_style);

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillRoundedRectangle(ID2D1RenderTarget *iface,
        const D2D1_ROUNDED_RECT *rect, ID2D1Brush *brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1RoundedRectangleGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, rect %p, brush %p.\n", iface, rect, brush);

    if (FAILED(hr = ID2D1Factory_CreateRoundedRectangleGeometry(render_target->factory, rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1RoundedRectangleGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawEllipse(ID2D1RenderTarget *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, ellipse, brush, stroke_width, stroke_style);

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_DrawGeometry(iface, (ID2D1Geometry *)geometry, brush, stroke_width, stroke_style);
    ID2D1EllipseGeometry_Release(geometry);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillEllipse(ID2D1RenderTarget *iface,
        const D2D1_ELLIPSE *ellipse, ID2D1Brush *brush)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    ID2D1EllipseGeometry *geometry;
    HRESULT hr;

    TRACE("iface %p, ellipse %p, brush %p.\n", iface, ellipse, brush);

    if (FAILED(hr = ID2D1Factory_CreateEllipseGeometry(render_target->factory, ellipse, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_FillGeometry(iface, (ID2D1Geometry *)geometry, brush, NULL);
    ID2D1EllipseGeometry_Release(geometry);
}

static void d2d_rt_draw_geometry(struct d2d_d3d_render_target *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, float stroke_width)
{
    ID3D10Buffer *ib, *vb, *vs_cb, *ps_cb;
    D3D10_SUBRESOURCE_DATA buffer_data;
    D3D10_BUFFER_DESC buffer_desc;
    const D2D1_MATRIX_3X2_F *w;
    float tmp_x, tmp_y;
    HRESULT hr;
    struct
    {
        struct
        {
            float _11, _21, _31, pad0;
            float _12, _22, _32, stroke_width;
        } transform_geometry;
        struct d2d_vec4 transform_rtx;
        struct d2d_vec4 transform_rty;
    } vs_cb_data;

    vs_cb_data.transform_geometry._11 = geometry->transform._11;
    vs_cb_data.transform_geometry._21 = geometry->transform._21;
    vs_cb_data.transform_geometry._31 = geometry->transform._31;
    vs_cb_data.transform_geometry.pad0 = 0.0f;
    vs_cb_data.transform_geometry._12 = geometry->transform._12;
    vs_cb_data.transform_geometry._22 = geometry->transform._22;
    vs_cb_data.transform_geometry._32 = geometry->transform._32;
    vs_cb_data.transform_geometry.stroke_width = stroke_width;

    w = &render_target->drawing_state.transform;

    tmp_x = render_target->desc.dpiX / 96.0f;
    vs_cb_data.transform_rtx.x = w->_11 * tmp_x;
    vs_cb_data.transform_rtx.y = w->_21 * tmp_x;
    vs_cb_data.transform_rtx.z = w->_31 * tmp_x;
    vs_cb_data.transform_rtx.w = 2.0f / render_target->pixel_size.width;

    tmp_y = render_target->desc.dpiY / 96.0f;
    vs_cb_data.transform_rty.x = w->_12 * tmp_y;
    vs_cb_data.transform_rty.y = w->_22 * tmp_y;
    vs_cb_data.transform_rty.z = w->_32 * tmp_y;
    vs_cb_data.transform_rty.w = -2.0f / render_target->pixel_size.height;

    buffer_desc.ByteWidth = sizeof(vs_cb_data);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = &vs_cb_data;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vs_cb)))
    {
        WARN("Failed to create constant buffer, hr %#x.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_brush_get_ps_cb(brush, NULL, render_target, &ps_cb)))
    {
        WARN("Failed to get ps constant buffer, hr %#x.\n", hr);
        ID3D10Buffer_Release(vs_cb);
        return;
    }

    if (geometry->outline.face_count)
    {
        buffer_desc.ByteWidth = geometry->outline.face_count * sizeof(*geometry->outline.faces);
        buffer_desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.faces;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create index buffer, hr %#x.\n", hr);
            goto done;
        }

        buffer_desc.ByteWidth = geometry->outline.vertex_count * sizeof(*geometry->outline.vertices);
        buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.vertices;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create vertex buffer, hr %#x.\n", hr);
            ID3D10Buffer_Release(ib);
            goto done;
        }

        d2d_rt_draw(render_target, D2D_SHAPE_TYPE_OUTLINE, ib, 3 * geometry->outline.face_count, vb,
                sizeof(*geometry->outline.vertices), vs_cb, ps_cb, brush, NULL);

        ID3D10Buffer_Release(vb);
        ID3D10Buffer_Release(ib);
    }

    if (geometry->outline.bezier_face_count)
    {
        buffer_desc.ByteWidth = geometry->outline.bezier_face_count * sizeof(*geometry->outline.bezier_faces);
        buffer_desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.bezier_faces;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create beziers index buffer, hr %#x.\n", hr);
            goto done;
        }

        buffer_desc.ByteWidth = geometry->outline.bezier_count * sizeof(*geometry->outline.beziers);
        buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->outline.beziers;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create beziers vertex buffer, hr %#x.\n", hr);
            ID3D10Buffer_Release(ib);
            goto done;
        }

        d2d_rt_draw(render_target, D2D_SHAPE_TYPE_BEZIER_OUTLINE, ib, 3 * geometry->outline.bezier_face_count, vb,
                sizeof(*geometry->outline.beziers), vs_cb, ps_cb, brush, NULL);

        ID3D10Buffer_Release(vb);
        ID3D10Buffer_Release(ib);
    }

done:
    ID3D10Buffer_Release(ps_cb);
    ID3D10Buffer_Release(vs_cb);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawGeometry(ID2D1RenderTarget *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, float stroke_width, ID2D1StrokeStyle *stroke_style)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);

    TRACE("iface %p, geometry %p, brush %p, stroke_width %.8e, stroke_style %p.\n",
            iface, geometry, brush, stroke_width, stroke_style);

    if (stroke_style)
        FIXME("Ignoring stroke style %p.\n", stroke_style);

    d2d_rt_draw_geometry(render_target, geometry_impl, brush_impl, stroke_width);
}

static void d2d_rt_fill_geometry(struct d2d_d3d_render_target *render_target,
        const struct d2d_geometry *geometry, struct d2d_brush *brush, struct d2d_brush *opacity_brush)
{
    ID3D10Buffer *ib, *vb, *vs_cb, *ps_cb;
    D3D10_SUBRESOURCE_DATA buffer_data;
    D3D10_BUFFER_DESC buffer_desc;
    D2D1_MATRIX_3X2_F w, g;
    float tmp_x, tmp_y;
    HRESULT hr;
    struct
    {
        float _11, _21, _31, pad0;
        float _12, _22, _32, pad1;
    } transform;

    tmp_x =  (2.0f * render_target->desc.dpiX) / (96.0f * render_target->pixel_size.width);
    tmp_y = -(2.0f * render_target->desc.dpiY) / (96.0f * render_target->pixel_size.height);
    w = render_target->drawing_state.transform;
    w._11 *= tmp_x;
    w._21 *= tmp_x;
    w._31 = w._31 * tmp_x - 1.0f;
    w._12 *= tmp_y;
    w._22 *= tmp_y;
    w._32 = w._32 * tmp_y + 1.0f;

    g = geometry->transform;
    d2d_matrix_multiply(&g, &w);

    transform._11 = g._11;
    transform._21 = g._21;
    transform._31 = g._31;
    transform.pad0 = 0.0f;
    transform._12 = g._12;
    transform._22 = g._22;
    transform._32 = g._32;
    transform.pad1 = 0.0f;

    buffer_desc.ByteWidth = sizeof(transform);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = &transform;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vs_cb)))
    {
        WARN("Failed to create constant buffer, hr %#x.\n", hr);
        return;
    }

    if (FAILED(hr = d2d_brush_get_ps_cb(brush, opacity_brush, render_target, &ps_cb)))
    {
        WARN("Failed to get ps constant buffer, hr %#x.\n", hr);
        ID3D10Buffer_Release(vs_cb);
        return;
    }

    if (geometry->fill.face_count)
    {
        buffer_desc.ByteWidth = geometry->fill.face_count * sizeof(*geometry->fill.faces);
        buffer_desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.faces;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &ib)))
        {
            WARN("Failed to create index buffer, hr %#x.\n", hr);
            goto done;
        }

        buffer_desc.ByteWidth = geometry->fill.vertex_count * sizeof(*geometry->fill.vertices);
        buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
        buffer_data.pSysMem = geometry->fill.vertices;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create vertex buffer, hr %#x.\n", hr);
            ID3D10Buffer_Release(ib);
            goto done;
        }

        d2d_rt_draw(render_target, D2D_SHAPE_TYPE_TRIANGLE, ib, 3 * geometry->fill.face_count, vb,
                sizeof(*geometry->fill.vertices), vs_cb, ps_cb, brush, opacity_brush);

        ID3D10Buffer_Release(vb);
        ID3D10Buffer_Release(ib);
    }

    if (geometry->fill.bezier_vertex_count)
    {
        buffer_desc.ByteWidth = geometry->fill.bezier_vertex_count * sizeof(*geometry->fill.bezier_vertices);
        buffer_data.pSysMem = geometry->fill.bezier_vertices;

        if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vb)))
        {
            ERR("Failed to create beziers vertex buffer, hr %#x.\n", hr);
            goto done;
        }

        d2d_rt_draw(render_target, D2D_SHAPE_TYPE_BEZIER, NULL, geometry->fill.bezier_vertex_count, vb,
                sizeof(*geometry->fill.bezier_vertices), vs_cb, ps_cb, brush, opacity_brush);

        ID3D10Buffer_Release(vb);
    }

done:
    ID3D10Buffer_Release(ps_cb);
    ID3D10Buffer_Release(vs_cb);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillGeometry(ID2D1RenderTarget *iface,
        ID2D1Geometry *geometry, ID2D1Brush *brush, ID2D1Brush *opacity_brush)
{
    const struct d2d_geometry *geometry_impl = unsafe_impl_from_ID2D1Geometry(geometry);
    struct d2d_brush *opacity_brush_impl = unsafe_impl_from_ID2D1Brush(opacity_brush);
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_brush *brush_impl = unsafe_impl_from_ID2D1Brush(brush);

    TRACE("iface %p, geometry %p, brush %p, opacity_brush %p.\n", iface, geometry, brush, opacity_brush);

    if (FAILED(render_target->error.code))
        return;

    if (opacity_brush && brush_impl->type != D2D_BRUSH_TYPE_BITMAP)
    {
        render_target->error.code = D2DERR_INCOMPATIBLE_BRUSH_TYPES;
        render_target->error.tag1 = render_target->drawing_state.tag1;
        render_target->error.tag2 = render_target->drawing_state.tag2;
        return;
    }

    d2d_rt_fill_geometry(render_target, geometry_impl, brush_impl, opacity_brush_impl);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillMesh(ID2D1RenderTarget *iface,
        ID2D1Mesh *mesh, ID2D1Brush *brush)
{
    FIXME("iface %p, mesh %p, brush %p stub!\n", iface, mesh, brush);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_FillOpacityMask(ID2D1RenderTarget *iface,
        ID2D1Bitmap *mask, ID2D1Brush *brush, D2D1_OPACITY_MASK_CONTENT content,
        const D2D1_RECT_F *dst_rect, const D2D1_RECT_F *src_rect)
{
    FIXME("iface %p, mask %p, brush %p, content %#x, dst_rect %s, src_rect %s stub!\n",
            iface, mask, brush, content, debug_d2d_rect_f(dst_rect), debug_d2d_rect_f(src_rect));
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawBitmap(ID2D1RenderTarget *iface,
        ID2D1Bitmap *bitmap, const D2D1_RECT_F *dst_rect, float opacity,
        D2D1_BITMAP_INTERPOLATION_MODE interpolation_mode, const D2D1_RECT_F *src_rect)
{
    D2D1_BITMAP_BRUSH_PROPERTIES bitmap_brush_desc;
    D2D1_BRUSH_PROPERTIES brush_desc;
    ID2D1BitmapBrush *brush;
    D2D1_RECT_F s, d;
    HRESULT hr;

    TRACE("iface %p, bitmap %p, dst_rect %s, opacity %.8e, interpolation_mode %#x, src_rect %s.\n",
            iface, bitmap, debug_d2d_rect_f(dst_rect), opacity, interpolation_mode, debug_d2d_rect_f(src_rect));

    if (src_rect)
    {
        s = *src_rect;
    }
    else
    {
        D2D1_SIZE_F size;

        size = ID2D1Bitmap_GetSize(bitmap);
        s.left = 0.0f;
        s.top = 0.0f;
        s.right = size.width;
        s.bottom = size.height;
    }

    if (dst_rect)
    {
        d = *dst_rect;
    }
    else
    {
        d.left = 0.0f;
        d.top = 0.0f;
        d.right = s.right - s.left;
        d.bottom = s.bottom - s.top;
    }

    bitmap_brush_desc.extendModeX = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.extendModeY = D2D1_EXTEND_MODE_CLAMP;
    bitmap_brush_desc.interpolationMode = interpolation_mode;

    brush_desc.opacity = opacity;
    brush_desc.transform._11 = fabsf((d.right - d.left) / (s.right - s.left));
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._31 = min(d.left, d.right) - min(s.left, s.right) * brush_desc.transform._11;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._22 = fabsf((d.bottom - d.top) / (s.bottom - s.top));
    brush_desc.transform._32 = min(d.top, d.bottom) - min(s.top, s.bottom) * brush_desc.transform._22;

    if (FAILED(hr = ID2D1RenderTarget_CreateBitmapBrush(iface, bitmap, &bitmap_brush_desc, &brush_desc, &brush)))
    {
        ERR("Failed to create bitmap brush, hr %#x.\n", hr);
        return;
    }

    ID2D1RenderTarget_FillRectangle(iface, &d, (ID2D1Brush *)brush);
    ID2D1BitmapBrush_Release(brush);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawText(ID2D1RenderTarget *iface,
        const WCHAR *string, UINT32 string_len, IDWriteTextFormat *text_format, const D2D1_RECT_F *layout_rect,
        ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options, DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    IDWriteTextLayout *text_layout;
    IDWriteFactory *dwrite_factory;
    D2D1_POINT_2F origin;
    HRESULT hr;

    TRACE("iface %p, string %s, string_len %u, text_format %p, layout_rect %s, "
            "brush %p, options %#x, measuring_mode %#x.\n",
            iface, debugstr_wn(string, string_len), string_len, text_format, debug_d2d_rect_f(layout_rect),
            brush, options, measuring_mode);

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#x.\n", hr);
        return;
    }

    if (measuring_mode == DWRITE_MEASURING_MODE_NATURAL)
        hr = IDWriteFactory_CreateTextLayout(dwrite_factory, string, string_len, text_format,
                layout_rect->right - layout_rect->left, layout_rect->bottom - layout_rect->top, &text_layout);
    else
        hr = IDWriteFactory_CreateGdiCompatibleTextLayout(dwrite_factory, string, string_len, text_format,
                layout_rect->right - layout_rect->left, layout_rect->bottom - layout_rect->top, render_target->desc.dpiX / 96.0f,
                (DWRITE_MATRIX*)&render_target->drawing_state.transform, measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL, &text_layout);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create text layout, hr %#x.\n", hr);
        return;
    }

    d2d_point_set(&origin, layout_rect->left, layout_rect->top);
    ID2D1RenderTarget_DrawTextLayout(iface, origin, text_layout, brush, options);
    IDWriteTextLayout_Release(text_layout);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawTextLayout(ID2D1RenderTarget *iface,
        D2D1_POINT_2F origin, IDWriteTextLayout *layout, ID2D1Brush *brush, D2D1_DRAW_TEXT_OPTIONS options)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    struct d2d_draw_text_layout_ctx ctx;
    HRESULT hr;

    TRACE("iface %p, origin {%.8e, %.8e}, layout %p, brush %p, options %#x.\n",
            iface, origin.x, origin.y, layout, brush, options);

    ctx.brush = brush;
    ctx.options = options;

    if (FAILED(hr = IDWriteTextLayout_Draw(layout,
            &ctx, &render_target->IDWriteTextRenderer_iface, origin.x, origin.y)))
        FIXME("Failed to draw text layout, hr %#x.\n", hr);
}

static D2D1_ANTIALIAS_MODE d2d_d3d_render_target_set_aa_mode_from_text_aa_mode(struct d2d_d3d_render_target *rt)
{
    D2D1_ANTIALIAS_MODE prev_antialias_mode = rt->drawing_state.antialiasMode;
    rt->drawing_state.antialiasMode = rt->drawing_state.textAntialiasMode == D2D1_TEXT_ANTIALIAS_MODE_ALIASED ?
            D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    return prev_antialias_mode;
}

static void d2d_rt_draw_glyph_run_outline(struct d2d_d3d_render_target *render_target,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush)
{
    D2D1_MATRIX_3X2_F *transform, prev_transform;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    ID2D1PathGeometry *geometry;
    ID2D1GeometrySink *sink;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(render_target->factory, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        return;
    }

    if (FAILED(hr = ID2D1PathGeometry_Open(geometry, &sink)))
    {
        ERR("Failed to open geometry sink, hr %#x.\n", hr);
        ID2D1PathGeometry_Release(geometry);
        return;
    }

    if (FAILED(hr = IDWriteFontFace_GetGlyphRunOutline(glyph_run->fontFace, glyph_run->fontEmSize,
            glyph_run->glyphIndices, glyph_run->glyphAdvances, glyph_run->glyphOffsets, glyph_run->glyphCount,
            glyph_run->isSideways, glyph_run->bidiLevel & 1, (IDWriteGeometrySink *)sink)))
    {
        ERR("Failed to get glyph run outline, hr %#x.\n", hr);
        ID2D1GeometrySink_Release(sink);
        ID2D1PathGeometry_Release(geometry);
        return;
    }

    if (FAILED(hr = ID2D1GeometrySink_Close(sink)))
        ERR("Failed to close geometry sink, hr %#x.\n", hr);
    ID2D1GeometrySink_Release(sink);

    transform = &render_target->drawing_state.transform;
    prev_transform = *transform;
    transform->_31 += baseline_origin.x * transform->_11 + baseline_origin.y * transform->_21;
    transform->_32 += baseline_origin.x * transform->_12 + baseline_origin.y * transform->_22;
    prev_antialias_mode = d2d_d3d_render_target_set_aa_mode_from_text_aa_mode(render_target);
    d2d_rt_fill_geometry(render_target, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;
    *transform = prev_transform;

    ID2D1PathGeometry_Release(geometry);
}

static void d2d_rt_draw_glyph_run_bitmap(struct d2d_d3d_render_target *render_target,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        float ppd, DWRITE_RENDERING_MODE rendering_mode, DWRITE_MEASURING_MODE measuring_mode,
        DWRITE_TEXT_ANTIALIAS_MODE antialias_mode)
{
    D2D1_MATRIX_3X2_F prev_transform, *transform;
    ID2D1RectangleGeometry *geometry = NULL;
    ID2D1BitmapBrush *opacity_brush = NULL;
    D2D1_BITMAP_PROPERTIES bitmap_desc;
    ID2D1Bitmap *opacity_bitmap = NULL;
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_TEXTURE_TYPE texture_type;
    D2D1_BRUSH_PROPERTIES brush_desc;
    IDWriteFactory2 *dwrite_factory;
    DWRITE_GLYPH_RUN scaled_run;
    void *opacity_values = NULL;
    size_t opacity_values_size;
    D2D1_SIZE_U bitmap_size;
    D2D1_RECT_F run_rect;
    RECT bounds;
    HRESULT hr;

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory2, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#x.\n", hr);
        return;
    }

    scaled_run = *glyph_run;
    scaled_run.fontEmSize *= ppd;
    hr = IDWriteFactory2_CreateGlyphRunAnalysis(dwrite_factory, &scaled_run,
            (DWRITE_MATRIX *)&render_target->drawing_state.transform, rendering_mode, measuring_mode,
            DWRITE_GRID_FIT_MODE_DEFAULT, antialias_mode, baseline_origin.x,
            baseline_origin.y, &analysis);
    IDWriteFactory2_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create glyph run analysis, hr %#x.\n", hr);
        return;
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED || antialias_mode == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE)
        texture_type = DWRITE_TEXTURE_ALIASED_1x1;
    else
        texture_type = DWRITE_TEXTURE_CLEARTYPE_3x1;

    if (FAILED(hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, texture_type, &bounds)))
    {
        ERR("Failed to get alpha texture bounds, hr %#x.\n", hr);
        goto done;
    }

    d2d_size_set(&bitmap_size, bounds.right - bounds.left, bounds.bottom - bounds.top);
    if (!bitmap_size.width || !bitmap_size.height)
    {
        /* Empty run, nothing to do. */
        goto done;
    }

    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_size.width *= 3;
    opacity_values_size = bitmap_size.width * bitmap_size.height;
    if (!(opacity_values = HeapAlloc(GetProcessHeap(), 0, opacity_values_size)))
    {
        ERR("Failed to allocate opacity values.\n");
        goto done;
    }

    if (FAILED(hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis,
            texture_type, &bounds, opacity_values, opacity_values_size)))
    {
        ERR("Failed to create alpha texture, hr %#x.\n", hr);
        goto done;
    }

    bitmap_desc.pixelFormat.format = DXGI_FORMAT_A8_UNORM;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = render_target->desc.dpiX;
    if (texture_type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        bitmap_desc.dpiX *= 3.0f;
    bitmap_desc.dpiY = render_target->desc.dpiY;
    if (FAILED(hr = d2d_d3d_render_target_CreateBitmap(&render_target->ID2D1RenderTarget_iface,
            bitmap_size, opacity_values, bitmap_size.width, &bitmap_desc, &opacity_bitmap)))
    {
        ERR("Failed to create opacity bitmap, hr %#x.\n", hr);
        goto done;
    }

    brush_desc.opacity = 1.0f;
    brush_desc.transform._11 = 1.0f;
    brush_desc.transform._12 = 0.0f;
    brush_desc.transform._21 = 0.0f;
    brush_desc.transform._22 = 1.0f;
    brush_desc.transform._31 = bounds.left;
    brush_desc.transform._32 = bounds.top;
    if (FAILED(hr = d2d_d3d_render_target_CreateBitmapBrush(&render_target->ID2D1RenderTarget_iface,
            opacity_bitmap, NULL, &brush_desc, &opacity_brush)))
    {
        ERR("Failed to create opacity bitmap brush, hr %#x.\n", hr);
        goto done;
    }

    d2d_rect_set(&run_rect, bounds.left, bounds.top, bounds.right, bounds.bottom);
    if (FAILED(hr = ID2D1Factory_CreateRectangleGeometry(render_target->factory, &run_rect, &geometry)))
    {
        ERR("Failed to create geometry, hr %#x.\n", hr);
        goto done;
    }

    transform = &render_target->drawing_state.transform;
    prev_transform = *transform;
    *transform = identity;
    d2d_rt_fill_geometry(render_target, unsafe_impl_from_ID2D1Geometry((ID2D1Geometry *)geometry),
            unsafe_impl_from_ID2D1Brush(brush), unsafe_impl_from_ID2D1Brush((ID2D1Brush *)opacity_brush));
    *transform = prev_transform;

done:
    if (geometry)
        ID2D1RectangleGeometry_Release(geometry);
    if (opacity_brush)
        ID2D1BitmapBrush_Release(opacity_brush);
    if (opacity_bitmap)
        ID2D1Bitmap_Release(opacity_bitmap);
    HeapFree(GetProcessHeap(), 0, opacity_values);
    IDWriteGlyphRunAnalysis_Release(analysis);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_DrawGlyphRun(ID2D1RenderTarget *iface,
        D2D1_POINT_2F baseline_origin, const DWRITE_GLYPH_RUN *glyph_run, ID2D1Brush *brush,
        DWRITE_MEASURING_MODE measuring_mode)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    DWRITE_TEXT_ANTIALIAS_MODE antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE;
    IDWriteRenderingParams *rendering_params;
    DWRITE_RENDERING_MODE rendering_mode;
    HRESULT hr;
    float ppd;

    TRACE("iface %p, baseline_origin {%.8e, %.8e}, glyph_run %p, brush %p, measuring_mode %#x.\n",
            iface, baseline_origin.x, baseline_origin.y, glyph_run, brush, measuring_mode);

    rendering_params = render_target->text_rendering_params ? render_target->text_rendering_params
            : render_target->default_text_rendering_params;

    rendering_mode = IDWriteRenderingParams_GetRenderingMode(rendering_params);

    switch (render_target->drawing_state.textAntialiasMode)
    {
    case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
        if (rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL
                || rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC
                || rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_NATURAL
                || rendering_mode == DWRITE_RENDERING_MODE_CLEARTYPE_GDI_CLASSIC)
        {
            render_target->error.code = E_INVALIDARG;
        }
        break;
    case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
        if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED
                || rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
        {
            render_target->error.code = E_INVALIDARG;
        }
        break;
    case D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE:
        if (rendering_mode == DWRITE_RENDERING_MODE_ALIASED)
            render_target->error.code = E_INVALIDARG;
        break;
    default:
        ;
    }

    if (FAILED(render_target->error.code))
        return;

    rendering_mode = DWRITE_RENDERING_MODE_DEFAULT;
    switch (render_target->drawing_state.textAntialiasMode)
    {
    case D2D1_TEXT_ANTIALIAS_MODE_DEFAULT:
        if (IDWriteRenderingParams_GetClearTypeLevel(rendering_params) > 0.0f)
            antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
        break;
    case D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE:
        antialias_mode = DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE;
        break;
    case D2D1_TEXT_ANTIALIAS_MODE_ALIASED:
        rendering_mode = DWRITE_RENDERING_MODE_ALIASED;
        break;
    default:
        ;
    }

    ppd = max(render_target->desc.dpiX, render_target->desc.dpiY) / 96.0f;
    if (rendering_mode == DWRITE_RENDERING_MODE_DEFAULT)
    {
        if (FAILED(hr = IDWriteFontFace_GetRecommendedRenderingMode(glyph_run->fontFace, glyph_run->fontEmSize,
                ppd, measuring_mode, rendering_params, &rendering_mode)))
        {
            ERR("Failed to get recommended rendering mode, hr %#x.\n", hr);
            rendering_mode = DWRITE_RENDERING_MODE_OUTLINE;
        }
    }

    if (rendering_mode == DWRITE_RENDERING_MODE_OUTLINE)
        d2d_rt_draw_glyph_run_outline(render_target, baseline_origin, glyph_run, brush);
    else
        d2d_rt_draw_glyph_run_bitmap(render_target, baseline_origin, glyph_run, brush,
                ppd, rendering_mode, measuring_mode, antialias_mode);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetTransform(ID2D1RenderTarget *iface,
        const D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    render_target->drawing_state.transform = *transform;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_GetTransform(ID2D1RenderTarget *iface,
        D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    *transform = render_target->drawing_state.transform;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetAntialiasMode(ID2D1RenderTarget *iface,
        D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, antialias_mode %#x stub!\n", iface, antialias_mode);

    render_target->drawing_state.antialiasMode = antialias_mode;
}

static D2D1_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_d3d_render_target_GetAntialiasMode(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.antialiasMode;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetTextAntialiasMode(ID2D1RenderTarget *iface,
        D2D1_TEXT_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, antialias_mode %#x.\n", iface, antialias_mode);

    render_target->drawing_state.textAntialiasMode = antialias_mode;
}

static D2D1_TEXT_ANTIALIAS_MODE STDMETHODCALLTYPE d2d_d3d_render_target_GetTextAntialiasMode(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return render_target->drawing_state.textAntialiasMode;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetTextRenderingParams(ID2D1RenderTarget *iface,
        IDWriteRenderingParams *text_rendering_params)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if (text_rendering_params)
        IDWriteRenderingParams_AddRef(text_rendering_params);
    if (render_target->text_rendering_params)
        IDWriteRenderingParams_Release(render_target->text_rendering_params);
    render_target->text_rendering_params = text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_GetTextRenderingParams(ID2D1RenderTarget *iface,
        IDWriteRenderingParams **text_rendering_params)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, text_rendering_params %p.\n", iface, text_rendering_params);

    if ((*text_rendering_params = render_target->text_rendering_params))
        IDWriteRenderingParams_AddRef(*text_rendering_params);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetTags(ID2D1RenderTarget *iface, D2D1_TAG tag1, D2D1_TAG tag2)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, tag1 %s, tag2 %s.\n", iface, wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));

    render_target->drawing_state.tag1 = tag1;
    render_target->drawing_state.tag2 = tag2;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_GetTags(ID2D1RenderTarget *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    *tag1 = render_target->drawing_state.tag1;
    *tag2 = render_target->drawing_state.tag2;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_PushLayer(ID2D1RenderTarget *iface,
        const D2D1_LAYER_PARAMETERS *layer_parameters, ID2D1Layer *layer)
{
    FIXME("iface %p, layer_parameters %p, layer %p stub!\n", iface, layer_parameters, layer);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_PopLayer(ID2D1RenderTarget *iface)
{
    FIXME("iface %p stub!\n", iface);
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_Flush(ID2D1RenderTarget *iface, D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    FIXME("iface %p, tag1 %p, tag2 %p stub!\n", iface, tag1, tag2);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SaveDrawingState(ID2D1RenderTarget *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_state_block *state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block);
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    state_block_impl->drawing_state = render_target->drawing_state;
    if (render_target->text_rendering_params)
        IDWriteRenderingParams_AddRef(render_target->text_rendering_params);
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_Release(state_block_impl->text_rendering_params);
    state_block_impl->text_rendering_params = render_target->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_RestoreDrawingState(ID2D1RenderTarget *iface,
        ID2D1DrawingStateBlock *state_block)
{
    struct d2d_state_block *state_block_impl = unsafe_impl_from_ID2D1DrawingStateBlock(state_block);
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, state_block %p.\n", iface, state_block);

    render_target->drawing_state = state_block_impl->drawing_state;
    if (state_block_impl->text_rendering_params)
        IDWriteRenderingParams_AddRef(state_block_impl->text_rendering_params);
    if (render_target->text_rendering_params)
        IDWriteRenderingParams_Release(render_target->text_rendering_params);
    render_target->text_rendering_params = state_block_impl->text_rendering_params;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_PushAxisAlignedClip(ID2D1RenderTarget *iface,
        const D2D1_RECT_F *clip_rect, D2D1_ANTIALIAS_MODE antialias_mode)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    D2D1_RECT_F transformed_rect;
    float x_scale, y_scale;
    D2D1_POINT_2F point;

    TRACE("iface %p, clip_rect %s, antialias_mode %#x.\n", iface, debug_d2d_rect_f(clip_rect), antialias_mode);

    if (antialias_mode != D2D1_ANTIALIAS_MODE_ALIASED)
        FIXME("Ignoring antialias_mode %#x.\n", antialias_mode);

    x_scale = render_target->desc.dpiX / 96.0f;
    y_scale = render_target->desc.dpiY / 96.0f;
    d2d_point_transform(&point, &render_target->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->top * y_scale);
    d2d_rect_set(&transformed_rect, point.x, point.y, point.x, point.y);
    d2d_point_transform(&point, &render_target->drawing_state.transform,
            clip_rect->left * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &render_target->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->top * y_scale);
    d2d_rect_expand(&transformed_rect, &point);
    d2d_point_transform(&point, &render_target->drawing_state.transform,
            clip_rect->right * x_scale, clip_rect->bottom * y_scale);
    d2d_rect_expand(&transformed_rect, &point);

    if (!d2d_clip_stack_push(&render_target->clip_stack, &transformed_rect))
        WARN("Failed to push clip rect.\n");
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_PopAxisAlignedClip(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p.\n", iface);

    d2d_clip_stack_pop(&render_target->clip_stack);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_Clear(ID2D1RenderTarget *iface, const D2D1_COLOR_F *color)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    D2D1_COLOR_F c = {0.0f, 0.0f, 0.0f, 0.0f};
    D3D10_SUBRESOURCE_DATA buffer_data;
    D3D10_BUFFER_DESC buffer_desc;
    ID3D10Buffer *vs_cb, *ps_cb;
    HRESULT hr;

    static const float transform[] =
    {
        1.0f,  0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
    };

    TRACE("iface %p, color %p.\n", iface, color);

    buffer_desc.ByteWidth = sizeof(transform);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = transform;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &vs_cb)))
    {
        WARN("Failed to create constant buffer, hr %#x.\n", hr);
        return;
    }

    if (color)
        c = *color;
    if (render_target->desc.pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE)
        c.a = 1.0f;
    c.r *= c.a;
    c.g *= c.a;
    c.b *= c.a;
    buffer_desc.ByteWidth = sizeof(c);
    buffer_data.pSysMem = &c;

    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device, &buffer_desc, &buffer_data, &ps_cb)))
    {
        WARN("Failed to create constant buffer, hr %#x.\n", hr);
        ID3D10Buffer_Release(vs_cb);
        return;
    }

    d2d_rt_draw(render_target, D2D_SHAPE_TYPE_TRIANGLE, render_target->ib, 6,
            render_target->vb, render_target->vb_stride, vs_cb, ps_cb, NULL, NULL);

    ID3D10Buffer_Release(ps_cb);
    ID3D10Buffer_Release(vs_cb);
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_BeginDraw(ID2D1RenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p.\n", iface);

    memset(&render_target->error, 0, sizeof(render_target->error));
}

static HRESULT STDMETHODCALLTYPE d2d_d3d_render_target_EndDraw(ID2D1RenderTarget *iface,
        D2D1_TAG *tag1, D2D1_TAG *tag2)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, tag1 %p, tag2 %p.\n", iface, tag1, tag2);

    if (tag1)
        *tag1 = render_target->error.tag1;
    if (tag2)
        *tag2 = render_target->error.tag2;

    return render_target->error.code;
}

static D2D1_PIXEL_FORMAT * STDMETHODCALLTYPE d2d_d3d_render_target_GetPixelFormat(ID2D1RenderTarget *iface,
        D2D1_PIXEL_FORMAT *format)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, format %p.\n", iface, format);

    *format = render_target->desc.pixelFormat;
    return format;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_SetDpi(ID2D1RenderTarget *iface, float dpi_x, float dpi_y)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, dpi_x %.8e, dpi_y %.8e.\n", iface, dpi_x, dpi_y);

    if (dpi_x == 0.0f && dpi_y == 0.0f)
    {
        dpi_x = 96.0f;
        dpi_y = 96.0f;
    }
    else if (dpi_x <= 0.0f || dpi_y <= 0.0f)
        return;

    render_target->desc.dpiX = dpi_x;
    render_target->desc.dpiY = dpi_y;
}

static void STDMETHODCALLTYPE d2d_d3d_render_target_GetDpi(ID2D1RenderTarget *iface, float *dpi_x, float *dpi_y)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, dpi_x %p, dpi_y %p.\n", iface, dpi_x, dpi_y);

    *dpi_x = render_target->desc.dpiX;
    *dpi_y = render_target->desc.dpiY;
}

static D2D1_SIZE_F * STDMETHODCALLTYPE d2d_d3d_render_target_GetSize(ID2D1RenderTarget *iface, D2D1_SIZE_F *size)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, size %p.\n", iface, size);

    size->width = render_target->pixel_size.width / (render_target->desc.dpiX / 96.0f);
    size->height = render_target->pixel_size.height / (render_target->desc.dpiY / 96.0f);
    return size;
}

static D2D1_SIZE_U * STDMETHODCALLTYPE d2d_d3d_render_target_GetPixelSize(ID2D1RenderTarget *iface,
        D2D1_SIZE_U *pixel_size)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);

    TRACE("iface %p, pixel_size %p.\n", iface, pixel_size);

    *pixel_size = render_target->pixel_size;
    return pixel_size;
}

static UINT32 STDMETHODCALLTYPE d2d_d3d_render_target_GetMaximumBitmapSize(ID2D1RenderTarget *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static BOOL STDMETHODCALLTYPE d2d_d3d_render_target_IsSupported(ID2D1RenderTarget *iface,
        const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    FIXME("iface %p, desc %p stub!\n", iface, desc);

    return FALSE;
}

static const struct ID2D1RenderTargetVtbl d2d_d3d_render_target_vtbl =
{
    d2d_d3d_render_target_QueryInterface,
    d2d_d3d_render_target_AddRef,
    d2d_d3d_render_target_Release,
    d2d_d3d_render_target_GetFactory,
    d2d_d3d_render_target_CreateBitmap,
    d2d_d3d_render_target_CreateBitmapFromWicBitmap,
    d2d_d3d_render_target_CreateSharedBitmap,
    d2d_d3d_render_target_CreateBitmapBrush,
    d2d_d3d_render_target_CreateSolidColorBrush,
    d2d_d3d_render_target_CreateGradientStopCollection,
    d2d_d3d_render_target_CreateLinearGradientBrush,
    d2d_d3d_render_target_CreateRadialGradientBrush,
    d2d_d3d_render_target_CreateCompatibleRenderTarget,
    d2d_d3d_render_target_CreateLayer,
    d2d_d3d_render_target_CreateMesh,
    d2d_d3d_render_target_DrawLine,
    d2d_d3d_render_target_DrawRectangle,
    d2d_d3d_render_target_FillRectangle,
    d2d_d3d_render_target_DrawRoundedRectangle,
    d2d_d3d_render_target_FillRoundedRectangle,
    d2d_d3d_render_target_DrawEllipse,
    d2d_d3d_render_target_FillEllipse,
    d2d_d3d_render_target_DrawGeometry,
    d2d_d3d_render_target_FillGeometry,
    d2d_d3d_render_target_FillMesh,
    d2d_d3d_render_target_FillOpacityMask,
    d2d_d3d_render_target_DrawBitmap,
    d2d_d3d_render_target_DrawText,
    d2d_d3d_render_target_DrawTextLayout,
    d2d_d3d_render_target_DrawGlyphRun,
    d2d_d3d_render_target_SetTransform,
    d2d_d3d_render_target_GetTransform,
    d2d_d3d_render_target_SetAntialiasMode,
    d2d_d3d_render_target_GetAntialiasMode,
    d2d_d3d_render_target_SetTextAntialiasMode,
    d2d_d3d_render_target_GetTextAntialiasMode,
    d2d_d3d_render_target_SetTextRenderingParams,
    d2d_d3d_render_target_GetTextRenderingParams,
    d2d_d3d_render_target_SetTags,
    d2d_d3d_render_target_GetTags,
    d2d_d3d_render_target_PushLayer,
    d2d_d3d_render_target_PopLayer,
    d2d_d3d_render_target_Flush,
    d2d_d3d_render_target_SaveDrawingState,
    d2d_d3d_render_target_RestoreDrawingState,
    d2d_d3d_render_target_PushAxisAlignedClip,
    d2d_d3d_render_target_PopAxisAlignedClip,
    d2d_d3d_render_target_Clear,
    d2d_d3d_render_target_BeginDraw,
    d2d_d3d_render_target_EndDraw,
    d2d_d3d_render_target_GetPixelFormat,
    d2d_d3d_render_target_SetDpi,
    d2d_d3d_render_target_GetDpi,
    d2d_d3d_render_target_GetSize,
    d2d_d3d_render_target_GetPixelSize,
    d2d_d3d_render_target_GetMaximumBitmapSize,
    d2d_d3d_render_target_IsSupported,
};

static inline struct d2d_d3d_render_target *impl_from_IDWriteTextRenderer(IDWriteTextRenderer *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_d3d_render_target, IDWriteTextRenderer_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_QueryInterface(IDWriteTextRenderer *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IDWriteTextRenderer)
            || IsEqualGUID(iid, &IID_IDWritePixelSnapping)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IDWriteTextRenderer_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_AddRef(IDWriteTextRenderer *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_d3d_render_target_AddRef(&render_target->ID2D1RenderTarget_iface);
}

static ULONG STDMETHODCALLTYPE d2d_text_renderer_Release(IDWriteTextRenderer *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p.\n", iface);

    return d2d_d3d_render_target_Release(&render_target->ID2D1RenderTarget_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_IsPixelSnappingDisabled(IDWriteTextRenderer *iface,
        void *ctx, BOOL *disabled)
{
    struct d2d_draw_text_layout_ctx *context = ctx;

    TRACE("iface %p, ctx %p, disabled %p.\n", iface, ctx, disabled);

    *disabled = context->options & D2D1_DRAW_TEXT_OPTIONS_NO_SNAP;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetCurrentTransform(IDWriteTextRenderer *iface,
        void *ctx, DWRITE_MATRIX *transform)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, transform %p.\n", iface, ctx, transform);

    ID2D1RenderTarget_GetTransform(&render_target->ID2D1RenderTarget_iface, (D2D1_MATRIX_3X2_F *)transform);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_GetPixelsPerDip(IDWriteTextRenderer *iface, void *ctx, float *ppd)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);

    TRACE("iface %p, ctx %p, ppd %p.\n", iface, ctx, ppd);

    *ppd = render_target->desc.dpiY / 96.0f;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawGlyphRun(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, DWRITE_MEASURING_MODE measuring_mode,
        const DWRITE_GLYPH_RUN *glyph_run, const DWRITE_GLYPH_RUN_DESCRIPTION *desc, IUnknown *effect)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);
    D2D1_POINT_2F baseline_origin = {baseline_origin_x, baseline_origin_y};
    struct d2d_draw_text_layout_ctx *context = ctx;
    BOOL color_font = FALSE;
    ID2D1Brush *brush;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, "
            "measuring_mode %#x, glyph_run %p, desc %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y,
            measuring_mode, glyph_run, desc, effect);

    if (desc)
        WARN("Ignoring glyph run description %p.\n", desc);
    if (context->options & ~(D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT))
        FIXME("Ignoring options %#x.\n", context->options);

    brush = d2d_draw_get_text_brush(context, effect);

    TRACE("%s\n", debugstr_wn(desc->string, desc->stringLength));

    if (context->options & D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT)
    {
        IDWriteFontFace2 *fontface;

        if (SUCCEEDED(IDWriteFontFace_QueryInterface(glyph_run->fontFace,
                &IID_IDWriteFontFace2, (void **)&fontface)))
        {
            color_font = IDWriteFontFace2_IsColorFont(fontface);
            IDWriteFontFace2_Release(fontface);
        }
    }

    if (color_font)
    {
        IDWriteColorGlyphRunEnumerator *layers;
        IDWriteFactory2 *dwrite_factory;
        HRESULT hr;

        if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2,
                (IUnknown **)&dwrite_factory)))
        {
            ERR("Failed to create dwrite factory, hr %#x.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        hr = IDWriteFactory2_TranslateColorGlyphRun(dwrite_factory, baseline_origin_x, baseline_origin_y,
                glyph_run, desc, measuring_mode, (DWRITE_MATRIX *)&render_target->drawing_state.transform, 0, &layers);
        IDWriteFactory2_Release(dwrite_factory);
        if (FAILED(hr))
        {
            ERR("Failed to create color glyph run enumerator, hr %#x.\n", hr);
            ID2D1Brush_Release(brush);
            return hr;
        }

        for (;;)
        {
            const DWRITE_COLOR_GLYPH_RUN *color_run;
            ID2D1Brush *color_brush;
            D2D1_POINT_2F origin;
            BOOL has_run = FALSE;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_MoveNext(layers, &has_run)))
            {
                ERR("Failed to switch color glyph layer, hr %#x.\n", hr);
                break;
            }

            if (!has_run)
                break;

            if (FAILED(hr = IDWriteColorGlyphRunEnumerator_GetCurrentRun(layers, &color_run)))
            {
                ERR("Failed to get current color run, hr %#x.\n", hr);
                break;
            }

            if (color_run->paletteIndex == 0xffff)
                color_brush = brush;
            else
            {
                if (FAILED(hr = ID2D1RenderTarget_CreateSolidColorBrush(&render_target->ID2D1RenderTarget_iface,
                        &color_run->runColor, NULL, (ID2D1SolidColorBrush **)&color_brush)))
                {
                    ERR("Failed to create solid color brush, hr %#x.\n", hr);
                    break;
                }
            }

            origin.x = color_run->baselineOriginX;
            origin.y = color_run->baselineOriginY;
            ID2D1RenderTarget_DrawGlyphRun(&render_target->ID2D1RenderTarget_iface,
                    origin, &color_run->glyphRun, color_brush, measuring_mode);

            if (color_brush != brush)
                ID2D1Brush_Release(color_brush);
        }

        IDWriteColorGlyphRunEnumerator_Release(layers);
    }
    else
        ID2D1RenderTarget_DrawGlyphRun(&render_target->ID2D1RenderTarget_iface,
                baseline_origin, glyph_run, brush, measuring_mode);

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawUnderline(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_UNDERLINE *underline, IUnknown *effect)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, underline %p, effect %p\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, underline, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            underline->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + underline->offset + thickness / 2.0f;
    end.x = start.x + underline->width;
    end.y = start.y;
    prev_antialias_mode = d2d_d3d_render_target_set_aa_mode_from_text_aa_mode(render_target);
    d2d_d3d_render_target_DrawLine(&render_target->ID2D1RenderTarget_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawStrikethrough(IDWriteTextRenderer *iface, void *ctx,
        float baseline_origin_x, float baseline_origin_y, const DWRITE_STRIKETHROUGH *strikethrough, IUnknown *effect)
{
    struct d2d_d3d_render_target *render_target = impl_from_IDWriteTextRenderer(iface);
    const D2D1_MATRIX_3X2_F *m = &render_target->drawing_state.transform;
    struct d2d_draw_text_layout_ctx *context = ctx;
    D2D1_ANTIALIAS_MODE prev_antialias_mode;
    D2D1_POINT_2F start, end;
    ID2D1Brush *brush;
    float thickness;

    TRACE("iface %p, ctx %p, baseline_origin_x %.8e, baseline_origin_y %.8e, strikethrough %p, effect %p.\n",
            iface, ctx, baseline_origin_x, baseline_origin_y, strikethrough, effect);

    /* minimal thickness in DIPs that will result in at least 1 pixel thick line */
    thickness = max(96.0f / (render_target->desc.dpiY * sqrtf(m->_21 * m->_21 + m->_22 * m->_22)),
            strikethrough->thickness);

    brush = d2d_draw_get_text_brush(context, effect);

    start.x = baseline_origin_x;
    start.y = baseline_origin_y + strikethrough->offset + thickness / 2.0f;
    end.x = start.x + strikethrough->width;
    end.y = start.y;
    prev_antialias_mode = d2d_d3d_render_target_set_aa_mode_from_text_aa_mode(render_target);
    d2d_d3d_render_target_DrawLine(&render_target->ID2D1RenderTarget_iface, start, end, brush, thickness, NULL);
    render_target->drawing_state.antialiasMode = prev_antialias_mode;

    ID2D1Brush_Release(brush);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_text_renderer_DrawInlineObject(IDWriteTextRenderer *iface, void *ctx,
        float origin_x, float origin_y, IDWriteInlineObject *object, BOOL is_sideways, BOOL is_rtl, IUnknown *effect)
{
    struct d2d_draw_text_layout_ctx *context = ctx;
    ID2D1Brush *brush;
    HRESULT hr;

    TRACE("iface %p, ctx %p, origin_x %.8e, origin_y %.8e, object %p, is_sideways %#x, is_rtl %#x, effect %p.\n",
            iface, ctx, origin_x, origin_y, object, is_sideways, is_rtl, effect);

    /* Inline objects may not pass effects all the way down, when using layout object internally for example.
       This is how default trimming sign object in DirectWrite works - it does not use effect passed to Draw(),
       and resulting DrawGlyphRun() is always called with NULL effect, however original effect is used and correct
       brush is selected at Direct2D level. */
    brush = context->brush;
    context->brush = d2d_draw_get_text_brush(context, effect);

    hr = IDWriteInlineObject_Draw(object, ctx, iface, origin_x, origin_y, is_sideways, is_rtl, effect);

    ID2D1Brush_Release(context->brush);
    context->brush = brush;

    return hr;
}

static const struct IDWriteTextRendererVtbl d2d_text_renderer_vtbl =
{
    d2d_text_renderer_QueryInterface,
    d2d_text_renderer_AddRef,
    d2d_text_renderer_Release,
    d2d_text_renderer_IsPixelSnappingDisabled,
    d2d_text_renderer_GetCurrentTransform,
    d2d_text_renderer_GetPixelsPerDip,
    d2d_text_renderer_DrawGlyphRun,
    d2d_text_renderer_DrawUnderline,
    d2d_text_renderer_DrawStrikethrough,
    d2d_text_renderer_DrawInlineObject,
};

static inline struct d2d_d3d_render_target *impl_from_ID2D1GdiInteropRenderTarget(ID2D1GdiInteropRenderTarget *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_d3d_render_target, ID2D1GdiInteropRenderTarget_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_QueryInterface(ID2D1GdiInteropRenderTarget *iface,
        REFIID iid, void **out)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(render_target->outer_unknown, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_AddRef(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_AddRef(render_target->outer_unknown);
}

static ULONG STDMETHODCALLTYPE d2d_gdi_interop_render_target_Release(ID2D1GdiInteropRenderTarget *iface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);

    TRACE("iface %p.\n", iface);

    return IUnknown_Release(render_target->outer_unknown);
}

static HRESULT d2d_d3d_render_target_get_surface(struct d2d_d3d_render_target *render_target, IDXGISurface1 **surface)
{
    ID3D10Resource *resource;
    HRESULT hr;

    ID3D10RenderTargetView_GetResource(render_target->view, &resource);
    hr = ID3D10Resource_QueryInterface(resource, &IID_IDXGISurface1, (void **)surface);
    ID3D10Resource_Release(resource);
    if (FAILED(hr))
    {
        *surface = NULL;
        WARN("Failed to get DXGI surface, %#x.\n", hr);
        return hr;
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_GetDC(ID2D1GdiInteropRenderTarget *iface,
        D2D1_DC_INITIALIZE_MODE mode, HDC *dc)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    HRESULT hr;

    TRACE("iface %p, mode %d, dc %p.\n", iface, mode, dc);

    if (FAILED(hr = d2d_d3d_render_target_get_surface(render_target, &surface)))
        return hr;

    hr = IDXGISurface1_GetDC(surface, mode != D2D1_DC_INITIALIZE_MODE_COPY, dc);
    IDXGISurface1_Release(surface);

    return hr;
}

static HRESULT STDMETHODCALLTYPE d2d_gdi_interop_render_target_ReleaseDC(ID2D1GdiInteropRenderTarget *iface,
        const RECT *update)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1GdiInteropRenderTarget(iface);
    IDXGISurface1 *surface;
    RECT update_rect;
    HRESULT hr;

    TRACE("iface %p, update rect %s.\n", iface, wine_dbgstr_rect(update));

    if (FAILED(hr = d2d_d3d_render_target_get_surface(render_target, &surface)))
        return hr;

    if (update)
        update_rect = *update;
    hr = IDXGISurface1_ReleaseDC(surface, update ? &update_rect : NULL);
    IDXGISurface1_Release(surface);

    return hr;
}

static const struct ID2D1GdiInteropRenderTargetVtbl d2d_gdi_interop_render_target_vtbl =
{
    d2d_gdi_interop_render_target_QueryInterface,
    d2d_gdi_interop_render_target_AddRef,
    d2d_gdi_interop_render_target_Release,
    d2d_gdi_interop_render_target_GetDC,
    d2d_gdi_interop_render_target_ReleaseDC,
};

static HRESULT d2d_d3d_render_target_init(struct d2d_d3d_render_target *render_target, ID2D1Factory *factory,
        IDXGISurface *surface, IUnknown *outer_unknown, const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    D3D10_SUBRESOURCE_DATA buffer_data;
    D3D10_STATE_BLOCK_MASK state_mask;
    DXGI_SURFACE_DESC surface_desc;
    IDWriteFactory *dwrite_factory;
    D3D10_RASTERIZER_DESC rs_desc;
    D3D10_BUFFER_DESC buffer_desc;
    D3D10_BLEND_DESC blend_desc;
    ID3D10Resource *resource;
    unsigned int i, j, k;
    HRESULT hr;

    static const D3D10_INPUT_ELEMENT_DESC il_desc_outline[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D10_INPUT_ELEMENT_DESC il_desc_triangle[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D10_INPUT_ELEMENT_DESC il_desc_bezier_outline[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"P", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"P", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 16, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"P", 2, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"PREV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 32, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"NEXT", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const D3D10_INPUT_ELEMENT_DESC il_desc_bezier[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 8, D3D10_INPUT_PER_VERTEX_DATA, 0},
    };
    static const DWORD vs_code_outline[] =
    {
#if 0
        float3x2 transform_geometry;
        float stroke_width;
        float4 transform_rtx;
        float4 transform_rty;

        /* The lines PₚᵣₑᵥP₀ and P₀Pₙₑₓₜ, both offset by ±½w, intersect each other at:
         *
         *   Pᵢ = P₀ ± w · ½q⃑ᵢ.
         *
         * Where:
         *
         *   q⃑ᵢ = q̂ₚᵣₑᵥ⊥ + tan(½θ) · -q̂ₚᵣₑᵥ
         *   θ  = ∠PₚᵣₑᵥP₀Pₙₑₓₜ
         *   q⃑ₚᵣₑᵥ = P₀ - Pₚᵣₑᵥ */
        float4 main(float2 position : POSITION, float2 prev : PREV, float2 next : NEXT) : SV_POSITION
        {
            float2 q_prev, q_next, v_p, q_i;
            float l;

            q_prev = float2(dot(prev, transform_geometry._11_21), dot(prev, transform_geometry._12_22));
            q_prev = normalize(q_prev);

            q_next = float2(dot(next, transform_geometry._11_21), dot(next, transform_geometry._12_22));
            q_next = normalize(q_next);

            /* tan(½θ) = sin(θ) / (1 + cos(θ))
             *         = (q̂ₚᵣₑᵥ⊥ · q̂ₙₑₓₜ) / (1 + (q̂ₚᵣₑᵥ · q̂ₙₑₓₜ)) */
            v_p = float2(-q_prev.y, q_prev.x);
            l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));
            q_i = l * q_prev + v_p;
            position = mul(float3(position, 1.0f), transform_geometry) + stroke_width * 0.5f * q_i;

            return float4(mul(float2x3(transform_rtx.xyz * transform_rtx.w, transform_rty.xyz * transform_rty.w),
                    float3(position.xy, 1.0f)) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
        }
#endif
        0x43425844, 0xe88f7af5, 0x480b101b, 0xfd80f66b, 0xd878e0b8, 0x00000001, 0x0000047c, 0x00000003,
        0x0000002c, 0x00000098, 0x000000cc, 0x4e475349, 0x00000064, 0x00000003, 0x00000008, 0x00000050,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000303, 0x00000059, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x0000005e, 0x00000000, 0x00000000, 0x00000003, 0x00000002,
        0x00000303, 0x49534f50, 0x4e4f4954, 0x45525000, 0x454e0056, 0xab005458, 0x4e47534f, 0x0000002c,
        0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x000003a8, 0x00010040, 0x000000ea, 0x04000059,
        0x00208e46, 0x00000000, 0x00000004, 0x0300005f, 0x00101032, 0x00000000, 0x0300005f, 0x00101032,
        0x00000001, 0x0300005f, 0x00101032, 0x00000002, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x02000068, 0x00000003, 0x0800000f, 0x00100012, 0x00000000, 0x00101046, 0x00000002, 0x00208046,
        0x00000000, 0x00000000, 0x0800000f, 0x00100022, 0x00000000, 0x00101046, 0x00000002, 0x00208046,
        0x00000000, 0x00000001, 0x0700000f, 0x00100042, 0x00000000, 0x00100046, 0x00000000, 0x00100046,
        0x00000000, 0x05000044, 0x00100042, 0x00000000, 0x0010002a, 0x00000000, 0x07000038, 0x00100032,
        0x00000000, 0x00100aa6, 0x00000000, 0x00100046, 0x00000000, 0x0800000f, 0x00100012, 0x00000001,
        0x00101046, 0x00000001, 0x00208046, 0x00000000, 0x00000000, 0x0800000f, 0x00100022, 0x00000001,
        0x00101046, 0x00000001, 0x00208046, 0x00000000, 0x00000001, 0x0700000f, 0x00100042, 0x00000000,
        0x00100046, 0x00000001, 0x00100046, 0x00000001, 0x05000044, 0x00100042, 0x00000000, 0x0010002a,
        0x00000000, 0x07000038, 0x00100032, 0x00000001, 0x00100aa6, 0x00000000, 0x00100046, 0x00000001,
        0x06000036, 0x001000c2, 0x00000001, 0x80100556, 0x00000041, 0x00000001, 0x0700000f, 0x00100042,
        0x00000000, 0x00100a26, 0x00000001, 0x00100046, 0x00000000, 0x0700000f, 0x00100012, 0x00000000,
        0x00100046, 0x00000001, 0x00100046, 0x00000000, 0x07000000, 0x00100012, 0x00000000, 0x0010000a,
        0x00000000, 0x00004001, 0x3f800000, 0x0800000e, 0x00100012, 0x00000000, 0x8010002a, 0x00000041,
        0x00000000, 0x0010000a, 0x00000000, 0x09000032, 0x00100032, 0x00000000, 0x00100006, 0x00000000,
        0x00100046, 0x00000001, 0x00100f36, 0x00000001, 0x08000038, 0x00100042, 0x00000000, 0x0020803a,
        0x00000000, 0x00000001, 0x00004001, 0x3f000000, 0x05000036, 0x00100032, 0x00000001, 0x00101046,
        0x00000000, 0x05000036, 0x00100042, 0x00000001, 0x00004001, 0x3f800000, 0x08000010, 0x00100012,
        0x00000002, 0x00100246, 0x00000001, 0x00208246, 0x00000000, 0x00000000, 0x08000010, 0x00100022,
        0x00000002, 0x00100246, 0x00000001, 0x00208246, 0x00000000, 0x00000001, 0x09000032, 0x00100032,
        0x00000000, 0x00100aa6, 0x00000000, 0x00100046, 0x00000000, 0x00100046, 0x00000002, 0x09000038,
        0x00100072, 0x00000001, 0x00208ff6, 0x00000000, 0x00000002, 0x00208246, 0x00000000, 0x00000002,
        0x05000036, 0x00100042, 0x00000000, 0x00004001, 0x3f800000, 0x07000010, 0x00100012, 0x00000001,
        0x00100246, 0x00000001, 0x00100246, 0x00000000, 0x09000038, 0x00100072, 0x00000002, 0x00208ff6,
        0x00000000, 0x00000003, 0x00208246, 0x00000000, 0x00000003, 0x07000010, 0x00100022, 0x00000001,
        0x00100246, 0x00000002, 0x00100246, 0x00000000, 0x0a000000, 0x00102032, 0x00000000, 0x00100046,
        0x00000001, 0x00004002, 0xbf800000, 0x3f800000, 0x00000000, 0x00000000, 0x08000036, 0x001020c2,
        0x00000000, 0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0100003e,
    };
    static const DWORD vs_code_triangle[] =
    {
        /* float3x2 transform;
         *
         * float4 main(float4 position : POSITION) : SV_POSITION
         * {
         *     return float4(mul(position.xyw, transform), position.zw);
         * } */
        0x43425844, 0x0add3194, 0x205f74ec, 0xab527fe7, 0xbe6ad704, 0x00000001, 0x00000128, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x49534f50, 0x4e4f4954, 0xababab00,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49, 0x52444853, 0x0000008c, 0x00010040,
        0x00000023, 0x04000059, 0x00208e46, 0x00000000, 0x00000002, 0x0300005f, 0x001010f2, 0x00000000,
        0x04000067, 0x001020f2, 0x00000000, 0x00000001, 0x08000010, 0x00102012, 0x00000000, 0x00101346,
        0x00000000, 0x00208246, 0x00000000, 0x00000000, 0x08000010, 0x00102022, 0x00000000, 0x00101346,
        0x00000000, 0x00208246, 0x00000000, 0x00000001, 0x05000036, 0x001020c2, 0x00000000, 0x00101ea6,
        0x00000000, 0x0100003e,
    };
    /*     ⎡p0.x p0.y 1⎤
     * A = ⎢p1.x p1.y 1⎥
     *     ⎣p2.x p2.y 1⎦
     *
     *     ⎡0 0⎤
     * B = ⎢½ 0⎥
     *     ⎣1 1⎦
     *
     * A' = ⎡p1.x-p0.x p1.y-p0.y⎤
     *      ⎣p2.x-p0.x p2.y-p0.y⎦
     *
     * B' = ⎡½ 0⎤
     *      ⎣1 1⎦
     *
     * A'T = B'
     * T = A'⁻¹B'
     */
    static const DWORD vs_code_bezier_outline[] =
    {
#if 0
        float3x2 transform_geometry;
        float stroke_width;
        float4 transform_rtx;
        float4 transform_rty;

        float4 main(float2 position : POSITION, float2 p0 : P0, float2 p1 : P1, float2 p2 : P2,
                float2 prev : PREV, float2 next : NEXT, out float4 texcoord : UV,
                out float2x2 stroke_transform : STROKE_TRANSFORM) : SV_POSITION
        {
            float2 q_prev, q_next, v_p, q_i, p;
            float2x2 geom, rt;
            float l;

            geom = float2x2(transform_geometry._11_21, transform_geometry._12_22);
            rt = float2x2(transform_rtx.xy, transform_rty.xy);
            stroke_transform = rt * stroke_width * 0.5f;

            p = mul(geom, position);
            p0 = mul(geom, p0);
            p1 = mul(geom, p1);
            p2 = mul(geom, p2);

            p -= p0;
            p1 -= p0;
            p2 -= p0;

            q_prev = normalize(mul(geom, prev));
            q_next = normalize(mul(geom, next));

            v_p = float2(-q_prev.y, q_prev.x);
            l = -dot(v_p, q_next) / (1.0f + dot(q_prev, q_next));
            q_i = l * q_prev + v_p;
            p += stroke_width * q_i;

            v_p = mul(rt, p2);
            v_p = normalize(float2(-v_p.y, v_p.x));
            if (abs(dot(mul(rt, p1), v_p)) < 1.0f)
            {
                texcoord.xzw = float3(0.0f, 0.0f, 0.0f);
                texcoord.y = dot(mul(rt, p), v_p);
            }
            else
            {
                texcoord.zw = sign(dot(mul(rt, p1), v_p)) * v_p;
                v_p = -float2(-p.y, p.x) / dot(float2(-p1.y, p1.x), p2);
                texcoord.x = dot(v_p, p1 - 0.5f * p2);
                texcoord.y = dot(v_p, p1);
            }

            position = mul(geom, position)
                    + float2(transform_geometry._31, transform_geometry._32) + stroke_width * q_i;
            position = mul(rt, position) + float2(transform_rtx.z, transform_rty.z);
            position = position * float2(transform_rtx.w, transform_rty.w) + float2(-1.0f, 1.0f);

            return float4(position, 0.0f, 1.0f);
        }
#endif
        0x43425844, 0x2b43c2df, 0x861ad5cd, 0x32b8fe33, 0x2d50f992, 0x00000001, 0x00000a9c, 0x00000003,
        0x0000002c, 0x000000e4, 0x00000174, 0x4e475349, 0x000000b0, 0x00000006, 0x00000008, 0x00000098,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000303, 0x000000a1, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000303, 0x000000a1, 0x00000001, 0x00000000, 0x00000003, 0x00000002,
        0x00000303, 0x000000a1, 0x00000002, 0x00000000, 0x00000003, 0x00000003, 0x00000303, 0x000000a3,
        0x00000000, 0x00000000, 0x00000003, 0x00000004, 0x00000303, 0x000000a8, 0x00000000, 0x00000000,
        0x00000003, 0x00000005, 0x00000303, 0x49534f50, 0x4e4f4954, 0x50005000, 0x00564552, 0x5458454e,
        0xababab00, 0x4e47534f, 0x00000088, 0x00000004, 0x00000008, 0x00000068, 0x00000000, 0x00000001,
        0x00000003, 0x00000000, 0x0000000f, 0x00000074, 0x00000000, 0x00000000, 0x00000003, 0x00000001,
        0x0000000f, 0x00000077, 0x00000000, 0x00000000, 0x00000003, 0x00000002, 0x00000c03, 0x00000077,
        0x00000001, 0x00000000, 0x00000003, 0x00000003, 0x00000c03, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x53005655, 0x4b4f5254, 0x52545f45, 0x46534e41, 0x004d524f, 0x52444853, 0x00000920, 0x00010040,
        0x00000248, 0x04000059, 0x00208e46, 0x00000000, 0x00000004, 0x0300005f, 0x00101032, 0x00000000,
        0x0300005f, 0x00101032, 0x00000001, 0x0300005f, 0x00101032, 0x00000002, 0x0300005f, 0x00101032,
        0x00000003, 0x0300005f, 0x00101032, 0x00000004, 0x0300005f, 0x00101032, 0x00000005, 0x04000067,
        0x001020f2, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000001, 0x03000065, 0x00102032,
        0x00000002, 0x03000065, 0x00102032, 0x00000003, 0x02000068, 0x00000005, 0x0800000f, 0x00100012,
        0x00000000, 0x00208046, 0x00000000, 0x00000000, 0x00101046, 0x00000005, 0x0800000f, 0x00100022,
        0x00000000, 0x00208046, 0x00000000, 0x00000001, 0x00101046, 0x00000005, 0x0700000f, 0x00100042,
        0x00000000, 0x00100046, 0x00000000, 0x00100046, 0x00000000, 0x05000044, 0x00100042, 0x00000000,
        0x0010002a, 0x00000000, 0x07000038, 0x00100032, 0x00000000, 0x00100aa6, 0x00000000, 0x00100046,
        0x00000000, 0x0800000f, 0x00100012, 0x00000001, 0x00208046, 0x00000000, 0x00000000, 0x00101046,
        0x00000004, 0x0800000f, 0x00100022, 0x00000001, 0x00208046, 0x00000000, 0x00000001, 0x00101046,
        0x00000004, 0x0700000f, 0x00100042, 0x00000000, 0x00100046, 0x00000001, 0x00100046, 0x00000001,
        0x05000044, 0x00100042, 0x00000000, 0x0010002a, 0x00000000, 0x07000038, 0x00100032, 0x00000001,
        0x00100aa6, 0x00000000, 0x00100046, 0x00000001, 0x06000036, 0x001000c2, 0x00000001, 0x80100556,
        0x00000041, 0x00000001, 0x0700000f, 0x00100042, 0x00000000, 0x00100a26, 0x00000001, 0x00100046,
        0x00000000, 0x0700000f, 0x00100012, 0x00000000, 0x00100046, 0x00000001, 0x00100046, 0x00000000,
        0x07000000, 0x00100012, 0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x3f800000, 0x0800000e,
        0x00100012, 0x00000000, 0x8010002a, 0x00000041, 0x00000000, 0x0010000a, 0x00000000, 0x09000032,
        0x00100032, 0x00000000, 0x00100006, 0x00000000, 0x00100046, 0x00000001, 0x00100f36, 0x00000001,
        0x0800000f, 0x00100012, 0x00000001, 0x00208046, 0x00000000, 0x00000000, 0x00101046, 0x00000000,
        0x08000000, 0x00100012, 0x00000002, 0x0010000a, 0x00000001, 0x0020802a, 0x00000000, 0x00000000,
        0x0800000f, 0x00100022, 0x00000001, 0x00208046, 0x00000000, 0x00000001, 0x00101046, 0x00000000,
        0x08000000, 0x00100022, 0x00000002, 0x0010001a, 0x00000001, 0x0020802a, 0x00000000, 0x00000001,
        0x0a000032, 0x001000c2, 0x00000000, 0x00208ff6, 0x00000000, 0x00000001, 0x00100406, 0x00000000,
        0x00100406, 0x00000002, 0x0800000f, 0x00100042, 0x00000001, 0x00208046, 0x00000000, 0x00000002,
        0x00100ae6, 0x00000000, 0x0800000f, 0x00100042, 0x00000000, 0x00208046, 0x00000000, 0x00000003,
        0x00100ae6, 0x00000000, 0x08000000, 0x00100042, 0x00000000, 0x0010002a, 0x00000000, 0x0020802a,
        0x00000000, 0x00000003, 0x08000038, 0x00100022, 0x00000002, 0x0010002a, 0x00000000, 0x0020803a,
        0x00000000, 0x00000003, 0x08000000, 0x00100042, 0x00000000, 0x0010002a, 0x00000001, 0x0020802a,
        0x00000000, 0x00000002, 0x08000038, 0x00100012, 0x00000002, 0x0010002a, 0x00000000, 0x0020803a,
        0x00000000, 0x00000002, 0x0a000000, 0x00102032, 0x00000000, 0x00100046, 0x00000002, 0x00004002,
        0xbf800000, 0x3f800000, 0x00000000, 0x00000000, 0x08000036, 0x001020c2, 0x00000000, 0x00004002,
        0x00000000, 0x00000000, 0x00000000, 0x3f800000, 0x0800000f, 0x00100012, 0x00000002, 0x00208046,
        0x00000000, 0x00000000, 0x00101046, 0x00000001, 0x0800000f, 0x00100022, 0x00000002, 0x00208046,
        0x00000000, 0x00000001, 0x00101046, 0x00000001, 0x08000000, 0x001000c2, 0x00000000, 0x00100406,
        0x00000001, 0x80100406, 0x00000041, 0x00000002, 0x0a000032, 0x00100032, 0x00000000, 0x00208ff6,
        0x00000000, 0x00000001, 0x00100046, 0x00000000, 0x00100ae6, 0x00000000, 0x0800000f, 0x00100012,
        0x00000001, 0x00208046, 0x00000000, 0x00000002, 0x00100046, 0x00000000, 0x0800000f, 0x00100022,
        0x00000001, 0x00208046, 0x00000000, 0x00000003, 0x00100046, 0x00000000, 0x0800000f, 0x00100012,
        0x00000003, 0x00208046, 0x00000000, 0x00000000, 0x00101046, 0x00000003, 0x0800000f, 0x00100022,
        0x00000003, 0x00208046, 0x00000000, 0x00000001, 0x00101046, 0x00000003, 0x08000000, 0x001000c2,
        0x00000001, 0x80100406, 0x00000041, 0x00000002, 0x00100406, 0x00000003, 0x0800000f, 0x00100082,
        0x00000000, 0x00208046, 0x00000000, 0x00000003, 0x00100ae6, 0x00000001, 0x06000036, 0x00100042,
        0x00000002, 0x8010003a, 0x00000041, 0x00000000, 0x0800000f, 0x00100082, 0x00000002, 0x00208046,
        0x00000000, 0x00000002, 0x00100ae6, 0x00000001, 0x0700000f, 0x00100082, 0x00000000, 0x00100ae6,
        0x00000002, 0x00100ae6, 0x00000002, 0x05000044, 0x00100082, 0x00000000, 0x0010003a, 0x00000000,
        0x07000038, 0x001000c2, 0x00000002, 0x00100ff6, 0x00000000, 0x00100ea6, 0x00000002, 0x0700000f,
        0x00100022, 0x00000003, 0x00100046, 0x00000001, 0x00100ae6, 0x00000002, 0x06000036, 0x00100042,
        0x00000000, 0x8010001a, 0x00000041, 0x00000000, 0x0800000f, 0x00100012, 0x00000001, 0x00208046,
        0x00000000, 0x00000000, 0x00101046, 0x00000002, 0x0800000f, 0x00100022, 0x00000001, 0x00208046,
        0x00000000, 0x00000001, 0x00101046, 0x00000002, 0x08000000, 0x00100032, 0x00000004, 0x80100046,
        0x00000041, 0x00000002, 0x00100046, 0x00000001, 0x06000036, 0x00100042, 0x00000004, 0x8010001a,
        0x00000041, 0x00000004, 0x0700000f, 0x00100022, 0x00000000, 0x00100a26, 0x00000004, 0x00100ae6,
        0x00000001, 0x0d000032, 0x00100032, 0x00000001, 0x80100ae6, 0x00000041, 0x00000001, 0x00004002,
        0x3f000000, 0x3f000000, 0x00000000, 0x00000000, 0x00100046, 0x00000004, 0x0800000e, 0x00100032,
        0x00000000, 0x80100a26, 0x00000041, 0x00000000, 0x00100556, 0x00000000, 0x0700000f, 0x00100012,
        0x00000001, 0x00100046, 0x00000000, 0x00100046, 0x00000001, 0x0700000f, 0x00100022, 0x00000001,
        0x00100046, 0x00000000, 0x00100046, 0x00000004, 0x0800000f, 0x00100012, 0x00000000, 0x00208046,
        0x00000000, 0x00000002, 0x00100046, 0x00000004, 0x0800000f, 0x00100022, 0x00000000, 0x00208046,
        0x00000000, 0x00000003, 0x00100046, 0x00000004, 0x0700000f, 0x00100012, 0x00000000, 0x00100046,
        0x00000000, 0x00100ae6, 0x00000002, 0x07000031, 0x00100022, 0x00000000, 0x00004001, 0x00000000,
        0x0010000a, 0x00000000, 0x07000031, 0x00100042, 0x00000000, 0x0010000a, 0x00000000, 0x00004001,
        0x00000000, 0x08000031, 0x00100012, 0x00000000, 0x8010000a, 0x00000081, 0x00000000, 0x00004001,
        0x3f800000, 0x0800001e, 0x00100022, 0x00000000, 0x8010001a, 0x00000041, 0x00000000, 0x0010002a,
        0x00000000, 0x0500002b, 0x00100022, 0x00000000, 0x0010001a, 0x00000000, 0x07000038, 0x001000c2,
        0x00000001, 0x00100ea6, 0x00000002, 0x00100556, 0x00000000, 0x08000036, 0x001000d2, 0x00000003,
        0x00004002, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x09000037, 0x001020f2, 0x00000001,
        0x00100006, 0x00000000, 0x00100e46, 0x00000003, 0x00100e46, 0x00000001, 0x06000036, 0x00100032,
        0x00000000, 0x00208046, 0x00000000, 0x00000002, 0x06000036, 0x001000c2, 0x00000000, 0x00208406,
        0x00000000, 0x00000003, 0x08000038, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x00208ff6,
        0x00000000, 0x00000001, 0x0a000038, 0x001000f2, 0x00000000, 0x00100e46, 0x00000000, 0x00004002,
        0x3f000000, 0x3f000000, 0x3f000000, 0x3f000000, 0x05000036, 0x00102032, 0x00000002, 0x00100086,
        0x00000000, 0x05000036, 0x00102032, 0x00000003, 0x001005d6, 0x00000000, 0x0100003e,
    };
    static const DWORD vs_code_bezier[] =
    {
#if 0
        float3x2 transform;

        float4 main(float4 position : POSITION,
              inout float3 texcoord : TEXCOORD0) : SV_POSITION
        {
            return float4(mul(position.xyw, transform), position.zw);
        }
#endif
        0x43425844, 0x5e578adb, 0x093f7e27, 0x50d478af, 0xec3dfa4f, 0x00000001, 0x00000198, 0x00000003,
        0x0000002c, 0x00000080, 0x000000d8, 0x4e475349, 0x0000004c, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000000, 0x00000003, 0x00000000, 0x00000f0f, 0x00000041, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000707, 0x49534f50, 0x4e4f4954, 0x58455400, 0x524f4f43, 0xabab0044,
        0x4e47534f, 0x00000050, 0x00000002, 0x00000008, 0x00000038, 0x00000000, 0x00000001, 0x00000003,
        0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000, 0x00000003, 0x00000001, 0x00000807,
        0x505f5653, 0x5449534f, 0x004e4f49, 0x43584554, 0x44524f4f, 0xababab00, 0x52444853, 0x000000b8,
        0x00010040, 0x0000002e, 0x04000059, 0x00208e46, 0x00000000, 0x00000002, 0x0300005f, 0x001010f2,
        0x00000000, 0x0300005f, 0x00101072, 0x00000001, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
        0x03000065, 0x00102072, 0x00000001, 0x08000010, 0x00102012, 0x00000000, 0x00101346, 0x00000000,
        0x00208246, 0x00000000, 0x00000000, 0x08000010, 0x00102022, 0x00000000, 0x00101346, 0x00000000,
        0x00208246, 0x00000000, 0x00000001, 0x05000036, 0x001020c2, 0x00000000, 0x00101ea6, 0x00000000,
        0x05000036, 0x00102072, 0x00000001, 0x00101246, 0x00000001, 0x0100003e,
    };
    static const DWORD ps_code_triangle_solid[] =
    {
        /* float4 color;
         *
         * float4 main(float4 position : SV_POSITION) : SV_Target
         * {
         *     return color;
         * } */
        0x43425844, 0x88eefcfd, 0x93d6fd47, 0x173c242f, 0x0106d07a, 0x00000001, 0x000000dc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000040, 0x00000040,
        0x00000010, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03000065, 0x001020f2, 0x00000000,
        0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code_triangle_solid_bitmap[] =
    {
#if 0
        float4 color;

        float3x2 transform;
        float opacity;
        bool ignore_alpha;

        SamplerState s;
        Texture2D t;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 texcoord;
            float4 ret;

            texcoord.x = position.x * transform._11 + position.y * transform._21 + transform._31;
            texcoord.y = position.x * transform._12 + position.y * transform._22 + transform._32;
            ret = t.Sample(s, texcoord) * opacity;
            if (ignore_alpha)
                ret.a = opacity;

            return color * ret.a;
        }
#endif
        0x43425844, 0x2260a2ae, 0x81907b3e, 0xcaf27063, 0xccb83ef2, 0x00000001, 0x00000208, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x0000016c, 0x00000040,
        0x0000005b, 0x04000059, 0x00208e46, 0x00000000, 0x00000004, 0x0300005a, 0x00106000, 0x00000000,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0800000f, 0x00100012, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000001, 0x08000000, 0x00100012, 0x00000000,
        0x0010000a, 0x00000000, 0x0020802a, 0x00000000, 0x00000001, 0x0800000f, 0x00100042, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000002, 0x08000000, 0x00100022, 0x00000000,
        0x0010002a, 0x00000000, 0x0020802a, 0x00000000, 0x00000002, 0x09000045, 0x001000f2, 0x00000000,
        0x00100046, 0x00000000, 0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x08000038, 0x00100012,
        0x00000000, 0x0010003a, 0x00000000, 0x0020803a, 0x00000000, 0x00000002, 0x0b000037, 0x00100012,
        0x00000000, 0x0020800a, 0x00000000, 0x00000003, 0x0020803a, 0x00000000, 0x00000002, 0x0010000a,
        0x00000000, 0x08000038, 0x001020f2, 0x00000000, 0x00100006, 0x00000000, 0x00208e46, 0x00000000,
        0x00000000, 0x0100003e,
    };
    static const DWORD ps_code_triangle_bitmap[] =
    {
#if 0
        float3x2 transform;
        float opacity;
        bool ignore_alpha;

        SamplerState s;
        Texture2D t;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 texcoord;
            float4 ret;

            texcoord.x = position.x * transform._11 + position.y * transform._21 + transform._31;
            texcoord.y = position.x * transform._12 + position.y * transform._22 + transform._32;
            ret = t.Sample(s, texcoord) * opacity;
            if (ignore_alpha)
                ret.a = opacity;

            return ret;
        }
#endif
        0x43425844, 0xf5bb1e01, 0xe3386963, 0xcaa095bd, 0xea2887de, 0x00000001, 0x000001fc, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000160, 0x00000040,
        0x00000058, 0x04000059, 0x00208e46, 0x00000000, 0x00000003, 0x0300005a, 0x00106000, 0x00000000,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0800000f, 0x00100012, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000000, 0x08000000, 0x00100012, 0x00000000,
        0x0010000a, 0x00000000, 0x0020802a, 0x00000000, 0x00000000, 0x0800000f, 0x00100042, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000001, 0x08000000, 0x00100022, 0x00000000,
        0x0010002a, 0x00000000, 0x0020802a, 0x00000000, 0x00000001, 0x09000045, 0x001000f2, 0x00000000,
        0x00100046, 0x00000000, 0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x08000038, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x00208ff6, 0x00000000, 0x00000001, 0x0b000037, 0x00102082,
        0x00000000, 0x0020800a, 0x00000000, 0x00000002, 0x0020803a, 0x00000000, 0x00000001, 0x0010003a,
        0x00000000, 0x05000036, 0x00102072, 0x00000000, 0x00100246, 0x00000000, 0x0100003e,
    };
    static const DWORD ps_code_triangle_bitmap_solid[] =
    {
#if 0
        float3x2 transform;
        float opacity;
        bool ignore_alpha;

        float4 color;

        SamplerState s;
        Texture2D t;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 texcoord;
            float4 ret;

            texcoord.x = position.x * transform._11 + position.y * transform._21 + transform._31;
            texcoord.y = position.x * transform._12 + position.y * transform._22 + transform._32;
            ret = t.Sample(s, texcoord) * opacity;
            if (ignore_alpha)
                ret.a = opacity;

            return ret * color.a;
        }
#endif
        0x43425844, 0x45447736, 0x63a6dd80, 0x1778fc71, 0x1e6d322e, 0x00000001, 0x00000208, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x0000016c, 0x00000040,
        0x0000005b, 0x04000059, 0x00208e46, 0x00000000, 0x00000004, 0x0300005a, 0x00106000, 0x00000000,
        0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001,
        0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0800000f, 0x00100012, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000000, 0x08000000, 0x00100012, 0x00000000,
        0x0010000a, 0x00000000, 0x0020802a, 0x00000000, 0x00000000, 0x0800000f, 0x00100042, 0x00000000,
        0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000001, 0x08000000, 0x00100022, 0x00000000,
        0x0010002a, 0x00000000, 0x0020802a, 0x00000000, 0x00000001, 0x09000045, 0x001000f2, 0x00000000,
        0x00100046, 0x00000000, 0x00107e46, 0x00000000, 0x00106000, 0x00000000, 0x08000038, 0x001000f2,
        0x00000000, 0x00100e46, 0x00000000, 0x00208ff6, 0x00000000, 0x00000001, 0x0b000037, 0x00100082,
        0x00000000, 0x0020800a, 0x00000000, 0x00000002, 0x0020803a, 0x00000000, 0x00000001, 0x0010003a,
        0x00000000, 0x08000038, 0x001020f2, 0x00000000, 0x00100e46, 0x00000000, 0x00208ff6, 0x00000000,
        0x00000003, 0x0100003e,
    };
    static const DWORD ps_code_triangle_bitmap_bitmap[] =
    {
#if 0
        struct brush
        {
            float3x2 transform;
            float opacity;
            bool ignore_alpha;
        } brush0, brush1;

        SamplerState s0, s1;
        Texture2D t0, t1;

        float4 main(float4 position : SV_POSITION) : SV_Target
        {
            float2 texcoord;
            float opacity;
            float4 ret;

            texcoord.x = position.x * brush0.transform._11 + position.y * brush0.transform._21 + brush0.transform._31;
            texcoord.y = position.x * brush0.transform._12 + position.y * brush0.transform._22 + brush0.transform._32;
            ret = t0.Sample(s0, texcoord) * brush0.opacity;
            if (brush0.ignore_alpha)
                ret.a = brush0.opacity;

            texcoord.x = position.x * brush1.transform._11 + position.y * brush1.transform._21 + brush1.transform._31;
            texcoord.y = position.x * brush1.transform._12 + position.y * brush1.transform._22 + brush1.transform._32;
            opacity = t1.Sample(s1, texcoord).a * brush1.opacity;
            if (brush1.ignore_alpha)
                opacity = brush1.opacity;

            return ret * opacity;
        }
#endif
        0x43425844, 0x8eee6bfc, 0x57b72708, 0xa0f7c086, 0x867c11ec, 0x00000001, 0x00000310, 0x00000003,
        0x0000002c, 0x00000060, 0x00000094, 0x4e475349, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000030f, 0x505f5653, 0x5449534f, 0x004e4f49,
        0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003,
        0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x00000274, 0x00000040,
        0x0000009d, 0x04000059, 0x00208e46, 0x00000000, 0x00000006, 0x0300005a, 0x00106000, 0x00000000,
        0x0300005a, 0x00106000, 0x00000001, 0x04001858, 0x00107000, 0x00000000, 0x00005555, 0x04001858,
        0x00107000, 0x00000001, 0x00005555, 0x04002064, 0x00101032, 0x00000000, 0x00000001, 0x03000065,
        0x001020f2, 0x00000000, 0x02000068, 0x00000002, 0x0800000f, 0x00100012, 0x00000000, 0x00101046,
        0x00000000, 0x00208046, 0x00000000, 0x00000003, 0x08000000, 0x00100012, 0x00000000, 0x0010000a,
        0x00000000, 0x0020802a, 0x00000000, 0x00000003, 0x0800000f, 0x00100042, 0x00000000, 0x00101046,
        0x00000000, 0x00208046, 0x00000000, 0x00000004, 0x08000000, 0x00100022, 0x00000000, 0x0010002a,
        0x00000000, 0x0020802a, 0x00000000, 0x00000004, 0x09000045, 0x001000f2, 0x00000000, 0x00100046,
        0x00000000, 0x00107e46, 0x00000001, 0x00106000, 0x00000001, 0x08000038, 0x00100012, 0x00000000,
        0x0010003a, 0x00000000, 0x0020803a, 0x00000000, 0x00000004, 0x0b000037, 0x00100012, 0x00000000,
        0x0020800a, 0x00000000, 0x00000005, 0x0020803a, 0x00000000, 0x00000004, 0x0010000a, 0x00000000,
        0x0800000f, 0x00100022, 0x00000000, 0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000000,
        0x08000000, 0x00100012, 0x00000001, 0x0010001a, 0x00000000, 0x0020802a, 0x00000000, 0x00000000,
        0x0800000f, 0x00100022, 0x00000000, 0x00101046, 0x00000000, 0x00208046, 0x00000000, 0x00000001,
        0x08000000, 0x00100022, 0x00000001, 0x0010001a, 0x00000000, 0x0020802a, 0x00000000, 0x00000001,
        0x09000045, 0x001000f2, 0x00000001, 0x00100046, 0x00000001, 0x00107e46, 0x00000000, 0x00106000,
        0x00000000, 0x08000038, 0x001000f2, 0x00000001, 0x00100e46, 0x00000001, 0x00208ff6, 0x00000000,
        0x00000001, 0x0b000037, 0x00100082, 0x00000001, 0x0020800a, 0x00000000, 0x00000002, 0x0020803a,
        0x00000000, 0x00000001, 0x0010003a, 0x00000001, 0x07000038, 0x001020f2, 0x00000000, 0x00100006,
        0x00000000, 0x00100e46, 0x00000001, 0x0100003e,
    };
    /* Evaluate the implicit form of the curve (u² - v = 0) in texture space,
     * using the screen-space partial derivatives to convert the calculated
     * distance to object space.
     *
     * d(x, y) = |f(x, y)| / ‖∇f(x, y)‖
     *         = |f(x, y)| / √((∂f/∂x)² + (∂f/∂y)²)
     * f(x, y) = u(x, y)² - v(x, y)
     * ∂f/∂x = 2u · ∂u/∂x - ∂v/∂x
     * ∂f/∂y = 2u · ∂u/∂y - ∂v/∂y */
    static const DWORD ps_code_bezier_solid_outline[] =
    {
#if 0
        float4 color;

        float4 main(float4 position : SV_POSITION, float4 uv : UV,
                nointerpolation float2x2 stroke_transform : STROKE_TRANSFORM) : SV_Target
        {
            float2 du, dv, df;

            du = float2(ddx(uv.x), ddy(uv.x));
            dv = float2(ddx(uv.y), ddy(uv.y));
            df = 2.0f * uv.x * du - dv;

            clip(dot(df, uv.zw));
            clip(length(mul(stroke_transform, df)) - abs(uv.x * uv.x - uv.y));
            return color;
        }
#endif
        0x43425844, 0x9da521d4, 0x4c86449e, 0x4f2c1641, 0x6f798508, 0x00000001, 0x000002f4, 0x00000003,
        0x0000002c, 0x000000bc, 0x000000f0, 0x4e475349, 0x00000088, 0x00000004, 0x00000008, 0x00000068,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000074, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000f0f, 0x00000077, 0x00000000, 0x00000000, 0x00000003, 0x00000002,
        0x00000303, 0x00000077, 0x00000001, 0x00000000, 0x00000003, 0x00000003, 0x00000303, 0x505f5653,
        0x5449534f, 0x004e4f49, 0x53005655, 0x4b4f5254, 0x52545f45, 0x46534e41, 0x004d524f, 0x4e47534f,
        0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000, 0x00000003, 0x00000000,
        0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000001fc, 0x00000040, 0x0000007f,
        0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03001062, 0x001010f2, 0x00000001, 0x03000862,
        0x00101032, 0x00000002, 0x03000862, 0x00101032, 0x00000003, 0x03000065, 0x001020f2, 0x00000000,
        0x02000068, 0x00000002, 0x0500000b, 0x00100032, 0x00000000, 0x00101046, 0x00000001, 0x0500000c,
        0x001000c2, 0x00000000, 0x00101406, 0x00000001, 0x07000000, 0x00100012, 0x00000001, 0x0010100a,
        0x00000001, 0x0010100a, 0x00000001, 0x0a000032, 0x00100032, 0x00000000, 0x00100006, 0x00000001,
        0x00100086, 0x00000000, 0x801005d6, 0x00000041, 0x00000000, 0x0700000f, 0x00100042, 0x00000000,
        0x00100046, 0x00000000, 0x00101ae6, 0x00000001, 0x07000031, 0x00100042, 0x00000000, 0x0010002a,
        0x00000000, 0x00004001, 0x00000000, 0x0304000d, 0x0010002a, 0x00000000, 0x07000038, 0x00100062,
        0x00000000, 0x00100556, 0x00000000, 0x00101106, 0x00000003, 0x09000032, 0x00100032, 0x00000000,
        0x00101046, 0x00000002, 0x00100006, 0x00000000, 0x00100596, 0x00000000, 0x0700000f, 0x00100012,
        0x00000000, 0x00100046, 0x00000000, 0x00100046, 0x00000000, 0x0500004b, 0x00100012, 0x00000000,
        0x0010000a, 0x00000000, 0x0a000032, 0x00100022, 0x00000000, 0x0010100a, 0x00000001, 0x0010100a,
        0x00000001, 0x8010101a, 0x00000041, 0x00000001, 0x08000000, 0x00100012, 0x00000000, 0x8010001a,
        0x000000c1, 0x00000000, 0x0010000a, 0x00000000, 0x07000031, 0x00100012, 0x00000000, 0x0010000a,
        0x00000000, 0x00004001, 0x00000000, 0x0304000d, 0x0010000a, 0x00000000, 0x06000036, 0x001020f2,
        0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
    };
    /* The basic idea here is to evaluate the implicit form of the curve in
     * texture space. "t.z" determines which side of the curve is shaded. */
    static const DWORD ps_code_bezier_solid[] =
    {
#if 0
        float4 color;

        float4 main(float4 position : SV_POSITION, float3 t : TEXCOORD0) : SV_Target
        {
            clip((t.x * t.x - t.y) * t.z);
            return color;
        }
#endif
        0x43425844, 0x66075f9e, 0x2ffe405b, 0xb551ee63, 0xa0d9f457, 0x00000001, 0x00000180, 0x00000003,
        0x0000002c, 0x00000084, 0x000000b8, 0x4e475349, 0x00000050, 0x00000002, 0x00000008, 0x00000038,
        0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x00000044, 0x00000000, 0x00000000,
        0x00000003, 0x00000001, 0x00000707, 0x505f5653, 0x5449534f, 0x004e4f49, 0x43584554, 0x44524f4f,
        0xababab00, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000000,
        0x00000003, 0x00000000, 0x0000000f, 0x545f5653, 0x65677261, 0xabab0074, 0x52444853, 0x000000c0,
        0x00000040, 0x00000030, 0x04000059, 0x00208e46, 0x00000000, 0x00000001, 0x03001062, 0x00101072,
        0x00000001, 0x03000065, 0x001020f2, 0x00000000, 0x02000068, 0x00000001, 0x0a000032, 0x00100012,
        0x00000000, 0x0010100a, 0x00000001, 0x0010100a, 0x00000001, 0x8010101a, 0x00000041, 0x00000001,
        0x07000038, 0x00100012, 0x00000000, 0x0010000a, 0x00000000, 0x0010102a, 0x00000001, 0x07000031,
        0x00100012, 0x00000000, 0x0010000a, 0x00000000, 0x00004001, 0x00000000, 0x0304000d, 0x0010000a,
        0x00000000, 0x06000036, 0x001020f2, 0x00000000, 0x00208e46, 0x00000000, 0x00000000, 0x0100003e,
    };
    static const struct brush_shader
    {
        const void *byte_code;
        size_t byte_code_size;
        enum d2d_shape_type shape_type;
        enum d2d_brush_type brush_type;
        enum d2d_brush_type opacity_brush_type;
    }
    brush_shaders[] =
    {
        {ps_code_triangle_solid, sizeof(ps_code_triangle_solid),
                D2D_SHAPE_TYPE_TRIANGLE, D2D_BRUSH_TYPE_SOLID, D2D_BRUSH_TYPE_COUNT},
        {ps_code_triangle_solid_bitmap, sizeof(ps_code_triangle_solid_bitmap),
                D2D_SHAPE_TYPE_TRIANGLE, D2D_BRUSH_TYPE_SOLID, D2D_BRUSH_TYPE_BITMAP},
        {ps_code_triangle_bitmap, sizeof(ps_code_triangle_bitmap),
                D2D_SHAPE_TYPE_TRIANGLE, D2D_BRUSH_TYPE_BITMAP, D2D_BRUSH_TYPE_COUNT},
        {ps_code_triangle_bitmap_solid, sizeof(ps_code_triangle_bitmap_solid),
                D2D_SHAPE_TYPE_TRIANGLE, D2D_BRUSH_TYPE_BITMAP, D2D_BRUSH_TYPE_SOLID},
        {ps_code_triangle_bitmap_bitmap, sizeof(ps_code_triangle_bitmap_bitmap),
                D2D_SHAPE_TYPE_TRIANGLE, D2D_BRUSH_TYPE_BITMAP, D2D_BRUSH_TYPE_BITMAP},
        {ps_code_bezier_solid, sizeof(ps_code_bezier_solid),
                D2D_SHAPE_TYPE_BEZIER, D2D_BRUSH_TYPE_SOLID, D2D_BRUSH_TYPE_COUNT},
        {ps_code_bezier_solid_outline, sizeof(ps_code_bezier_solid_outline),
                D2D_SHAPE_TYPE_BEZIER_OUTLINE, D2D_BRUSH_TYPE_SOLID, D2D_BRUSH_TYPE_COUNT},
    };
    static const struct
    {
        float x, y;
    }
    quad[] =
    {
        {-1.0f,  1.0f},
        {-1.0f, -1.0f},
        { 1.0f,  1.0f},
        { 1.0f, -1.0f},
    };
    static const UINT16 indices[] = {0, 1, 2, 2, 1, 3};
    float dpi_x, dpi_y;

    dpi_x = desc->dpiX;
    dpi_y = desc->dpiY;

    if (dpi_x == 0.0f && dpi_y == 0.0f)
    {
        dpi_x = 96.0f;
        dpi_y = 96.0f;
    }
    else if (dpi_x <= 0.0f || dpi_y <= 0.0f)
        return E_INVALIDARG;

    if (desc->type != D2D1_RENDER_TARGET_TYPE_DEFAULT && desc->type != D2D1_RENDER_TARGET_TYPE_HARDWARE)
        WARN("Ignoring render target type %#x.\n", desc->type);
    if (desc->usage != D2D1_RENDER_TARGET_USAGE_NONE)
        FIXME("Ignoring render target usage %#x.\n", desc->usage);
    if (desc->minLevel != D2D1_FEATURE_LEVEL_DEFAULT)
        WARN("Ignoring feature level %#x.\n", desc->minLevel);

    render_target->ID2D1RenderTarget_iface.lpVtbl = &d2d_d3d_render_target_vtbl;
    render_target->ID2D1GdiInteropRenderTarget_iface.lpVtbl = &d2d_gdi_interop_render_target_vtbl;
    render_target->IDWriteTextRenderer_iface.lpVtbl = &d2d_text_renderer_vtbl;
    render_target->refcount = 1;
    render_target->factory = factory;
    ID2D1Factory_AddRef(render_target->factory);

    render_target->outer_unknown = outer_unknown ? outer_unknown :
            (IUnknown *)&render_target->ID2D1RenderTarget_iface;

    if (FAILED(hr = IDXGISurface_GetDevice(surface, &IID_ID3D10Device, (void **)&render_target->device)))
    {
        WARN("Failed to get device interface, hr %#x.\n", hr);
        ID2D1Factory_Release(render_target->factory);
        return hr;
    }

    if (FAILED(hr = IDXGISurface_QueryInterface(surface, &IID_ID3D10Resource, (void **)&resource)))
    {
        WARN("Failed to get ID3D10Resource interface, hr %#x.\n", hr);
        goto err;
    }

    hr = ID3D10Device_CreateRenderTargetView(render_target->device, resource, NULL, &render_target->view);
    ID3D10Resource_Release(resource);
    if (FAILED(hr))
    {
        WARN("Failed to create rendertarget view, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = D3D10StateBlockMaskEnableAll(&state_mask)))
    {
        WARN("Failed to create stateblock mask, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = D3D10CreateStateBlock(render_target->device, &state_mask, &render_target->stateblock)))
    {
        WARN("Failed to create stateblock, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateInputLayout(render_target->device, il_desc_outline,
            ARRAY_SIZE(il_desc_outline), vs_code_outline, sizeof(vs_code_outline),
            &render_target->shape_resources[D2D_SHAPE_TYPE_OUTLINE].il)))
    {
        WARN("Failed to create outline input layout, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateInputLayout(render_target->device, il_desc_triangle,
            ARRAY_SIZE(il_desc_triangle), vs_code_triangle, sizeof(vs_code_triangle),
            &render_target->shape_resources[D2D_SHAPE_TYPE_TRIANGLE].il)))
    {
        WARN("Failed to create triangle input layout, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateInputLayout(render_target->device, il_desc_bezier_outline,
            ARRAY_SIZE(il_desc_bezier_outline), vs_code_bezier_outline, sizeof(vs_code_bezier_outline),
            &render_target->shape_resources[D2D_SHAPE_TYPE_BEZIER_OUTLINE].il)))
    {
        WARN("Failed to create bezier outline input layout, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateInputLayout(render_target->device, il_desc_bezier,
            ARRAY_SIZE(il_desc_bezier), vs_code_bezier, sizeof(vs_code_bezier),
            &render_target->shape_resources[D2D_SHAPE_TYPE_BEZIER].il)))
    {
        WARN("Failed to create bezier input layout, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateVertexShader(render_target->device, vs_code_outline,
            sizeof(vs_code_outline), &render_target->shape_resources[D2D_SHAPE_TYPE_OUTLINE].vs)))
    {
        WARN("Failed to create outline vertex shader, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateVertexShader(render_target->device, vs_code_bezier_outline,
            sizeof(vs_code_bezier_outline), &render_target->shape_resources[D2D_SHAPE_TYPE_BEZIER_OUTLINE].vs)))
    {
        WARN("Failed to create bezier outline vertex shader, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateVertexShader(render_target->device, vs_code_triangle,
            sizeof(vs_code_triangle), &render_target->shape_resources[D2D_SHAPE_TYPE_TRIANGLE].vs)))
    {
        WARN("Failed to create triangle vertex shader, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = ID3D10Device_CreateVertexShader(render_target->device,  vs_code_bezier,
            sizeof(vs_code_bezier), &render_target->shape_resources[D2D_SHAPE_TYPE_BEZIER].vs)))
    {
        WARN("Failed to create bezier vertex shader, hr %#x.\n", hr);
        goto err;
    }

    for (i = 0; i < ARRAY_SIZE(brush_shaders); ++i)
    {
        const struct brush_shader *bs = &brush_shaders[i];
        if (FAILED(hr = ID3D10Device_CreatePixelShader(render_target->device, bs->byte_code, bs->byte_code_size,
                &render_target->shape_resources[bs->shape_type].ps[bs->brush_type][bs->opacity_brush_type])))
        {
            WARN("Failed to create pixel shader for shape type %#x and brush types %#x/%#x.\n",
                    bs->shape_type, bs->brush_type, bs->opacity_brush_type);
            goto err;
        }
    }

    for (j = 0; j < D2D_BRUSH_TYPE_COUNT; ++j)
    {
        for (k = 0; k < D2D_BRUSH_TYPE_COUNT + 1; ++k)
        {
            struct d2d_shape_resources *outline = &render_target->shape_resources[D2D_SHAPE_TYPE_OUTLINE];
            struct d2d_shape_resources *triangle = &render_target->shape_resources[D2D_SHAPE_TYPE_TRIANGLE];

            if (triangle->ps[j][k])
                ID3D10PixelShader_AddRef(outline->ps[j][k] = triangle->ps[j][k]);
        }
    }

    buffer_desc.ByteWidth = sizeof(indices);
    buffer_desc.Usage = D3D10_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
    buffer_desc.CPUAccessFlags = 0;
    buffer_desc.MiscFlags = 0;

    buffer_data.pSysMem = indices;
    buffer_data.SysMemPitch = 0;
    buffer_data.SysMemSlicePitch = 0;

    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device,
            &buffer_desc, &buffer_data, &render_target->ib)))
    {
        WARN("Failed to create clear index buffer, hr %#x.\n", hr);
        goto err;
    }

    buffer_desc.ByteWidth = sizeof(quad);
    buffer_desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    buffer_data.pSysMem = quad;

    render_target->vb_stride = sizeof(*quad);
    if (FAILED(hr = ID3D10Device_CreateBuffer(render_target->device,
            &buffer_desc, &buffer_data, &render_target->vb)))
    {
        WARN("Failed to create clear vertex buffer, hr %#x.\n", hr);
        goto err;
    }

    rs_desc.FillMode = D3D10_FILL_SOLID;
    rs_desc.CullMode = D3D10_CULL_NONE;
    rs_desc.FrontCounterClockwise = FALSE;
    rs_desc.DepthBias = 0;
    rs_desc.DepthBiasClamp = 0.0f;
    rs_desc.SlopeScaledDepthBias = 0.0f;
    rs_desc.DepthClipEnable = TRUE;
    rs_desc.ScissorEnable = TRUE;
    rs_desc.MultisampleEnable = FALSE;
    rs_desc.AntialiasedLineEnable = FALSE;
    if (FAILED(hr = ID3D10Device_CreateRasterizerState(render_target->device, &rs_desc, &render_target->rs)))
    {
        WARN("Failed to create clear rasterizer state, hr %#x.\n", hr);
        goto err;
    }

    memset(&blend_desc, 0, sizeof(blend_desc));
    blend_desc.BlendEnable[0] = TRUE;
    blend_desc.SrcBlend = D3D10_BLEND_ONE;
    blend_desc.DestBlend = D3D10_BLEND_INV_SRC_ALPHA;
    blend_desc.BlendOp = D3D10_BLEND_OP_ADD;
    if (desc->pixelFormat.alphaMode == D2D1_ALPHA_MODE_IGNORE)
    {
        blend_desc.SrcBlendAlpha = D3D10_BLEND_ZERO;
        blend_desc.DestBlendAlpha = D3D10_BLEND_ONE;
    }
    else
    {
        blend_desc.SrcBlendAlpha = D3D10_BLEND_ONE;
        blend_desc.DestBlendAlpha = D3D10_BLEND_INV_SRC_ALPHA;
    }
    blend_desc.BlendOpAlpha = D3D10_BLEND_OP_ADD;
    blend_desc.RenderTargetWriteMask[0] = D3D10_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(hr = ID3D10Device_CreateBlendState(render_target->device, &blend_desc, &render_target->bs)))
    {
        WARN("Failed to create blend state, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&dwrite_factory)))
    {
        ERR("Failed to create dwrite factory, hr %#x.\n", hr);
        goto err;
    }

    hr = IDWriteFactory_CreateRenderingParams(dwrite_factory, &render_target->default_text_rendering_params);
    IDWriteFactory_Release(dwrite_factory);
    if (FAILED(hr))
    {
        ERR("Failed to create default text rendering parameters, hr %#x.\n", hr);
        goto err;
    }

    if (FAILED(hr = IDXGISurface_GetDesc(surface, &surface_desc)))
    {
        WARN("Failed to get surface desc, hr %#x.\n", hr);
        goto err;
    }

    render_target->desc.pixelFormat = desc->pixelFormat;
    render_target->pixel_size.width = surface_desc.Width;
    render_target->pixel_size.height = surface_desc.Height;
    render_target->drawing_state.transform = identity;

    if (!d2d_clip_stack_init(&render_target->clip_stack))
    {
        WARN("Failed to initialize clip stack.\n");
        hr = E_FAIL;
        goto err;
    }

    render_target->desc.dpiX = dpi_x;
    render_target->desc.dpiY = dpi_y;

    return S_OK;

err:
    if (render_target->default_text_rendering_params)
        IDWriteRenderingParams_Release(render_target->default_text_rendering_params);
    if (render_target->bs)
        ID3D10BlendState_Release(render_target->bs);
    if (render_target->rs)
        ID3D10RasterizerState_Release(render_target->rs);
    if (render_target->vb)
        ID3D10Buffer_Release(render_target->vb);
    if (render_target->ib)
        ID3D10Buffer_Release(render_target->ib);
    for (i = 0; i < D2D_SHAPE_TYPE_COUNT; ++i)
    {
        for (j = 0; j < D2D_BRUSH_TYPE_COUNT; ++j)
        {
            for (k = 0; k < D2D_BRUSH_TYPE_COUNT + 1; ++k)
            {
                if (render_target->shape_resources[i].ps[j][k])
                    ID3D10PixelShader_Release(render_target->shape_resources[i].ps[j][k]);
            }
        }
        if (render_target->shape_resources[i].vs)
            ID3D10VertexShader_Release(render_target->shape_resources[i].vs);
        if (render_target->shape_resources[i].il)
            ID3D10InputLayout_Release(render_target->shape_resources[i].il);
    }
    if (render_target->stateblock)
        render_target->stateblock->lpVtbl->Release(render_target->stateblock);
    if (render_target->view)
        ID3D10RenderTargetView_Release(render_target->view);
    if (render_target->device)
        ID3D10Device_Release(render_target->device);
    ID2D1Factory_Release(render_target->factory);
    return hr;
}

HRESULT d2d_d3d_create_render_target(ID2D1Factory *factory, IDXGISurface *surface, IUnknown *outer_unknown,
        const D2D1_RENDER_TARGET_PROPERTIES *desc, ID2D1RenderTarget **render_target)
{
    struct d2d_d3d_render_target *object;
    HRESULT hr;

    if (!(object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d2d_d3d_render_target_init(object, factory, surface, outer_unknown, desc)))
    {
        WARN("Failed to initialize render target, hr %#x.\n", hr);
        HeapFree(GetProcessHeap(), 0, object);
        return hr;
    }

    TRACE("Created render target %p.\n", object);
    *render_target = &object->ID2D1RenderTarget_iface;

    return S_OK;
}

HRESULT d2d_d3d_render_target_create_rtv(ID2D1RenderTarget *iface, IDXGISurface1 *surface)
{
    struct d2d_d3d_render_target *render_target = impl_from_ID2D1RenderTarget(iface);
    DXGI_SURFACE_DESC surface_desc;
    ID3D10RenderTargetView *view;
    ID3D10Resource *resource;
    HRESULT hr;

    if (!surface)
    {
        ID3D10RenderTargetView_Release(render_target->view);
        render_target->view = NULL;
        return S_OK;
    }

    if (FAILED(hr = IDXGISurface1_GetDesc(surface, &surface_desc)))
    {
        WARN("Failed to get surface desc, hr %#x.\n", hr);
        return hr;
    }

    if (FAILED(hr = IDXGISurface1_QueryInterface(surface, &IID_ID3D10Resource, (void **)&resource)))
    {
        WARN("Failed to get ID3D10Resource interface, hr %#x.\n", hr);
        return hr;
    }

    hr = ID3D10Device_CreateRenderTargetView(render_target->device, resource, NULL, &view);
    ID3D10Resource_Release(resource);
    if (FAILED(hr))
    {
        WARN("Failed to create rendertarget view, hr %#x.\n", hr);
        return hr;
    }

    render_target->pixel_size.width = surface_desc.Width;
    render_target->pixel_size.height = surface_desc.Height;
    if (render_target->view)
        ID3D10RenderTargetView_Release(render_target->view);
    render_target->view = view;

    return S_OK;
}
