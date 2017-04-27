/*
 *    Text format and layout
 *
 * Copyright 2012, 2014-2017 Nikolay Sivov for CodeWeavers
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

#include <stdarg.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "dwrite_private.h"
#include "scripts.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwrite);

struct dwrite_textformat_data {
    WCHAR *family_name;
    UINT32 family_len;
    WCHAR *locale;
    UINT32 locale_len;

    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;

    DWRITE_PARAGRAPH_ALIGNMENT paralign;
    DWRITE_READING_DIRECTION readingdir;
    DWRITE_WORD_WRAPPING wrapping;
    BOOL last_line_wrapping;
    DWRITE_TEXT_ALIGNMENT textalignment;
    DWRITE_FLOW_DIRECTION flow;
    DWRITE_VERTICAL_GLYPH_ORIENTATION vertical_orientation;
    DWRITE_OPTICAL_ALIGNMENT optical_alignment;
    DWRITE_LINE_SPACING spacing;

    FLOAT fontsize;

    DWRITE_TRIMMING trimming;
    IDWriteInlineObject *trimmingsign;

    IDWriteFontCollection *collection;
    IDWriteFontFallback *fallback;
};

enum layout_range_attr_kind {
    LAYOUT_RANGE_ATTR_WEIGHT,
    LAYOUT_RANGE_ATTR_STYLE,
    LAYOUT_RANGE_ATTR_STRETCH,
    LAYOUT_RANGE_ATTR_FONTSIZE,
    LAYOUT_RANGE_ATTR_EFFECT,
    LAYOUT_RANGE_ATTR_INLINE,
    LAYOUT_RANGE_ATTR_UNDERLINE,
    LAYOUT_RANGE_ATTR_STRIKETHROUGH,
    LAYOUT_RANGE_ATTR_PAIR_KERNING,
    LAYOUT_RANGE_ATTR_FONTCOLL,
    LAYOUT_RANGE_ATTR_LOCALE,
    LAYOUT_RANGE_ATTR_FONTFAMILY,
    LAYOUT_RANGE_ATTR_SPACING,
    LAYOUT_RANGE_ATTR_TYPOGRAPHY
};

struct layout_range_attr_value {
    DWRITE_TEXT_RANGE range;
    union {
        DWRITE_FONT_WEIGHT weight;
        DWRITE_FONT_STYLE style;
        DWRITE_FONT_STRETCH stretch;
        FLOAT fontsize;
        IDWriteInlineObject *object;
        IUnknown *effect;
        BOOL underline;
        BOOL strikethrough;
        BOOL pair_kerning;
        IDWriteFontCollection *collection;
        const WCHAR *locale;
        const WCHAR *fontfamily;
        FLOAT spacing[3]; /* in arguments order - leading, trailing, advance */
        IDWriteTypography *typography;
    } u;
};

enum layout_range_kind {
    LAYOUT_RANGE_REGULAR,
    LAYOUT_RANGE_UNDERLINE,
    LAYOUT_RANGE_STRIKETHROUGH,
    LAYOUT_RANGE_EFFECT,
    LAYOUT_RANGE_SPACING,
    LAYOUT_RANGE_TYPOGRAPHY
};

struct layout_range_header {
    struct list entry;
    enum layout_range_kind kind;
    DWRITE_TEXT_RANGE range;
};

struct layout_range {
    struct layout_range_header h;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    FLOAT fontsize;
    DWRITE_FONT_STRETCH stretch;
    IDWriteInlineObject *object;
    BOOL pair_kerning;
    IDWriteFontCollection *collection;
    WCHAR locale[LOCALE_NAME_MAX_LENGTH];
    WCHAR *fontfamily;
};

struct layout_range_bool {
    struct layout_range_header h;
    BOOL value;
};

struct layout_range_iface {
    struct layout_range_header h;
    IUnknown *iface;
};

struct layout_range_spacing {
    struct layout_range_header h;
    FLOAT leading;
    FLOAT trailing;
    FLOAT min_advance;
};

enum layout_run_kind {
    LAYOUT_RUN_REGULAR,
    LAYOUT_RUN_INLINE
};

struct inline_object_run {
    IDWriteInlineObject *object;
    UINT16 length;
};

struct regular_layout_run {
    DWRITE_GLYPH_RUN_DESCRIPTION descr;
    DWRITE_GLYPH_RUN run;
    DWRITE_SCRIPT_ANALYSIS sa;
    UINT16 *glyphs;
    UINT16 *clustermap;
    FLOAT  *advances;
    DWRITE_GLYPH_OFFSET *offsets;
    /* this is actual glyph count after shaping, it's not necessary the same as reported to Draw() */
    UINT32 glyphcount;
};

struct layout_run {
    struct list entry;
    enum layout_run_kind kind;
    union {
        struct inline_object_run object;
        struct regular_layout_run regular;
    } u;
    FLOAT baseline;
    FLOAT height;
};

struct layout_effective_run {
    struct list entry;
    const struct layout_run *run; /* nominal run this one is based on */
    UINT32 start;           /* relative text position, 0 means first text position of a nominal run */
    UINT32 length;          /* length in codepoints that this run covers */
    UINT32 glyphcount;      /* total glyph count in this run */
    IUnknown *effect;       /* original reference is kept only at range level */
    FLOAT origin_x;         /* baseline X position */
    FLOAT origin_y;         /* baseline Y position */
    FLOAT align_dx;         /* adjustment from text alignment */
    FLOAT width;            /* run width */
    UINT16 *clustermap;     /* effective clustermap, allocated separately, is not reused from nominal map */
    UINT32 line;            /* 0-based line index in line metrics array */
    BOOL underlined;        /* set if this run is underlined */
};

struct layout_effective_inline {
    struct list entry;
    IDWriteInlineObject *object;  /* inline object, set explicitly or added when trimming a line */
    IUnknown *effect;             /* original reference is kept only at range level */
    FLOAT baseline;
    FLOAT origin_x;               /* left X position */
    FLOAT origin_y;               /* left top corner Y position */
    FLOAT align_dx;               /* adjustment from text alignment */
    FLOAT width;                  /* object width as it's reported it */
    BOOL  is_sideways;            /* vertical flow direction flag passed to Draw */
    BOOL  is_rtl;                 /* bidi flag passed to Draw */
    UINT32 line;                  /* 0-based line index in line metrics array */
};

struct layout_underline {
    struct list entry;
    const struct layout_effective_run *run;
    DWRITE_UNDERLINE u;
};

struct layout_strikethrough {
    struct list entry;
    const struct layout_effective_run *run;
    DWRITE_STRIKETHROUGH s;
};

struct layout_cluster {
    const struct layout_run *run; /* link to nominal run this cluster belongs to */
    UINT32 position;        /* relative to run, first cluster has 0 position */
};

struct layout_line {
    FLOAT height;   /* height based on content */
    FLOAT baseline; /* baseline based on content */
};

enum layout_recompute_mask {
    RECOMPUTE_CLUSTERS            = 1 << 0,
    RECOMPUTE_MINIMAL_WIDTH       = 1 << 1,
    RECOMPUTE_LINES               = 1 << 2,
    RECOMPUTE_OVERHANGS           = 1 << 3,
    RECOMPUTE_LINES_AND_OVERHANGS = RECOMPUTE_LINES | RECOMPUTE_OVERHANGS,
    RECOMPUTE_EVERYTHING          = 0xffff
};

struct dwrite_textlayout {
    IDWriteTextLayout3 IDWriteTextLayout3_iface;
    IDWriteTextFormat1 IDWriteTextFormat1_iface;
    IDWriteTextAnalysisSink1 IDWriteTextAnalysisSink1_iface;
    IDWriteTextAnalysisSource1 IDWriteTextAnalysisSource1_iface;
    LONG ref;

    IDWriteFactory4 *factory;

    WCHAR *str;
    UINT32 len;
    struct dwrite_textformat_data format;
    struct list strike_ranges;
    struct list underline_ranges;
    struct list typographies;
    struct list effects;
    struct list spacing;
    struct list ranges;
    struct list runs;
    /* lists ready to use by Draw() */
    struct list eruns;
    struct list inlineobjects;
    struct list underlines;
    struct list strikethrough;
    USHORT recompute;

    DWRITE_LINE_BREAKPOINT *nominal_breakpoints;
    DWRITE_LINE_BREAKPOINT *actual_breakpoints;

    struct layout_cluster *clusters;
    DWRITE_CLUSTER_METRICS *clustermetrics;
    UINT32 cluster_count;
    FLOAT  minwidth;

    struct layout_line *lines;
    DWRITE_LINE_METRICS1 *linemetrics;
    UINT32 line_alloc;

    DWRITE_TEXT_METRICS1 metrics;
    DWRITE_OVERHANG_METRICS overhangs;

    DWRITE_MEASURING_MODE measuringmode;

    /* gdi-compatible layout specifics */
    FLOAT ppdip;
    DWRITE_MATRIX transform;
};

struct dwrite_textformat {
    IDWriteTextFormat2 IDWriteTextFormat2_iface;
    LONG ref;
    struct dwrite_textformat_data format;
};

struct dwrite_trimmingsign {
    IDWriteInlineObject IDWriteInlineObject_iface;
    LONG ref;

    IDWriteTextLayout *layout;
};

struct dwrite_typography {
    IDWriteTypography IDWriteTypography_iface;
    LONG ref;

    DWRITE_FONT_FEATURE *features;
    UINT32 allocated;
    UINT32 count;
};

struct dwrite_vec {
    FLOAT x;
    FLOAT y;
};

static const IDWriteTextFormat2Vtbl dwritetextformatvtbl;

static void release_format_data(struct dwrite_textformat_data *data)
{
    if (data->collection) IDWriteFontCollection_Release(data->collection);
    if (data->fallback) IDWriteFontFallback_Release(data->fallback);
    if (data->trimmingsign) IDWriteInlineObject_Release(data->trimmingsign);
    heap_free(data->family_name);
    heap_free(data->locale);
}

static inline struct dwrite_textlayout *impl_from_IDWriteTextLayout3(IDWriteTextLayout3 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_textlayout, IDWriteTextLayout3_iface);
}

static inline struct dwrite_textlayout *impl_layout_from_IDWriteTextFormat1(IDWriteTextFormat1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_textlayout, IDWriteTextFormat1_iface);
}

static inline struct dwrite_textlayout *impl_from_IDWriteTextAnalysisSink1(IDWriteTextAnalysisSink1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_textlayout, IDWriteTextAnalysisSink1_iface);
}

static inline struct dwrite_textlayout *impl_from_IDWriteTextAnalysisSource1(IDWriteTextAnalysisSource1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_textlayout, IDWriteTextAnalysisSource1_iface);
}

static inline struct dwrite_textformat *impl_from_IDWriteTextFormat2(IDWriteTextFormat2 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_textformat, IDWriteTextFormat2_iface);
}

static struct dwrite_textformat *unsafe_impl_from_IDWriteTextFormat(IDWriteTextFormat*);

static inline struct dwrite_trimmingsign *impl_from_IDWriteInlineObject(IDWriteInlineObject *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_trimmingsign, IDWriteInlineObject_iface);
}

static inline struct dwrite_typography *impl_from_IDWriteTypography(IDWriteTypography *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_typography, IDWriteTypography_iface);
}

static inline const char *debugstr_rundescr(const DWRITE_GLYPH_RUN_DESCRIPTION *descr)
{
    return wine_dbg_sprintf("[%u,%u)", descr->textPosition, descr->textPosition + descr->stringLength);
}

static inline BOOL is_layout_gdi_compatible(struct dwrite_textlayout *layout)
{
    return layout->measuringmode != DWRITE_MEASURING_MODE_NATURAL;
}

static inline HRESULT format_set_textalignment(struct dwrite_textformat_data *format, DWRITE_TEXT_ALIGNMENT alignment,
    BOOL *changed)
{
    if ((UINT32)alignment > DWRITE_TEXT_ALIGNMENT_JUSTIFIED)
        return E_INVALIDARG;
    if (changed) *changed = format->textalignment != alignment;
    format->textalignment = alignment;
    return S_OK;
}

static inline HRESULT format_set_paralignment(struct dwrite_textformat_data *format,
    DWRITE_PARAGRAPH_ALIGNMENT alignment, BOOL *changed)
{
    if ((UINT32)alignment > DWRITE_PARAGRAPH_ALIGNMENT_CENTER)
        return E_INVALIDARG;
    if (changed) *changed = format->paralign != alignment;
    format->paralign = alignment;
    return S_OK;
}

static inline HRESULT format_set_readingdirection(struct dwrite_textformat_data *format,
    DWRITE_READING_DIRECTION direction, BOOL *changed)
{
    if ((UINT32)direction > DWRITE_READING_DIRECTION_BOTTOM_TO_TOP)
        return E_INVALIDARG;
    if (changed) *changed = format->readingdir != direction;
    format->readingdir = direction;
    return S_OK;
}

static inline HRESULT format_set_wordwrapping(struct dwrite_textformat_data *format,
    DWRITE_WORD_WRAPPING wrapping, BOOL *changed)
{
    if ((UINT32)wrapping > DWRITE_WORD_WRAPPING_CHARACTER)
        return E_INVALIDARG;
    if (changed) *changed = format->wrapping != wrapping;
    format->wrapping = wrapping;
    return S_OK;
}

static inline HRESULT format_set_flowdirection(struct dwrite_textformat_data *format,
    DWRITE_FLOW_DIRECTION direction, BOOL *changed)
{
    if ((UINT32)direction > DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT)
        return E_INVALIDARG;
    if (changed) *changed = format->flow != direction;
    format->flow = direction;
    return S_OK;
}

static inline HRESULT format_set_trimming(struct dwrite_textformat_data *format,
    DWRITE_TRIMMING const *trimming, IDWriteInlineObject *trimming_sign, BOOL *changed)
{
    if (changed)
        *changed = FALSE;

    if ((UINT32)trimming->granularity > DWRITE_TRIMMING_GRANULARITY_WORD)
        return E_INVALIDARG;

    if (changed) {
        *changed = !!memcmp(&format->trimming, trimming, sizeof(*trimming));
        if (format->trimmingsign != trimming_sign)
            *changed = TRUE;
    }

    format->trimming = *trimming;
    if (format->trimmingsign)
        IDWriteInlineObject_Release(format->trimmingsign);
    format->trimmingsign = trimming_sign;
    if (format->trimmingsign)
        IDWriteInlineObject_AddRef(format->trimmingsign);
    return S_OK;
}

static inline HRESULT format_set_linespacing(struct dwrite_textformat_data *format,
    DWRITE_LINE_SPACING const *spacing, BOOL *changed)
{
    if (spacing->height < 0.0f || spacing->leadingBefore < 0.0f || spacing->leadingBefore > 1.0f ||
        (UINT32)spacing->method > DWRITE_LINE_SPACING_METHOD_PROPORTIONAL)
        return E_INVALIDARG;

    if (changed)
        *changed = memcmp(spacing, &format->spacing, sizeof(*spacing));

    format->spacing = *spacing;
    return S_OK;
}

static HRESULT get_fontfallback_from_format(const struct dwrite_textformat_data *format, IDWriteFontFallback **fallback)
{
    *fallback = format->fallback;
    if (*fallback)
        IDWriteFontFallback_AddRef(*fallback);
    return S_OK;
}

static HRESULT set_fontfallback_for_format(struct dwrite_textformat_data *format, IDWriteFontFallback *fallback)
{
    if (format->fallback)
        IDWriteFontFallback_Release(format->fallback);
    format->fallback = fallback;
    if (fallback)
        IDWriteFontFallback_AddRef(fallback);
    return S_OK;
}

static HRESULT format_set_optical_alignment(struct dwrite_textformat_data *format,
    DWRITE_OPTICAL_ALIGNMENT alignment)
{
    if ((UINT32)alignment > DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS)
        return E_INVALIDARG;
    format->optical_alignment = alignment;
    return S_OK;
}

static BOOL is_run_rtl(const struct layout_effective_run *run)
{
    return run->run->u.regular.run.bidiLevel & 1;
}

static struct layout_run *alloc_layout_run(enum layout_run_kind kind)
{
    struct layout_run *ret;

    ret = heap_alloc(sizeof(*ret));
    if (!ret) return NULL;

    memset(ret, 0, sizeof(*ret));
    ret->kind = kind;
    if (kind == LAYOUT_RUN_REGULAR) {
        ret->u.regular.sa.script = Script_Unknown;
        ret->u.regular.sa.shapes = DWRITE_SCRIPT_SHAPES_DEFAULT;
    }

    return ret;
}

static void free_layout_runs(struct dwrite_textlayout *layout)
{
    struct layout_run *cur, *cur2;
    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->runs, struct layout_run, entry) {
        list_remove(&cur->entry);
        if (cur->kind == LAYOUT_RUN_REGULAR) {
            if (cur->u.regular.run.fontFace)
                IDWriteFontFace_Release(cur->u.regular.run.fontFace);
            heap_free(cur->u.regular.glyphs);
            heap_free(cur->u.regular.clustermap);
            heap_free(cur->u.regular.advances);
            heap_free(cur->u.regular.offsets);
        }
        heap_free(cur);
    }
}

static void free_layout_eruns(struct dwrite_textlayout *layout)
{
    struct layout_effective_inline *in, *in2;
    struct layout_effective_run *cur, *cur2;
    struct layout_strikethrough *s, *s2;
    struct layout_underline *u, *u2;

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->eruns, struct layout_effective_run, entry) {
        list_remove(&cur->entry);
        heap_free(cur->clustermap);
        heap_free(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(in, in2, &layout->inlineobjects, struct layout_effective_inline, entry) {
        list_remove(&in->entry);
        heap_free(in);
    }

    LIST_FOR_EACH_ENTRY_SAFE(u, u2, &layout->underlines, struct layout_underline, entry) {
        list_remove(&u->entry);
        heap_free(u);
    }

    LIST_FOR_EACH_ENTRY_SAFE(s, s2, &layout->strikethrough, struct layout_strikethrough, entry) {
        list_remove(&s->entry);
        heap_free(s);
    }
}

/* Used to resolve break condition by forcing stronger condition over weaker. */
static inline DWRITE_BREAK_CONDITION override_break_condition(DWRITE_BREAK_CONDITION existingbreak, DWRITE_BREAK_CONDITION newbreak)
{
    switch (existingbreak) {
    case DWRITE_BREAK_CONDITION_NEUTRAL:
        return newbreak;
    case DWRITE_BREAK_CONDITION_CAN_BREAK:
        return newbreak == DWRITE_BREAK_CONDITION_NEUTRAL ? existingbreak : newbreak;
    /* let's keep stronger conditions as is */
    case DWRITE_BREAK_CONDITION_MAY_NOT_BREAK:
    case DWRITE_BREAK_CONDITION_MUST_BREAK:
        break;
    default:
        ERR("unknown break condition %d\n", existingbreak);
    }

    return existingbreak;
}

/* This helper should be used to get effective range length, in other words it returns number of text
   positions from range starting point to the end of the range, limited by layout text length */
static inline UINT32 get_clipped_range_length(const struct dwrite_textlayout *layout, const struct layout_range *range)
{
    if (range->h.range.startPosition + range->h.range.length <= layout->len)
        return range->h.range.length;
    return layout->len - range->h.range.startPosition;
}

/* Actual breakpoint data gets updated with break condition required by inline object set for range 'cur'. */
static HRESULT layout_update_breakpoints_range(struct dwrite_textlayout *layout, const struct layout_range *cur)
{
    DWRITE_BREAK_CONDITION before, after;
    UINT32 i, length;
    HRESULT hr;

    /* ignore returned conditions if failed */
    hr = IDWriteInlineObject_GetBreakConditions(cur->object, &before, &after);
    if (FAILED(hr))
        after = before = DWRITE_BREAK_CONDITION_NEUTRAL;

    if (!layout->actual_breakpoints) {
        layout->actual_breakpoints = heap_alloc(sizeof(DWRITE_LINE_BREAKPOINT)*layout->len);
        if (!layout->actual_breakpoints)
            return E_OUTOFMEMORY;
        memcpy(layout->actual_breakpoints, layout->nominal_breakpoints, sizeof(DWRITE_LINE_BREAKPOINT)*layout->len);
    }

    length = get_clipped_range_length(layout, cur);
    for (i = cur->h.range.startPosition; i < length + cur->h.range.startPosition; i++) {
        /* for first codepoint check if there's anything before it and update accordingly */
        if (i == cur->h.range.startPosition) {
            if (i > 0)
                layout->actual_breakpoints[i].breakConditionBefore = layout->actual_breakpoints[i-1].breakConditionAfter =
                    override_break_condition(layout->actual_breakpoints[i-1].breakConditionAfter, before);
            else
                layout->actual_breakpoints[i].breakConditionBefore = before;
            layout->actual_breakpoints[i].breakConditionAfter = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
        }
        /* similar check for last codepoint */
        else if (i == cur->h.range.startPosition + length - 1) {
            if (i == layout->len - 1)
                layout->actual_breakpoints[i].breakConditionAfter = after;
            else
                layout->actual_breakpoints[i].breakConditionAfter = layout->actual_breakpoints[i+1].breakConditionBefore =
                    override_break_condition(layout->actual_breakpoints[i+1].breakConditionBefore, after);
            layout->actual_breakpoints[i].breakConditionBefore = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
        }
        /* for all positions within a range disable breaks */
        else {
            layout->actual_breakpoints[i].breakConditionBefore = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
            layout->actual_breakpoints[i].breakConditionAfter = DWRITE_BREAK_CONDITION_MAY_NOT_BREAK;
        }

        layout->actual_breakpoints[i].isWhitespace = 0;
        layout->actual_breakpoints[i].isSoftHyphen = 0;
    }

    return S_OK;
}

static struct layout_range *get_layout_range_by_pos(struct dwrite_textlayout *layout, UINT32 pos);

static inline DWRITE_LINE_BREAKPOINT get_effective_breakpoint(const struct dwrite_textlayout *layout, UINT32 pos)
{
    if (layout->actual_breakpoints)
        return layout->actual_breakpoints[pos];
    return layout->nominal_breakpoints[pos];
}

static inline void init_cluster_metrics(const struct dwrite_textlayout *layout, const struct regular_layout_run *run,
    UINT16 start_glyph, UINT16 stop_glyph, UINT32 stop_position, UINT16 length, DWRITE_CLUSTER_METRICS *metrics)
{
    UINT8 breakcondition;
    UINT32 position;
    UINT16 j;

    /* For clusters made of control chars we report zero glyphs, and we need zero cluster
       width as well; advances are already computed at this point and are not necessary zero. */
    metrics->width = 0.0f;
    if (run->run.glyphCount) {
        for (j = start_glyph; j < stop_glyph; j++)
            metrics->width += run->run.glyphAdvances[j];
    }
    metrics->length = length;

    position = run->descr.textPosition + stop_position;
    if (stop_glyph == run->glyphcount)
        breakcondition = get_effective_breakpoint(layout, position).breakConditionAfter;
    else {
        breakcondition = get_effective_breakpoint(layout, position).breakConditionBefore;
        if (stop_position) position -= 1;
    }

    metrics->canWrapLineAfter = breakcondition == DWRITE_BREAK_CONDITION_CAN_BREAK ||
                                breakcondition == DWRITE_BREAK_CONDITION_MUST_BREAK;
    if (metrics->length == 1) {
        DWRITE_LINE_BREAKPOINT bp = get_effective_breakpoint(layout, position);
        metrics->isWhitespace = bp.isWhitespace;
        metrics->isNewline = metrics->canWrapLineAfter && lb_is_newline_char(layout->str[position]);
        metrics->isSoftHyphen = bp.isSoftHyphen;
    }
    else {
        metrics->isWhitespace = 0;
        metrics->isNewline = 0;
        metrics->isSoftHyphen = 0;
    }
    metrics->isRightToLeft = run->run.bidiLevel & 1;
    metrics->padding = 0;
}

/*

  All clusters in a 'run' will be added to 'layout' data, starting at index pointed to by 'cluster'.
  On return 'cluster' is updated to point to next metrics struct to be filled in on next call.
  Note that there's no need to reallocate anything at this point as we allocate one cluster per
  codepoint initially.

*/
static void layout_set_cluster_metrics(struct dwrite_textlayout *layout, const struct layout_run *r, UINT32 *cluster)
{
    DWRITE_CLUSTER_METRICS *metrics = &layout->clustermetrics[*cluster];
    struct layout_cluster *c = &layout->clusters[*cluster];
    const struct regular_layout_run *run = &r->u.regular;
    UINT32 i, start = 0;

    for (i = 0; i < run->descr.stringLength; i++) {
        BOOL end = i == run->descr.stringLength - 1;

        if (run->descr.clusterMap[start] != run->descr.clusterMap[i]) {
            init_cluster_metrics(layout, run, run->descr.clusterMap[start], run->descr.clusterMap[i], i,
                i - start, metrics);
            c->position = start;
            c->run = r;

            *cluster += 1;
            metrics++;
            c++;
            start = i;
        }

        if (end) {
            init_cluster_metrics(layout, run, run->descr.clusterMap[start], run->glyphcount, i,
                i - start + 1, metrics);
            c->position = start;
            c->run = r;

            *cluster += 1;
            return;
        }
    }
}

#define SCALE_FONT_METRIC(metric, emSize, metrics) ((FLOAT)(metric) * (emSize) / (FLOAT)(metrics)->designUnitsPerEm)

static void layout_get_font_metrics(struct dwrite_textlayout *layout, IDWriteFontFace *fontface, FLOAT emsize,
    DWRITE_FONT_METRICS *fontmetrics)
{
    if (is_layout_gdi_compatible(layout)) {
        HRESULT hr = IDWriteFontFace_GetGdiCompatibleMetrics(fontface, emsize, layout->ppdip, &layout->transform, fontmetrics);
        if (FAILED(hr))
            WARN("failed to get compat metrics, 0x%08x\n", hr);
    }
    else
        IDWriteFontFace_GetMetrics(fontface, fontmetrics);
}

static void layout_get_font_height(FLOAT emsize, DWRITE_FONT_METRICS *fontmetrics, FLOAT *baseline, FLOAT *height)
{
    *baseline = SCALE_FONT_METRIC(fontmetrics->ascent + fontmetrics->lineGap, emsize, fontmetrics);
    *height = SCALE_FONT_METRIC(fontmetrics->ascent + fontmetrics->descent + fontmetrics->lineGap, emsize, fontmetrics);
}

static HRESULT layout_compute_runs(struct dwrite_textlayout *layout)
{
    IDWriteFontFallback *fallback;
    IDWriteTextAnalyzer *analyzer;
    struct layout_range *range;
    struct layout_run *r;
    UINT32 cluster = 0;
    HRESULT hr;

    free_layout_eruns(layout);
    free_layout_runs(layout);

    /* Cluster data arrays are allocated once, assuming one text position per cluster. */
    if (!layout->clustermetrics && layout->len) {
        layout->clustermetrics = heap_alloc(layout->len*sizeof(*layout->clustermetrics));
        layout->clusters = heap_alloc(layout->len*sizeof(*layout->clusters));
        if (!layout->clustermetrics || !layout->clusters) {
            heap_free(layout->clustermetrics);
            heap_free(layout->clusters);
            return E_OUTOFMEMORY;
        }
    }
    layout->cluster_count = 0;

    hr = get_textanalyzer(&analyzer);
    if (FAILED(hr))
        return hr;

    LIST_FOR_EACH_ENTRY(range, &layout->ranges, struct layout_range, h.entry) {
        /* we don't care about ranges that don't contain any text */
        if (range->h.range.startPosition >= layout->len)
            break;

        /* inline objects override actual text in a range */
        if (range->object) {
            hr = layout_update_breakpoints_range(layout, range);
            if (FAILED(hr))
                return hr;

            r = alloc_layout_run(LAYOUT_RUN_INLINE);
            if (!r)
                return E_OUTOFMEMORY;

            r->u.object.object = range->object;
            r->u.object.length = get_clipped_range_length(layout, range);
            list_add_tail(&layout->runs, &r->entry);
            continue;
        }

        /* initial splitting by script */
        hr = IDWriteTextAnalyzer_AnalyzeScript(analyzer, (IDWriteTextAnalysisSource*)&layout->IDWriteTextAnalysisSource1_iface,
            range->h.range.startPosition, get_clipped_range_length(layout, range), (IDWriteTextAnalysisSink*)&layout->IDWriteTextAnalysisSink1_iface);
        if (FAILED(hr))
            break;

        /* this splits it further */
        hr = IDWriteTextAnalyzer_AnalyzeBidi(analyzer, (IDWriteTextAnalysisSource*)&layout->IDWriteTextAnalysisSource1_iface,
            range->h.range.startPosition, get_clipped_range_length(layout, range), (IDWriteTextAnalysisSink*)&layout->IDWriteTextAnalysisSink1_iface);
        if (FAILED(hr))
            break;
    }

    if (layout->format.fallback) {
        fallback = layout->format.fallback;
        IDWriteFontFallback_AddRef(fallback);
    }
    else {
        hr = IDWriteFactory4_GetSystemFontFallback(layout->factory, &fallback);
        if (FAILED(hr))
            return hr;
    }

    /* resolve run fonts */
    LIST_FOR_EACH_ENTRY(r, &layout->runs, struct layout_run, entry) {
        struct regular_layout_run *run = &r->u.regular;
        IDWriteFont *font;
        UINT32 length;

        if (r->kind == LAYOUT_RUN_INLINE)
            continue;

        range = get_layout_range_by_pos(layout, run->descr.textPosition);

        if (run->sa.shapes == DWRITE_SCRIPT_SHAPES_NO_VISUAL) {
            IDWriteFontCollection *collection;

            if (range->collection) {
                collection = range->collection;
                IDWriteFontCollection_AddRef(collection);
            }
            else
                IDWriteFactory4_GetSystemFontCollection(layout->factory, FALSE, (IDWriteFontCollection1**)&collection, FALSE);

            hr = create_matching_font(collection, range->fontfamily, range->weight,
                range->style, range->stretch, &font);

            IDWriteFontCollection_Release(collection);

            if (FAILED(hr)) {
                WARN("%s: failed to create a font for non visual run, %s, collection %p\n", debugstr_rundescr(&run->descr),
                    debugstr_w(range->fontfamily), range->collection);
                return hr;
            }

            hr = IDWriteFont_CreateFontFace(font, &run->run.fontFace);
            IDWriteFont_Release(font);
            if (FAILED(hr))
                return hr;

            run->run.fontEmSize = range->fontsize;
            continue;
        }

        length = run->descr.stringLength;

        while (length) {
            UINT32 mapped_length;
            FLOAT scale;

            run = &r->u.regular;

            hr = IDWriteFontFallback_MapCharacters(fallback,
                (IDWriteTextAnalysisSource*)&layout->IDWriteTextAnalysisSource1_iface,
                run->descr.textPosition,
                run->descr.stringLength,
                range->collection,
                range->fontfamily,
                range->weight,
                range->style,
                range->stretch,
                &mapped_length,
                &font,
                &scale);
            if (FAILED(hr)) {
                WARN("%s: failed to map family %s, collection %p\n", debugstr_rundescr(&run->descr), debugstr_w(range->fontfamily), range->collection);
                return hr;
            }

            hr = IDWriteFont_CreateFontFace(font, &run->run.fontFace);
            IDWriteFont_Release(font);
            if (FAILED(hr))
                return hr;
            run->run.fontEmSize = range->fontsize * scale;

            if (mapped_length < length) {
                struct regular_layout_run *nextrun;
                struct layout_run *nextr;

                /* keep mapped part for current run, add another run for the rest */
                nextr = alloc_layout_run(LAYOUT_RUN_REGULAR);
                if (!nextr)
                    return E_OUTOFMEMORY;

                *nextr = *r;
                nextrun = &nextr->u.regular;
                nextrun->descr.textPosition = run->descr.textPosition + mapped_length;
                nextrun->descr.stringLength = run->descr.stringLength - mapped_length;
                nextrun->descr.string = &layout->str[nextrun->descr.textPosition];
                run->descr.stringLength = mapped_length;
                list_add_after(&r->entry, &nextr->entry);
                r = nextr;
            }

            length -= mapped_length;
        }
    }

    IDWriteFontFallback_Release(fallback);

    /* fill run info */
    LIST_FOR_EACH_ENTRY(r, &layout->runs, struct layout_run, entry) {
        DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props = NULL;
        DWRITE_SHAPING_TEXT_PROPERTIES *text_props = NULL;
        struct regular_layout_run *run = &r->u.regular;
        DWRITE_FONT_METRICS fontmetrics = { 0 };
        UINT32 max_count;

        /* we need to do very little in case of inline objects */
        if (r->kind == LAYOUT_RUN_INLINE) {
            DWRITE_CLUSTER_METRICS *metrics = &layout->clustermetrics[cluster];
            struct layout_cluster *c = &layout->clusters[cluster];
            DWRITE_INLINE_OBJECT_METRICS inlinemetrics;

            metrics->width = 0.0f;
            metrics->length = r->u.object.length;
            metrics->canWrapLineAfter = 0;
            metrics->isWhitespace = 0;
            metrics->isNewline = 0;
            metrics->isSoftHyphen = 0;
            metrics->isRightToLeft = 0;
            metrics->padding = 0;
            c->run = r;
            c->position = 0; /* there's always one cluster per inline object, so 0 is valid value */
            cluster++;

            /* it's not fatal if GetMetrics() fails, all returned metrics are ignored */
            hr = IDWriteInlineObject_GetMetrics(r->u.object.object, &inlinemetrics);
            if (FAILED(hr)) {
                memset(&inlinemetrics, 0, sizeof(inlinemetrics));
                hr = S_OK;
            }
            metrics->width = inlinemetrics.width;
            r->baseline = inlinemetrics.baseline;
            r->height = inlinemetrics.height;

            /* FIXME: use resolved breakpoints in this case too */

            continue;
        }

        range = get_layout_range_by_pos(layout, run->descr.textPosition);
        run->descr.localeName = range->locale;
        run->clustermap = heap_alloc(run->descr.stringLength*sizeof(UINT16));

        max_count = 3*run->descr.stringLength/2 + 16;
        run->glyphs = heap_alloc(max_count*sizeof(UINT16));
        if (!run->clustermap || !run->glyphs)
            goto memerr;

        text_props = heap_alloc(run->descr.stringLength*sizeof(DWRITE_SHAPING_TEXT_PROPERTIES));
        glyph_props = heap_alloc(max_count*sizeof(DWRITE_SHAPING_GLYPH_PROPERTIES));
        if (!text_props || !glyph_props)
            goto memerr;

        while (1) {
            hr = IDWriteTextAnalyzer_GetGlyphs(analyzer, run->descr.string, run->descr.stringLength,
                run->run.fontFace, run->run.isSideways, run->run.bidiLevel & 1, &run->sa, run->descr.localeName,
                NULL /* FIXME */, NULL, NULL, 0, max_count, run->clustermap, text_props, run->glyphs, glyph_props,
                &run->glyphcount);
            if (hr == E_NOT_SUFFICIENT_BUFFER) {
                heap_free(run->glyphs);
                heap_free(glyph_props);

                max_count = run->glyphcount;

                run->glyphs = heap_alloc(max_count*sizeof(UINT16));
                glyph_props = heap_alloc(max_count*sizeof(DWRITE_SHAPING_GLYPH_PROPERTIES));
                if (!run->glyphs || !glyph_props)
                    goto memerr;

                continue;
            }

            break;
        }

        if (FAILED(hr)) {
            heap_free(text_props);
            heap_free(glyph_props);
            WARN("%s: shaping failed 0x%08x\n", debugstr_rundescr(&run->descr), hr);
            continue;
        }

        run->run.glyphIndices = run->glyphs;
        run->descr.clusterMap = run->clustermap;

        run->advances = heap_alloc(run->glyphcount*sizeof(FLOAT));
        run->offsets = heap_alloc(run->glyphcount*sizeof(DWRITE_GLYPH_OFFSET));
        if (!run->advances || !run->offsets)
            goto memerr;

        /* now set advances and offsets */
        if (is_layout_gdi_compatible(layout))
            hr = IDWriteTextAnalyzer_GetGdiCompatibleGlyphPlacements(analyzer, run->descr.string, run->descr.clusterMap,
                text_props, run->descr.stringLength, run->run.glyphIndices, glyph_props, run->glyphcount,
                run->run.fontFace, run->run.fontEmSize, layout->ppdip, &layout->transform,
                layout->measuringmode == DWRITE_MEASURING_MODE_GDI_NATURAL, run->run.isSideways,
                run->run.bidiLevel & 1, &run->sa, run->descr.localeName, NULL, NULL, 0, run->advances, run->offsets);
        else
            hr = IDWriteTextAnalyzer_GetGlyphPlacements(analyzer, run->descr.string, run->descr.clusterMap, text_props,
                run->descr.stringLength, run->run.glyphIndices, glyph_props, run->glyphcount, run->run.fontFace,
                run->run.fontEmSize, run->run.isSideways, run->run.bidiLevel & 1, &run->sa, run->descr.localeName,
                NULL, NULL, 0, run->advances, run->offsets);

        heap_free(text_props);
        heap_free(glyph_props);
        if (FAILED(hr))
            WARN("%s: failed to get glyph placement info, 0x%08x\n", debugstr_rundescr(&run->descr), hr);

        run->run.glyphAdvances = run->advances;
        run->run.glyphOffsets = run->offsets;

        /* Special treatment for runs that don't produce visual output, shaping code adds normal glyphs for them,
           with valid cluster map and potentially with non-zero advances; layout code exposes those as zero width clusters. */
        if (run->sa.shapes == DWRITE_SCRIPT_SHAPES_NO_VISUAL)
            run->run.glyphCount = 0;
        else
            run->run.glyphCount = run->glyphcount;

        /* baseline derived from font metrics */
        layout_get_font_metrics(layout, run->run.fontFace, run->run.fontEmSize, &fontmetrics);
        layout_get_font_height(run->run.fontEmSize, &fontmetrics, &r->baseline, &r->height);

        layout_set_cluster_metrics(layout, r, &cluster);
        continue;

    memerr:
        heap_free(text_props);
        heap_free(glyph_props);
        heap_free(run->clustermap);
        heap_free(run->glyphs);
        heap_free(run->advances);
        heap_free(run->offsets);
        run->advances = NULL;
        run->offsets = NULL;
        run->clustermap = run->glyphs = NULL;
        hr = E_OUTOFMEMORY;
        break;
    }

    if (hr == S_OK) {
        layout->cluster_count = cluster;
        if (cluster)
            layout->clustermetrics[cluster-1].canWrapLineAfter = 1;
    }

    IDWriteTextAnalyzer_Release(analyzer);
    return hr;
}

static HRESULT layout_compute(struct dwrite_textlayout *layout)
{
    HRESULT hr;

    if (!(layout->recompute & RECOMPUTE_CLUSTERS))
        return S_OK;

    /* nominal breakpoints are evaluated only once, because string never changes */
    if (!layout->nominal_breakpoints) {
        IDWriteTextAnalyzer *analyzer;
        HRESULT hr;

        layout->nominal_breakpoints = heap_alloc(sizeof(DWRITE_LINE_BREAKPOINT)*layout->len);
        if (!layout->nominal_breakpoints)
            return E_OUTOFMEMORY;

        hr = get_textanalyzer(&analyzer);
        if (FAILED(hr))
            return hr;

        hr = IDWriteTextAnalyzer_AnalyzeLineBreakpoints(analyzer, (IDWriteTextAnalysisSource*)&layout->IDWriteTextAnalysisSource1_iface,
            0, layout->len, (IDWriteTextAnalysisSink*)&layout->IDWriteTextAnalysisSink1_iface);
        IDWriteTextAnalyzer_Release(analyzer);
    }
    if (layout->actual_breakpoints) {
        heap_free(layout->actual_breakpoints);
        layout->actual_breakpoints = NULL;
    }

    hr = layout_compute_runs(layout);

    if (TRACE_ON(dwrite)) {
        struct layout_run *cur;

        LIST_FOR_EACH_ENTRY(cur, &layout->runs, struct layout_run, entry) {
            if (cur->kind == LAYOUT_RUN_INLINE)
                TRACE("run inline object %p, len %u\n", cur->u.object.object, cur->u.object.length);
            else
                TRACE("run [%u,%u], len %u, bidilevel %u\n", cur->u.regular.descr.textPosition, cur->u.regular.descr.textPosition +
                    cur->u.regular.descr.stringLength-1, cur->u.regular.descr.stringLength, cur->u.regular.run.bidiLevel);
        }
    }

    layout->recompute &= ~RECOMPUTE_CLUSTERS;
    return hr;
}

static inline FLOAT get_cluster_range_width(struct dwrite_textlayout *layout, UINT32 start, UINT32 end)
{
    FLOAT width = 0.0f;
    for (; start < end; start++)
        width += layout->clustermetrics[start].width;
    return width;
}

static struct layout_range_header *get_layout_range_header_by_pos(struct list *ranges, UINT32 pos)
{
    struct layout_range_header *cur;

    LIST_FOR_EACH_ENTRY(cur, ranges, struct layout_range_header, entry) {
        DWRITE_TEXT_RANGE *r = &cur->range;
        if (r->startPosition <= pos && pos < r->startPosition + r->length)
            return cur;
    }

    return NULL;
}

static inline IUnknown *layout_get_effect_from_pos(struct dwrite_textlayout *layout, UINT32 pos)
{
    struct layout_range_header *h = get_layout_range_header_by_pos(&layout->effects, pos);
    return ((struct layout_range_iface*)h)->iface;
}

static inline BOOL layout_is_erun_rtl(const struct layout_effective_run *erun)
{
    return erun->run->u.regular.run.bidiLevel & 1;
}

/* A set of parameters that additionally splits resulting runs. It happens after shaping and all text processing,
   no glyph changes are possible. It's understandable for drawing effects, because DrawGlyphRun() reports them as
   one of the arguments, but it also happens for decorations, so every effective run has uniform
   underline/strikethough/effect tuple. */
struct layout_final_splitting_params {
    BOOL strikethrough;
    BOOL underline;
    IUnknown *effect;
};

static inline BOOL layout_get_strikethrough_from_pos(struct dwrite_textlayout *layout, UINT32 pos)
{
    struct layout_range_header *h = get_layout_range_header_by_pos(&layout->strike_ranges, pos);
    return ((struct layout_range_bool*)h)->value;
}

static inline BOOL layout_get_underline_from_pos(struct dwrite_textlayout *layout, UINT32 pos)
{
    struct layout_range_header *h = get_layout_range_header_by_pos(&layout->underline_ranges, pos);
    return ((struct layout_range_bool*)h)->value;
}

static void layout_splitting_params_from_pos(struct dwrite_textlayout *layout, UINT32 pos,
    struct layout_final_splitting_params *params)
{
    params->strikethrough = layout_get_strikethrough_from_pos(layout, pos);
    params->underline = layout_get_underline_from_pos(layout, pos);
    params->effect = layout_get_effect_from_pos(layout, pos);
}

static BOOL is_same_splitting_params(const struct layout_final_splitting_params *left,
    const struct layout_final_splitting_params *right)
{
    return left->strikethrough == right->strikethrough &&
           left->underline == right->underline &&
           left->effect == right->effect;
}

static void layout_get_erun_font_metrics(struct dwrite_textlayout *layout, struct layout_effective_run *erun,
    DWRITE_FONT_METRICS *metrics)
{
    memset(metrics, 0, sizeof(*metrics));
    if (is_layout_gdi_compatible(layout)) {
        HRESULT hr = IDWriteFontFace_GetGdiCompatibleMetrics(
            erun->run->u.regular.run.fontFace,
            erun->run->u.regular.run.fontEmSize,
            layout->ppdip,
            &layout->transform,
            metrics);
        if (FAILED(hr))
            WARN("failed to get font metrics, 0x%08x\n", hr);
    }
    else
        IDWriteFontFace_GetMetrics(erun->run->u.regular.run.fontFace, metrics);
}

/* Effective run is built from consecutive clusters of a single nominal run, 'first_cluster' is 0 based cluster index,
   'cluster_count' indicates how many clusters to add, including first one. */
static HRESULT layout_add_effective_run(struct dwrite_textlayout *layout, const struct layout_run *r, UINT32 first_cluster,
    UINT32 cluster_count, UINT32 line, FLOAT origin_x, struct layout_final_splitting_params *params)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    UINT32 i, start, length, last_cluster;
    struct layout_effective_run *run;

    if (r->kind == LAYOUT_RUN_INLINE) {
        struct layout_effective_inline *inlineobject;

        inlineobject = heap_alloc(sizeof(*inlineobject));
        if (!inlineobject)
            return E_OUTOFMEMORY;

        inlineobject->object = r->u.object.object;
        inlineobject->width = get_cluster_range_width(layout, first_cluster, first_cluster + cluster_count);
        inlineobject->origin_x = is_rtl ? origin_x - inlineobject->width : origin_x;
        inlineobject->origin_y = 0.0f; /* set after line is built */
        inlineobject->align_dx = 0.0f;
        inlineobject->baseline = r->baseline;

        /* It's not clear how these two are set, possibly directionality
           is derived from surrounding text (replaced text could have
           different ranges which differ in reading direction). */
        inlineobject->is_sideways = FALSE;
        inlineobject->is_rtl = FALSE;
        inlineobject->line = line;

        /* effect assigned from start position and on is used for inline objects */
        inlineobject->effect = layout_get_effect_from_pos(layout, layout->clusters[first_cluster].position);

        list_add_tail(&layout->inlineobjects, &inlineobject->entry);
        return S_OK;
    }

    run = heap_alloc(sizeof(*run));
    if (!run)
        return E_OUTOFMEMORY;

    /* No need to iterate for that, use simple fact that:
       <last cluster position> = <first cluster position> + <sum of cluster lengths not including last one> */
    last_cluster = first_cluster + cluster_count - 1;
    length = layout->clusters[last_cluster].position - layout->clusters[first_cluster].position +
        layout->clustermetrics[last_cluster].length;

    run->clustermap = heap_alloc(sizeof(UINT16)*length);
    if (!run->clustermap) {
        heap_free(run);
        return E_OUTOFMEMORY;
    }

    run->run = r;
    run->start = start = layout->clusters[first_cluster].position;
    run->length = length;
    run->width = get_cluster_range_width(layout, first_cluster, first_cluster + cluster_count);

    /* Check if run direction matches paragraph direction, if it doesn't adjust by
       run width */
    if (layout_is_erun_rtl(run) ^ is_rtl)
        run->origin_x = is_rtl ? origin_x - run->width : origin_x + run->width;
    else
        run->origin_x = origin_x;

    run->origin_y = 0.0f; /* set after line is built */
    run->align_dx = 0.0f;
    run->line = line;

    if (r->u.regular.run.glyphCount) {
        /* trim from the left */
        run->glyphcount = r->u.regular.run.glyphCount - r->u.regular.clustermap[start];
        /* trim from the right */
        if (start + length < r->u.regular.descr.stringLength - 1)
            run->glyphcount -= r->u.regular.run.glyphCount - r->u.regular.clustermap[start + length];
    }
    else
        run->glyphcount = 0;

    /* cluster map needs to be shifted */
    for (i = 0; i < length; i++)
        run->clustermap[i] = r->u.regular.clustermap[start + i] - r->u.regular.clustermap[start];

    run->effect = params->effect;
    run->underlined = params->underline;
    list_add_tail(&layout->eruns, &run->entry);

    /* Strikethrough style is guaranteed to be consistent within effective run,
       its width equals to run width, thickness and offset are derived from
       font metrics, rest of the values are from layout or run itself */
    if (params->strikethrough) {
        struct layout_strikethrough *s;
        DWRITE_FONT_METRICS metrics;

        s = heap_alloc(sizeof(*s));
        if (!s)
            return E_OUTOFMEMORY;

        layout_get_erun_font_metrics(layout, run, &metrics);
        s->s.width = get_cluster_range_width(layout, first_cluster, first_cluster + cluster_count);
        s->s.thickness = SCALE_FONT_METRIC(metrics.strikethroughThickness, r->u.regular.run.fontEmSize, &metrics);
        /* Negative offset moves it above baseline as Y coordinate grows downward. */
        s->s.offset = -SCALE_FONT_METRIC(metrics.strikethroughPosition, r->u.regular.run.fontEmSize, &metrics);
        s->s.readingDirection = layout->format.readingdir;
        s->s.flowDirection = layout->format.flow;
        s->s.localeName = r->u.regular.descr.localeName;
        s->s.measuringMode = layout->measuringmode;
        s->run = run;

        list_add_tail(&layout->strikethrough, &s->entry);
    }

    return S_OK;
}

static HRESULT layout_set_line_metrics(struct dwrite_textlayout *layout, DWRITE_LINE_METRICS1 *metrics)
{
    UINT32 i = layout->metrics.lineCount;

    if (!layout->line_alloc) {
        layout->line_alloc = 5;
        layout->linemetrics = heap_alloc(layout->line_alloc * sizeof(*layout->linemetrics));
        layout->lines = heap_alloc(layout->line_alloc * sizeof(*layout->lines));
        if (!layout->linemetrics || !layout->lines) {
            heap_free(layout->linemetrics);
            heap_free(layout->lines);
            layout->linemetrics = NULL;
            layout->lines = NULL;
            return E_OUTOFMEMORY;
        }
    }

    if (layout->metrics.lineCount == layout->line_alloc) {
        DWRITE_LINE_METRICS1 *metrics;
        struct layout_line *lines;

        if ((metrics = heap_realloc(layout->linemetrics, layout->line_alloc * 2 * sizeof(*layout->linemetrics))))
            layout->linemetrics = metrics;
        if ((lines = heap_realloc(layout->lines, layout->line_alloc * 2 * sizeof(*layout->lines))))
            layout->lines = lines;

        if (!metrics || !lines)
            return E_OUTOFMEMORY;

        layout->line_alloc *= 2;
    }

    layout->linemetrics[i] = *metrics;

    switch (layout->format.spacing.method)
    {
    case DWRITE_LINE_SPACING_METHOD_UNIFORM:
        if (layout->format.spacing.method == DWRITE_LINE_SPACING_METHOD_UNIFORM) {
            layout->linemetrics[i].height = layout->format.spacing.height;
            layout->linemetrics[i].baseline = layout->format.spacing.baseline;
        }
        break;
    case DWRITE_LINE_SPACING_METHOD_PROPORTIONAL:
        if (layout->format.spacing.method == DWRITE_LINE_SPACING_METHOD_UNIFORM) {
            layout->linemetrics[i].height = layout->format.spacing.height * metrics->height;
            layout->linemetrics[i].baseline = layout->format.spacing.baseline * metrics->baseline;
        }
        break;
    default:
        /* using content values */;
    }

    layout->lines[i].height = metrics->height;
    layout->lines[i].baseline = metrics->baseline;

    layout->metrics.lineCount++;
    return S_OK;
}

static inline struct layout_effective_run *layout_get_next_erun(struct dwrite_textlayout *layout,
    const struct layout_effective_run *cur)
{
    struct list *e;

    if (!cur)
        e = list_head(&layout->eruns);
    else
        e = list_next(&layout->eruns, &cur->entry);
    if (!e)
        return NULL;
    return LIST_ENTRY(e, struct layout_effective_run, entry);
}

static inline struct layout_effective_run *layout_get_prev_erun(struct dwrite_textlayout *layout,
    const struct layout_effective_run *cur)
{
    struct list *e;

    if (!cur)
        e = list_tail(&layout->eruns);
    else
        e = list_prev(&layout->eruns, &cur->entry);
    if (!e)
        return NULL;
    return LIST_ENTRY(e, struct layout_effective_run, entry);
}

static inline struct layout_effective_inline *layout_get_next_inline_run(struct dwrite_textlayout *layout,
    const struct layout_effective_inline *cur)
{
    struct list *e;

    if (!cur)
        e = list_head(&layout->inlineobjects);
    else
        e = list_next(&layout->inlineobjects, &cur->entry);
    if (!e)
        return NULL;
    return LIST_ENTRY(e, struct layout_effective_inline, entry);
}

static FLOAT layout_get_line_width(struct dwrite_textlayout *layout,
    struct layout_effective_run *erun, struct layout_effective_inline *inrun, UINT32 line)
{
    FLOAT width = 0.0f;

    while (erun && erun->line == line) {
        width += erun->width;
        erun = layout_get_next_erun(layout, erun);
        if (!erun)
            break;
    }

    while (inrun && inrun->line == line) {
        width += inrun->width;
        inrun = layout_get_next_inline_run(layout, inrun);
        if (!inrun)
            break;
    }

    return width;
}

static inline BOOL should_skip_transform(const DWRITE_MATRIX *m, FLOAT *det)
{
    *det = m->m11 * m->m22 - m->m12 * m->m21;
    /* on certain conditions we can skip transform */
    return (!memcmp(m, &identity, sizeof(*m)) || fabsf(*det) <= 1e-10f);
}

static inline void layout_apply_snapping(struct dwrite_vec *vec, BOOL skiptransform, FLOAT ppdip,
    const DWRITE_MATRIX *m, FLOAT det)
{
    if (!skiptransform) {
        FLOAT vec2[2];

        /* apply transform */
        vec->x *= ppdip;
        vec->y *= ppdip;

        vec2[0] = m->m11 * vec->x + m->m21 * vec->y + m->dx;
        vec2[1] = m->m12 * vec->x + m->m22 * vec->y + m->dy;

        /* snap */
        vec2[0] = floorf(vec2[0] + 0.5f);
        vec2[1] = floorf(vec2[1] + 0.5f);

        /* apply inverted transform, we don't care about X component at this point */
        vec->x = (m->m22 * vec2[0] - m->m21 * vec2[1] + m->m21 * m->dy - m->m22 * m->dx) / det;
        vec->x /= ppdip;

        vec->y = (-m->m12 * vec2[0] + m->m11 * vec2[1] - (m->m11 * m->dy - m->m12 * m->dx)) / det;
        vec->y /= ppdip;
    }
    else {
        vec->x = floorf(vec->x * ppdip + 0.5f) / ppdip;
        vec->y = floorf(vec->y * ppdip + 0.5f) / ppdip;
    }
}

static void layout_apply_leading_alignment(struct dwrite_textlayout *layout)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    struct layout_effective_inline *inrun;
    struct layout_effective_run *erun;

    erun = layout_get_next_erun(layout, NULL);
    inrun = layout_get_next_inline_run(layout, NULL);

    while (erun) {
        erun->align_dx = 0.0f;
        erun = layout_get_next_erun(layout, erun);
    }

    while (inrun) {
        inrun->align_dx = 0.0f;
        inrun = layout_get_next_inline_run(layout, inrun);
    }

    layout->metrics.left = is_rtl ? layout->metrics.layoutWidth - layout->metrics.width : 0.0f;
}

static void layout_apply_trailing_alignment(struct dwrite_textlayout *layout)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    struct layout_effective_inline *inrun;
    struct layout_effective_run *erun;
    UINT32 line;

    erun = layout_get_next_erun(layout, NULL);
    inrun = layout_get_next_inline_run(layout, NULL);

    for (line = 0; line < layout->metrics.lineCount; line++) {
        FLOAT width = layout_get_line_width(layout, erun, inrun, line);
        FLOAT shift = layout->metrics.layoutWidth - width;

        if (is_rtl)
            shift *= -1.0f;

        while (erun && erun->line == line) {
            erun->align_dx = shift;
            erun = layout_get_next_erun(layout, erun);
        }

        while (inrun && inrun->line == line) {
            inrun->align_dx = shift;
            inrun = layout_get_next_inline_run(layout, inrun);
        }
    }

    layout->metrics.left = is_rtl ? 0.0f : layout->metrics.layoutWidth - layout->metrics.width;
}

static inline FLOAT layout_get_centered_shift(struct dwrite_textlayout *layout, BOOL skiptransform,
    FLOAT width, FLOAT det)
{
    if (is_layout_gdi_compatible(layout)) {
        struct dwrite_vec vec = { layout->metrics.layoutWidth - width, 0.0f};
        layout_apply_snapping(&vec, skiptransform, layout->ppdip, &layout->transform, det);
        return floorf(vec.x / 2.0f);
    }
    else
        return (layout->metrics.layoutWidth - width) / 2.0f;
}

static void layout_apply_centered_alignment(struct dwrite_textlayout *layout)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    struct layout_effective_inline *inrun;
    struct layout_effective_run *erun;
    BOOL skiptransform;
    UINT32 line;
    FLOAT det;

    erun = layout_get_next_erun(layout, NULL);
    inrun = layout_get_next_inline_run(layout, NULL);

    skiptransform = should_skip_transform(&layout->transform, &det);

    for (line = 0; line < layout->metrics.lineCount; line++) {
        FLOAT width = layout_get_line_width(layout, erun, inrun, line);
        FLOAT shift = layout_get_centered_shift(layout, skiptransform, width, det);

        if (is_rtl)
            shift *= -1.0f;

        while (erun && erun->line == line) {
            erun->align_dx = shift;
            erun = layout_get_next_erun(layout, erun);
        }

        while (inrun && inrun->line == line) {
            inrun->align_dx = shift;
            inrun = layout_get_next_inline_run(layout, inrun);
        }
    }

    layout->metrics.left = (layout->metrics.layoutWidth - layout->metrics.width) / 2.0f;
}

static void layout_apply_text_alignment(struct dwrite_textlayout *layout)
{
    switch (layout->format.textalignment)
    {
    case DWRITE_TEXT_ALIGNMENT_LEADING:
        layout_apply_leading_alignment(layout);
        break;
    case DWRITE_TEXT_ALIGNMENT_TRAILING:
        layout_apply_trailing_alignment(layout);
        break;
    case DWRITE_TEXT_ALIGNMENT_CENTER:
        layout_apply_centered_alignment(layout);
        break;
    case DWRITE_TEXT_ALIGNMENT_JUSTIFIED:
        FIXME("alignment %d not implemented\n", layout->format.textalignment);
        break;
    default:
        ;
    }
}

static void layout_apply_par_alignment(struct dwrite_textlayout *layout)
{
    struct layout_effective_inline *inrun;
    struct layout_effective_run *erun;
    FLOAT origin_y = 0.0f;
    UINT32 line;

    /* alignment mode defines origin, after that all run origins are updated
       the same way */

    switch (layout->format.paralign)
    {
    case DWRITE_PARAGRAPH_ALIGNMENT_NEAR:
        origin_y = 0.0f;
        break;
    case DWRITE_PARAGRAPH_ALIGNMENT_FAR:
        origin_y = layout->metrics.layoutHeight - layout->metrics.height;
        break;
    case DWRITE_PARAGRAPH_ALIGNMENT_CENTER:
        origin_y = (layout->metrics.layoutHeight - layout->metrics.height) / 2.0f;
        break;
    default:
        ;
    }

    layout->metrics.top = origin_y;

    erun = layout_get_next_erun(layout, NULL);
    inrun = layout_get_next_inline_run(layout, NULL);
    for (line = 0; line < layout->metrics.lineCount; line++) {
        FLOAT pos_y = origin_y + layout->linemetrics[line].baseline;

        while (erun && erun->line == line) {
            erun->origin_y = pos_y;
            erun = layout_get_next_erun(layout, erun);
        }

        while (inrun && inrun->line == line) {
            inrun->origin_y = pos_y - inrun->baseline;
            inrun = layout_get_next_inline_run(layout, inrun);
        }

        origin_y += layout->linemetrics[line].height;
    }
}

struct layout_underline_splitting_params {
    const WCHAR *locale; /* points to range data, no additional allocation */
    IUnknown *effect;    /* does not hold another reference */
};

static void init_u_splitting_params_from_erun(struct layout_effective_run *erun,
    struct layout_underline_splitting_params *params)
{
    params->locale = erun->run->u.regular.descr.localeName;
    params->effect = erun->effect;
}

static BOOL is_same_u_splitting(struct layout_underline_splitting_params *left,
    struct layout_underline_splitting_params *right)
{
    return left->effect == right->effect && !strcmpiW(left->locale, right->locale);
}

static HRESULT layout_add_underline(struct dwrite_textlayout *layout, struct layout_effective_run *first,
    struct layout_effective_run *last)
{
    FLOAT thickness, offset, runheight;
    struct layout_effective_run *cur;
    DWRITE_FONT_METRICS metrics;

    if (first == layout_get_prev_erun(layout, last)) {
        layout_get_erun_font_metrics(layout, first, &metrics);
        thickness = SCALE_FONT_METRIC(metrics.underlineThickness, first->run->u.regular.run.fontEmSize, &metrics);
        offset = SCALE_FONT_METRIC(metrics.underlinePosition, first->run->u.regular.run.fontEmSize, &metrics);
        runheight = SCALE_FONT_METRIC(metrics.capHeight, first->run->u.regular.run.fontEmSize, &metrics);
    }
    else {
        FLOAT width = 0.0f;

        /* Single underline is added for consecutive underlined runs. In this case underline parameters are
           calculated as weighted average, where run width acts as a weight. */
        thickness = offset = runheight = 0.0f;
        cur = first;
        do {
            layout_get_erun_font_metrics(layout, cur, &metrics);

            thickness += SCALE_FONT_METRIC(metrics.underlineThickness, cur->run->u.regular.run.fontEmSize, &metrics) * cur->width;
            offset += SCALE_FONT_METRIC(metrics.underlinePosition, cur->run->u.regular.run.fontEmSize, &metrics) * cur->width;
            runheight = max(SCALE_FONT_METRIC(metrics.capHeight, cur->run->u.regular.run.fontEmSize, &metrics), runheight);
            width += cur->width;

            cur = layout_get_next_erun(layout, cur);
        } while (cur != last);

        thickness /= width;
        offset /= width;
    }

    cur = first;
    do {
        struct layout_underline_splitting_params params, prev_params;
        struct layout_effective_run *next, *w;
        struct layout_underline *u;

        init_u_splitting_params_from_erun(cur, &prev_params);
        while ((next = layout_get_next_erun(layout, cur)) != last) {
            init_u_splitting_params_from_erun(next, &params);
            if (!is_same_u_splitting(&prev_params, &params))
                break;
            cur = next;
        }

        u = heap_alloc(sizeof(*u));
        if (!u)
            return E_OUTOFMEMORY;

        w = cur;
        u->u.width = 0.0f;
        while (w != next) {
            u->u.width += w->width;
            w = layout_get_next_erun(layout, w);
        }

        u->u.thickness = thickness;
        /* Font metrics convention is to have it negative when below baseline, for rendering
           however Y grows from baseline down for horizontal baseline. */
        u->u.offset = -offset;
        u->u.runHeight = runheight;
        u->u.readingDirection = is_run_rtl(cur) ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT :
            DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
        u->u.flowDirection = layout->format.flow;
        u->u.localeName = cur->run->u.regular.descr.localeName;
        u->u.measuringMode = layout->measuringmode;
        u->run = cur;
        list_add_tail(&layout->underlines, &u->entry);

        cur = next;
    } while (cur != last);

    return S_OK;
}

/* Adds zero width line, metrics are derived from font at specified text position. */
static HRESULT layout_set_dummy_line_metrics(struct dwrite_textlayout *layout, UINT32 pos)
{
    DWRITE_LINE_METRICS1 metrics = { 0 };
    DWRITE_FONT_METRICS fontmetrics;
    struct layout_range *range;
    IDWriteFontFace *fontface;
    IDWriteFont *font;
    HRESULT hr;

    range = get_layout_range_by_pos(layout, pos);
    hr = create_matching_font(range->collection,
        range->fontfamily,
        range->weight,
        range->style,
        range->stretch,
        &font);
    if (FAILED(hr))
        return hr;
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    IDWriteFont_Release(font);
    if (FAILED(hr))
        return hr;

    layout_get_font_metrics(layout, fontface, range->fontsize, &fontmetrics);
    layout_get_font_height(range->fontsize, &fontmetrics, &metrics.baseline, &metrics.height);
    IDWriteFontFace_Release(fontface);

    return layout_set_line_metrics(layout, &metrics);
}

static void layout_add_line(struct dwrite_textlayout *layout, UINT32 first_cluster, UINT32 last_cluster,
        UINT32 *textpos)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    struct layout_final_splitting_params params, prev_params;
    DWRITE_INLINE_OBJECT_METRICS sign_metrics = { 0 };
    UINT32 line = layout->metrics.lineCount, i;
    DWRITE_LINE_METRICS1 metrics = { 0 };
    UINT32 index, start, pos = *textpos;
    FLOAT descent, trailingspacewidth;
    BOOL append_trimming_run = FALSE;
    const struct layout_run *run;
    FLOAT width, origin_x;
    HRESULT hr;

    /* Take a look at clusters we got for this line in reverse order to set trailing properties for current line */
    for (index = last_cluster, trailingspacewidth = 0.0f; index >= first_cluster; index--) {
        DWRITE_CLUSTER_METRICS *cluster = &layout->clustermetrics[index];
        struct layout_cluster *lc = &layout->clusters[index];
        WCHAR ch;

        /* This also filters out clusters added from inline objects, those are never
           treated as a white space. */
        if (!cluster->isWhitespace)
            break;

        /* Every isNewline cluster is also isWhitespace, but not every
           newline character cluster has isNewline set, so go back to original string. */
        ch = lc->run->u.regular.descr.string[lc->position];
        if (cluster->length == 1 && lb_is_newline_char(ch))
            metrics.newlineLength += cluster->length;

        metrics.trailingWhitespaceLength += cluster->length;
        trailingspacewidth += cluster->width;
    }

    /* Line metrics length includes trailing whitespace length too */
    for (i = first_cluster; i <= last_cluster; i++)
        metrics.length += layout->clustermetrics[i].length;

    /* Ignore trailing whitespaces */
    while (last_cluster > first_cluster) {
        if (!layout->clustermetrics[last_cluster].isWhitespace)
            break;

        last_cluster--;
    }

    /* Does not include trailing space width */
    width = get_cluster_range_width(layout, first_cluster, last_cluster + 1);

    /* Append trimming run if necessary */
    if (width > layout->metrics.layoutWidth && layout->format.trimmingsign != NULL &&
            layout->format.trimming.granularity != DWRITE_TRIMMING_GRANULARITY_NONE) {
        FLOAT trimmed_width = width;

        hr = IDWriteInlineObject_GetMetrics(layout->format.trimmingsign, &sign_metrics);
        if (SUCCEEDED(hr)) {
            while (last_cluster > first_cluster) {
                if (trimmed_width + sign_metrics.width <= layout->metrics.layoutWidth)
                    break;
                trimmed_width -= layout->clustermetrics[last_cluster--].width;
            }
            append_trimming_run = TRUE;
        }
        else
            WARN("Failed to get trimming sign metrics, lines won't be trimmed, hr %#x.\n", hr);

        width = trimmed_width + sign_metrics.width;
    }

    layout_splitting_params_from_pos(layout, pos, &params);
    prev_params = params;
    run = layout->clusters[first_cluster].run;

    /* Form runs from a range of clusters; this is what will be reported with DrawGlyphRun() */
    origin_x = is_rtl ? layout->metrics.layoutWidth : 0.0f;
    for (start = first_cluster, i = first_cluster; i <= last_cluster; i++) {
        layout_splitting_params_from_pos(layout, pos, &params);

        if (run != layout->clusters[i].run || !is_same_splitting_params(&prev_params, &params)) {
            hr = layout_add_effective_run(layout, run, start, i - start, line, origin_x, &prev_params);
            if (FAILED(hr))
                return;

            origin_x += is_rtl ? -get_cluster_range_width(layout, start, i) :
                get_cluster_range_width(layout, start, i);
            run = layout->clusters[i].run;
            start = i;
        }

        prev_params = params;
        pos += layout->clustermetrics[i].length;
    }

    /* Final run from what's left from cluster range */
    hr = layout_add_effective_run(layout, run, start, i - start, line, origin_x, &prev_params);
    if (FAILED(hr))
        return;

    if (append_trimming_run) {
        struct layout_effective_inline *trimming_sign;

        trimming_sign = heap_alloc(sizeof(*trimming_sign));
        if (!trimming_sign)
            return;

        trimming_sign->object = layout->format.trimmingsign;
        trimming_sign->width = sign_metrics.width;
        origin_x += is_rtl ? -get_cluster_range_width(layout, start, i) : get_cluster_range_width(layout, start, i);
        trimming_sign->origin_x = is_rtl ? origin_x - trimming_sign->width : origin_x;
        trimming_sign->origin_y = 0.0f; /* set after line is built */
        trimming_sign->align_dx = 0.0f;
        trimming_sign->baseline = sign_metrics.baseline;

        trimming_sign->is_sideways = FALSE;
        trimming_sign->is_rtl = FALSE;
        trimming_sign->line = line;

        trimming_sign->effect = NULL; /* FIXME */

        list_add_tail(&layout->inlineobjects, &trimming_sign->entry);
    }

    /* Look for max baseline and descent for this line */
    for (index = first_cluster, metrics.baseline = 0.0f, descent = 0.0f; index <= last_cluster; index++) {
        const struct layout_run *cur = layout->clusters[index].run;
        FLOAT cur_descent = cur->height - cur->baseline;

        if (cur->baseline > metrics.baseline)
            metrics.baseline = cur->baseline;
        if (cur_descent > descent)
            descent = cur_descent;
    }

    layout->metrics.width = max(width, layout->metrics.width);
    layout->metrics.widthIncludingTrailingWhitespace = max(width + trailingspacewidth,
        layout->metrics.widthIncludingTrailingWhitespace);

    metrics.height = descent + metrics.baseline;
    metrics.isTrimmed = append_trimming_run || width > layout->metrics.layoutWidth;
    layout_set_line_metrics(layout, &metrics);

    *textpos += metrics.length;
}

static void layout_set_line_positions(struct dwrite_textlayout *layout)
{
    struct layout_effective_inline *inrun;
    struct layout_effective_run *erun;
    FLOAT origin_y;
    UINT32 line;

    /* Now all line info is here, update effective runs positions in flow direction */
    erun = layout_get_next_erun(layout, NULL);
    inrun = layout_get_next_inline_run(layout, NULL);

    for (line = 0, origin_y = 0.0f; line < layout->metrics.lineCount; line++) {
        FLOAT pos_y = origin_y + layout->linemetrics[line].baseline;

        /* For all runs on this line */
        while (erun && erun->line == line) {
            erun->origin_y = pos_y;
            erun = layout_get_next_erun(layout, erun);
        }

        /* Same for inline runs */
        while (inrun && inrun->line == line) {
            inrun->origin_y = pos_y - inrun->baseline;
            inrun = layout_get_next_inline_run(layout, inrun);
        }

        origin_y += layout->linemetrics[line].height;
    }

    layout->metrics.height = origin_y;

    /* Initial paragraph alignment is always near */
    if (layout->format.paralign != DWRITE_PARAGRAPH_ALIGNMENT_NEAR)
        layout_apply_par_alignment(layout);
}

static BOOL layout_can_wrap_after(const struct dwrite_textlayout *layout, UINT32 cluster)
{
    if (layout->format.wrapping == DWRITE_WORD_WRAPPING_CHARACTER)
        return TRUE;

    return layout->clustermetrics[cluster].canWrapLineAfter;
}

static HRESULT layout_compute_effective_runs(struct dwrite_textlayout *layout)
{
    BOOL is_rtl = layout->format.readingdir == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    struct layout_effective_run *erun, *first_underlined;
    UINT32 i, start, textpos, last_breaking_point;
    DWRITE_LINE_METRICS1 metrics;
    FLOAT width;
    UINT32 line;
    HRESULT hr;

    if (!(layout->recompute & RECOMPUTE_LINES))
        return S_OK;

    free_layout_eruns(layout);

    hr = layout_compute(layout);
    if (FAILED(hr))
        return hr;

    layout->metrics.lineCount = 0;
    memset(&metrics, 0, sizeof(metrics));

    layout->metrics.height = 0.0f;
    layout->metrics.width = 0.0f;
    layout->metrics.widthIncludingTrailingWhitespace = 0.0f;

    last_breaking_point = ~0u;

    for (i = 0, start = 0, width = 0.0f, textpos = 0; i < layout->cluster_count; i++) {
        BOOL overflow = FALSE;

        while (i < layout->cluster_count && !layout->clustermetrics[i].isNewline) {
            /* Check for overflow */
            overflow = ((width + layout->clustermetrics[i].width > layout->metrics.layoutWidth) &&
                    (layout->format.wrapping != DWRITE_WORD_WRAPPING_NO_WRAP));
            if (overflow)
                break;

            if (layout_can_wrap_after(layout, i))
                last_breaking_point = i;
            width += layout->clustermetrics[i].width;
            i++;
        }
        i = min(i, layout->cluster_count - 1);

        /* Ignore if overflown on whitespace */
        if (overflow && !(layout->clustermetrics[i].isWhitespace && layout_can_wrap_after(layout, i))) {
            /* Use most recently found breaking point */
            if (last_breaking_point != ~0u) {
                i = last_breaking_point;
                last_breaking_point = ~0u;
            }
            else {
                /* Otherwise proceed forward to next newline or breaking point */
                for (; i < layout->cluster_count; i++)
                    if (layout_can_wrap_after(layout, i) || layout->clustermetrics[i].isNewline)
                        break;
            }
        }
        i = min(i, layout->cluster_count - 1);

        layout_add_line(layout, start, i, &textpos);
        start = i + 1;
        width = 0.0f;
    }

    /* Add dummy line if:
       - there's no text, metrics come from first range in this case;
       - last ended with a mandatory break, metrics come from last text position.
    */
    if (layout->len == 0)
        hr = layout_set_dummy_line_metrics(layout, 0);
    else if (layout->clustermetrics[layout->cluster_count - 1].isNewline)
        hr = layout_set_dummy_line_metrics(layout, layout->len - 1);
    if (FAILED(hr))
        return hr;

    layout->metrics.left = is_rtl ? layout->metrics.layoutWidth - layout->metrics.width : 0.0f;
    layout->metrics.top = 0.0f;
    layout->metrics.maxBidiReorderingDepth = 1; /* FIXME */

    /* Add explicit underlined runs */
    erun = layout_get_next_erun(layout, NULL);
    first_underlined = erun && erun->underlined ? erun : NULL;
    for (line = 0; line < layout->metrics.lineCount; line++) {
        while (erun && erun->line == line) {
            erun = layout_get_next_erun(layout, erun);

            if (first_underlined && (!erun || !erun->underlined)) {
                layout_add_underline(layout, first_underlined, erun);
                first_underlined = NULL;
            }
            else if (!first_underlined && erun && erun->underlined)
                first_underlined = erun;
        }
    }

    /* Position runs in flow direction */
    layout_set_line_positions(layout);

    /* Initial alignment is always leading */
    if (layout->format.textalignment != DWRITE_TEXT_ALIGNMENT_LEADING)
        layout_apply_text_alignment(layout);

    layout->recompute &= ~RECOMPUTE_LINES;
    return hr;
}

static BOOL is_same_layout_attrvalue(struct layout_range_header const *h, enum layout_range_attr_kind attr,
        struct layout_range_attr_value *value)
{
    struct layout_range_spacing const *range_spacing = (struct layout_range_spacing*)h;
    struct layout_range_iface const *range_iface = (struct layout_range_iface*)h;
    struct layout_range_bool const *range_bool = (struct layout_range_bool*)h;
    struct layout_range const *range = (struct layout_range*)h;

    switch (attr) {
    case LAYOUT_RANGE_ATTR_WEIGHT:
        return range->weight == value->u.weight;
    case LAYOUT_RANGE_ATTR_STYLE:
        return range->style == value->u.style;
    case LAYOUT_RANGE_ATTR_STRETCH:
        return range->stretch == value->u.stretch;
    case LAYOUT_RANGE_ATTR_FONTSIZE:
        return range->fontsize == value->u.fontsize;
    case LAYOUT_RANGE_ATTR_INLINE:
        return range->object == value->u.object;
    case LAYOUT_RANGE_ATTR_EFFECT:
        return range_iface->iface == value->u.effect;
    case LAYOUT_RANGE_ATTR_UNDERLINE:
        return range_bool->value == value->u.underline;
    case LAYOUT_RANGE_ATTR_STRIKETHROUGH:
        return range_bool->value == value->u.strikethrough;
    case LAYOUT_RANGE_ATTR_PAIR_KERNING:
        return range->pair_kerning == value->u.pair_kerning;
    case LAYOUT_RANGE_ATTR_FONTCOLL:
        return range->collection == value->u.collection;
    case LAYOUT_RANGE_ATTR_LOCALE:
        return strcmpiW(range->locale, value->u.locale) == 0;
    case LAYOUT_RANGE_ATTR_FONTFAMILY:
        return strcmpW(range->fontfamily, value->u.fontfamily) == 0;
    case LAYOUT_RANGE_ATTR_SPACING:
        return range_spacing->leading == value->u.spacing[0] &&
               range_spacing->trailing == value->u.spacing[1] &&
               range_spacing->min_advance == value->u.spacing[2];
    case LAYOUT_RANGE_ATTR_TYPOGRAPHY:
        return range_iface->iface == (IUnknown*)value->u.typography;
    default:
        ;
    }

    return FALSE;
}

static inline BOOL is_same_layout_attributes(struct layout_range_header const *hleft, struct layout_range_header const *hright)
{
    switch (hleft->kind)
    {
    case LAYOUT_RANGE_REGULAR:
    {
        struct layout_range const *left = (struct layout_range const*)hleft;
        struct layout_range const *right = (struct layout_range const*)hright;
        return left->weight == right->weight &&
               left->style  == right->style &&
               left->stretch == right->stretch &&
               left->fontsize == right->fontsize &&
               left->object == right->object &&
               left->pair_kerning == right->pair_kerning &&
               left->collection == right->collection &&
              !strcmpiW(left->locale, right->locale) &&
              !strcmpW(left->fontfamily, right->fontfamily);
    }
    case LAYOUT_RANGE_UNDERLINE:
    case LAYOUT_RANGE_STRIKETHROUGH:
    {
        struct layout_range_bool const *left = (struct layout_range_bool const*)hleft;
        struct layout_range_bool const *right = (struct layout_range_bool const*)hright;
        return left->value == right->value;
    }
    case LAYOUT_RANGE_EFFECT:
    case LAYOUT_RANGE_TYPOGRAPHY:
    {
        struct layout_range_iface const *left = (struct layout_range_iface const*)hleft;
        struct layout_range_iface const *right = (struct layout_range_iface const*)hright;
        return left->iface == right->iface;
    }
    case LAYOUT_RANGE_SPACING:
    {
        struct layout_range_spacing const *left = (struct layout_range_spacing const*)hleft;
        struct layout_range_spacing const *right = (struct layout_range_spacing const*)hright;
        return left->leading == right->leading &&
               left->trailing == right->trailing &&
               left->min_advance == right->min_advance;
    }
    default:
        FIXME("unknown range kind %d\n", hleft->kind);
        return FALSE;
    }
}

static inline BOOL is_same_text_range(const DWRITE_TEXT_RANGE *left, const DWRITE_TEXT_RANGE *right)
{
    return left->startPosition == right->startPosition && left->length == right->length;
}

/* Allocates range and inits it with default values from text format. */
static struct layout_range_header *alloc_layout_range(struct dwrite_textlayout *layout, const DWRITE_TEXT_RANGE *r,
    enum layout_range_kind kind)
{
    struct layout_range_header *h;

    switch (kind)
    {
    case LAYOUT_RANGE_REGULAR:
    {
        struct layout_range *range;

        range = heap_alloc(sizeof(*range));
        if (!range) return NULL;

        range->weight = layout->format.weight;
        range->style  = layout->format.style;
        range->stretch = layout->format.stretch;
        range->fontsize = layout->format.fontsize;
        range->object = NULL;
        range->pair_kerning = FALSE;

        range->fontfamily = heap_strdupW(layout->format.family_name);
        if (!range->fontfamily) {
            heap_free(range);
            return NULL;
        }

        range->collection = layout->format.collection;
        if (range->collection)
            IDWriteFontCollection_AddRef(range->collection);
        strcpyW(range->locale, layout->format.locale);

        h = &range->h;
        break;
    }
    case LAYOUT_RANGE_UNDERLINE:
    case LAYOUT_RANGE_STRIKETHROUGH:
    {
        struct layout_range_bool *range;

        range = heap_alloc(sizeof(*range));
        if (!range) return NULL;

        range->value = FALSE;
        h = &range->h;
        break;
    }
    case LAYOUT_RANGE_EFFECT:
    case LAYOUT_RANGE_TYPOGRAPHY:
    {
        struct layout_range_iface *range;

        range = heap_alloc(sizeof(*range));
        if (!range) return NULL;

        range->iface = NULL;
        h = &range->h;
        break;
    }
    case LAYOUT_RANGE_SPACING:
    {
        struct layout_range_spacing *range;

        range = heap_alloc(sizeof(*range));
        if (!range) return NULL;

        range->leading = 0.0f;
        range->trailing = 0.0f;
        range->min_advance = 0.0f;
        h = &range->h;
        break;
    }
    default:
        FIXME("unknown range kind %d\n", kind);
        return NULL;
    }

    h->kind = kind;
    h->range = *r;
    return h;
}

static struct layout_range_header *alloc_layout_range_from(struct layout_range_header *h, const DWRITE_TEXT_RANGE *r)
{
    struct layout_range_header *ret;

    switch (h->kind)
    {
    case LAYOUT_RANGE_REGULAR:
    {
        struct layout_range *from = (struct layout_range*)h;

        struct layout_range *range = heap_alloc(sizeof(*range));
        if (!range) return NULL;

        *range = *from;
        range->fontfamily = heap_strdupW(from->fontfamily);
        if (!range->fontfamily) {
            heap_free(range);
            return NULL;
        }

        /* update refcounts */
        if (range->object)
            IDWriteInlineObject_AddRef(range->object);
        if (range->collection)
            IDWriteFontCollection_AddRef(range->collection);
        ret = &range->h;
        break;
    }
    case LAYOUT_RANGE_UNDERLINE:
    case LAYOUT_RANGE_STRIKETHROUGH:
    {
        struct layout_range_bool *strike = heap_alloc(sizeof(*strike));
        if (!strike) return NULL;

        *strike = *(struct layout_range_bool*)h;
        ret = &strike->h;
        break;
    }
    case LAYOUT_RANGE_EFFECT:
    case LAYOUT_RANGE_TYPOGRAPHY:
    {
        struct layout_range_iface *effect = heap_alloc(sizeof(*effect));
        if (!effect) return NULL;

        *effect = *(struct layout_range_iface*)h;
        if (effect->iface)
            IUnknown_AddRef(effect->iface);
        ret = &effect->h;
        break;
    }
    case LAYOUT_RANGE_SPACING:
    {
        struct layout_range_spacing *spacing = heap_alloc(sizeof(*spacing));
        if (!spacing) return NULL;

        *spacing = *(struct layout_range_spacing*)h;
        ret = &spacing->h;
        break;
    }
    default:
        FIXME("unknown range kind %d\n", h->kind);
        return NULL;
    }

    ret->range = *r;
    return ret;
}

static void free_layout_range(struct layout_range_header *h)
{
    if (!h)
        return;

    switch (h->kind)
    {
    case LAYOUT_RANGE_REGULAR:
    {
        struct layout_range *range = (struct layout_range*)h;

        if (range->object)
            IDWriteInlineObject_Release(range->object);
        if (range->collection)
            IDWriteFontCollection_Release(range->collection);
        heap_free(range->fontfamily);
        break;
    }
    case LAYOUT_RANGE_EFFECT:
    case LAYOUT_RANGE_TYPOGRAPHY:
    {
        struct layout_range_iface *range = (struct layout_range_iface*)h;
        if (range->iface)
            IUnknown_Release(range->iface);
        break;
    }
    default:
        ;
    }

    heap_free(h);
}

static void free_layout_ranges_list(struct dwrite_textlayout *layout)
{
    struct layout_range_header *cur, *cur2;

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->ranges, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->underline_ranges, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->strike_ranges, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->effects, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->spacing, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }

    LIST_FOR_EACH_ENTRY_SAFE(cur, cur2, &layout->typographies, struct layout_range_header, entry) {
        list_remove(&cur->entry);
        free_layout_range(cur);
    }
}

static struct layout_range_header *find_outer_range(struct list *ranges, const DWRITE_TEXT_RANGE *range)
{
    struct layout_range_header *cur;

    LIST_FOR_EACH_ENTRY(cur, ranges, struct layout_range_header, entry) {

        if (cur->range.startPosition > range->startPosition)
            return NULL;

        if ((cur->range.startPosition + cur->range.length < range->startPosition + range->length) &&
            (range->startPosition < cur->range.startPosition + cur->range.length))
            return NULL;
        if (cur->range.startPosition + cur->range.length >= range->startPosition + range->length)
            return cur;
    }

    return NULL;
}

static struct layout_range *get_layout_range_by_pos(struct dwrite_textlayout *layout, UINT32 pos)
{
    struct layout_range *cur;

    LIST_FOR_EACH_ENTRY(cur, &layout->ranges, struct layout_range, h.entry) {
        DWRITE_TEXT_RANGE *r = &cur->h.range;
        if (r->startPosition <= pos && pos < r->startPosition + r->length)
            return cur;
    }

    return NULL;
}

static inline BOOL set_layout_range_iface_attr(IUnknown **dest, IUnknown *value)
{
    if (*dest == value) return FALSE;

    if (*dest)
        IUnknown_Release(*dest);
    *dest = value;
    if (*dest)
        IUnknown_AddRef(*dest);

    return TRUE;
}

static BOOL set_layout_range_attrval(struct layout_range_header *h, enum layout_range_attr_kind attr, struct layout_range_attr_value *value)
{
    struct layout_range_spacing *dest_spacing = (struct layout_range_spacing*)h;
    struct layout_range_iface *dest_iface = (struct layout_range_iface*)h;
    struct layout_range_bool *dest_bool = (struct layout_range_bool*)h;
    struct layout_range *dest = (struct layout_range*)h;

    BOOL changed = FALSE;

    switch (attr) {
    case LAYOUT_RANGE_ATTR_WEIGHT:
        changed = dest->weight != value->u.weight;
        dest->weight = value->u.weight;
        break;
    case LAYOUT_RANGE_ATTR_STYLE:
        changed = dest->style != value->u.style;
        dest->style = value->u.style;
        break;
    case LAYOUT_RANGE_ATTR_STRETCH:
        changed = dest->stretch != value->u.stretch;
        dest->stretch = value->u.stretch;
        break;
    case LAYOUT_RANGE_ATTR_FONTSIZE:
        changed = dest->fontsize != value->u.fontsize;
        dest->fontsize = value->u.fontsize;
        break;
    case LAYOUT_RANGE_ATTR_INLINE:
        changed = set_layout_range_iface_attr((IUnknown**)&dest->object, (IUnknown*)value->u.object);
        break;
    case LAYOUT_RANGE_ATTR_EFFECT:
        changed = set_layout_range_iface_attr((IUnknown**)&dest_iface->iface, (IUnknown*)value->u.effect);
        break;
    case LAYOUT_RANGE_ATTR_UNDERLINE:
        changed = dest_bool->value != value->u.underline;
        dest_bool->value = value->u.underline;
        break;
    case LAYOUT_RANGE_ATTR_STRIKETHROUGH:
        changed = dest_bool->value != value->u.strikethrough;
        dest_bool->value = value->u.strikethrough;
        break;
    case LAYOUT_RANGE_ATTR_PAIR_KERNING:
        changed = dest->pair_kerning != value->u.pair_kerning;
        dest->pair_kerning = value->u.pair_kerning;
        break;
    case LAYOUT_RANGE_ATTR_FONTCOLL:
        changed = set_layout_range_iface_attr((IUnknown**)&dest->collection, (IUnknown*)value->u.collection);
        break;
    case LAYOUT_RANGE_ATTR_LOCALE:
        changed = strcmpiW(dest->locale, value->u.locale) != 0;
        if (changed) {
            strcpyW(dest->locale, value->u.locale);
            strlwrW(dest->locale);
        }
        break;
    case LAYOUT_RANGE_ATTR_FONTFAMILY:
        changed = strcmpW(dest->fontfamily, value->u.fontfamily) != 0;
        if (changed) {
            heap_free(dest->fontfamily);
            dest->fontfamily = heap_strdupW(value->u.fontfamily);
        }
        break;
    case LAYOUT_RANGE_ATTR_SPACING:
        changed = dest_spacing->leading != value->u.spacing[0] ||
            dest_spacing->trailing != value->u.spacing[1] ||
            dest_spacing->min_advance != value->u.spacing[2];
        dest_spacing->leading = value->u.spacing[0];
        dest_spacing->trailing = value->u.spacing[1];
        dest_spacing->min_advance = value->u.spacing[2];
        break;
    case LAYOUT_RANGE_ATTR_TYPOGRAPHY:
        changed = set_layout_range_iface_attr((IUnknown**)&dest_iface->iface, (IUnknown*)value->u.typography);
        break;
    default:
        ;
    }

    return changed;
}

static inline BOOL is_in_layout_range(const DWRITE_TEXT_RANGE *outer, const DWRITE_TEXT_RANGE *inner)
{
    return (inner->startPosition >= outer->startPosition) &&
           (inner->startPosition + inner->length <= outer->startPosition + outer->length);
}

static inline HRESULT return_range(const struct layout_range_header *h, DWRITE_TEXT_RANGE *r)
{
    if (r) *r = h->range;
    return S_OK;
}

/* Sets attribute value for given range, does all needed splitting/merging of existing ranges. */
static HRESULT set_layout_range_attr(struct dwrite_textlayout *layout, enum layout_range_attr_kind attr, struct layout_range_attr_value *value)
{
    struct layout_range_header *cur, *right, *left, *outer;
    BOOL changed = FALSE;
    struct list *ranges;
    DWRITE_TEXT_RANGE r;

    /* ignore zero length ranges */
    if (value->range.length == 0)
        return S_OK;

    /* select from ranges lists */
    switch (attr)
    {
    case LAYOUT_RANGE_ATTR_WEIGHT:
    case LAYOUT_RANGE_ATTR_STYLE:
    case LAYOUT_RANGE_ATTR_STRETCH:
    case LAYOUT_RANGE_ATTR_FONTSIZE:
    case LAYOUT_RANGE_ATTR_INLINE:
    case LAYOUT_RANGE_ATTR_PAIR_KERNING:
    case LAYOUT_RANGE_ATTR_FONTCOLL:
    case LAYOUT_RANGE_ATTR_LOCALE:
    case LAYOUT_RANGE_ATTR_FONTFAMILY:
        ranges = &layout->ranges;
        break;
    case LAYOUT_RANGE_ATTR_UNDERLINE:
        ranges = &layout->underline_ranges;
        break;
    case LAYOUT_RANGE_ATTR_STRIKETHROUGH:
        ranges = &layout->strike_ranges;
        break;
    case LAYOUT_RANGE_ATTR_EFFECT:
        ranges = &layout->effects;
        break;
    case LAYOUT_RANGE_ATTR_SPACING:
        ranges = &layout->spacing;
        break;
    case LAYOUT_RANGE_ATTR_TYPOGRAPHY:
        ranges = &layout->typographies;
        break;
    default:
        FIXME("unknown attr kind %d\n", attr);
        return E_FAIL;
    }

    /* If new range is completely within existing range, split existing range in two */
    if ((outer = find_outer_range(ranges, &value->range))) {

        /* no need to add same range */
        if (is_same_layout_attrvalue(outer, attr, value))
            return S_OK;

        /* for matching range bounds just replace data */
        if (is_same_text_range(&outer->range, &value->range)) {
            changed = set_layout_range_attrval(outer, attr, value);
            goto done;
        }

        /* add new range to the left */
        if (value->range.startPosition == outer->range.startPosition) {
            left = alloc_layout_range_from(outer, &value->range);
            if (!left) return E_OUTOFMEMORY;

            changed = set_layout_range_attrval(left, attr, value);
            list_add_before(&outer->entry, &left->entry);
            outer->range.startPosition += value->range.length;
            outer->range.length -= value->range.length;
            goto done;
        }

        /* add new range to the right */
        if (value->range.startPosition + value->range.length == outer->range.startPosition + outer->range.length) {
            right = alloc_layout_range_from(outer, &value->range);
            if (!right) return E_OUTOFMEMORY;

            changed = set_layout_range_attrval(right, attr, value);
            list_add_after(&outer->entry, &right->entry);
            outer->range.length -= value->range.length;
            goto done;
        }

        r.startPosition = value->range.startPosition + value->range.length;
        r.length = outer->range.length + outer->range.startPosition - r.startPosition;

        /* right part */
        right = alloc_layout_range_from(outer, &r);
        /* new range in the middle */
        cur = alloc_layout_range_from(outer, &value->range);
        if (!right || !cur) {
            free_layout_range(right);
            free_layout_range(cur);
            return E_OUTOFMEMORY;
        }

        /* reuse container range as a left part */
        outer->range.length = value->range.startPosition - outer->range.startPosition;

        /* new part */
        set_layout_range_attrval(cur, attr, value);

        list_add_after(&outer->entry, &cur->entry);
        list_add_after(&cur->entry, &right->entry);

        layout->recompute = RECOMPUTE_EVERYTHING;
        return S_OK;
    }

    /* Now it's only possible that given range contains some existing ranges, fully or partially.
       Update all of them. */
    left = get_layout_range_header_by_pos(ranges, value->range.startPosition);
    if (left->range.startPosition == value->range.startPosition)
        changed = set_layout_range_attrval(left, attr, value);
    else /* need to split */ {
        r.startPosition = value->range.startPosition;
        r.length = left->range.length - value->range.startPosition + left->range.startPosition;
        left->range.length -= r.length;
        cur = alloc_layout_range_from(left, &r);
        changed = set_layout_range_attrval(cur, attr, value);
        list_add_after(&left->entry, &cur->entry);
    }
    cur = LIST_ENTRY(list_next(ranges, &left->entry), struct layout_range_header, entry);

    /* for all existing ranges covered by new one update value */
    while (cur && is_in_layout_range(&value->range, &cur->range)) {
        changed |= set_layout_range_attrval(cur, attr, value);
        cur = LIST_ENTRY(list_next(ranges, &cur->entry), struct layout_range_header, entry);
    }

    /* it's possible rightmost range intersects */
    if (cur && (cur->range.startPosition < value->range.startPosition + value->range.length)) {
        r.startPosition = cur->range.startPosition;
        r.length = value->range.startPosition + value->range.length - cur->range.startPosition;
        left = alloc_layout_range_from(cur, &r);
        changed |= set_layout_range_attrval(left, attr, value);
        cur->range.startPosition += left->range.length;
        cur->range.length -= left->range.length;
        list_add_before(&cur->entry, &left->entry);
    }

done:
    if (changed) {
        struct list *next, *i;

        layout->recompute = RECOMPUTE_EVERYTHING;
        i = list_head(ranges);
        while ((next = list_next(ranges, i))) {
            struct layout_range_header *next_range = LIST_ENTRY(next, struct layout_range_header, entry);

            cur = LIST_ENTRY(i, struct layout_range_header, entry);
            if (is_same_layout_attributes(cur, next_range)) {
                /* remove similar range */
                cur->range.length += next_range->range.length;
                list_remove(next);
                free_layout_range(next_range);
            }
            else
                i = list_next(ranges, i);
        }
    }

    return S_OK;
}

static inline const WCHAR *get_string_attribute_ptr(struct layout_range *range, enum layout_range_attr_kind kind)
{
    const WCHAR *str;

    switch (kind) {
        case LAYOUT_RANGE_ATTR_LOCALE:
            str = range->locale;
            break;
        case LAYOUT_RANGE_ATTR_FONTFAMILY:
            str = range->fontfamily;
            break;
        default:
            str = NULL;
    }

    return str;
}

static HRESULT get_string_attribute_length(struct dwrite_textlayout *layout, enum layout_range_attr_kind kind, UINT32 position,
    UINT32 *length, DWRITE_TEXT_RANGE *r)
{
    struct layout_range *range;
    const WCHAR *str;

    range = get_layout_range_by_pos(layout, position);
    if (!range) {
        *length = 0;
        return S_OK;
    }

    str = get_string_attribute_ptr(range, kind);
    *length = strlenW(str);
    return return_range(&range->h, r);
}

static HRESULT get_string_attribute_value(struct dwrite_textlayout *layout, enum layout_range_attr_kind kind, UINT32 position,
    WCHAR *ret, UINT32 length, DWRITE_TEXT_RANGE *r)
{
    struct layout_range *range;
    const WCHAR *str;

    if (length == 0)
        return E_INVALIDARG;

    ret[0] = 0;
    range = get_layout_range_by_pos(layout, position);
    if (!range)
        return E_INVALIDARG;

    str = get_string_attribute_ptr(range, kind);
    if (length < strlenW(str) + 1)
        return E_NOT_SUFFICIENT_BUFFER;

    strcpyW(ret, str);
    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_QueryInterface(IDWriteTextLayout3 *iface, REFIID riid, void **obj)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    *obj = NULL;

    if (IsEqualIID(riid, &IID_IDWriteTextLayout3) ||
        IsEqualIID(riid, &IID_IDWriteTextLayout2) ||
        IsEqualIID(riid, &IID_IDWriteTextLayout1) ||
        IsEqualIID(riid, &IID_IDWriteTextLayout) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
    }
    else if (IsEqualIID(riid, &IID_IDWriteTextFormat1) ||
             IsEqualIID(riid, &IID_IDWriteTextFormat))
        *obj = &This->IDWriteTextFormat1_iface;

    if (*obj) {
        IDWriteTextLayout3_AddRef(iface);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetextlayout_AddRef(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritetextlayout_Release(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        IDWriteFactory4_Release(This->factory);
        free_layout_ranges_list(This);
        free_layout_eruns(This);
        free_layout_runs(This);
        release_format_data(&This->format);
        heap_free(This->nominal_breakpoints);
        heap_free(This->actual_breakpoints);
        heap_free(This->clustermetrics);
        heap_free(This->clusters);
        heap_free(This->linemetrics);
        heap_free(This->lines);
        heap_free(This->str);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritetextlayout_SetTextAlignment(IDWriteTextLayout3 *iface, DWRITE_TEXT_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_SetTextAlignment(&This->IDWriteTextFormat1_iface, alignment);
}

static HRESULT WINAPI dwritetextlayout_SetParagraphAlignment(IDWriteTextLayout3 *iface, DWRITE_PARAGRAPH_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_SetParagraphAlignment(&This->IDWriteTextFormat1_iface, alignment);
}

static HRESULT WINAPI dwritetextlayout_SetWordWrapping(IDWriteTextLayout3 *iface, DWRITE_WORD_WRAPPING wrapping)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_SetWordWrapping(&This->IDWriteTextFormat1_iface, wrapping);
}

static HRESULT WINAPI dwritetextlayout_SetReadingDirection(IDWriteTextLayout3 *iface, DWRITE_READING_DIRECTION direction)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_SetReadingDirection(&This->IDWriteTextFormat1_iface, direction);
}

static HRESULT WINAPI dwritetextlayout_SetFlowDirection(IDWriteTextLayout3 *iface, DWRITE_FLOW_DIRECTION direction)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%d)\n", This, direction);
    return IDWriteTextFormat1_SetFlowDirection(&This->IDWriteTextFormat1_iface, direction);
}

static HRESULT WINAPI dwritetextlayout_SetIncrementalTabStop(IDWriteTextLayout3 *iface, FLOAT tabstop)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%.2f)\n", This, tabstop);
    return IDWriteTextFormat1_SetIncrementalTabStop(&This->IDWriteTextFormat1_iface, tabstop);
}

static HRESULT WINAPI dwritetextlayout_SetTrimming(IDWriteTextLayout3 *iface, DWRITE_TRIMMING const *trimming,
    IDWriteInlineObject *trimming_sign)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%p %p)\n", This, trimming, trimming_sign);
    return IDWriteTextFormat1_SetTrimming(&This->IDWriteTextFormat1_iface, trimming, trimming_sign);
}

static HRESULT WINAPI dwritetextlayout_SetLineSpacing(IDWriteTextLayout3 *iface, DWRITE_LINE_SPACING_METHOD spacing,
    FLOAT line_spacing, FLOAT baseline)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%d %.2f %.2f)\n", This, spacing, line_spacing, baseline);
    return IDWriteTextFormat1_SetLineSpacing(&This->IDWriteTextFormat1_iface, spacing, line_spacing, baseline);
}

static DWRITE_TEXT_ALIGNMENT WINAPI dwritetextlayout_GetTextAlignment(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetTextAlignment(&This->IDWriteTextFormat1_iface);
}

static DWRITE_PARAGRAPH_ALIGNMENT WINAPI dwritetextlayout_GetParagraphAlignment(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetParagraphAlignment(&This->IDWriteTextFormat1_iface);
}

static DWRITE_WORD_WRAPPING WINAPI dwritetextlayout_GetWordWrapping(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetWordWrapping(&This->IDWriteTextFormat1_iface);
}

static DWRITE_READING_DIRECTION WINAPI dwritetextlayout_GetReadingDirection(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetReadingDirection(&This->IDWriteTextFormat1_iface);
}

static DWRITE_FLOW_DIRECTION WINAPI dwritetextlayout_GetFlowDirection(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFlowDirection(&This->IDWriteTextFormat1_iface);
}

static FLOAT WINAPI dwritetextlayout_GetIncrementalTabStop(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetIncrementalTabStop(&This->IDWriteTextFormat1_iface);
}

static HRESULT WINAPI dwritetextlayout_GetTrimming(IDWriteTextLayout3 *iface, DWRITE_TRIMMING *options,
    IDWriteInlineObject **trimming_sign)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetTrimming(&This->IDWriteTextFormat1_iface, options, trimming_sign);
}

static HRESULT WINAPI dwritetextlayout_GetLineSpacing(IDWriteTextLayout3 *iface, DWRITE_LINE_SPACING_METHOD *method,
    FLOAT *spacing, FLOAT *baseline)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat_GetLineSpacing((IDWriteTextFormat*)&This->IDWriteTextFormat1_iface, method, spacing, baseline);
}

static HRESULT WINAPI dwritetextlayout_GetFontCollection(IDWriteTextLayout3 *iface, IDWriteFontCollection **collection)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontCollection(&This->IDWriteTextFormat1_iface, collection);
}

static UINT32 WINAPI dwritetextlayout_GetFontFamilyNameLength(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontFamilyNameLength(&This->IDWriteTextFormat1_iface);
}

static HRESULT WINAPI dwritetextlayout_GetFontFamilyName(IDWriteTextLayout3 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontFamilyName(&This->IDWriteTextFormat1_iface, name, size);
}

static DWRITE_FONT_WEIGHT WINAPI dwritetextlayout_GetFontWeight(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontWeight(&This->IDWriteTextFormat1_iface);
}

static DWRITE_FONT_STYLE WINAPI dwritetextlayout_GetFontStyle(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontStyle(&This->IDWriteTextFormat1_iface);
}

static DWRITE_FONT_STRETCH WINAPI dwritetextlayout_GetFontStretch(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontStretch(&This->IDWriteTextFormat1_iface);
}

static FLOAT WINAPI dwritetextlayout_GetFontSize(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetFontSize(&This->IDWriteTextFormat1_iface);
}

static UINT32 WINAPI dwritetextlayout_GetLocaleNameLength(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetLocaleNameLength(&This->IDWriteTextFormat1_iface);
}

static HRESULT WINAPI dwritetextlayout_GetLocaleName(IDWriteTextLayout3 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    return IDWriteTextFormat1_GetLocaleName(&This->IDWriteTextFormat1_iface, name, size);
}

static HRESULT WINAPI dwritetextlayout_SetMaxWidth(IDWriteTextLayout3 *iface, FLOAT maxWidth)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    BOOL changed;

    TRACE("(%p)->(%.2f)\n", This, maxWidth);

    if (maxWidth < 0.0f)
        return E_INVALIDARG;

    changed = This->metrics.layoutWidth != maxWidth;
    This->metrics.layoutWidth = maxWidth;

    if (changed)
        This->recompute |= RECOMPUTE_LINES_AND_OVERHANGS;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_SetMaxHeight(IDWriteTextLayout3 *iface, FLOAT maxHeight)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    BOOL changed;

    TRACE("(%p)->(%.2f)\n", This, maxHeight);

    if (maxHeight < 0.0f)
        return E_INVALIDARG;

    changed = This->metrics.layoutHeight != maxHeight;
    This->metrics.layoutHeight = maxHeight;

    if (changed)
        This->recompute |= RECOMPUTE_LINES_AND_OVERHANGS;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_SetFontCollection(IDWriteTextLayout3 *iface, IDWriteFontCollection* collection, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%p %s)\n", This, collection, debugstr_range(&range));

    value.range = range;
    value.u.collection = collection;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_FONTCOLL, &value);
}

static HRESULT WINAPI dwritetextlayout_SetFontFamilyName(IDWriteTextLayout3 *iface, WCHAR const *name, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%s %s)\n", This, debugstr_w(name), debugstr_range(&range));

    if (!name)
        return E_INVALIDARG;

    value.range = range;
    value.u.fontfamily = name;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_FONTFAMILY, &value);
}

static HRESULT WINAPI dwritetextlayout_SetFontWeight(IDWriteTextLayout3 *iface, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, weight, debugstr_range(&range));

    if ((UINT32)weight > DWRITE_FONT_WEIGHT_ULTRA_BLACK)
        return E_INVALIDARG;

    value.range = range;
    value.u.weight = weight;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_WEIGHT, &value);
}

static HRESULT WINAPI dwritetextlayout_SetFontStyle(IDWriteTextLayout3 *iface, DWRITE_FONT_STYLE style, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, style, debugstr_range(&range));

    if ((UINT32)style > DWRITE_FONT_STYLE_ITALIC)
        return E_INVALIDARG;

    value.range = range;
    value.u.style = style;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_STYLE, &value);
}

static HRESULT WINAPI dwritetextlayout_SetFontStretch(IDWriteTextLayout3 *iface, DWRITE_FONT_STRETCH stretch, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, stretch, debugstr_range(&range));

    if (stretch == DWRITE_FONT_STRETCH_UNDEFINED || (UINT32)stretch > DWRITE_FONT_STRETCH_ULTRA_EXPANDED)
        return E_INVALIDARG;

    value.range = range;
    value.u.stretch = stretch;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_STRETCH, &value);
}

static HRESULT WINAPI dwritetextlayout_SetFontSize(IDWriteTextLayout3 *iface, FLOAT size, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%.2f %s)\n", This, size, debugstr_range(&range));

    if (size <= 0.0f)
        return E_INVALIDARG;

    value.range = range;
    value.u.fontsize = size;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_FONTSIZE, &value);
}

static HRESULT WINAPI dwritetextlayout_SetUnderline(IDWriteTextLayout3 *iface, BOOL underline, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, underline, debugstr_range(&range));

    value.range = range;
    value.u.underline = underline;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_UNDERLINE, &value);
}

static HRESULT WINAPI dwritetextlayout_SetStrikethrough(IDWriteTextLayout3 *iface, BOOL strikethrough, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, strikethrough, debugstr_range(&range));

    value.range = range;
    value.u.strikethrough = strikethrough;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_STRIKETHROUGH, &value);
}

static HRESULT WINAPI dwritetextlayout_SetDrawingEffect(IDWriteTextLayout3 *iface, IUnknown* effect, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%p %s)\n", This, effect, debugstr_range(&range));

    value.range = range;
    value.u.effect = effect;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_EFFECT, &value);
}

static HRESULT WINAPI dwritetextlayout_SetInlineObject(IDWriteTextLayout3 *iface, IDWriteInlineObject *object, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%p %s)\n", This, object, debugstr_range(&range));

    value.range = range;
    value.u.object = object;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_INLINE, &value);
}

static HRESULT WINAPI dwritetextlayout_SetTypography(IDWriteTextLayout3 *iface, IDWriteTypography* typography, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%p %s)\n", This, typography, debugstr_range(&range));

    value.range = range;
    value.u.typography = typography;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_TYPOGRAPHY, &value);
}

static HRESULT WINAPI dwritetextlayout_SetLocaleName(IDWriteTextLayout3 *iface, WCHAR const* locale, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%s %s)\n", This, debugstr_w(locale), debugstr_range(&range));

    if (!locale || strlenW(locale) > LOCALE_NAME_MAX_LENGTH-1)
        return E_INVALIDARG;

    value.range = range;
    value.u.locale = locale;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_LOCALE, &value);
}

static FLOAT WINAPI dwritetextlayout_GetMaxWidth(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)\n", This);
    return This->metrics.layoutWidth;
}

static FLOAT WINAPI dwritetextlayout_GetMaxHeight(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)\n", This);
    return This->metrics.layoutHeight;
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontCollection(IDWriteTextLayout3 *iface, UINT32 position,
    IDWriteFontCollection** collection, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, collection, r);

    if (position >= This->len)
        return S_OK;

    range = get_layout_range_by_pos(This, position);
    *collection = range->collection;
    if (*collection)
        IDWriteFontCollection_AddRef(*collection);

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontFamilyNameLength(IDWriteTextLayout3 *iface,
    UINT32 position, UINT32 *length, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%d %p %p)\n", This, position, length, r);
    return get_string_attribute_length(This, LAYOUT_RANGE_ATTR_FONTFAMILY, position, length, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontFamilyName(IDWriteTextLayout3 *iface,
    UINT32 position, WCHAR *name, UINT32 length, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%u %p %u %p)\n", This, position, name, length, r);
    return get_string_attribute_value(This, LAYOUT_RANGE_ATTR_FONTFAMILY, position, name, length, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontWeight(IDWriteTextLayout3 *iface,
    UINT32 position, DWRITE_FONT_WEIGHT *weight, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, weight, r);

    if (position >= This->len)
        return S_OK;

    range = get_layout_range_by_pos(This, position);
    *weight = range->weight;

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontStyle(IDWriteTextLayout3 *iface,
    UINT32 position, DWRITE_FONT_STYLE *style, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, style, r);

    range = get_layout_range_by_pos(This, position);
    *style = range->style;
    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontStretch(IDWriteTextLayout3 *iface,
    UINT32 position, DWRITE_FONT_STRETCH *stretch, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, stretch, r);

    range = get_layout_range_by_pos(This, position);
    *stretch = range->stretch;
    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetFontSize(IDWriteTextLayout3 *iface,
    UINT32 position, FLOAT *size, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, size, r);

    range = get_layout_range_by_pos(This, position);
    *size = range->fontsize;
    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_GetUnderline(IDWriteTextLayout3 *iface,
    UINT32 position, BOOL *underline, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_bool *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, underline, r);

    range = (struct layout_range_bool*)get_layout_range_header_by_pos(&This->underline_ranges, position);
    *underline = range->value;

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_GetStrikethrough(IDWriteTextLayout3 *iface,
    UINT32 position, BOOL *strikethrough, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_bool *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, strikethrough, r);

    range = (struct layout_range_bool*)get_layout_range_header_by_pos(&This->strike_ranges, position);
    *strikethrough = range->value;

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_GetDrawingEffect(IDWriteTextLayout3 *iface,
    UINT32 position, IUnknown **effect, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_iface *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, effect, r);

    range = (struct layout_range_iface*)get_layout_range_header_by_pos(&This->effects, position);
    *effect = range->iface;
    if (*effect)
        IUnknown_AddRef(*effect);

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_GetInlineObject(IDWriteTextLayout3 *iface,
    UINT32 position, IDWriteInlineObject **object, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, object, r);

    if (position >= This->len)
        return S_OK;

    range = get_layout_range_by_pos(This, position);
    *object = range->object;
    if (*object)
        IDWriteInlineObject_AddRef(*object);

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_GetTypography(IDWriteTextLayout3 *iface,
    UINT32 position, IDWriteTypography** typography, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_iface *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, typography, r);

    range = (struct layout_range_iface*)get_layout_range_header_by_pos(&This->typographies, position);
    *typography = (IDWriteTypography*)range->iface;
    if (*typography)
        IDWriteTypography_AddRef(*typography);

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetLocaleNameLength(IDWriteTextLayout3 *iface,
    UINT32 position, UINT32* length, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%u %p %p)\n", This, position, length, r);
    return get_string_attribute_length(This, LAYOUT_RANGE_ATTR_LOCALE, position, length, r);
}

static HRESULT WINAPI dwritetextlayout_layout_GetLocaleName(IDWriteTextLayout3 *iface,
    UINT32 position, WCHAR* locale, UINT32 length, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%u %p %u %p)\n", This, position, locale, length, r);
    return get_string_attribute_value(This, LAYOUT_RANGE_ATTR_LOCALE, position, locale, length, r);
}

static inline FLOAT renderer_apply_snapping(FLOAT coord, BOOL skiptransform, FLOAT ppdip, FLOAT det,
    const DWRITE_MATRIX *m)
{
    FLOAT vec[2], vec2[2];

    if (!skiptransform) {
        /* apply transform */
        vec[0] = 0.0f;
        vec[1] = coord * ppdip;

        vec2[0] = m->m11 * vec[0] + m->m21 * vec[1] + m->dx;
        vec2[1] = m->m12 * vec[0] + m->m22 * vec[1] + m->dy;

        /* snap */
        vec2[0] = floorf(vec2[0] + 0.5f);
        vec2[1] = floorf(vec2[1] + 0.5f);

        /* apply inverted transform, we don't care about X component at this point */
        vec[1] = (-m->m12 * vec2[0] + m->m11 * vec2[1] - (m->m11 * m->dy - m->m12 * m->dx)) / det;
        vec[1] /= ppdip;
    }
    else
        vec[1] = floorf(coord * ppdip + 0.5f) / ppdip;

    return vec[1];
}

static HRESULT WINAPI dwritetextlayout_Draw(IDWriteTextLayout3 *iface,
    void *context, IDWriteTextRenderer* renderer, FLOAT origin_x, FLOAT origin_y)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    BOOL disabled = FALSE, skiptransform = FALSE;
    struct layout_effective_inline *inlineobject;
    struct layout_effective_run *run;
    struct layout_strikethrough *s;
    struct layout_underline *u;
    FLOAT det = 0.0f, ppdip = 0.0f;
    DWRITE_MATRIX m = { 0 };
    HRESULT hr;

    TRACE("(%p)->(%p %p %.2f %.2f)\n", This, context, renderer, origin_x, origin_y);

    hr = layout_compute_effective_runs(This);
    if (FAILED(hr))
        return hr;

    hr = IDWriteTextRenderer_IsPixelSnappingDisabled(renderer, context, &disabled);
    if (FAILED(hr))
        return hr;

    if (!disabled) {
        hr = IDWriteTextRenderer_GetPixelsPerDip(renderer, context, &ppdip);
        if (FAILED(hr))
            return hr;

        hr = IDWriteTextRenderer_GetCurrentTransform(renderer, context, &m);
        if (FAILED(hr))
            return hr;

        /* it's only allowed to have a diagonal/antidiagonal transform matrix */
        if (ppdip <= 0.0f ||
            (m.m11 * m.m22 != 0.0f && (m.m12 != 0.0f || m.m21 != 0.0f)) ||
            (m.m12 * m.m21 != 0.0f && (m.m11 != 0.0f || m.m22 != 0.0f)))
            disabled = TRUE;
        else
            skiptransform = should_skip_transform(&m, &det);
    }

#define SNAP_COORD(x) (disabled ? (x) : renderer_apply_snapping((x), skiptransform, ppdip, det, &m))
    /* 1. Regular runs */
    LIST_FOR_EACH_ENTRY(run, &This->eruns, struct layout_effective_run, entry) {
        const struct regular_layout_run *regular = &run->run->u.regular;
        UINT32 start_glyph = regular->clustermap[run->start];
        DWRITE_GLYPH_RUN_DESCRIPTION descr;
        DWRITE_GLYPH_RUN glyph_run;

        /* Everything but cluster map will be reused from nominal run, as we only need
           to adjust some pointers. Cluster map however is rebuilt when effective run is added,
           it can't be reused because it has to start with 0 index for each reported run. */
        glyph_run = regular->run;
        glyph_run.glyphCount = run->glyphcount;

        /* fixup glyph data arrays */
        glyph_run.glyphIndices += start_glyph;
        glyph_run.glyphAdvances += start_glyph;
        glyph_run.glyphOffsets += start_glyph;

        /* description */
        descr = regular->descr;
        descr.stringLength = run->length;
        descr.string += run->start;
        descr.clusterMap = run->clustermap;
        descr.textPosition += run->start;

        /* return value is ignored */
        IDWriteTextRenderer_DrawGlyphRun(renderer,
            context,
            run->origin_x + run->align_dx + origin_x,
            SNAP_COORD(run->origin_y + origin_y),
            This->measuringmode,
            &glyph_run,
            &descr,
            run->effect);
    }

    /* 2. Inline objects */
    LIST_FOR_EACH_ENTRY(inlineobject, &This->inlineobjects, struct layout_effective_inline, entry) {
        IDWriteTextRenderer_DrawInlineObject(renderer,
            context,
            inlineobject->origin_x + inlineobject->align_dx + origin_x,
            SNAP_COORD(inlineobject->origin_y + origin_y),
            inlineobject->object,
            inlineobject->is_sideways,
            inlineobject->is_rtl,
            inlineobject->effect);
    }

    /* 3. Underlines */
    LIST_FOR_EACH_ENTRY(u, &This->underlines, struct layout_underline, entry) {
        IDWriteTextRenderer_DrawUnderline(renderer,
            context,
            /* horizontal underline always grows from left to right, width is always added to origin regardless of run direction */
            (is_run_rtl(u->run) ? u->run->origin_x - u->run->width : u->run->origin_x) + u->run->align_dx + origin_x,
            SNAP_COORD(u->run->origin_y + origin_y),
            &u->u,
            u->run->effect);
    }

    /* 4. Strikethrough */
    LIST_FOR_EACH_ENTRY(s, &This->strikethrough, struct layout_strikethrough, entry) {
        IDWriteTextRenderer_DrawStrikethrough(renderer,
            context,
            s->run->origin_x + s->run->align_dx + origin_x,
            SNAP_COORD(s->run->origin_y + origin_y),
            &s->s,
            s->run->effect);
    }
#undef SNAP_COORD

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_GetLineMetrics(IDWriteTextLayout3 *iface,
    DWRITE_LINE_METRICS *metrics, UINT32 max_count, UINT32 *count)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p %u %p)\n", This, metrics, max_count, count);

    hr = layout_compute_effective_runs(This);
    if (FAILED(hr))
        return hr;

    if (metrics) {
        UINT32 i, c = min(max_count, This->metrics.lineCount);
        for (i = 0; i < c; i++)
            memcpy(metrics + i, This->linemetrics + i, sizeof(*metrics));
    }

    *count = This->metrics.lineCount;
    return max_count >= This->metrics.lineCount ? S_OK : E_NOT_SUFFICIENT_BUFFER;
}

static HRESULT WINAPI dwritetextlayout_GetMetrics(IDWriteTextLayout3 *iface, DWRITE_TEXT_METRICS *metrics)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    DWRITE_TEXT_METRICS1 metrics1;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, metrics);

    hr = IDWriteTextLayout3_GetMetrics(iface, &metrics1);
    if (hr == S_OK)
        memcpy(metrics, &metrics1, sizeof(*metrics));

    return hr;
}

static void scale_glyph_bbox(RECT *bbox, FLOAT emSize, UINT16 units_per_em, D2D_RECT_F *ret)
{
#define SCALE(x) ((FLOAT)x * emSize / (FLOAT)units_per_em)
    ret->left = SCALE(bbox->left);
    ret->right = SCALE(bbox->right);
    ret->top = SCALE(bbox->top);
    ret->bottom = SCALE(bbox->bottom);
#undef SCALE
}

static void d2d_rect_offset(D2D_RECT_F *rect, FLOAT x, FLOAT y)
{
    rect->left += x;
    rect->right += x;
    rect->top += y;
    rect->bottom += y;
}

static BOOL d2d_rect_is_empty(const D2D_RECT_F *rect)
{
    return ((rect->left >= rect->right) || (rect->top >= rect->bottom));
}

static void d2d_rect_union(D2D_RECT_F *dst, const D2D_RECT_F *src)
{
    if (d2d_rect_is_empty(dst)) {
        if (d2d_rect_is_empty(src)) {
            dst->left = dst->right = dst->top = dst->bottom = 0.0f;
            return;
        }
        else
            *dst = *src;
    }
    else {
        if (!d2d_rect_is_empty(src)) {
            dst->left   = min(dst->left, src->left);
            dst->right  = max(dst->right, src->right);
            dst->top    = min(dst->top, src->top);
            dst->bottom = max(dst->bottom, src->bottom);
        }
    }
}

static void layout_get_erun_bbox(struct dwrite_textlayout *layout, struct layout_effective_run *run, D2D_RECT_F *bbox)
{
    const struct regular_layout_run *regular = &run->run->u.regular;
    UINT32 start_glyph = regular->clustermap[run->start];
    const DWRITE_GLYPH_RUN *glyph_run = &regular->run;
    DWRITE_FONT_METRICS font_metrics;
    D2D_POINT_2F origin = { 0 };
    UINT32 i;

    IDWriteFontFace_GetMetrics(glyph_run->fontFace, &font_metrics);

    origin.x = run->origin_x + run->align_dx;
    origin.y = run->origin_y;
    for (i = 0; i < run->glyphcount; i++) {
        D2D_RECT_F glyph_bbox;
        RECT design_bbox;

        freetype_get_design_glyph_bbox((IDWriteFontFace4 *)glyph_run->fontFace, font_metrics.designUnitsPerEm,
                glyph_run->glyphIndices[i + start_glyph], &design_bbox);

        scale_glyph_bbox(&design_bbox, glyph_run->fontEmSize, font_metrics.designUnitsPerEm, &glyph_bbox);
        d2d_rect_offset(&glyph_bbox, origin.x + glyph_run->glyphOffsets[i + start_glyph].advanceOffset,
                origin.y + glyph_run->glyphOffsets[i + start_glyph].ascenderOffset);
        d2d_rect_union(bbox, &glyph_bbox);

        /* FIXME: take care of vertical/rtl */
        origin.x += glyph_run->glyphAdvances[i + start_glyph];
    }
}

static HRESULT WINAPI dwritetextlayout_GetOverhangMetrics(IDWriteTextLayout3 *iface,
        DWRITE_OVERHANG_METRICS *overhangs)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_effective_run *run;
    D2D_RECT_F bbox = { 0 };
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, overhangs);

    memset(overhangs, 0, sizeof(*overhangs));

    if (!(This->recompute & RECOMPUTE_OVERHANGS)) {
        *overhangs = This->overhangs;
        return S_OK;
    }

    hr = layout_compute_effective_runs(This);
    if (FAILED(hr))
        return hr;

    LIST_FOR_EACH_ENTRY(run, &This->eruns, struct layout_effective_run, entry) {
        D2D_RECT_F run_bbox;

        layout_get_erun_bbox(This, run, &run_bbox);
        d2d_rect_union(&bbox, &run_bbox);
    }

    /* FIXME: iterate over inline objects too */

    /* deltas from text content metrics */
    This->overhangs.left = -bbox.left;
    This->overhangs.top = -bbox.top;
    This->overhangs.right = bbox.right - This->metrics.layoutWidth;
    This->overhangs.bottom = bbox.bottom - This->metrics.layoutHeight;
    This->recompute &= ~RECOMPUTE_OVERHANGS;

    *overhangs = This->overhangs;

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_GetClusterMetrics(IDWriteTextLayout3 *iface,
    DWRITE_CLUSTER_METRICS *metrics, UINT32 max_count, UINT32 *count)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p %u %p)\n", This, metrics, max_count, count);

    hr = layout_compute(This);
    if (FAILED(hr))
        return hr;

    if (metrics)
        memcpy(metrics, This->clustermetrics, sizeof(DWRITE_CLUSTER_METRICS)*min(max_count, This->cluster_count));

    *count = This->cluster_count;
    return max_count >= This->cluster_count ? S_OK : E_NOT_SUFFICIENT_BUFFER;
}

static HRESULT WINAPI dwritetextlayout_DetermineMinWidth(IDWriteTextLayout3 *iface, FLOAT* min_width)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    UINT32 start;
    FLOAT width;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, min_width);

    if (!min_width)
        return E_INVALIDARG;

    if (!(This->recompute & RECOMPUTE_MINIMAL_WIDTH))
        goto width_done;

    *min_width = 0.0f;
    hr = layout_compute(This);
    if (FAILED(hr))
        return hr;

    /* Find widest word without emergency breaking between clusters, trailing whitespaces
       preceding breaking point do not contribute to word width. */
    for (start = 0; start < This->cluster_count;) {
        UINT32 end = start, j, next;

        /* Last cluster always could be wrapped after. */
        while (!This->clustermetrics[end].canWrapLineAfter)
            end++;
        /* make is so current cluster range that we can wrap after is [start,end) */
        end++;

        next = end;

        /* Ignore trailing whitespace clusters, in case of single space range will
           be reduced to empty range, or [start,start+1). */
        while (end > start && This->clustermetrics[end-1].isWhitespace)
            end--;

        /* check if cluster range exceeds last minimal width */
        width = 0.0f;
        for (j = start; j < end; j++)
            width += This->clustermetrics[j].width;

        start = next;

        if (width > This->minwidth)
            This->minwidth = width;
    }
    This->recompute &= ~RECOMPUTE_MINIMAL_WIDTH;

width_done:
    *min_width = This->minwidth;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_HitTestPoint(IDWriteTextLayout3 *iface,
    FLOAT pointX, FLOAT pointY, BOOL* is_trailinghit, BOOL* is_inside, DWRITE_HIT_TEST_METRICS *metrics)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    FIXME("(%p)->(%f %f %p %p %p): stub\n", This, pointX, pointY, is_trailinghit, is_inside, metrics);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextlayout_HitTestTextPosition(IDWriteTextLayout3 *iface,
    UINT32 textPosition, BOOL is_trailinghit, FLOAT* pointX, FLOAT* pointY, DWRITE_HIT_TEST_METRICS *metrics)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    FIXME("(%p)->(%u %d %p %p %p): stub\n", This, textPosition, is_trailinghit, pointX, pointY, metrics);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextlayout_HitTestTextRange(IDWriteTextLayout3 *iface,
    UINT32 textPosition, UINT32 textLength, FLOAT originX, FLOAT originY,
    DWRITE_HIT_TEST_METRICS *metrics, UINT32 max_metricscount, UINT32* actual_metricscount)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    FIXME("(%p)->(%u %u %f %f %p %u %p): stub\n", This, textPosition, textLength, originX, originY, metrics,
        max_metricscount, actual_metricscount);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextlayout1_SetPairKerning(IDWriteTextLayout3 *iface, BOOL is_pairkerning_enabled,
        DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%d %s)\n", This, is_pairkerning_enabled, debugstr_range(&range));

    value.range = range;
    value.u.pair_kerning = !!is_pairkerning_enabled;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_PAIR_KERNING, &value);
}

static HRESULT WINAPI dwritetextlayout1_GetPairKerning(IDWriteTextLayout3 *iface, UINT32 position, BOOL *is_pairkerning_enabled,
        DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range *range;

    TRACE("(%p)->(%u %p %p)\n", This, position, is_pairkerning_enabled, r);

    if (position >= This->len)
        return S_OK;

    range = get_layout_range_by_pos(This, position);
    *is_pairkerning_enabled = range->pair_kerning;

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout1_SetCharacterSpacing(IDWriteTextLayout3 *iface, FLOAT leading, FLOAT trailing,
    FLOAT min_advance, DWRITE_TEXT_RANGE range)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_attr_value value;

    TRACE("(%p)->(%.2f %.2f %.2f %s)\n", This, leading, trailing, min_advance, debugstr_range(&range));

    if (min_advance < 0.0f)
        return E_INVALIDARG;

    value.range = range;
    value.u.spacing[0] = leading;
    value.u.spacing[1] = trailing;
    value.u.spacing[2] = min_advance;
    return set_layout_range_attr(This, LAYOUT_RANGE_ATTR_SPACING, &value);
}

static HRESULT WINAPI dwritetextlayout1_GetCharacterSpacing(IDWriteTextLayout3 *iface, UINT32 position, FLOAT *leading,
    FLOAT *trailing, FLOAT *min_advance, DWRITE_TEXT_RANGE *r)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    struct layout_range_spacing *range;

    TRACE("(%p)->(%u %p %p %p %p)\n", This, position, leading, trailing, min_advance, r);

    range = (struct layout_range_spacing*)get_layout_range_header_by_pos(&This->spacing, position);
    *leading = range->leading;
    *trailing = range->trailing;
    *min_advance = range->min_advance;

    return return_range(&range->h, r);
}

static HRESULT WINAPI dwritetextlayout2_GetMetrics(IDWriteTextLayout3 *iface, DWRITE_TEXT_METRICS1 *metrics)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, metrics);

    hr = layout_compute_effective_runs(This);
    if (FAILED(hr))
        return hr;

    *metrics = This->metrics;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout2_SetVerticalGlyphOrientation(IDWriteTextLayout3 *iface, DWRITE_VERTICAL_GLYPH_ORIENTATION orientation)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);

    TRACE("(%p)->(%d)\n", This, orientation);

    if ((UINT32)orientation > DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED)
        return E_INVALIDARG;

    This->format.vertical_orientation = orientation;
    return S_OK;
}

static DWRITE_VERTICAL_GLYPH_ORIENTATION WINAPI dwritetextlayout2_GetVerticalGlyphOrientation(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)\n", This);
    return This->format.vertical_orientation;
}

static HRESULT WINAPI dwritetextlayout2_SetLastLineWrapping(IDWriteTextLayout3 *iface, BOOL lastline_wrapping_enabled)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%d)\n", This, lastline_wrapping_enabled);
    return IDWriteTextFormat1_SetLastLineWrapping(&This->IDWriteTextFormat1_iface, lastline_wrapping_enabled);
}

static BOOL WINAPI dwritetextlayout2_GetLastLineWrapping(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)\n", This);
    return IDWriteTextFormat1_GetLastLineWrapping(&This->IDWriteTextFormat1_iface);
}

static HRESULT WINAPI dwritetextlayout2_SetOpticalAlignment(IDWriteTextLayout3 *iface, DWRITE_OPTICAL_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%d)\n", This, alignment);
    return IDWriteTextFormat1_SetOpticalAlignment(&This->IDWriteTextFormat1_iface, alignment);
}

static DWRITE_OPTICAL_ALIGNMENT WINAPI dwritetextlayout2_GetOpticalAlignment(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)\n", This);
    return IDWriteTextFormat1_GetOpticalAlignment(&This->IDWriteTextFormat1_iface);
}

static HRESULT WINAPI dwritetextlayout2_SetFontFallback(IDWriteTextLayout3 *iface, IDWriteFontFallback *fallback)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return set_fontfallback_for_format(&This->format, fallback);
}

static HRESULT WINAPI dwritetextlayout2_GetFontFallback(IDWriteTextLayout3 *iface, IDWriteFontFallback **fallback)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return get_fontfallback_from_format(&This->format, fallback);
}

static HRESULT WINAPI dwritetextlayout3_InvalidateLayout(IDWriteTextLayout3 *iface)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);

    TRACE("(%p)\n", This);

    This->recompute = RECOMPUTE_EVERYTHING;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout3_SetLineSpacing(IDWriteTextLayout3 *iface, DWRITE_LINE_SPACING const *spacing)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, spacing);

    hr = format_set_linespacing(&This->format, spacing, &changed);
    if (FAILED(hr))
        return hr;

    if (changed) {
        if (!(This->recompute & RECOMPUTE_LINES)) {
            UINT32 line;

            switch (This->format.spacing.method)
            {
            case DWRITE_LINE_SPACING_METHOD_DEFAULT:
                for (line = 0; line < This->metrics.lineCount; line++) {
                    This->linemetrics[line].height = This->lines[line].height;
                    This->linemetrics[line].baseline = This->lines[line].baseline;
                }
                break;
            case DWRITE_LINE_SPACING_METHOD_UNIFORM:
                for (line = 0; line < This->metrics.lineCount; line++) {
                    This->linemetrics[line].height = This->format.spacing.height;
                    This->linemetrics[line].baseline = This->format.spacing.baseline;
                }
                break;
            case DWRITE_LINE_SPACING_METHOD_PROPORTIONAL:
                for (line = 0; line < This->metrics.lineCount; line++) {
                    This->linemetrics[line].height = This->format.spacing.height * This->lines[line].height;
                    This->linemetrics[line].baseline = This->format.spacing.baseline * This->lines[line].baseline;
                }
                break;
            default:
                ;
            }

            layout_set_line_positions(This);
        }

        This->recompute |= RECOMPUTE_OVERHANGS;
    }

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout3_GetLineSpacing(IDWriteTextLayout3 *iface, DWRITE_LINE_SPACING *spacing)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);

    TRACE("(%p)->(%p)\n", This, spacing);

    *spacing = This->format.spacing;
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout3_GetLineMetrics(IDWriteTextLayout3 *iface, DWRITE_LINE_METRICS1 *metrics,
    UINT32 max_count, UINT32 *count)
{
    struct dwrite_textlayout *This = impl_from_IDWriteTextLayout3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p %u %p)\n", This, metrics, max_count, count);

    hr = layout_compute_effective_runs(This);
    if (FAILED(hr))
        return hr;

    if (metrics)
        memcpy(metrics, This->linemetrics, sizeof(*metrics) * min(max_count, This->metrics.lineCount));

    *count = This->metrics.lineCount;
    return max_count >= This->metrics.lineCount ? S_OK : E_NOT_SUFFICIENT_BUFFER;
}

static const IDWriteTextLayout3Vtbl dwritetextlayoutvtbl = {
    dwritetextlayout_QueryInterface,
    dwritetextlayout_AddRef,
    dwritetextlayout_Release,
    dwritetextlayout_SetTextAlignment,
    dwritetextlayout_SetParagraphAlignment,
    dwritetextlayout_SetWordWrapping,
    dwritetextlayout_SetReadingDirection,
    dwritetextlayout_SetFlowDirection,
    dwritetextlayout_SetIncrementalTabStop,
    dwritetextlayout_SetTrimming,
    dwritetextlayout_SetLineSpacing,
    dwritetextlayout_GetTextAlignment,
    dwritetextlayout_GetParagraphAlignment,
    dwritetextlayout_GetWordWrapping,
    dwritetextlayout_GetReadingDirection,
    dwritetextlayout_GetFlowDirection,
    dwritetextlayout_GetIncrementalTabStop,
    dwritetextlayout_GetTrimming,
    dwritetextlayout_GetLineSpacing,
    dwritetextlayout_GetFontCollection,
    dwritetextlayout_GetFontFamilyNameLength,
    dwritetextlayout_GetFontFamilyName,
    dwritetextlayout_GetFontWeight,
    dwritetextlayout_GetFontStyle,
    dwritetextlayout_GetFontStretch,
    dwritetextlayout_GetFontSize,
    dwritetextlayout_GetLocaleNameLength,
    dwritetextlayout_GetLocaleName,
    dwritetextlayout_SetMaxWidth,
    dwritetextlayout_SetMaxHeight,
    dwritetextlayout_SetFontCollection,
    dwritetextlayout_SetFontFamilyName,
    dwritetextlayout_SetFontWeight,
    dwritetextlayout_SetFontStyle,
    dwritetextlayout_SetFontStretch,
    dwritetextlayout_SetFontSize,
    dwritetextlayout_SetUnderline,
    dwritetextlayout_SetStrikethrough,
    dwritetextlayout_SetDrawingEffect,
    dwritetextlayout_SetInlineObject,
    dwritetextlayout_SetTypography,
    dwritetextlayout_SetLocaleName,
    dwritetextlayout_GetMaxWidth,
    dwritetextlayout_GetMaxHeight,
    dwritetextlayout_layout_GetFontCollection,
    dwritetextlayout_layout_GetFontFamilyNameLength,
    dwritetextlayout_layout_GetFontFamilyName,
    dwritetextlayout_layout_GetFontWeight,
    dwritetextlayout_layout_GetFontStyle,
    dwritetextlayout_layout_GetFontStretch,
    dwritetextlayout_layout_GetFontSize,
    dwritetextlayout_GetUnderline,
    dwritetextlayout_GetStrikethrough,
    dwritetextlayout_GetDrawingEffect,
    dwritetextlayout_GetInlineObject,
    dwritetextlayout_GetTypography,
    dwritetextlayout_layout_GetLocaleNameLength,
    dwritetextlayout_layout_GetLocaleName,
    dwritetextlayout_Draw,
    dwritetextlayout_GetLineMetrics,
    dwritetextlayout_GetMetrics,
    dwritetextlayout_GetOverhangMetrics,
    dwritetextlayout_GetClusterMetrics,
    dwritetextlayout_DetermineMinWidth,
    dwritetextlayout_HitTestPoint,
    dwritetextlayout_HitTestTextPosition,
    dwritetextlayout_HitTestTextRange,
    dwritetextlayout1_SetPairKerning,
    dwritetextlayout1_GetPairKerning,
    dwritetextlayout1_SetCharacterSpacing,
    dwritetextlayout1_GetCharacterSpacing,
    dwritetextlayout2_GetMetrics,
    dwritetextlayout2_SetVerticalGlyphOrientation,
    dwritetextlayout2_GetVerticalGlyphOrientation,
    dwritetextlayout2_SetLastLineWrapping,
    dwritetextlayout2_GetLastLineWrapping,
    dwritetextlayout2_SetOpticalAlignment,
    dwritetextlayout2_GetOpticalAlignment,
    dwritetextlayout2_SetFontFallback,
    dwritetextlayout2_GetFontFallback,
    dwritetextlayout3_InvalidateLayout,
    dwritetextlayout3_SetLineSpacing,
    dwritetextlayout3_GetLineSpacing,
    dwritetextlayout3_GetLineMetrics
};

static HRESULT WINAPI dwritetextformat_layout_QueryInterface(IDWriteTextFormat1 *iface, REFIID riid, void **obj)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);
    return IDWriteTextLayout3_QueryInterface(&This->IDWriteTextLayout3_iface, riid, obj);
}

static ULONG WINAPI dwritetextformat_layout_AddRef(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    return IDWriteTextLayout3_AddRef(&This->IDWriteTextLayout3_iface);
}

static ULONG WINAPI dwritetextformat_layout_Release(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    return IDWriteTextLayout3_Release(&This->IDWriteTextLayout3_iface);
}

static HRESULT WINAPI dwritetextformat_layout_SetTextAlignment(IDWriteTextFormat1 *iface, DWRITE_TEXT_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%d)\n", This, alignment);

    hr = format_set_textalignment(&This->format, alignment, &changed);
    if (FAILED(hr))
        return hr;

    /* if layout is not ready there's nothing to align */
    if (changed && !(This->recompute & RECOMPUTE_LINES))
        layout_apply_text_alignment(This);

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_SetParagraphAlignment(IDWriteTextFormat1 *iface, DWRITE_PARAGRAPH_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%d)\n", This, alignment);

    hr = format_set_paralignment(&This->format, alignment, &changed);
    if (FAILED(hr))
        return hr;

    /* if layout is not ready there's nothing to align */
    if (changed && !(This->recompute & RECOMPUTE_LINES))
        layout_apply_par_alignment(This);

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_SetWordWrapping(IDWriteTextFormat1 *iface, DWRITE_WORD_WRAPPING wrapping)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%d)\n", This, wrapping);

    hr = format_set_wordwrapping(&This->format, wrapping, &changed);
    if (FAILED(hr))
        return hr;

    if (changed)
        This->recompute |= RECOMPUTE_LINES_AND_OVERHANGS;

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_SetReadingDirection(IDWriteTextFormat1 *iface, DWRITE_READING_DIRECTION direction)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%d)\n", This, direction);

    hr = format_set_readingdirection(&This->format, direction, &changed);
    if (FAILED(hr))
        return hr;

    if (changed)
        This->recompute = RECOMPUTE_EVERYTHING;

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_SetFlowDirection(IDWriteTextFormat1 *iface, DWRITE_FLOW_DIRECTION direction)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%d)\n", This, direction);

    hr = format_set_flowdirection(&This->format, direction, &changed);
    if (FAILED(hr))
        return hr;

    if (changed)
        This->recompute = RECOMPUTE_EVERYTHING;

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_SetIncrementalTabStop(IDWriteTextFormat1 *iface, FLOAT tabstop)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    FIXME("(%p)->(%f): stub\n", This, tabstop);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextformat_layout_SetTrimming(IDWriteTextFormat1 *iface, DWRITE_TRIMMING const *trimming,
    IDWriteInlineObject *trimming_sign)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    BOOL changed;
    HRESULT hr;

    TRACE("(%p)->(%p %p)\n", This, trimming, trimming_sign);

    hr = format_set_trimming(&This->format, trimming, trimming_sign, &changed);

    if (changed)
        This->recompute |= RECOMPUTE_LINES_AND_OVERHANGS;

    return hr;
}

static HRESULT WINAPI dwritetextformat_layout_SetLineSpacing(IDWriteTextFormat1 *iface, DWRITE_LINE_SPACING_METHOD method,
    FLOAT height, FLOAT baseline)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    DWRITE_LINE_SPACING spacing;

    TRACE("(%p)->(%d %f %f)\n", This, method, height, baseline);

    spacing = This->format.spacing;
    spacing.method = method;
    spacing.height = height;
    spacing.baseline = baseline;
    return IDWriteTextLayout3_SetLineSpacing(&This->IDWriteTextLayout3_iface, &spacing);
}

static DWRITE_TEXT_ALIGNMENT WINAPI dwritetextformat_layout_GetTextAlignment(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.textalignment;
}

static DWRITE_PARAGRAPH_ALIGNMENT WINAPI dwritetextformat_layout_GetParagraphAlignment(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.paralign;
}

static DWRITE_WORD_WRAPPING WINAPI dwritetextformat_layout_GetWordWrapping(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.wrapping;
}

static DWRITE_READING_DIRECTION WINAPI dwritetextformat_layout_GetReadingDirection(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.readingdir;
}

static DWRITE_FLOW_DIRECTION WINAPI dwritetextformat_layout_GetFlowDirection(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.flow;
}

static FLOAT WINAPI dwritetextformat_layout_GetIncrementalTabStop(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    FIXME("(%p): stub\n", This);
    return 0.0f;
}

static HRESULT WINAPI dwritetextformat_layout_GetTrimming(IDWriteTextFormat1 *iface, DWRITE_TRIMMING *options,
    IDWriteInlineObject **trimming_sign)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%p %p)\n", This, options, trimming_sign);

    *options = This->format.trimming;
    *trimming_sign = This->format.trimmingsign;
    if (*trimming_sign)
        IDWriteInlineObject_AddRef(*trimming_sign);
    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_GetLineSpacing(IDWriteTextFormat1 *iface, DWRITE_LINE_SPACING_METHOD *method,
    FLOAT *spacing, FLOAT *baseline)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%p %p %p)\n", This, method, spacing, baseline);

    *method = This->format.spacing.method;
    *spacing = This->format.spacing.height;
    *baseline = This->format.spacing.baseline;
    return S_OK;
}

static HRESULT WINAPI dwritetextformat_layout_GetFontCollection(IDWriteTextFormat1 *iface, IDWriteFontCollection **collection)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%p)\n", This, collection);

    *collection = This->format.collection;
    if (*collection)
        IDWriteFontCollection_AddRef(*collection);
    return S_OK;
}

static UINT32 WINAPI dwritetextformat_layout_GetFontFamilyNameLength(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.family_len;
}

static HRESULT WINAPI dwritetextformat_layout_GetFontFamilyName(IDWriteTextFormat1 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%p %u)\n", This, name, size);

    if (size <= This->format.family_len) return E_NOT_SUFFICIENT_BUFFER;
    strcpyW(name, This->format.family_name);
    return S_OK;
}

static DWRITE_FONT_WEIGHT WINAPI dwritetextformat_layout_GetFontWeight(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.weight;
}

static DWRITE_FONT_STYLE WINAPI dwritetextformat_layout_GetFontStyle(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.style;
}

static DWRITE_FONT_STRETCH WINAPI dwritetextformat_layout_GetFontStretch(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.stretch;
}

static FLOAT WINAPI dwritetextformat_layout_GetFontSize(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.fontsize;
}

static UINT32 WINAPI dwritetextformat_layout_GetLocaleNameLength(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.locale_len;
}

static HRESULT WINAPI dwritetextformat_layout_GetLocaleName(IDWriteTextFormat1 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%p %u)\n", This, name, size);

    if (size <= This->format.locale_len) return E_NOT_SUFFICIENT_BUFFER;
    strcpyW(name, This->format.locale);
    return S_OK;
}

static HRESULT WINAPI dwritetextformat1_layout_SetVerticalGlyphOrientation(IDWriteTextFormat1 *iface, DWRITE_VERTICAL_GLYPH_ORIENTATION orientation)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    FIXME("(%p)->(%d): stub\n", This, orientation);
    return E_NOTIMPL;
}

static DWRITE_VERTICAL_GLYPH_ORIENTATION WINAPI dwritetextformat1_layout_GetVerticalGlyphOrientation(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    FIXME("(%p): stub\n", This);
    return DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT;
}

static HRESULT WINAPI dwritetextformat1_layout_SetLastLineWrapping(IDWriteTextFormat1 *iface, BOOL lastline_wrapping_enabled)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);

    TRACE("(%p)->(%d)\n", This, lastline_wrapping_enabled);

    This->format.last_line_wrapping = !!lastline_wrapping_enabled;
    return S_OK;
}

static BOOL WINAPI dwritetextformat1_layout_GetLastLineWrapping(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.last_line_wrapping;
}

static HRESULT WINAPI dwritetextformat1_layout_SetOpticalAlignment(IDWriteTextFormat1 *iface, DWRITE_OPTICAL_ALIGNMENT alignment)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)->(%d)\n", This, alignment);
    return format_set_optical_alignment(&This->format, alignment);
}

static DWRITE_OPTICAL_ALIGNMENT WINAPI dwritetextformat1_layout_GetOpticalAlignment(IDWriteTextFormat1 *iface)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)\n", This);
    return This->format.optical_alignment;
}

static HRESULT WINAPI dwritetextformat1_layout_SetFontFallback(IDWriteTextFormat1 *iface, IDWriteFontFallback *fallback)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return IDWriteTextLayout3_SetFontFallback(&This->IDWriteTextLayout3_iface, fallback);
}

static HRESULT WINAPI dwritetextformat1_layout_GetFontFallback(IDWriteTextFormat1 *iface, IDWriteFontFallback **fallback)
{
    struct dwrite_textlayout *This = impl_layout_from_IDWriteTextFormat1(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return IDWriteTextLayout3_GetFontFallback(&This->IDWriteTextLayout3_iface, fallback);
}

static const IDWriteTextFormat1Vtbl dwritetextformat1_layout_vtbl = {
    dwritetextformat_layout_QueryInterface,
    dwritetextformat_layout_AddRef,
    dwritetextformat_layout_Release,
    dwritetextformat_layout_SetTextAlignment,
    dwritetextformat_layout_SetParagraphAlignment,
    dwritetextformat_layout_SetWordWrapping,
    dwritetextformat_layout_SetReadingDirection,
    dwritetextformat_layout_SetFlowDirection,
    dwritetextformat_layout_SetIncrementalTabStop,
    dwritetextformat_layout_SetTrimming,
    dwritetextformat_layout_SetLineSpacing,
    dwritetextformat_layout_GetTextAlignment,
    dwritetextformat_layout_GetParagraphAlignment,
    dwritetextformat_layout_GetWordWrapping,
    dwritetextformat_layout_GetReadingDirection,
    dwritetextformat_layout_GetFlowDirection,
    dwritetextformat_layout_GetIncrementalTabStop,
    dwritetextformat_layout_GetTrimming,
    dwritetextformat_layout_GetLineSpacing,
    dwritetextformat_layout_GetFontCollection,
    dwritetextformat_layout_GetFontFamilyNameLength,
    dwritetextformat_layout_GetFontFamilyName,
    dwritetextformat_layout_GetFontWeight,
    dwritetextformat_layout_GetFontStyle,
    dwritetextformat_layout_GetFontStretch,
    dwritetextformat_layout_GetFontSize,
    dwritetextformat_layout_GetLocaleNameLength,
    dwritetextformat_layout_GetLocaleName,
    dwritetextformat1_layout_SetVerticalGlyphOrientation,
    dwritetextformat1_layout_GetVerticalGlyphOrientation,
    dwritetextformat1_layout_SetLastLineWrapping,
    dwritetextformat1_layout_GetLastLineWrapping,
    dwritetextformat1_layout_SetOpticalAlignment,
    dwritetextformat1_layout_GetOpticalAlignment,
    dwritetextformat1_layout_SetFontFallback,
    dwritetextformat1_layout_GetFontFallback,
};

static HRESULT WINAPI dwritetextlayout_sink_QueryInterface(IDWriteTextAnalysisSink1 *iface,
    REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IDWriteTextAnalysisSink1) ||
        IsEqualIID(riid, &IID_IDWriteTextAnalysisSink) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteTextAnalysisSink1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetextlayout_sink_AddRef(IDWriteTextAnalysisSink1 *iface)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSink1(iface);
    return IDWriteTextLayout3_AddRef(&layout->IDWriteTextLayout3_iface);
}

static ULONG WINAPI dwritetextlayout_sink_Release(IDWriteTextAnalysisSink1 *iface)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSink1(iface);
    return IDWriteTextLayout3_Release(&layout->IDWriteTextLayout3_iface);
}

static HRESULT WINAPI dwritetextlayout_sink_SetScriptAnalysis(IDWriteTextAnalysisSink1 *iface,
    UINT32 position, UINT32 length, DWRITE_SCRIPT_ANALYSIS const* sa)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSink1(iface);
    struct layout_run *run;

    TRACE("[%u,%u) script=%u:%s\n", position, position + length, sa->script, debugstr_sa_script(sa->script));

    run = alloc_layout_run(LAYOUT_RUN_REGULAR);
    if (!run)
        return E_OUTOFMEMORY;

    run->u.regular.descr.string = &layout->str[position];
    run->u.regular.descr.stringLength = length;
    run->u.regular.descr.textPosition = position;
    run->u.regular.sa = *sa;
    list_add_tail(&layout->runs, &run->entry);
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_sink_SetLineBreakpoints(IDWriteTextAnalysisSink1 *iface,
    UINT32 position, UINT32 length, DWRITE_LINE_BREAKPOINT const* breakpoints)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSink1(iface);

    if (position + length > layout->len)
        return E_FAIL;

    memcpy(&layout->nominal_breakpoints[position], breakpoints, length*sizeof(DWRITE_LINE_BREAKPOINT));
    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_sink_SetBidiLevel(IDWriteTextAnalysisSink1 *iface, UINT32 position,
    UINT32 length, UINT8 explicitLevel, UINT8 resolvedLevel)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSink1(iface);
    struct layout_run *cur_run;

    TRACE("[%u,%u) %u %u\n", position, position + length, explicitLevel, resolvedLevel);

    LIST_FOR_EACH_ENTRY(cur_run, &layout->runs, struct layout_run, entry) {
        struct regular_layout_run *cur = &cur_run->u.regular;
        struct layout_run *run;

        if (cur_run->kind == LAYOUT_RUN_INLINE)
            continue;

        /* FIXME: levels are reported in a natural forward direction, so start loop from a run we ended on */
        if (position < cur->descr.textPosition || position >= cur->descr.textPosition + cur->descr.stringLength)
            continue;

        /* full hit - just set run level */
        if (cur->descr.textPosition == position && cur->descr.stringLength == length) {
            cur->run.bidiLevel = resolvedLevel;
            break;
        }

        /* current run is fully covered, move to next one */
        if (cur->descr.textPosition == position && cur->descr.stringLength < length) {
            cur->run.bidiLevel = resolvedLevel;
            position += cur->descr.stringLength;
            length -= cur->descr.stringLength;
            continue;
        }

        /* all fully covered runs are processed at this point, reuse existing run for remaining
           reported bidi range and add another run for the rest of original one */

        run = alloc_layout_run(LAYOUT_RUN_REGULAR);
        if (!run)
            return E_OUTOFMEMORY;

        *run = *cur_run;
        run->u.regular.descr.textPosition = position + length;
        run->u.regular.descr.stringLength = cur->descr.stringLength - length;
        run->u.regular.descr.string = &layout->str[position + length];

        /* reduce existing run */
        cur->run.bidiLevel = resolvedLevel;
        cur->descr.stringLength = length;

        list_add_after(&cur_run->entry, &run->entry);
        break;
    }

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_sink_SetNumberSubstitution(IDWriteTextAnalysisSink1 *iface,
    UINT32 position, UINT32 length, IDWriteNumberSubstitution* substitution)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextlayout_sink_SetGlyphOrientation(IDWriteTextAnalysisSink1 *iface,
    UINT32 position, UINT32 length, DWRITE_GLYPH_ORIENTATION_ANGLE angle, UINT8 adjusted_bidi_level,
    BOOL is_sideways, BOOL is_rtl)
{
    return E_NOTIMPL;
}

static const IDWriteTextAnalysisSink1Vtbl dwritetextlayoutsinkvtbl = {
    dwritetextlayout_sink_QueryInterface,
    dwritetextlayout_sink_AddRef,
    dwritetextlayout_sink_Release,
    dwritetextlayout_sink_SetScriptAnalysis,
    dwritetextlayout_sink_SetLineBreakpoints,
    dwritetextlayout_sink_SetBidiLevel,
    dwritetextlayout_sink_SetNumberSubstitution,
    dwritetextlayout_sink_SetGlyphOrientation
};

static HRESULT WINAPI dwritetextlayout_source_QueryInterface(IDWriteTextAnalysisSource1 *iface,
    REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IDWriteTextAnalysisSource1) ||
        IsEqualIID(riid, &IID_IDWriteTextAnalysisSource) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteTextAnalysisSource1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetextlayout_source_AddRef(IDWriteTextAnalysisSource1 *iface)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);
    return IDWriteTextLayout3_AddRef(&layout->IDWriteTextLayout3_iface);
}

static ULONG WINAPI dwritetextlayout_source_Release(IDWriteTextAnalysisSource1 *iface)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);
    return IDWriteTextLayout3_Release(&layout->IDWriteTextLayout3_iface);
}

static HRESULT WINAPI dwritetextlayout_source_GetTextAtPosition(IDWriteTextAnalysisSource1 *iface,
    UINT32 position, WCHAR const** text, UINT32* text_len)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);

    TRACE("(%p)->(%u %p %p)\n", layout, position, text, text_len);

    if (position < layout->len) {
        *text = &layout->str[position];
        *text_len = layout->len - position;
    }
    else {
        *text = NULL;
        *text_len = 0;
    }

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_source_GetTextBeforePosition(IDWriteTextAnalysisSource1 *iface,
    UINT32 position, WCHAR const** text, UINT32* text_len)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);

    TRACE("(%p)->(%u %p %p)\n", layout, position, text, text_len);

    if (position > 0 && position < layout->len) {
        *text = layout->str;
        *text_len = position;
    }
    else {
        *text = NULL;
        *text_len = 0;
    }

    return S_OK;
}

static DWRITE_READING_DIRECTION WINAPI dwritetextlayout_source_GetParagraphReadingDirection(IDWriteTextAnalysisSource1 *iface)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);
    return IDWriteTextLayout3_GetReadingDirection(&layout->IDWriteTextLayout3_iface);
}

static HRESULT WINAPI dwritetextlayout_source_GetLocaleName(IDWriteTextAnalysisSource1 *iface,
    UINT32 position, UINT32* text_len, WCHAR const** locale)
{
    struct dwrite_textlayout *layout = impl_from_IDWriteTextAnalysisSource1(iface);
    struct layout_range *range = get_layout_range_by_pos(layout, position);

    if (position < layout->len) {
        struct layout_range *next;

        *locale = range->locale;
        *text_len = range->h.range.length - position;

        next = LIST_ENTRY(list_next(&layout->ranges, &range->h.entry), struct layout_range, h.entry);
        while (next && next->h.range.startPosition < layout->len && !strcmpW(range->locale, next->locale)) {
            *text_len += next->h.range.length;
            next = LIST_ENTRY(list_next(&layout->ranges, &next->h.entry), struct layout_range, h.entry);
        }

        *text_len = min(*text_len, layout->len - position);
    }
    else {
        *locale = NULL;
        *text_len = 0;
    }

    return S_OK;
}

static HRESULT WINAPI dwritetextlayout_source_GetNumberSubstitution(IDWriteTextAnalysisSource1 *iface,
    UINT32 position, UINT32* text_len, IDWriteNumberSubstitution **substitution)
{
    FIXME("%u %p %p: stub\n", position, text_len, substitution);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextlayout_source_GetVerticalGlyphOrientation(IDWriteTextAnalysisSource1 *iface,
    UINT32 position, UINT32 *length, DWRITE_VERTICAL_GLYPH_ORIENTATION *orientation, UINT8 *bidi_level)
{
    FIXME("%u %p %p %p: stub\n", position, length, orientation, bidi_level);
    return E_NOTIMPL;
}

static const IDWriteTextAnalysisSource1Vtbl dwritetextlayoutsourcevtbl = {
    dwritetextlayout_source_QueryInterface,
    dwritetextlayout_source_AddRef,
    dwritetextlayout_source_Release,
    dwritetextlayout_source_GetTextAtPosition,
    dwritetextlayout_source_GetTextBeforePosition,
    dwritetextlayout_source_GetParagraphReadingDirection,
    dwritetextlayout_source_GetLocaleName,
    dwritetextlayout_source_GetNumberSubstitution,
    dwritetextlayout_source_GetVerticalGlyphOrientation
};

static HRESULT layout_format_from_textformat(struct dwrite_textlayout *layout, IDWriteTextFormat *format)
{
    struct dwrite_textformat *textformat;
    IDWriteTextFormat1 *format1;
    UINT32 len;
    HRESULT hr;

    if ((textformat = unsafe_impl_from_IDWriteTextFormat(format))) {
        layout->format = textformat->format;

        layout->format.locale = heap_strdupW(textformat->format.locale);
        layout->format.family_name = heap_strdupW(textformat->format.family_name);
        if (!layout->format.locale || !layout->format.family_name)
        {
            heap_free(layout->format.locale);
            heap_free(layout->format.family_name);
            return E_OUTOFMEMORY;
        }

        if (layout->format.trimmingsign)
            IDWriteInlineObject_AddRef(layout->format.trimmingsign);
        if (layout->format.collection)
            IDWriteFontCollection_AddRef(layout->format.collection);
        if (layout->format.fallback)
            IDWriteFontFallback_AddRef(layout->format.fallback);

        return S_OK;
    }

    layout->format.weight  = IDWriteTextFormat_GetFontWeight(format);
    layout->format.style   = IDWriteTextFormat_GetFontStyle(format);
    layout->format.stretch = IDWriteTextFormat_GetFontStretch(format);
    layout->format.fontsize= IDWriteTextFormat_GetFontSize(format);
    layout->format.textalignment = IDWriteTextFormat_GetTextAlignment(format);
    layout->format.paralign = IDWriteTextFormat_GetParagraphAlignment(format);
    layout->format.wrapping = IDWriteTextFormat_GetWordWrapping(format);
    layout->format.readingdir = IDWriteTextFormat_GetReadingDirection(format);
    layout->format.flow = IDWriteTextFormat_GetFlowDirection(format);
    layout->format.fallback = NULL;
    layout->format.spacing.leadingBefore = 0.0f;
    layout->format.spacing.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_DEFAULT;
    hr = IDWriteTextFormat_GetLineSpacing(format, &layout->format.spacing.method,
        &layout->format.spacing.height, &layout->format.spacing.baseline);
    if (FAILED(hr))
        return hr;

    hr = IDWriteTextFormat_GetTrimming(format, &layout->format.trimming, &layout->format.trimmingsign);
    if (FAILED(hr))
        return hr;

    /* locale name and length */
    len = IDWriteTextFormat_GetLocaleNameLength(format);
    layout->format.locale = heap_alloc((len+1)*sizeof(WCHAR));
    if (!layout->format.locale)
        return E_OUTOFMEMORY;

    hr = IDWriteTextFormat_GetLocaleName(format, layout->format.locale, len+1);
    if (FAILED(hr))
        return hr;
    layout->format.locale_len = len;

    /* font family name and length */
    len = IDWriteTextFormat_GetFontFamilyNameLength(format);
    layout->format.family_name = heap_alloc((len+1)*sizeof(WCHAR));
    if (!layout->format.family_name)
        return E_OUTOFMEMORY;

    hr = IDWriteTextFormat_GetFontFamilyName(format, layout->format.family_name, len+1);
    if (FAILED(hr))
        return hr;
    layout->format.family_len = len;

    hr = IDWriteTextFormat_QueryInterface(format, &IID_IDWriteTextFormat1, (void**)&format1);
    if (hr == S_OK) {
        IDWriteTextFormat2 *format2;

        layout->format.vertical_orientation = IDWriteTextFormat1_GetVerticalGlyphOrientation(format1);
        layout->format.optical_alignment = IDWriteTextFormat1_GetOpticalAlignment(format1);
        IDWriteTextFormat1_GetFontFallback(format1, &layout->format.fallback);

        if (IDWriteTextFormat1_QueryInterface(format1, &IID_IDWriteTextFormat2, (void**)&format2) == S_OK) {
            IDWriteTextFormat2_GetLineSpacing(format2, &layout->format.spacing);
            IDWriteTextFormat2_Release(format2);
        }

        IDWriteTextFormat1_Release(format1);
    }
    else {
        layout->format.vertical_orientation = DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT;
        layout->format.optical_alignment = DWRITE_OPTICAL_ALIGNMENT_NONE;
    }

    return IDWriteTextFormat_GetFontCollection(format, &layout->format.collection);
}

static HRESULT init_textlayout(const struct textlayout_desc *desc, struct dwrite_textlayout *layout)
{
    struct layout_range_header *range, *strike, *underline, *effect, *spacing, *typography;
    static const DWRITE_TEXT_RANGE r = { 0, ~0u };
    HRESULT hr;

    layout->IDWriteTextLayout3_iface.lpVtbl = &dwritetextlayoutvtbl;
    layout->IDWriteTextFormat1_iface.lpVtbl = &dwritetextformat1_layout_vtbl;
    layout->IDWriteTextAnalysisSink1_iface.lpVtbl = &dwritetextlayoutsinkvtbl;
    layout->IDWriteTextAnalysisSource1_iface.lpVtbl = &dwritetextlayoutsourcevtbl;
    layout->ref = 1;
    layout->len = desc->length;
    layout->recompute = RECOMPUTE_EVERYTHING;
    layout->nominal_breakpoints = NULL;
    layout->actual_breakpoints = NULL;
    layout->cluster_count = 0;
    layout->clustermetrics = NULL;
    layout->clusters = NULL;
    layout->linemetrics = NULL;
    layout->lines = NULL;
    layout->line_alloc = 0;
    layout->minwidth = 0.0f;
    list_init(&layout->eruns);
    list_init(&layout->inlineobjects);
    list_init(&layout->underlines);
    list_init(&layout->strikethrough);
    list_init(&layout->runs);
    list_init(&layout->ranges);
    list_init(&layout->strike_ranges);
    list_init(&layout->underline_ranges);
    list_init(&layout->effects);
    list_init(&layout->spacing);
    list_init(&layout->typographies);
    memset(&layout->format, 0, sizeof(layout->format));
    memset(&layout->metrics, 0, sizeof(layout->metrics));
    layout->metrics.layoutWidth = desc->max_width;
    layout->metrics.layoutHeight = desc->max_height;
    layout->measuringmode = DWRITE_MEASURING_MODE_NATURAL;

    layout->ppdip = 0.0f;
    memset(&layout->transform, 0, sizeof(layout->transform));

    layout->str = heap_strdupnW(desc->string, desc->length);
    if (desc->length && !layout->str) {
        hr = E_OUTOFMEMORY;
        goto fail;
    }

    hr = layout_format_from_textformat(layout, desc->format);
    if (FAILED(hr))
        goto fail;

    range = alloc_layout_range(layout, &r, LAYOUT_RANGE_REGULAR);
    strike = alloc_layout_range(layout, &r, LAYOUT_RANGE_STRIKETHROUGH);
    underline = alloc_layout_range(layout, &r, LAYOUT_RANGE_UNDERLINE);
    effect = alloc_layout_range(layout, &r, LAYOUT_RANGE_EFFECT);
    spacing = alloc_layout_range(layout, &r, LAYOUT_RANGE_SPACING);
    typography = alloc_layout_range(layout, &r, LAYOUT_RANGE_TYPOGRAPHY);
    if (!range || !strike || !effect || !spacing || !typography || !underline) {
        free_layout_range(range);
        free_layout_range(strike);
        free_layout_range(underline);
        free_layout_range(effect);
        free_layout_range(spacing);
        free_layout_range(typography);
        hr = E_OUTOFMEMORY;
        goto fail;
    }

    if (desc->is_gdi_compatible)
        layout->measuringmode = desc->use_gdi_natural ? DWRITE_MEASURING_MODE_GDI_NATURAL : DWRITE_MEASURING_MODE_GDI_CLASSIC;
    else
        layout->measuringmode = DWRITE_MEASURING_MODE_NATURAL;
    layout->ppdip = desc->ppdip;
    layout->transform = desc->transform ? *desc->transform : identity;

    layout->factory = desc->factory;
    IDWriteFactory4_AddRef(layout->factory);
    list_add_head(&layout->ranges, &range->entry);
    list_add_head(&layout->strike_ranges, &strike->entry);
    list_add_head(&layout->underline_ranges, &underline->entry);
    list_add_head(&layout->effects, &effect->entry);
    list_add_head(&layout->spacing, &spacing->entry);
    list_add_head(&layout->typographies, &typography->entry);
    return S_OK;

fail:
    IDWriteTextLayout3_Release(&layout->IDWriteTextLayout3_iface);
    return hr;
}

HRESULT create_textlayout(const struct textlayout_desc *desc, IDWriteTextLayout **ret)
{
    struct dwrite_textlayout *layout;
    HRESULT hr;

    *ret = NULL;

    if (!desc->format || !desc->string)
        return E_INVALIDARG;

    layout = heap_alloc(sizeof(struct dwrite_textlayout));
    if (!layout) return E_OUTOFMEMORY;

    hr = init_textlayout(desc, layout);
    if (hr == S_OK)
        *ret = (IDWriteTextLayout*)&layout->IDWriteTextLayout3_iface;

    return hr;
}

static HRESULT WINAPI dwritetrimmingsign_QueryInterface(IDWriteInlineObject *iface, REFIID riid, void **obj)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteInlineObject)) {
        *obj = iface;
        IDWriteInlineObject_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetrimmingsign_AddRef(IDWriteInlineObject *iface)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritetrimmingsign_Release(IDWriteInlineObject *iface)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        IDWriteTextLayout_Release(This->layout);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritetrimmingsign_Draw(IDWriteInlineObject *iface, void *context, IDWriteTextRenderer *renderer,
    FLOAT originX, FLOAT originY, BOOL is_sideways, BOOL is_rtl, IUnknown *effect)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);
    DWRITE_TEXT_RANGE range = { 0, ~0u };
    DWRITE_TEXT_METRICS metrics;
    DWRITE_LINE_METRICS line;
    UINT32 line_count;
    HRESULT hr;

    TRACE("(%p)->(%p %p %.2f %.2f %d %d %p)\n", This, context, renderer, originX, originY, is_sideways, is_rtl, effect);

    IDWriteTextLayout_SetDrawingEffect(This->layout, effect, range);
    IDWriteTextLayout_GetLineMetrics(This->layout, &line, 1, &line_count);
    IDWriteTextLayout_GetMetrics(This->layout, &metrics);
    hr = IDWriteTextLayout_Draw(This->layout, context, renderer, originX, originY - line.baseline);
    IDWriteTextLayout_SetDrawingEffect(This->layout, NULL, range);
    return hr;
}

static HRESULT WINAPI dwritetrimmingsign_GetMetrics(IDWriteInlineObject *iface, DWRITE_INLINE_OBJECT_METRICS *ret)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);
    DWRITE_TEXT_METRICS metrics;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, ret);

    hr = IDWriteTextLayout_GetMetrics(This->layout, &metrics);
    if (FAILED(hr)) {
        memset(ret, 0, sizeof(*ret));
        return hr;
    }

    ret->width = metrics.width;
    ret->height = 0.0f;
    ret->baseline = 0.0f;
    ret->supportsSideways = FALSE;
    return S_OK;
}

static HRESULT WINAPI dwritetrimmingsign_GetOverhangMetrics(IDWriteInlineObject *iface, DWRITE_OVERHANG_METRICS *overhangs)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);
    TRACE("(%p)->(%p)\n", This, overhangs);
    return IDWriteTextLayout_GetOverhangMetrics(This->layout, overhangs);
}

static HRESULT WINAPI dwritetrimmingsign_GetBreakConditions(IDWriteInlineObject *iface, DWRITE_BREAK_CONDITION *before,
        DWRITE_BREAK_CONDITION *after)
{
    struct dwrite_trimmingsign *This = impl_from_IDWriteInlineObject(iface);

    TRACE("(%p)->(%p %p)\n", This, before, after);

    *before = *after = DWRITE_BREAK_CONDITION_NEUTRAL;
    return S_OK;
}

static const IDWriteInlineObjectVtbl dwritetrimmingsignvtbl = {
    dwritetrimmingsign_QueryInterface,
    dwritetrimmingsign_AddRef,
    dwritetrimmingsign_Release,
    dwritetrimmingsign_Draw,
    dwritetrimmingsign_GetMetrics,
    dwritetrimmingsign_GetOverhangMetrics,
    dwritetrimmingsign_GetBreakConditions
};

static inline BOOL is_reading_direction_horz(DWRITE_READING_DIRECTION direction)
{
    return (direction == DWRITE_READING_DIRECTION_LEFT_TO_RIGHT) ||
           (direction == DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
}

static inline BOOL is_reading_direction_vert(DWRITE_READING_DIRECTION direction)
{
    return (direction == DWRITE_READING_DIRECTION_TOP_TO_BOTTOM) ||
           (direction == DWRITE_READING_DIRECTION_BOTTOM_TO_TOP);
}

static inline BOOL is_flow_direction_horz(DWRITE_FLOW_DIRECTION direction)
{
    return (direction == DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT) ||
           (direction == DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT);
}

static inline BOOL is_flow_direction_vert(DWRITE_FLOW_DIRECTION direction)
{
    return (direction == DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM) ||
           (direction == DWRITE_FLOW_DIRECTION_BOTTOM_TO_TOP);
}

HRESULT create_trimmingsign(IDWriteFactory4 *factory, IDWriteTextFormat *format, IDWriteInlineObject **sign)
{
    static const WCHAR ellipsisW = 0x2026;
    struct dwrite_trimmingsign *This;
    DWRITE_READING_DIRECTION reading;
    DWRITE_FLOW_DIRECTION flow;
    HRESULT hr;

    *sign = NULL;

    /* Validate reading/flow direction here, layout creation won't complain about
       invalid combinations. */
    reading = IDWriteTextFormat_GetReadingDirection(format);
    flow = IDWriteTextFormat_GetFlowDirection(format);

    if ((is_reading_direction_horz(reading) && is_flow_direction_horz(flow)) ||
        (is_reading_direction_vert(reading) && is_flow_direction_vert(flow)))
        return DWRITE_E_FLOWDIRECTIONCONFLICTS;

    This = heap_alloc(sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->IDWriteInlineObject_iface.lpVtbl = &dwritetrimmingsignvtbl;
    This->ref = 1;

    hr = IDWriteFactory4_CreateTextLayout(factory, &ellipsisW, 1, format, 0.0f, 0.0f, &This->layout);
    if (FAILED(hr)) {
        heap_free(This);
        return hr;
    }

    IDWriteTextLayout_SetWordWrapping(This->layout, DWRITE_WORD_WRAPPING_NO_WRAP);
    IDWriteTextLayout_SetParagraphAlignment(This->layout, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    *sign = &This->IDWriteInlineObject_iface;

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_QueryInterface(IDWriteTextFormat2 *iface, REFIID riid, void **obj)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteTextFormat2) ||
        IsEqualIID(riid, &IID_IDWriteTextFormat1) ||
        IsEqualIID(riid, &IID_IDWriteTextFormat)  ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteTextFormat2_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetextformat_AddRef(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritetextformat_Release(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        release_format_data(&This->format);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritetextformat_SetTextAlignment(IDWriteTextFormat2 *iface, DWRITE_TEXT_ALIGNMENT alignment)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, alignment);
    return format_set_textalignment(&This->format, alignment, NULL);
}

static HRESULT WINAPI dwritetextformat_SetParagraphAlignment(IDWriteTextFormat2 *iface, DWRITE_PARAGRAPH_ALIGNMENT alignment)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, alignment);
    return format_set_paralignment(&This->format, alignment, NULL);
}

static HRESULT WINAPI dwritetextformat_SetWordWrapping(IDWriteTextFormat2 *iface, DWRITE_WORD_WRAPPING wrapping)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, wrapping);
    return format_set_wordwrapping(&This->format, wrapping, NULL);
}

static HRESULT WINAPI dwritetextformat_SetReadingDirection(IDWriteTextFormat2 *iface, DWRITE_READING_DIRECTION direction)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, direction);
    return format_set_readingdirection(&This->format, direction, NULL);
}

static HRESULT WINAPI dwritetextformat_SetFlowDirection(IDWriteTextFormat2 *iface, DWRITE_FLOW_DIRECTION direction)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, direction);
    return format_set_flowdirection(&This->format, direction, NULL);
}

static HRESULT WINAPI dwritetextformat_SetIncrementalTabStop(IDWriteTextFormat2 *iface, FLOAT tabstop)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    FIXME("(%p)->(%f): stub\n", This, tabstop);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritetextformat_SetTrimming(IDWriteTextFormat2 *iface, DWRITE_TRIMMING const *trimming,
    IDWriteInlineObject *trimming_sign)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p %p)\n", This, trimming, trimming_sign);
    return format_set_trimming(&This->format, trimming, trimming_sign, NULL);
}

static HRESULT WINAPI dwritetextformat_SetLineSpacing(IDWriteTextFormat2 *iface, DWRITE_LINE_SPACING_METHOD method,
    FLOAT height, FLOAT baseline)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    DWRITE_LINE_SPACING spacing;

    TRACE("(%p)->(%d %f %f)\n", This, method, height, baseline);

    spacing = This->format.spacing;
    spacing.method = method;
    spacing.height = height;
    spacing.baseline = baseline;

    return format_set_linespacing(&This->format, &spacing, NULL);
}

static DWRITE_TEXT_ALIGNMENT WINAPI dwritetextformat_GetTextAlignment(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.textalignment;
}

static DWRITE_PARAGRAPH_ALIGNMENT WINAPI dwritetextformat_GetParagraphAlignment(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.paralign;
}

static DWRITE_WORD_WRAPPING WINAPI dwritetextformat_GetWordWrapping(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.wrapping;
}

static DWRITE_READING_DIRECTION WINAPI dwritetextformat_GetReadingDirection(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.readingdir;
}

static DWRITE_FLOW_DIRECTION WINAPI dwritetextformat_GetFlowDirection(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.flow;
}

static FLOAT WINAPI dwritetextformat_GetIncrementalTabStop(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    FIXME("(%p): stub\n", This);
    return 0.0f;
}

static HRESULT WINAPI dwritetextformat_GetTrimming(IDWriteTextFormat2 *iface, DWRITE_TRIMMING *options,
    IDWriteInlineObject **trimming_sign)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p %p)\n", This, options, trimming_sign);

    *options = This->format.trimming;
    if ((*trimming_sign = This->format.trimmingsign))
        IDWriteInlineObject_AddRef(*trimming_sign);

    return S_OK;
}

static HRESULT WINAPI dwritetextformat_GetLineSpacing(IDWriteTextFormat2 *iface, DWRITE_LINE_SPACING_METHOD *method,
    FLOAT *spacing, FLOAT *baseline)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p %p %p)\n", This, method, spacing, baseline);

    *method = This->format.spacing.method;
    *spacing = This->format.spacing.height;
    *baseline = This->format.spacing.baseline;
    return S_OK;
}

static HRESULT WINAPI dwritetextformat_GetFontCollection(IDWriteTextFormat2 *iface, IDWriteFontCollection **collection)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%p)\n", This, collection);

    *collection = This->format.collection;
    IDWriteFontCollection_AddRef(*collection);

    return S_OK;
}

static UINT32 WINAPI dwritetextformat_GetFontFamilyNameLength(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.family_len;
}

static HRESULT WINAPI dwritetextformat_GetFontFamilyName(IDWriteTextFormat2 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%p %u)\n", This, name, size);

    if (size <= This->format.family_len) return E_NOT_SUFFICIENT_BUFFER;
    strcpyW(name, This->format.family_name);
    return S_OK;
}

static DWRITE_FONT_WEIGHT WINAPI dwritetextformat_GetFontWeight(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.weight;
}

static DWRITE_FONT_STYLE WINAPI dwritetextformat_GetFontStyle(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.style;
}

static DWRITE_FONT_STRETCH WINAPI dwritetextformat_GetFontStretch(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.stretch;
}

static FLOAT WINAPI dwritetextformat_GetFontSize(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.fontsize;
}

static UINT32 WINAPI dwritetextformat_GetLocaleNameLength(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.locale_len;
}

static HRESULT WINAPI dwritetextformat_GetLocaleName(IDWriteTextFormat2 *iface, WCHAR *name, UINT32 size)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%p %u)\n", This, name, size);

    if (size <= This->format.locale_len) return E_NOT_SUFFICIENT_BUFFER;
    strcpyW(name, This->format.locale);
    return S_OK;
}

static HRESULT WINAPI dwritetextformat1_SetVerticalGlyphOrientation(IDWriteTextFormat2 *iface, DWRITE_VERTICAL_GLYPH_ORIENTATION orientation)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%d)\n", This, orientation);

    if ((UINT32)orientation > DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED)
        return E_INVALIDARG;

    This->format.vertical_orientation = orientation;
    return S_OK;
}

static DWRITE_VERTICAL_GLYPH_ORIENTATION WINAPI dwritetextformat1_GetVerticalGlyphOrientation(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.vertical_orientation;
}

static HRESULT WINAPI dwritetextformat1_SetLastLineWrapping(IDWriteTextFormat2 *iface, BOOL lastline_wrapping_enabled)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%d)\n", This, lastline_wrapping_enabled);

    This->format.last_line_wrapping = !!lastline_wrapping_enabled;
    return S_OK;
}

static BOOL WINAPI dwritetextformat1_GetLastLineWrapping(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.last_line_wrapping;
}

static HRESULT WINAPI dwritetextformat1_SetOpticalAlignment(IDWriteTextFormat2 *iface, DWRITE_OPTICAL_ALIGNMENT alignment)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%d)\n", This, alignment);
    return format_set_optical_alignment(&This->format, alignment);
}

static DWRITE_OPTICAL_ALIGNMENT WINAPI dwritetextformat1_GetOpticalAlignment(IDWriteTextFormat2 *iface)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)\n", This);
    return This->format.optical_alignment;
}

static HRESULT WINAPI dwritetextformat1_SetFontFallback(IDWriteTextFormat2 *iface, IDWriteFontFallback *fallback)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return set_fontfallback_for_format(&This->format, fallback);
}

static HRESULT WINAPI dwritetextformat1_GetFontFallback(IDWriteTextFormat2 *iface, IDWriteFontFallback **fallback)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p)\n", This, fallback);
    return get_fontfallback_from_format(&This->format, fallback);
}

static HRESULT WINAPI dwritetextformat2_SetLineSpacing(IDWriteTextFormat2 *iface, DWRITE_LINE_SPACING const *spacing)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);
    TRACE("(%p)->(%p)\n", This, spacing);
    return format_set_linespacing(&This->format, spacing, NULL);
}

static HRESULT WINAPI dwritetextformat2_GetLineSpacing(IDWriteTextFormat2 *iface, DWRITE_LINE_SPACING *spacing)
{
    struct dwrite_textformat *This = impl_from_IDWriteTextFormat2(iface);

    TRACE("(%p)->(%p)\n", This, spacing);

    *spacing = This->format.spacing;
    return S_OK;
}

static const IDWriteTextFormat2Vtbl dwritetextformatvtbl = {
    dwritetextformat_QueryInterface,
    dwritetextformat_AddRef,
    dwritetextformat_Release,
    dwritetextformat_SetTextAlignment,
    dwritetextformat_SetParagraphAlignment,
    dwritetextformat_SetWordWrapping,
    dwritetextformat_SetReadingDirection,
    dwritetextformat_SetFlowDirection,
    dwritetextformat_SetIncrementalTabStop,
    dwritetextformat_SetTrimming,
    dwritetextformat_SetLineSpacing,
    dwritetextformat_GetTextAlignment,
    dwritetextformat_GetParagraphAlignment,
    dwritetextformat_GetWordWrapping,
    dwritetextformat_GetReadingDirection,
    dwritetextformat_GetFlowDirection,
    dwritetextformat_GetIncrementalTabStop,
    dwritetextformat_GetTrimming,
    dwritetextformat_GetLineSpacing,
    dwritetextformat_GetFontCollection,
    dwritetextformat_GetFontFamilyNameLength,
    dwritetextformat_GetFontFamilyName,
    dwritetextformat_GetFontWeight,
    dwritetextformat_GetFontStyle,
    dwritetextformat_GetFontStretch,
    dwritetextformat_GetFontSize,
    dwritetextformat_GetLocaleNameLength,
    dwritetextformat_GetLocaleName,
    dwritetextformat1_SetVerticalGlyphOrientation,
    dwritetextformat1_GetVerticalGlyphOrientation,
    dwritetextformat1_SetLastLineWrapping,
    dwritetextformat1_GetLastLineWrapping,
    dwritetextformat1_SetOpticalAlignment,
    dwritetextformat1_GetOpticalAlignment,
    dwritetextformat1_SetFontFallback,
    dwritetextformat1_GetFontFallback,
    dwritetextformat2_SetLineSpacing,
    dwritetextformat2_GetLineSpacing
};

static struct dwrite_textformat *unsafe_impl_from_IDWriteTextFormat(IDWriteTextFormat *iface)
{
    return (iface->lpVtbl == (IDWriteTextFormatVtbl*)&dwritetextformatvtbl) ?
        CONTAINING_RECORD((IDWriteTextFormat2 *)iface, struct dwrite_textformat, IDWriteTextFormat2_iface) : NULL;
}

HRESULT create_textformat(const WCHAR *family_name, IDWriteFontCollection *collection, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
    DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale, IDWriteTextFormat **format)
{
    struct dwrite_textformat *This;

    *format = NULL;

    if (size <= 0.0f)
        return E_INVALIDARG;

    if (((UINT32)weight > DWRITE_FONT_WEIGHT_ULTRA_BLACK) ||
        ((UINT32)stretch > DWRITE_FONT_STRETCH_ULTRA_EXPANDED) ||
        ((UINT32)style > DWRITE_FONT_STYLE_ITALIC))
        return E_INVALIDARG;

    This = heap_alloc(sizeof(struct dwrite_textformat));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteTextFormat2_iface.lpVtbl = &dwritetextformatvtbl;
    This->ref = 1;
    This->format.family_name = heap_strdupW(family_name);
    This->format.family_len = strlenW(family_name);
    This->format.locale = heap_strdupW(locale);
    This->format.locale_len = strlenW(locale);
    /* force locale name to lower case, layout will inherit this modified value */
    strlwrW(This->format.locale);
    This->format.weight = weight;
    This->format.style = style;
    This->format.fontsize = size;
    This->format.stretch = stretch;
    This->format.textalignment = DWRITE_TEXT_ALIGNMENT_LEADING;
    This->format.optical_alignment = DWRITE_OPTICAL_ALIGNMENT_NONE;
    This->format.paralign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR;
    This->format.wrapping = DWRITE_WORD_WRAPPING_WRAP;
    This->format.last_line_wrapping = TRUE;
    This->format.readingdir = DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    This->format.flow = DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM;
    This->format.spacing.method = DWRITE_LINE_SPACING_METHOD_DEFAULT;
    This->format.spacing.height = 0.0f;
    This->format.spacing.baseline = 0.0f;
    This->format.spacing.leadingBefore = 0.0f;
    This->format.spacing.fontLineGapUsage = DWRITE_FONT_LINE_GAP_USAGE_DEFAULT;
    This->format.vertical_orientation = DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT;
    This->format.trimming.granularity = DWRITE_TRIMMING_GRANULARITY_NONE;
    This->format.trimming.delimiter = 0;
    This->format.trimming.delimiterCount = 0;
    This->format.trimmingsign = NULL;
    This->format.collection = collection;
    This->format.fallback = NULL;
    IDWriteFontCollection_AddRef(collection);

    *format = (IDWriteTextFormat*)&This->IDWriteTextFormat2_iface;

    return S_OK;
}

static HRESULT WINAPI dwritetypography_QueryInterface(IDWriteTypography *iface, REFIID riid, void **obj)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);

    TRACE("(%p)->(%s %p)\n", typography, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteTypography) || IsEqualIID(riid, &IID_IUnknown)) {
        *obj = iface;
        IDWriteTypography_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG WINAPI dwritetypography_AddRef(IDWriteTypography *iface)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);
    ULONG ref = InterlockedIncrement(&typography->ref);
    TRACE("(%p)->(%d)\n", typography, ref);
    return ref;
}

static ULONG WINAPI dwritetypography_Release(IDWriteTypography *iface)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);
    ULONG ref = InterlockedDecrement(&typography->ref);

    TRACE("(%p)->(%d)\n", typography, ref);

    if (!ref) {
        heap_free(typography->features);
        heap_free(typography);
    }

    return ref;
}

static HRESULT WINAPI dwritetypography_AddFontFeature(IDWriteTypography *iface, DWRITE_FONT_FEATURE feature)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);

    TRACE("(%p)->(%x %u)\n", typography, feature.nameTag, feature.parameter);

    if (typography->count == typography->allocated) {
        DWRITE_FONT_FEATURE *ptr = heap_realloc(typography->features, 2*typography->allocated*sizeof(DWRITE_FONT_FEATURE));
        if (!ptr)
            return E_OUTOFMEMORY;

        typography->features = ptr;
        typography->allocated *= 2;
    }

    typography->features[typography->count++] = feature;
    return S_OK;
}

static UINT32 WINAPI dwritetypography_GetFontFeatureCount(IDWriteTypography *iface)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);
    TRACE("(%p)\n", typography);
    return typography->count;
}

static HRESULT WINAPI dwritetypography_GetFontFeature(IDWriteTypography *iface, UINT32 index, DWRITE_FONT_FEATURE *feature)
{
    struct dwrite_typography *typography = impl_from_IDWriteTypography(iface);

    TRACE("(%p)->(%u %p)\n", typography, index, feature);

    if (index >= typography->count)
        return E_INVALIDARG;

    *feature = typography->features[index];
    return S_OK;
}

static const IDWriteTypographyVtbl dwritetypographyvtbl = {
    dwritetypography_QueryInterface,
    dwritetypography_AddRef,
    dwritetypography_Release,
    dwritetypography_AddFontFeature,
    dwritetypography_GetFontFeatureCount,
    dwritetypography_GetFontFeature
};

HRESULT create_typography(IDWriteTypography **ret)
{
    struct dwrite_typography *typography;

    *ret = NULL;

    typography = heap_alloc(sizeof(*typography));
    if (!typography)
        return E_OUTOFMEMORY;

    typography->IDWriteTypography_iface.lpVtbl = &dwritetypographyvtbl;
    typography->ref = 1;
    typography->allocated = 2;
    typography->count = 0;

    typography->features = heap_alloc(typography->allocated*sizeof(DWRITE_FONT_FEATURE));
    if (!typography->features) {
        heap_free(typography);
        return E_OUTOFMEMORY;
    }

    *ret = &typography->IDWriteTypography_iface;
    return S_OK;
}
