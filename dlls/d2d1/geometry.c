/*
 * Copyright 2015 Henri Verbeet for CodeWeavers
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
#ifdef HAVE_FLOAT_H
#include <float.h>
#endif

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

#define D2D_CDT_EDGE_FLAG_FREED         0x80000000u
#define D2D_CDT_EDGE_FLAG_VISITED(r)    (1u << (r))

#define D2D_FP_EPS (1.0f / (1 << FLT_MANT_DIG))

static const D2D1_MATRIX_3X2_F identity =
{
    1.0f, 0.0f,
    0.0f, 1.0f,
    0.0f, 0.0f,
};

enum d2d_cdt_edge_next
{
    D2D_EDGE_NEXT_ORIGIN = 0,
    D2D_EDGE_NEXT_ROT = 1,
    D2D_EDGE_NEXT_SYM = 2,
    D2D_EDGE_NEXT_TOR = 3,
};

enum d2d_vertex_type
{
    D2D_VERTEX_TYPE_NONE,
    D2D_VERTEX_TYPE_LINE,
    D2D_VERTEX_TYPE_BEZIER,
};

struct d2d_figure
{
    D2D1_POINT_2F *vertices;
    size_t vertices_size;
    enum d2d_vertex_type *vertex_types;
    size_t vertex_types_size;
    size_t vertex_count;

    D2D1_POINT_2F *bezier_controls;
    size_t bezier_controls_size;
    size_t bezier_control_count;

    D2D1_RECT_F bounds;
};

struct d2d_cdt_edge_ref
{
    size_t idx;
    enum d2d_cdt_edge_next r;
};

struct d2d_cdt_edge
{
    struct d2d_cdt_edge_ref next[4];
    size_t vertex[2];
    unsigned int flags;
};

struct d2d_cdt
{
    struct d2d_cdt_edge *edges;
    size_t edges_size;
    size_t edge_count;
    size_t free_edge;

    const D2D1_POINT_2F *vertices;
};

struct d2d_geometry_intersection
{
    size_t figure_idx;
    size_t segment_idx;
    float t;
    D2D1_POINT_2F p;
};

struct d2d_geometry_intersections
{
    struct d2d_geometry_intersection *intersections;
    size_t intersections_size;
    size_t intersection_count;
};

struct d2d_fp_two_vec2
{
    float x[2];
    float y[2];
};

struct d2d_fp_fin
{
    float *now, *other;
    size_t length;
};

static void d2d_bezier_vertex_set(struct d2d_bezier_vertex *b,
        const D2D1_POINT_2F *p, float u, float v, float sign)
{
    b->position = *p;
    b->texcoord.u = u;
    b->texcoord.v = v;
    b->texcoord.sign = sign;
}

static void d2d_fp_two_sum(float *out, float a, float b)
{
    float a_virt, a_round, b_virt, b_round;

    out[1] = a + b;
    b_virt = out[1] - a;
    a_virt = out[1] - b_virt;
    b_round = b - b_virt;
    a_round = a - a_virt;
    out[0] = a_round + b_round;
}

static void d2d_fp_fast_two_sum(float *out, float a, float b)
{
    float b_virt;

    out[1] = a + b;
    b_virt = out[1] - a;
    out[0] = b - b_virt;
}

static void d2d_fp_two_two_sum(float *out, const float *a, const float *b)
{
    float sum[2];

    d2d_fp_two_sum(out, a[0], b[0]);
    d2d_fp_two_sum(sum, a[1], out[1]);
    d2d_fp_two_sum(&out[1], sum[0], b[1]);
    d2d_fp_two_sum(&out[2], sum[1], out[2]);
}

static void d2d_fp_two_diff_tail(float *out, float a, float b, float x)
{
    float a_virt, a_round, b_virt, b_round;

    b_virt = a - x;
    a_virt = x + b_virt;
    b_round = b_virt - b;
    a_round = a - a_virt;
    *out = a_round + b_round;
}

static void d2d_fp_two_two_diff(float *out, const float *a, const float *b)
{
    float sum[2], diff;

    diff = a[0] - b[0];
    d2d_fp_two_diff_tail(out, a[0], b[0], diff);
    d2d_fp_two_sum(sum, a[1], diff);
    diff = sum[0] - b[1];
    d2d_fp_two_diff_tail(&out[1], sum[0], b[1], diff);
    d2d_fp_two_sum(&out[2], sum[1], diff);
}

static void d2d_fp_split(float *out, float a)
{
    float a_big, c;

    c = a * ((1 << (FLT_MANT_DIG / 2)) + 1.0f);
    a_big = c - a;
    out[1] = c - a_big;
    out[0] = a - out[1];
}

static void d2d_fp_two_product_presplit(float *out, float a, float b, const float *b_split)
{
    float a_split[2], err1, err2, err3;

    out[1] = a * b;
    d2d_fp_split(a_split, a);
    err1 = out[1] - (a_split[1] * b_split[1]);
    err2 = err1 - (a_split[0] * b_split[1]);
    err3 = err2 - (a_split[1] * b_split[0]);
    out[0] = (a_split[0] * b_split[0]) - err3;
}

static void d2d_fp_two_product(float *out, float a, float b)
{
    float b_split[2];

    d2d_fp_split(b_split, b);
    d2d_fp_two_product_presplit(out, a, b, b_split);
}

static void d2d_fp_square(float *out, float a)
{
    float a_split[2], err1, err2;

    out[1] = a * a;
    d2d_fp_split(a_split, a);
    err1 = out[1] - (a_split[1] * a_split[1]);
    err2 = err1 - ((a_split[1] + a_split[1]) * a_split[0]);
    out[0] = (a_split[0] * a_split[0]) - err2;
}

static float d2d_fp_estimate(float *a, size_t len)
{
    float out = a[0];
    size_t idx = 1;

    while (idx < len)
        out += a[idx++];

    return out;
}

static void d2d_fp_fast_expansion_sum_zeroelim(float *out, size_t *out_len,
        const float *a, size_t a_len, const float *b, size_t b_len)
{
    float sum[2], q, a_curr, b_curr;
    size_t a_idx, b_idx, out_idx;

    a_curr = a[0];
    b_curr = b[0];
    a_idx = b_idx = 0;
    if ((b_curr > a_curr) == (b_curr > -a_curr))
    {
        q = a_curr;
        a_curr = a[++a_idx];
    }
    else
    {
        q = b_curr;
        b_curr = b[++b_idx];
    }
    out_idx = 0;
    if (a_idx < a_len && b_idx < b_len)
    {
        if ((b_curr > a_curr) == (b_curr > -a_curr))
        {
            d2d_fp_fast_two_sum(sum, a_curr, q);
            a_curr = a[++a_idx];
        }
        else
        {
            d2d_fp_fast_two_sum(sum, b_curr, q);
            b_curr = b[++b_idx];
        }
        if (sum[0] != 0.0f)
            out[out_idx++] = sum[0];
        q = sum[1];
        while (a_idx < a_len && b_idx < b_len)
        {
            if ((b_curr > a_curr) == (b_curr > -a_curr))
            {
                d2d_fp_two_sum(sum, q, a_curr);
                a_curr = a[++a_idx];
            }
            else
            {
                d2d_fp_two_sum(sum, q, b_curr);
                b_curr = b[++b_idx];
            }
            if (sum[0] != 0.0f)
                out[out_idx++] = sum[0];
            q = sum[1];
        }
    }
    while (a_idx < a_len)
    {
        d2d_fp_two_sum(sum, q, a_curr);
        a_curr = a[++a_idx];
        if (sum[0] != 0.0f)
            out[out_idx++] = sum[0];
        q = sum[1];
    }
    while (b_idx < b_len)
    {
        d2d_fp_two_sum(sum, q, b_curr);
        b_curr = b[++b_idx];
        if (sum[0] != 0.0f)
            out[out_idx++] = sum[0];
        q = sum[1];
    }
    if (q != 0.0f || !out_idx)
        out[out_idx++] = q;

    *out_len = out_idx;
}

static void d2d_fp_scale_expansion_zeroelim(float *out, size_t *out_len, const float *a, size_t a_len, float b)
{
    float product[2], sum[2], b_split[2], q[2], a_curr;
    size_t a_idx, out_idx;

    d2d_fp_split(b_split, b);
    d2d_fp_two_product_presplit(q, a[0], b, b_split);
    out_idx = 0;
    if (q[0] != 0.0f)
        out[out_idx++] = q[0];
    for (a_idx = 1; a_idx < a_len; ++a_idx)
    {
        a_curr = a[a_idx];
        d2d_fp_two_product_presplit(product, a_curr, b, b_split);
        d2d_fp_two_sum(sum, q[1], product[0]);
        if (sum[0] != 0.0f)
            out[out_idx++] = sum[0];
        d2d_fp_fast_two_sum(q, product[1], sum[1]);
        if (q[0] != 0.0f)
            out[out_idx++] = q[0];
    }
    if (q[1] != 0.0f || !out_idx)
        out[out_idx++] = q[1];

    *out_len = out_idx;
}

static void d2d_point_subtract(D2D1_POINT_2F *out,
        const D2D1_POINT_2F *a, const D2D1_POINT_2F *b)
{
    out->x = a->x - b->x;
    out->y = a->y - b->y;
}

/* This implementation is based on the paper "Adaptive Precision
 * Floating-Point Arithmetic and Fast Robust Geometric Predicates" and
 * associated (Public Domain) code by Jonathan Richard Shewchuk. */
static float d2d_point_ccw(const D2D1_POINT_2F *a, const D2D1_POINT_2F *b, const D2D1_POINT_2F *c)
{
    static const float err_bound_result = (3.0f + 8.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_a = (3.0f + 16.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_b = (2.0f + 12.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_c = (9.0f + 64.0f * D2D_FP_EPS) * D2D_FP_EPS * D2D_FP_EPS;
    float det_d[16], det_c2[12], det_c1[8], det_b[4], temp4[4], temp2a[2], temp2b[2], abxacy[2], abyacx[2];
    size_t det_d_len, det_c2_len, det_c1_len;
    float det, det_sum, err_bound;
    struct d2d_fp_two_vec2 ab, ac;

    ab.x[1] = b->x - a->x;
    ab.y[1] = b->y - a->y;
    ac.x[1] = c->x - a->x;
    ac.y[1] = c->y - a->y;

    abxacy[1] = ab.x[1] * ac.y[1];
    abyacx[1] = ab.y[1] * ac.x[1];
    det = abxacy[1] - abyacx[1];

    if (abxacy[1] > 0.0f)
    {
        if (abyacx[1] <= 0.0f)
            return det;
        det_sum = abxacy[1] + abyacx[1];
    }
    else if (abxacy[1] < 0.0f)
    {
        if (abyacx[1] >= 0.0f)
            return det;
        det_sum = -abxacy[1] - abyacx[1];
    }
    else
    {
        return det;
    }

    err_bound = err_bound_a * det_sum;
    if (det >= err_bound || -det >= err_bound)
        return det;

    d2d_fp_two_product(abxacy, ab.x[1], ac.y[1]);
    d2d_fp_two_product(abyacx, ab.y[1], ac.x[1]);
    d2d_fp_two_two_diff(det_b, abxacy, abyacx);

    det = d2d_fp_estimate(det_b, 4);
    err_bound = err_bound_b * det_sum;
    if (det >= err_bound || -det >= err_bound)
        return det;

    d2d_fp_two_diff_tail(&ab.x[0], b->x, a->x, ab.x[1]);
    d2d_fp_two_diff_tail(&ab.y[0], b->y, a->y, ab.y[1]);
    d2d_fp_two_diff_tail(&ac.x[0], c->x, a->x, ac.x[1]);
    d2d_fp_two_diff_tail(&ac.y[0], c->y, a->y, ac.y[1]);

    if (ab.x[0] == 0.0f && ab.y[0] == 0.0f && ac.x[0] == 0.0f && ac.y[0] == 0.0f)
        return det;

    err_bound = err_bound_c * det_sum + err_bound_result * fabsf(det);
    det += (ab.x[1] * ac.y[0] + ac.y[1] * ab.x[0]) - (ab.y[1] * ac.x[0] + ac.x[1] * ab.y[0]);
    if (det >= err_bound || -det >= err_bound)
        return det;

    d2d_fp_two_product(temp2a, ab.x[0], ac.y[1]);
    d2d_fp_two_product(temp2b, ab.y[0], ac.x[1]);
    d2d_fp_two_two_diff(temp4, temp2a, temp2b);
    d2d_fp_fast_expansion_sum_zeroelim(det_c1, &det_c1_len, det_b, 4, temp4, 4);

    d2d_fp_two_product(temp2a, ab.x[1], ac.y[0]);
    d2d_fp_two_product(temp2b, ab.y[1], ac.x[0]);
    d2d_fp_two_two_diff(temp4, temp2a, temp2b);
    d2d_fp_fast_expansion_sum_zeroelim(det_c2, &det_c2_len, det_c1, det_c1_len, temp4, 4);

    d2d_fp_two_product(temp2a, ab.x[0], ac.y[0]);
    d2d_fp_two_product(temp2b, ab.y[0], ac.x[0]);
    d2d_fp_two_two_diff(temp4, temp2a, temp2b);
    d2d_fp_fast_expansion_sum_zeroelim(det_d, &det_d_len, det_c2, det_c2_len, temp4, 4);

    return det_d[det_d_len - 1];
}

static BOOL d2d_array_reserve(void **elements, size_t *capacity, size_t element_count, size_t element_size)
{
    size_t new_capacity, max_capacity;
    void *new_elements;

    if (element_count <= *capacity)
        return TRUE;

    max_capacity = ~(size_t)0 / element_size;
    if (max_capacity < element_count)
        return FALSE;

    new_capacity = max(*capacity, 4);
    while (new_capacity < element_count && new_capacity <= max_capacity / 2)
        new_capacity *= 2;

    if (new_capacity < element_count)
        new_capacity = max_capacity;

    if (*elements)
        new_elements = HeapReAlloc(GetProcessHeap(), 0, *elements, new_capacity * element_size);
    else
        new_elements = HeapAlloc(GetProcessHeap(), 0, new_capacity * element_size);

    if (!new_elements)
        return FALSE;

    *elements = new_elements;
    *capacity = new_capacity;
    return TRUE;
}

static void d2d_figure_update_bounds(struct d2d_figure *figure, D2D1_POINT_2F vertex)
{
    if (vertex.x < figure->bounds.left)
        figure->bounds.left = vertex.x;
    if (vertex.x > figure->bounds.right)
        figure->bounds.right = vertex.x;
    if (vertex.y < figure->bounds.top)
        figure->bounds.top = vertex.y;
    if (vertex.y > figure->bounds.bottom)
        figure->bounds.bottom = vertex.y;
}

static BOOL d2d_figure_insert_vertex(struct d2d_figure *figure, size_t idx, D2D1_POINT_2F vertex)
{
    if (!d2d_array_reserve((void **)&figure->vertices, &figure->vertices_size,
            figure->vertex_count + 1, sizeof(*figure->vertices)))
    {
        ERR("Failed to grow vertices array.\n");
        return FALSE;
    }

    if (!d2d_array_reserve((void **)&figure->vertex_types, &figure->vertex_types_size,
            figure->vertex_count + 1, sizeof(*figure->vertex_types)))
    {
        ERR("Failed to grow vertex types array.\n");
        return FALSE;
    }

    memmove(&figure->vertices[idx + 1], &figure->vertices[idx],
            (figure->vertex_count - idx) * sizeof(*figure->vertices));
    memmove(&figure->vertex_types[idx + 1], &figure->vertex_types[idx],
            (figure->vertex_count - idx) * sizeof(*figure->vertex_types));
    figure->vertices[idx] = vertex;
    figure->vertex_types[idx] = D2D_VERTEX_TYPE_NONE;
    d2d_figure_update_bounds(figure, vertex);
    ++figure->vertex_count;
    return TRUE;
}

static BOOL d2d_figure_add_vertex(struct d2d_figure *figure, D2D1_POINT_2F vertex)
{
    if (!d2d_array_reserve((void **)&figure->vertices, &figure->vertices_size,
            figure->vertex_count + 1, sizeof(*figure->vertices)))
    {
        ERR("Failed to grow vertices array.\n");
        return FALSE;
    }

    if (!d2d_array_reserve((void **)&figure->vertex_types, &figure->vertex_types_size,
            figure->vertex_count + 1, sizeof(*figure->vertex_types)))
    {
        ERR("Failed to grow vertex types array.\n");
        return FALSE;
    }

    figure->vertices[figure->vertex_count] = vertex;
    figure->vertex_types[figure->vertex_count] = D2D_VERTEX_TYPE_NONE;
    d2d_figure_update_bounds(figure, vertex);
    ++figure->vertex_count;
    return TRUE;
}

static BOOL d2d_figure_add_bezier_control(struct d2d_figure *figure, const D2D1_POINT_2F *p)
{
    if (!d2d_array_reserve((void **)&figure->bezier_controls, &figure->bezier_controls_size,
            figure->bezier_control_count + 1, sizeof(*figure->bezier_controls)))
    {
        ERR("Failed to grow bezier controls array.\n");
        return FALSE;
    }

    figure->bezier_controls[figure->bezier_control_count++] = *p;

    return TRUE;
}

static void d2d_cdt_edge_rot(struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    dst->idx = src->idx;
    dst->r = (src->r + D2D_EDGE_NEXT_ROT) & 3;
}

static void d2d_cdt_edge_sym(struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    dst->idx = src->idx;
    dst->r = (src->r + D2D_EDGE_NEXT_SYM) & 3;
}

static void d2d_cdt_edge_tor(struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    dst->idx = src->idx;
    dst->r = (src->r + D2D_EDGE_NEXT_TOR) & 3;
}

static void d2d_cdt_edge_next_left(const struct d2d_cdt *cdt,
        struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    d2d_cdt_edge_rot(dst, &cdt->edges[src->idx].next[(src->r + D2D_EDGE_NEXT_TOR) & 3]);
}

static void d2d_cdt_edge_next_origin(const struct d2d_cdt *cdt,
        struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    *dst = cdt->edges[src->idx].next[src->r];
}

static void d2d_cdt_edge_prev_origin(const struct d2d_cdt *cdt,
        struct d2d_cdt_edge_ref *dst, const struct d2d_cdt_edge_ref *src)
{
    d2d_cdt_edge_rot(dst, &cdt->edges[src->idx].next[(src->r + D2D_EDGE_NEXT_ROT) & 3]);
}

static size_t d2d_cdt_edge_origin(const struct d2d_cdt *cdt, const struct d2d_cdt_edge_ref *e)
{
    return cdt->edges[e->idx].vertex[e->r >> 1];
}

static size_t d2d_cdt_edge_destination(const struct d2d_cdt *cdt, const struct d2d_cdt_edge_ref *e)
{
    return cdt->edges[e->idx].vertex[!(e->r >> 1)];
}

static void d2d_cdt_edge_set_origin(const struct d2d_cdt *cdt,
        const struct d2d_cdt_edge_ref *e, size_t vertex)
{
    cdt->edges[e->idx].vertex[e->r >> 1] = vertex;
}

static void d2d_cdt_edge_set_destination(const struct d2d_cdt *cdt,
        const struct d2d_cdt_edge_ref *e, size_t vertex)
{
    cdt->edges[e->idx].vertex[!(e->r >> 1)] = vertex;
}

static float d2d_cdt_ccw(const struct d2d_cdt *cdt, size_t a, size_t b, size_t c)
{
    return d2d_point_ccw(&cdt->vertices[a], &cdt->vertices[b], &cdt->vertices[c]);
}

static BOOL d2d_cdt_rightof(const struct d2d_cdt *cdt, size_t p, const struct d2d_cdt_edge_ref *e)
{
    return d2d_cdt_ccw(cdt, p, d2d_cdt_edge_destination(cdt, e), d2d_cdt_edge_origin(cdt, e)) > 0.0f;
}

static BOOL d2d_cdt_leftof(const struct d2d_cdt *cdt, size_t p, const struct d2d_cdt_edge_ref *e)
{
    return d2d_cdt_ccw(cdt, p, d2d_cdt_edge_origin(cdt, e), d2d_cdt_edge_destination(cdt, e)) > 0.0f;
}

/* |ax ay|
 * |bx by| */
static void d2d_fp_four_det2x2(float *out, float ax, float ay, float bx, float by)
{
    float axby[2], aybx[2];

    d2d_fp_two_product(axby, ax, by);
    d2d_fp_two_product(aybx, ay, bx);
    d2d_fp_two_two_diff(out, axby, aybx);
}

/* (a->x² + a->y²) * det2x2 */
static void d2d_fp_sub_det3x3(float *out, size_t *out_len, const struct d2d_fp_two_vec2 *a, const float *det2x2)
{
    size_t axd_len, ayd_len, axxd_len, ayyd_len;
    float axd[8], ayd[8], axxd[16], ayyd[16];

    d2d_fp_scale_expansion_zeroelim(axd, &axd_len, det2x2, 4, a->x[1]);
    d2d_fp_scale_expansion_zeroelim(axxd, &axxd_len, axd, axd_len, a->x[1]);
    d2d_fp_scale_expansion_zeroelim(ayd, &ayd_len, det2x2, 4, a->y[1]);
    d2d_fp_scale_expansion_zeroelim(ayyd, &ayyd_len, ayd, ayd_len, a->y[1]);
    d2d_fp_fast_expansion_sum_zeroelim(out, out_len, axxd, axxd_len, ayyd, ayyd_len);
}

/* det_abt = det_ab * c[0]
 * fin += c[0] * (az * b - bz * a + c[1] * det_ab * 2.0f) */
static void d2d_cdt_incircle_refine1(struct d2d_fp_fin *fin, float *det_abt, size_t *det_abt_len,
        const float *det_ab, float a, const float *az, float b, const float *bz, const float *c)
{
    size_t temp48_len, temp32_len, temp16a_len, temp16b_len, temp16c_len, temp8_len;
    float temp48[48], temp32[32], temp16a[16], temp16b[16], temp16c[16], temp8[8];
    float *swap;

    d2d_fp_scale_expansion_zeroelim(det_abt, det_abt_len, det_ab, 4, c[0]);
    d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, det_abt, *det_abt_len, 2.0f * c[1]);
    d2d_fp_scale_expansion_zeroelim(temp8, &temp8_len, az, 4, c[0]);
    d2d_fp_scale_expansion_zeroelim(temp16b, &temp16b_len, temp8, temp8_len, b);
    d2d_fp_scale_expansion_zeroelim(temp8, &temp8_len, bz, 4, c[0]);
    d2d_fp_scale_expansion_zeroelim(temp16c, &temp16c_len, temp8, temp8_len, -a);
    d2d_fp_fast_expansion_sum_zeroelim(temp32, &temp32_len, temp16a, temp16a_len, temp16b, temp16b_len);
    d2d_fp_fast_expansion_sum_zeroelim(temp48, &temp48_len, temp16c, temp16c_len, temp32, temp32_len);
    d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp48, temp48_len);
    swap = fin->now; fin->now = fin->other; fin->other = swap;
}

static void d2d_cdt_incircle_refine2(struct d2d_fp_fin *fin, const struct d2d_fp_two_vec2 *a,
        const struct d2d_fp_two_vec2 *b, const float *bz, const struct d2d_fp_two_vec2 *c, const float *cz,
        const float *axt_det_bc, size_t axt_det_bc_len, const float *ayt_det_bc, size_t ayt_det_bc_len)
{
    size_t temp64_len, temp48_len, temp32a_len, temp32b_len, temp16a_len, temp16b_len, temp8_len;
    float temp64[64], temp48[48], temp32a[32], temp32b[32], temp16a[16], temp16b[16], temp8[8];
    float bct[8], bctt[4], temp4a[4], temp4b[4], temp2a[2], temp2b[2];
    size_t bct_len, bctt_len;
    float *swap;

    /* bct = (b->x[0] * c->y[1] + b->x[1] * c->y[0]) - (c->x[0] * b->y[1] + c->x[1] * b->y[0]) */
    /* bctt = b->x[0] * c->y[0] + c->x[0] * b->y[0] */
    if (b->x[0] != 0.0f || b->y[0] != 0.0f || c->x[0] != 0.0f || c->y[0] != 0.0f)
    {
        d2d_fp_two_product(temp2a, b->x[0], c->y[1]);
        d2d_fp_two_product(temp2b, b->x[1], c->y[0]);
        d2d_fp_two_two_sum(temp4a, temp2a, temp2b);
        d2d_fp_two_product(temp2a, c->x[0], -b->y[1]);
        d2d_fp_two_product(temp2b, c->x[1], -b->y[0]);
        d2d_fp_two_two_sum(temp4b, temp2a, temp2b);
        d2d_fp_fast_expansion_sum_zeroelim(bct, &bct_len, temp4a, 4, temp4b, 4);

        d2d_fp_two_product(temp2a, b->x[0], c->y[0]);
        d2d_fp_two_product(temp2b, c->x[0], b->y[0]);
        d2d_fp_two_two_diff(bctt, temp2a, temp2b);
        bctt_len = 4;
    }
    else
    {
        bct[0] = 0.0f;
        bct_len = 1;
        bctt[0] = 0.0f;
        bctt_len = 1;
    }

    if (a->x[0] != 0.0f)
    {
        size_t axt_bct_len, axt_bctt_len;
        float axt_bct[16], axt_bctt[8];

        /* fin += a->x[0] * (axt_det_bc + bct * 2.0f * a->x[1]) */
        d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, axt_det_bc, axt_det_bc_len, a->x[0]);
        d2d_fp_scale_expansion_zeroelim(axt_bct, &axt_bct_len, bct, bct_len, a->x[0]);
        d2d_fp_scale_expansion_zeroelim(temp32a, &temp32a_len, axt_bct, axt_bct_len, 2.0f * a->x[1]);
        d2d_fp_fast_expansion_sum_zeroelim(temp48, &temp48_len, temp16a, temp16a_len, temp32a, temp32a_len);
        d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp48, temp48_len);
        swap = fin->now; fin->now = fin->other; fin->other = swap;

        if (b->y[0] != 0.0f)
        {
            /* fin += a->x[0] * cz * b->y[0] */
            d2d_fp_scale_expansion_zeroelim(temp8, &temp8_len, cz, 4, a->x[0]);
            d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, temp8, temp8_len, b->y[0]);
            d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp16a, temp16a_len);
            swap = fin->now; fin->now = fin->other; fin->other = swap;
        }

        if (c->y[0] != 0.0f)
        {
            /* fin -= a->x[0] * bz * c->y[0] */
            d2d_fp_scale_expansion_zeroelim(temp8, &temp8_len, bz, 4, -a->x[0]);
            d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, temp8, temp8_len, c->y[0]);
            d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp16a, temp16a_len);
            swap = fin->now; fin->now = fin->other; fin->other = swap;
        }

        /* fin += a->x[0] * (bct * a->x[0] + bctt * (2.0f * a->x[1] + a->x[0])) */
        d2d_fp_scale_expansion_zeroelim(temp32a, &temp32a_len, axt_bct, axt_bct_len, a->x[0]);
        d2d_fp_scale_expansion_zeroelim(axt_bctt, &axt_bctt_len, bctt, bctt_len, a->x[0]);
        d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, axt_bctt, axt_bctt_len, 2.0f * a->x[1]);
        d2d_fp_scale_expansion_zeroelim(temp16b, &temp16b_len, axt_bctt, axt_bctt_len, a->x[0]);
        d2d_fp_fast_expansion_sum_zeroelim(temp32b, &temp32b_len, temp16a, temp16a_len, temp16b, temp16b_len);
        d2d_fp_fast_expansion_sum_zeroelim(temp64, &temp64_len, temp32a, temp32a_len, temp32b, temp32b_len);
        d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp64, temp64_len);
        swap = fin->now; fin->now = fin->other; fin->other = swap;
    }

    if (a->y[0] != 0.0f)
    {
        size_t ayt_bct_len, ayt_bctt_len;
        float ayt_bct[16], ayt_bctt[8];

        /* fin += a->y[0] * (ayt_det_bc + bct * 2.0f * a->y[1]) */
        d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, ayt_det_bc, ayt_det_bc_len, a->y[0]);
        d2d_fp_scale_expansion_zeroelim(ayt_bct, &ayt_bct_len, bct, bct_len, a->y[0]);
        d2d_fp_scale_expansion_zeroelim(temp32a, &temp32a_len, ayt_bct, ayt_bct_len, 2.0f * a->y[1]);
        d2d_fp_fast_expansion_sum_zeroelim(temp48, &temp48_len, temp16a, temp16a_len, temp32a, temp32a_len);
        d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp48, temp48_len);
        swap = fin->now; fin->now = fin->other; fin->other = swap;

        /* fin += a->y[0] * (bct * a->y[0] + bctt * (2.0f * a->y[1] + a->y[0])) */
        d2d_fp_scale_expansion_zeroelim(temp32a, &temp32a_len, ayt_bct, ayt_bct_len, a->y[0]);
        d2d_fp_scale_expansion_zeroelim(ayt_bctt, &ayt_bctt_len, bctt, bctt_len, a->y[0]);
        d2d_fp_scale_expansion_zeroelim(temp16a, &temp16a_len, ayt_bctt, ayt_bctt_len, 2.0f * a->y[1]);
        d2d_fp_scale_expansion_zeroelim(temp16b, &temp16b_len, ayt_bctt, ayt_bctt_len, a->y[0]);
        d2d_fp_fast_expansion_sum_zeroelim(temp32b, &temp32b_len, temp16a, temp16a_len, temp16b, temp16b_len);
        d2d_fp_fast_expansion_sum_zeroelim(temp64, &temp64_len, temp32a, temp32a_len, temp32b, temp32b_len);
        d2d_fp_fast_expansion_sum_zeroelim(fin->other, &fin->length, fin->now, fin->length, temp64, temp64_len);
        swap = fin->now; fin->now = fin->other; fin->other = swap;
    }
}

/* Determine if point D is inside or outside the circle defined by points A,
 * B, C. As explained in the paper by Guibas and Stolfi, this is equivalent to
 * calculating the signed volume of the tetrahedron defined by projecting the
 * points onto the paraboloid of revolution x = x² + y²,
 * λ:(x, y) → (x, y, x² + y²). I.e., D is inside the cirlce if
 *
 * |λ(A) 1|
 * |λ(B) 1| > 0
 * |λ(C) 1|
 * |λ(D) 1|
 *
 * After translating D to the origin, that becomes:
 *
 * |λ(A-D)|
 * |λ(B-D)| > 0
 * |λ(C-D)|
 *
 * This implementation is based on the paper "Adaptive Precision
 * Floating-Point Arithmetic and Fast Robust Geometric Predicates" and
 * associated (Public Domain) code by Jonathan Richard Shewchuk. */
static BOOL d2d_cdt_incircle(const struct d2d_cdt *cdt, size_t a, size_t b, size_t c, size_t d)
{
    static const float err_bound_result = (3.0f + 8.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_a = (10.0f + 96.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_b = (4.0f + 48.0f * D2D_FP_EPS) * D2D_FP_EPS;
    static const float err_bound_c = (44.0f + 576.0f * D2D_FP_EPS) * D2D_FP_EPS * D2D_FP_EPS;

    size_t axt_det_bc_len, ayt_det_bc_len, bxt_det_ca_len, byt_det_ca_len, cxt_det_ab_len, cyt_det_ab_len;
    float axt_det_bc[8], ayt_det_bc[8], bxt_det_ca[8], byt_det_ca[8], cxt_det_ab[8], cyt_det_ab[8];
    float fin1[1152], fin2[1152], temp64[64], sub_det_a[32], sub_det_b[32], sub_det_c[32];
    float det_bc[4], det_ca[4], det_ab[4], daz[4], dbz[4], dcz[4], temp2a[2], temp2b[2];
    size_t temp64_len, sub_det_a_len, sub_det_b_len, sub_det_c_len;
    float dbxdcy, dbydcx, dcxday, dcydax, daxdby, daydbx;
    const D2D1_POINT_2F *p = cdt->vertices;
    struct d2d_fp_two_vec2 da, db, dc;
    float permanent, err_bound, det;
    struct d2d_fp_fin fin;

    da.x[1] = p[a].x - p[d].x;
    da.y[1] = p[a].y - p[d].y;
    db.x[1] = p[b].x - p[d].x;
    db.y[1] = p[b].y - p[d].y;
    dc.x[1] = p[c].x - p[d].x;
    dc.y[1] = p[c].y - p[d].y;

    daz[3] = da.x[1] * da.x[1] + da.y[1] * da.y[1];
    dbxdcy = db.x[1] * dc.y[1];
    dbydcx = db.y[1] * dc.x[1];

    dbz[3] = db.x[1] * db.x[1] + db.y[1] * db.y[1];
    dcxday = dc.x[1] * da.y[1];
    dcydax = dc.y[1] * da.x[1];

    dcz[3] = dc.x[1] * dc.x[1] + dc.y[1] * dc.y[1];
    daxdby = da.x[1] * db.y[1];
    daydbx = da.y[1] * db.x[1];

    det = daz[3] * (dbxdcy - dbydcx) + dbz[3] * (dcxday - dcydax) + dcz[3] * (daxdby - daydbx);
    permanent = daz[3] * (fabsf(dbxdcy) + fabsf(dbydcx))
            + dbz[3] * (fabsf(dcxday) + fabsf(dcydax))
            + dcz[3] * (fabsf(daxdby) + fabsf(daydbx));
    err_bound = err_bound_a * permanent;
    if (det > err_bound || -det > err_bound)
        return det > 0.0f;

    fin.now = fin1;
    fin.other = fin2;

    d2d_fp_four_det2x2(det_bc, db.x[1], db.y[1], dc.x[1], dc.y[1]);
    d2d_fp_sub_det3x3(sub_det_a, &sub_det_a_len, &da, det_bc);

    d2d_fp_four_det2x2(det_ca, dc.x[1], dc.y[1], da.x[1], da.y[1]);
    d2d_fp_sub_det3x3(sub_det_b, &sub_det_b_len, &db, det_ca);

    d2d_fp_four_det2x2(det_ab, da.x[1], da.y[1], db.x[1], db.y[1]);
    d2d_fp_sub_det3x3(sub_det_c, &sub_det_c_len, &dc, det_ab);

    d2d_fp_fast_expansion_sum_zeroelim(temp64, &temp64_len, sub_det_a, sub_det_a_len, sub_det_b, sub_det_b_len);
    d2d_fp_fast_expansion_sum_zeroelim(fin.now, &fin.length, temp64, temp64_len, sub_det_c, sub_det_c_len);
    det = d2d_fp_estimate(fin.now, fin.length);
    err_bound = err_bound_b * permanent;
    if (det >= err_bound || -det >= err_bound)
        return det > 0.0f;

    d2d_fp_two_diff_tail(&da.x[0], p[a].x, p[d].x, da.x[1]);
    d2d_fp_two_diff_tail(&da.y[0], p[a].y, p[d].y, da.y[1]);
    d2d_fp_two_diff_tail(&db.x[0], p[b].x, p[d].x, db.x[1]);
    d2d_fp_two_diff_tail(&db.y[0], p[b].y, p[d].y, db.y[1]);
    d2d_fp_two_diff_tail(&dc.x[0], p[c].x, p[d].x, dc.x[1]);
    d2d_fp_two_diff_tail(&dc.y[0], p[c].y, p[d].y, dc.y[1]);
    if (da.x[0] == 0.0f && db.x[0] == 0.0f && dc.x[0] == 0.0f
            && da.y[0] == 0.0f && db.y[0] == 0.0f && dc.y[0] == 0.0f)
        return det > 0.0f;

    err_bound = err_bound_c * permanent + err_bound_result * fabsf(det);
    det += (daz[3] * ((db.x[1] * dc.y[0] + dc.y[1] * db.x[0]) - (db.y[1] * dc.x[0] + dc.x[1] * db.y[0]))
            + 2.0f * (da.x[1] * da.x[0] + da.y[1] * da.y[0]) * (db.x[1] * dc.y[1] - db.y[1] * dc.x[1]))
            + (dbz[3] * ((dc.x[1] * da.y[0] + da.y[1] * dc.x[0]) - (dc.y[1] * da.x[0] + da.x[1] * dc.y[0]))
            + 2.0f * (db.x[1] * db.x[0] + db.y[1] * db.y[0]) * (dc.x[1] * da.y[1] - dc.y[1] * da.x[1]))
            + (dcz[3] * ((da.x[1] * db.y[0] + db.y[1] * da.x[0]) - (da.y[1] * db.x[0] + db.x[1] * da.y[0]))
            + 2.0f * (dc.x[1] * dc.x[0] + dc.y[1] * dc.y[0]) * (da.x[1] * db.y[1] - da.y[1] * db.x[1]));
    if (det >= err_bound || -det >= err_bound)
        return det > 0.0f;

    if (db.x[0] != 0.0f || db.y[0] != 0.0f || dc.x[0] != 0.0f || dc.y[0] != 0.0f)
    {
        d2d_fp_square(temp2a, da.x[1]);
        d2d_fp_square(temp2b, da.y[1]);
        d2d_fp_two_two_sum(daz, temp2a, temp2b);
    }
    if (dc.x[0] != 0.0f || dc.y[0] != 0.0f || da.x[0] != 0.0f || da.y[0] != 0.0f)
    {
        d2d_fp_square(temp2a, db.x[1]);
        d2d_fp_square(temp2b, db.y[1]);
        d2d_fp_two_two_sum(dbz, temp2a, temp2b);
    }
    if (da.x[0] != 0.0f || da.y[0] != 0.0f || db.x[0] != 0.0f || db.y[0] != 0.0f)
    {
        d2d_fp_square(temp2a, dc.x[1]);
        d2d_fp_square(temp2b, dc.y[1]);
        d2d_fp_two_two_sum(dcz, temp2a, temp2b);
    }

    if (da.x[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, axt_det_bc, &axt_det_bc_len, det_bc, dc.y[1], dcz, db.y[1], dbz, da.x);
    if (da.y[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, ayt_det_bc, &ayt_det_bc_len, det_bc, db.x[1], dbz, dc.x[1], dcz, da.y);
    if (db.x[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, bxt_det_ca, &bxt_det_ca_len, det_ca, da.y[1], daz, dc.y[1], dcz, db.x);
    if (db.y[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, byt_det_ca, &byt_det_ca_len, det_ca, dc.x[1], dcz, da.x[1], daz, db.y);
    if (dc.x[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, cxt_det_ab, &cxt_det_ab_len, det_ab, db.y[1], dbz, da.y[1], daz, dc.x);
    if (dc.y[0] != 0.0f)
        d2d_cdt_incircle_refine1(&fin, cyt_det_ab, &cyt_det_ab_len, det_ab, da.x[1], daz, db.x[1], dbz, dc.y);

    if (da.x[0] != 0.0f || da.y[0] != 0.0f)
        d2d_cdt_incircle_refine2(&fin, &da, &db, dbz, &dc, dcz,
                axt_det_bc, axt_det_bc_len, ayt_det_bc, ayt_det_bc_len);
    if (db.x[0] != 0.0f || db.y[0] != 0.0f)
        d2d_cdt_incircle_refine2(&fin, &db, &dc, dcz, &da, daz,
                bxt_det_ca, bxt_det_ca_len, byt_det_ca, byt_det_ca_len);
    if (dc.x[0] != 0.0f || dc.y[0] != 0.0f)
        d2d_cdt_incircle_refine2(&fin, &dc, &da, daz, &db, dbz,
                cxt_det_ab, cxt_det_ab_len, cyt_det_ab, cyt_det_ab_len);

    return fin.now[fin.length - 1] > 0.0f;
}

static void d2d_cdt_splice(const struct d2d_cdt *cdt, const struct d2d_cdt_edge_ref *a,
        const struct d2d_cdt_edge_ref *b)
{
    struct d2d_cdt_edge_ref ta, tb, alpha, beta;

    ta = cdt->edges[a->idx].next[a->r];
    tb = cdt->edges[b->idx].next[b->r];
    cdt->edges[a->idx].next[a->r] = tb;
    cdt->edges[b->idx].next[b->r] = ta;

    d2d_cdt_edge_rot(&alpha, &ta);
    d2d_cdt_edge_rot(&beta, &tb);

    ta = cdt->edges[alpha.idx].next[alpha.r];
    tb = cdt->edges[beta.idx].next[beta.r];
    cdt->edges[alpha.idx].next[alpha.r] = tb;
    cdt->edges[beta.idx].next[beta.r] = ta;
}

static BOOL d2d_cdt_create_edge(struct d2d_cdt *cdt, struct d2d_cdt_edge_ref *e)
{
    struct d2d_cdt_edge *edge;

    if (cdt->free_edge != ~0u)
    {
        e->idx = cdt->free_edge;
        cdt->free_edge = cdt->edges[e->idx].next[D2D_EDGE_NEXT_ORIGIN].idx;
    }
    else
    {
        if (!d2d_array_reserve((void **)&cdt->edges, &cdt->edges_size, cdt->edge_count + 1, sizeof(*cdt->edges)))
        {
            ERR("Failed to grow edges array.\n");
            return FALSE;
        }
        e->idx = cdt->edge_count++;
    }
    e->r = 0;

    edge = &cdt->edges[e->idx];
    edge->next[D2D_EDGE_NEXT_ORIGIN] = *e;
    d2d_cdt_edge_tor(&edge->next[D2D_EDGE_NEXT_ROT], e);
    d2d_cdt_edge_sym(&edge->next[D2D_EDGE_NEXT_SYM], e);
    d2d_cdt_edge_rot(&edge->next[D2D_EDGE_NEXT_TOR], e);
    edge->flags = 0;

    return TRUE;
}

static void d2d_cdt_destroy_edge(struct d2d_cdt *cdt, const struct d2d_cdt_edge_ref *e)
{
    struct d2d_cdt_edge_ref next, sym, prev;

    d2d_cdt_edge_next_origin(cdt, &next, e);
    if (next.idx != e->idx || next.r != e->r)
    {
        d2d_cdt_edge_prev_origin(cdt, &prev, e);
        d2d_cdt_splice(cdt, e, &prev);
    }

    d2d_cdt_edge_sym(&sym, e);

    d2d_cdt_edge_next_origin(cdt, &next, &sym);
    if (next.idx != sym.idx || next.r != sym.r)
    {
        d2d_cdt_edge_prev_origin(cdt, &prev, &sym);
        d2d_cdt_splice(cdt, &sym, &prev);
    }

    cdt->edges[e->idx].flags |= D2D_CDT_EDGE_FLAG_FREED;
    cdt->edges[e->idx].next[D2D_EDGE_NEXT_ORIGIN].idx = cdt->free_edge;
    cdt->free_edge = e->idx;
}

static BOOL d2d_cdt_connect(struct d2d_cdt *cdt, struct d2d_cdt_edge_ref *e,
        const struct d2d_cdt_edge_ref *a, const struct d2d_cdt_edge_ref *b)
{
    struct d2d_cdt_edge_ref tmp;

    if (!d2d_cdt_create_edge(cdt, e))
        return FALSE;
    d2d_cdt_edge_set_origin(cdt, e, d2d_cdt_edge_destination(cdt, a));
    d2d_cdt_edge_set_destination(cdt, e, d2d_cdt_edge_origin(cdt, b));
    d2d_cdt_edge_next_left(cdt, &tmp, a);
    d2d_cdt_splice(cdt, e, &tmp);
    d2d_cdt_edge_sym(&tmp, e);
    d2d_cdt_splice(cdt, &tmp, b);

    return TRUE;
}

static BOOL d2d_cdt_merge(struct d2d_cdt *cdt, struct d2d_cdt_edge_ref *left_outer,
        struct d2d_cdt_edge_ref *left_inner, struct d2d_cdt_edge_ref *right_inner,
        struct d2d_cdt_edge_ref *right_outer)
{
    struct d2d_cdt_edge_ref base_edge, tmp;

    /* Create the base edge between both parts. */
    for (;;)
    {
        if (d2d_cdt_leftof(cdt, d2d_cdt_edge_origin(cdt, right_inner), left_inner))
        {
            d2d_cdt_edge_next_left(cdt, left_inner, left_inner);
        }
        else if (d2d_cdt_rightof(cdt, d2d_cdt_edge_origin(cdt, left_inner), right_inner))
        {
            d2d_cdt_edge_sym(&tmp, right_inner);
            d2d_cdt_edge_next_origin(cdt, right_inner, &tmp);
        }
        else
        {
            break;
        }
    }

    d2d_cdt_edge_sym(&tmp, right_inner);
    if (!d2d_cdt_connect(cdt, &base_edge, &tmp, left_inner))
        return FALSE;
    if (d2d_cdt_edge_origin(cdt, left_inner) == d2d_cdt_edge_origin(cdt, left_outer))
        d2d_cdt_edge_sym(left_outer, &base_edge);
    if (d2d_cdt_edge_origin(cdt, right_inner) == d2d_cdt_edge_origin(cdt, right_outer))
        *right_outer = base_edge;

    for (;;)
    {
        struct d2d_cdt_edge_ref left_candidate, right_candidate, sym_base_edge;
        BOOL left_valid, right_valid;

        /* Find the left candidate. */
        d2d_cdt_edge_sym(&sym_base_edge, &base_edge);
        d2d_cdt_edge_next_origin(cdt, &left_candidate, &sym_base_edge);
        if ((left_valid = d2d_cdt_leftof(cdt, d2d_cdt_edge_destination(cdt, &left_candidate), &sym_base_edge)))
        {
            d2d_cdt_edge_next_origin(cdt, &tmp, &left_candidate);
            while (d2d_cdt_edge_destination(cdt, &tmp) != d2d_cdt_edge_destination(cdt, &sym_base_edge)
                    && d2d_cdt_incircle(cdt,
                    d2d_cdt_edge_origin(cdt, &sym_base_edge), d2d_cdt_edge_destination(cdt, &sym_base_edge),
                    d2d_cdt_edge_destination(cdt, &left_candidate), d2d_cdt_edge_destination(cdt, &tmp)))
            {
                d2d_cdt_destroy_edge(cdt, &left_candidate);
                left_candidate = tmp;
                d2d_cdt_edge_next_origin(cdt, &tmp, &left_candidate);
            }
        }
        d2d_cdt_edge_sym(&left_candidate, &left_candidate);

        /* Find the right candidate. */
        d2d_cdt_edge_prev_origin(cdt, &right_candidate, &base_edge);
        if ((right_valid = d2d_cdt_rightof(cdt, d2d_cdt_edge_destination(cdt, &right_candidate), &base_edge)))
        {
            d2d_cdt_edge_prev_origin(cdt, &tmp, &right_candidate);
            while (d2d_cdt_edge_destination(cdt, &tmp) != d2d_cdt_edge_destination(cdt, &base_edge)
                    && d2d_cdt_incircle(cdt,
                    d2d_cdt_edge_origin(cdt, &sym_base_edge), d2d_cdt_edge_destination(cdt, &sym_base_edge),
                    d2d_cdt_edge_destination(cdt, &right_candidate), d2d_cdt_edge_destination(cdt, &tmp)))
            {
                d2d_cdt_destroy_edge(cdt, &right_candidate);
                right_candidate = tmp;
                d2d_cdt_edge_prev_origin(cdt, &tmp, &right_candidate);
            }
        }

        if (!left_valid && !right_valid)
            break;

        /* Connect the appropriate candidate with the base edge. */
        if (!left_valid || (right_valid && d2d_cdt_incircle(cdt,
                d2d_cdt_edge_origin(cdt, &left_candidate), d2d_cdt_edge_destination(cdt, &left_candidate),
                d2d_cdt_edge_origin(cdt, &right_candidate), d2d_cdt_edge_destination(cdt, &right_candidate))))
        {
            if (!d2d_cdt_connect(cdt, &base_edge, &right_candidate, &sym_base_edge))
                return FALSE;
        }
        else
        {
            if (!d2d_cdt_connect(cdt, &base_edge, &sym_base_edge, &left_candidate))
                return FALSE;
        }
    }

    return TRUE;
}

/* Create a Delaunay triangulation from a set of vertices. This is an
 * implementation of the divide-and-conquer algorithm described by Guibas and
 * Stolfi. Should be called with at least two vertices. */
static BOOL d2d_cdt_triangulate(struct d2d_cdt *cdt, size_t start_vertex, size_t vertex_count,
        struct d2d_cdt_edge_ref *left_edge, struct d2d_cdt_edge_ref *right_edge)
{
    struct d2d_cdt_edge_ref left_inner, left_outer, right_inner, right_outer, tmp;
    size_t cut;

    /* Only two vertices, create a single edge. */
    if (vertex_count == 2)
    {
        struct d2d_cdt_edge_ref a;

        if (!d2d_cdt_create_edge(cdt, &a))
            return FALSE;
        d2d_cdt_edge_set_origin(cdt, &a, start_vertex);
        d2d_cdt_edge_set_destination(cdt, &a, start_vertex + 1);

        *left_edge = a;
        d2d_cdt_edge_sym(right_edge, &a);

        return TRUE;
    }

    /* Three vertices, create a triangle. */
    if (vertex_count == 3)
    {
        struct d2d_cdt_edge_ref a, b, c;
        float det;

        if (!d2d_cdt_create_edge(cdt, &a))
            return FALSE;
        if (!d2d_cdt_create_edge(cdt, &b))
            return FALSE;
        d2d_cdt_edge_sym(&tmp, &a);
        d2d_cdt_splice(cdt, &tmp, &b);

        d2d_cdt_edge_set_origin(cdt, &a, start_vertex);
        d2d_cdt_edge_set_destination(cdt, &a, start_vertex + 1);
        d2d_cdt_edge_set_origin(cdt, &b, start_vertex + 1);
        d2d_cdt_edge_set_destination(cdt, &b, start_vertex + 2);

        det = d2d_cdt_ccw(cdt, start_vertex, start_vertex + 1, start_vertex + 2);
        if (det != 0.0f && !d2d_cdt_connect(cdt, &c, &b, &a))
            return FALSE;

        if (det < 0.0f)
        {
            d2d_cdt_edge_sym(left_edge, &c);
            *right_edge = c;
        }
        else
        {
            *left_edge = a;
            d2d_cdt_edge_sym(right_edge, &b);
        }

        return TRUE;
    }

    /* More than tree vertices, divide. */
    cut = vertex_count / 2;
    if (!d2d_cdt_triangulate(cdt, start_vertex, cut, &left_outer, &left_inner))
        return FALSE;
    if (!d2d_cdt_triangulate(cdt, start_vertex + cut, vertex_count - cut, &right_inner, &right_outer))
        return FALSE;
    /* Merge the left and right parts. */
    if (!d2d_cdt_merge(cdt, &left_outer, &left_inner, &right_inner, &right_outer))
        return FALSE;

    *left_edge = left_outer;
    *right_edge = right_outer;
    return TRUE;
}

static int d2d_cdt_compare_vertices(const void *a, const void *b)
{
    const D2D1_POINT_2F *p0 = a;
    const D2D1_POINT_2F *p1 = b;
    float diff = p0->x - p1->x;

    if (diff == 0.0f)
        diff = p0->y - p1->y;

    return diff == 0.0f ? 0 : (diff > 0.0f ? 1 : -1);
}

/* Determine whether a given point is inside the geometry, using the current
 * fill mode rule. */
static BOOL d2d_path_geometry_point_inside(const struct d2d_geometry *geometry,
        const D2D1_POINT_2F *probe, BOOL triangles_only)
{
    const D2D1_POINT_2F *p0, *p1;
    D2D1_POINT_2F v_p, v_probe;
    unsigned int score;
    size_t i, j;

    for (i = 0, score = 0; i < geometry->u.path.figure_count; ++i)
    {
        const struct d2d_figure *figure = &geometry->u.path.figures[i];

        if (probe->x < figure->bounds.left || probe->x > figure->bounds.right
                || probe->y < figure->bounds.top || probe->y > figure->bounds.bottom)
            continue;

        p0 = &figure->vertices[figure->vertex_count - 1];
        for (j = 0; j < figure->vertex_count; ++j)
        {
            if (!triangles_only && figure->vertex_types[j] == D2D_VERTEX_TYPE_NONE)
                continue;

            p1 = &figure->vertices[j];
            d2d_point_subtract(&v_p, p1, p0);
            d2d_point_subtract(&v_probe, probe, p0);

            if ((probe->y < p0->y) != (probe->y < p1->y) && v_probe.x < v_p.x * (v_probe.y / v_p.y))
            {
                if (geometry->u.path.fill_mode == D2D1_FILL_MODE_ALTERNATE || (probe->y < p0->y))
                    ++score;
                else
                    --score;
            }

            p0 = p1;
        }
    }

    return geometry->u.path.fill_mode == D2D1_FILL_MODE_ALTERNATE ? score & 1 : score;
}

static BOOL d2d_path_geometry_add_fill_face(struct d2d_geometry *geometry, const struct d2d_cdt *cdt,
        const struct d2d_cdt_edge_ref *base_edge)
{
    struct d2d_cdt_edge_ref tmp;
    struct d2d_face *face;
    D2D1_POINT_2F probe;

    if (cdt->edges[base_edge->idx].flags & D2D_CDT_EDGE_FLAG_VISITED(base_edge->r))
        return TRUE;

    if (!d2d_array_reserve((void **)&geometry->fill.faces, &geometry->fill.faces_size,
            geometry->fill.face_count + 1, sizeof(*geometry->fill.faces)))
    {
        ERR("Failed to grow faces array.\n");
        return FALSE;
    }

    face = &geometry->fill.faces[geometry->fill.face_count];

    /* It may seem tempting to use the center of the face as probe origin, but
     * multiplying by powers of two works much better for preserving accuracy. */

    tmp = *base_edge;
    cdt->edges[tmp.idx].flags |= D2D_CDT_EDGE_FLAG_VISITED(tmp.r);
    face->v[0] = d2d_cdt_edge_origin(cdt, &tmp);
    probe.x = cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].x * 0.25f;
    probe.y = cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].y * 0.25f;

    d2d_cdt_edge_next_left(cdt, &tmp, &tmp);
    cdt->edges[tmp.idx].flags |= D2D_CDT_EDGE_FLAG_VISITED(tmp.r);
    face->v[1] = d2d_cdt_edge_origin(cdt, &tmp);
    probe.x += cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].x * 0.25f;
    probe.y += cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].y * 0.25f;

    d2d_cdt_edge_next_left(cdt, &tmp, &tmp);
    cdt->edges[tmp.idx].flags |= D2D_CDT_EDGE_FLAG_VISITED(tmp.r);
    face->v[2] = d2d_cdt_edge_origin(cdt, &tmp);
    probe.x += cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].x * 0.50f;
    probe.y += cdt->vertices[d2d_cdt_edge_origin(cdt, &tmp)].y * 0.50f;

    if (d2d_cdt_leftof(cdt, face->v[2], base_edge) && d2d_path_geometry_point_inside(geometry, &probe, TRUE))
        ++geometry->fill.face_count;

    return TRUE;
}

static BOOL d2d_cdt_generate_faces(const struct d2d_cdt *cdt, struct d2d_geometry *geometry)
{
    struct d2d_cdt_edge_ref base_edge;
    size_t i;

    for (i = 0; i < cdt->edge_count; ++i)
    {
        if (cdt->edges[i].flags & D2D_CDT_EDGE_FLAG_FREED)
            continue;

        base_edge.idx = i;
        base_edge.r = 0;
        if (!d2d_path_geometry_add_fill_face(geometry, cdt, &base_edge))
            goto fail;
        d2d_cdt_edge_sym(&base_edge, &base_edge);
        if (!d2d_path_geometry_add_fill_face(geometry, cdt, &base_edge))
            goto fail;
    }

    return TRUE;

fail:
    HeapFree(GetProcessHeap(), 0, geometry->fill.faces);
    geometry->fill.faces = NULL;
    geometry->fill.faces_size = 0;
    geometry->fill.face_count = 0;
    return FALSE;
}

static BOOL d2d_cdt_fixup(struct d2d_cdt *cdt, const struct d2d_cdt_edge_ref *base_edge)
{
    struct d2d_cdt_edge_ref candidate, next, new_base;
    unsigned int count = 0;

    d2d_cdt_edge_next_left(cdt, &next, base_edge);
    if (next.idx == base_edge->idx)
    {
        ERR("Degenerate face.\n");
        return FALSE;
    }

    candidate = next;
    while (d2d_cdt_edge_destination(cdt, &next) != d2d_cdt_edge_origin(cdt, base_edge))
    {
        if (d2d_cdt_incircle(cdt, d2d_cdt_edge_origin(cdt, base_edge), d2d_cdt_edge_destination(cdt, base_edge),
                d2d_cdt_edge_destination(cdt, &candidate), d2d_cdt_edge_destination(cdt, &next)))
            candidate = next;
        d2d_cdt_edge_next_left(cdt, &next, &next);
        ++count;
    }

    if (count > 1)
    {
        d2d_cdt_edge_next_left(cdt, &next, &candidate);
        if (d2d_cdt_edge_destination(cdt, &next) == d2d_cdt_edge_origin(cdt, base_edge))
            d2d_cdt_edge_next_left(cdt, &next, base_edge);
        else
            next = *base_edge;
        if (!d2d_cdt_connect(cdt, &new_base, &candidate, &next))
            return FALSE;
        if (!d2d_cdt_fixup(cdt, &new_base))
            return FALSE;
        d2d_cdt_edge_sym(&new_base, &new_base);
        if (!d2d_cdt_fixup(cdt, &new_base))
            return FALSE;
    }

    return TRUE;
}

static void d2d_cdt_cut_edges(struct d2d_cdt *cdt, struct d2d_cdt_edge_ref *end_edge,
        const struct d2d_cdt_edge_ref *base_edge, size_t start_vertex, size_t end_vertex)
{
    struct d2d_cdt_edge_ref next;
    float ccw;

    d2d_cdt_edge_next_left(cdt, &next, base_edge);
    if (d2d_cdt_edge_destination(cdt, &next) == end_vertex)
    {
        *end_edge = next;
        return;
    }

    ccw = d2d_cdt_ccw(cdt, d2d_cdt_edge_destination(cdt, &next), end_vertex, start_vertex);
    if (ccw == 0.0f)
    {
        *end_edge = next;
        return;
    }

    if (ccw > 0.0f)
        d2d_cdt_edge_next_left(cdt, &next, &next);

    d2d_cdt_edge_sym(&next, &next);
    d2d_cdt_cut_edges(cdt, end_edge, &next, start_vertex, end_vertex);
    d2d_cdt_destroy_edge(cdt, &next);
}

static BOOL d2d_cdt_insert_segment(struct d2d_cdt *cdt, struct d2d_geometry *geometry,
        const struct d2d_cdt_edge_ref *origin, struct d2d_cdt_edge_ref *edge, size_t end_vertex)
{
    struct d2d_cdt_edge_ref base_edge, current, new_origin, next, target;
    size_t current_destination, current_origin;

    for (current = *origin;; current = next)
    {
        d2d_cdt_edge_next_origin(cdt, &next, &current);

        current_destination = d2d_cdt_edge_destination(cdt, &current);
        if (current_destination == end_vertex)
        {
            d2d_cdt_edge_sym(edge, &current);
            return TRUE;
        }

        current_origin = d2d_cdt_edge_origin(cdt, &current);
        if (d2d_cdt_ccw(cdt, end_vertex, current_origin, current_destination) == 0.0f
                && (cdt->vertices[current_destination].x > cdt->vertices[current_origin].x)
                == (cdt->vertices[end_vertex].x > cdt->vertices[current_origin].x)
                && (cdt->vertices[current_destination].y > cdt->vertices[current_origin].y)
                == (cdt->vertices[end_vertex].y > cdt->vertices[current_origin].y))
        {
            d2d_cdt_edge_sym(&new_origin, &current);
            return d2d_cdt_insert_segment(cdt, geometry, &new_origin, edge, end_vertex);
        }

        if (d2d_cdt_rightof(cdt, end_vertex, &next) && d2d_cdt_leftof(cdt, end_vertex, &current))
        {
            d2d_cdt_edge_next_left(cdt, &base_edge, &current);

            d2d_cdt_edge_sym(&base_edge, &base_edge);
            d2d_cdt_cut_edges(cdt, &target, &base_edge, d2d_cdt_edge_origin(cdt, origin), end_vertex);
            d2d_cdt_destroy_edge(cdt, &base_edge);

            if (!d2d_cdt_connect(cdt, &base_edge, &target, &current))
                return FALSE;
            *edge = base_edge;
            if (!d2d_cdt_fixup(cdt, &base_edge))
                return FALSE;
            d2d_cdt_edge_sym(&base_edge, &base_edge);
            if (!d2d_cdt_fixup(cdt, &base_edge))
                return FALSE;

            if (d2d_cdt_edge_origin(cdt, edge) == end_vertex)
                return TRUE;
            new_origin = *edge;
            return d2d_cdt_insert_segment(cdt, geometry, &new_origin, edge, end_vertex);
        }

        if (next.idx == origin->idx)
        {
            ERR("Triangle not found.\n");
            return FALSE;
        }
    }
}

static BOOL d2d_cdt_insert_segments(struct d2d_cdt *cdt, struct d2d_geometry *geometry)
{
    size_t start_vertex, end_vertex, i, j, k;
    struct d2d_cdt_edge_ref edge, new_edge;
    const struct d2d_figure *figure;
    const D2D1_POINT_2F *p;
    BOOL found;

    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        figure = &geometry->u.path.figures[i];

        p = bsearch(&figure->vertices[figure->vertex_count - 1], cdt->vertices,
                geometry->fill.vertex_count, sizeof(*p), d2d_cdt_compare_vertices);
        start_vertex = p - cdt->vertices;

        for (k = 0, found = FALSE; k < cdt->edge_count; ++k)
        {
            if (cdt->edges[k].flags & D2D_CDT_EDGE_FLAG_FREED)
                continue;

            edge.idx = k;
            edge.r = 0;

            if (d2d_cdt_edge_origin(cdt, &edge) == start_vertex)
            {
                found = TRUE;
                break;
            }
            d2d_cdt_edge_sym(&edge, &edge);
            if (d2d_cdt_edge_origin(cdt, &edge) == start_vertex)
            {
                found = TRUE;
                break;
            }
        }

        if (!found)
        {
            ERR("Edge not found.\n");
            return FALSE;
        }

        for (j = 0; j < figure->vertex_count; start_vertex = end_vertex, ++j)
        {
            p = bsearch(&figure->vertices[j], cdt->vertices,
                    geometry->fill.vertex_count, sizeof(*p), d2d_cdt_compare_vertices);
            end_vertex = p - cdt->vertices;

            if (start_vertex == end_vertex)
                continue;

            if (!d2d_cdt_insert_segment(cdt, geometry, &edge, &new_edge, end_vertex))
                return FALSE;
            edge = new_edge;
        }
    }

    return TRUE;
}

static BOOL d2d_geometry_intersections_add(struct d2d_geometry_intersections *i,
        size_t figure_idx, size_t segment_idx, float t, D2D1_POINT_2F p)
{
    struct d2d_geometry_intersection *intersection;

    if (!d2d_array_reserve((void **)&i->intersections, &i->intersections_size,
            i->intersection_count + 1, sizeof(*i->intersections)))
    {
        ERR("Failed to grow intersections array.\n");
        return FALSE;
    }

    intersection = &i->intersections[i->intersection_count++];
    intersection->figure_idx = figure_idx;
    intersection->segment_idx = segment_idx;
    intersection->t = t;
    intersection->p = p;

    return TRUE;
}

static int d2d_geometry_intersections_compare(const void *a, const void *b)
{
    const struct d2d_geometry_intersection *i0 = a;
    const struct d2d_geometry_intersection *i1 = b;

    if (i0->figure_idx != i1->figure_idx)
        return i0->figure_idx - i1->figure_idx;
    if (i0->segment_idx != i1->segment_idx)
        return i0->segment_idx - i1->segment_idx;
    if (i0->t != i1->t)
        return i0->t > i1->t ? 1 : -1;
    return 0;
}

/* Intersect the geometry's segments with themselves. This uses the
 * straightforward approach of testing everything against everything, but
 * there certainly exist more scalable algorithms for this. */
/* FIXME: Beziers can't currently self-intersect. */
static BOOL d2d_geometry_intersect_self(struct d2d_geometry *geometry)
{
    D2D1_POINT_2F p0, p1, q0, q1, v_p, v_q, v_qp, intersection;
    struct d2d_geometry_intersections intersections = {0};
    struct d2d_figure *figure_p, *figure_q;
    size_t i, j, k, l, max_l;
    BOOL ret = FALSE;
    float s, t, det;

    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        figure_p = &geometry->u.path.figures[i];
        p0 = figure_p->vertices[figure_p->vertex_count - 1];
        for (k = 0; k < figure_p->vertex_count; p0 = p1, ++k)
        {
            p1 = figure_p->vertices[k];
            d2d_point_subtract(&v_p, &p1, &p0);
            for (j = 0; j < i || (j == i && k); ++j)
            {
                figure_q = &geometry->u.path.figures[j];

                if (figure_p->bounds.left > figure_q->bounds.right
                        || figure_q->bounds.left > figure_p->bounds.right
                        || figure_p->bounds.top > figure_q->bounds.bottom
                        || figure_q->bounds.top > figure_p->bounds.bottom)
                    continue;

                max_l = j == i ? k - 1 : figure_q->vertex_count;
                q0 = figure_q->vertices[figure_q->vertex_count - 1];
                for (l = 0; l < max_l; q0 = q1, ++l)
                {
                    q1 = figure_q->vertices[l];
                    d2d_point_subtract(&v_q, &q1, &q0);
                    d2d_point_subtract(&v_qp, &p0, &q0);

                    det = v_p.x * v_q.y - v_p.y * v_q.x;
                    if (det == 0.0f)
                        continue;

                    s = (v_q.x * v_qp.y - v_q.y * v_qp.x) / det;
                    t = (v_p.x * v_qp.y - v_p.y * v_qp.x) / det;

                    if (s < 0.0f || s > 1.0f || t < 0.0f || t > 1.0f)
                        continue;

                    intersection.x = p0.x + v_p.x * s;
                    intersection.y = p0.y + v_p.y * s;

                    if (t > 0.0f && t < 1.0f
                            && !d2d_geometry_intersections_add(&intersections, j, l, t, intersection))
                        goto done;

                    if (s > 0.0f && s < 1.0f
                            && !d2d_geometry_intersections_add(&intersections, i, k, s, intersection))
                        goto done;
                }
            }
        }
    }

    qsort(intersections.intersections, intersections.intersection_count,
            sizeof(*intersections.intersections), d2d_geometry_intersections_compare);
    for (i = 0; i < intersections.intersection_count; ++i)
    {
        const struct d2d_geometry_intersection *inter = &intersections.intersections[i];

        if (!i || inter->figure_idx != intersections.intersections[i - 1].figure_idx)
            j = 0;

        if (!d2d_figure_insert_vertex(&geometry->u.path.figures[inter->figure_idx],
                inter->segment_idx + j, inter->p))
            goto done;
        ++j;
    }

    ret = TRUE;

done:
    HeapFree(GetProcessHeap(), 0, intersections.intersections);
    return ret;
}

static HRESULT d2d_path_geometry_triangulate(struct d2d_geometry *geometry)
{
    struct d2d_cdt_edge_ref left_edge, right_edge;
    size_t vertex_count, i, j;
    struct d2d_cdt cdt = {0};
    D2D1_POINT_2F *vertices;

    for (i = 0, vertex_count = 0; i < geometry->u.path.figure_count; ++i)
    {
        vertex_count += geometry->u.path.figures[i].vertex_count;
    }

    if (vertex_count < 3)
    {
        WARN("Geometry has %lu vertices.\n", (long)vertex_count);
        return S_OK;
    }

    if (!(vertices = HeapAlloc(GetProcessHeap(), 0, vertex_count * sizeof(*vertices))))
        return E_OUTOFMEMORY;

    for (i = 0, j = 0; i < geometry->u.path.figure_count; ++i)
    {
        memcpy(&vertices[j], geometry->u.path.figures[i].vertices,
                geometry->u.path.figures[i].vertex_count * sizeof(*vertices));
        j += geometry->u.path.figures[i].vertex_count;
    }

    /* Sort vertices, eliminate duplicates. */
    qsort(vertices, vertex_count, sizeof(*vertices), d2d_cdt_compare_vertices);
    for (i = 1; i < vertex_count; ++i)
    {
        if (!memcmp(&vertices[i - 1], &vertices[i], sizeof(*vertices)))
        {
            --vertex_count;
            memmove(&vertices[i], &vertices[i + 1], (vertex_count - i) * sizeof(*vertices));
            --i;
        }
    }

    geometry->fill.vertices = vertices;
    geometry->fill.vertex_count = vertex_count;

    cdt.free_edge = ~0u;
    cdt.vertices = vertices;
    if (!d2d_cdt_triangulate(&cdt, 0, vertex_count, &left_edge, &right_edge))
        goto fail;
    if (!d2d_cdt_insert_segments(&cdt, geometry))
        goto fail;
    if (!d2d_cdt_generate_faces(&cdt, geometry))
        goto fail;

    HeapFree(GetProcessHeap(), 0, cdt.edges);
    return S_OK;

fail:
    geometry->fill.vertices = NULL;
    geometry->fill.vertex_count = 0;
    HeapFree(GetProcessHeap(), 0, vertices);
    HeapFree(GetProcessHeap(), 0, cdt.edges);
    return E_FAIL;
}

static BOOL d2d_path_geometry_add_figure(struct d2d_geometry *geometry)
{
    struct d2d_figure *figure;

    if (!d2d_array_reserve((void **)&geometry->u.path.figures, &geometry->u.path.figures_size,
            geometry->u.path.figure_count + 1, sizeof(*geometry->u.path.figures)))
    {
        ERR("Failed to grow figures array.\n");
        return FALSE;
    }

    figure = &geometry->u.path.figures[geometry->u.path.figure_count];
    memset(figure, 0, sizeof(*figure));
    figure->bounds.left = FLT_MAX;
    figure->bounds.top = FLT_MAX;
    figure->bounds.right = -FLT_MAX;
    figure->bounds.bottom = -FLT_MAX;

    ++geometry->u.path.figure_count;
    return TRUE;
}

static void d2d_geometry_cleanup(struct d2d_geometry *geometry)
{
    HeapFree(GetProcessHeap(), 0, geometry->fill.bezier_vertices);
    HeapFree(GetProcessHeap(), 0, geometry->fill.faces);
    HeapFree(GetProcessHeap(), 0, geometry->fill.vertices);
    ID2D1Factory_Release(geometry->factory);
}

static void d2d_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        const D2D1_MATRIX_3X2_F *transform, const struct ID2D1GeometryVtbl *vtbl)
{
    geometry->ID2D1Geometry_iface.lpVtbl = vtbl;
    geometry->refcount = 1;
    ID2D1Factory_AddRef(geometry->factory = factory);
    geometry->transform = *transform;
}

static inline struct d2d_geometry *impl_from_ID2D1GeometrySink(ID2D1GeometrySink *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_geometry, u.path.ID2D1GeometrySink_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_geometry_sink_QueryInterface(ID2D1GeometrySink *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1GeometrySink)
            || IsEqualGUID(iid, &IID_ID2D1SimplifiedGeometrySink)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1GeometrySink_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_geometry_sink_AddRef(ID2D1GeometrySink *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);

    TRACE("iface %p.\n", iface);

    return ID2D1Geometry_AddRef(&geometry->ID2D1Geometry_iface);
}

static ULONG STDMETHODCALLTYPE d2d_geometry_sink_Release(ID2D1GeometrySink *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);

    TRACE("iface %p.\n", iface);

    return ID2D1Geometry_Release(&geometry->ID2D1Geometry_iface);
}

static void STDMETHODCALLTYPE d2d_geometry_sink_SetFillMode(ID2D1GeometrySink *iface, D2D1_FILL_MODE mode)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);

    TRACE("iface %p, mode %#x.\n", iface, mode);

    geometry->u.path.fill_mode = mode;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_SetSegmentFlags(ID2D1GeometrySink *iface, D2D1_PATH_SEGMENT flags)
{
    FIXME("iface %p, flags %#x stub!\n", iface, flags);
}

static void STDMETHODCALLTYPE d2d_geometry_sink_BeginFigure(ID2D1GeometrySink *iface,
        D2D1_POINT_2F start_point, D2D1_FIGURE_BEGIN figure_begin)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);

    TRACE("iface %p, start_point {%.8e, %.8e}, figure_begin %#x.\n",
            iface, start_point.x, start_point.y, figure_begin);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_OPEN)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    if (figure_begin != D2D1_FIGURE_BEGIN_FILLED)
        FIXME("Ignoring figure_begin %#x.\n", figure_begin);

    if (!d2d_path_geometry_add_figure(geometry))
    {
        ERR("Failed to add figure.\n");
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    if (!d2d_figure_add_vertex(&geometry->u.path.figures[geometry->u.path.figure_count - 1], start_point))
    {
        ERR("Failed to add vertex.\n");
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    geometry->u.path.state = D2D_GEOMETRY_STATE_FIGURE;
    ++geometry->u.path.segment_count;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddLines(ID2D1GeometrySink *iface,
        const D2D1_POINT_2F *points, UINT32 count)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);
    struct d2d_figure *figure = &geometry->u.path.figures[geometry->u.path.figure_count - 1];
    unsigned int i;

    TRACE("iface %p, points %p, count %u.\n", iface, points, count);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_FIGURE)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    for (i = 0; i < count; ++i)
    {
        figure->vertex_types[figure->vertex_count - 1] = D2D_VERTEX_TYPE_LINE;
        if (!d2d_figure_add_vertex(figure, points[i]))
        {
            ERR("Failed to add vertex.\n");
            return;
        }
    }

    geometry->u.path.segment_count += count;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddBeziers(ID2D1GeometrySink *iface,
        const D2D1_BEZIER_SEGMENT *beziers, UINT32 count)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);
    struct d2d_figure *figure = &geometry->u.path.figures[geometry->u.path.figure_count - 1];
    D2D1_POINT_2F p;
    unsigned int i;

    TRACE("iface %p, beziers %p, count %u.\n", iface, beziers, count);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_FIGURE)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    for (i = 0; i < count; ++i)
    {
        /* FIXME: This tries to approximate a cubic bezier with a quadratic one. */
        p.x = (beziers[i].point1.x + beziers[i].point2.x) * 0.75f;
        p.y = (beziers[i].point1.y + beziers[i].point2.y) * 0.75f;
        p.x -= (figure->vertices[figure->vertex_count - 1].x + beziers[i].point3.x) * 0.25f;
        p.y -= (figure->vertices[figure->vertex_count - 1].y + beziers[i].point3.y) * 0.25f;
        figure->vertex_types[figure->vertex_count - 1] = D2D_VERTEX_TYPE_BEZIER;

        if (!d2d_figure_add_bezier_control(figure, &p))
        {
            ERR("Failed to add bezier control.\n");
            geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
            return;
        }

        if (!d2d_figure_add_vertex(figure, beziers[i].point3))
        {
            ERR("Failed to add bezier vertex.\n");
            geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
            return;
        }
    }

    geometry->u.path.segment_count += count;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_EndFigure(ID2D1GeometrySink *iface, D2D1_FIGURE_END figure_end)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);
    struct d2d_figure *figure;

    TRACE("iface %p, figure_end %#x.\n", iface, figure_end);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_FIGURE)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    figure = &geometry->u.path.figures[geometry->u.path.figure_count - 1];
    figure->vertex_types[figure->vertex_count - 1] = D2D_VERTEX_TYPE_LINE;
    if (figure_end != D2D1_FIGURE_END_CLOSED)
        FIXME("Ignoring figure_end %#x.\n", figure_end);

    geometry->u.path.state = D2D_GEOMETRY_STATE_OPEN;
}

static void d2d_path_geometry_free_figures(struct d2d_geometry *geometry)
{
    size_t i;

    if (!geometry->u.path.figures)
        return;

    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        HeapFree(GetProcessHeap(), 0, geometry->u.path.figures[i].bezier_controls);
        HeapFree(GetProcessHeap(), 0, geometry->u.path.figures[i].vertices);
    }
    HeapFree(GetProcessHeap(), 0, geometry->u.path.figures);
    geometry->u.path.figures = NULL;
    geometry->u.path.figures_size = 0;
}

static HRESULT d2d_geometry_resolve_beziers(struct d2d_geometry *geometry)
{
    size_t bezier_idx, control_idx, i, j;

    for (i = 0; i < geometry->u.path.figure_count; ++i)
    {
        geometry->fill.bezier_vertex_count += 3 * geometry->u.path.figures[i].bezier_control_count;
    }

    if (!(geometry->fill.bezier_vertices = HeapAlloc(GetProcessHeap(), 0,
            geometry->fill.bezier_vertex_count * sizeof(*geometry->fill.bezier_vertices))))
    {
        ERR("Failed to allocate bezier vertices array.\n");
        geometry->fill.bezier_vertex_count = 0;
        return E_OUTOFMEMORY;
    }

    for (i = 0, bezier_idx = 0; i < geometry->u.path.figure_count; ++i)
    {
        struct d2d_figure *figure = &geometry->u.path.figures[i];
        if (figure->bezier_control_count)
        {
            for (j = 0, control_idx = 0; j < figure->vertex_count; ++j)
            {
                const D2D1_POINT_2F *p0, *p1, *p2;
                struct d2d_bezier_vertex *b;
                float sign = -1.0f;

                if (figure->vertex_types[j] != D2D_VERTEX_TYPE_BEZIER)
                    continue;

                b = &geometry->fill.bezier_vertices[bezier_idx * 3];
                p0 = &figure->vertices[j];
                p1 = &figure->bezier_controls[control_idx++];

                if (d2d_path_geometry_point_inside(geometry, p1, FALSE))
                {
                    sign = 1.0f;
                    d2d_figure_insert_vertex(figure, j + 1, *p1);
                    /* Inserting a vertex potentially invalidates p0. */
                    p0 = &figure->vertices[j];
                    ++j;
                }

                if (j == figure->vertex_count - 1)
                    p2 = &figure->vertices[0];
                else
                    p2 = &figure->vertices[j + 1];

                d2d_bezier_vertex_set(&b[0], p0, 0.0f, 0.0f, sign);
                d2d_bezier_vertex_set(&b[1], p1, 0.5f, 0.0f, sign);
                d2d_bezier_vertex_set(&b[2], p2, 1.0f, 1.0f, sign);
                ++bezier_idx;
            }
        }
    }

    return TRUE;
}

static HRESULT STDMETHODCALLTYPE d2d_geometry_sink_Close(ID2D1GeometrySink *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);
    HRESULT hr = E_FAIL;

    TRACE("iface %p.\n", iface);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_OPEN)
    {
        if (geometry->u.path.state != D2D_GEOMETRY_STATE_CLOSED)
            geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return D2DERR_WRONG_STATE;
    }
    geometry->u.path.state = D2D_GEOMETRY_STATE_CLOSED;

    if (!d2d_geometry_intersect_self(geometry))
        goto done;
    if (FAILED(hr = d2d_geometry_resolve_beziers(geometry)))
        goto done;
    if (FAILED(hr = d2d_path_geometry_triangulate(geometry)))
        goto done;

done:
    if (FAILED(hr))
    {
        HeapFree(GetProcessHeap(), 0, geometry->fill.bezier_vertices);
        geometry->fill.bezier_vertex_count = 0;
        d2d_path_geometry_free_figures(geometry);
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
    }
    return hr;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddLine(ID2D1GeometrySink *iface, D2D1_POINT_2F point)
{
    TRACE("iface %p, point {%.8e, %.8e}.\n", iface, point.x, point.y);

    d2d_geometry_sink_AddLines(iface, &point, 1);
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddBezier(ID2D1GeometrySink *iface, const D2D1_BEZIER_SEGMENT *bezier)
{
    TRACE("iface %p, bezier %p.\n", iface, bezier);

    d2d_geometry_sink_AddBeziers(iface, bezier, 1);
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddQuadraticBezier(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *bezier)
{
    TRACE("iface %p, bezier %p.\n", iface, bezier);

    ID2D1GeometrySink_AddQuadraticBeziers(iface, bezier, 1);
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddQuadraticBeziers(ID2D1GeometrySink *iface,
        const D2D1_QUADRATIC_BEZIER_SEGMENT *beziers, UINT32 bezier_count)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);
    struct d2d_figure *figure = &geometry->u.path.figures[geometry->u.path.figure_count - 1];
    unsigned int i;

    TRACE("iface %p, beziers %p, bezier_count %u.\n", iface, beziers, bezier_count);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_FIGURE)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    for (i = 0; i < bezier_count; ++i)
    {
        figure->vertex_types[figure->vertex_count - 1] = D2D_VERTEX_TYPE_BEZIER;
        if (!d2d_figure_add_bezier_control(figure, &beziers[i].point1))
        {
            ERR("Failed to add bezier.\n");
            geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
            return;
        }

        if (!d2d_figure_add_vertex(figure, beziers[i].point2))
        {
            ERR("Failed to add bezier vertex.\n");
            geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
            return;
        }
    }

    geometry->u.path.segment_count += bezier_count;
}

static void STDMETHODCALLTYPE d2d_geometry_sink_AddArc(ID2D1GeometrySink *iface, const D2D1_ARC_SEGMENT *arc)
{
    struct d2d_geometry *geometry = impl_from_ID2D1GeometrySink(iface);

    FIXME("iface %p, arc %p stub!\n", iface, arc);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_FIGURE)
    {
        geometry->u.path.state = D2D_GEOMETRY_STATE_ERROR;
        return;
    }

    if (!d2d_figure_add_vertex(&geometry->u.path.figures[geometry->u.path.figure_count - 1], arc->point))
    {
        ERR("Failed to add vertex.\n");
        return;
    }

    ++geometry->u.path.segment_count;
}

static const struct ID2D1GeometrySinkVtbl d2d_geometry_sink_vtbl =
{
    d2d_geometry_sink_QueryInterface,
    d2d_geometry_sink_AddRef,
    d2d_geometry_sink_Release,
    d2d_geometry_sink_SetFillMode,
    d2d_geometry_sink_SetSegmentFlags,
    d2d_geometry_sink_BeginFigure,
    d2d_geometry_sink_AddLines,
    d2d_geometry_sink_AddBeziers,
    d2d_geometry_sink_EndFigure,
    d2d_geometry_sink_Close,
    d2d_geometry_sink_AddLine,
    d2d_geometry_sink_AddBezier,
    d2d_geometry_sink_AddQuadraticBezier,
    d2d_geometry_sink_AddQuadraticBeziers,
    d2d_geometry_sink_AddArc,
};

static inline struct d2d_geometry *impl_from_ID2D1PathGeometry(ID2D1PathGeometry *iface)
{
    return CONTAINING_RECORD((ID2D1Geometry *)iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_QueryInterface(ID2D1PathGeometry *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1PathGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1PathGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_path_geometry_AddRef(ID2D1PathGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_path_geometry_Release(ID2D1PathGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
    {
        d2d_path_geometry_free_figures(geometry);
        d2d_geometry_cleanup(geometry);
        HeapFree(GetProcessHeap(), 0, geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_path_geometry_GetFactory(ID2D1PathGeometry *iface, ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_GetBounds(ID2D1PathGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, transform %p, bounds %p stub!\n", iface, transform, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_GetWidenedBounds(ID2D1PathGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_StrokeContainsPoint(ID2D1PathGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    FIXME("iface %p, point {%.8e, %.8e}, stroke_width %.8e, stroke_style %p, "
            "transform %p, tolerance %.8e, contains %p stub!\n",
            iface, point.x, point.y, stroke_width, stroke_style, transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_FillContainsPoint(ID2D1PathGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);
    D2D1_MATRIX_3X2_F g_i;

    TRACE("iface %p, point {%.8e, %.8e}, transform %p, tolerance %.8e, contains %p.\n",
            iface, point.x, point.y, transform, tolerance, contains);

    if (transform)
    {
        if (!d2d_matrix_invert(&g_i, transform))
            return D2DERR_UNSUPPORTED_OPERATION;
        d2d_point_transform(&point, &g_i, point.x, point.y);
    }

    *contains = !!d2d_path_geometry_point_inside(geometry, &point, FALSE);

    TRACE("-> %#x.\n", *contains);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_CompareWithGeometry(ID2D1PathGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Simplify(ID2D1PathGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, option %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, option, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Tessellate(ID2D1PathGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_CombineWithGeometry(ID2D1PathGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Outline(ID2D1PathGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_ComputeArea(ID2D1PathGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    FIXME("iface %p, transform %p, tolerance %.8e, area %p stub!\n", iface, transform, tolerance, area);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_ComputeLength(ID2D1PathGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_ComputePointAtLength(ID2D1PathGeometry *iface, float length,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point, D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Widen(ID2D1PathGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Open(ID2D1PathGeometry *iface, ID2D1GeometrySink **sink)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);

    TRACE("iface %p, sink %p.\n", iface, sink);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_INITIAL)
        return D2DERR_WRONG_STATE;

    *sink = &geometry->u.path.ID2D1GeometrySink_iface;
    ID2D1GeometrySink_AddRef(*sink);

    geometry->u.path.state = D2D_GEOMETRY_STATE_OPEN;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_Stream(ID2D1PathGeometry *iface, ID2D1GeometrySink *sink)
{
    FIXME("iface %p, sink %p stub!\n", iface, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_GetSegmentCount(ID2D1PathGeometry *iface, UINT32 *count)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);

    TRACE("iface %p, count %p.\n", iface, count);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_CLOSED)
        return D2DERR_WRONG_STATE;

    *count = geometry->u.path.segment_count;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_path_geometry_GetFigureCount(ID2D1PathGeometry *iface, UINT32 *count)
{
    struct d2d_geometry *geometry = impl_from_ID2D1PathGeometry(iface);

    TRACE("iface %p, count %p.\n", iface, count);

    if (geometry->u.path.state != D2D_GEOMETRY_STATE_CLOSED)
        return D2DERR_WRONG_STATE;

    *count = geometry->u.path.figure_count;

    return S_OK;
}

static const struct ID2D1PathGeometryVtbl d2d_path_geometry_vtbl =
{
    d2d_path_geometry_QueryInterface,
    d2d_path_geometry_AddRef,
    d2d_path_geometry_Release,
    d2d_path_geometry_GetFactory,
    d2d_path_geometry_GetBounds,
    d2d_path_geometry_GetWidenedBounds,
    d2d_path_geometry_StrokeContainsPoint,
    d2d_path_geometry_FillContainsPoint,
    d2d_path_geometry_CompareWithGeometry,
    d2d_path_geometry_Simplify,
    d2d_path_geometry_Tessellate,
    d2d_path_geometry_CombineWithGeometry,
    d2d_path_geometry_Outline,
    d2d_path_geometry_ComputeArea,
    d2d_path_geometry_ComputeLength,
    d2d_path_geometry_ComputePointAtLength,
    d2d_path_geometry_Widen,
    d2d_path_geometry_Open,
    d2d_path_geometry_Stream,
    d2d_path_geometry_GetSegmentCount,
    d2d_path_geometry_GetFigureCount,
};

void d2d_path_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory)
{
    d2d_geometry_init(geometry, factory, &identity, (ID2D1GeometryVtbl *)&d2d_path_geometry_vtbl);
    geometry->u.path.ID2D1GeometrySink_iface.lpVtbl = &d2d_geometry_sink_vtbl;
}

static inline struct d2d_geometry *impl_from_ID2D1RectangleGeometry(ID2D1RectangleGeometry *iface)
{
    return CONTAINING_RECORD((ID2D1Geometry *)iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_QueryInterface(ID2D1RectangleGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1RectangleGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1RectangleGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_rectangle_geometry_AddRef(ID2D1RectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_rectangle_geometry_Release(ID2D1RectangleGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
    {
        d2d_geometry_cleanup(geometry);
        HeapFree(GetProcessHeap(), 0, geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_rectangle_geometry_GetFactory(ID2D1RectangleGeometry *iface, ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_GetBounds(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, transform %p, bounds %p stub!\n", iface, transform, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_GetWidenedBounds(ID2D1RectangleGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_StrokeContainsPoint(ID2D1RectangleGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    FIXME("iface %p, point {%.8e, %.8e}, stroke_width %.8e, stroke_style %p, "
            "transform %p, tolerance %.8e, contains %p stub!\n",
            iface, point.x, point.y, stroke_width, stroke_style, transform, tolerance, contains);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_FillContainsPoint(ID2D1RectangleGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);
    D2D1_RECT_F *rect = &geometry->u.rectangle.rect;
    float dx, dy;

    TRACE("iface %p, point {%.8e, %.8e}, transform %p, tolerance %.8e, contains %p.\n",
            iface, point.x, point.y, transform, tolerance, contains);

    if (transform)
    {
        D2D1_MATRIX_3X2_F g_i;

        if (!d2d_matrix_invert(&g_i, transform))
            return D2DERR_UNSUPPORTED_OPERATION;
        d2d_point_transform(&point, &g_i, point.x, point.y);
    }

    if (tolerance == 0.0f)
        tolerance = D2D1_DEFAULT_FLATTENING_TOLERANCE;

    dx = max(fabsf((rect->right  + rect->left) / 2.0f - point.x) - (rect->right  - rect->left) / 2.0f, 0.0f);
    dy = max(fabsf((rect->bottom + rect->top)  / 2.0f - point.y) - (rect->bottom - rect->top)  / 2.0f, 0.0f);

    *contains = tolerance * tolerance > (dx * dx + dy * dy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_CompareWithGeometry(ID2D1RectangleGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Simplify(ID2D1RectangleGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, option %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, option, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Tessellate(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_CombineWithGeometry(ID2D1RectangleGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Outline(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputeArea(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    FIXME("iface %p, transform %p, tolerance %.8e, area %p stub!\n", iface, transform, tolerance, area);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputeLength(ID2D1RectangleGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_ComputePointAtLength(ID2D1RectangleGeometry *iface,
        float length, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point,
        D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_rectangle_geometry_Widen(ID2D1RectangleGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_rectangle_geometry_GetRect(ID2D1RectangleGeometry *iface, D2D1_RECT_F *rect)
{
    struct d2d_geometry *geometry = impl_from_ID2D1RectangleGeometry(iface);

    TRACE("iface %p, rect %p.\n", iface, rect);

    *rect = geometry->u.rectangle.rect;
}

static const struct ID2D1RectangleGeometryVtbl d2d_rectangle_geometry_vtbl =
{
    d2d_rectangle_geometry_QueryInterface,
    d2d_rectangle_geometry_AddRef,
    d2d_rectangle_geometry_Release,
    d2d_rectangle_geometry_GetFactory,
    d2d_rectangle_geometry_GetBounds,
    d2d_rectangle_geometry_GetWidenedBounds,
    d2d_rectangle_geometry_StrokeContainsPoint,
    d2d_rectangle_geometry_FillContainsPoint,
    d2d_rectangle_geometry_CompareWithGeometry,
    d2d_rectangle_geometry_Simplify,
    d2d_rectangle_geometry_Tessellate,
    d2d_rectangle_geometry_CombineWithGeometry,
    d2d_rectangle_geometry_Outline,
    d2d_rectangle_geometry_ComputeArea,
    d2d_rectangle_geometry_ComputeLength,
    d2d_rectangle_geometry_ComputePointAtLength,
    d2d_rectangle_geometry_Widen,
    d2d_rectangle_geometry_GetRect,
};

HRESULT d2d_rectangle_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory, const D2D1_RECT_F *rect)
{
    D2D1_POINT_2F *fv;
    float l, r, t, b;

    d2d_geometry_init(geometry, factory, &identity, (ID2D1GeometryVtbl *)&d2d_rectangle_geometry_vtbl);
    geometry->u.rectangle.rect = *rect;

    if (!(geometry->fill.vertices = HeapAlloc(GetProcessHeap(), 0, 4 * sizeof(*geometry->fill.vertices))))
    {
        d2d_geometry_cleanup(geometry);
        return E_OUTOFMEMORY;
    }
    geometry->fill.vertex_count = 4;
    if (!d2d_array_reserve((void **)&geometry->fill.faces,
            &geometry->fill.faces_size, 2, sizeof(*geometry->fill.faces)))
    {
        d2d_geometry_cleanup(geometry);
        return E_OUTOFMEMORY;
    }
    geometry->fill.face_count = 2;

    l = min(rect->left, rect->right);
    r = max(rect->left, rect->right);
    t = min(rect->top, rect->bottom);
    b = max(rect->top, rect->bottom);

    fv = geometry->fill.vertices;
    d2d_point_set(&fv[0], l, t);
    d2d_point_set(&fv[1], l, b);
    d2d_point_set(&fv[2], r, t);
    d2d_point_set(&fv[3], r, b);

    geometry->fill.faces[0].v[0] = 0;
    geometry->fill.faces[0].v[1] = 2;
    geometry->fill.faces[0].v[2] = 1;
    geometry->fill.faces[1].v[0] = 1;
    geometry->fill.faces[1].v[1] = 2;
    geometry->fill.faces[1].v[2] = 3;

    return S_OK;
}

static inline struct d2d_geometry *impl_from_ID2D1TransformedGeometry(ID2D1TransformedGeometry *iface)
{
    return CONTAINING_RECORD((ID2D1Geometry *)iface, struct d2d_geometry, ID2D1Geometry_iface);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_QueryInterface(ID2D1TransformedGeometry *iface,
        REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID2D1TransformedGeometry)
            || IsEqualGUID(iid, &IID_ID2D1Geometry)
            || IsEqualGUID(iid, &IID_ID2D1Resource)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        ID2D1TransformedGeometry_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE d2d_transformed_geometry_AddRef(ID2D1TransformedGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    ULONG refcount = InterlockedIncrement(&geometry->refcount);

    TRACE("%p increasing refcount to %u.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_transformed_geometry_Release(ID2D1TransformedGeometry *iface)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    ULONG refcount = InterlockedDecrement(&geometry->refcount);

    TRACE("%p decreasing refcount to %u.\n", iface, refcount);

    if (!refcount)
    {
        geometry->fill.bezier_vertices = NULL;
        geometry->fill.faces = NULL;
        geometry->fill.vertices = NULL;
        ID2D1Geometry_Release(geometry->u.transformed.src_geometry);
        d2d_geometry_cleanup(geometry);
        HeapFree(GetProcessHeap(), 0, geometry);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetFactory(ID2D1TransformedGeometry *iface,
        ID2D1Factory **factory)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, factory %p.\n", iface, factory);

    ID2D1Factory_AddRef(*factory = geometry->factory);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_GetBounds(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, transform %p, bounds %p stub!\n", iface, transform, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_GetWidenedBounds(ID2D1TransformedGeometry *iface,
        float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, D2D1_RECT_F *bounds)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, bounds %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, bounds);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_StrokeContainsPoint(ID2D1TransformedGeometry *iface,
        D2D1_POINT_2F point, float stroke_width, ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, point {%.8e, %.8e}, stroke_width %.8e, stroke_style %p, "
            "transform %p, tolerance %.8e, contains %p.\n",
            iface, point.x, point.y, stroke_width, stroke_style, transform, tolerance, contains);

    g = geometry->transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_StrokeContainsPoint(geometry->u.transformed.src_geometry, point, stroke_width, stroke_style,
            &g, tolerance, contains);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_FillContainsPoint(ID2D1TransformedGeometry *iface,
        D2D1_POINT_2F point, const D2D1_MATRIX_3X2_F *transform, float tolerance, BOOL *contains)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);
    D2D1_MATRIX_3X2_F g;

    TRACE("iface %p, point {%.8e, %.8e}, transform %p, tolerance %.8e, contains %p.\n",
            iface, point.x, point.y, transform, tolerance, contains);

    g = geometry->transform;
    if (transform)
        d2d_matrix_multiply(&g, transform);

    return ID2D1Geometry_FillContainsPoint(geometry->u.transformed.src_geometry, point, &g, tolerance, contains);
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_CompareWithGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry *geometry, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_GEOMETRY_RELATION *relation)
{
    FIXME("iface %p, geometry %p, transform %p, tolerance %.8e, relation %p stub!\n",
            iface, geometry, transform, tolerance, relation);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Simplify(ID2D1TransformedGeometry *iface,
        D2D1_GEOMETRY_SIMPLIFICATION_OPTION option, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, option %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, option, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Tessellate(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1TessellationSink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_CombineWithGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry *geometry, D2D1_COMBINE_MODE combine_mode, const D2D1_MATRIX_3X2_F *transform,
        float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, geometry %p, combine_mode %#x, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, geometry, combine_mode, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Outline(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, transform %p, tolerance %.8e, sink %p stub!\n", iface, transform, tolerance, sink);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputeArea(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *area)
{
    FIXME("iface %p, transform %p, tolerance %.8e, area %p stub!\n", iface, transform, tolerance, area);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputeLength(ID2D1TransformedGeometry *iface,
        const D2D1_MATRIX_3X2_F *transform, float tolerance, float *length)
{
    FIXME("iface %p, transform %p, tolerance %.8e, length %p stub!\n", iface, transform, tolerance, length);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_ComputePointAtLength(ID2D1TransformedGeometry *iface,
        float length, const D2D1_MATRIX_3X2_F *transform, float tolerance, D2D1_POINT_2F *point,
        D2D1_POINT_2F *tangent)
{
    FIXME("iface %p, length %.8e, transform %p, tolerance %.8e, point %p, tangent %p stub!\n",
            iface, length, transform, tolerance, point, tangent);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE d2d_transformed_geometry_Widen(ID2D1TransformedGeometry *iface, float stroke_width,
        ID2D1StrokeStyle *stroke_style, const D2D1_MATRIX_3X2_F *transform, float tolerance,
        ID2D1SimplifiedGeometrySink *sink)
{
    FIXME("iface %p, stroke_width %.8e, stroke_style %p, transform %p, tolerance %.8e, sink %p stub!\n",
            iface, stroke_width, stroke_style, transform, tolerance, sink);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetSourceGeometry(ID2D1TransformedGeometry *iface,
        ID2D1Geometry **src_geometry)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, src_geometry %p.\n", iface, src_geometry);

    ID2D1Geometry_AddRef(*src_geometry = geometry->u.transformed.src_geometry);
}

static void STDMETHODCALLTYPE d2d_transformed_geometry_GetTransform(ID2D1TransformedGeometry *iface,
        D2D1_MATRIX_3X2_F *transform)
{
    struct d2d_geometry *geometry = impl_from_ID2D1TransformedGeometry(iface);

    TRACE("iface %p, transform %p.\n", iface, transform);

    *transform = geometry->transform;
}

static const struct ID2D1TransformedGeometryVtbl d2d_transformed_geometry_vtbl =
{
    d2d_transformed_geometry_QueryInterface,
    d2d_transformed_geometry_AddRef,
    d2d_transformed_geometry_Release,
    d2d_transformed_geometry_GetFactory,
    d2d_transformed_geometry_GetBounds,
    d2d_transformed_geometry_GetWidenedBounds,
    d2d_transformed_geometry_StrokeContainsPoint,
    d2d_transformed_geometry_FillContainsPoint,
    d2d_transformed_geometry_CompareWithGeometry,
    d2d_transformed_geometry_Simplify,
    d2d_transformed_geometry_Tessellate,
    d2d_transformed_geometry_CombineWithGeometry,
    d2d_transformed_geometry_Outline,
    d2d_transformed_geometry_ComputeArea,
    d2d_transformed_geometry_ComputeLength,
    d2d_transformed_geometry_ComputePointAtLength,
    d2d_transformed_geometry_Widen,
    d2d_transformed_geometry_GetSourceGeometry,
    d2d_transformed_geometry_GetTransform,
};

void d2d_transformed_geometry_init(struct d2d_geometry *geometry, ID2D1Factory *factory,
        ID2D1Geometry *src_geometry, const D2D_MATRIX_3X2_F *transform)
{
    struct d2d_geometry *src_impl;

    d2d_geometry_init(geometry, factory, transform, (ID2D1GeometryVtbl *)&d2d_transformed_geometry_vtbl);
    ID2D1Geometry_AddRef(geometry->u.transformed.src_geometry = src_geometry);
    src_impl = unsafe_impl_from_ID2D1Geometry(src_geometry);
    geometry->fill = src_impl->fill;
}

struct d2d_geometry *unsafe_impl_from_ID2D1Geometry(ID2D1Geometry *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (const ID2D1GeometryVtbl *)&d2d_path_geometry_vtbl
            || iface->lpVtbl == (const ID2D1GeometryVtbl *)&d2d_rectangle_geometry_vtbl
            || iface->lpVtbl == (const ID2D1GeometryVtbl *)&d2d_transformed_geometry_vtbl);
    return CONTAINING_RECORD(iface, struct d2d_geometry, ID2D1Geometry_iface);
}
