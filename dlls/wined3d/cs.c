/*
 * Copyright 2013 Henri Verbeet for CodeWeavers
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
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

#if !defined(STAGING_CSMT)
#define WINED3D_INITIAL_CS_SIZE 4096

enum wined3d_cs_op
{
#else  /* STAGING_CSMT */
enum wined3d_cs_op
{
    WINED3D_CS_OP_NOP,
    WINED3D_CS_OP_SKIP,
    WINED3D_CS_OP_FENCE,
#endif /* STAGING_CSMT */
    WINED3D_CS_OP_PRESENT,
    WINED3D_CS_OP_CLEAR,
    WINED3D_CS_OP_DRAW,
    WINED3D_CS_OP_SET_PREDICATION,
    WINED3D_CS_OP_SET_VIEWPORT,
    WINED3D_CS_OP_SET_SCISSOR_RECT,
    WINED3D_CS_OP_SET_RENDERTARGET_VIEW,
    WINED3D_CS_OP_SET_DEPTH_STENCIL_VIEW,
    WINED3D_CS_OP_SET_VERTEX_DECLARATION,
    WINED3D_CS_OP_SET_STREAM_SOURCE,
    WINED3D_CS_OP_SET_STREAM_SOURCE_FREQ,
    WINED3D_CS_OP_SET_STREAM_OUTPUT,
    WINED3D_CS_OP_SET_INDEX_BUFFER,
    WINED3D_CS_OP_SET_CONSTANT_BUFFER,
    WINED3D_CS_OP_SET_TEXTURE,
    WINED3D_CS_OP_SET_SHADER_RESOURCE_VIEW,
    WINED3D_CS_OP_SET_UNORDERED_ACCESS_VIEW,
    WINED3D_CS_OP_SET_SAMPLER,
    WINED3D_CS_OP_SET_SHADER,
    WINED3D_CS_OP_SET_RASTERIZER_STATE,
    WINED3D_CS_OP_SET_RENDER_STATE,
    WINED3D_CS_OP_SET_TEXTURE_STATE,
    WINED3D_CS_OP_SET_SAMPLER_STATE,
    WINED3D_CS_OP_SET_TRANSFORM,
    WINED3D_CS_OP_SET_CLIP_PLANE,
    WINED3D_CS_OP_SET_COLOR_KEY,
    WINED3D_CS_OP_SET_MATERIAL,
    WINED3D_CS_OP_RESET_STATE,
    WINED3D_CS_OP_DESTROY_OBJECT,
    WINED3D_CS_OP_QUERY_ISSUE,
    WINED3D_CS_OP_PRELOAD_RESOURCE,
    WINED3D_CS_OP_UNLOAD_RESOURCE,
#if !defined(STAGING_CSMT)
    WINED3D_CS_OP_MAP,
    WINED3D_CS_OP_UNMAP,
#else  /* STAGING_CSMT */
    WINED3D_CS_OP_SET_VS_CONSTS_F,
    WINED3D_CS_OP_SET_VS_CONSTS_B,
    WINED3D_CS_OP_SET_VS_CONSTS_I,
    WINED3D_CS_OP_SET_PS_CONSTS_F,
    WINED3D_CS_OP_SET_PS_CONSTS_B,
    WINED3D_CS_OP_SET_PS_CONSTS_I,
    WINED3D_CS_OP_GLFINISH,
    WINED3D_CS_OP_SET_BASE_VERTEX_INDEX,
    WINED3D_CS_OP_SET_PRIMITIVE_TYPE,
    WINED3D_CS_OP_SET_LIGHT,
    WINED3D_CS_OP_SET_LIGHT_ENABLE,
    WINED3D_CS_OP_BLT,
    WINED3D_CS_OP_CLEAR_RTV,
    WINED3D_CS_OP_TEXTURE_CHANGED,
    WINED3D_CS_OP_TEXTURE_MAP,
    WINED3D_CS_OP_TEXTURE_UNMAP,
    WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION,
    WINED3D_CS_OP_BUFFER_SWAP_MEM,
    WINED3D_CS_OP_BUFFER_INVALIDATE_RANGE,
    WINED3D_CS_OP_UPDATE_TEXTURE,
    WINED3D_CS_OP_UPDATE_SUB_RESOURCE,
    WINED3D_CS_OP_CREATE_VBO,
    WINED3D_CS_OP_SAMPLER_INIT,
    WINED3D_CS_OP_GET_DC,
    WINED3D_CS_OP_RELEASE_DC,
    WINED3D_CS_OP_CREATE_DUMMY_TEXTURES,
    WINED3D_CS_OP_CREATE_SWAPCHAIN_CONTEXT,
    WINED3D_CS_OP_DELETE_GL_CONTEXTS,
    WINED3D_CS_OP_UPDATE_SWAP_INTERVAL,
    WINED3D_CS_OP_STOP,
};

struct wined3d_cs_stop
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_fence
{
    enum wined3d_cs_op opcode;
    BOOL *signalled;
#endif /* STAGING_CSMT */
};

struct wined3d_cs_present
{
    enum wined3d_cs_op opcode;
    HWND dst_window_override;
    struct wined3d_swapchain *swapchain;
    RECT src_rect;
    RECT dst_rect;
    DWORD flags;
};

struct wined3d_cs_clear
{
    enum wined3d_cs_op opcode;
    DWORD flags;
    struct wined3d_color color;
    float depth;
    DWORD stencil;
    unsigned int rect_count;
    RECT rects[1];
};

struct wined3d_cs_draw
{
    enum wined3d_cs_op opcode;
    int base_vertex_idx;
    unsigned int start_idx;
    unsigned int index_count;
    unsigned int start_instance;
    unsigned int instance_count;
    BOOL indexed;
};

struct wined3d_cs_set_predication
{
    enum wined3d_cs_op opcode;
    struct wined3d_query *predicate;
    BOOL value;
};

struct wined3d_cs_set_viewport
{
    enum wined3d_cs_op opcode;
    struct wined3d_viewport viewport;
};

struct wined3d_cs_set_scissor_rect
{
    enum wined3d_cs_op opcode;
    RECT rect;
};

struct wined3d_cs_set_rendertarget_view
{
    enum wined3d_cs_op opcode;
    unsigned int view_idx;
    struct wined3d_rendertarget_view *view;
};

struct wined3d_cs_set_depth_stencil_view
{
    enum wined3d_cs_op opcode;
    struct wined3d_rendertarget_view *view;
};

struct wined3d_cs_set_vertex_declaration
{
    enum wined3d_cs_op opcode;
    struct wined3d_vertex_declaration *declaration;
};

struct wined3d_cs_set_stream_source
{
    enum wined3d_cs_op opcode;
    UINT stream_idx;
    struct wined3d_buffer *buffer;
    UINT offset;
    UINT stride;
};

struct wined3d_cs_set_stream_source_freq
{
    enum wined3d_cs_op opcode;
    UINT stream_idx;
    UINT frequency;
    UINT flags;
};

struct wined3d_cs_set_stream_output
{
    enum wined3d_cs_op opcode;
    UINT stream_idx;
    struct wined3d_buffer *buffer;
    UINT offset;
};

struct wined3d_cs_set_index_buffer
{
    enum wined3d_cs_op opcode;
    struct wined3d_buffer *buffer;
    enum wined3d_format_id format_id;
    unsigned int offset;
};

struct wined3d_cs_set_constant_buffer
{
    enum wined3d_cs_op opcode;
    enum wined3d_shader_type type;
    UINT cb_idx;
    struct wined3d_buffer *buffer;
};

struct wined3d_cs_set_texture
{
    enum wined3d_cs_op opcode;
    UINT stage;
    struct wined3d_texture *texture;
};

struct wined3d_cs_set_color_key
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    WORD flags;
    WORD set;
    struct wined3d_color_key color_key;
};

struct wined3d_cs_set_shader_resource_view
{
    enum wined3d_cs_op opcode;
    enum wined3d_shader_type type;
    UINT view_idx;
    struct wined3d_shader_resource_view *view;
};

struct wined3d_cs_set_unordered_access_view
{
    enum wined3d_cs_op opcode;
    unsigned int view_idx;
    struct wined3d_unordered_access_view *view;
};

struct wined3d_cs_set_sampler
{
    enum wined3d_cs_op opcode;
    enum wined3d_shader_type type;
    UINT sampler_idx;
    struct wined3d_sampler *sampler;
};

struct wined3d_cs_set_shader
{
    enum wined3d_cs_op opcode;
    enum wined3d_shader_type type;
    struct wined3d_shader *shader;
};

struct wined3d_cs_set_rasterizer_state
{
    enum wined3d_cs_op opcode;
    struct wined3d_rasterizer_state *state;
};

struct wined3d_cs_set_render_state
{
    enum wined3d_cs_op opcode;
    enum wined3d_render_state state;
    DWORD value;
};

struct wined3d_cs_set_texture_state
{
    enum wined3d_cs_op opcode;
    UINT stage;
    enum wined3d_texture_stage_state state;
    DWORD value;
};

struct wined3d_cs_set_sampler_state
{
    enum wined3d_cs_op opcode;
    UINT sampler_idx;
    enum wined3d_sampler_state state;
    DWORD value;
};

struct wined3d_cs_set_transform
{
    enum wined3d_cs_op opcode;
    enum wined3d_transform_state state;
    struct wined3d_matrix matrix;
};

struct wined3d_cs_set_clip_plane
{
    enum wined3d_cs_op opcode;
    UINT plane_idx;
    struct wined3d_vec4 plane;
};

struct wined3d_cs_set_material
{
    enum wined3d_cs_op opcode;
    struct wined3d_material material;
};

struct wined3d_cs_reset_state
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_destroy_object
{
    enum wined3d_cs_op opcode;
    void (*callback)(void *object);
    void *object;
};

struct wined3d_cs_query_issue
{
    enum wined3d_cs_op opcode;
    struct wined3d_query *query;
    DWORD flags;
};

struct wined3d_cs_preload_resource
{
    enum wined3d_cs_op opcode;
    struct wined3d_resource *resource;
};

struct wined3d_cs_unload_resource
{
    enum wined3d_cs_op opcode;
    struct wined3d_resource *resource;
};

#if !defined(STAGING_CSMT)
struct wined3d_cs_map
{
    enum wined3d_cs_op opcode;
    struct wined3d_resource *resource;
    unsigned int sub_resource_idx;
    struct wined3d_map_desc *map_desc;
    const struct wined3d_box *box;
    DWORD flags;
    HRESULT *hr;
};

struct wined3d_cs_unmap
#else  /* STAGING_CSMT */
struct wined3d_cs_set_consts_f
{
    enum wined3d_cs_op opcode;
    unsigned int start_idx;
    unsigned int count;
    struct wined3d_vec4 constants[1];
};

struct wined3d_cs_set_consts_b
{
    enum wined3d_cs_op opcode;
    unsigned int start_idx;
    unsigned int count;
    BOOL constants[1];
};

struct wined3d_cs_set_consts_i
{
    enum wined3d_cs_op opcode;
    unsigned int start_idx;
    unsigned int count;
    struct wined3d_ivec4 constants[1];
};

struct wined3d_cs_finish
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_set_base_vertex_index
{
    enum wined3d_cs_op opcode;
    UINT base_vertex_index;
};

struct wined3d_cs_set_primitive_type
{
    enum wined3d_cs_op opcode;
    GLenum gl_primitive_type;
};

struct wined3d_cs_set_light
{
    enum wined3d_cs_op opcode;
    struct wined3d_light_info light;
};

struct wined3d_cs_set_light_enable
{
    enum wined3d_cs_op opcode;
    UINT idx;
    BOOL enable;
};

struct wined3d_cs_blt
{
    enum wined3d_cs_op opcode;
    struct wined3d_surface *dst_surface;
    RECT dst_rect;
    struct wined3d_surface *src_surface;
    RECT src_rect;
    DWORD flags;
    struct wined3d_blt_fx fx;
    enum wined3d_texture_filter_type filter;
};

struct wined3d_cs_clear_rtv
{
    enum wined3d_cs_op opcode;
    struct wined3d_rendertarget_view *view;
    RECT rect;
    DWORD flags;
    struct wined3d_color color;
    float depth;
    DWORD stencil;
    const struct blit_shader *blitter;
};

struct wined3d_cs_texture_map
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
    DWORD flags;
    void **mem;
};

struct wined3d_cs_texture_unmap
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
};

struct wined3d_cs_texture_add_dirty_region
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
};

struct wined3d_cs_texture_changed
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
    struct wined3d_gl_bo *swap_buffer;
    void *swap_heap_memory;
};

struct wined3d_cs_buffer_swap_mem
{
    enum wined3d_cs_op opcode;
    struct wined3d_buffer *buffer;
    BYTE *mem;
};

struct wined3d_cs_buffer_invalidate_bo_range
{
    enum wined3d_cs_op opcode;
    struct wined3d_buffer *buffer;
    unsigned int offset, size;
};

struct wined3d_cs_skip
{
    enum wined3d_cs_op opcode;
    DWORD size;
};

struct wined3d_cs_update_texture
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *src, *dst;
};

struct wined3d_cs_update_sub_resource
#endif /* STAGING_CSMT */
{
    enum wined3d_cs_op opcode;
    struct wined3d_resource *resource;
#if !defined(STAGING_CSMT)
    unsigned int sub_resource_idx;
    HRESULT *hr;
};

static void wined3d_cs_exec_present(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    unsigned int sub_resource_idx, row_pitch, depth_pitch;
    const struct wined3d_box *box;
    const void *data;
};

struct wined3d_cs_create_vbo
{
    enum wined3d_cs_op opcode;
    struct wined3d_buffer *buffer;
};

struct wined3d_cs_sampler_init
{
    enum wined3d_cs_op opcode;
    struct wined3d_sampler *sampler;
};

struct wined3d_cs_get_release_dc
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
};

struct wined3d_cs_create_dummy_textures
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_create_swapchain_context
{
    enum wined3d_cs_op opcode;
    struct wined3d_swapchain *swapchain;
    HRESULT *ret;
};

struct wined3d_cs_delete_gl_contexts
{
    enum wined3d_cs_op opcode;
    struct wined3d_swapchain *swapchain;
};

struct wined3d_cs_update_swap_interval
{
    enum wined3d_cs_op opcode;
    struct wined3d_swapchain *swapchain;
};

static void wined3d_cs_mt_submit(struct wined3d_cs *cs, size_t size)
{
    LONG new_val = (cs->queue.head + size) & (WINED3D_CS_QUEUE_SIZE - 1);
    /* There is only one thread writing to queue.head, InterlockedExchange
     * is used for the memory barrier. */
    InterlockedExchange(&cs->queue.head, new_val);

    if (InterlockedCompareExchange(&cs->waiting_for_event, FALSE, TRUE))
        SetEvent(cs->event);
}

static void wined3d_cs_mt_submit_prio(struct wined3d_cs *cs, size_t size)
{
    LONG new_val = (cs->prio_queue.head + size) & (WINED3D_CS_QUEUE_SIZE - 1);
    /* There is only one thread writing to queue.head, InterlockedExchange
     * is used for the memory barrier. */
    InterlockedExchange(&cs->prio_queue.head, new_val);

    if (InterlockedCompareExchange(&cs->waiting_for_event, FALSE, TRUE))
        SetEvent(cs->event);
}

static UINT wined3d_cs_exec_nop(struct wined3d_cs *cs, const void *data)
{
    return sizeof(enum wined3d_cs_op);
}

static UINT wined3d_cs_exec_skip(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_skip *op = data;

    return op->size;
}

static UINT wined3d_cs_exec_fence(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_fence *op = data;

    InterlockedExchange(op->signalled, TRUE);

    return sizeof(*op);
}

static void wined3d_cs_emit_fence(struct wined3d_cs *cs, BOOL *signalled)
{
    struct wined3d_cs_fence *op;

    *signalled = FALSE;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_FENCE;
    op->signalled = signalled;
    cs->ops->submit(cs, sizeof(*op));
}

static void wined3d_cs_emit_fence_prio(struct wined3d_cs *cs, BOOL *signalled)
{
    struct wined3d_cs_fence *op;

    *signalled = FALSE;

    op = cs->ops->require_space_prio(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_FENCE;
    op->signalled = signalled;
    cs->ops->submit_prio(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_present(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_present *op = data;
    struct wined3d_swapchain *swapchain;
    unsigned int i;

    swapchain = op->swapchain;
    wined3d_swapchain_set_window(swapchain, op->dst_window_override);

#if !defined(STAGING_CSMT)
    swapchain->swapchain_ops->swapchain_present(swapchain, &op->src_rect, &op->dst_rect, op->flags);
#else  /* STAGING_CSMT */
    swapchain->swapchain_ops->swapchain_present(swapchain, &op->src_rect, &op->dst_rect, op->flags,
            cs->state.fb.depth_stencil);

    InterlockedDecrement(&cs->pending_presents);
#endif /* STAGING_CSMT */

    wined3d_resource_release(&swapchain->front_buffer->resource);
    for (i = 0; i < swapchain->desc.backbuffer_count; ++i)
    {
        wined3d_resource_release(&swapchain->back_buffers[i]->resource);
    }
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_present(struct wined3d_cs *cs, struct wined3d_swapchain *swapchain,
        const RECT *src_rect, const RECT *dst_rect, HWND dst_window_override, DWORD flags)
{
    struct wined3d_cs_present *op;
    unsigned int i;
#if defined(STAGING_CSMT)
    LONG pending;
#endif /* STAGING_CSMT */

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_PRESENT;
    op->dst_window_override = dst_window_override;
    op->swapchain = swapchain;
    op->src_rect = *src_rect;
    op->dst_rect = *dst_rect;
    op->flags = flags;

    wined3d_resource_acquire(&swapchain->front_buffer->resource);
    for (i = 0; i < swapchain->desc.backbuffer_count; ++i)
    {
        wined3d_resource_acquire(&swapchain->back_buffers[i]->resource);
    }

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_clear(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    pending = InterlockedIncrement(&cs->pending_presents);

    cs->ops->submit(cs, sizeof(*op));

    /* D3D10 documentation suggests that Windows allows the game to run
     * 3 frames ahead of the GPU. Increasing this above 1 causes uneven
     * animation in some games, most notably StarCraft II. The framerates
     * don't show this problem. The issue is more noticable with vsync
     * on, but also happens with vsync off.
     *
     * In Counter-Strike: Source a frame difference of 3 causes noticable
     * input delay that makes the game unplayable. */
    while (pending > 1)
        pending = InterlockedCompareExchange(&cs->pending_presents, 0, 0);
}

static UINT wined3d_cs_exec_clear(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_clear *op = data;
    const struct wined3d_state *state;
    struct wined3d_device *device;
    unsigned int i;
    RECT draw_rect;
#if !defined(STAGING_CSMT)

    device = cs->device;
    state = &device->state;
    wined3d_get_draw_rect(state, &draw_rect);
    device_clear_render_targets(device, device->adapter->gl_info.limits.buffers,
            &device->fb, op->rect_count, op->rects, &draw_rect, op->flags,
#else  /* STAGING_CSMT */
    size_t size = FIELD_OFFSET(struct wined3d_cs_clear, rects[op->rect_count]);

    device = cs->device;
    state = &cs->state;
    wined3d_get_draw_rect(state, &draw_rect);
    device_clear_render_targets(device, device->adapter->gl_info.limits.buffers,
            &cs->state.fb, op->rect_count, op->rects, &draw_rect, op->flags,
#endif /* STAGING_CSMT */
            &op->color, op->depth, op->stencil);

    if (op->flags & WINED3DCLEAR_TARGET)
    {
        for (i = 0; i < device->adapter->gl_info.limits.buffers; ++i)
        {
#if !defined(STAGING_CSMT)
            if (state->fb->render_targets[i])
                wined3d_resource_release(state->fb->render_targets[i]->resource);
#else  /* STAGING_CSMT */
            if (state->fb.render_targets[i])
                wined3d_resource_release(state->fb.render_targets[i]->resource);
#endif /* STAGING_CSMT */
        }
    }
    if (op->flags & (WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL))
#if !defined(STAGING_CSMT)
        wined3d_resource_release(state->fb->depth_stencil->resource);
#else  /* STAGING_CSMT */
        wined3d_resource_release(state->fb.depth_stencil->resource);

    return size;
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_clear(struct wined3d_cs *cs, DWORD rect_count, const RECT *rects,
        DWORD flags, const struct wined3d_color *color, float depth, DWORD stencil)
{
    const struct wined3d_state *state = &cs->device->state;
    struct wined3d_cs_clear *op;
    unsigned int i;
#if !defined(STAGING_CSMT)

    op = cs->ops->require_space(cs, FIELD_OFFSET(struct wined3d_cs_clear, rects[rect_count]));
#else  /* STAGING_CSMT */
    size_t size = FIELD_OFFSET(struct wined3d_cs_clear, rects[rect_count]);
    op = cs->ops->require_space(cs, size);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_CLEAR;
    op->flags = flags;
    op->color = *color;
    op->depth = depth;
    op->stencil = stencil;
    op->rect_count = rect_count;
    memcpy(op->rects, rects, sizeof(*rects) * rect_count);

    if (flags & WINED3DCLEAR_TARGET)
    {
        for (i = 0; i < cs->device->adapter->gl_info.limits.buffers; ++i)
        {
#if !defined(STAGING_CSMT)
            if (state->fb->render_targets[i])
                wined3d_resource_acquire(state->fb->render_targets[i]->resource);
#else  /* STAGING_CSMT */
            if (state->fb.render_targets[i])
                wined3d_resource_acquire(state->fb.render_targets[i]->resource);
#endif /* STAGING_CSMT */
        }
    }
    if (flags & (WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL))
#if !defined(STAGING_CSMT)
        wined3d_resource_acquire(state->fb->depth_stencil->resource);

    cs->ops->submit(cs);
}

static void wined3d_cs_exec_draw(struct wined3d_cs *cs, const void *data)
{
    struct wined3d_state *state = &cs->device->state;
#else  /* STAGING_CSMT */
        wined3d_resource_acquire(state->fb.depth_stencil->resource);

    cs->ops->submit(cs, size);
}

static inline BOOL wined3d_cs_colorwrite_enabled(const struct wined3d_state *state, unsigned int i)
{
    switch (i)
    {
        case 0:
            return !!state->render_states[WINED3D_RS_COLORWRITEENABLE];
        case 1:
            return !!state->render_states[WINED3D_RS_COLORWRITEENABLE1];
        case 2:
            return !!state->render_states[WINED3D_RS_COLORWRITEENABLE2];
        case 3:
            return !!state->render_states[WINED3D_RS_COLORWRITEENABLE3];
        default:
            ERR("Unexpected color target %u.\n", i);
            return TRUE;
    }
}

static inline BOOL wined3d_cs_depth_stencil_enabled(const struct wined3d_state *state)
{
    return state->render_states[WINED3D_RS_ZENABLE]
            || state->render_states[WINED3D_RS_STENCILENABLE];
}

static UINT wined3d_cs_exec_draw(struct wined3d_cs *cs, const void *data)
{
    struct wined3d_state *state = &cs->state;
#endif /* STAGING_CSMT */
    struct wined3d_shader_sampler_map_entry *entry;
    struct wined3d_shader_resource_view *view;
    const struct wined3d_cs_draw *op = data;
#if defined(STAGING_CSMT)
    const struct wined3d_gl_info *gl_info = &cs->device->adapter->gl_info;
#endif /* STAGING_CSMT */
    struct wined3d_shader *shader;
    unsigned int i, j;

#if defined(STAGING_CSMT)
    if (op->indexed && !gl_info->supported[ARB_DRAW_ELEMENTS_BASE_VERTEX])
    {
        if (cs->state.load_base_vertex_index != cs->state.base_vertex_index)
        {
            cs->state.load_base_vertex_index = cs->state.base_vertex_index;
            device_invalidate_state(cs->device, STATE_BASEVERTEXINDEX);
        }
    }
    else if (cs->state.load_base_vertex_index)
    {
        cs->state.load_base_vertex_index = 0;
        device_invalidate_state(cs->device, STATE_BASEVERTEXINDEX);
    }

#endif /* STAGING_CSMT */
    if (!cs->device->adapter->gl_info.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX]
            && state->load_base_vertex_index != op->base_vertex_idx)
    {
        state->load_base_vertex_index = op->base_vertex_idx;
        device_invalidate_state(cs->device, STATE_BASEVERTEXINDEX);
    }

    draw_primitive(cs->device, state, op->base_vertex_idx, op->start_idx,
            op->index_count, op->start_instance, op->instance_count, op->indexed);

    if (op->indexed)
        wined3d_resource_release(&state->index_buffer->resource);
    for (i = 0; i < ARRAY_SIZE(state->streams); ++i)
    {
        if (state->streams[i].buffer)
            wined3d_resource_release(&state->streams[i].buffer->resource);
    }
    for (i = 0; i < ARRAY_SIZE(state->textures); ++i)
    {
        if (state->textures[i])
            wined3d_resource_release(&state->textures[i]->resource);
    }
    for (i = 0; i < cs->device->adapter->gl_info.limits.buffers; ++i)
    {
#if !defined(STAGING_CSMT)
        if (state->fb->render_targets[i])
            wined3d_resource_release(state->fb->render_targets[i]->resource);
    }
    if (state->fb->depth_stencil)
        wined3d_resource_release(state->fb->depth_stencil->resource);
#else  /* STAGING_CSMT */
        if (state->fb.render_targets[i] && wined3d_cs_colorwrite_enabled(state, i))
            wined3d_resource_release(state->fb.render_targets[i]->resource);
    }
    if (state->fb.depth_stencil && wined3d_cs_depth_stencil_enabled(state))
        wined3d_resource_release(state->fb.depth_stencil->resource);
#endif /* STAGING_CSMT */
    for (i = 0; i < WINED3D_SHADER_TYPE_COUNT; ++i)
    {
        if (!(shader = state->shader[i]))
            continue;

        for (j = 0; j < WINED3D_MAX_CBS; ++j)
        {
            if (state->cb[i][j])
                wined3d_resource_release(&state->cb[i][j]->resource);
        }

        for (j = 0; j < shader->reg_maps.sampler_map.count; ++j)
        {
            entry = &shader->reg_maps.sampler_map.entries[j];

            if (!(view = state->shader_resource_view[i][entry->resource_idx]))
                continue;

            wined3d_resource_release(view->resource);
        }
    }
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_draw(struct wined3d_cs *cs, int base_vertex_idx, unsigned int start_idx,
        unsigned int index_count, unsigned int start_instance, unsigned int instance_count, BOOL indexed)
{
    const struct wined3d_state *state = &cs->device->state;
    struct wined3d_shader_sampler_map_entry *entry;
    struct wined3d_shader_resource_view *view;
    struct wined3d_shader *shader;
    struct wined3d_cs_draw *op;
    unsigned int i, j;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_DRAW;
    op->base_vertex_idx = base_vertex_idx;
    op->start_idx = start_idx;
    op->index_count = index_count;
    op->start_instance = start_instance;
    op->instance_count = instance_count;
    op->indexed = indexed;

    if (indexed)
#if !defined(STAGING_CSMT)
        wined3d_resource_acquire(&state->index_buffer->resource);
#else  /* STAGING_CSMT */
    {
        wined3d_resource_acquire(&state->index_buffer->resource);
        state->index_buffer->ignore_discard = FALSE;
    }
#endif /* STAGING_CSMT */
    for (i = 0; i < ARRAY_SIZE(state->streams); ++i)
    {
        if (state->streams[i].buffer)
#if !defined(STAGING_CSMT)
            wined3d_resource_acquire(&state->streams[i].buffer->resource);
#else  /* STAGING_CSMT */
        {
            wined3d_resource_acquire(&state->streams[i].buffer->resource);
            state->streams[i].buffer->ignore_discard = FALSE;
        }
#endif /* STAGING_CSMT */
    }
    for (i = 0; i < ARRAY_SIZE(state->textures); ++i)
    {
        if (state->textures[i])
            wined3d_resource_acquire(&state->textures[i]->resource);
    }
    for (i = 0; i < cs->device->adapter->gl_info.limits.buffers; ++i)
    {
#if !defined(STAGING_CSMT)
        if (state->fb->render_targets[i])
            wined3d_resource_acquire(state->fb->render_targets[i]->resource);
    }
    if (state->fb->depth_stencil)
        wined3d_resource_acquire(state->fb->depth_stencil->resource);
#else  /* STAGING_CSMT */
        if (state->fb.render_targets[i] && wined3d_cs_colorwrite_enabled(state, i))
            wined3d_resource_acquire(state->fb.render_targets[i]->resource);
    }
    if (state->fb.depth_stencil && wined3d_cs_depth_stencil_enabled(state))
        wined3d_resource_acquire(state->fb.depth_stencil->resource);
#endif /* STAGING_CSMT */
    for (i = 0; i < WINED3D_SHADER_TYPE_COUNT; ++i)
    {
        if (!(shader = state->shader[i]))
            continue;

        for (j = 0; j < WINED3D_MAX_CBS; ++j)
        {
            if (state->cb[i][j])
                wined3d_resource_acquire(&state->cb[i][j]->resource);
        }

        for (j = 0; j < shader->reg_maps.sampler_map.count; ++j)
        {
            entry = &shader->reg_maps.sampler_map.entries[j];

            if (!(view = state->shader_resource_view[i][entry->resource_idx]))
                continue;

            wined3d_resource_acquire(view->resource);
        }
    }

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_predication(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_predication(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_predication *op = data;

    cs->state.predicate = op->predicate;
    cs->state.predicate_value = op->value;
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_predication(struct wined3d_cs *cs, struct wined3d_query *predicate, BOOL value)
{
    struct wined3d_cs_set_predication *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_PREDICATION;
    op->predicate = predicate;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_viewport(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_viewport(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_viewport *op = data;

    cs->state.viewport = op->viewport;
    device_invalidate_state(cs->device, STATE_VIEWPORT);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_viewport(struct wined3d_cs *cs, const struct wined3d_viewport *viewport)
{
    struct wined3d_cs_set_viewport *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_VIEWPORT;
    op->viewport = *viewport;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_scissor_rect(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_scissor_rect(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_scissor_rect *op = data;

    cs->state.scissor_rect = op->rect;
    device_invalidate_state(cs->device, STATE_SCISSORRECT);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_scissor_rect(struct wined3d_cs *cs, const RECT *rect)
{
    struct wined3d_cs_set_scissor_rect *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_SCISSOR_RECT;
    op->rect = *rect;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_rendertarget_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_rendertarget_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_rendertarget_view *op = data;

#if !defined(STAGING_CSMT)
    cs->state.fb->render_targets[op->view_idx] = op->view;
    device_invalidate_state(cs->device, STATE_FRAMEBUFFER);
#else  /* STAGING_CSMT */
    cs->state.fb.render_targets[op->view_idx] = op->view;
    device_invalidate_state(cs->device, STATE_FRAMEBUFFER);

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_rendertarget_view(struct wined3d_cs *cs, unsigned int view_idx,
        struct wined3d_rendertarget_view *view)
{
    struct wined3d_cs_set_rendertarget_view *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_RENDERTARGET_VIEW;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_depth_stencil_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_depth_stencil_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_depth_stencil_view *op = data;
    struct wined3d_device *device = cs->device;
    struct wined3d_rendertarget_view *prev;

#if !defined(STAGING_CSMT)
    if ((prev = cs->state.fb->depth_stencil))
#else  /* STAGING_CSMT */
    if ((prev = cs->state.fb.depth_stencil))
#endif /* STAGING_CSMT */
    {
        struct wined3d_surface *prev_surface = wined3d_rendertarget_view_get_surface(prev);

        if (prev_surface && (device->swapchains[0]->desc.flags & WINED3D_SWAPCHAIN_DISCARD_DEPTHSTENCIL
                || prev_surface->container->flags & WINED3D_TEXTURE_DISCARD))
        {
            surface_modify_ds_location(prev_surface, WINED3D_LOCATION_DISCARDED, prev->width, prev->height);
#if !defined(STAGING_CSMT)
            if (prev_surface == device->onscreen_depth_stencil)
            {
                wined3d_texture_decref(device->onscreen_depth_stencil->container);
                device->onscreen_depth_stencil = NULL;
#else  /* STAGING_CSMT */
            if (prev_surface == cs->onscreen_depth_stencil)
            {
                wined3d_texture_decref(cs->onscreen_depth_stencil->container);
                cs->onscreen_depth_stencil = NULL;
#endif /* STAGING_CSMT */
            }
        }
    }

#if !defined(STAGING_CSMT)
    cs->fb.depth_stencil = op->view;
#else  /* STAGING_CSMT */
    cs->state.fb.depth_stencil = op->view;
#endif /* STAGING_CSMT */

    if (!prev != !op->view)
    {
        /* Swapping NULL / non NULL depth stencil affects the depth and tests */
        device_invalidate_state(device, STATE_RENDER(WINED3D_RS_ZENABLE));
        device_invalidate_state(device, STATE_RENDER(WINED3D_RS_STENCILENABLE));
        device_invalidate_state(device, STATE_RENDER(WINED3D_RS_STENCILWRITEMASK));
        device_invalidate_state(device, STATE_RENDER(WINED3D_RS_DEPTHBIAS));
    }
    else if (prev && (prev->format_flags & WINED3DFMT_FLAG_FLOAT)
            != (op->view->format_flags & WINED3DFMT_FLAG_FLOAT))
    {
        device_invalidate_state(device, STATE_RENDER(WINED3D_RS_DEPTHBIAS));
    }

    device_invalidate_state(device, STATE_FRAMEBUFFER);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_depth_stencil_view(struct wined3d_cs *cs, struct wined3d_rendertarget_view *view)
{
    struct wined3d_cs_set_depth_stencil_view *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_DEPTH_STENCIL_VIEW;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_vertex_declaration(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_vertex_declaration(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_vertex_declaration *op = data;

    cs->state.vertex_declaration = op->declaration;
    device_invalidate_state(cs->device, STATE_VDECL);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_vertex_declaration(struct wined3d_cs *cs, struct wined3d_vertex_declaration *declaration)
{
    struct wined3d_cs_set_vertex_declaration *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_VERTEX_DECLARATION;
    op->declaration = declaration;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_stream_source(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_stream_source(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_stream_source *op = data;
    struct wined3d_stream_state *stream;
    struct wined3d_buffer *prev;

    stream = &cs->state.streams[op->stream_idx];
    prev = stream->buffer;
    stream->buffer = op->buffer;
    stream->offset = op->offset;
    stream->stride = op->stride;

    if (op->buffer)
        InterlockedIncrement(&op->buffer->resource.bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource.bind_count);

    device_invalidate_state(cs->device, STATE_STREAMSRC);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_stream_source(struct wined3d_cs *cs, UINT stream_idx,
        struct wined3d_buffer *buffer, UINT offset, UINT stride)
{
    struct wined3d_cs_set_stream_source *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_STREAM_SOURCE;
    op->stream_idx = stream_idx;
    op->buffer = buffer;
    op->offset = offset;
    op->stride = stride;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_stream_source_freq(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_stream_source_freq(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_stream_source_freq *op = data;
    struct wined3d_stream_state *stream;

    stream = &cs->state.streams[op->stream_idx];
    stream->frequency = op->frequency;
    stream->flags = op->flags;

    device_invalidate_state(cs->device, STATE_STREAMSRC);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_stream_source_freq(struct wined3d_cs *cs, UINT stream_idx, UINT frequency, UINT flags)
{
    struct wined3d_cs_set_stream_source_freq *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_STREAM_SOURCE_FREQ;
    op->stream_idx = stream_idx;
    op->frequency = frequency;
    op->flags = flags;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_stream_output(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_stream_output(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_stream_output *op = data;
    struct wined3d_stream_output *stream;
    struct wined3d_buffer *prev;

    stream = &cs->state.stream_output[op->stream_idx];
    prev = stream->buffer;
    stream->buffer = op->buffer;
    stream->offset = op->offset;

    if (op->buffer)
        InterlockedIncrement(&op->buffer->resource.bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource.bind_count);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_stream_output(struct wined3d_cs *cs, UINT stream_idx,
        struct wined3d_buffer *buffer, UINT offset)
{
    struct wined3d_cs_set_stream_output *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_STREAM_OUTPUT;
    op->stream_idx = stream_idx;
    op->buffer = buffer;
    op->offset = offset;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_index_buffer(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_index_buffer(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_index_buffer *op = data;
    struct wined3d_buffer *prev;

    prev = cs->state.index_buffer;
    cs->state.index_buffer = op->buffer;
    cs->state.index_format = op->format_id;
    cs->state.index_offset = op->offset;

    if (op->buffer)
        InterlockedIncrement(&op->buffer->resource.bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource.bind_count);

    device_invalidate_state(cs->device, STATE_INDEXBUFFER);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_index_buffer(struct wined3d_cs *cs, struct wined3d_buffer *buffer,
        enum wined3d_format_id format_id, unsigned int offset)
{
    struct wined3d_cs_set_index_buffer *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_INDEX_BUFFER;
    op->buffer = buffer;
    op->format_id = format_id;
    op->offset = offset;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_constant_buffer(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_constant_buffer(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_constant_buffer *op = data;
    struct wined3d_buffer *prev;

    prev = cs->state.cb[op->type][op->cb_idx];
    cs->state.cb[op->type][op->cb_idx] = op->buffer;

    if (op->buffer)
        InterlockedIncrement(&op->buffer->resource.bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource.bind_count);

    device_invalidate_state(cs->device, STATE_CONSTANT_BUFFER(op->type));
#if defined(STAGING_CSMT)
    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_constant_buffer(struct wined3d_cs *cs, enum wined3d_shader_type type,
        UINT cb_idx, struct wined3d_buffer *buffer)
{
    struct wined3d_cs_set_constant_buffer *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_CONSTANT_BUFFER;
    op->type = type;
    op->cb_idx = cb_idx;
    op->buffer = buffer;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_texture(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_texture(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_gl_info *gl_info = &cs->device->adapter->gl_info;
    const struct wined3d_d3d_info *d3d_info = &cs->device->adapter->d3d_info;
    const struct wined3d_cs_set_texture *op = data;
    struct wined3d_texture *prev;
    BOOL old_use_color_key = FALSE, new_use_color_key = FALSE;

    prev = cs->state.textures[op->stage];
    cs->state.textures[op->stage] = op->texture;

    if (op->texture)
    {
        const struct wined3d_format *new_format = op->texture->resource.format;
        const struct wined3d_format *old_format = prev ? prev->resource.format : NULL;
        unsigned int old_fmt_flags = prev ? prev->resource.format_flags : 0;
        unsigned int new_fmt_flags = op->texture->resource.format_flags;

        if (InterlockedIncrement(&op->texture->resource.bind_count) == 1)
            op->texture->sampler = op->stage;

        if (!prev || op->texture->target != prev->target
                || (!is_same_fixup(new_format->color_fixup, old_format->color_fixup)
                && !(can_use_texture_swizzle(gl_info, new_format) && can_use_texture_swizzle(gl_info, old_format)))
                || (new_fmt_flags & WINED3DFMT_FLAG_SHADOW) != (old_fmt_flags & WINED3DFMT_FLAG_SHADOW))
            device_invalidate_state(cs->device, STATE_SHADER(WINED3D_SHADER_TYPE_PIXEL));

        if (!prev && op->stage < d3d_info->limits.ffp_blend_stages)
        {
            /* The source arguments for color and alpha ops have different
             * meanings when a NULL texture is bound, so the COLOR_OP and
             * ALPHA_OP have to be dirtified. */
            device_invalidate_state(cs->device, STATE_TEXTURESTAGE(op->stage, WINED3D_TSS_COLOR_OP));
            device_invalidate_state(cs->device, STATE_TEXTURESTAGE(op->stage, WINED3D_TSS_ALPHA_OP));
        }

        if (!op->stage && op->texture->async.color_key_flags & WINED3D_CKEY_SRC_BLT)
            new_use_color_key = TRUE;
    }

    if (prev)
    {
        if (InterlockedDecrement(&prev->resource.bind_count) && prev->sampler == op->stage)
        {
            unsigned int i;

            /* Search for other stages the texture is bound to. Shouldn't
             * happen if applications bind textures to a single stage only. */
            TRACE("Searching for other stages the texture is bound to.\n");
            for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
            {
                if (cs->state.textures[i] == prev)
                {
                    TRACE("Texture is also bound to stage %u.\n", i);
                    prev->sampler = i;
                    break;
                }
            }
        }

        if (!op->texture && op->stage < d3d_info->limits.ffp_blend_stages)
        {
            device_invalidate_state(cs->device, STATE_TEXTURESTAGE(op->stage, WINED3D_TSS_COLOR_OP));
            device_invalidate_state(cs->device, STATE_TEXTURESTAGE(op->stage, WINED3D_TSS_ALPHA_OP));
        }

        if (!op->stage && prev->async.color_key_flags & WINED3D_CKEY_SRC_BLT)
            old_use_color_key = TRUE;
    }

    device_invalidate_state(cs->device, STATE_SAMPLER(op->stage));

    if (new_use_color_key != old_use_color_key)
        device_invalidate_state(cs->device, STATE_RENDER(WINED3D_RS_COLORKEYENABLE));

    if (new_use_color_key)
        device_invalidate_state(cs->device, STATE_COLOR_KEY);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_texture(struct wined3d_cs *cs, UINT stage, struct wined3d_texture *texture)
{
    struct wined3d_cs_set_texture *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_TEXTURE;
    op->stage = stage;
    op->texture = texture;
#if !defined(STAGING_CSMT)

    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_shader_resource_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_shader_resource_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_shader_resource_view *op = data;

    cs->state.shader_resource_view[op->type][op->view_idx] = op->view;
    device_invalidate_state(cs->device, STATE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_unordered_access_view(struct wined3d_cs *cs, unsigned int view_idx,
        struct wined3d_unordered_access_view *view)
{
    struct wined3d_cs_set_unordered_access_view *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_UNORDERED_ACCESS_VIEW;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_unordered_access_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_unordered_access_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_unordered_access_view *op = data;
    struct wined3d_unordered_access_view *prev;

    prev = cs->state.unordered_access_view[op->view_idx];
    cs->state.unordered_access_view[op->view_idx] = op->view;

    if (op->view)
        InterlockedIncrement(&op->view->resource->bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource->bind_count);

    device_invalidate_state(cs->device, STATE_UNORDERED_ACCESS_VIEW_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_shader_resource_view(struct wined3d_cs *cs, enum wined3d_shader_type type,
        UINT view_idx, struct wined3d_shader_resource_view *view)
{
    struct wined3d_cs_set_shader_resource_view *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_SHADER_RESOURCE_VIEW;
    op->type = type;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_sampler(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_sampler(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_sampler *op = data;

    cs->state.sampler[op->type][op->sampler_idx] = op->sampler;
    device_invalidate_state(cs->device, STATE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_sampler(struct wined3d_cs *cs, enum wined3d_shader_type type,
        UINT sampler_idx, struct wined3d_sampler *sampler)
{
    struct wined3d_cs_set_sampler *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_SAMPLER;
    op->type = type;
    op->sampler_idx = sampler_idx;
    op->sampler = sampler;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_shader(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_shader(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_shader *op = data;

    cs->state.shader[op->type] = op->shader;
    device_invalidate_state(cs->device, STATE_SHADER(op->type));
    device_invalidate_state(cs->device, STATE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_shader(struct wined3d_cs *cs, enum wined3d_shader_type type, struct wined3d_shader *shader)
{
    struct wined3d_cs_set_shader *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_SHADER;
    op->type = type;
    op->shader = shader;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_rasterizer_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_rasterizer_state(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_rasterizer_state *op = data;

    cs->state.rasterizer_state = op->state;
    device_invalidate_state(cs->device, STATE_FRONTFACE);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_rasterizer_state(struct wined3d_cs *cs,
        struct wined3d_rasterizer_state *rasterizer_state)
{
    struct wined3d_cs_set_rasterizer_state *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_RASTERIZER_STATE;
    op->state = rasterizer_state;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_render_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_vs_consts_f(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_f *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.vs_consts_f[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device->shader_backend->shader_update_float_vertex_constants(device,
            op->start_idx, op->count);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

static UINT wined3d_cs_exec_set_ps_consts_f(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_f *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.ps_consts_f[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device->shader_backend->shader_update_float_pixel_constants(device,
            op->start_idx, op->count);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

void wined3d_cs_emit_set_consts_f(struct wined3d_cs *cs, unsigned int start_idx, unsigned int count,
        const struct wined3d_vec4 *constants, enum wined3d_shader_type type)
{
    struct wined3d_cs_set_consts_f *op;
    size_t size = sizeof(*op) + sizeof(op->constants[0]) * (count - 1);

    op = cs->ops->require_space(cs, size);
    switch (type)
    {
        case WINED3D_SHADER_TYPE_PIXEL:
            op->opcode = WINED3D_CS_OP_SET_PS_CONSTS_F;
            break;

        case WINED3D_SHADER_TYPE_VERTEX:
            op->opcode = WINED3D_CS_OP_SET_VS_CONSTS_F;
            break;

        case WINED3D_SHADER_TYPE_GEOMETRY:
        case WINED3D_SHADER_TYPE_HULL:
        case WINED3D_SHADER_TYPE_DOMAIN:
        case WINED3D_SHADER_TYPE_COMPUTE:
            FIXME("Invalid for geometry/compute shaders\n");
            return;

        case WINED3D_SHADER_TYPE_COUNT:
            break;
    }
    op->start_idx = start_idx;
    op->count = count;
    memcpy(op->constants, constants, sizeof(op->constants[0]) * count);

    cs->ops->submit(cs, size);
}

static UINT wined3d_cs_exec_set_render_state(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_render_state *op = data;

    cs->state.render_states[op->state] = op->value;
    device_invalidate_state(cs->device, STATE_RENDER(op->state));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_render_state(struct wined3d_cs *cs, enum wined3d_render_state state, DWORD value)
{
    struct wined3d_cs_set_render_state *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_RENDER_STATE;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_texture_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_vs_consts_b(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_b *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.vs_consts_b[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device_invalidate_shader_constants(device, WINED3D_SHADER_CONST_VS_B);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

static UINT wined3d_cs_exec_set_ps_consts_b(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_b *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.ps_consts_b[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device_invalidate_shader_constants(device, WINED3D_SHADER_CONST_PS_B);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

void wined3d_cs_emit_set_consts_b(struct wined3d_cs *cs, unsigned int start_idx,
        unsigned int count, const BOOL *constants, enum wined3d_shader_type type)
{
    struct wined3d_cs_set_consts_b *op;
    size_t size = sizeof(*op) + sizeof(op->constants[0]) * (count - 1);

    op = cs->ops->require_space(cs, size);
    switch (type)
    {
        case WINED3D_SHADER_TYPE_PIXEL:
            op->opcode = WINED3D_CS_OP_SET_PS_CONSTS_B;
            break;

        case WINED3D_SHADER_TYPE_VERTEX:
            op->opcode = WINED3D_CS_OP_SET_VS_CONSTS_B;
            break;

        case WINED3D_SHADER_TYPE_GEOMETRY:
        case WINED3D_SHADER_TYPE_DOMAIN:
        case WINED3D_SHADER_TYPE_HULL:
        case WINED3D_SHADER_TYPE_COMPUTE:
            FIXME("Invalid for geometry/compute shaders\n");
            return;

        case WINED3D_SHADER_TYPE_COUNT:
            break;
    }
    op->start_idx = start_idx;
    op->count = count;
    memcpy(op->constants, constants, sizeof(op->constants[0]) * count);

    cs->ops->submit(cs, size);
}

static UINT wined3d_cs_exec_set_vs_consts_i(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_i *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.vs_consts_i[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device_invalidate_shader_constants(device, WINED3D_SHADER_CONST_VS_I);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

static UINT wined3d_cs_exec_set_ps_consts_i(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_consts_i *op = data;
    struct wined3d_device *device = cs->device;

    memcpy(&cs->state.ps_consts_i[op->start_idx], op->constants, sizeof(op->constants[0]) * op->count);

    device_invalidate_shader_constants(device, WINED3D_SHADER_CONST_PS_I);

    return sizeof(*op) + sizeof(op->constants[0]) * (op->count - 1);
}

void wined3d_cs_emit_set_consts_i(struct wined3d_cs *cs, unsigned int start_idx, unsigned int count,
        const struct wined3d_ivec4 *constants, enum wined3d_shader_type type)
{
    struct wined3d_cs_set_consts_i *op;
    size_t size = sizeof(*op) + sizeof(op->constants[0]) * (count - 1);

    op = cs->ops->require_space(cs, size);
    switch (type)
    {
        case WINED3D_SHADER_TYPE_PIXEL:
            op->opcode = WINED3D_CS_OP_SET_PS_CONSTS_I;
            break;

        case WINED3D_SHADER_TYPE_VERTEX:
            op->opcode = WINED3D_CS_OP_SET_VS_CONSTS_I;
            break;

        case WINED3D_SHADER_TYPE_GEOMETRY:
        case WINED3D_SHADER_TYPE_DOMAIN:
        case WINED3D_SHADER_TYPE_HULL:
        case WINED3D_SHADER_TYPE_COMPUTE:
            FIXME("Invalid for geometry/compute shaders\n");
            return;

        case WINED3D_SHADER_TYPE_COUNT:
            break;
    }
    op->start_idx = start_idx;
    op->count = count;
    memcpy(op->constants, constants, sizeof(op->constants[0]) * count);

    cs->ops->submit(cs, size);
}

static UINT wined3d_cs_exec_set_texture_state(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_texture_state *op = data;

    cs->state.texture_states[op->stage][op->state] = op->value;
    device_invalidate_state(cs->device, STATE_TEXTURESTAGE(op->stage, op->state));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_texture_state(struct wined3d_cs *cs, UINT stage,
        enum wined3d_texture_stage_state state, DWORD value)
{
    struct wined3d_cs_set_texture_state *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_TEXTURE_STATE;
    op->stage = stage;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_sampler_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_sampler_state(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_sampler_state *op = data;

    cs->state.sampler_states[op->sampler_idx][op->state] = op->value;
    device_invalidate_state(cs->device, STATE_SAMPLER(op->sampler_idx));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_sampler_state(struct wined3d_cs *cs, UINT sampler_idx,
        enum wined3d_sampler_state state, DWORD value)
{
    struct wined3d_cs_set_sampler_state *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_SAMPLER_STATE;
    op->sampler_idx = sampler_idx;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_transform(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_transform(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_transform *op = data;

    cs->state.transforms[op->state] = op->matrix;
    if (op->state < WINED3D_TS_WORLD_MATRIX(cs->device->adapter->d3d_info.limits.ffp_vertex_blend_matrices))
        device_invalidate_state(cs->device, STATE_TRANSFORM(op->state));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_transform(struct wined3d_cs *cs, enum wined3d_transform_state state,
        const struct wined3d_matrix *matrix)
{
    struct wined3d_cs_set_transform *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_TRANSFORM;
    op->state = state;
    op->matrix = *matrix;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_clip_plane(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_clip_plane(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_clip_plane *op = data;

    cs->state.clip_planes[op->plane_idx] = op->plane;
    device_invalidate_state(cs->device, STATE_CLIPPLANE(op->plane_idx));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_clip_plane(struct wined3d_cs *cs, UINT plane_idx, const struct wined3d_vec4 *plane)
{
    struct wined3d_cs_set_clip_plane *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_CLIP_PLANE;
    op->plane_idx = plane_idx;
    op->plane = *plane;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_color_key(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_color_key(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_color_key *op = data;
    struct wined3d_texture *texture = op->texture;

    if (op->set)
    {
        switch (op->flags)
        {
            case WINED3D_CKEY_DST_BLT:
                texture->async.dst_blt_color_key = op->color_key;
                texture->async.color_key_flags |= WINED3D_CKEY_DST_BLT;
                break;

            case WINED3D_CKEY_DST_OVERLAY:
                texture->async.dst_overlay_color_key = op->color_key;
                texture->async.color_key_flags |= WINED3D_CKEY_DST_OVERLAY;
                break;

            case WINED3D_CKEY_SRC_BLT:
                if (texture == cs->state.textures[0])
                {
                    device_invalidate_state(cs->device, STATE_COLOR_KEY);
                    if (!(texture->async.color_key_flags & WINED3D_CKEY_SRC_BLT))
                        device_invalidate_state(cs->device, STATE_RENDER(WINED3D_RS_COLORKEYENABLE));
                }

                texture->async.src_blt_color_key = op->color_key;
                texture->async.color_key_flags |= WINED3D_CKEY_SRC_BLT;
                break;

            case WINED3D_CKEY_SRC_OVERLAY:
                texture->async.src_overlay_color_key = op->color_key;
                texture->async.color_key_flags |= WINED3D_CKEY_SRC_OVERLAY;
                break;
        }
    }
    else
    {
        switch (op->flags)
        {
            case WINED3D_CKEY_DST_BLT:
                texture->async.color_key_flags &= ~WINED3D_CKEY_DST_BLT;
                break;

            case WINED3D_CKEY_DST_OVERLAY:
                texture->async.color_key_flags &= ~WINED3D_CKEY_DST_OVERLAY;
                break;

            case WINED3D_CKEY_SRC_BLT:
                if (texture == cs->state.textures[0] && texture->async.color_key_flags & WINED3D_CKEY_SRC_BLT)
                    device_invalidate_state(cs->device, STATE_RENDER(WINED3D_RS_COLORKEYENABLE));

                texture->async.color_key_flags &= ~WINED3D_CKEY_SRC_BLT;
                break;

            case WINED3D_CKEY_SRC_OVERLAY:
                texture->async.color_key_flags &= ~WINED3D_CKEY_SRC_OVERLAY;
                break;
        }
    }
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_color_key(struct wined3d_cs *cs, struct wined3d_texture *texture,
        WORD flags, const struct wined3d_color_key *color_key)
{
    struct wined3d_cs_set_color_key *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_COLOR_KEY;
    op->texture = texture;
    op->flags = flags;
    if (color_key)
    {
        op->color_key = *color_key;
        op->set = 1;
    }
    else
        op->set = 0;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_material(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_material(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_material *op = data;

    cs->state.material = op->material;
    device_invalidate_state(cs->device, STATE_MATERIAL);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_material(struct wined3d_cs *cs, const struct wined3d_material *material)
{
    struct wined3d_cs_set_material *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_MATERIAL;
    op->material = *material;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_reset_state(struct wined3d_cs *cs, const void *data)
{
    struct wined3d_adapter *adapter = cs->device->adapter;
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_reset_state(struct wined3d_cs *cs, const void *data)
{
    struct wined3d_adapter *adapter = cs->device->adapter;
    HRESULT hr;
#endif /* STAGING_CSMT */

    state_cleanup(&cs->state);
    memset(&cs->state, 0, sizeof(cs->state));
#if !defined(STAGING_CSMT)
    state_init(&cs->state, &cs->fb, &adapter->gl_info, &adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT);
#else  /* STAGING_CSMT */
    if (FAILED(hr = state_init(&cs->state, &adapter->gl_info, &adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT)))
        ERR("Failed to initialize CS state, hr %#x.\n", hr);

    return sizeof(struct wined3d_cs_reset_state);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_reset_state(struct wined3d_cs *cs)
{
    struct wined3d_cs_reset_state *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_RESET_STATE;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_destroy_object(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_destroy_object(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_destroy_object *op = data;

    op->callback(op->object);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_destroy_object(struct wined3d_cs *cs, void (*callback)(void *object), void *object)
{
    struct wined3d_cs_destroy_object *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_DESTROY_OBJECT;
    op->callback = callback;
    op->object = object;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_query_issue(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_query_issue(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_query_issue *op = data;
    struct wined3d_query *query = op->query;
#if !defined(STAGING_CSMT)

    query->query_ops->query_issue(query, op->flags);
#else  /* STAGING_CSMT */
    BOOL poll;

    poll = query->query_ops->query_issue(query, op->flags);

    if (wined3d_settings.cs_multithreaded)
    {
        if (poll && list_empty(&query->poll_list_entry))
        {
            list_add_tail(&cs->query_poll_list, &query->poll_list_entry);
        }
        else if (op->flags & WINED3DISSUE_END)
        {
            /* Can happen when an occlusion query is ended without being started,
             * in which case we don't want to poll, but still have to counter-balance
             * the increment of the main counter (!poll && list_empty).
             *
             * This can also happen if an event query is re-issued before the first
             * fence was reached (poll && !list_empty). In this case the query is
             * already in the list and the poll function will check the new fence.
             * We have to counter-balance the discarded increment. */
            InterlockedIncrement(&query->counter_retrieved);
        }
    }

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_query_issue(struct wined3d_cs *cs, struct wined3d_query *query, DWORD flags)
{
    struct wined3d_cs_query_issue *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_QUERY_ISSUE;
    op->query = query;
    op->flags = flags;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_preload_resource(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_preload_resource(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_preload_resource *op = data;
    struct wined3d_resource *resource = op->resource;

    resource->resource_ops->resource_preload(resource);
    wined3d_resource_release(resource);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_preload_resource(struct wined3d_cs *cs, struct wined3d_resource *resource)
{
    struct wined3d_cs_preload_resource *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_PRELOAD_RESOURCE;
    op->resource = resource;

    wined3d_resource_acquire(resource);

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_unload_resource(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_unload_resource(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_unload_resource *op = data;
    struct wined3d_resource *resource = op->resource;

    resource->resource_ops->resource_unload(resource);
    wined3d_resource_release(resource);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_unload_resource(struct wined3d_cs *cs, struct wined3d_resource *resource)
{
    struct wined3d_cs_unload_resource *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_UNLOAD_RESOURCE;
    op->resource = resource;

    wined3d_resource_acquire(resource);

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_map(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_map *op = data;
    struct wined3d_resource *resource = op->resource;

    *op->hr = resource->resource_ops->resource_sub_resource_map(resource,
            op->sub_resource_idx, op->map_desc, op->box, op->flags);
}

HRESULT wined3d_cs_map(struct wined3d_cs *cs, struct wined3d_resource *resource, unsigned int sub_resource_idx,
        struct wined3d_map_desc *map_desc, const struct wined3d_box *box, unsigned int flags)
{
    struct wined3d_cs_map *op;
    HRESULT hr;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_MAP;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->map_desc = map_desc;
    op->box = box;
    op->flags = flags;
    op->hr = &hr;

    cs->ops->submit(cs);

    return hr;
}

static void wined3d_cs_exec_unmap(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_unmap *op = data;
    struct wined3d_resource *resource = op->resource;

    *op->hr = resource->resource_ops->resource_sub_resource_unmap(resource, op->sub_resource_idx);
}

HRESULT wined3d_cs_unmap(struct wined3d_cs *cs, struct wined3d_resource *resource, unsigned int sub_resource_idx)
{
    struct wined3d_cs_unmap *op;
    HRESULT hr;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_UNMAP;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->hr = &hr;

    cs->ops->submit(cs);

    return hr;
}

static void (* const wined3d_cs_op_handlers[])(struct wined3d_cs *cs, const void *data) =
{
#else  /* STAGING_CSMT */
    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_glfinish(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_finish *op = data;
    struct wined3d_device *device = cs->device;
    struct wined3d_context *context;

    if (!device->d3d_initialized)
        return sizeof(*op);

    context = context_acquire(device, NULL);
    context->gl_info->gl_ops.gl.p_glFinish();
    context_release(context);

    return sizeof(*op);
}

void wined3d_cs_emit_glfinish(struct wined3d_cs *cs)
{
    struct wined3d_cs_finish *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_GLFINISH;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_base_vertex_index(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_base_vertex_index *op = data;

    cs->state.base_vertex_index = op->base_vertex_index;
    device_invalidate_state(cs->device, STATE_BASEVERTEXINDEX);

    return sizeof(*op);
}

void wined3d_cs_emit_set_base_vertex_index(struct wined3d_cs *cs,
        UINT base_vertex_index)
{
    struct wined3d_cs_set_base_vertex_index *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_BASE_VERTEX_INDEX;
    op->base_vertex_index = base_vertex_index;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_primitive_type(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_primitive_type *op = data;
    GLenum prev;

    prev = cs->state.gl_primitive_type;

    if (op->gl_primitive_type == GL_POINTS || prev == GL_POINTS)
        device_invalidate_state(cs->device, STATE_POINT_ENABLE);

    cs->state.gl_primitive_type = op->gl_primitive_type;

    return sizeof(*op);
}

void wined3d_cs_emit_set_primitive_type(struct wined3d_cs *cs, GLenum primitive_type)
{
    struct wined3d_cs_set_primitive_type *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_PRIMITIVE_TYPE;
    op->gl_primitive_type = primitive_type;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_light(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_light *op = data;

    UINT light_idx = op->light.OriginalIndex;
    UINT hash_idx = LIGHTMAP_HASHFUNC(op->light.OriginalIndex);
    struct wined3d_light_info *object = NULL;
    struct list *e;

    LIST_FOR_EACH(e, &cs->state.light_map[hash_idx])
    {
        object = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if (object->OriginalIndex == light_idx)
            break;
        object = NULL;
    }

    if (!object)
    {
        TRACE("Adding new light\n");
        object = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*object));
        if (!object)
        {
            ERR("Out of memory!\n");
            return sizeof(*op);
        }

        list_add_head(&cs->state.light_map[hash_idx], &object->entry);
        object->glIndex = -1;
        object->OriginalIndex = light_idx;
    }

    /* Update the live definitions if the light is currently assigned a glIndex. */
    if (object->glIndex != -1)
    {
        if (object->OriginalParms.type != op->light.OriginalParms.type)
            device_invalidate_state(cs->device, STATE_LIGHT_TYPE);
        device_invalidate_state(cs->device, STATE_ACTIVELIGHT(object->glIndex));
    }

    object->OriginalParms = op->light.OriginalParms;
    object->position = op->light.position;
    object->direction = op->light.direction;
    object->exponent = op->light.exponent;
    object->cutoff = op->light.cutoff;

    return sizeof(*op);
}

void wined3d_cs_emit_set_light(struct wined3d_cs *cs, const struct wined3d_light_info *light)
{
    struct wined3d_cs_set_light *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_LIGHT;
    op->light = *light;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_set_light_enable(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_set_light_enable *op = data;
    UINT hash_idx = LIGHTMAP_HASHFUNC(op->idx);
    struct wined3d_light_info *light_info = NULL;
    struct list *e;
    struct wined3d_device *device = cs->device;

    LIST_FOR_EACH(e, &cs->state.light_map[hash_idx])
    {
        light_info = LIST_ENTRY(e, struct wined3d_light_info, entry);
        if (light_info->OriginalIndex == op->idx)
            break;
        light_info = NULL;
    }
    TRACE("Found light %p.\n", light_info);

    /* Should be handled by the device by emitting a set_light op */
    if (!light_info)
    {
        ERR("Light enabled requested but light not defined in cs state!\n");
        return sizeof(*op);
    }

    if (!op->enable)
    {
        if (light_info->glIndex != -1)
        {
            device_invalidate_state(device, STATE_LIGHT_TYPE);
            device_invalidate_state(device, STATE_ACTIVELIGHT(light_info->glIndex));
            cs->state.lights[light_info->glIndex] = NULL;
            light_info->glIndex = -1;
        }
        else
        {
            TRACE("Light already disabled, nothing to do\n");
        }
        light_info->enabled = FALSE;
    }
    else
    {
        light_info->enabled = TRUE;
        if (light_info->glIndex != -1)
        {
            TRACE("Nothing to do as light was enabled\n");
        }
        else
        {
            unsigned int i;
            const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
            /* Find a free GL light. */
            for (i = 0; i < gl_info->limits.lights; ++i)
            {
                if (!cs->state.lights[i])
                {
                    cs->state.lights[i] = light_info;
                    light_info->glIndex = i;
                    break;
                }
            }
            if (light_info->glIndex == -1)
            {
                /* Should be caught by the device before emitting
                 * the light_enable op */
                ERR("Too many concurrently active lights in cs\n");
                return sizeof(*op);
            }

            /* i == light_info->glIndex */
            device_invalidate_state(device, STATE_LIGHT_TYPE);
            device_invalidate_state(device, STATE_ACTIVELIGHT(i));
        }
    }

    return sizeof(*op);
}

void wined3d_cs_emit_set_light_enable(struct wined3d_cs *cs, UINT idx, BOOL enable)
{
    struct wined3d_cs_set_light_enable *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SET_LIGHT_ENABLE;
    op->idx = idx;
    op->enable = enable;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_blt(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_blt *op = data;

    surface_blt_ugly(op->dst_surface, &op->dst_rect,
            op->src_surface, &op->src_rect,
            op->flags, &op->fx, op->filter);

    wined3d_resource_release(&op->dst_surface->container->resource);
    if (op->src_surface && op->src_surface != op->dst_surface)
        wined3d_resource_release(&op->src_surface->container->resource);

    return sizeof(*op);
}

void wined3d_cs_emit_blt(struct wined3d_cs *cs, struct wined3d_surface *dst_surface,
        const RECT *dst_rect, struct wined3d_surface *src_surface,
        const RECT *src_rect, DWORD flags, const struct wined3d_blt_fx *fx,
        enum wined3d_texture_filter_type filter)
{
    struct wined3d_cs_blt *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_BLT;
    op->dst_surface = dst_surface;
    op->dst_rect = *dst_rect;
    op->src_surface = src_surface;
    op->src_rect = *src_rect;
    op->flags = flags;
    op->filter = filter;
    if (fx)
        op->fx = *fx;

    wined3d_resource_acquire(&dst_surface->container->resource);
    if (src_surface && src_surface != dst_surface)
        wined3d_resource_acquire(&src_surface->container->resource);

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_clear_rtv(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_clear_rtv *op = data;
    struct wined3d_device *device = cs->device;

    if (op->flags & WINED3DCLEAR_TARGET)
        op->blitter->color_fill(device, op->view, &op->rect, &op->color);
    else
        op->blitter->depth_fill(device, op->view, &op->rect, op->flags, op->depth, op->stencil);

    wined3d_resource_release(op->view->resource);

    return sizeof(*op);
}

void wined3d_cs_emit_clear_rtv(struct wined3d_cs *cs, struct wined3d_rendertarget_view *view,
        const RECT *rect, DWORD flags, const struct wined3d_color *color, float depth, DWORD stencil,
        const struct blit_shader *blitter)
{
    struct wined3d_cs_clear_rtv *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_CLEAR_RTV;
    op->view = view;
    op->rect = *rect;
    op->flags = flags;
    if (flags & WINED3DCLEAR_TARGET)
        op->color = *color;
    op->depth = depth;
    op->stencil = stencil;
    op->blitter = blitter;

    wined3d_resource_acquire(view->resource);

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_texture_changed(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_texture_changed *op = data;

    wined3d_texture_changed(op->texture, op->sub_resource_idx, op->swap_buffer, op->swap_heap_memory);

    return sizeof(*op);
}

void wined3d_cs_emit_texture_changed(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx, struct wined3d_gl_bo *swap_buffer, void *swap_heap_memory)
{
    struct wined3d_cs_texture_changed *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_TEXTURE_CHANGED;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;
    op->swap_buffer = swap_buffer;
    op->swap_heap_memory = swap_heap_memory;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_texture_map(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_texture_map *op = data;

    *op->mem = wined3d_texture_map_internal(op->texture, op->sub_resource_idx, op->flags);

    return sizeof(*op);
}

void *wined3d_cs_emit_texture_map(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx, DWORD flags)
{
    struct wined3d_cs_texture_map *op;
    void *ret;

    op = cs->ops->require_space_prio(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_TEXTURE_MAP;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;
    op->flags = flags;
    op->mem = &ret;

    cs->ops->submit_prio(cs, sizeof(*op));

    cs->ops->finish_prio(cs);

    return ret;
}

static UINT wined3d_cs_exec_texture_unmap(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_texture_unmap *op = data;

    wined3d_texture_unmap_internal(op->texture, op->sub_resource_idx);

    return sizeof(*op);
}

void wined3d_cs_emit_texture_unmap(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx)
{
    struct wined3d_cs_texture_unmap *op;

    op = cs->ops->require_space_prio(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_TEXTURE_UNMAP;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;

    cs->ops->submit_prio(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_texture_add_dirty_region(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_texture_add_dirty_region *op = data;
    struct wined3d_texture *texture = op->texture;
    struct wined3d_context *context;

    context = context_acquire(cs->device, NULL);
    if (!wined3d_texture_load_location(texture, op->sub_resource_idx, context, texture->resource.map_binding))
    {
        ERR("Failed to load location %s.\n", wined3d_debug_location(texture->resource.map_binding));
    }
    else
    {
        wined3d_texture_invalidate_location(texture, op->sub_resource_idx, ~texture->resource.map_binding);
    }
    context_release(context);

    wined3d_resource_release(&texture->resource);

    return sizeof(*op);
}

void wined3d_cs_emit_texture_add_dirty_region(struct wined3d_cs *cs,
        struct wined3d_texture *texture, unsigned int sub_resource_idx,
        const struct wined3d_box *dirty_region)
{
    struct wined3d_cs_texture_add_dirty_region *op;

    if (dirty_region)
        WARN("Ignoring dirty_region %s.\n", debug_box(dirty_region));

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;

    wined3d_resource_acquire(&texture->resource);

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_update_texture(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_update_texture *op = data;
    struct wined3d_context *context;

    context = context_acquire(cs->device, NULL);
    device_exec_update_texture(context, op->src, op->dst);
    context_release(context);

    wined3d_resource_release(&op->src->resource);
    wined3d_resource_release(&op->dst->resource);

    return sizeof(*op);
}

void wined3d_cs_emit_update_texture(struct wined3d_cs *cs, struct wined3d_texture *src,
        struct wined3d_texture *dst)
{
    struct wined3d_cs_update_texture *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_UPDATE_TEXTURE;
    op->src = src;
    op->dst = dst;

    wined3d_resource_acquire(&op->src->resource);
    wined3d_resource_acquire(&op->dst->resource);

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_update_sub_resource(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_update_sub_resource *op = data;

    struct wined3d_texture_sub_resource *sub_resource;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_const_bo_address addr;
    struct wined3d_context *context;
    struct wined3d_texture *texture;
    struct wined3d_surface *surface;
    POINT dst_point;
    RECT src_rect;
    unsigned int width, height, level;

    texture = wined3d_texture_from_resource(op->resource);
    sub_resource = wined3d_texture_get_sub_resource(texture, op->sub_resource_idx);
    surface = sub_resource->u.surface;

    level = op->sub_resource_idx % texture->level_count;
    width = wined3d_texture_get_level_width(texture, level);
    height = wined3d_texture_get_level_height(texture, level);

    src_rect.left = 0;
    src_rect.top = 0;
    if (op->box)
    {
        src_rect.right = op->box->right - op->box->left;
        src_rect.bottom = op->box->bottom - op->box->top;
        dst_point.x = op->box->left;
        dst_point.y = op->box->top;
    }
    else
    {
        src_rect.right = width;
        src_rect.bottom = height;
        dst_point.x = 0;
        dst_point.y = 0;
    }

    addr.buffer_object = 0;
    addr.addr = op->data;

    context = context_acquire(texture->resource.device, NULL);
    gl_info = context->gl_info;

    /* Only load the surface for partial updates. */
    if (!dst_point.x && !dst_point.y && src_rect.right == width && src_rect.bottom == height)
        wined3d_texture_prepare_texture(texture, context, FALSE);
    else
        wined3d_texture_load_location(texture, op->sub_resource_idx, context, WINED3D_LOCATION_TEXTURE_RGB);
    wined3d_texture_bind_and_dirtify(texture, context, FALSE);

    wined3d_surface_upload_data(surface, gl_info, texture->resource.format,
            &src_rect, op->row_pitch, &dst_point, FALSE, &addr);

    context_release(context);

    wined3d_texture_validate_location(texture, op->sub_resource_idx, WINED3D_LOCATION_TEXTURE_RGB);
    wined3d_texture_invalidate_location(texture, op->sub_resource_idx, ~WINED3D_LOCATION_TEXTURE_RGB);

    return sizeof(*op);
}

void wined3d_cs_emit_update_sub_resource(struct wined3d_cs *cs, struct wined3d_resource *resource,
        unsigned int sub_resource_idx, const struct wined3d_box *box, const void *data, unsigned int row_pitch,
        unsigned int depth_pitch)
{
    struct wined3d_cs_update_sub_resource *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_UPDATE_SUB_RESOURCE;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->box = box;
    op->data = data;
    op->row_pitch = row_pitch;
    op->depth_pitch = depth_pitch;

    cs->ops->submit(cs, sizeof(*op));
    /* The data pointer may go away, need to wait until the data is read. Copying the data may be faster.
     * Don't forget to copy box as well in this case. */
    cs->ops->finish(cs);
}

static UINT wined3d_cs_exec_buffer_swap_mem(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_buffer_swap_mem *op = data;
    struct wined3d_buffer *buffer = op->buffer;

    buffer_swap_mem(buffer, op->mem);

    if (!buffer->buffer_object && buffer->resource.bind_count)
    {
        device_invalidate_state(cs->device, STATE_STREAMSRC);
        device_invalidate_state(cs->device, STATE_INDEXBUFFER);
    }
    return sizeof(*op);
}

void wined3d_cs_emit_buffer_swap_mem(struct wined3d_cs *cs, struct wined3d_buffer *buffer, BYTE *mem)
{
    struct wined3d_cs_buffer_swap_mem *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_BUFFER_SWAP_MEM;
    op->buffer = buffer;
    op->mem = mem;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_buffer_invalidate_bo_range(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_buffer_invalidate_bo_range *op = data;

    buffer_invalidate_bo_range(op->buffer, op->offset, op->size);
    return sizeof(*op);
}

void wined3d_cs_emit_buffer_invalidate_bo_range(struct wined3d_cs *cs,
        struct wined3d_buffer *buffer, unsigned int offset, unsigned int size)
{
    struct wined3d_cs_buffer_invalidate_bo_range *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_BUFFER_INVALIDATE_RANGE;
    op->buffer = buffer;
    op->offset = offset;
    op->size = size;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_create_vbo(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_create_vbo *op = data;
    struct wined3d_context *context = context_acquire(cs->device, NULL);

    buffer_create_buffer_object(op->buffer, context);

    context_release(context);

    return sizeof(*op);
}

void wined3d_cs_emit_create_vbo(struct wined3d_cs *cs, struct wined3d_buffer *buffer)
{
    struct wined3d_cs_create_vbo *op;

    op = cs->ops->require_space_prio(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_CREATE_VBO;
    op->buffer = buffer;

    cs->ops->submit_prio(cs, sizeof(*op));
    cs->ops->finish_prio(cs);
}

static UINT wined3d_cs_exec_sampler_init(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_sampler_init *op = data;

    wined3d_sampler_init(op->sampler);

    return sizeof(*op);
}

void wined3d_cs_emit_sampler_init(struct wined3d_cs *cs, struct wined3d_sampler *sampler)
{
    struct wined3d_cs_sampler_init *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_SAMPLER_INIT;
    op->sampler = sampler;

    cs->ops->submit(cs, sizeof(*op));
}

static UINT wined3d_cs_exec_get_dc(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_get_release_dc *op = data;

    wined3d_texture_get_dc_cs(op->texture, op->sub_resource_idx);

    return sizeof(*op);
}

void wined3d_cs_emit_get_dc(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx)
{
    struct wined3d_cs_get_release_dc *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_GET_DC;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);
}

static UINT wined3d_cs_exec_release_dc(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_get_release_dc *op = data;

    wined3d_texture_release_dc_cs(op->texture, op->sub_resource_idx);

    return sizeof(*op);
}

void wined3d_cs_emit_release_dc(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx)
{
    struct wined3d_cs_get_release_dc *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_RELEASE_DC;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);
}

static UINT wined3d_cs_exec_create_dummy_textures(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_create_dummy_textures *op = data;
    struct wined3d_context *context = context_acquire(cs->device, NULL);

    device_create_dummy_textures(cs->device, context);

    context_release(context);
    return sizeof(*op);
}

void wined3d_cs_emit_create_dummy_textures(struct wined3d_cs *cs)
{
    struct wined3d_cs_create_dummy_textures *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_CREATE_DUMMY_TEXTURES;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);
}

static UINT wined3d_cs_exec_create_swapchain_context(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_create_swapchain_context *op = data;

    *op->ret = swapchain_create_context_cs(cs->device, op->swapchain);

    return sizeof(*op);
}

HRESULT wined3d_cs_emit_create_swapchain_context(struct wined3d_cs *cs, struct wined3d_swapchain *swapchain)
{
    HRESULT ret;
    struct wined3d_cs_create_swapchain_context *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_CREATE_SWAPCHAIN_CONTEXT;
    op->swapchain = swapchain;
    op->ret = &ret;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);

    return ret;
}

static UINT wined3d_cs_exec_delete_gl_contexts(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_delete_gl_contexts *op = data;

    device_delete_opengl_contexts_cs(cs->device, op->swapchain);

    return sizeof(*op);
}

void wined3d_cs_emit_delete_opengl_contexts(struct wined3d_cs *cs, struct wined3d_swapchain *swapchain)
{
    struct wined3d_cs_delete_gl_contexts *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_DELETE_GL_CONTEXTS;
    op->swapchain = swapchain;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);
}

static UINT wined3d_cs_exec_update_swap_interval(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_update_swap_interval *op = data;

    swapchain_update_swap_interval(op->swapchain);

    return sizeof(*op);
}

void wined3d_cs_emit_update_swap_interval(struct wined3d_cs *cs, struct wined3d_swapchain *swapchain)
{
    struct wined3d_cs_update_swap_interval *op;

    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_UPDATE_SWAP_INTERVAL;
    op->swapchain = swapchain;

    cs->ops->submit(cs, sizeof(*op));
    cs->ops->finish(cs);
}

static UINT (* const wined3d_cs_op_handlers[])(struct wined3d_cs *cs, const void *data) =
{
    /* WINED3D_CS_OP_NOP                        */ wined3d_cs_exec_nop,
    /* WINED3D_CS_OP_SKIP                       */ wined3d_cs_exec_skip,
    /* WINED3D_CS_OP_FENCE                      */ wined3d_cs_exec_fence,
#endif /* STAGING_CSMT */
    /* WINED3D_CS_OP_PRESENT                    */ wined3d_cs_exec_present,
    /* WINED3D_CS_OP_CLEAR                      */ wined3d_cs_exec_clear,
    /* WINED3D_CS_OP_DRAW                       */ wined3d_cs_exec_draw,
    /* WINED3D_CS_OP_SET_PREDICATION            */ wined3d_cs_exec_set_predication,
    /* WINED3D_CS_OP_SET_VIEWPORT               */ wined3d_cs_exec_set_viewport,
    /* WINED3D_CS_OP_SET_SCISSOR_RECT           */ wined3d_cs_exec_set_scissor_rect,
    /* WINED3D_CS_OP_SET_RENDERTARGET_VIEW      */ wined3d_cs_exec_set_rendertarget_view,
    /* WINED3D_CS_OP_SET_DEPTH_STENCIL_VIEW     */ wined3d_cs_exec_set_depth_stencil_view,
    /* WINED3D_CS_OP_SET_VERTEX_DECLARATION     */ wined3d_cs_exec_set_vertex_declaration,
    /* WINED3D_CS_OP_SET_STREAM_SOURCE          */ wined3d_cs_exec_set_stream_source,
    /* WINED3D_CS_OP_SET_STREAM_SOURCE_FREQ     */ wined3d_cs_exec_set_stream_source_freq,
    /* WINED3D_CS_OP_SET_STREAM_OUTPUT          */ wined3d_cs_exec_set_stream_output,
    /* WINED3D_CS_OP_SET_INDEX_BUFFER           */ wined3d_cs_exec_set_index_buffer,
    /* WINED3D_CS_OP_SET_CONSTANT_BUFFER        */ wined3d_cs_exec_set_constant_buffer,
    /* WINED3D_CS_OP_SET_TEXTURE                */ wined3d_cs_exec_set_texture,
    /* WINED3D_CS_OP_SET_SHADER_RESOURCE_VIEW   */ wined3d_cs_exec_set_shader_resource_view,
    /* WINED3D_CS_OP_SET_UNORDERED_ACCESS_VIEW  */ wined3d_cs_exec_set_unordered_access_view,
    /* WINED3D_CS_OP_SET_SAMPLER                */ wined3d_cs_exec_set_sampler,
    /* WINED3D_CS_OP_SET_SHADER                 */ wined3d_cs_exec_set_shader,
    /* WINED3D_CS_OP_SET_RASTERIZER_STATE       */ wined3d_cs_exec_set_rasterizer_state,
    /* WINED3D_CS_OP_SET_RENDER_STATE           */ wined3d_cs_exec_set_render_state,
    /* WINED3D_CS_OP_SET_TEXTURE_STATE          */ wined3d_cs_exec_set_texture_state,
    /* WINED3D_CS_OP_SET_SAMPLER_STATE          */ wined3d_cs_exec_set_sampler_state,
    /* WINED3D_CS_OP_SET_TRANSFORM              */ wined3d_cs_exec_set_transform,
    /* WINED3D_CS_OP_SET_CLIP_PLANE             */ wined3d_cs_exec_set_clip_plane,
    /* WINED3D_CS_OP_SET_COLOR_KEY              */ wined3d_cs_exec_set_color_key,
    /* WINED3D_CS_OP_SET_MATERIAL               */ wined3d_cs_exec_set_material,
    /* WINED3D_CS_OP_RESET_STATE                */ wined3d_cs_exec_reset_state,
    /* WINED3D_CS_OP_DESTROY_OBJECT             */ wined3d_cs_exec_destroy_object,
    /* WINED3D_CS_OP_QUERY_ISSUE                */ wined3d_cs_exec_query_issue,
    /* WINED3D_CS_OP_PRELOAD_RESOURCE           */ wined3d_cs_exec_preload_resource,
    /* WINED3D_CS_OP_UNLOAD_RESOURCE            */ wined3d_cs_exec_unload_resource,
#if !defined(STAGING_CSMT)
    /* WINED3D_CS_OP_MAP                        */ wined3d_cs_exec_map,
    /* WINED3D_CS_OP_UNMAP                      */ wined3d_cs_exec_unmap,
};

static void *wined3d_cs_st_require_space(struct wined3d_cs *cs, size_t size)
{
    if (size > cs->data_size)
    {
        void *new_data;

        size = max( size, cs->data_size * 2 );
        if (!(new_data = HeapReAlloc(GetProcessHeap(), 0, cs->data, size)))
            return NULL;

        cs->data_size = size;
        cs->data = new_data;
    }

    return cs->data;
}

static void wined3d_cs_st_submit(struct wined3d_cs *cs)
{
    enum wined3d_cs_op opcode = *(const enum wined3d_cs_op *)cs->data;

    wined3d_cs_op_handlers[opcode](cs, cs->data);
}

static void wined3d_cs_st_push_constants(struct wined3d_cs *cs, enum wined3d_push_constants p,
        unsigned int start_idx, unsigned int count, const void *constants)
{
    struct wined3d_device *device = cs->device;
    unsigned int context_count;
    unsigned int i;
    size_t offset;

    static const struct
    {
        size_t offset;
        size_t size;
        DWORD mask;
    }
    push_constant_info[] =
    {
        /* WINED3D_PUSH_CONSTANTS_VS_F */
        {FIELD_OFFSET(struct wined3d_state, vs_consts_f), sizeof(struct wined3d_vec4),  WINED3D_SHADER_CONST_VS_F},
        /* WINED3D_PUSH_CONSTANTS_PS_F */
        {FIELD_OFFSET(struct wined3d_state, ps_consts_f), sizeof(struct wined3d_vec4),  WINED3D_SHADER_CONST_PS_F},
        /* WINED3D_PUSH_CONSTANTS_VS_I */
        {FIELD_OFFSET(struct wined3d_state, vs_consts_i), sizeof(struct wined3d_ivec4), WINED3D_SHADER_CONST_VS_I},
        /* WINED3D_PUSH_CONSTANTS_PS_I */
        {FIELD_OFFSET(struct wined3d_state, ps_consts_i), sizeof(struct wined3d_ivec4), WINED3D_SHADER_CONST_PS_I},
        /* WINED3D_PUSH_CONSTANTS_VS_B */
        {FIELD_OFFSET(struct wined3d_state, vs_consts_b), sizeof(BOOL),                 WINED3D_SHADER_CONST_VS_B},
        /* WINED3D_PUSH_CONSTANTS_PS_B */
        {FIELD_OFFSET(struct wined3d_state, ps_consts_b), sizeof(BOOL),                 WINED3D_SHADER_CONST_PS_B},
    };

    if (p == WINED3D_PUSH_CONSTANTS_VS_F)
        device->shader_backend->shader_update_float_vertex_constants(device, start_idx, count);
    else if (p == WINED3D_PUSH_CONSTANTS_PS_F)
        device->shader_backend->shader_update_float_pixel_constants(device, start_idx, count);

    offset = push_constant_info[p].offset + start_idx * push_constant_info[p].size;
    memcpy((BYTE *)&cs->state + offset, constants, count * push_constant_info[p].size);
    for (i = 0, context_count = device->context_count; i < context_count; ++i)
    {
        device->contexts[i]->constant_update_mask |= push_constant_info[p].mask;
    }
#else  /* STAGING_CSMT */
    /* WINED3D_CS_OP_SET_VS_CONSTS_F            */ wined3d_cs_exec_set_vs_consts_f,
    /* WINED3D_CS_OP_SET_VS_CONSTS_B            */ wined3d_cs_exec_set_vs_consts_b,
    /* WINED3D_CS_OP_SET_VS_CONSTS_I            */ wined3d_cs_exec_set_vs_consts_i,
    /* WINED3D_CS_OP_SET_PS_CONSTS_F            */ wined3d_cs_exec_set_ps_consts_f,
    /* WINED3D_CS_OP_SET_PS_CONSTS_B            */ wined3d_cs_exec_set_ps_consts_b,
    /* WINED3D_CS_OP_SET_PS_CONSTS_I            */ wined3d_cs_exec_set_ps_consts_i,
    /* WINED3D_CS_OP_GLFINISH                   */ wined3d_cs_exec_glfinish,
    /* WINED3D_CS_OP_SET_BASE_VERTEX_INDEX      */ wined3d_cs_exec_set_base_vertex_index,
    /* WINED3D_CS_OP_SET_PRIMITIVE_TYPE         */ wined3d_cs_exec_set_primitive_type,
    /* WINED3D_CS_OP_SET_LIGHT                  */ wined3d_cs_exec_set_light,
    /* WINED3D_CS_OP_SET_LIGHT_ENABLE           */ wined3d_cs_exec_set_light_enable,
    /* WINED3D_CS_OP_BLT                        */ wined3d_cs_exec_blt,
    /* WINED3D_CS_OP_CLEAR_RTV                  */ wined3d_cs_exec_clear_rtv,
    /* WINED3D_CS_OP_TEXTURE_CHANGED            */ wined3d_cs_exec_texture_changed,
    /* WINED3D_CS_OP_TEXTURE_MAP                */ wined3d_cs_exec_texture_map,
    /* WINED3D_CS_OP_TEXTURE_UNMAP              */ wined3d_cs_exec_texture_unmap,
    /* WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION   */ wined3d_cs_exec_texture_add_dirty_region,
    /* WINED3D_CS_OP_BUFFER_SWAP_MEM            */ wined3d_cs_exec_buffer_swap_mem,
    /* WINED3D_CS_OP_BUFFER_INVALIDATE_RANGE    */ wined3d_cs_exec_buffer_invalidate_bo_range,
    /* WINED3D_CS_OP_UPDATE_TEXTURE             */ wined3d_cs_exec_update_texture,
    /* WINED3D_CS_OP_UPDATE_SUB_RESOURCE        */ wined3d_cs_exec_update_sub_resource,
    /* WINED3D_CS_OP_CREATE_VBO                 */ wined3d_cs_exec_create_vbo,
    /* WINED3D_CS_OP_SAMPLER_INIT               */ wined3d_cs_exec_sampler_init,
    /* WINED3D_CS_OP_GET_DC                     */ wined3d_cs_exec_get_dc,
    /* WINED3D_CS_OP_RELEASE_DC                 */ wined3d_cs_exec_release_dc,
    /* WINED3D_CS_OP_CREATE_DUMMY_TEXTURES      */ wined3d_cs_exec_create_dummy_textures,
    /* WINED3D_CS_OP_CREATE_SWAPCHAIN_CONTEXT   */ wined3d_cs_exec_create_swapchain_context,
    /* WINED3D_CS_OP_DELETE_GL_CONTEXTS         */ wined3d_cs_exec_delete_gl_contexts,
    /* WINED3D_CS_OP_UPDATE_SWAP_INTERVAL       */ wined3d_cs_exec_update_swap_interval,
};

static inline void *_wined3d_cs_mt_require_space(struct wined3d_cs *cs, size_t size, BOOL prio)
{
    struct wined3d_cs_queue *queue = prio ? &cs->prio_queue : &cs->queue;
    size_t queue_size = sizeof(queue->data) / sizeof(*queue->data);

    if (cs->thread_id == GetCurrentThreadId())
        ERR("Attempting to queue a command from the CS thread.\n");

    if (queue_size - size < queue->head)
    {
        struct wined3d_cs_skip *skip;
        size_t nop_size = queue_size - queue->head;

        skip = _wined3d_cs_mt_require_space(cs, nop_size, prio);
        if (nop_size < sizeof(*skip))
        {
            skip->opcode = WINED3D_CS_OP_NOP;
        }
        else
        {
            skip->opcode = WINED3D_CS_OP_SKIP;
            skip->size = nop_size;
        }

        if (prio)
            cs->ops->submit_prio(cs, nop_size);
        else
            cs->ops->submit(cs, nop_size);

        assert(!queue->head);
    }

    while(1)
    {
        LONG head = queue->head;
        LONG tail = *((volatile LONG *)&queue->tail);
        LONG new_pos;
        /* Empty */
        if (head == tail)
            break;
        /* Head ahead of tail, take care of wrap-around */
        new_pos = (head + size) & (WINED3D_CS_QUEUE_SIZE - 1);
        if (head > tail && (new_pos || tail))
            break;
        /* Tail ahead of head, but still enough space */
        if (new_pos < tail && new_pos)
            break;

        TRACE("Waiting for free space. Head %u, tail %u, want %u\n", head, tail,
                (unsigned int) size);
    }

    return &queue->data[queue->head];
}

static inline void *wined3d_cs_mt_require_space(struct wined3d_cs *cs, size_t size)
{
    return _wined3d_cs_mt_require_space(cs, size, FALSE);
}

static inline void *wined3d_cs_mt_require_space_prio(struct wined3d_cs *cs, size_t size)
{
    return _wined3d_cs_mt_require_space(cs, size, TRUE);
}

/* FIXME: wined3d_device_uninit_3d() should either flush and wait, or be an
 * OP itself. */
static void wined3d_cs_emit_stop(struct wined3d_cs *cs)
{
    struct wined3d_cs_stop *op;

    op = wined3d_cs_mt_require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_STOP;

    wined3d_cs_mt_submit(cs, sizeof(*op));
}

static void wined3d_cs_mt_finish(struct wined3d_cs *cs)
{
    BOOL fence;
    wined3d_cs_emit_fence(cs, &fence);

    /* A busy wait should be fine, we're not supposed to have to wait very
     * long. */
    while (!InterlockedCompareExchange(&fence, TRUE, TRUE));
}

static void wined3d_cs_mt_finish_prio(struct wined3d_cs *cs)
{
    BOOL fence;

    if (cs->thread_id == GetCurrentThreadId())
    {
        static BOOL once;
        if (!once)
        {
            FIXME("flush_and_wait called from cs thread\n");
            once = TRUE;
        }
        return;
    }

    wined3d_cs_emit_fence_prio(cs, &fence);

    /* A busy wait should be fine, we're not supposed to have to wait very
     * long. */
    while (!InterlockedCompareExchange(&fence, TRUE, TRUE));
}

static const struct wined3d_cs_ops wined3d_cs_mt_ops =
{
    wined3d_cs_mt_require_space,
    wined3d_cs_mt_require_space_prio,
    wined3d_cs_mt_submit,
    wined3d_cs_mt_submit_prio,
    wined3d_cs_mt_finish,
    wined3d_cs_mt_finish_prio,
};

static void wined3d_cs_st_submit(struct wined3d_cs *cs, size_t size)
{
    enum wined3d_cs_op opcode = *(const enum wined3d_cs_op *)&cs->queue.data;

    if (opcode >= WINED3D_CS_OP_STOP)
    {
        ERR("Invalid opcode %#x.\n", opcode);
        return;
    }

    wined3d_cs_op_handlers[opcode](cs, &cs->queue.data);
}

static void wined3d_cs_st_finish(struct wined3d_cs *cs)
{
}

static void *wined3d_cs_st_require_space(struct wined3d_cs *cs, size_t size)
{
    return cs->queue.data;
#endif /* STAGING_CSMT */
}

static const struct wined3d_cs_ops wined3d_cs_st_ops =
{
    wined3d_cs_st_require_space,
#if !defined(STAGING_CSMT)
    wined3d_cs_st_submit,
    wined3d_cs_st_push_constants,
};
#else  /* STAGING_CSMT */
    wined3d_cs_st_require_space,
    wined3d_cs_st_submit,
    wined3d_cs_st_submit,
    wined3d_cs_st_finish,
    wined3d_cs_st_finish,
};

void wined3d_cs_switch_onscreen_ds(struct wined3d_cs *cs,
        struct wined3d_context *context, struct wined3d_surface *depth_stencil)
{
    if (cs->onscreen_depth_stencil)
    {
        struct wined3d_texture *texture = cs->onscreen_depth_stencil->container;

        wined3d_texture_load_location(texture,
                surface_get_sub_resource_idx(cs->onscreen_depth_stencil),
                context, WINED3D_LOCATION_TEXTURE_RGB);

        surface_modify_ds_location(cs->onscreen_depth_stencil, WINED3D_LOCATION_TEXTURE_RGB,
                cs->onscreen_depth_stencil->ds_current_size.cx,
                cs->onscreen_depth_stencil->ds_current_size.cy);
        wined3d_texture_decref(texture);
    }
    cs->onscreen_depth_stencil = depth_stencil;
    wined3d_texture_incref(cs->onscreen_depth_stencil->container);
}

static inline void poll_queries(struct wined3d_cs *cs)
{
    struct wined3d_query *query, *cursor;

    LIST_FOR_EACH_ENTRY_SAFE(query, cursor, &cs->query_poll_list, struct wined3d_query, poll_list_entry)
    {
        BOOL ret;

        ret = query->query_ops->query_poll(query, 0);
        if (ret)
        {
            list_remove(&query->poll_list_entry);
            list_init(&query->poll_list_entry);
            InterlockedIncrement(&query->counter_retrieved);
        }
    }
}

static inline BOOL queue_is_empty(const struct wined3d_cs_queue *queue)
{
    return *((volatile LONG *)&queue->head) == queue->tail;
}

static void wined3d_cs_wait_event(struct wined3d_cs *cs)
{
    InterlockedExchange(&cs->waiting_for_event, TRUE);

    /* The main thread might enqueue a finish command and block on it
     * after the worker thread decided to enter wined3d_cs_wait_event
     * and before waiting_for_event was set to TRUE. Check again if
     * the queues are empty */
    if (!queue_is_empty(&cs->prio_queue) || !queue_is_empty(&cs->queue))
    {
        /* The main thread might have signalled the event, or be in the process
         * of doing so. Wait for the event to reset it. ResetEvent is not good
         * because the main thread might be beween the waiting_for_event reset
         * and SignalEvent call. */
        if (!InterlockedCompareExchange(&cs->waiting_for_event, FALSE, TRUE))
            WaitForSingleObject(cs->event, INFINITE);
    }
    else
    {
        WaitForSingleObject(cs->event, INFINITE);
    }
}

static DWORD WINAPI wined3d_cs_run(void *thread_param)
{
    struct wined3d_cs *cs = thread_param;
    enum wined3d_cs_op opcode;
    LONG tail;
    char poll = 0;
    struct wined3d_cs_queue *queue;
    unsigned int spin_count = 0;

    TRACE("Started.\n");

    list_init(&cs->query_poll_list);
    cs->thread_id = GetCurrentThreadId();
    for (;;)
    {
        if (poll == 10)
        {
            poll = 0;
            poll_queries(cs);
        }
        else
            poll++;

        if (!queue_is_empty(&cs->prio_queue))
        {
            queue = &cs->prio_queue;
        }
        else if (!queue_is_empty(&cs->queue))
        {
            queue = &cs->queue;
            if (!queue_is_empty(&cs->prio_queue))
                queue = &cs->prio_queue;
        }
        else
        {
            spin_count++;
            if (spin_count >= WINED3D_CS_SPIN_COUNT && list_empty(&cs->query_poll_list))
                wined3d_cs_wait_event(cs);

            continue;
        }

        spin_count = 0;

        tail = queue->tail;
        opcode = *(const enum wined3d_cs_op *)&queue->data[tail];

        if (opcode >= WINED3D_CS_OP_STOP)
        {
            if (opcode > WINED3D_CS_OP_STOP)
                ERR("Invalid opcode %#x.\n", opcode);
            goto done;
        }

        tail += wined3d_cs_op_handlers[opcode](cs, &queue->data[tail]);
        tail &= (WINED3D_CS_QUEUE_SIZE - 1);
        InterlockedExchange(&queue->tail, tail);
    }

done:
    TRACE("Stopped.\n");
    return 0;
}
#endif /* STAGING_CSMT */

struct wined3d_cs *wined3d_cs_create(struct wined3d_device *device)
{
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
#if !defined(STAGING_CSMT)
    struct wined3d_cs *cs;
#else  /* STAGING_CSMT */
    struct wined3d_cs *cs = NULL;
#endif /* STAGING_CSMT */

    if (!(cs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*cs))))
        return NULL;

#if !defined(STAGING_CSMT)
    if (!(cs->fb.render_targets = wined3d_calloc(gl_info->limits.buffers, sizeof(*cs->fb.render_targets))))
    {
        HeapFree(GetProcessHeap(), 0, cs);
        return NULL;
    }

    state_init(&cs->state, &cs->fb, gl_info, &device->adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT);
#else  /* STAGING_CSMT */
    if (FAILED(state_init(&cs->state, gl_info, &device->adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT)))
    {
        goto err;
    }
#endif /* STAGING_CSMT */

    cs->ops = &wined3d_cs_st_ops;
    cs->device = device;

#if !defined(STAGING_CSMT)
    cs->data_size = WINED3D_INITIAL_CS_SIZE;
    if (!(cs->data = HeapAlloc(GetProcessHeap(), 0, cs->data_size)))
    {
        state_cleanup(&cs->state);
        HeapFree(GetProcessHeap(), 0, cs->fb.render_targets);
        HeapFree(GetProcessHeap(), 0, cs);
        return NULL;
#else  /* STAGING_CSMT */
    if (wined3d_settings.cs_multithreaded)
    {
        cs->ops = &wined3d_cs_mt_ops;

        cs->event = CreateEventW(NULL, FALSE, FALSE, NULL);

        if (!(cs->thread = CreateThread(NULL, 0, wined3d_cs_run, cs, 0, NULL)))
        {
            ERR("Failed to create wined3d command stream thread.\n");
            goto err;
        }
#endif /* STAGING_CSMT */
    }

    return cs;
#if defined(STAGING_CSMT)

err:
    if (cs)
        state_cleanup(&cs->state);
    HeapFree(GetProcessHeap(), 0, cs);
    return NULL;
#endif /* STAGING_CSMT */
}

void wined3d_cs_destroy(struct wined3d_cs *cs)
{
#if !defined(STAGING_CSMT)
    state_cleanup(&cs->state);
    HeapFree(GetProcessHeap(), 0, cs->fb.render_targets);
    HeapFree(GetProcessHeap(), 0, cs->data);
#else  /* STAGING_CSMT */
    DWORD ret;

    state_cleanup(&cs->state);

    if (wined3d_settings.cs_multithreaded)
    {
        wined3d_cs_emit_stop(cs);

        ret = WaitForSingleObject(cs->thread, INFINITE);
        CloseHandle(cs->thread);
        if (ret != WAIT_OBJECT_0)
            ERR("Wait failed (%#x).\n", ret);
        if (!CloseHandle(cs->event))
            ERR("Closing event failed.\n");
    }

#endif /* STAGING_CSMT */
    HeapFree(GetProcessHeap(), 0, cs);
}
