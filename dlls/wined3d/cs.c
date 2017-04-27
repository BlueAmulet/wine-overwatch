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
#if defined(STAGING_CSMT)
#include <assert.h>
#endif /* STAGING_CSMT */
#include "wined3d_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d);

#define WINED3D_INITIAL_CS_SIZE 4096

enum wined3d_cs_op
{
#if defined(STAGING_CSMT)
    WINED3D_CS_OP_SYNC,
    WINED3D_CS_OP_GLFINISH,
#endif /* STAGING_CSMT */
    WINED3D_CS_OP_PRESENT,
    WINED3D_CS_OP_CLEAR,
    WINED3D_CS_OP_DISPATCH,
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
    WINED3D_CS_OP_SET_LIGHT,
    WINED3D_CS_OP_SET_LIGHT_ENABLE,
    WINED3D_CS_OP_RESET_STATE,
    WINED3D_CS_OP_CALLBACK,
    WINED3D_CS_OP_QUERY_ISSUE,
#if defined(STAGING_CSMT)
    WINED3D_CS_OP_QUERY_POLL,
#endif /* STAGING_CSMT */
    WINED3D_CS_OP_PRELOAD_RESOURCE,
    WINED3D_CS_OP_UNLOAD_RESOURCE,
    WINED3D_CS_OP_MAP,
    WINED3D_CS_OP_UNMAP,
#if defined(STAGING_CSMT)
    WINED3D_CS_OP_PUSH_CONSTANTS,
    WINED3D_CS_OP_BLT,
    WINED3D_CS_OP_CLEAR_RTV,
    WINED3D_CS_OP_UPDATE_TEXTURE,
    WINED3D_CS_OP_UPDATE_SUB_RESOURCE,
    WINED3D_CS_OP_GET_DC,
    WINED3D_CS_OP_RELEASE_DC,
    WINED3D_CS_OP_UPDATE_SWAP_INTERVAL,
    WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION,
    WINED3D_CS_OP_BUFFER_COPY,
    WINED3D_CS_OP_MAP_VERTEX_BUFFERS,
    WINED3D_CS_OP_STOP,
};

struct wined3d_cs_sync
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_glfinish
{
    enum wined3d_cs_op opcode;
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

struct wined3d_cs_dispatch
{
    enum wined3d_cs_op opcode;
    unsigned int group_count_x;
    unsigned int group_count_y;
    unsigned int group_count_z;
};

struct wined3d_cs_draw
{
    enum wined3d_cs_op opcode;
    GLenum primitive_type;
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
    enum wined3d_pipeline pipeline;
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

struct wined3d_cs_set_light
{
    enum wined3d_cs_op opcode;
    struct wined3d_light_info light;
};

struct wined3d_cs_set_light_enable
{
    enum wined3d_cs_op opcode;
    unsigned int idx;
    BOOL enable;
};

struct wined3d_cs_reset_state
{
    enum wined3d_cs_op opcode;
};

struct wined3d_cs_callback
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

#if defined(STAGING_CSMT)
struct wined3d_cs_query_poll
{
    enum wined3d_cs_op opcode;
    struct wined3d_query *query;
    DWORD flags;
    BOOL *ret;
};

#endif /* STAGING_CSMT */
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
{
    enum wined3d_cs_op opcode;
    struct wined3d_resource *resource;
    unsigned int sub_resource_idx;
    HRESULT *hr;
};

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_present(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
struct wined3d_cs_push_constants
{
    enum wined3d_cs_op opcode;
    enum wined3d_push_constants p;
    unsigned int start_idx;
    unsigned int count;
    BYTE constants[1];
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

struct wined3d_cs_update_texture
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *src, *dst;
};

struct wined3d_cs_update_sub_resource
{
    enum wined3d_cs_op opcode;
    unsigned int size;
    struct wined3d_resource *resource;
    unsigned int sub_resource_idx, row_pitch, depth_pitch;
    const struct wined3d_box *box;
    const void *data;
    struct wined3d_box copy_box;
    BYTE copy_data[1];
};

struct wined3d_cs_get_release_dc
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
    HRESULT *hr;
};

struct wined3d_cs_update_swap_interval
{
    enum wined3d_cs_op opcode;
    struct wined3d_swapchain *swapchain;
};

struct wined3d_cs_texture_add_dirty_region
{
    enum wined3d_cs_op opcode;
    struct wined3d_texture *texture;
    unsigned int sub_resource_idx;
};

struct wined3d_cs_buffer_copy
{
    enum wined3d_cs_op opcode;
    struct wined3d_buffer *dst_buffer;
    unsigned int dst_offset;
    struct wined3d_buffer *src_buffer;
    unsigned int src_offset;
    unsigned int size;
};

struct wined3d_cs_map_vertex_buffers
{
    enum wined3d_cs_op opcode;
    UINT src_start_idx;
    struct wined3d_stream_info *stream_info;
};

struct wined3d_cs_stop
{
    enum wined3d_cs_op opcode;
};

static UINT wined3d_cs_exec_sync(struct wined3d_cs *cs, const void *data)
{
    return sizeof(struct wined3d_cs_sync);
}

void wined3d_cs_emit_sync(struct wined3d_cs *cs)
{
    struct wined3d_cs_sync *op;

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_SYNC;

    cs->ops->submit_and_wait(cs);
}

static UINT wined3d_cs_exec_glfinish(struct wined3d_cs *cs, const void *data)
{
    struct wined3d_context *context = context_get_current();

    if (context)
        context->gl_info->gl_ops.gl.p_glFinish();

    return sizeof(struct wined3d_cs_glfinish);
}

void wined3d_cs_emit_glfinish(struct wined3d_cs *cs)
{
    struct wined3d_cs_glfinish *op;

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_GLFINISH;

    cs->ops->submit_and_wait(cs);
}

static UINT wined3d_cs_exec_present(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_present *op = data;
    struct wined3d_swapchain *swapchain;
    unsigned int i;

    swapchain = op->swapchain;
    wined3d_swapchain_set_window(swapchain, op->dst_window_override);

    swapchain->swapchain_ops->swapchain_present(swapchain, &op->src_rect, &op->dst_rect, op->flags);

#if defined(STAGING_CSMT)
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
#if !defined(STAGING_CSMT)

    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    LONG pending;

    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
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

    cs->ops->submit(cs);

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
    const struct wined3d_state *state = &cs->state;
    const struct wined3d_cs_clear *op = data;
    struct wined3d_device *device;
    unsigned int i;
    RECT draw_rect;
#if defined(STAGING_CSMT)
    size_t size = FIELD_OFFSET(struct wined3d_cs_clear, rects[op->rect_count]);
#endif /* STAGING_CSMT */

    device = cs->device;
    wined3d_get_draw_rect(state, &draw_rect);
    device_clear_render_targets(device, device->adapter->gl_info.limits.buffers,
            state->fb, op->rect_count, op->rects, &draw_rect, op->flags,
            &op->color, op->depth, op->stencil);

    if (op->flags & WINED3DCLEAR_TARGET)
    {
        for (i = 0; i < device->adapter->gl_info.limits.buffers; ++i)
        {
            if (state->fb->render_targets[i])
                wined3d_resource_release(state->fb->render_targets[i]->resource);
        }
    }
    if (op->flags & (WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL))
        wined3d_resource_release(state->fb->depth_stencil->resource);
#if defined(STAGING_CSMT)

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
    op = cs->ops->require_space(cs, FIELD_OFFSET(struct wined3d_cs_clear, rects[rect_count]), 0);
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
            if (state->fb->render_targets[i])
                wined3d_resource_acquire(state->fb->render_targets[i]->resource);
        }
    }
    if (flags & (WINED3DCLEAR_ZBUFFER | WINED3DCLEAR_STENCIL))
        wined3d_resource_acquire(state->fb->depth_stencil->resource);

    cs->ops->submit(cs);
}

static void acquire_shader_resources(const struct wined3d_state *state, unsigned int shader_mask)
{
    struct wined3d_shader_sampler_map_entry *entry;
    struct wined3d_shader_resource_view *view;
    struct wined3d_shader *shader;
    unsigned int i, j;

    for (i = 0; i < WINED3D_SHADER_TYPE_COUNT; ++i)
    {
        if (!(shader_mask & (1u << i)))
            continue;

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
}

static void release_shader_resources(const struct wined3d_state *state, unsigned int shader_mask)
{
    struct wined3d_shader_sampler_map_entry *entry;
    struct wined3d_shader_resource_view *view;
    struct wined3d_shader *shader;
    unsigned int i, j;

    for (i = 0; i < WINED3D_SHADER_TYPE_COUNT; ++i)
    {
        if (!(shader_mask & (1u << i)))
            continue;

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
}

static void acquire_unordered_access_resources(const struct wined3d_shader *shader,
        struct wined3d_unordered_access_view * const *views)
{
    unsigned int i;

    if (!shader)
        return;

    for (i = 0; i < MAX_UNORDERED_ACCESS_VIEWS; ++i)
    {
        if (!shader->reg_maps.uav_resource_info[i].type)
            continue;

        if (!views[i])
            continue;

        wined3d_resource_acquire(views[i]->resource);
    }
}

static void release_unordered_access_resources(const struct wined3d_shader *shader,
        struct wined3d_unordered_access_view * const *views)
{
    unsigned int i;

    if (!shader)
        return;

    for (i = 0; i < MAX_UNORDERED_ACCESS_VIEWS; ++i)
    {
        if (!shader->reg_maps.uav_resource_info[i].type)
            continue;

        if (!views[i])
            continue;

        wined3d_resource_release(views[i]->resource);
    }
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_dispatch(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_dispatch(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_dispatch *op = data;
    struct wined3d_state *state = &cs->state;

    dispatch_compute(cs->device, state,
            op->group_count_x, op->group_count_y, op->group_count_z);

    release_shader_resources(state, 1u << WINED3D_SHADER_TYPE_COMPUTE);
    release_unordered_access_resources(state->shader[WINED3D_SHADER_TYPE_COMPUTE],
            state->unordered_access_view[WINED3D_PIPELINE_COMPUTE]);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_dispatch(struct wined3d_cs *cs,
        unsigned int group_count_x, unsigned int group_count_y, unsigned int group_count_z)
{
    const struct wined3d_state *state = &cs->device->state;
    struct wined3d_cs_dispatch *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_DISPATCH;
    op->group_count_x = group_count_x;
    op->group_count_y = group_count_y;
    op->group_count_z = group_count_z;

    acquire_shader_resources(state, 1u << WINED3D_SHADER_TYPE_COMPUTE);
    acquire_unordered_access_resources(state->shader[WINED3D_SHADER_TYPE_COMPUTE],
            state->unordered_access_view[WINED3D_PIPELINE_COMPUTE]);

    cs->ops->submit(cs);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_draw(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_draw(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    struct wined3d_state *state = &cs->state;
    const struct wined3d_cs_draw *op = data;
    unsigned int i;

    if (!cs->device->adapter->gl_info.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX]
            && state->load_base_vertex_index != op->base_vertex_idx)
    {
        state->load_base_vertex_index = op->base_vertex_idx;
        device_invalidate_state(cs->device, STATE_BASEVERTEXINDEX);
    }

    if (state->gl_primitive_type != op->primitive_type)
    {
        if (state->gl_primitive_type == GL_POINTS || op->primitive_type == GL_POINTS)
            device_invalidate_state(cs->device, STATE_POINT_ENABLE);
        state->gl_primitive_type = op->primitive_type;
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
        if (state->fb->render_targets[i])
            wined3d_resource_release(state->fb->render_targets[i]->resource);
    }
    if (state->fb->depth_stencil)
        wined3d_resource_release(state->fb->depth_stencil->resource);
    release_shader_resources(state, ~(1u << WINED3D_SHADER_TYPE_COMPUTE));
    release_unordered_access_resources(state->shader[WINED3D_SHADER_TYPE_PIXEL],
            state->unordered_access_view[WINED3D_PIPELINE_GRAPHICS]);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_draw(struct wined3d_cs *cs, GLenum primitive_type, int base_vertex_idx, unsigned int start_idx,
        unsigned int index_count, unsigned int start_instance, unsigned int instance_count, BOOL indexed)
{
    const struct wined3d_state *state = &cs->device->state;
    struct wined3d_cs_draw *op;
    unsigned int i;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_DRAW;
    op->primitive_type = primitive_type;
    op->base_vertex_idx = base_vertex_idx;
    op->start_idx = start_idx;
    op->index_count = index_count;
    op->start_instance = start_instance;
    op->instance_count = instance_count;
    op->indexed = indexed;

    if (indexed)
        wined3d_resource_acquire(&state->index_buffer->resource);
    for (i = 0; i < ARRAY_SIZE(state->streams); ++i)
    {
        if (state->streams[i].buffer)
            wined3d_resource_acquire(&state->streams[i].buffer->resource);
    }
    for (i = 0; i < ARRAY_SIZE(state->textures); ++i)
    {
        if (state->textures[i])
            wined3d_resource_acquire(&state->textures[i]->resource);
    }
    for (i = 0; i < cs->device->adapter->gl_info.limits.buffers; ++i)
    {
        if (state->fb->render_targets[i])
            wined3d_resource_acquire(state->fb->render_targets[i]->resource);
    }
    if (state->fb->depth_stencil)
        wined3d_resource_acquire(state->fb->depth_stencil->resource);
    acquire_shader_resources(state, ~(1u << WINED3D_SHADER_TYPE_COMPUTE));
    acquire_unordered_access_resources(state->shader[WINED3D_SHADER_TYPE_PIXEL],
            state->unordered_access_view[WINED3D_PIPELINE_GRAPHICS]);

    cs->ops->submit(cs);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_set_predication(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_PREDICATION;
    op->predicate = predicate;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_viewport(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_VIEWPORT;
    op->viewport = *viewport;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_scissor_rect(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_SCISSOR_RECT;
    op->rect = *rect;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_rendertarget_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_rendertarget_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_rendertarget_view *op = data;

    cs->state.fb->render_targets[op->view_idx] = op->view;
    device_invalidate_state(cs->device, STATE_FRAMEBUFFER);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_rendertarget_view(struct wined3d_cs *cs, unsigned int view_idx,
        struct wined3d_rendertarget_view *view)
{
    struct wined3d_cs_set_rendertarget_view *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_RENDERTARGET_VIEW;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_depth_stencil_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_depth_stencil_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_depth_stencil_view *op = data;
    struct wined3d_device *device = cs->device;
    struct wined3d_rendertarget_view *prev;

    if ((prev = cs->state.fb->depth_stencil))
    {
        struct wined3d_surface *prev_surface = wined3d_rendertarget_view_get_surface(prev);

        if (prev_surface && (device->swapchains[0]->desc.flags & WINED3D_SWAPCHAIN_DISCARD_DEPTHSTENCIL
                || prev_surface->container->flags & WINED3D_TEXTURE_DISCARD))
        {
            wined3d_texture_validate_location(prev_surface->container,
                    prev->sub_resource_idx, WINED3D_LOCATION_DISCARDED);
        }
    }

    cs->fb.depth_stencil = op->view;

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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_DEPTH_STENCIL_VIEW;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_vertex_declaration(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_VERTEX_DECLARATION;
    op->declaration = declaration;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_stream_source(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
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
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_STREAM_SOURCE_FREQ;
    op->stream_idx = stream_idx;
    op->frequency = frequency;
    op->flags = flags;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_stream_output(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_STREAM_OUTPUT;
    op->stream_idx = stream_idx;
    op->buffer = buffer;
    op->offset = offset;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_index_buffer(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_INDEX_BUFFER;
    op->buffer = buffer;
    op->format_id = format_id;
    op->offset = offset;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_constant_buffer(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_CONSTANT_BUFFER;
    op->type = type;
    op->cb_idx = cb_idx;
    op->buffer = buffer;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_texture(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_TEXTURE;
    op->stage = stage;
    op->texture = texture;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_shader_resource_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_shader_resource_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_shader_resource_view *op = data;
    struct wined3d_shader_resource_view *prev;

    prev = cs->state.shader_resource_view[op->type][op->view_idx];
    cs->state.shader_resource_view[op->type][op->view_idx] = op->view;

    if (op->view)
        InterlockedIncrement(&op->view->resource->bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource->bind_count);

    if (op->type != WINED3D_SHADER_TYPE_COMPUTE)
        device_invalidate_state(cs->device, STATE_GRAPHICS_SHADER_RESOURCE_BINDING);
    else
        device_invalidate_state(cs->device, STATE_COMPUTE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_shader_resource_view(struct wined3d_cs *cs, enum wined3d_shader_type type,
        UINT view_idx, struct wined3d_shader_resource_view *view)
{
    struct wined3d_cs_set_shader_resource_view *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_SHADER_RESOURCE_VIEW;
    op->type = type;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_unordered_access_view(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_unordered_access_view(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_unordered_access_view *op = data;
    struct wined3d_unordered_access_view *prev;

    prev = cs->state.unordered_access_view[op->pipeline][op->view_idx];
    cs->state.unordered_access_view[op->pipeline][op->view_idx] = op->view;

    if (op->view)
        InterlockedIncrement(&op->view->resource->bind_count);
    if (prev)
        InterlockedDecrement(&prev->resource->bind_count);

    device_invalidate_state(cs->device, STATE_UNORDERED_ACCESS_VIEW_BINDING(op->pipeline));
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_unordered_access_view(struct wined3d_cs *cs, enum wined3d_pipeline pipeline,
        unsigned int view_idx, struct wined3d_unordered_access_view *view)
{
    struct wined3d_cs_set_unordered_access_view *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_UNORDERED_ACCESS_VIEW;
    op->pipeline = pipeline;
    op->view_idx = view_idx;
    op->view = view;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_sampler(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_sampler(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_sampler *op = data;

    cs->state.sampler[op->type][op->sampler_idx] = op->sampler;
    if (op->type != WINED3D_SHADER_TYPE_COMPUTE)
        device_invalidate_state(cs->device, STATE_GRAPHICS_SHADER_RESOURCE_BINDING);
    else
        device_invalidate_state(cs->device, STATE_COMPUTE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_sampler(struct wined3d_cs *cs, enum wined3d_shader_type type,
        UINT sampler_idx, struct wined3d_sampler *sampler)
{
    struct wined3d_cs_set_sampler *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_SAMPLER;
    op->type = type;
    op->sampler_idx = sampler_idx;
    op->sampler = sampler;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_shader(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_shader(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_shader *op = data;

    cs->state.shader[op->type] = op->shader;
    device_invalidate_state(cs->device, STATE_SHADER(op->type));
    if (op->type != WINED3D_SHADER_TYPE_COMPUTE)
        device_invalidate_state(cs->device, STATE_GRAPHICS_SHADER_RESOURCE_BINDING);
    else
        device_invalidate_state(cs->device, STATE_COMPUTE_SHADER_RESOURCE_BINDING);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_shader(struct wined3d_cs *cs, enum wined3d_shader_type type, struct wined3d_shader *shader)
{
    struct wined3d_cs_set_shader *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_SHADER;
    op->type = type;
    op->shader = shader;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_rasterizer_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_RASTERIZER_STATE;
    op->state = rasterizer_state;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_render_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_RENDER_STATE;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_texture_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_TEXTURE_STATE;
    op->stage = stage;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_sampler_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_SAMPLER_STATE;
    op->sampler_idx = sampler_idx;
    op->state = state;
    op->value = value;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_transform(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_TRANSFORM;
    op->state = state;
    op->matrix = *matrix;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_clip_plane(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_CLIP_PLANE;
    op->plane_idx = plane_idx;
    op->plane = *plane;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_color_key(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
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
    cs->ops->submit_delayed(cs);
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_MATERIAL;
    op->material = *material;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_light(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_light(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_light *op = data;
    struct wined3d_light_info *light_info;
    unsigned int light_idx, hash_idx;

    light_idx = op->light.OriginalIndex;

    if (!(light_info = wined3d_state_get_light(&cs->state, light_idx)))
    {
        TRACE("Adding new light.\n");
        if (!(light_info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*light_info))))
        {
            ERR("Failed to allocate light info.\n");
#if !defined(STAGING_CSMT)
            return;
#else  /* STAGING_CSMT */
            return sizeof(*op);;
#endif /* STAGING_CSMT */
        }

        hash_idx = LIGHTMAP_HASHFUNC(light_idx);
        list_add_head(&cs->state.light_map[hash_idx], &light_info->entry);
        light_info->glIndex = -1;
        light_info->OriginalIndex = light_idx;
    }

    if (light_info->glIndex != -1)
    {
        if (light_info->OriginalParms.type != op->light.OriginalParms.type)
            device_invalidate_state(cs->device, STATE_LIGHT_TYPE);
        device_invalidate_state(cs->device, STATE_ACTIVELIGHT(light_info->glIndex));
    }

    light_info->OriginalParms = op->light.OriginalParms;
    light_info->position = op->light.position;
    light_info->direction = op->light.direction;
    light_info->exponent = op->light.exponent;
    light_info->cutoff = op->light.cutoff;
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_light(struct wined3d_cs *cs, const struct wined3d_light_info *light)
{
    struct wined3d_cs_set_light *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_LIGHT;
    op->light = *light;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_set_light_enable(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_set_light_enable(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_set_light_enable *op = data;
    struct wined3d_device *device = cs->device;
    struct wined3d_light_info *light_info;
    int prev_idx;

    if (!(light_info = wined3d_state_get_light(&cs->state, op->idx)))
    {
        ERR("Light doesn't exist.\n");
#if !defined(STAGING_CSMT)
        return;
#else  /* STAGING_CSMT */
        return sizeof(*op);
#endif /* STAGING_CSMT */
    }

    prev_idx = light_info->glIndex;
    wined3d_state_enable_light(&cs->state, &device->adapter->d3d_info, light_info, op->enable);
    if (light_info->glIndex != prev_idx)
    {
        device_invalidate_state(device, STATE_LIGHT_TYPE);
        device_invalidate_state(device, STATE_ACTIVELIGHT(op->enable ? light_info->glIndex : prev_idx));
    }
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_set_light_enable(struct wined3d_cs *cs, unsigned int idx, BOOL enable)
{
    struct wined3d_cs_set_light_enable *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_SET_LIGHT_ENABLE;
    op->idx = idx;
    op->enable = enable;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
}

static void wined3d_cs_exec_reset_state(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_reset_state(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    struct wined3d_adapter *adapter = cs->device->adapter;

    state_cleanup(&cs->state);
    memset(&cs->state, 0, sizeof(cs->state));
    state_init(&cs->state, &cs->fb, &adapter->gl_info, &adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT);
#if defined(STAGING_CSMT)

    return sizeof(struct wined3d_cs_reset_state);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_reset_state(struct wined3d_cs *cs)
{
    struct wined3d_cs_reset_state *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
    op->opcode = WINED3D_CS_OP_RESET_STATE;

    cs->ops->submit(cs);
}

static void wined3d_cs_exec_callback(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_RESET_STATE;

    cs->ops->submit_delayed(cs);
}

static UINT wined3d_cs_exec_callback(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_callback *op = data;

    op->callback(op->object);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

static void wined3d_cs_emit_callback(struct wined3d_cs *cs, void (*callback)(void *object), void *object)
{
    struct wined3d_cs_callback *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_CALLBACK;
    op->callback = callback;
    op->object = object;

    cs->ops->submit(cs);
}

void wined3d_cs_destroy_object(struct wined3d_cs *cs, void (*callback)(void *object), void *object)
{
    wined3d_cs_emit_callback(cs, callback, object);
}

void wined3d_cs_init_object(struct wined3d_cs *cs, void (*callback)(void *object), void *object)
{
    wined3d_cs_emit_callback(cs, callback, object);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_query_issue(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_query_issue(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_query_issue *op = data;
    struct wined3d_query *query = op->query;
#if !defined(STAGING_CSMT)

    query->query_ops->query_issue(query, op->flags);
#else  /* STAGING_CSMT */
    struct wined3d_context *context;

    query->query_ops->query_issue(query, op->flags);

    InterlockedDecrement(&query->pending);

    if (query->flush && (context = context_get_current()))
        context->gl_info->gl_ops.gl.p_glFlush();

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

void wined3d_cs_emit_query_issue(struct wined3d_cs *cs, struct wined3d_query *query, DWORD flags)
{
    struct wined3d_cs_query_issue *op;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_QUERY_ISSUE;
    op->query = query;
    op->flags = flags;

#if defined(STAGING_CSMT)
    InterlockedIncrement(&query->pending);

#endif /* STAGING_CSMT */
    cs->ops->submit(cs);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_preload_resource(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_query_poll(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_query_poll *op = data;
    struct wined3d_query *query = op->query;

    *op->ret = query->query_ops->query_poll(query, op->flags);

    return sizeof(*op);
}

BOOL wined3d_cs_emit_query_poll(struct wined3d_cs *cs, struct wined3d_query *query, DWORD flags)
{
    struct wined3d_cs_query_poll *op;
    BOOL ret;

    op = cs->ops->require_space(cs, sizeof(*op), 1);
    op->opcode = WINED3D_CS_OP_QUERY_POLL;
    op->query = query;
    op->flags = flags;
    op->ret = &ret;

    cs->ops->submit_and_wait(cs);

    return ret;
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_PRELOAD_RESOURCE;
    op->resource = resource;

    wined3d_resource_acquire(resource);

    cs->ops->submit(cs);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_unload_resource(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
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

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 0);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_UNLOAD_RESOURCE;
    op->resource = resource;

    wined3d_resource_acquire(resource);

    cs->ops->submit(cs);
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_map(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_map(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_map *op = data;
    struct wined3d_resource *resource = op->resource;

    *op->hr = resource->resource_ops->resource_sub_resource_map(resource,
            op->sub_resource_idx, op->map_desc, op->box, op->flags);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

HRESULT wined3d_cs_map(struct wined3d_cs *cs, struct wined3d_resource *resource, unsigned int sub_resource_idx,
        struct wined3d_map_desc *map_desc, const struct wined3d_box *box, unsigned int flags)
{
    struct wined3d_cs_map *op;
    HRESULT hr;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 1);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_MAP;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->map_desc = map_desc;
    op->box = box;
    op->flags = flags;
    op->hr = &hr;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
#else  /* STAGING_CSMT */
    cs->ops->submit_and_wait(cs);
#endif /* STAGING_CSMT */

    return hr;
}

#if !defined(STAGING_CSMT)
static void wined3d_cs_exec_unmap(struct wined3d_cs *cs, const void *data)
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_unmap(struct wined3d_cs *cs, const void *data)
#endif /* STAGING_CSMT */
{
    const struct wined3d_cs_unmap *op = data;
    struct wined3d_resource *resource = op->resource;

    *op->hr = resource->resource_ops->resource_sub_resource_unmap(resource, op->sub_resource_idx);
#if defined(STAGING_CSMT)

    return sizeof(*op);
#endif /* STAGING_CSMT */
}

HRESULT wined3d_cs_unmap(struct wined3d_cs *cs, struct wined3d_resource *resource, unsigned int sub_resource_idx)
{
    struct wined3d_cs_unmap *op;
    HRESULT hr;

#if !defined(STAGING_CSMT)
    op = cs->ops->require_space(cs, sizeof(*op));
#else  /* STAGING_CSMT */
    op = cs->ops->require_space(cs, sizeof(*op), 1);
#endif /* STAGING_CSMT */
    op->opcode = WINED3D_CS_OP_UNMAP;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->hr = &hr;

#if !defined(STAGING_CSMT)
    cs->ops->submit(cs);
#else  /* STAGING_CSMT */
    cs->ops->submit_and_wait(cs);

    return hr;
}

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

static UINT wined3d_cs_exec_push_constants(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_push_constants *op = data;
    struct wined3d_device *device = cs->device;
    size_t size = FIELD_OFFSET(struct wined3d_cs_push_constants, constants[op->count * push_constant_info[op->p].size]);
    unsigned int context_count;
    unsigned int i;
    size_t offset;

    if (op->p == WINED3D_PUSH_CONSTANTS_VS_F)
        device->shader_backend->shader_update_float_vertex_constants(device, op->start_idx, op->count);
    else if (op->p == WINED3D_PUSH_CONSTANTS_PS_F)
        device->shader_backend->shader_update_float_pixel_constants(device, op->start_idx, op->count);

    offset = push_constant_info[op->p].offset + op->start_idx * push_constant_info[op->p].size;
    memcpy((BYTE *)&cs->state + offset, op->constants, op->count * push_constant_info[op->p].size);
    for (i = 0, context_count = device->context_count; i < context_count; ++i)
    {
        device->contexts[i]->constant_update_mask |= push_constant_info[op->p].mask;
    }

    return size;
}

void wined3d_cs_emit_push_constants(struct wined3d_cs *cs, enum wined3d_push_constants p,
        unsigned int start_idx, unsigned int count, const void *constants)
{
    struct wined3d_cs_push_constants *op;

    op = cs->ops->require_space(cs, FIELD_OFFSET(struct wined3d_cs_push_constants, constants[count * push_constant_info[p].size]), 0);
    op->opcode = WINED3D_CS_OP_PUSH_CONSTANTS;
    op->p = p;
    op->start_idx = start_idx;
    op->count = count;
    memcpy(op->constants, constants, count * push_constant_info[p].size);

    cs->ops->submit_delayed(cs);
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

    op = cs->ops->require_space(cs, sizeof(*op), 0);
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

    cs->ops->submit(cs);
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

    op = cs->ops->require_space(cs, sizeof(*op), 0);
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

    cs->ops->submit(cs);
}

static UINT wined3d_cs_exec_update_texture(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_update_texture *op = data;
    struct wined3d_context *context;

    context = context_acquire(cs->device, NULL, 0);
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

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_UPDATE_TEXTURE;
    op->src = src;
    op->dst = dst;

    wined3d_resource_acquire(&op->src->resource);
    wined3d_resource_acquire(&op->dst->resource);

    cs->ops->submit(cs);
}

static UINT wined3d_cs_exec_update_sub_resource(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_update_sub_resource *op = data;
    struct wined3d_const_bo_address addr;
    struct wined3d_context *context;
    struct wined3d_texture *texture;
    unsigned int width, height, depth, level;

    if (op->resource->type == WINED3D_RTYPE_BUFFER)
    {
        struct wined3d_buffer *buffer = buffer_from_resource(op->resource);
        HRESULT hr;

        if (FAILED(hr = wined3d_buffer_upload_data(buffer, op->box, op->data)))
            WARN("Failed to update buffer data, hr %#x.\n", hr);

        wined3d_resource_release(op->resource);

        return op->size;
    }

    texture = wined3d_texture_from_resource(op->resource);

    level = op->sub_resource_idx % texture->level_count;
    width = wined3d_texture_get_level_width(texture, level);
    height = wined3d_texture_get_level_height(texture, level);
    depth = wined3d_texture_get_level_depth(texture, level);

    addr.buffer_object = 0;
    addr.addr = op->data;

    context = context_acquire(texture->resource.device, NULL, 0);

    /* Only load the sub-resource for partial updates. */
    if (!op->box || (!op->box->left && !op->box->top && !op->box->front
            && op->box->right == width && op->box->bottom == height && op->box->back == depth))
        wined3d_texture_prepare_texture(texture, context, FALSE);
    else
        wined3d_texture_load_location(texture, op->sub_resource_idx, context, WINED3D_LOCATION_TEXTURE_RGB);
    wined3d_texture_bind_and_dirtify(texture, context, FALSE);

    wined3d_texture_upload_data(texture, op->sub_resource_idx, context, op->box, &addr, op->row_pitch, op->depth_pitch);

    context_release(context);

    wined3d_texture_validate_location(texture, op->sub_resource_idx, WINED3D_LOCATION_TEXTURE_RGB);
    wined3d_texture_invalidate_location(texture, op->sub_resource_idx, ~WINED3D_LOCATION_TEXTURE_RGB);

    wined3d_resource_release(op->resource);

    return op->size;
}

void wined3d_cs_emit_update_sub_resource(struct wined3d_cs *cs, struct wined3d_resource *resource,
        unsigned int sub_resource_idx, const struct wined3d_box *box, const void *data, unsigned int row_pitch,
        unsigned int depth_pitch)
{
    struct wined3d_cs_update_sub_resource *op;
    unsigned int update_w, update_h, update_d;
    size_t data_size, size;

    if (resource->type != WINED3D_RTYPE_BUFFER && resource->format_flags & WINED3DFMT_FLAG_BLOCKS)
        goto no_async;

    if (box)
    {
        update_w = box->right - box->left;
        update_h = box->bottom - box->top;
        update_d = box->back - box->front;
    }
    else if (resource->type != WINED3D_RTYPE_BUFFER)
    {
        struct wined3d_texture *texture = wined3d_texture_from_resource(resource);
        unsigned int level = sub_resource_idx % texture->level_count;
        update_w = wined3d_texture_get_level_width(texture, level);
        update_h = wined3d_texture_get_level_height(texture, level);
        update_d = wined3d_texture_get_level_depth(texture, level);
    }
    else
    {
        update_w = resource->size;
    }

    data_size = 0;
    switch (resource->type)
    {
        case WINED3D_RTYPE_TEXTURE_3D:
            data_size += max(update_d - 1, 0) * depth_pitch;
            /* fall-through */
        case WINED3D_RTYPE_TEXTURE_2D:
            data_size += max(update_h - 1, 0) * row_pitch;
            /* fall-through */
        case WINED3D_RTYPE_TEXTURE_1D:
            data_size += update_w * resource->format->byte_count;
            break;
        case WINED3D_RTYPE_BUFFER:
            data_size = update_w;
            break;
    }

    size = FIELD_OFFSET(struct wined3d_cs_update_sub_resource, copy_data[data_size]);
    if (size > sizeof(cs->current_block->data))
        goto no_async;

    op = cs->ops->require_space(cs, size, 0);
    op->opcode = WINED3D_CS_OP_UPDATE_SUB_RESOURCE;
    op->size = size;
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->box = box ? &op->copy_box : NULL;
    op->data = op->copy_data;
    op->row_pitch = row_pitch;
    op->depth_pitch = depth_pitch;

    if (box) op->copy_box = *box;
    memcpy(op->copy_data, data, data_size);

    wined3d_resource_acquire(resource);

    cs->ops->submit(cs);
    return;

no_async:
    wined3d_resource_wait_idle(resource);

    op = cs->ops->require_space(cs, sizeof(*op), 1);
    op->opcode = WINED3D_CS_OP_UPDATE_SUB_RESOURCE;
    op->size = sizeof(*op);
    op->resource = resource;
    op->sub_resource_idx = sub_resource_idx;
    op->box = box;
    op->data = data;
    op->row_pitch = row_pitch;
    op->depth_pitch = depth_pitch;

    wined3d_resource_acquire(resource);

    cs->ops->submit_and_wait(cs);
}

static UINT wined3d_cs_exec_get_dc(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_get_release_dc *op = data;

    *op->hr = wined3d_texture_get_dc_cs(op->texture, op->sub_resource_idx);

    return sizeof(*op);
}

HRESULT wined3d_cs_emit_get_dc(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx)
{
    struct wined3d_cs_get_release_dc *op;
    HRESULT hr;

    op = cs->ops->require_space(cs, sizeof(*op), 1);
    op->opcode = WINED3D_CS_OP_GET_DC;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;
    op->hr = &hr;

    cs->ops->submit_and_wait(cs);
#endif /* STAGING_CSMT */

    return hr;
}

#if !defined(STAGING_CSMT)
static void (* const wined3d_cs_op_handlers[])(struct wined3d_cs *cs, const void *data) =
{
#else  /* STAGING_CSMT */
static UINT wined3d_cs_exec_release_dc(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_get_release_dc *op = data;

    *op->hr = wined3d_texture_release_dc_cs(op->texture, op->sub_resource_idx);

    return sizeof(*op);
}

HRESULT wined3d_cs_emit_release_dc(struct wined3d_cs *cs, struct wined3d_texture *texture,
        unsigned int sub_resource_idx)
{
    struct wined3d_cs_get_release_dc *op;
    HRESULT hr;

    op = cs->ops->require_space(cs, sizeof(*op), 1);
    op->opcode = WINED3D_CS_OP_RELEASE_DC;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;
    op->hr = &hr;

    cs->ops->submit_and_wait(cs);

    return hr;
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

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_UPDATE_SWAP_INTERVAL;
    op->swapchain = swapchain;

    cs->ops->submit_and_wait(cs);
}

static UINT wined3d_cs_exec_texture_add_dirty_region(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_texture_add_dirty_region *op = data;
    struct wined3d_texture *texture = op->texture;
    struct wined3d_context *context;

    context = context_acquire(cs->device, NULL, 0);
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

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION;
    op->texture = texture;
    op->sub_resource_idx = sub_resource_idx;

    wined3d_resource_acquire(&texture->resource);

    cs->ops->submit(cs);
}

static UINT wined3d_cs_exec_buffer_copy(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_buffer_copy *op = data;
    HRESULT hr;

    if (FAILED(hr = wined3d_buffer_copy(op->dst_buffer, op->dst_offset, op->src_buffer, op->src_offset, op->size)))
        ERR("Failed to copy buffer, hr %#x.\n", hr);

    wined3d_resource_release(&op->dst_buffer->resource);
    wined3d_resource_release(&op->src_buffer->resource);

    return sizeof(*op);
}

void wined3d_cs_emit_buffer_copy(struct wined3d_cs *cs, struct wined3d_buffer *dst_buffer,
        unsigned int dst_offset, struct wined3d_buffer *src_buffer, unsigned int src_offset,
        unsigned int size)
{
    struct wined3d_cs_buffer_copy *op;

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_BUFFER_COPY;
    op->dst_buffer = dst_buffer;
    op->dst_offset = dst_offset;
    op->src_buffer = src_buffer;
    op->src_offset = src_offset;
    op->size = size;

    wined3d_resource_acquire(&dst_buffer->resource);
    wined3d_resource_acquire(&src_buffer->resource);

    cs->ops->submit(cs);
}

static UINT wined3d_cs_exec_map_vertex_buffers(struct wined3d_cs *cs, const void *data)
{
    const struct wined3d_cs_map_vertex_buffers *op = data;
    struct wined3d_state *state = &cs->device->state;
    const struct wined3d_gl_info *gl_info;
    struct wined3d_context *context;
    struct wined3d_shader *vs;
    unsigned int i;
    WORD map;

    /* Need any context to write to the vbo. */
    context = context_acquire(cs->device, NULL, 0);
    gl_info = context->gl_info;

    vs = state->shader[WINED3D_SHADER_TYPE_VERTEX];
    state->shader[WINED3D_SHADER_TYPE_VERTEX] = NULL;
    context_stream_info_from_declaration(context, state, op->stream_info);
    state->shader[WINED3D_SHADER_TYPE_VERTEX] = vs;

    /* We can't convert FROM a VBO, and vertex buffers used to source into
     * process_vertices() are unlikely to ever be used for drawing. Release
     * VBOs in those buffers and fix up the stream_info structure.
     *
     * Also apply the start index. */
    for (i = 0, map = op->stream_info->use_map; map; map >>= 1, ++i)
    {
        struct wined3d_stream_info_element *e;
        struct wined3d_buffer *buffer;

        if (!(map & 1))
            continue;

        e = &op->stream_info->elements[i];
        buffer = state->streams[e->stream_idx].buffer;
        e->data.buffer_object = 0;
        e->data.addr += (ULONG_PTR)wined3d_buffer_load_sysmem(buffer, context);
        if (buffer->buffer_object)
        {
            GL_EXTCALL(glDeleteBuffers(1, &buffer->buffer_object));
            buffer->buffer_object = 0;
            wined3d_buffer_invalidate_location(buffer, WINED3D_LOCATION_BUFFER);
        }
        if (e->data.addr)
            e->data.addr += e->stride * op->src_start_idx;
    }

    context_release(context);

    return sizeof(*op);
}

void wined3d_cs_emit_map_vertex_buffers(struct wined3d_cs *cs, UINT src_start_idx,
        struct wined3d_stream_info *stream_info)
{
    struct wined3d_cs_map_vertex_buffers *op;

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_MAP_VERTEX_BUFFERS;
    op->src_start_idx = src_start_idx;
    op->stream_info = stream_info;

    cs->ops->submit_and_wait(cs);
}

static UINT (* const wined3d_cs_op_handlers[])(struct wined3d_cs *cs, const void *data) =
{
    /* WINED3D_CS_OP_SYNC                       */ wined3d_cs_exec_sync,
    /* WINED3D_CS_OP_GLFINISH                   */ wined3d_cs_exec_glfinish,
#endif /* STAGING_CSMT */
    /* WINED3D_CS_OP_PRESENT                    */ wined3d_cs_exec_present,
    /* WINED3D_CS_OP_CLEAR                      */ wined3d_cs_exec_clear,
    /* WINED3D_CS_OP_DISPATCH                   */ wined3d_cs_exec_dispatch,
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
    /* WINED3D_CS_OP_SET_LIGHT                  */ wined3d_cs_exec_set_light,
    /* WINED3D_CS_OP_SET_LIGHT_ENABLE           */ wined3d_cs_exec_set_light_enable,
    /* WINED3D_CS_OP_RESET_STATE                */ wined3d_cs_exec_reset_state,
    /* WINED3D_CS_OP_CALLBACK                   */ wined3d_cs_exec_callback,
    /* WINED3D_CS_OP_QUERY_ISSUE                */ wined3d_cs_exec_query_issue,
#if defined(STAGING_CSMT)
    /* WINED3D_CS_OP_QUERY_POLL                 */ wined3d_cs_exec_query_poll,
#endif /* STAGING_CSMT */
    /* WINED3D_CS_OP_PRELOAD_RESOURCE           */ wined3d_cs_exec_preload_resource,
    /* WINED3D_CS_OP_UNLOAD_RESOURCE            */ wined3d_cs_exec_unload_resource,
    /* WINED3D_CS_OP_MAP                        */ wined3d_cs_exec_map,
    /* WINED3D_CS_OP_UNMAP                      */ wined3d_cs_exec_unmap,
#if !defined(STAGING_CSMT)
};

static void *wined3d_cs_st_require_space(struct wined3d_cs *cs, size_t size)
#else  /* STAGING_CSMT */
    /* WINED3D_CS_OP_PUSH_CONSTANTS             */ wined3d_cs_exec_push_constants,
    /* WINED3D_CS_OP_BLT                        */ wined3d_cs_exec_blt,
    /* WINED3D_CS_OP_CLEAR_RTV                  */ wined3d_cs_exec_clear_rtv,
    /* WINED3D_CS_OP_UPDATE_TEXTURE             */ wined3d_cs_exec_update_texture,
    /* WINED3D_CS_OP_UPDATE_SUB_RESOURCE        */ wined3d_cs_exec_update_sub_resource,
    /* WINED3D_CS_OP_GET_DC                     */ wined3d_cs_exec_get_dc,
    /* WINED3D_CS_OP_RELEASE_DC                 */ wined3d_cs_exec_release_dc,
    /* WINED3D_CS_OP_UPDATE_SWAP_INTERVAL       */ wined3d_cs_exec_update_swap_interval,
    /* WINED3D_CS_OP_TEXTURE_ADD_DIRTY_REGION   */ wined3d_cs_exec_texture_add_dirty_region,
    /* WINED3D_CS_OP_BUFFER_COPY                */ wined3d_cs_exec_buffer_copy,
    /* WINED3D_CS_OP_MAP_VERTEX_BUFFERS         */ wined3d_cs_exec_map_vertex_buffers,
};

static void *wined3d_cs_st_require_space(struct wined3d_cs *cs, size_t size, int priority)
#endif /* STAGING_CSMT */
{
    if (size > (cs->data_size - cs->end))
    {
        size_t new_size;
        void *new_data;

        new_size = max(size, cs->data_size * 2);
        if (!cs->end)
            new_data = HeapReAlloc(GetProcessHeap(), 0, cs->data, new_size);
        else
            new_data = HeapAlloc(GetProcessHeap(), 0, new_size);
        if (!new_data)
            return NULL;

        cs->data_size = new_size;
        cs->start = cs->end = 0;
        cs->data = new_data;
    }

    cs->end += size;

    return (BYTE *)cs->data + cs->start;
}

static void wined3d_cs_st_submit(struct wined3d_cs *cs)
{
    enum wined3d_cs_op opcode;
    size_t start;
    BYTE *data;

    data = cs->data;
    start = cs->start;
    cs->start = cs->end;

    opcode = *(const enum wined3d_cs_op *)&data[start];
    wined3d_cs_op_handlers[opcode](cs, &data[start]);

    if (cs->data == data)
        cs->start = cs->end = start;
    else if (!start)
        HeapFree(GetProcessHeap(), 0, data);
}

#if !defined(STAGING_CSMT)
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
#else  /* STAGING_CSMT */
static const struct wined3d_cs_ops wined3d_cs_st_ops =
{
    wined3d_cs_st_require_space,
    wined3d_cs_st_submit,
    wined3d_cs_st_submit,
    wined3d_cs_st_submit,
};

static void wined3d_cs_list_enqueue(struct wined3d_cs_list *list, struct wined3d_cs_block *block)
{
    EnterCriticalSection(&list->lock);
    list_add_tail(&list->blocks, &block->entry);
    LeaveCriticalSection(&list->lock);
    InterlockedIncrement(&list->count);
}

static struct wined3d_cs_block *wined3d_cs_list_dequeue(struct wined3d_cs_list *list)
{
    struct list *head;

    if (!list->count) return NULL;
    EnterCriticalSection(&list->lock);
    if (!(head = list_head(&list->blocks)))
    {
        LeaveCriticalSection(&list->lock);
        return NULL;
    }
    list_remove(head);
    LeaveCriticalSection(&list->lock);
    InterlockedDecrement(&list->count);

    return LIST_ENTRY(head, struct wined3d_cs_block, entry);
}

static void wined3d_cs_wait_event(struct wined3d_cs *cs)
{
    InterlockedExchange(&cs->waiting_for_event, TRUE);

    /* The main thread might enqueue a finish command and block on it
     * after the worker thread decided to enter wined3d_cs_wait_event
     * and before waiting_for_event was set to TRUE. Check again if
     * the queues are empty */
    if (cs->exec_list.count || cs->exec_prio_list.count)
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
#endif /* STAGING_CSMT */
    }
}

#if !defined(STAGING_CSMT)
static const struct wined3d_cs_ops wined3d_cs_st_ops =
{
    wined3d_cs_st_require_space,
    wined3d_cs_st_submit,
    wined3d_cs_st_push_constants,
};
#else  /* STAGING_CSMT */
static struct wined3d_cs_block *wined3d_cs_dequeue_command(struct wined3d_cs *cs)
{
    struct wined3d_cs_block *block;
    DWORD spin_count = 0;

    /* FIXME: Use an event to wait after a couple of spins. */
    for (;;)
    {
        if ((block = wined3d_cs_list_dequeue(&cs->exec_prio_list)))
            return block;
        if ((block = wined3d_cs_list_dequeue(&cs->exec_list)))
            return block;

        spin_count++;
        if (spin_count >= WINED3D_CS_SPIN_COUNT)
        {
            wined3d_cs_wait_event(cs);
            spin_count = 0;
        }
    }
}

static void wined3d_cs_list_init(struct wined3d_cs_list *list)
{
    InitializeCriticalSectionAndSpinCount(&list->lock, WINED3D_CS_SPIN_COUNT);
    list->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": wined3d_cs_list_lock");

    list_init(&list->blocks);
}

static void wined3d_cs_list_cleanup(struct wined3d_cs_list *list)
{
    struct wined3d_cs_block *block, *next;

    LIST_FOR_EACH_ENTRY_SAFE(block, next, &list->blocks, struct wined3d_cs_block, entry)
    {
        list_remove(&block->entry);
        HeapFree(GetProcessHeap(), 0, block);
    }

    list->lock.DebugInfo->Spare[0] = 0;
    DeleteCriticalSection(&list->lock);
}

static struct wined3d_cs_block *wined3d_cs_get_block(struct wined3d_cs *cs, struct wined3d_cs_list *list)
{
    struct wined3d_cs_block *block;

    while (!(block = wined3d_cs_list_dequeue(&cs->free_list)))
    {
        if (cs->num_blocks < 1024 &&  /* limit memory usage to about 16 MB */
            (block = HeapAlloc(GetProcessHeap(), 0, sizeof(*block))))
        {
            cs->num_blocks++;
            break;
        }
        while (!InterlockedCompareExchange(&cs->free_list.count, 0, 0));
    }

    block->pos = 0;
    block->list = list;
    block->fence = NULL;

    return block;
}

static void *wined3d_cs_mt_require_space(struct wined3d_cs *cs, size_t size, int priority)
{
    struct wined3d_cs_list *list = priority ? &cs->exec_prio_list : &cs->exec_list;
    struct wined3d_cs_block *block;
    void *data;

    if (cs->thread_id == GetCurrentThreadId())
        return wined3d_cs_st_require_space(cs, size, priority);

    assert(size <= sizeof(block->data));

    block = cs->current_block;
    if (!block || block->pos + size > sizeof(block->data) || block->list != list)
    {
        if (block) cs->ops->submit(cs);
        block = wined3d_cs_get_block(cs, list);
        cs->current_block = block;
    }

    data = &block->data[block->pos];
    block->pos += size;

    return data;
}

static void wined3d_cs_mt_submit(struct wined3d_cs *cs)
{
    struct wined3d_cs_block *block;

    if (cs->thread_id == GetCurrentThreadId())
        return wined3d_cs_st_submit(cs);

    block = cs->current_block;
    wined3d_cs_list_enqueue(block->list, block);
    cs->current_block = NULL;

    if (InterlockedCompareExchange(&cs->waiting_for_event, FALSE, TRUE))
        SetEvent(cs->event);
}

static void wined3d_cs_mt_submit_and_wait(struct wined3d_cs *cs)
{
    struct wined3d_cs_block *block;
    BOOL fence = FALSE;

    if (cs->thread_id == GetCurrentThreadId())
        return wined3d_cs_st_submit(cs);

    block = cs->current_block;
    block->fence = &fence;
    wined3d_cs_list_enqueue(block->list, block);
    cs->current_block = NULL;

    if (InterlockedCompareExchange(&cs->waiting_for_event, FALSE, TRUE))
        SetEvent(cs->event);

    /* A busy wait should be fine, we're not supposed to have to wait very
     * long. */
    while (!InterlockedCompareExchange(&fence, TRUE, TRUE));
}

static void wined3d_cs_mt_submit_delayed(struct wined3d_cs *cs)
{
    assert(cs->thread_id != GetCurrentThreadId());
}

static const struct wined3d_cs_ops wined3d_cs_mt_ops =
{
    wined3d_cs_mt_require_space,
    wined3d_cs_mt_submit,
    wined3d_cs_mt_submit_and_wait,
    wined3d_cs_mt_submit_delayed,
};

static void wined3d_cs_mt_emit_stop(struct wined3d_cs *cs)
{
    struct wined3d_cs_stop *op;

    assert(cs->thread_id != GetCurrentThreadId());
    assert(cs->ops == &wined3d_cs_mt_ops);

    op = cs->ops->require_space(cs, sizeof(*op), 0);
    op->opcode = WINED3D_CS_OP_STOP;

    cs->ops->submit(cs);
}

static inline BOOL wined3d_cs_process_command(struct wined3d_cs *cs, struct wined3d_cs_block *block)
{
    UINT pos = 0;
    BOOL ret = TRUE;

    while (pos < block->pos)
    {
        enum wined3d_cs_op opcode = *(const enum wined3d_cs_op *)&block->data[pos];

        if (opcode >= WINED3D_CS_OP_STOP)
        {
            if (opcode > WINED3D_CS_OP_STOP)
                ERR("Invalid opcode %#x.\n", opcode);
            ret = FALSE;
            break;
        }

        pos += wined3d_cs_op_handlers[opcode](cs, &block->data[pos]);
    }

    if (block->fence)
        InterlockedExchange(block->fence, TRUE);

    wined3d_cs_list_enqueue(&cs->free_list, block);
    return ret;
}

static DWORD WINAPI wined3d_cs_run(void *thread_param)
{
    struct wined3d_cs *cs = thread_param;

    TRACE("Started.\n");

    for (;;)
    {
        struct wined3d_cs_block *block;
        block = wined3d_cs_dequeue_command(cs);
        if (!wined3d_cs_process_command(cs, block))
            break;
    }

    TRACE("Stopped.\n");
    return 0;
}
#endif /* STAGING_CSMT */

struct wined3d_cs *wined3d_cs_create(struct wined3d_device *device)
{
    const struct wined3d_gl_info *gl_info = &device->adapter->gl_info;
    struct wined3d_cs *cs;

    if (!(cs = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*cs))))
        return NULL;

    if (!(cs->fb.render_targets = wined3d_calloc(gl_info->limits.buffers, sizeof(*cs->fb.render_targets))))
    {
        HeapFree(GetProcessHeap(), 0, cs);
        return NULL;
    }

    state_init(&cs->state, &cs->fb, gl_info, &device->adapter->d3d_info,
            WINED3D_STATE_NO_REF | WINED3D_STATE_INIT_DEFAULT);

    cs->ops = &wined3d_cs_st_ops;
    cs->device = device;

    cs->data_size = WINED3D_INITIAL_CS_SIZE;
    if (!(cs->data = HeapAlloc(GetProcessHeap(), 0, cs->data_size)))
    {
        state_cleanup(&cs->state);
        HeapFree(GetProcessHeap(), 0, cs->fb.render_targets);
        HeapFree(GetProcessHeap(), 0, cs);
        return NULL;
    }

#if defined(STAGING_CSMT)
    if (!(cs->event = CreateEventW(NULL, FALSE, FALSE, NULL)))
    {
        state_cleanup(&cs->state);
        HeapFree(GetProcessHeap(), 0, cs->data);
        HeapFree(GetProcessHeap(), 0, cs);
        return NULL;
    }

    if (wined3d_settings.cs_multithreaded)
    {
        cs->ops = &wined3d_cs_mt_ops;

        wined3d_cs_list_init(&cs->free_list);
        wined3d_cs_list_init(&cs->exec_list);
        wined3d_cs_list_init(&cs->exec_prio_list);

        if (!(cs->thread = CreateThread(NULL, 0, wined3d_cs_run, cs, 0, &cs->thread_id)))
        {
            ERR("Failed to create wined3d command stream thread.\n");
            state_cleanup(&cs->state);
            CloseHandle(cs->event);
            HeapFree(GetProcessHeap(), 0, cs->fb.render_targets);
            HeapFree(GetProcessHeap(), 0, cs->data);
            HeapFree(GetProcessHeap(), 0, cs);
            return NULL;
        }
    }

#endif /* STAGING_CSMT */
    return cs;
}

void wined3d_cs_destroy(struct wined3d_cs *cs)
{
    state_cleanup(&cs->state);
#if defined(STAGING_CSMT)

    if (wined3d_settings.cs_multithreaded)
    {
        wined3d_cs_mt_emit_stop(cs);
        WaitForSingleObject(cs->thread, INFINITE);
        CloseHandle(cs->thread);

        wined3d_cs_list_cleanup(&cs->exec_prio_list);
        wined3d_cs_list_cleanup(&cs->exec_list);
        wined3d_cs_list_cleanup(&cs->free_list);
    }

    CloseHandle(cs->event);
#endif /* STAGING_CSMT */
    HeapFree(GetProcessHeap(), 0, cs->fb.render_targets);
    HeapFree(GetProcessHeap(), 0, cs->data);
    HeapFree(GetProcessHeap(), 0, cs);
}
