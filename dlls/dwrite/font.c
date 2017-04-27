/*
 *    Font and collections
 *
 * Copyright 2011 Huw Davies
 * Copyright 2012, 2014-2016 Nikolay Sivov for CodeWeavers
 * Copyright 2014 Aric Stewart for CodeWeavers
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

#include <assert.h>
#include <math.h>

#define COBJMACROS

#include "dwrite_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwrite);
WINE_DECLARE_DEBUG_CHANNEL(dwrite_file);

#define MS_HEAD_TAG DWRITE_MAKE_OPENTYPE_TAG('h','e','a','d')
#define MS_OS2_TAG  DWRITE_MAKE_OPENTYPE_TAG('O','S','/','2')
#define MS_CMAP_TAG DWRITE_MAKE_OPENTYPE_TAG('c','m','a','p')
#define MS_NAME_TAG DWRITE_MAKE_OPENTYPE_TAG('n','a','m','e')
#define MS_VDMX_TAG DWRITE_MAKE_OPENTYPE_TAG('V','D','M','X')
#define MS_GASP_TAG DWRITE_MAKE_OPENTYPE_TAG('g','a','s','p')
#define MS_CPAL_TAG DWRITE_MAKE_OPENTYPE_TAG('C','P','A','L')
#define MS_COLR_TAG DWRITE_MAKE_OPENTYPE_TAG('C','O','L','R')

static const IID IID_issystemcollection = {0x14d88047,0x331f,0x4cd3,{0xbc,0xa8,0x3e,0x67,0x99,0xaf,0x34,0x75}};

static const FLOAT RECOMMENDED_OUTLINE_AA_THRESHOLD = 100.0f;
static const FLOAT RECOMMENDED_OUTLINE_A_THRESHOLD = 350.0f;
static const FLOAT RECOMMENDED_NATURAL_PPEM = 20.0f;

static const WCHAR extraW[] = {'e','x','t','r','a',0};
static const WCHAR ultraW[] = {'u','l','t','r','a',0};
static const WCHAR semiW[] = {'s','e','m','i',0};
static const WCHAR extW[] = {'e','x','t',0};
static const WCHAR thinW[] = {'t','h','i','n',0};
static const WCHAR lightW[] = {'l','i','g','h','t',0};
static const WCHAR mediumW[] = {'m','e','d','i','u','m',0};
static const WCHAR blackW[] = {'b','l','a','c','k',0};
static const WCHAR condensedW[] = {'c','o','n','d','e','n','s','e','d',0};
static const WCHAR expandedW[] = {'e','x','p','a','n','d','e','d',0};
static const WCHAR italicW[] = {'i','t','a','l','i','c',0};
static const WCHAR boldW[] = {'B','o','l','d',0};
static const WCHAR obliqueW[] = {'O','b','l','i','q','u','e',0};
static const WCHAR regularW[] = {'R','e','g','u','l','a','r',0};
static const WCHAR demiW[] = {'d','e','m','i',0};
static const WCHAR spaceW[] = {' ',0};
static const WCHAR enusW[] = {'e','n','-','u','s',0};

struct dwrite_font_propvec {
    FLOAT stretch;
    FLOAT style;
    FLOAT weight;
};

struct dwrite_font_data {
    LONG ref;

    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_PANOSE panose;
    struct dwrite_font_propvec propvec;

    DWRITE_FONT_METRICS1 metrics;
    IDWriteLocalizedStrings *info_strings[DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_CID_NAME+1];
    IDWriteLocalizedStrings *names;

    /* data needed to create fontface instance */
    IDWriteFactory4 *factory;
    DWRITE_FONT_FACE_TYPE face_type;
    IDWriteFontFile *file;
    UINT32 face_index;

    WCHAR *facename;

    USHORT simulations;

    LOGFONTW lf;

    /* used to mark font as tested when scanning for simulation candidate */
    BOOL bold_sim_tested : 1;
    BOOL oblique_sim_tested : 1;
};

struct dwrite_fontlist {
    IDWriteFontList1 IDWriteFontList1_iface;
    LONG ref;

    IDWriteFontFamily1 *family;
    struct dwrite_font_data **fonts;
    UINT32 font_count;
};

struct dwrite_fontfamily_data {
    LONG ref;

    IDWriteLocalizedStrings *familyname;

    struct dwrite_font_data **fonts;
    UINT32 font_count;
    UINT32 font_alloc;
    BOOL has_normal_face : 1;
    BOOL has_oblique_face : 1;
    BOOL has_italic_face : 1;
};

struct dwrite_fontcollection {
    IDWriteFontCollection1 IDWriteFontCollection1_iface;
    LONG ref;

    struct dwrite_fontfamily_data **family_data;
    UINT32 family_count;
    UINT32 family_alloc;
};

struct dwrite_fontfamily {
    IDWriteFontFamily1 IDWriteFontFamily1_iface;
    LONG ref;

    struct dwrite_fontfamily_data *data;

    IDWriteFontCollection1 *collection;
};

struct dwrite_font {
    IDWriteFont3 IDWriteFont3_iface;
    LONG ref;

    IDWriteFontFamily1 *family;

    DWRITE_FONT_STYLE style;
    struct dwrite_font_data *data;
};

struct dwrite_fonttable {
    void  *data;
    void  *context;
    UINT32 size;
    BOOL   exists;
};

enum runanalysis_flags {
    RUNANALYSIS_BOUNDS_READY  = 1 << 0,
    RUNANALYSIS_BITMAP_READY  = 1 << 1,
    RUNANALYSIS_USE_TRANSFORM = 1 << 2
};

struct dwrite_glyphrunanalysis {
    IDWriteGlyphRunAnalysis IDWriteGlyphRunAnalysis_iface;
    LONG ref;

    DWRITE_RENDERING_MODE1 rendering_mode;
    DWRITE_GLYPH_RUN run; /* glyphAdvances and glyphOffsets are not used */
    DWRITE_MATRIX m;
    FLOAT ppdip;
    UINT16 *glyphs;
    D2D_POINT_2F origin;
    D2D_POINT_2F *advances;
    D2D_POINT_2F *advanceoffsets;
    D2D_POINT_2F *ascenderoffsets;

    UINT8 flags;
    RECT bounds;
    BYTE *bitmap;
};

struct dwrite_colorglyphenum {
    IDWriteColorGlyphRunEnumerator IDWriteColorGlyphRunEnumerator_iface;
    LONG ref;

    FLOAT origin_x;                   /* original run origin */
    FLOAT origin_y;

    IDWriteFontFace4 *fontface;       /* for convenience */
    DWRITE_COLOR_GLYPH_RUN colorrun;  /* returned with GetCurrentRun() */
    DWRITE_GLYPH_RUN run;             /* base run */
    UINT32 palette;                   /* palette index to get layer color from */
    FLOAT *advances;                  /* original or measured advances for base glyphs */
    FLOAT *color_advances;            /* returned color run points to this */
    DWRITE_GLYPH_OFFSET *offsets;     /* original offsets, or NULL */
    DWRITE_GLYPH_OFFSET *color_offsets; /* returned color run offsets, or NULL */
    UINT16 *glyphindices;             /* returned color run points to this */
    struct dwrite_colorglyph *glyphs; /* current glyph color info */
    BOOL has_regular_glyphs;          /* TRUE if there's any glyph without a color */
    UINT16 current_layer;             /* enumerator position, updated with MoveNext */
    UINT16 max_layer_num;             /* max number of layers for this run */
    struct dwrite_fonttable colr;     /* used to access layers */
};

#define GLYPH_BLOCK_SHIFT 8
#define GLYPH_BLOCK_SIZE  (1UL << GLYPH_BLOCK_SHIFT)
#define GLYPH_BLOCK_MASK  (GLYPH_BLOCK_SIZE - 1)
#define GLYPH_MAX         65536

enum fontface_flags {
    FONTFACE_IS_SYMBOL             = 1 << 0,
    FONTFACE_IS_MONOSPACED         = 1 << 1,
    FONTFACE_HAS_KERN_PAIRS        = 1 << 2,
    FONTFACE_HAS_VERTICAL_VARIANTS = 1 << 3
};

struct dwrite_fontface {
    IDWriteFontFace4 IDWriteFontFace4_iface;
    LONG ref;

    IDWriteFontFileStream **streams;
    IDWriteFontFile **files;
    UINT32 file_count;
    UINT32 index;

    USHORT simulations;
    DWRITE_FONT_FACE_TYPE type;
    DWRITE_FONT_METRICS1 metrics;
    DWRITE_CARET_METRICS caret;
    INT charmap;
    UINT16 flags;

    struct dwrite_fonttable cmap;
    struct dwrite_fonttable vdmx;
    struct dwrite_fonttable gasp;
    struct dwrite_fonttable cpal;
    struct dwrite_fonttable colr;
    DWRITE_GLYPH_METRICS *glyphs[GLYPH_MAX/GLYPH_BLOCK_SIZE];

    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_PANOSE panose;

    LOGFONTW lf;
};

struct dwrite_fontfile {
    IDWriteFontFile IDWriteFontFile_iface;
    LONG ref;

    IDWriteFontFileLoader *loader;
    void *reference_key;
    UINT32 key_size;
    IDWriteFontFileStream *stream;
};

struct dwrite_fontfacereference {
    IDWriteFontFaceReference IDWriteFontFaceReference_iface;
    LONG ref;

    IDWriteFontFile *file;
    UINT32 index;
    USHORT simulations;
    IDWriteFactory4 *factory;
};

static inline struct dwrite_fontface *impl_from_IDWriteFontFace4(IDWriteFontFace4 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontface, IDWriteFontFace4_iface);
}

static inline struct dwrite_font *impl_from_IDWriteFont3(IDWriteFont3 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_font, IDWriteFont3_iface);
}

static inline struct dwrite_fontfile *impl_from_IDWriteFontFile(IDWriteFontFile *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontfile, IDWriteFontFile_iface);
}

static inline struct dwrite_fontfamily *impl_from_IDWriteFontFamily1(IDWriteFontFamily1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontfamily, IDWriteFontFamily1_iface);
}

static inline struct dwrite_fontcollection *impl_from_IDWriteFontCollection1(IDWriteFontCollection1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontcollection, IDWriteFontCollection1_iface);
}

static inline struct dwrite_glyphrunanalysis *impl_from_IDWriteGlyphRunAnalysis(IDWriteGlyphRunAnalysis *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_glyphrunanalysis, IDWriteGlyphRunAnalysis_iface);
}

static inline struct dwrite_colorglyphenum *impl_from_IDWriteColorGlyphRunEnumerator(IDWriteColorGlyphRunEnumerator *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_colorglyphenum, IDWriteColorGlyphRunEnumerator_iface);
}

static inline struct dwrite_fontlist *impl_from_IDWriteFontList1(IDWriteFontList1 *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontlist, IDWriteFontList1_iface);
}

static inline struct dwrite_fontfacereference *impl_from_IDWriteFontFaceReference(IDWriteFontFaceReference *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_fontfacereference, IDWriteFontFaceReference_iface);
}

static inline const char *debugstr_tag(UINT32 tag)
{
    return debugstr_an((char*)&tag, 4);
}

static HRESULT get_cached_glyph_metrics(struct dwrite_fontface *fontface, UINT16 glyph, DWRITE_GLYPH_METRICS *metrics)
{
    static const DWRITE_GLYPH_METRICS nil;
    DWRITE_GLYPH_METRICS *block = fontface->glyphs[glyph >> GLYPH_BLOCK_SHIFT];

    if (!block || !memcmp(&block[glyph & GLYPH_BLOCK_MASK], &nil, sizeof(DWRITE_GLYPH_METRICS))) return S_FALSE;
    memcpy(metrics, &block[glyph & GLYPH_BLOCK_MASK], sizeof(*metrics));
    return S_OK;
}

static HRESULT set_cached_glyph_metrics(struct dwrite_fontface *fontface, UINT16 glyph, DWRITE_GLYPH_METRICS *metrics)
{
    DWRITE_GLYPH_METRICS **block = &fontface->glyphs[glyph >> GLYPH_BLOCK_SHIFT];

    if (!*block) {
        /* start new block */
        *block = heap_alloc_zero(sizeof(*metrics) * GLYPH_BLOCK_SIZE);
        if (!*block)
            return E_OUTOFMEMORY;
    }

    memcpy(&(*block)[glyph & GLYPH_BLOCK_MASK], metrics, sizeof(*metrics));
    return S_OK;
}

static void* get_fontface_table(IDWriteFontFace4 *fontface, UINT32 tag, struct dwrite_fonttable *table)
{
    HRESULT hr;

    if (table->data || !table->exists)
        return table->data;

    table->exists = FALSE;
    hr = IDWriteFontFace4_TryGetFontTable(fontface, tag, (const void**)&table->data, &table->size, &table->context,
        &table->exists);
    if (FAILED(hr) || !table->exists) {
        WARN("Font does not have a %s table\n", debugstr_tag(tag));
        return NULL;
    }

    return table->data;
}

static void init_font_prop_vec(DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,
    struct dwrite_font_propvec *vec)
{
    vec->stretch = ((INT32)stretch - DWRITE_FONT_STRETCH_NORMAL) * 11.0f;
    vec->style = style * 7.0f;
    vec->weight = ((INT32)weight - DWRITE_FONT_WEIGHT_NORMAL) / 100.0f * 5.0f;
}

static FLOAT get_font_prop_vec_distance(const struct dwrite_font_propvec *left, const struct dwrite_font_propvec *right)
{
    return powf(left->stretch - right->stretch, 2) + powf(left->style - right->style, 2) + powf(left->weight - right->weight, 2);
}

static FLOAT get_font_prop_vec_dotproduct(const struct dwrite_font_propvec *left, const struct dwrite_font_propvec *right)
{
    return left->stretch * right->stretch + left->style * right->style + left->weight * right->weight;
}

static inline void* get_fontface_cmap(struct dwrite_fontface *fontface)
{
    return get_fontface_table(&fontface->IDWriteFontFace4_iface, MS_CMAP_TAG, &fontface->cmap);
}

static inline void* get_fontface_vdmx(struct dwrite_fontface *fontface)
{
    return get_fontface_table(&fontface->IDWriteFontFace4_iface, MS_VDMX_TAG, &fontface->vdmx);
}

static inline void* get_fontface_gasp(struct dwrite_fontface *fontface, UINT32 *size)
{
    void *ptr = get_fontface_table(&fontface->IDWriteFontFace4_iface, MS_GASP_TAG, &fontface->gasp);
    *size = fontface->gasp.size;
    return ptr;
}

static inline void* get_fontface_cpal(struct dwrite_fontface *fontface)
{
    return get_fontface_table(&fontface->IDWriteFontFace4_iface, MS_CPAL_TAG, &fontface->cpal);
}

static inline void* get_fontface_colr(struct dwrite_fontface *fontface)
{
    return get_fontface_table(&fontface->IDWriteFontFace4_iface, MS_COLR_TAG, &fontface->colr);
}

static void release_font_data(struct dwrite_font_data *data)
{
    int i;

    if (InterlockedDecrement(&data->ref) > 0)
        return;

    for (i = DWRITE_INFORMATIONAL_STRING_NONE; i < sizeof(data->info_strings)/sizeof(data->info_strings[0]); i++) {
        if (data->info_strings[i])
            IDWriteLocalizedStrings_Release(data->info_strings[i]);
    }
    if (data->names)
        IDWriteLocalizedStrings_Release(data->names);

    IDWriteFontFile_Release(data->file);
    IDWriteFactory4_Release(data->factory);
    heap_free(data->facename);
    heap_free(data);
}

static void release_fontfamily_data(struct dwrite_fontfamily_data *data)
{
    int i;

    if (InterlockedDecrement(&data->ref) > 0)
        return;

    for (i = 0; i < data->font_count; i++)
        release_font_data(data->fonts[i]);
    heap_free(data->fonts);
    IDWriteLocalizedStrings_Release(data->familyname);
    heap_free(data);
}

static HRESULT WINAPI dwritefontface_QueryInterface(IDWriteFontFace4 *iface, REFIID riid, void **obj)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontFace4) ||
        IsEqualIID(riid, &IID_IDWriteFontFace3) ||
        IsEqualIID(riid, &IID_IDWriteFontFace2) ||
        IsEqualIID(riid, &IID_IDWriteFontFace1) ||
        IsEqualIID(riid, &IID_IDWriteFontFace) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFontFace4_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontface_AddRef(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontface_Release(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        UINT32 i;

        if (This->cmap.context)
            IDWriteFontFace4_ReleaseFontTable(iface, This->cmap.context);
        if (This->vdmx.context)
            IDWriteFontFace4_ReleaseFontTable(iface, This->vdmx.context);
        if (This->gasp.context)
            IDWriteFontFace4_ReleaseFontTable(iface, This->gasp.context);
        if (This->cpal.context)
            IDWriteFontFace4_ReleaseFontTable(iface, This->cpal.context);
        if (This->colr.context)
            IDWriteFontFace4_ReleaseFontTable(iface, This->colr.context);
        for (i = 0; i < This->file_count; i++) {
            if (This->streams[i])
                IDWriteFontFileStream_Release(This->streams[i]);
            if (This->files[i])
                IDWriteFontFile_Release(This->files[i]);
        }
        heap_free(This->streams);
        heap_free(This->files);

        for (i = 0; i < sizeof(This->glyphs)/sizeof(This->glyphs[0]); i++)
            heap_free(This->glyphs[i]);

        freetype_notify_cacheremove(iface);
        heap_free(This);
    }

    return ref;
}

static DWRITE_FONT_FACE_TYPE WINAPI dwritefontface_GetType(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->type;
}

static HRESULT WINAPI dwritefontface_GetFiles(IDWriteFontFace4 *iface, UINT32 *number_of_files,
    IDWriteFontFile **fontfiles)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    int i;

    TRACE("(%p)->(%p %p)\n", This, number_of_files, fontfiles);
    if (fontfiles == NULL)
    {
        *number_of_files = This->file_count;
        return S_OK;
    }
    if (*number_of_files < This->file_count)
        return E_INVALIDARG;

    for (i = 0; i < This->file_count; i++)
    {
        IDWriteFontFile_AddRef(This->files[i]);
        fontfiles[i] = This->files[i];
    }

    return S_OK;
}

static UINT32 WINAPI dwritefontface_GetIndex(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->index;
}

static DWRITE_FONT_SIMULATIONS WINAPI dwritefontface_GetSimulations(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->simulations;
}

static BOOL WINAPI dwritefontface_IsSymbolFont(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return !!(This->flags & FONTFACE_IS_SYMBOL);
}

static void WINAPI dwritefontface_GetMetrics(IDWriteFontFace4 *iface, DWRITE_FONT_METRICS *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)->(%p)\n", This, metrics);
    memcpy(metrics, &This->metrics, sizeof(*metrics));
}

static UINT16 WINAPI dwritefontface_GetGlyphCount(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return freetype_get_glyphcount(iface);
}

static HRESULT WINAPI dwritefontface_GetDesignGlyphMetrics(IDWriteFontFace4 *iface,
    UINT16 const *glyphs, UINT32 glyph_count, DWRITE_GLYPH_METRICS *ret, BOOL is_sideways)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    HRESULT hr;
    UINT32 i;

    TRACE("(%p)->(%p %u %p %d)\n", This, glyphs, glyph_count, ret, is_sideways);

    if (!glyphs)
        return E_INVALIDARG;

    if (is_sideways)
        FIXME("sideways metrics are not supported.\n");

    for (i = 0; i < glyph_count; i++) {
        DWRITE_GLYPH_METRICS metrics;

        hr = get_cached_glyph_metrics(This, glyphs[i], &metrics);
        if (hr != S_OK) {
            freetype_get_design_glyph_metrics(iface, This->metrics.designUnitsPerEm, glyphs[i], &metrics);
            hr = set_cached_glyph_metrics(This, glyphs[i], &metrics);
            if (FAILED(hr))
                return hr;
        }
        ret[i] = metrics;
    }

    return S_OK;
}

static HRESULT WINAPI dwritefontface_GetGlyphIndices(IDWriteFontFace4 *iface, UINT32 const *codepoints,
    UINT32 count, UINT16 *glyph_indices)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);

    TRACE("(%p)->(%p %u %p)\n", This, codepoints, count, glyph_indices);

    if (!glyph_indices)
        return E_INVALIDARG;

    if (!codepoints) {
        memset(glyph_indices, 0, count*sizeof(UINT16));
        return E_INVALIDARG;
    }

    freetype_get_glyphs(iface, This->charmap, codepoints, count, glyph_indices);
    return S_OK;
}

static HRESULT WINAPI dwritefontface_TryGetFontTable(IDWriteFontFace4 *iface, UINT32 table_tag,
    const void **table_data, UINT32 *table_size, void **context, BOOL *exists)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    struct file_stream_desc stream_desc;

    TRACE("(%p)->(%s %p %p %p %p)\n", This, debugstr_tag(table_tag), table_data, table_size, context, exists);

    stream_desc.stream = This->streams[0];
    stream_desc.face_type = This->type;
    stream_desc.face_index = This->index;
    return opentype_get_font_table(&stream_desc, table_tag, table_data, context, table_size, exists);
}

static void WINAPI dwritefontface_ReleaseFontTable(IDWriteFontFace4 *iface, void *table_context)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);

    TRACE("(%p)->(%p)\n", This, table_context);

    IDWriteFontFileStream_ReleaseFileFragment(This->streams[0], table_context);
}

static HRESULT WINAPI dwritefontface_GetGlyphRunOutline(IDWriteFontFace4 *iface, FLOAT emSize,
    UINT16 const *glyphs, FLOAT const* advances, DWRITE_GLYPH_OFFSET const *offsets,
    UINT32 count, BOOL is_sideways, BOOL is_rtl, IDWriteGeometrySink *sink)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);

    TRACE("(%p)->(%.2f %p %p %p %u %d %d %p)\n", This, emSize, glyphs, advances, offsets,
        count, is_sideways, is_rtl, sink);

    if (!glyphs || !sink)
        return E_INVALIDARG;

    if (is_sideways)
        FIXME("sideways mode is not supported.\n");

    return freetype_get_glyphrun_outline(iface, emSize, glyphs, advances, offsets, count, is_rtl, sink);
}

static DWRITE_RENDERING_MODE fontface_renderingmode_from_measuringmode(DWRITE_MEASURING_MODE measuring,
    FLOAT ppem, WORD gasp)
{
    DWRITE_RENDERING_MODE mode = DWRITE_RENDERING_MODE_DEFAULT;

    switch (measuring)
    {
    case DWRITE_MEASURING_MODE_NATURAL:
    {
        if (!(gasp & GASP_SYMMETRIC_SMOOTHING) && (ppem <= RECOMMENDED_NATURAL_PPEM))
            mode = DWRITE_RENDERING_MODE_NATURAL;
        else
            mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
        break;
    }
    case DWRITE_MEASURING_MODE_GDI_CLASSIC:
        mode = DWRITE_RENDERING_MODE_GDI_CLASSIC;
        break;
    case DWRITE_MEASURING_MODE_GDI_NATURAL:
        mode = DWRITE_RENDERING_MODE_GDI_NATURAL;
        break;
    default:
        ;
    }

    return mode;
}

static HRESULT WINAPI dwritefontface_GetRecommendedRenderingMode(IDWriteFontFace4 *iface, FLOAT emSize,
    FLOAT ppdip, DWRITE_MEASURING_MODE measuring, IDWriteRenderingParams *params, DWRITE_RENDERING_MODE *mode)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    WORD gasp, *ptr;
    UINT32 size;
    FLOAT ppem;

    TRACE("(%p)->(%.2f %.2f %d %p %p)\n", This, emSize, ppdip, measuring, params, mode);

    if (!params) {
        *mode = DWRITE_RENDERING_MODE_DEFAULT;
        return E_INVALIDARG;
    }

    *mode = IDWriteRenderingParams_GetRenderingMode(params);
    if (*mode != DWRITE_RENDERING_MODE_DEFAULT)
        return S_OK;

    ppem = emSize * ppdip;

    if (ppem >= RECOMMENDED_OUTLINE_AA_THRESHOLD) {
        *mode = DWRITE_RENDERING_MODE_OUTLINE;
        return S_OK;
    }

    ptr = get_fontface_gasp(This, &size);
    gasp = opentype_get_gasp_flags(ptr, size, ppem);
    *mode = fontface_renderingmode_from_measuringmode(measuring, ppem, gasp);
    return S_OK;
}

static HRESULT WINAPI dwritefontface_GetGdiCompatibleMetrics(IDWriteFontFace4 *iface, FLOAT emSize, FLOAT pixels_per_dip,
    DWRITE_MATRIX const *transform, DWRITE_FONT_METRICS *metrics)
{
    DWRITE_FONT_METRICS1 metrics1;
    HRESULT hr = IDWriteFontFace4_GetGdiCompatibleMetrics(iface, emSize, pixels_per_dip, transform, &metrics1);
    memcpy(metrics, &metrics1, sizeof(*metrics));
    return hr;
}

static inline int round_metric(FLOAT metric)
{
    return (int)floorf(metric + 0.5f);
}

static HRESULT WINAPI dwritefontface_GetGdiCompatibleGlyphMetrics(IDWriteFontFace4 *iface, FLOAT emSize, FLOAT ppdip,
    DWRITE_MATRIX const *m, BOOL use_gdi_natural, UINT16 const *glyphs, UINT32 glyph_count,
    DWRITE_GLYPH_METRICS *metrics, BOOL is_sideways)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    DWRITE_MEASURING_MODE mode;
    FLOAT scale, size;
    HRESULT hr;
    UINT32 i;

    TRACE("(%p)->(%.2f %.2f %p %d %p %u %p %d)\n", This, emSize, ppdip, m, use_gdi_natural, glyphs,
        glyph_count, metrics, is_sideways);

    if (m && memcmp(m, &identity, sizeof(*m)))
        FIXME("transform is not supported, %s\n", debugstr_matrix(m));

    size = emSize * ppdip;
    scale = size / This->metrics.designUnitsPerEm;
    mode = use_gdi_natural ? DWRITE_MEASURING_MODE_GDI_NATURAL : DWRITE_MEASURING_MODE_GDI_CLASSIC;

    for (i = 0; i < glyph_count; i++) {
        DWRITE_GLYPH_METRICS *ret = metrics + i;
        DWRITE_GLYPH_METRICS design;

        hr = IDWriteFontFace4_GetDesignGlyphMetrics(iface, glyphs + i, 1, &design, is_sideways);
        if (FAILED(hr))
            return hr;

        ret->advanceWidth = freetype_get_glyph_advance(iface, size, glyphs[i], mode);
        ret->advanceWidth = round_metric(ret->advanceWidth * This->metrics.designUnitsPerEm / size);

#define SCALE_METRIC(x) ret->x = round_metric(round_metric((design.x) * scale) / scale)
        SCALE_METRIC(leftSideBearing);
        SCALE_METRIC(rightSideBearing);
        SCALE_METRIC(topSideBearing);
        SCALE_METRIC(advanceHeight);
        SCALE_METRIC(bottomSideBearing);
        SCALE_METRIC(verticalOriginY);
#undef  SCALE_METRIC
    }

    return S_OK;
}

static void WINAPI dwritefontface1_GetMetrics(IDWriteFontFace4 *iface, DWRITE_FONT_METRICS1 *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)->(%p)\n", This, metrics);
    *metrics = This->metrics;
}

static HRESULT WINAPI dwritefontface1_GetGdiCompatibleMetrics(IDWriteFontFace4 *iface, FLOAT em_size, FLOAT pixels_per_dip,
    const DWRITE_MATRIX *m, DWRITE_FONT_METRICS1 *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    const DWRITE_FONT_METRICS1 *design = &This->metrics;
    UINT16 ascent, descent;
    FLOAT scale;

    TRACE("(%p)->(%.2f %.2f %p %p)\n", This, em_size, pixels_per_dip, m, metrics);

    if (em_size <= 0.0f || pixels_per_dip <= 0.0f) {
        memset(metrics, 0, sizeof(*metrics));
        return E_INVALIDARG;
    }

    em_size *= pixels_per_dip;
    if (m && m->m22 != 0.0f)
        em_size *= fabs(m->m22);

    scale = em_size / design->designUnitsPerEm;
    if (!opentype_get_vdmx_size(get_fontface_vdmx(This), em_size, &ascent, &descent)) {
        ascent = round_metric(design->ascent * scale);
        descent = round_metric(design->descent * scale);
    }

#define SCALE_METRIC(x) metrics->x = round_metric(round_metric((design->x) * scale) / scale)
    metrics->designUnitsPerEm = design->designUnitsPerEm;
    metrics->ascent = round_metric(ascent / scale);
    metrics->descent = round_metric(descent / scale);

    SCALE_METRIC(lineGap);
    SCALE_METRIC(capHeight);
    SCALE_METRIC(xHeight);
    SCALE_METRIC(underlinePosition);
    SCALE_METRIC(underlineThickness);
    SCALE_METRIC(strikethroughPosition);
    SCALE_METRIC(strikethroughThickness);
    SCALE_METRIC(glyphBoxLeft);
    SCALE_METRIC(glyphBoxTop);
    SCALE_METRIC(glyphBoxRight);
    SCALE_METRIC(glyphBoxBottom);
    SCALE_METRIC(subscriptPositionX);
    SCALE_METRIC(subscriptPositionY);
    SCALE_METRIC(subscriptSizeX);
    SCALE_METRIC(subscriptSizeY);
    SCALE_METRIC(superscriptPositionX);
    SCALE_METRIC(superscriptPositionY);
    SCALE_METRIC(superscriptSizeX);
    SCALE_METRIC(superscriptSizeY);

    metrics->hasTypographicMetrics = design->hasTypographicMetrics;
#undef SCALE_METRIC

    return S_OK;
}

static void WINAPI dwritefontface1_GetCaretMetrics(IDWriteFontFace4 *iface, DWRITE_CARET_METRICS *metrics)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)->(%p)\n", This, metrics);
    *metrics = This->caret;
}

static HRESULT WINAPI dwritefontface1_GetUnicodeRanges(IDWriteFontFace4 *iface, UINT32 max_count,
    DWRITE_UNICODE_RANGE *ranges, UINT32 *count)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);

    TRACE("(%p)->(%u %p %p)\n", This, max_count, ranges, count);

    *count = 0;
    if (max_count && !ranges)
        return E_INVALIDARG;

    return opentype_cmap_get_unicode_ranges(get_fontface_cmap(This), max_count, ranges, count);
}

static BOOL WINAPI dwritefontface1_IsMonospacedFont(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return !!(This->flags & FONTFACE_IS_MONOSPACED);
}

static HRESULT WINAPI dwritefontface1_GetDesignGlyphAdvances(IDWriteFontFace4 *iface,
    UINT32 glyph_count, UINT16 const *glyphs, INT32 *advances, BOOL is_sideways)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    UINT32 i;

    TRACE("(%p)->(%u %p %p %d)\n", This, glyph_count, glyphs, advances, is_sideways);

    if (is_sideways)
        FIXME("sideways mode not supported\n");

    for (i = 0; i < glyph_count; i++)
        advances[i] = freetype_get_glyph_advance(iface, This->metrics.designUnitsPerEm, glyphs[i], DWRITE_MEASURING_MODE_NATURAL);

    return S_OK;
}

static HRESULT WINAPI dwritefontface1_GetGdiCompatibleGlyphAdvances(IDWriteFontFace4 *iface,
    FLOAT em_size, FLOAT ppdip, const DWRITE_MATRIX *m, BOOL use_gdi_natural,
    BOOL is_sideways, UINT32 glyph_count, UINT16 const *glyphs, INT32 *advances)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    DWRITE_MEASURING_MODE mode;
    UINT32 i;

    TRACE("(%p)->(%.2f %.2f %p %d %d %u %p %p)\n", This, em_size, ppdip, m,
        use_gdi_natural, is_sideways, glyph_count, glyphs, advances);

    if (em_size < 0.0f || ppdip <= 0.0f) {
        memset(advances, 0, sizeof(*advances) * glyph_count);
        return E_INVALIDARG;
    }

    em_size *= ppdip;
    if (em_size == 0.0f) {
        memset(advances, 0, sizeof(*advances) * glyph_count);
        return S_OK;
    }

    if (m && memcmp(m, &identity, sizeof(*m)))
        FIXME("transform is not supported, %s\n", debugstr_matrix(m));

    mode = use_gdi_natural ? DWRITE_MEASURING_MODE_GDI_NATURAL : DWRITE_MEASURING_MODE_GDI_CLASSIC;
    for (i = 0; i < glyph_count; i++) {
        advances[i] = freetype_get_glyph_advance(iface, em_size, glyphs[i], mode);
        advances[i] = round_metric(advances[i] * This->metrics.designUnitsPerEm / em_size);
    }

    return S_OK;
}

static HRESULT WINAPI dwritefontface1_GetKerningPairAdjustments(IDWriteFontFace4 *iface, UINT32 count,
    const UINT16 *indices, INT32 *adjustments)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    UINT32 i;

    TRACE("(%p)->(%u %p %p)\n", This, count, indices, adjustments);

    if (!(indices || adjustments) || !count)
        return E_INVALIDARG;

    if (!indices || count == 1) {
        memset(adjustments, 0, count*sizeof(INT32));
        return E_INVALIDARG;
    }

    if (This->flags & FONTFACE_HAS_KERN_PAIRS) {
        memset(adjustments, 0, count*sizeof(INT32));
        return S_OK;
    }

    for (i = 0; i < count-1; i++)
        adjustments[i] = freetype_get_kerning_pair_adjustment(iface, indices[i], indices[i+1]);
    adjustments[count-1] = 0;

    return S_OK;
}

static BOOL WINAPI dwritefontface1_HasKerningPairs(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return !!(This->flags & FONTFACE_HAS_KERN_PAIRS);
}

static HRESULT WINAPI dwritefontface1_GetRecommendedRenderingMode(IDWriteFontFace4 *iface,
    FLOAT font_emsize, FLOAT dpiX, FLOAT dpiY, const DWRITE_MATRIX *transform, BOOL is_sideways,
    DWRITE_OUTLINE_THRESHOLD threshold, DWRITE_MEASURING_MODE measuring_mode, DWRITE_RENDERING_MODE *rendering_mode)
{
    DWRITE_GRID_FIT_MODE gridfitmode;
    return IDWriteFontFace2_GetRecommendedRenderingMode((IDWriteFontFace2*)iface, font_emsize, dpiX, dpiY, transform, is_sideways,
        threshold, measuring_mode, NULL, rendering_mode, &gridfitmode);
}

static HRESULT WINAPI dwritefontface1_GetVerticalGlyphVariants(IDWriteFontFace4 *iface, UINT32 glyph_count,
    const UINT16 *nominal_indices, UINT16 *vertical_indices)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%u %p %p): stub\n", This, glyph_count, nominal_indices, vertical_indices);
    return E_NOTIMPL;
}

static BOOL WINAPI dwritefontface1_HasVerticalGlyphVariants(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return !!(This->flags & FONTFACE_HAS_VERTICAL_VARIANTS);
}

static BOOL WINAPI dwritefontface2_IsColorFont(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return get_fontface_cpal(This) && get_fontface_colr(This);
}

static UINT32 WINAPI dwritefontface2_GetColorPaletteCount(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return opentype_get_cpal_palettecount(get_fontface_cpal(This));
}

static UINT32 WINAPI dwritefontface2_GetPaletteEntryCount(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return opentype_get_cpal_paletteentrycount(get_fontface_cpal(This));
}

static HRESULT WINAPI dwritefontface2_GetPaletteEntries(IDWriteFontFace4 *iface, UINT32 palette_index,
    UINT32 first_entry_index, UINT32 entry_count, DWRITE_COLOR_F *entries)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)->(%u %u %u %p)\n", This, palette_index, first_entry_index, entry_count, entries);
    return opentype_get_cpal_entries(get_fontface_cpal(This), palette_index, first_entry_index, entry_count, entries);
}

static HRESULT WINAPI dwritefontface2_GetRecommendedRenderingMode(IDWriteFontFace4 *iface, FLOAT emSize,
    FLOAT dpiX, FLOAT dpiY, DWRITE_MATRIX const *m, BOOL is_sideways, DWRITE_OUTLINE_THRESHOLD threshold,
    DWRITE_MEASURING_MODE measuringmode, IDWriteRenderingParams *params, DWRITE_RENDERING_MODE *renderingmode,
    DWRITE_GRID_FIT_MODE *gridfitmode)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FLOAT emthreshold;
    WORD gasp, *ptr;
    UINT32 size;

    TRACE("(%p)->(%.2f %.2f %.2f %p %d %d %d %p %p %p)\n", This, emSize, dpiX, dpiY, m, is_sideways, threshold,
        measuringmode, params, renderingmode, gridfitmode);

    if (m)
        FIXME("transform not supported %s\n", debugstr_matrix(m));

    if (is_sideways)
        FIXME("sideways mode not supported\n");

    emSize *= max(dpiX, dpiY) / 96.0f;

    *renderingmode = DWRITE_RENDERING_MODE_DEFAULT;
    *gridfitmode = DWRITE_GRID_FIT_MODE_DEFAULT;
    if (params) {
        IDWriteRenderingParams2 *params2;
        HRESULT hr;

        hr = IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams2, (void**)&params2);
        if (hr == S_OK) {
            *renderingmode = IDWriteRenderingParams2_GetRenderingMode(params2);
            *gridfitmode = IDWriteRenderingParams2_GetGridFitMode(params2);
            IDWriteRenderingParams2_Release(params2);
        }
        else
            *renderingmode = IDWriteRenderingParams_GetRenderingMode(params);
    }

    emthreshold = threshold == DWRITE_OUTLINE_THRESHOLD_ANTIALIASED ? RECOMMENDED_OUTLINE_AA_THRESHOLD : RECOMMENDED_OUTLINE_A_THRESHOLD;

    ptr = get_fontface_gasp(This, &size);
    gasp = opentype_get_gasp_flags(ptr, size, emSize);

    if (*renderingmode == DWRITE_RENDERING_MODE_DEFAULT) {
        if (emSize >= emthreshold)
            *renderingmode = DWRITE_RENDERING_MODE_OUTLINE;
        else
            *renderingmode = fontface_renderingmode_from_measuringmode(measuringmode, emSize, gasp);
    }

    if (*gridfitmode == DWRITE_GRID_FIT_MODE_DEFAULT) {
        if (emSize >= emthreshold)
            *gridfitmode = DWRITE_GRID_FIT_MODE_DISABLED;
        else if (measuringmode == DWRITE_MEASURING_MODE_GDI_CLASSIC || measuringmode == DWRITE_MEASURING_MODE_GDI_NATURAL)
            *gridfitmode = DWRITE_GRID_FIT_MODE_ENABLED;
        else
            *gridfitmode = (gasp & (GASP_GRIDFIT|GASP_SYMMETRIC_GRIDFIT)) ? DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED;
    }

    return S_OK;
}

static HRESULT WINAPI dwritefontface3_GetFontFaceReference(IDWriteFontFace4 *iface, IDWriteFontFaceReference **ref)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%p): stub\n", This, ref);
    return E_NOTIMPL;
}

static void WINAPI dwritefontface3_GetPanose(IDWriteFontFace4 *iface, DWRITE_PANOSE *panose)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)->(%p)\n", This, panose);
    *panose = This->panose;
}

static DWRITE_FONT_WEIGHT WINAPI dwritefontface3_GetWeight(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->weight;
}

static DWRITE_FONT_STRETCH WINAPI dwritefontface3_GetStretch(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->stretch;
}

static DWRITE_FONT_STYLE WINAPI dwritefontface3_GetStyle(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    TRACE("(%p)\n", This);
    return This->style;
}

static HRESULT WINAPI dwritefontface3_GetFamilyNames(IDWriteFontFace4 *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%p): stub\n", This, names);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface3_GetFaceNames(IDWriteFontFace4 *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%p): stub\n", This, names);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface3_GetInformationalStrings(IDWriteFontFace4 *iface, DWRITE_INFORMATIONAL_STRING_ID stringid,
    IDWriteLocalizedStrings **strings, BOOL *exists)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%u %p %p): stub\n", This, stringid, strings, exists);
    return E_NOTIMPL;
}

static BOOL WINAPI dwritefontface3_HasCharacter(IDWriteFontFace4 *iface, UINT32 ch)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    UINT16 index;
    HRESULT hr;

    TRACE("(%p)->(0x%08x)\n", This, ch);

    index = 0;
    hr = IDWriteFontFace4_GetGlyphIndices(iface, &ch, 1, &index);
    if (FAILED(hr))
        return FALSE;

    return index != 0;
}

static HRESULT WINAPI dwritefontface3_GetRecommendedRenderingMode(IDWriteFontFace4 *iface, FLOAT emSize, FLOAT dpiX, FLOAT dpiY,
    DWRITE_MATRIX const *m, BOOL is_sideways, DWRITE_OUTLINE_THRESHOLD threshold, DWRITE_MEASURING_MODE measuring_mode,
    IDWriteRenderingParams *params, DWRITE_RENDERING_MODE1 *rendering_mode, DWRITE_GRID_FIT_MODE *gridfit_mode)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FLOAT emthreshold;
    WORD gasp, *ptr;
    UINT32 size;

    TRACE("(%p)->(%.2f %.2f %.2f %p %d %d %d %p %p %p)\n", This, emSize, dpiX, dpiY, m, is_sideways, threshold,
        measuring_mode, params, rendering_mode, gridfit_mode);

    if (m)
        FIXME("transform not supported %s\n", debugstr_matrix(m));

    if (is_sideways)
        FIXME("sideways mode not supported\n");

    emSize *= max(dpiX, dpiY) / 96.0f;

    *rendering_mode = DWRITE_RENDERING_MODE1_DEFAULT;
    *gridfit_mode = DWRITE_GRID_FIT_MODE_DEFAULT;
    if (params) {
        IDWriteRenderingParams3 *params3;
        HRESULT hr;

        hr = IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams3, (void**)&params3);
        if (hr == S_OK) {
            *rendering_mode = IDWriteRenderingParams3_GetRenderingMode1(params3);
            *gridfit_mode = IDWriteRenderingParams3_GetGridFitMode(params3);
            IDWriteRenderingParams3_Release(params3);
        }
        else
            *rendering_mode = IDWriteRenderingParams_GetRenderingMode(params);
    }

    emthreshold = threshold == DWRITE_OUTLINE_THRESHOLD_ANTIALIASED ? RECOMMENDED_OUTLINE_AA_THRESHOLD : RECOMMENDED_OUTLINE_A_THRESHOLD;

    ptr = get_fontface_gasp(This, &size);
    gasp = opentype_get_gasp_flags(ptr, size, emSize);

    if (*rendering_mode == DWRITE_RENDERING_MODE1_DEFAULT) {
        if (emSize >= emthreshold)
            *rendering_mode = DWRITE_RENDERING_MODE1_OUTLINE;
        else
            *rendering_mode = fontface_renderingmode_from_measuringmode(measuring_mode, emSize, gasp);
    }

    if (*gridfit_mode == DWRITE_GRID_FIT_MODE_DEFAULT) {
        if (emSize >= emthreshold)
            *gridfit_mode = DWRITE_GRID_FIT_MODE_DISABLED;
        else if (measuring_mode == DWRITE_MEASURING_MODE_GDI_CLASSIC || measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL)
            *gridfit_mode = DWRITE_GRID_FIT_MODE_ENABLED;
        else
            *gridfit_mode = (gasp & (GASP_GRIDFIT|GASP_SYMMETRIC_GRIDFIT)) ? DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED;
    }

    return S_OK;
}

static BOOL WINAPI dwritefontface3_IsCharacterLocal(IDWriteFontFace4 *iface, UINT32 ch)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(0x%x): stub\n", This, ch);
    return FALSE;
}

static BOOL WINAPI dwritefontface3_IsGlyphLocal(IDWriteFontFace4 *iface, UINT16 glyph)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%u): stub\n", This, glyph);
    return FALSE;
}

static HRESULT WINAPI dwritefontface3_AreCharactersLocal(IDWriteFontFace4 *iface, WCHAR const *text,
    UINT32 count, BOOL enqueue_if_not, BOOL *are_local)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%s:%u %d %p): stub\n", This, debugstr_wn(text, count), count, enqueue_if_not, are_local);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface3_AreGlyphsLocal(IDWriteFontFace4 *iface, UINT16 const *glyphs,
    UINT32 count, BOOL enqueue_if_not, BOOL *are_local)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%p %u %d %p): stub\n", This, glyphs, count, enqueue_if_not, are_local);
    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontface4_GetGlyphImageFormats_(IDWriteFontFace4 *iface, UINT16 glyph,
    UINT32 ppem_first, UINT32 ppem_last, DWRITE_GLYPH_IMAGE_FORMATS *formats)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%u %u %u %p): stub\n", This, glyph, ppem_first, ppem_last, formats);
    return E_NOTIMPL;
}

static DWRITE_GLYPH_IMAGE_FORMATS WINAPI dwritefontface4_GetGlyphImageFormats(IDWriteFontFace4 *iface)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p): stub\n", This);
    return DWRITE_GLYPH_IMAGE_FORMATS_NONE;
}

static HRESULT WINAPI dwritefontface4_GetGlyphImageData(IDWriteFontFace4 *iface, UINT16 glyph,
    UINT32 ppem, DWRITE_GLYPH_IMAGE_FORMATS format, DWRITE_GLYPH_IMAGE_DATA *data, void **context)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%u %u %d %p %p): stub\n", This, glyph, ppem, format, data, context);
    return E_NOTIMPL;
}

static void WINAPI dwritefontface4_ReleaseGlyphImageData(IDWriteFontFace4 *iface, void *context)
{
    struct dwrite_fontface *This = impl_from_IDWriteFontFace4(iface);
    FIXME("(%p)->(%p): stub\n", This, context);
}

static const IDWriteFontFace4Vtbl dwritefontfacevtbl = {
    dwritefontface_QueryInterface,
    dwritefontface_AddRef,
    dwritefontface_Release,
    dwritefontface_GetType,
    dwritefontface_GetFiles,
    dwritefontface_GetIndex,
    dwritefontface_GetSimulations,
    dwritefontface_IsSymbolFont,
    dwritefontface_GetMetrics,
    dwritefontface_GetGlyphCount,
    dwritefontface_GetDesignGlyphMetrics,
    dwritefontface_GetGlyphIndices,
    dwritefontface_TryGetFontTable,
    dwritefontface_ReleaseFontTable,
    dwritefontface_GetGlyphRunOutline,
    dwritefontface_GetRecommendedRenderingMode,
    dwritefontface_GetGdiCompatibleMetrics,
    dwritefontface_GetGdiCompatibleGlyphMetrics,
    dwritefontface1_GetMetrics,
    dwritefontface1_GetGdiCompatibleMetrics,
    dwritefontface1_GetCaretMetrics,
    dwritefontface1_GetUnicodeRanges,
    dwritefontface1_IsMonospacedFont,
    dwritefontface1_GetDesignGlyphAdvances,
    dwritefontface1_GetGdiCompatibleGlyphAdvances,
    dwritefontface1_GetKerningPairAdjustments,
    dwritefontface1_HasKerningPairs,
    dwritefontface1_GetRecommendedRenderingMode,
    dwritefontface1_GetVerticalGlyphVariants,
    dwritefontface1_HasVerticalGlyphVariants,
    dwritefontface2_IsColorFont,
    dwritefontface2_GetColorPaletteCount,
    dwritefontface2_GetPaletteEntryCount,
    dwritefontface2_GetPaletteEntries,
    dwritefontface2_GetRecommendedRenderingMode,
    dwritefontface3_GetFontFaceReference,
    dwritefontface3_GetPanose,
    dwritefontface3_GetWeight,
    dwritefontface3_GetStretch,
    dwritefontface3_GetStyle,
    dwritefontface3_GetFamilyNames,
    dwritefontface3_GetFaceNames,
    dwritefontface3_GetInformationalStrings,
    dwritefontface3_HasCharacter,
    dwritefontface3_GetRecommendedRenderingMode,
    dwritefontface3_IsCharacterLocal,
    dwritefontface3_IsGlyphLocal,
    dwritefontface3_AreCharactersLocal,
    dwritefontface3_AreGlyphsLocal,
    dwritefontface4_GetGlyphImageFormats_,
    dwritefontface4_GetGlyphImageFormats,
    dwritefontface4_GetGlyphImageData,
    dwritefontface4_ReleaseGlyphImageData
};

static HRESULT get_fontface_from_font(struct dwrite_font *font, IDWriteFontFace4 **fontface)
{
    struct dwrite_font_data *data = font->data;
    struct fontface_desc desc;
    struct list *cached_list;
    HRESULT hr;

    *fontface = NULL;

    hr = factory_get_cached_fontface(data->factory, &data->file, data->face_index,
        font->data->simulations, (IDWriteFontFace**)fontface, &cached_list);
    if (hr != S_FALSE)
        return hr;

    desc.factory = data->factory;
    desc.face_type = data->face_type;
    desc.files = &data->file;
    desc.files_number = 1;
    desc.index = data->face_index;
    desc.simulations = data->simulations;
    desc.font_data = data;
    hr = create_fontface(&desc, fontface);
    if (FAILED(hr))
        return hr;

    factory_cache_fontface(cached_list, *fontface);

    return S_OK;
}

static HRESULT WINAPI dwritefont_QueryInterface(IDWriteFont3 *iface, REFIID riid, void **obj)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFont3) ||
        IsEqualIID(riid, &IID_IDWriteFont2) ||
        IsEqualIID(riid, &IID_IDWriteFont1) ||
        IsEqualIID(riid, &IID_IDWriteFont)  ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFont3_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefont_AddRef(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefont_Release(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        IDWriteFontFamily1_Release(This->family);
        release_font_data(This->data);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefont_GetFontFamily(IDWriteFont3 *iface, IDWriteFontFamily **family)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)->(%p)\n", This, family);

    *family = (IDWriteFontFamily*)This->family;
    IDWriteFontFamily_AddRef(*family);
    return S_OK;
}

static DWRITE_FONT_WEIGHT WINAPI dwritefont_GetWeight(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)\n", This);
    return This->data->weight;
}

static DWRITE_FONT_STRETCH WINAPI dwritefont_GetStretch(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)\n", This);
    return This->data->stretch;
}

static DWRITE_FONT_STYLE WINAPI dwritefont_GetStyle(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)\n", This);
    return This->style;
}

static BOOL WINAPI dwritefont_IsSymbolFont(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    IDWriteFontFace4 *fontface;
    HRESULT hr;

    TRACE("(%p)\n", This);

    hr = get_fontface_from_font(This, &fontface);
    if (FAILED(hr))
        return FALSE;

    return IDWriteFontFace4_IsSymbolFont(fontface);
}

static HRESULT WINAPI dwritefont_GetFaceNames(IDWriteFont3 *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)->(%p)\n", This, names);
    return clone_localizedstring(This->data->names, names);
}

static HRESULT WINAPI dwritefont_GetInformationalStrings(IDWriteFont3 *iface,
    DWRITE_INFORMATIONAL_STRING_ID stringid, IDWriteLocalizedStrings **strings, BOOL *exists)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    struct dwrite_font_data *data = This->data;
    HRESULT hr;

    TRACE("(%p)->(%d %p %p)\n", This, stringid, strings, exists);

    *exists = FALSE;
    *strings = NULL;

    if (stringid > DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_CID_NAME || stringid == DWRITE_INFORMATIONAL_STRING_NONE)
        return S_OK;

    if (!data->info_strings[stringid]) {
        IDWriteFontFace4 *fontface;
        const void *table_data;
        BOOL table_exists;
        void *context;
        UINT32 size;

        hr = get_fontface_from_font(This, &fontface);
        if (FAILED(hr))
            return hr;

        table_exists = FALSE;
        hr = IDWriteFontFace4_TryGetFontTable(fontface, MS_NAME_TAG, &table_data, &size, &context, &table_exists);
        if (FAILED(hr) || !table_exists)
            WARN("no NAME table found.\n");

        if (table_exists) {
            hr = opentype_get_font_info_strings(table_data, stringid, &data->info_strings[stringid]);
            if (FAILED(hr) || !data->info_strings[stringid])
                return hr;
            IDWriteFontFace4_ReleaseFontTable(fontface, context);
        }
    }

    hr = clone_localizedstring(data->info_strings[stringid], strings);
    if (FAILED(hr))
        return hr;

    *exists = TRUE;
    return S_OK;
}

static DWRITE_FONT_SIMULATIONS WINAPI dwritefont_GetSimulations(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)\n", This);
    return This->data->simulations;
}

static void WINAPI dwritefont_GetMetrics(IDWriteFont3 *iface, DWRITE_FONT_METRICS *metrics)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);

    TRACE("(%p)->(%p)\n", This, metrics);
    memcpy(metrics, &This->data->metrics, sizeof(*metrics));
}

static HRESULT WINAPI dwritefont_HasCharacter(IDWriteFont3 *iface, UINT32 value, BOOL *exists)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    IDWriteFontFace4 *fontface;
    UINT16 index;
    HRESULT hr;

    TRACE("(%p)->(0x%08x %p)\n", This, value, exists);

    *exists = FALSE;

    hr = get_fontface_from_font(This, &fontface);
    if (FAILED(hr))
        return hr;

    index = 0;
    hr = IDWriteFontFace4_GetGlyphIndices(fontface, &value, 1, &index);
    if (FAILED(hr))
        return hr;

    *exists = index != 0;
    return S_OK;
}

static HRESULT WINAPI dwritefont_CreateFontFace(IDWriteFont3 *iface, IDWriteFontFace **fontface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)->(%p)\n", This, fontface);
    return IDWriteFont3_CreateFontFace(iface, (IDWriteFontFace3**)fontface);
}

static void WINAPI dwritefont1_GetMetrics(IDWriteFont3 *iface, DWRITE_FONT_METRICS1 *metrics)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)->(%p)\n", This, metrics);
    *metrics = This->data->metrics;
}

static void WINAPI dwritefont1_GetPanose(IDWriteFont3 *iface, DWRITE_PANOSE *panose)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    TRACE("(%p)->(%p)\n", This, panose);
    *panose = This->data->panose;
}

static HRESULT WINAPI dwritefont1_GetUnicodeRanges(IDWriteFont3 *iface, UINT32 max_count, DWRITE_UNICODE_RANGE *ranges, UINT32 *count)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    IDWriteFontFace4 *fontface;
    HRESULT hr;

    TRACE("(%p)->(%u %p %p)\n", This, max_count, ranges, count);

    hr = get_fontface_from_font(This, &fontface);
    if (FAILED(hr))
        return hr;

    return IDWriteFontFace4_GetUnicodeRanges(fontface, max_count, ranges, count);
}

static BOOL WINAPI dwritefont1_IsMonospacedFont(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    IDWriteFontFace4 *fontface;
    HRESULT hr;

    TRACE("(%p)\n", This);

    hr = get_fontface_from_font(This, &fontface);
    if (FAILED(hr))
        return FALSE;

    return IDWriteFontFace4_IsMonospacedFont(fontface);
}

static BOOL WINAPI dwritefont2_IsColorFont(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    IDWriteFontFace4 *fontface;
    HRESULT hr;

    TRACE("(%p)\n", This);

    hr = get_fontface_from_font(This, &fontface);
    if (FAILED(hr))
        return FALSE;

    return IDWriteFontFace4_IsColorFont(fontface);
}

static HRESULT WINAPI dwritefont3_CreateFontFace(IDWriteFont3 *iface, IDWriteFontFace3 **fontface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, fontface);

    hr = get_fontface_from_font(This, (IDWriteFontFace4**)fontface);
    if (hr == S_OK)
        IDWriteFontFace3_AddRef(*fontface);

    return hr;
}

static BOOL WINAPI dwritefont3_Equals(IDWriteFont3 *iface, IDWriteFont *font)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    FIXME("(%p)->(%p): stub\n", This, font);
    return FALSE;
}

static HRESULT WINAPI dwritefont3_GetFontFaceReference(IDWriteFont3 *iface, IDWriteFontFaceReference **reference)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);

    TRACE("(%p)->(%p)\n", This, reference);

    return IDWriteFactory4_CreateFontFaceReference_(This->data->factory, This->data->file, This->data->face_index,
        This->data->simulations, reference);
}

static BOOL WINAPI dwritefont3_HasCharacter(IDWriteFont3 *iface, UINT32 ch)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    HRESULT hr;
    BOOL ret;

    TRACE("(%p)->(0x%x)\n", This, ch);

    hr = IDWriteFont_HasCharacter((IDWriteFont*)iface, ch, &ret);
    return hr == S_OK && ret;
}

static DWRITE_LOCALITY WINAPI dwritefont3_GetLocality(IDWriteFont3 *iface)
{
    struct dwrite_font *This = impl_from_IDWriteFont3(iface);
    FIXME("(%p): stub\n", This);
    return DWRITE_LOCALITY_LOCAL;
}

static const IDWriteFont3Vtbl dwritefontvtbl = {
    dwritefont_QueryInterface,
    dwritefont_AddRef,
    dwritefont_Release,
    dwritefont_GetFontFamily,
    dwritefont_GetWeight,
    dwritefont_GetStretch,
    dwritefont_GetStyle,
    dwritefont_IsSymbolFont,
    dwritefont_GetFaceNames,
    dwritefont_GetInformationalStrings,
    dwritefont_GetSimulations,
    dwritefont_GetMetrics,
    dwritefont_HasCharacter,
    dwritefont_CreateFontFace,
    dwritefont1_GetMetrics,
    dwritefont1_GetPanose,
    dwritefont1_GetUnicodeRanges,
    dwritefont1_IsMonospacedFont,
    dwritefont2_IsColorFont,
    dwritefont3_CreateFontFace,
    dwritefont3_Equals,
    dwritefont3_GetFontFaceReference,
    dwritefont3_HasCharacter,
    dwritefont3_GetLocality
};

static struct dwrite_font *unsafe_impl_from_IDWriteFont(IDWriteFont *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (IDWriteFontVtbl*)&dwritefontvtbl);
    return CONTAINING_RECORD((IDWriteFont3 *)iface, struct dwrite_font, IDWriteFont3_iface);
}

static struct dwrite_fontface *unsafe_impl_from_IDWriteFontFace(IDWriteFontFace *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (IDWriteFontFaceVtbl*)&dwritefontfacevtbl);
    return CONTAINING_RECORD((IDWriteFontFace4 *)iface, struct dwrite_fontface, IDWriteFontFace4_iface);
}

void get_logfont_from_font(IDWriteFont *iface, LOGFONTW *lf)
{
    struct dwrite_font *font = unsafe_impl_from_IDWriteFont(iface);
    *lf = font->data->lf;
}

void get_logfont_from_fontface(IDWriteFontFace *iface, LOGFONTW *lf)
{
    struct dwrite_fontface *fontface = unsafe_impl_from_IDWriteFontFace(iface);
    *lf = fontface->lf;
}

static HRESULT create_font(struct dwrite_font_data *data, IDWriteFontFamily1 *family, IDWriteFont3 **font)
{
    struct dwrite_font *This;
    *font = NULL;

    This = heap_alloc(sizeof(struct dwrite_font));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFont3_iface.lpVtbl = &dwritefontvtbl;
    This->ref = 1;
    This->family = family;
    IDWriteFontFamily1_AddRef(family);
    This->style = data->style;
    This->data = data;
    InterlockedIncrement(&This->data->ref);

    *font = &This->IDWriteFont3_iface;

    return S_OK;
}

/* IDWriteFontList1 */
static HRESULT WINAPI dwritefontlist_QueryInterface(IDWriteFontList1 *iface, REFIID riid, void **obj)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontList1) ||
        IsEqualIID(riid, &IID_IDWriteFontList) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFontList1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontlist_AddRef(IDWriteFontList1 *iface)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontlist_Release(IDWriteFontList1 *iface)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        UINT32 i;

        for (i = 0; i < This->font_count; i++)
            release_font_data(This->fonts[i]);
        IDWriteFontFamily1_Release(This->family);
        heap_free(This->fonts);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefontlist_GetFontCollection(IDWriteFontList1 *iface, IDWriteFontCollection **collection)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);
    return IDWriteFontFamily1_GetFontCollection(This->family, collection);
}

static UINT32 WINAPI dwritefontlist_GetFontCount(IDWriteFontList1 *iface)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);
    TRACE("(%p)\n", This);
    return This->font_count;
}

static HRESULT WINAPI dwritefontlist_GetFont(IDWriteFontList1 *iface, UINT32 index, IDWriteFont **font)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, font);

    *font = NULL;

    if (This->font_count == 0)
        return S_FALSE;

    if (index >= This->font_count)
        return E_INVALIDARG;

    return create_font(This->fonts[index], This->family, (IDWriteFont3**)font);
}

static DWRITE_LOCALITY WINAPI dwritefontlist1_GetFontLocality(IDWriteFontList1 *iface, UINT32 index)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);

    FIXME("(%p)->(%u): stub\n", This, index);

    return DWRITE_LOCALITY_LOCAL;
}

static HRESULT WINAPI dwritefontlist1_GetFont(IDWriteFontList1 *iface, UINT32 index, IDWriteFont3 **font)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, font);

    *font = NULL;

    if (This->font_count == 0)
        return S_FALSE;

    if (index >= This->font_count)
        return E_FAIL;

    return create_font(This->fonts[index], This->family, font);
}

static HRESULT WINAPI dwritefontlist1_GetFontFaceReference(IDWriteFontList1 *iface, UINT32 index,
    IDWriteFontFaceReference **reference)
{
    struct dwrite_fontlist *This = impl_from_IDWriteFontList1(iface);
    IDWriteFont3 *font;
    HRESULT hr;

    TRACE("(%p)->(%u %p)\n", This, index, reference);

    *reference = NULL;

    hr = IDWriteFontList1_GetFont(iface, index, &font);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFont3_GetFontFaceReference(font, reference);
    IDWriteFont3_Release(font);

    return hr;
}

static const IDWriteFontList1Vtbl dwritefontlistvtbl = {
    dwritefontlist_QueryInterface,
    dwritefontlist_AddRef,
    dwritefontlist_Release,
    dwritefontlist_GetFontCollection,
    dwritefontlist_GetFontCount,
    dwritefontlist_GetFont,
    dwritefontlist1_GetFontLocality,
    dwritefontlist1_GetFont,
    dwritefontlist1_GetFontFaceReference
};

static HRESULT WINAPI dwritefontfamily_QueryInterface(IDWriteFontFamily1 *iface, REFIID riid, void **obj)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontFamily1) ||
        IsEqualIID(riid, &IID_IDWriteFontFamily) ||
        IsEqualIID(riid, &IID_IDWriteFontList) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFontFamily1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontfamily_AddRef(IDWriteFontFamily1 *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontfamily_Release(IDWriteFontFamily1 *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        IDWriteFontCollection1_Release(This->collection);
        release_fontfamily_data(This->data);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefontfamily_GetFontCollection(IDWriteFontFamily1 *iface, IDWriteFontCollection **collection)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);

    TRACE("(%p)->(%p)\n", This, collection);

    *collection = (IDWriteFontCollection*)This->collection;
    IDWriteFontCollection_AddRef(*collection);
    return S_OK;
}

static UINT32 WINAPI dwritefontfamily_GetFontCount(IDWriteFontFamily1 *iface)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    TRACE("(%p)\n", This);
    return This->data->font_count;
}

static HRESULT WINAPI dwritefontfamily_GetFont(IDWriteFontFamily1 *iface, UINT32 index, IDWriteFont **font)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, font);

    *font = NULL;

    if (This->data->font_count == 0)
        return S_FALSE;

    if (index >= This->data->font_count)
        return E_INVALIDARG;

    return create_font(This->data->fonts[index], iface, (IDWriteFont3**)font);
}

static HRESULT WINAPI dwritefontfamily_GetFamilyNames(IDWriteFontFamily1 *iface, IDWriteLocalizedStrings **names)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    return clone_localizedstring(This->data->familyname, names);
}

static BOOL is_better_font_match(const struct dwrite_font_propvec *next, const struct dwrite_font_propvec *cur,
    const struct dwrite_font_propvec *req)
{
    FLOAT cur_to_req = get_font_prop_vec_distance(cur, req);
    FLOAT next_to_req = get_font_prop_vec_distance(next, req);
    FLOAT cur_req_prod, next_req_prod;

    if (next_to_req < cur_to_req)
        return TRUE;

    if (next_to_req > cur_to_req)
        return FALSE;

    cur_req_prod = get_font_prop_vec_dotproduct(cur, req);
    next_req_prod = get_font_prop_vec_dotproduct(next, req);

    if (next_req_prod > cur_req_prod)
        return TRUE;

    if (next_req_prod < cur_req_prod)
        return FALSE;

    if (next->stretch > cur->stretch)
        return TRUE;
    if (next->stretch < cur->stretch)
        return FALSE;

    if (next->style > cur->style)
        return TRUE;
    if (next->style < cur->style)
        return FALSE;

    if (next->weight > cur->weight)
        return TRUE;
    if (next->weight < cur->weight)
        return FALSE;

    /* full match, no reason to prefer new variant */
    return FALSE;
}

static HRESULT WINAPI dwritefontfamily_GetFirstMatchingFont(IDWriteFontFamily1 *iface, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFont **font)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    struct dwrite_font_propvec req;
    struct dwrite_font_data *match;
    UINT32 i;

    TRACE("(%p)->(%d %d %d %p)\n", This, weight, stretch, style, font);

    if (This->data->font_count == 0) {
        *font = NULL;
        return DWRITE_E_NOFONT;
    }

    init_font_prop_vec(weight, stretch, style, &req);
    match = This->data->fonts[0];

    for (i = 1; i < This->data->font_count; i++) {
        if (is_better_font_match(&This->data->fonts[i]->propvec, &match->propvec, &req))
            match = This->data->fonts[i];
    }

    return create_font(match, iface, (IDWriteFont3**)font);
}

typedef BOOL (*matching_filter_func)(const struct dwrite_font_data*);

static BOOL is_font_acceptable_for_normal(const struct dwrite_font_data *font)
{
    return font->style == DWRITE_FONT_STYLE_NORMAL || font->style == DWRITE_FONT_STYLE_ITALIC;
}

static BOOL is_font_acceptable_for_oblique_italic(const struct dwrite_font_data *font)
{
    return font->style == DWRITE_FONT_STYLE_OBLIQUE || font->style == DWRITE_FONT_STYLE_ITALIC;
}

static void matchingfonts_sort(struct dwrite_fontlist *fonts, const struct dwrite_font_propvec *req)
{
    UINT32 b = fonts->font_count - 1, j, t;

    while (1) {
        t = b;

        for (j = 0; j < b; j++) {
            if (is_better_font_match(&fonts->fonts[j+1]->propvec, &fonts->fonts[j]->propvec, req)) {
                struct dwrite_font_data *s = fonts->fonts[j];
                fonts->fonts[j] = fonts->fonts[j+1];
                fonts->fonts[j+1] = s;
                t = j;
            }
        }

        if (t == b)
            break;
        b = t;
    };
}

static HRESULT WINAPI dwritefontfamily_GetMatchingFonts(IDWriteFontFamily1 *iface, DWRITE_FONT_WEIGHT weight,
    DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFontList **ret)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    matching_filter_func func = NULL;
    struct dwrite_font_propvec req;
    struct dwrite_fontlist *fonts;
    UINT32 i;

    TRACE("(%p)->(%d %d %d %p)\n", This, weight, stretch, style, ret);

    *ret = NULL;

    fonts = heap_alloc(sizeof(*fonts));
    if (!fonts)
        return E_OUTOFMEMORY;

    /* Allocate as many as family has, not all of them will be necessary used. */
    fonts->fonts = heap_alloc(sizeof(*fonts->fonts) * This->data->font_count);
    if (!fonts->fonts) {
        heap_free(fonts);
        return E_OUTOFMEMORY;
    }

    fonts->IDWriteFontList1_iface.lpVtbl = &dwritefontlistvtbl;
    fonts->ref = 1;
    fonts->family = iface;
    IDWriteFontFamily1_AddRef(fonts->family);
    fonts->font_count = 0;

    /* Normal style accepts Normal or Italic, Oblique and Italic - both Oblique and Italic styles */
    if (style == DWRITE_FONT_STYLE_NORMAL) {
        if (This->data->has_normal_face || This->data->has_italic_face)
            func = is_font_acceptable_for_normal;
    }
    else /* requested oblique or italic */ {
        if (This->data->has_oblique_face || This->data->has_italic_face)
            func = is_font_acceptable_for_oblique_italic;
    }

    for (i = 0; i < This->data->font_count; i++) {
        if (!func || func(This->data->fonts[i])) {
            fonts->fonts[fonts->font_count] = This->data->fonts[i];
            InterlockedIncrement(&This->data->fonts[i]->ref);
            fonts->font_count++;
        }
    }

    /* now potential matches are sorted using same criteria GetFirstMatchingFont uses */
    init_font_prop_vec(weight, stretch, style, &req);
    matchingfonts_sort(fonts, &req);

    *ret = (IDWriteFontList*)&fonts->IDWriteFontList1_iface;
    return S_OK;
}

static DWRITE_LOCALITY WINAPI dwritefontfamily1_GetFontLocality(IDWriteFontFamily1 *iface, UINT32 index)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);

    FIXME("(%p)->(%u): stub\n", This, index);

    return DWRITE_LOCALITY_LOCAL;
}

static HRESULT WINAPI dwritefontfamily1_GetFont(IDWriteFontFamily1 *iface, UINT32 index, IDWriteFont3 **font)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, font);

    *font = NULL;

    if (This->data->font_count == 0)
        return S_FALSE;

    if (index >= This->data->font_count)
        return E_FAIL;

    return create_font(This->data->fonts[index], iface, font);
}

static HRESULT WINAPI dwritefontfamily1_GetFontFaceReference(IDWriteFontFamily1 *iface, UINT32 index,
    IDWriteFontFaceReference **reference)
{
    struct dwrite_fontfamily *This = impl_from_IDWriteFontFamily1(iface);
    IDWriteFont3 *font;
    HRESULT hr;

    TRACE("(%p)->(%u %p)\n", This, index, reference);

    *reference = NULL;

    hr = IDWriteFontFamily1_GetFont(iface, index, &font);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFont3_GetFontFaceReference(font, reference);
    IDWriteFont3_Release(font);

    return hr;
}

static const IDWriteFontFamily1Vtbl fontfamilyvtbl = {
    dwritefontfamily_QueryInterface,
    dwritefontfamily_AddRef,
    dwritefontfamily_Release,
    dwritefontfamily_GetFontCollection,
    dwritefontfamily_GetFontCount,
    dwritefontfamily_GetFont,
    dwritefontfamily_GetFamilyNames,
    dwritefontfamily_GetFirstMatchingFont,
    dwritefontfamily_GetMatchingFonts,
    dwritefontfamily1_GetFontLocality,
    dwritefontfamily1_GetFont,
    dwritefontfamily1_GetFontFaceReference
};

static HRESULT create_fontfamily(struct dwrite_fontfamily_data *data, IDWriteFontCollection1 *collection, IDWriteFontFamily1 **family)
{
    struct dwrite_fontfamily *This;

    *family = NULL;

    This = heap_alloc(sizeof(struct dwrite_fontfamily));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFontFamily1_iface.lpVtbl = &fontfamilyvtbl;
    This->ref = 1;
    This->collection = collection;
    IDWriteFontCollection1_AddRef(collection);
    This->data = data;
    InterlockedIncrement(&This->data->ref);

    *family = &This->IDWriteFontFamily1_iface;

    return S_OK;
}

BOOL is_system_collection(IDWriteFontCollection *collection)
{
    void *obj;
    return IDWriteFontCollection_QueryInterface(collection, &IID_issystemcollection, (void**)&obj) == S_OK;
}

static HRESULT WINAPI dwritesystemfontcollection_QueryInterface(IDWriteFontCollection1 *iface, REFIID riid, void **obj)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontCollection1) ||
        IsEqualIID(riid, &IID_IDWriteFontCollection) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFontCollection1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;

    if (IsEqualIID(riid, &IID_issystemcollection))
        return S_OK;

    return E_NOINTERFACE;
}

static HRESULT WINAPI dwritefontcollection_QueryInterface(IDWriteFontCollection1 *iface, REFIID riid, void **obj)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontCollection1) ||
        IsEqualIID(riid, &IID_IDWriteFontCollection) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        IDWriteFontCollection1_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontcollection_AddRef(IDWriteFontCollection1 *iface)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontcollection_Release(IDWriteFontCollection1 *iface)
{
    unsigned int i;
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        for (i = 0; i < This->family_count; i++)
            release_fontfamily_data(This->family_data[i]);
        heap_free(This->family_data);
        heap_free(This);
    }

    return ref;
}

static UINT32 WINAPI dwritefontcollection_GetFontFamilyCount(IDWriteFontCollection1 *iface)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    TRACE("(%p)\n", This);
    return This->family_count;
}

static HRESULT WINAPI dwritefontcollection_GetFontFamily(IDWriteFontCollection1 *iface, UINT32 index, IDWriteFontFamily **family)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, family);

    if (index >= This->family_count) {
        *family = NULL;
        return E_FAIL;
    }

    return create_fontfamily(This->family_data[index], iface, (IDWriteFontFamily1**)family);
}

static UINT32 collection_find_family(struct dwrite_fontcollection *collection, const WCHAR *name)
{
    UINT32 i;

    for (i = 0; i < collection->family_count; i++) {
        IDWriteLocalizedStrings *family_name = collection->family_data[i]->familyname;
        UINT32 j, count = IDWriteLocalizedStrings_GetCount(family_name);
        HRESULT hr;

        for (j = 0; j < count; j++) {
            WCHAR buffer[255];
            hr = IDWriteLocalizedStrings_GetString(family_name, j, buffer, 255);
            if (SUCCEEDED(hr) && !strcmpiW(buffer, name))
                return i;
        }
    }

    return ~0u;
}

static HRESULT WINAPI dwritefontcollection_FindFamilyName(IDWriteFontCollection1 *iface, const WCHAR *name, UINT32 *index, BOOL *exists)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    TRACE("(%p)->(%s %p %p)\n", This, debugstr_w(name), index, exists);
    *index = collection_find_family(This, name);
    *exists = *index != ~0u;
    return S_OK;
}

static BOOL is_same_fontfile(IDWriteFontFile *left, IDWriteFontFile *right)
{
    UINT32 left_key_size, right_key_size;
    const void *left_key, *right_key;
    HRESULT hr;

    if (left == right)
        return TRUE;

    hr = IDWriteFontFile_GetReferenceKey(left, &left_key, &left_key_size);
    if (FAILED(hr))
        return FALSE;

    hr = IDWriteFontFile_GetReferenceKey(right, &right_key, &right_key_size);
    if (FAILED(hr))
        return FALSE;

    if (left_key_size != right_key_size)
        return FALSE;

    return !memcmp(left_key, right_key, left_key_size);
}

static HRESULT WINAPI dwritefontcollection_GetFontFromFontFace(IDWriteFontCollection1 *iface, IDWriteFontFace *face, IDWriteFont **font)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);
    struct dwrite_fontfamily_data *found_family = NULL;
    struct dwrite_font_data *found_font = NULL;
    IDWriteFontFamily1 *family;
    UINT32 i, j, face_index;
    IDWriteFontFile *file;
    HRESULT hr;

    TRACE("(%p)->(%p %p)\n", This, face, font);

    *font = NULL;

    if (!face)
        return E_INVALIDARG;

    i = 1;
    hr = IDWriteFontFace_GetFiles(face, &i, &file);
    if (FAILED(hr))
        return hr;
    face_index = IDWriteFontFace_GetIndex(face);

    for (i = 0; i < This->family_count; i++) {
        struct dwrite_fontfamily_data *family_data = This->family_data[i];
        for (j = 0; j < family_data->font_count; j++) {
            struct dwrite_font_data *font_data = family_data->fonts[j];

            if (face_index == font_data->face_index && is_same_fontfile(file, font_data->file)) {
                found_font = font_data;
                found_family = family_data;
                break;
            }
        }
    }

    if (!found_font)
        return DWRITE_E_NOFONT;

    hr = create_fontfamily(found_family, iface, &family);
    if (FAILED(hr))
        return hr;

    hr = create_font(found_font, family, (IDWriteFont3**)font);
    IDWriteFontFamily1_Release(family);
    return hr;
}

static HRESULT WINAPI dwritefontcollection1_GetFontSet(IDWriteFontCollection1 *iface, IDWriteFontSet **fontset)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);

    FIXME("(%p)->(%p): stub\n", This, fontset);

    return E_NOTIMPL;
}

static HRESULT WINAPI dwritefontcollection1_GetFontFamily(IDWriteFontCollection1 *iface, UINT32 index, IDWriteFontFamily1 **family)
{
    struct dwrite_fontcollection *This = impl_from_IDWriteFontCollection1(iface);

    TRACE("(%p)->(%u %p)\n", This, index, family);

    if (index >= This->family_count) {
        *family = NULL;
        return E_FAIL;
    }

    return create_fontfamily(This->family_data[index], iface, family);
}

static const IDWriteFontCollection1Vtbl fontcollectionvtbl = {
    dwritefontcollection_QueryInterface,
    dwritefontcollection_AddRef,
    dwritefontcollection_Release,
    dwritefontcollection_GetFontFamilyCount,
    dwritefontcollection_GetFontFamily,
    dwritefontcollection_FindFamilyName,
    dwritefontcollection_GetFontFromFontFace,
    dwritefontcollection1_GetFontSet,
    dwritefontcollection1_GetFontFamily
};

static const IDWriteFontCollection1Vtbl systemfontcollectionvtbl = {
    dwritesystemfontcollection_QueryInterface,
    dwritefontcollection_AddRef,
    dwritefontcollection_Release,
    dwritefontcollection_GetFontFamilyCount,
    dwritefontcollection_GetFontFamily,
    dwritefontcollection_FindFamilyName,
    dwritefontcollection_GetFontFromFontFace,
    dwritefontcollection1_GetFontSet,
    dwritefontcollection1_GetFontFamily
};

static HRESULT fontfamily_add_font(struct dwrite_fontfamily_data *family_data, struct dwrite_font_data *font_data)
{
    if (family_data->font_count + 1 >= family_data->font_alloc) {
        struct dwrite_font_data **new_list;
        UINT32 new_alloc;

        new_alloc = family_data->font_alloc * 2;
        new_list = heap_realloc(family_data->fonts, sizeof(*family_data->fonts) * new_alloc);
        if (!new_list)
            return E_OUTOFMEMORY;
        family_data->fonts = new_list;
        family_data->font_alloc = new_alloc;
    }

    family_data->fonts[family_data->font_count] = font_data;
    family_data->font_count++;
    if (font_data->style == DWRITE_FONT_STYLE_NORMAL)
        family_data->has_normal_face = 1;
    else if (font_data->style == DWRITE_FONT_STYLE_OBLIQUE)
        family_data->has_oblique_face = 1;
    else
        family_data->has_italic_face = 1;
    return S_OK;
}

static HRESULT fontcollection_add_family(struct dwrite_fontcollection *collection, struct dwrite_fontfamily_data *family)
{
    if (collection->family_alloc < collection->family_count + 1) {
        struct dwrite_fontfamily_data **new_list;
        UINT32 new_alloc;

        new_alloc = collection->family_alloc * 2;
        new_list = heap_realloc(collection->family_data, sizeof(*new_list) * new_alloc);
        if (!new_list)
            return E_OUTOFMEMORY;

        collection->family_alloc = new_alloc;
        collection->family_data = new_list;
    }

    collection->family_data[collection->family_count] = family;
    collection->family_count++;

    return S_OK;
}

static HRESULT init_font_collection(struct dwrite_fontcollection *collection, BOOL is_system)
{
    collection->IDWriteFontCollection1_iface.lpVtbl = is_system ? &systemfontcollectionvtbl : &fontcollectionvtbl;
    collection->ref = 1;
    collection->family_count = 0;
    collection->family_alloc = is_system ? 30 : 5;
    collection->family_data = heap_alloc(sizeof(*collection->family_data) * collection->family_alloc);
    if (!collection->family_data)
        return E_OUTOFMEMORY;

    return S_OK;
}

HRESULT get_filestream_from_file(IDWriteFontFile *file, IDWriteFontFileStream **stream)
{
    IDWriteFontFileLoader *loader;
    const void *key;
    UINT32 key_size;
    HRESULT hr;

    *stream = NULL;

    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFontFile_GetLoader(file, &loader);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFontFileLoader_CreateStreamFromKey(loader, key, key_size, stream);
    IDWriteFontFileLoader_Release(loader);
    if (FAILED(hr))
        return hr;

    return hr;
}

static void fontstrings_get_en_string(IDWriteLocalizedStrings *strings, WCHAR *buffer, UINT32 size)
{
    BOOL exists = FALSE;
    UINT32 index;
    HRESULT hr;

    buffer[0] = 0;
    hr = IDWriteLocalizedStrings_FindLocaleName(strings, enusW, &index, &exists);
    if (FAILED(hr) || !exists)
        return;

    IDWriteLocalizedStrings_GetString(strings, index, buffer, size);
}

static int trim_spaces(WCHAR *in, WCHAR *ret)
{
    int len;

    while (isspaceW(*in))
        in++;

    ret[0] = 0;
    if (!(len = strlenW(in)))
        return 0;

    while (isspaceW(in[len-1]))
        len--;

    memcpy(ret, in, len*sizeof(WCHAR));
    ret[len] = 0;

    return len;
}

struct name_token {
    struct list entry;
    const WCHAR *ptr;
    INT len;     /* token length */
    INT fulllen; /* full length including following separators */
};

static inline BOOL is_name_separator_char(WCHAR ch)
{
    return ch == ' ' || ch == '.' || ch == '-' || ch == '_';
}

struct name_pattern {
    const WCHAR *part1; /* NULL indicates end of list */
    const WCHAR *part2; /* optional, if not NULL should point to non-empty string */
};

static BOOL match_pattern_list(struct list *tokens, const struct name_pattern *patterns, struct name_token *match)
{
    const struct name_pattern *pattern;
    struct name_token *token;
    int i = 0;

    while ((pattern = &patterns[i++])->part1) {
        int len_part1 = strlenW(pattern->part1);
        int len_part2 = pattern->part2 ? strlenW(pattern->part2) : 0;

        LIST_FOR_EACH_ENTRY(token, tokens, struct name_token, entry) {
            if (len_part2 == 0) {
                /* simple case with single part pattern */
                if (token->len != len_part1)
                    continue;

                if (!strncmpiW(token->ptr, pattern->part1, len_part1)) {
                    if (match) *match = *token;
                    list_remove(&token->entry);
                    heap_free(token);
                    return TRUE;
                }
            }
            else {
                struct name_token *next_token;
                struct list *next_entry;

                /* pattern parts are stored in reading order, tokens list is reversed */
                if (token->len < len_part2)
                    continue;

                /* it's possible to have combined string as a token, like ExtraCondensed */
                if (token->len == len_part1 + len_part2) {
                    if (strncmpiW(token->ptr, pattern->part1, len_part1))
                        continue;

                    if (strncmpiW(&token->ptr[len_part1], pattern->part2, len_part2))
                        continue;

                    /* combined string match */
                    if (match) *match = *token;
                    list_remove(&token->entry);
                    heap_free(token);
                    return TRUE;
                }

                /* now it's only possible to have two tokens matched to respective pattern parts */
                if (token->len != len_part2)
                    continue;

                next_entry = list_next(tokens, &token->entry);
                if (next_entry) {
                    next_token = LIST_ENTRY(next_entry, struct name_token, entry);
                    if (next_token->len != len_part1)
                        continue;

                    if (strncmpiW(token->ptr, pattern->part2, len_part2))
                        continue;

                    if (strncmpiW(next_token->ptr, pattern->part1, len_part1))
                        continue;

                    /* both parts matched, remove tokens */
                    if (match) {
                        match->ptr = next_token->ptr;
                        match->len = (token->ptr - next_token->ptr) + token->len;
                    }
                    list_remove(&token->entry);
                    list_remove(&next_token->entry);
                    heap_free(next_token);
                    heap_free(token);
                    return TRUE;
                }
            }
        }
    }

    if (match) {
        match->ptr = NULL;
        match->len = 0;
    }
    return FALSE;
}

static DWRITE_FONT_STYLE font_extract_style(struct list *tokens, DWRITE_FONT_STYLE style, struct name_token *match)
{
    static const WCHAR itaW[] = {'i','t','a',0};
    static const WCHAR italW[] = {'i','t','a','l',0};
    static const WCHAR cursiveW[] = {'c','u','r','s','i','v','e',0};
    static const WCHAR kursivW[] = {'k','u','r','s','i','v',0};

    static const WCHAR inclinedW[] = {'i','n','c','l','i','n','e','d',0};
    static const WCHAR backslantedW[] = {'b','a','c','k','s','l','a','n','t','e','d',0};
    static const WCHAR backslantW[] = {'b','a','c','k','s','l','a','n','t',0};
    static const WCHAR slantedW[] = {'s','l','a','n','t','e','d',0};

    static const struct name_pattern italic_patterns[] = {
        { itaW },
        { italW },
        { italicW },
        { cursiveW },
        { kursivW },
        { NULL }
    };

    static const struct name_pattern oblique_patterns[] = {
        { inclinedW },
        { obliqueW },
        { backslantedW },
        { backslantW },
        { slantedW },
        { NULL }
    };

    /* italic patterns first */
    if (match_pattern_list(tokens, italic_patterns, match))
        return DWRITE_FONT_STYLE_ITALIC;

    /* oblique patterns */
    if (match_pattern_list(tokens, oblique_patterns, match))
        return DWRITE_FONT_STYLE_OBLIQUE;

    return style;
}

static DWRITE_FONT_STRETCH font_extract_stretch(struct list *tokens, DWRITE_FONT_STRETCH stretch,
    struct name_token *match)
{
    static const WCHAR compressedW[] = {'c','o','m','p','r','e','s','s','e','d',0};
    static const WCHAR extendedW[] = {'e','x','t','e','n','d','e','d',0};
    static const WCHAR compactW[] = {'c','o','m','p','a','c','t',0};
    static const WCHAR narrowW[] = {'n','a','r','r','o','w',0};
    static const WCHAR wideW[] = {'w','i','d','e',0};
    static const WCHAR condW[] = {'c','o','n','d',0};

    static const struct name_pattern ultracondensed_patterns[] = {
        { extraW, compressedW },
        { extW, compressedW },
        { ultraW, compressedW },
        { ultraW, condensedW },
        { ultraW, condW },
        { NULL }
    };

    static const struct name_pattern extracondensed_patterns[] = {
        { compressedW },
        { extraW, condensedW },
        { extW, condensedW },
        { extraW, condW },
        { extW, condW },
        { NULL }
    };

    static const struct name_pattern semicondensed_patterns[] = {
        { narrowW },
        { compactW },
        { semiW, condensedW },
        { semiW, condW },
        { NULL }
    };

    static const struct name_pattern semiexpanded_patterns[] = {
        { wideW },
        { semiW, expandedW },
        { semiW, extendedW },
        { NULL }
    };

    static const struct name_pattern extraexpanded_patterns[] = {
        { extraW, expandedW },
        { extW, expandedW },
        { extraW, extendedW },
        { extW, extendedW },
        { NULL }
    };

    static const struct name_pattern ultraexpanded_patterns[] = {
        { ultraW, expandedW },
        { ultraW, extendedW },
        { NULL }
    };

    static const struct name_pattern condensed_patterns[] = {
        { condensedW },
        { condW },
        { NULL }
    };

    static const struct name_pattern expanded_patterns[] = {
        { expandedW },
        { extendedW },
        { NULL }
    };

    if (match_pattern_list(tokens, ultracondensed_patterns, match))
        return DWRITE_FONT_STRETCH_ULTRA_CONDENSED;

    if (match_pattern_list(tokens, extracondensed_patterns, match))
        return DWRITE_FONT_STRETCH_EXTRA_CONDENSED;

    if (match_pattern_list(tokens, semicondensed_patterns, match))
        return DWRITE_FONT_STRETCH_SEMI_CONDENSED;

    if (match_pattern_list(tokens, semiexpanded_patterns, match))
        return DWRITE_FONT_STRETCH_SEMI_EXPANDED;

    if (match_pattern_list(tokens, extraexpanded_patterns, match))
        return DWRITE_FONT_STRETCH_EXTRA_EXPANDED;

    if (match_pattern_list(tokens, ultraexpanded_patterns, match))
        return DWRITE_FONT_STRETCH_ULTRA_EXPANDED;

    if (match_pattern_list(tokens, condensed_patterns, match))
        return DWRITE_FONT_STRETCH_CONDENSED;

    if (match_pattern_list(tokens, expanded_patterns, match))
        return DWRITE_FONT_STRETCH_EXPANDED;

    return stretch;
}

static DWRITE_FONT_WEIGHT font_extract_weight(struct list *tokens, DWRITE_FONT_WEIGHT weight,
    struct name_token *match)
{
    static const WCHAR heavyW[] = {'h','e','a','v','y',0};
    static const WCHAR nordW[] = {'n','o','r','d',0};

    static const struct name_pattern thin_patterns[] = {
        { extraW, thinW },
        { extW, thinW },
        { ultraW, thinW },
        { NULL }
    };

    static const struct name_pattern extralight_patterns[] = {
        { extraW, lightW },
        { extW, lightW },
        { ultraW, lightW },
        { NULL }
    };

    static const struct name_pattern semilight_patterns[] = {
        { semiW, lightW },
        { NULL }
    };

    static const struct name_pattern demibold_patterns[] = {
        { semiW, boldW },
        { demiW, boldW },
        { NULL }
    };

    static const struct name_pattern extrabold_patterns[] = {
        { extraW, boldW },
        { extW, boldW },
        { ultraW, boldW },
        { NULL }
    };

    static const struct name_pattern extrablack_patterns[] = {
        { extraW, blackW },
        { extW, blackW },
        { ultraW, blackW },
        { NULL }
    };

    static const struct name_pattern bold_patterns[] = {
        { boldW },
        { NULL }
    };

    static const struct name_pattern thin2_patterns[] = {
        { thinW },
        { NULL }
    };

    static const struct name_pattern light_patterns[] = {
        { lightW },
        { NULL }
    };

    static const struct name_pattern medium_patterns[] = {
        { mediumW },
        { NULL }
    };

    static const struct name_pattern black_patterns[] = {
        { blackW },
        { heavyW },
        { nordW },
        { NULL }
    };

    static const struct name_pattern demibold2_patterns[] = {
        { demiW },
        { NULL }
    };

    static const struct name_pattern extrabold2_patterns[] = {
        { ultraW },
        { NULL }
    };

    /* FIXME: allow optional 'face' suffix, separated or not. It's removed together with
       matching pattern. */

    if (match_pattern_list(tokens, thin_patterns, match))
        return DWRITE_FONT_WEIGHT_THIN;

    if (match_pattern_list(tokens, extralight_patterns, match))
        return DWRITE_FONT_WEIGHT_EXTRA_LIGHT;

    if (match_pattern_list(tokens, semilight_patterns, match))
        return DWRITE_FONT_WEIGHT_SEMI_LIGHT;

    if (match_pattern_list(tokens, demibold_patterns, match))
        return DWRITE_FONT_WEIGHT_DEMI_BOLD;

    if (match_pattern_list(tokens, extrabold_patterns, match))
        return DWRITE_FONT_WEIGHT_EXTRA_BOLD;

    if (match_pattern_list(tokens, extrablack_patterns, match))
        return DWRITE_FONT_WEIGHT_EXTRA_BLACK;

    if (match_pattern_list(tokens, bold_patterns, match))
        return DWRITE_FONT_WEIGHT_BOLD;

    if (match_pattern_list(tokens, thin2_patterns, match))
        return DWRITE_FONT_WEIGHT_THIN;

    if (match_pattern_list(tokens, light_patterns, match))
        return DWRITE_FONT_WEIGHT_LIGHT;

    if (match_pattern_list(tokens, medium_patterns, match))
        return DWRITE_FONT_WEIGHT_MEDIUM;

    if (match_pattern_list(tokens, black_patterns, match))
        return DWRITE_FONT_WEIGHT_BLACK;

    if (match_pattern_list(tokens, black_patterns, match))
        return DWRITE_FONT_WEIGHT_BLACK;

    if (match_pattern_list(tokens, demibold2_patterns, match))
        return DWRITE_FONT_WEIGHT_DEMI_BOLD;

    if (match_pattern_list(tokens, extrabold2_patterns, match))
        return DWRITE_FONT_WEIGHT_EXTRA_BOLD;

    /* FIXME: use abbreviated names to extract weight */

    return weight;
}

struct knownweight_entry {
    const WCHAR *nameW;
    DWRITE_FONT_WEIGHT weight;
};

static int compare_knownweights(const void *a, const void* b)
{
    DWRITE_FONT_WEIGHT target = *(DWRITE_FONT_WEIGHT*)a;
    const struct knownweight_entry *entry = (struct knownweight_entry*)b;
    int ret = 0;

    if (target > entry->weight)
        ret = 1;
    else if (target < entry->weight)
        ret = -1;

    return ret;
}

static BOOL is_known_weight_value(DWRITE_FONT_WEIGHT weight, WCHAR *nameW)
{
    static const WCHAR extralightW[] = {'E','x','t','r','a',' ','L','i','g','h','t',0};
    static const WCHAR semilightW[] = {'S','e','m','i',' ','L','i','g','h','t',0};
    static const WCHAR extrablackW[] = {'E','x','t','r','a',' ','B','l','a','c','k',0};
    static const WCHAR extraboldW[] = {'E','x','t','r','a',' ','B','o','l','d',0};
    static const WCHAR demiboldW[] = {'D','e','m','i',' ','B','o','l','d',0};
    const struct knownweight_entry *ptr;

    static const struct knownweight_entry knownweights[] = {
        { thinW,       DWRITE_FONT_WEIGHT_THIN },
        { extralightW, DWRITE_FONT_WEIGHT_EXTRA_LIGHT },
        { lightW,      DWRITE_FONT_WEIGHT_LIGHT },
        { semilightW,  DWRITE_FONT_WEIGHT_SEMI_LIGHT },
        { mediumW,     DWRITE_FONT_WEIGHT_MEDIUM },
        { demiboldW,   DWRITE_FONT_WEIGHT_DEMI_BOLD },
        { boldW,       DWRITE_FONT_WEIGHT_BOLD },
        { extraboldW,  DWRITE_FONT_WEIGHT_EXTRA_BOLD },
        { blackW,      DWRITE_FONT_WEIGHT_BLACK },
        { extrablackW, DWRITE_FONT_WEIGHT_EXTRA_BLACK }
    };

    ptr = bsearch(&weight, knownweights, sizeof(knownweights)/sizeof(knownweights[0]), sizeof(knownweights[0]),
        compare_knownweights);
    if (!ptr) {
        nameW[0] = 0;
        return FALSE;
    }

    strcpyW(nameW, ptr->nameW);
    return TRUE;
}

static inline void font_name_token_to_str(const struct name_token *name, WCHAR *strW)
{
    memcpy(strW, name->ptr, name->len * sizeof(WCHAR));
    strW[name->len] = 0;
}

/* Modifies facenameW string, and returns pointer to regular term that was removed */
static const WCHAR *facename_remove_regular_term(WCHAR *facenameW, INT len)
{
    static const WCHAR bookW[] = {'B','o','o','k',0};
    static const WCHAR normalW[] = {'N','o','r','m','a','l',0};
    static const WCHAR regularW[] = {'R','e','g','u','l','a','r',0};
    static const WCHAR romanW[] = {'R','o','m','a','n',0};
    static const WCHAR uprightW[] = {'U','p','r','i','g','h','t',0};

    static const WCHAR *regular_patterns[] = {
        bookW,
        normalW,
        regularW,
        romanW,
        uprightW,
        NULL
    };

    const WCHAR *regular_ptr = NULL, *ptr;
    int i = 0;

    if (len == -1)
        len = strlenW(facenameW);

    /* remove rightmost regular variant from face name */
    while (!regular_ptr && (ptr = regular_patterns[i++])) {
        int pattern_len = strlenW(ptr);
        WCHAR *src;

        if (pattern_len > len)
            continue;

        src = facenameW + len - pattern_len;
        while (src >= facenameW) {
            if (!strncmpiW(src, ptr, pattern_len)) {
                memmove(src, src + pattern_len, (len - pattern_len - (src - facenameW) + 1)*sizeof(WCHAR));
                len = strlenW(facenameW);
                regular_ptr = ptr;
                break;
            }
            else
                src--;
        }
    }

    return regular_ptr;
}

static void fontname_tokenize(struct list *tokens, const WCHAR *nameW)
{
    const WCHAR *ptr;

    list_init(tokens);
    ptr = nameW;

    while (*ptr) {
        struct name_token *token = heap_alloc(sizeof(*token));
        token->ptr = ptr;
        token->len = 0;
        token->fulllen = 0;

        while (*ptr && !is_name_separator_char(*ptr)) {
            token->len++;
            token->fulllen++;
            ptr++;
        }

        /* skip separators */
        while (is_name_separator_char(*ptr)) {
            token->fulllen++;
            ptr++;
        }

        list_add_head(tokens, &token->entry);
    }
}

static void fontname_tokens_to_str(struct list *tokens, WCHAR *nameW)
{
    struct name_token *token, *token2;
    LIST_FOR_EACH_ENTRY_SAFE_REV(token, token2, tokens, struct name_token, entry) {
        int len;

        list_remove(&token->entry);

        /* don't include last separator */
        len = list_empty(tokens) ? token->len : token->fulllen;
        memcpy(nameW, token->ptr, len * sizeof(WCHAR));
        nameW += len;

        heap_free(token);
    }
    *nameW = 0;
}

static BOOL font_apply_differentiation_rules(struct dwrite_font_data *font, WCHAR *familyW, WCHAR *faceW)
{
    struct name_token stretch_name, weight_name, style_name;
    WCHAR familynameW[255], facenameW[255], finalW[255];
    WCHAR weightW[32], stretchW[32], styleW[32];
    const WCHAR *regular_ptr = NULL;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_WEIGHT weight;
    struct list tokens;
    int len;

    /* remove leading and trailing spaces from family and face name */
    trim_spaces(familyW, familynameW);
    len = trim_spaces(faceW, facenameW);

    /* remove rightmost regular variant from face name */
    regular_ptr = facename_remove_regular_term(facenameW, len);

    /* append face name to family name, FIXME check if face name is a substring of family name */
    if (*facenameW) {
        strcatW(familynameW, spaceW);
        strcatW(familynameW, facenameW);
    }

    /* tokenize with " .-_" */
    fontname_tokenize(&tokens, familynameW);

    /* extract and resolve style */
    font->style = font_extract_style(&tokens, font->style, &style_name);

    /* extract stretch */
    stretch = font_extract_stretch(&tokens, font->stretch, &stretch_name);

    /* extract weight */
    weight = font_extract_weight(&tokens, font->weight, &weight_name);

    /* resolve weight */
    if (weight != font->weight) {
        if (!(weight < DWRITE_FONT_WEIGHT_NORMAL && font->weight < DWRITE_FONT_WEIGHT_NORMAL) &&
            !(weight > DWRITE_FONT_WEIGHT_MEDIUM && font->weight > DWRITE_FONT_WEIGHT_MEDIUM) &&
            !((weight == DWRITE_FONT_WEIGHT_NORMAL && font->weight == DWRITE_FONT_WEIGHT_MEDIUM) ||
              (weight == DWRITE_FONT_WEIGHT_MEDIUM && font->weight == DWRITE_FONT_WEIGHT_NORMAL)) &&
            !(abs(weight - font->weight) <= 150 &&
              font->weight != DWRITE_FONT_WEIGHT_NORMAL &&
              font->weight != DWRITE_FONT_WEIGHT_MEDIUM &&
              font->weight != DWRITE_FONT_WEIGHT_BOLD)) {

            font->weight = weight;
        }
    }

    /* Resolve stretch - extracted stretch can't be normal, it will override specified stretch if
       it's leaning in opposite direction from normal comparing to specified stretch or if specified
       stretch itself is normal (extracted stretch is never normal). */
    if (stretch != font->stretch) {
        if ((font->stretch == DWRITE_FONT_STRETCH_NORMAL) ||
            (font->stretch < DWRITE_FONT_STRETCH_NORMAL && stretch > DWRITE_FONT_STRETCH_NORMAL) ||
            (font->stretch > DWRITE_FONT_STRETCH_NORMAL && stretch < DWRITE_FONT_STRETCH_NORMAL)) {

            font->stretch = stretch;
        }
    }

    /* FIXME: cleanup face name from possible 2-3 digit prefixes */

    /* get final combined string from what's left in token list, list is released */
    fontname_tokens_to_str(&tokens, finalW);

    if (!strcmpW(familyW, finalW))
        return FALSE;

    /* construct face name */
    strcpyW(familyW, finalW);

    /* resolved weight name */
    if (weight_name.ptr)
        font_name_token_to_str(&weight_name, weightW);
    /* ignore normal weight */
    else if (font->weight == DWRITE_FONT_WEIGHT_NORMAL)
        weightW[0] = 0;
    /* for known weight values use appropriate names */
    else if (is_known_weight_value(font->weight, weightW)) {
    }
    /* use Wnnn format as a fallback in case weight is not one of known values */
    else {
        static const WCHAR fmtW[] = {'W','%','d',0};
        sprintfW(weightW, fmtW, font->weight);
    }

    /* resolved stretch name */
    if (stretch_name.ptr)
        font_name_token_to_str(&stretch_name, stretchW);
    /* ignore normal stretch */
    else if (font->stretch == DWRITE_FONT_STRETCH_NORMAL)
        stretchW[0] = 0;
    /* use predefined stretch names */
    else {
        static const WCHAR ultracondensedW[] = {'U','l','t','r','a',' ','C','o','n','d','e','n','s','e','d',0};
        static const WCHAR extracondensedW[] = {'E','x','t','r','a',' ','C','o','n','d','e','n','s','e','d',0};
        static const WCHAR semicondensedW[] = {'S','e','m','i',' ','C','o','n','d','e','n','s','e','d',0};
        static const WCHAR semiexpandedW[] = {'S','e','m','i',' ','E','x','p','a','n','d','e','d',0};
        static const WCHAR extraexpandedW[] = {'E','x','t','r','a',' ','E','x','p','a','n','d','e','d',0};
        static const WCHAR ultraexpandedW[] = {'U','l','t','r','a',' ','E','x','p','a','n','d','e','d',0};

        static const WCHAR *stretchnamesW[] = {
            NULL, /* DWRITE_FONT_STRETCH_UNDEFINED */
            ultracondensedW,
            extracondensedW,
            condensedW,
            semicondensedW,
            NULL, /* DWRITE_FONT_STRETCH_NORMAL */
            semiexpandedW,
            expandedW,
            extraexpandedW,
            ultraexpandedW
        };
        strcpyW(stretchW, stretchnamesW[font->stretch]);
    }

    /* resolved style name */
    if (style_name.ptr)
        font_name_token_to_str(&style_name, styleW);
    else if (font->style == DWRITE_FONT_STYLE_NORMAL)
        styleW[0] = 0;
    /* use predefined names */
    else {
        if (font->style == DWRITE_FONT_STYLE_ITALIC)
            strcpyW(styleW, italicW);
        else
            strcpyW(styleW, obliqueW);
    }

    /* use Regular match if it was found initially */
    if (!*weightW && !*stretchW && !*styleW)
        strcpyW(faceW, regular_ptr ? regular_ptr : regularW);
    else {
        faceW[0] = 0;
        if (*stretchW)
            strcpyW(faceW, stretchW);
        if (*weightW) {
            if (*faceW)
                strcatW(faceW, spaceW);
            strcatW(faceW, weightW);
        }
        if (*styleW) {
            if (*faceW)
                strcatW(faceW, spaceW);
            strcatW(faceW, styleW);
        }
    }

    TRACE("resolved family %s, face %s\n", debugstr_w(familyW), debugstr_w(faceW));
    return TRUE;
}

static HRESULT init_font_data(const struct fontface_desc *desc, IDWriteLocalizedStrings **family_name, struct dwrite_font_data **ret)
{
    struct file_stream_desc stream_desc;
    struct dwrite_font_props props;
    struct dwrite_font_data *data;
    IDWriteFontFileStream *stream;
    WCHAR familyW[255], faceW[255];
    HRESULT hr;

    *ret = NULL;
    data = heap_alloc_zero(sizeof(*data));
    if (!data)
        return E_OUTOFMEMORY;

    hr = get_filestream_from_file(desc->files[0], &stream);
    if (FAILED(hr)) {
        heap_free(data);
        return hr;
    }

    data->ref = 1;
    data->factory = desc->factory;
    data->file = desc->files[0];
    data->face_index = desc->index;
    data->face_type = desc->face_type;
    data->simulations = DWRITE_FONT_SIMULATIONS_NONE;
    data->bold_sim_tested = 0;
    data->oblique_sim_tested = 0;
    IDWriteFontFile_AddRef(data->file);
    IDWriteFactory4_AddRef(data->factory);

    stream_desc.stream = stream;
    stream_desc.face_type = desc->face_type;
    stream_desc.face_index = desc->index;
    opentype_get_font_properties(&stream_desc, &props);
    opentype_get_font_metrics(&stream_desc, &data->metrics, NULL);
    opentype_get_font_facename(&stream_desc, props.lf.lfFaceName, &data->names);

    /* get family name from font file */
    hr = opentype_get_font_familyname(&stream_desc, family_name);
    IDWriteFontFileStream_Release(stream);
    if (FAILED(hr)) {
        WARN("unable to get family name from font\n");
        release_font_data(data);
        return hr;
    }

    data->style = props.style;
    data->stretch = props.stretch;
    data->weight = props.weight;
    data->panose = props.panose;
    data->lf = props.lf;

    fontstrings_get_en_string(*family_name, familyW, sizeof(familyW)/sizeof(WCHAR));
    fontstrings_get_en_string(data->names, faceW, sizeof(faceW)/sizeof(WCHAR));
    if (font_apply_differentiation_rules(data, familyW, faceW)) {
        set_en_localizedstring(*family_name, familyW);
        set_en_localizedstring(data->names, faceW);
    }

    init_font_prop_vec(data->weight, data->stretch, data->style, &data->propvec);

    *ret = data;
    return S_OK;
}

static HRESULT init_font_data_from_font(const struct dwrite_font_data *src, DWRITE_FONT_SIMULATIONS sim, const WCHAR *facenameW,
    struct dwrite_font_data **ret)
{
    struct dwrite_font_data *data;

    *ret = NULL;
    data = heap_alloc_zero(sizeof(*data));
    if (!data)
        return E_OUTOFMEMORY;

    *data = *src;
    data->ref = 1;
    data->simulations |= sim;
    if (sim == DWRITE_FONT_SIMULATIONS_BOLD)
        data->weight = DWRITE_FONT_WEIGHT_BOLD;
    else if (sim == DWRITE_FONT_SIMULATIONS_OBLIQUE)
        data->style = DWRITE_FONT_STYLE_OBLIQUE;
    memset(data->info_strings, 0, sizeof(data->info_strings));
    data->names = NULL;
    IDWriteFactory4_AddRef(data->factory);
    IDWriteFontFile_AddRef(data->file);

    create_localizedstrings(&data->names);
    add_localizedstring(data->names, enusW, facenameW);

    init_font_prop_vec(data->weight, data->stretch, data->style, &data->propvec);

    *ret = data;
    return S_OK;
};

static HRESULT init_fontfamily_data(IDWriteLocalizedStrings *familyname, struct dwrite_fontfamily_data **ret)
{
    struct dwrite_fontfamily_data *data;

    data = heap_alloc(sizeof(*data));
    if (!data)
        return E_OUTOFMEMORY;

    data->ref = 1;
    data->font_count = 0;
    data->font_alloc = 2;
    data->has_normal_face = 0;
    data->has_oblique_face = 0;
    data->has_italic_face = 0;

    data->fonts = heap_alloc(sizeof(*data->fonts)*data->font_alloc);
    if (!data->fonts) {
        heap_free(data);
        return E_OUTOFMEMORY;
    }

    data->familyname = familyname;
    IDWriteLocalizedStrings_AddRef(familyname);

    *ret = data;
    return S_OK;
}

static void fontfamily_add_bold_simulated_face(struct dwrite_fontfamily_data *family)
{
    UINT32 i, j, heaviest;

    for (i = 0; i < family->font_count; i++) {
        DWRITE_FONT_WEIGHT weight = family->fonts[i]->weight;
        heaviest = i;

        if (family->fonts[i]->bold_sim_tested)
            continue;

        family->fonts[i]->bold_sim_tested = 1;
        for (j = i; j < family->font_count; j++) {
            if (family->fonts[j]->bold_sim_tested)
                continue;

            if ((family->fonts[i]->style == family->fonts[j]->style) &&
                (family->fonts[i]->stretch == family->fonts[j]->stretch)) {
                if (family->fonts[j]->weight > weight) {
                    weight = family->fonts[j]->weight;
                    heaviest = j;
                }
                family->fonts[j]->bold_sim_tested = 1;
            }
        }

        if (weight >= DWRITE_FONT_WEIGHT_SEMI_LIGHT && weight <= 550) {
            static const struct name_pattern weightsim_patterns[] = {
                { extraW, lightW },
                { extW, lightW },
                { ultraW, lightW },
                { semiW, lightW },
                { semiW, boldW },
                { demiW, boldW },
                { boldW },
                { thinW },
                { lightW },
                { mediumW },
                { demiW },
                { NULL }
            };

            WCHAR facenameW[255], initialW[255];
            struct dwrite_font_data *boldface;
            struct list tokens;

            /* add Bold simulation based on heaviest face data */

            /* Simulated face name should only contain Bold as weight term,
               so remove existing regular and weight terms. */
            fontstrings_get_en_string(family->fonts[heaviest]->names, initialW, sizeof(initialW)/sizeof(WCHAR));
            facename_remove_regular_term(initialW, -1);

            /* remove current weight pattern */
            fontname_tokenize(&tokens, initialW);
            match_pattern_list(&tokens, weightsim_patterns, NULL);
            fontname_tokens_to_str(&tokens, facenameW);

            /* Bold suffix for new name */
            if (*facenameW)
                strcatW(facenameW, spaceW);
            strcatW(facenameW, boldW);

            if (init_font_data_from_font(family->fonts[heaviest], DWRITE_FONT_SIMULATIONS_BOLD, facenameW, &boldface) == S_OK) {
                boldface->bold_sim_tested = 1;
                boldface->lf.lfWeight += (FW_BOLD - FW_REGULAR) / 2 + 1;
                fontfamily_add_font(family, boldface);
            }
        }
    }
}

static void fontfamily_add_oblique_simulated_face(struct dwrite_fontfamily_data *family)
{
    UINT32 i, j;

    for (i = 0; i < family->font_count; i++) {
        UINT32 regular = ~0u, oblique = ~0u;
        struct dwrite_font_data *obliqueface;
        WCHAR facenameW[255];

        if (family->fonts[i]->oblique_sim_tested)
            continue;

        family->fonts[i]->oblique_sim_tested = 1;
        if (family->fonts[i]->style == DWRITE_FONT_STYLE_NORMAL)
            regular = i;
        else if (family->fonts[i]->style == DWRITE_FONT_STYLE_OBLIQUE)
            oblique = i;

        /* find regular style with same weight/stretch values */
        for (j = i; j < family->font_count; j++) {
            if (family->fonts[j]->oblique_sim_tested)
                continue;

            if ((family->fonts[i]->weight == family->fonts[j]->weight) &&
                (family->fonts[i]->stretch == family->fonts[j]->stretch)) {

                family->fonts[j]->oblique_sim_tested = 1;
                if (regular == ~0 && family->fonts[j]->style == DWRITE_FONT_STYLE_NORMAL)
                    regular = j;

                if (oblique == ~0 && family->fonts[j]->style == DWRITE_FONT_STYLE_OBLIQUE)
                    oblique = j;
            }

            if (regular != ~0u && oblique != ~0u)
                break;
        }

        /* no regular variant for this weight/stretch pair, nothing to base simulated face on */
        if (regular == ~0u)
            continue;

        /* regular face exists, and corresponding oblique is present as well, nothing to do */
        if (oblique != ~0u)
            continue;

        /* add oblique simulation based on this regular face */

        /* remove regular term if any, append 'Oblique' */
        fontstrings_get_en_string(family->fonts[regular]->names, facenameW, sizeof(facenameW)/sizeof(WCHAR));
        facename_remove_regular_term(facenameW, -1);

        if (*facenameW)
            strcatW(facenameW, spaceW);
        strcatW(facenameW, obliqueW);

        if (init_font_data_from_font(family->fonts[regular], DWRITE_FONT_SIMULATIONS_OBLIQUE, facenameW, &obliqueface) == S_OK) {
            obliqueface->oblique_sim_tested = 1;
            obliqueface->lf.lfItalic = 1;
            fontfamily_add_font(family, obliqueface);
        }
    }
}

static BOOL fontcollection_add_replacement(struct dwrite_fontcollection *collection, const WCHAR *target_name,
    const WCHAR *replacement_name)
{
    UINT32 i = collection_find_family(collection, replacement_name);
    struct dwrite_fontfamily_data *target;
    IDWriteLocalizedStrings *strings;
    HRESULT hr;

    /* replacement does not exist */
    if (i == ~0u)
        return FALSE;

    hr = create_localizedstrings(&strings);
    if (FAILED(hr))
        return FALSE;

    /* add a new family with target name, reuse font data from replacement */
    add_localizedstring(strings, enusW, target_name);
    hr = init_fontfamily_data(strings, &target);
    if (hr == S_OK) {
        struct dwrite_fontfamily_data *replacement = collection->family_data[i];
        WCHAR nameW[255];

        for (i = 0; i < replacement->font_count; i++)
            fontfamily_add_font(target, replacement->fonts[i]);

        fontcollection_add_family(collection, target);
        fontstrings_get_en_string(replacement->familyname, nameW, sizeof(nameW)/sizeof(WCHAR));
        TRACE("replacement %s -> %s\n", debugstr_w(target_name), debugstr_w(nameW));
    }
    IDWriteLocalizedStrings_Release(strings);
    return TRUE;
}

/* Add family mappings from HKCU\Software\Wine\Fonts\Replacements. This only affects
   system font collections. */
static void fontcollection_add_replacements(struct dwrite_fontcollection *collection)
{
    DWORD max_namelen, max_datalen, i = 0, type, datalen, namelen;
    WCHAR *name;
    void *data;
    HKEY hkey;

    if (RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Wine\\Fonts\\Replacements", &hkey))
        return;

    if (RegQueryInfoKeyW(hkey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_namelen, &max_datalen, NULL, NULL)) {
        RegCloseKey(hkey);
        return;
    }

    max_namelen++; /* returned value doesn't include room for '\0' */
    name = heap_alloc(max_namelen * sizeof(WCHAR));
    data = heap_alloc(max_datalen);

    datalen = max_datalen;
    namelen = max_namelen;
    while (RegEnumValueW(hkey, i++, name, &namelen, NULL, &type, data, &datalen) == ERROR_SUCCESS) {
        if (collection_find_family(collection, name) == ~0u) {
            if (type == REG_MULTI_SZ) {
                WCHAR *replacement = data;
                while (*replacement) {
                    if (fontcollection_add_replacement(collection, name, replacement))
                        break;
                    replacement += strlenW(replacement) + 1;
                }
            }
            else if (type == REG_SZ)
                fontcollection_add_replacement(collection, name, data);
        }
        else
	    TRACE("%s is available, won't be replaced.\n", debugstr_w(name));

        datalen = max_datalen;
        namelen = max_namelen;
    }

    heap_free(data);
    heap_free(name);
    RegCloseKey(hkey);
}

HRESULT create_font_collection(IDWriteFactory4 *factory, IDWriteFontFileEnumerator *enumerator, BOOL is_system,
    IDWriteFontCollection1 **ret)
{
    struct fontfile_enum {
        struct list entry;
        IDWriteFontFile *file;
    };
    struct fontfile_enum *fileenum, *fileenum2;
    struct dwrite_fontcollection *collection;
    struct list scannedfiles;
    BOOL current = FALSE;
    HRESULT hr = S_OK;
    UINT32 i;

    *ret = NULL;

    collection = heap_alloc(sizeof(struct dwrite_fontcollection));
    if (!collection) return E_OUTOFMEMORY;

    hr = init_font_collection(collection, is_system);
    if (FAILED(hr)) {
        heap_free(collection);
        return hr;
    }

    *ret = &collection->IDWriteFontCollection1_iface;

    TRACE("building font collection:\n");

    list_init(&scannedfiles);
    while (hr == S_OK) {
        DWRITE_FONT_FACE_TYPE face_type;
        DWRITE_FONT_FILE_TYPE file_type;
        BOOL supported, same = FALSE;
        IDWriteFontFile *file;
        UINT32 face_count;

        current = FALSE;
        hr = IDWriteFontFileEnumerator_MoveNext(enumerator, &current);
        if (FAILED(hr) || !current)
            break;

        hr = IDWriteFontFileEnumerator_GetCurrentFontFile(enumerator, &file);
        if (FAILED(hr))
            break;

        /* check if we've scanned this file already */
        LIST_FOR_EACH_ENTRY(fileenum, &scannedfiles, struct fontfile_enum, entry) {
            if ((same = is_same_fontfile(fileenum->file, file)))
                break;
        }

        if (same) {
            IDWriteFontFile_Release(file);
            continue;
        }

        /* failed font files are skipped */
        hr = IDWriteFontFile_Analyze(file, &supported, &file_type, &face_type, &face_count);
        if (FAILED(hr) || !supported || face_count == 0) {
            TRACE("unsupported font (%p, 0x%08x, %d, %u)\n", file, hr, supported, face_count);
            IDWriteFontFile_Release(file);
            hr = S_OK;
            continue;
        }

        /* add to scanned list */
        fileenum = heap_alloc(sizeof(*fileenum));
        fileenum->file = file;
        list_add_tail(&scannedfiles, &fileenum->entry);

        for (i = 0; i < face_count; i++) {
            IDWriteLocalizedStrings *family_name = NULL;
            struct dwrite_font_data *font_data;
            struct fontface_desc desc;
            WCHAR familyW[255];
            UINT32 index;

            desc.factory = factory;
            desc.face_type = face_type;
            desc.files = &file;
            desc.files_number = 1;
            desc.index = i;
            desc.simulations = DWRITE_FONT_SIMULATIONS_NONE;
            desc.font_data = NULL;

            /* alloc and init new font data structure */
            hr = init_font_data(&desc, &family_name, &font_data);
            if (FAILED(hr)) {
                /* move to next one */
                hr = S_OK;
                continue;
            }

            fontstrings_get_en_string(family_name, familyW, sizeof(familyW)/sizeof(WCHAR));

            /* ignore dot named faces */
            if (familyW[0] == '.') {
                WARN("Ignoring face %s\n", debugstr_w(familyW));
                IDWriteLocalizedStrings_Release(family_name);
                release_font_data(font_data);
                continue;
            }

            index = collection_find_family(collection, familyW);
            if (index != ~0u)
                hr = fontfamily_add_font(collection->family_data[index], font_data);
            else {
                struct dwrite_fontfamily_data *family_data;

                /* create and init new family */
                hr = init_fontfamily_data(family_name, &family_data);
                if (hr == S_OK) {
                    /* add font to family, family - to collection */
                    hr = fontfamily_add_font(family_data, font_data);
                    if (hr == S_OK)
                        hr = fontcollection_add_family(collection, family_data);

                    if (FAILED(hr))
                        release_fontfamily_data(family_data);
                }
            }

            IDWriteLocalizedStrings_Release(family_name);

            if (FAILED(hr))
                break;
        }
    }

    LIST_FOR_EACH_ENTRY_SAFE(fileenum, fileenum2, &scannedfiles, struct fontfile_enum, entry) {
        IDWriteFontFile_Release(fileenum->file);
        list_remove(&fileenum->entry);
        heap_free(fileenum);
    }

    for (i = 0; i < collection->family_count; i++) {
        fontfamily_add_bold_simulated_face(collection->family_data[i]);
        fontfamily_add_oblique_simulated_face(collection->family_data[i]);
    }

    if (is_system)
        fontcollection_add_replacements(collection);

    return hr;
}

struct system_fontfile_enumerator
{
    IDWriteFontFileEnumerator IDWriteFontFileEnumerator_iface;
    LONG ref;

    IDWriteFactory4 *factory;
    HKEY hkey;
    int index;
};

static inline struct system_fontfile_enumerator *impl_from_IDWriteFontFileEnumerator(IDWriteFontFileEnumerator* iface)
{
    return CONTAINING_RECORD(iface, struct system_fontfile_enumerator, IDWriteFontFileEnumerator_iface);
}

static HRESULT WINAPI systemfontfileenumerator_QueryInterface(IDWriteFontFileEnumerator *iface, REFIID riid, void **obj)
{
    *obj = NULL;

    if (IsEqualIID(riid, &IID_IDWriteFontFileEnumerator) || IsEqualIID(riid, &IID_IUnknown)) {
        IDWriteFontFileEnumerator_AddRef(iface);
        *obj = iface;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI systemfontfileenumerator_AddRef(IDWriteFontFileEnumerator *iface)
{
    struct system_fontfile_enumerator *enumerator = impl_from_IDWriteFontFileEnumerator(iface);
    return InterlockedIncrement(&enumerator->ref);
}

static ULONG WINAPI systemfontfileenumerator_Release(IDWriteFontFileEnumerator *iface)
{
    struct system_fontfile_enumerator *enumerator = impl_from_IDWriteFontFileEnumerator(iface);
    ULONG ref = InterlockedDecrement(&enumerator->ref);

    if (!ref) {
        IDWriteFactory4_Release(enumerator->factory);
        RegCloseKey(enumerator->hkey);
        heap_free(enumerator);
    }

    return ref;
}

static HRESULT create_local_file_reference(IDWriteFactory4 *factory, const WCHAR *filename, IDWriteFontFile **file)
{
    HRESULT hr;

    /* Fonts installed in 'Fonts' system dir don't get full path in registry font files cache */
    if (!strchrW(filename, '\\')) {
        static const WCHAR fontsW[] = {'\\','f','o','n','t','s','\\',0};
        WCHAR fullpathW[MAX_PATH];

        GetWindowsDirectoryW(fullpathW, sizeof(fullpathW)/sizeof(WCHAR));
        strcatW(fullpathW, fontsW);
        strcatW(fullpathW, filename);

        hr = IDWriteFactory4_CreateFontFileReference(factory, fullpathW, NULL, file);
    }
    else
        hr = IDWriteFactory4_CreateFontFileReference(factory, filename, NULL, file);

    return hr;
}

static HRESULT WINAPI systemfontfileenumerator_GetCurrentFontFile(IDWriteFontFileEnumerator *iface, IDWriteFontFile **file)
{
    struct system_fontfile_enumerator *enumerator = impl_from_IDWriteFontFileEnumerator(iface);
    DWORD ret, type, val_count, count;
    WCHAR *value, *filename;
    HRESULT hr;

    *file = NULL;

    if (enumerator->index < 0)
        return E_FAIL;

    ret = RegQueryInfoKeyW(enumerator->hkey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &val_count, &count, NULL, NULL);
    if (ret != ERROR_SUCCESS)
        return E_FAIL;

    val_count++;
    value = heap_alloc( val_count * sizeof(value[0]) );
    filename = heap_alloc(count);
    if (!value || !filename) {
        heap_free(value);
        heap_free(filename);
        return E_OUTOFMEMORY;
    }

    ret = RegEnumValueW(enumerator->hkey, enumerator->index, value, &val_count, NULL, &type, (BYTE*)filename, &count);
    if (ret) {
        heap_free(value);
        heap_free(filename);
        return E_FAIL;
    }

    hr = create_local_file_reference(enumerator->factory, filename, file);

    heap_free(value);
    heap_free(filename);
    return hr;
}

static HRESULT WINAPI systemfontfileenumerator_MoveNext(IDWriteFontFileEnumerator *iface, BOOL *current)
{
    struct system_fontfile_enumerator *enumerator = impl_from_IDWriteFontFileEnumerator(iface);
    DWORD ret, max_val_count;
    WCHAR *value;

    *current = FALSE;
    enumerator->index++;

    ret = RegQueryInfoKeyW(enumerator->hkey, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &max_val_count, NULL, NULL, NULL);
    if (ret != ERROR_SUCCESS)
        return E_FAIL;

    max_val_count++;
    if (!(value = heap_alloc( max_val_count * sizeof(value[0]) )))
        return E_OUTOFMEMORY;

    /* iterate until we find next string value */
    while (1) {
        DWORD type = 0, count, val_count;

        val_count = max_val_count;
        value[0] = 0;
        if (RegEnumValueW(enumerator->hkey, enumerator->index, value, &val_count, NULL, &type, NULL, &count))
            break;
        if (type == REG_SZ && *value && *value != '@') {
            *current = TRUE;
            break;
        }
        enumerator->index++;
    }

    TRACE("index = %d, current = %d\n", enumerator->index, *current);
    heap_free(value);
    return S_OK;
}

static const struct IDWriteFontFileEnumeratorVtbl systemfontfileenumeratorvtbl =
{
    systemfontfileenumerator_QueryInterface,
    systemfontfileenumerator_AddRef,
    systemfontfileenumerator_Release,
    systemfontfileenumerator_MoveNext,
    systemfontfileenumerator_GetCurrentFontFile
};

static HRESULT create_system_fontfile_enumerator(IDWriteFactory4 *factory, IDWriteFontFileEnumerator **ret)
{
    struct system_fontfile_enumerator *enumerator;
    static const WCHAR fontslistW[] = {
        'S','o','f','t','w','a','r','e','\\','M','i','c','r','o','s','o','f','t','\\',
        'W','i','n','d','o','w','s',' ','N','T','\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
        'F','o','n','t','s',0
    };

    *ret = NULL;

    enumerator = heap_alloc(sizeof(*enumerator));
    if (!enumerator)
        return E_OUTOFMEMORY;

    enumerator->IDWriteFontFileEnumerator_iface.lpVtbl = &systemfontfileenumeratorvtbl;
    enumerator->ref = 1;
    enumerator->factory = factory;
    enumerator->index = -1;
    IDWriteFactory4_AddRef(factory);

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fontslistW, 0, GENERIC_READ, &enumerator->hkey)) {
        ERR("failed to open fonts list key\n");
        IDWriteFactory4_Release(factory);
        heap_free(enumerator);
        return E_FAIL;
    }

    *ret = &enumerator->IDWriteFontFileEnumerator_iface;

    return S_OK;
}

HRESULT get_system_fontcollection(IDWriteFactory4 *factory, IDWriteFontCollection1 **collection)
{
    IDWriteFontFileEnumerator *enumerator;
    HRESULT hr;

    *collection = NULL;

    hr = create_system_fontfile_enumerator(factory, &enumerator);
    if (FAILED(hr))
        return hr;

    TRACE("building system font collection for factory %p\n", factory);
    hr = create_font_collection(factory, enumerator, TRUE, collection);
    IDWriteFontFileEnumerator_Release(enumerator);
    return hr;
}

static HRESULT eudc_collection_add_family(IDWriteFactory4 *factory, struct dwrite_fontcollection *collection,
    const WCHAR *keynameW, const WCHAR *pathW)
{
    static const WCHAR defaultfontW[] = {'S','y','s','t','e','m','D','e','f','a','u','l','t','E','U','D','C','F','o','n','t',0};
    static const WCHAR emptyW[] = {0};
    IDWriteLocalizedStrings *names;
    DWRITE_FONT_FACE_TYPE face_type;
    DWRITE_FONT_FILE_TYPE file_type;
    BOOL supported;
    UINT32 face_count, i;
    IDWriteFontFile *file;
    HRESULT hr;
    struct dwrite_fontfamily_data *family_data;

    /* create font file from this path */
    hr = create_local_file_reference(factory, pathW, &file);
    if (FAILED(hr))
        return S_FALSE;

    /* failed font files are skipped */
    hr = IDWriteFontFile_Analyze(file, &supported, &file_type, &face_type, &face_count);
    if (FAILED(hr) || !supported || face_count == 0) {
        TRACE("unsupported font (%p, 0x%08x, %d, %u)\n", file, hr, supported, face_count);
        IDWriteFontFile_Release(file);
        return S_FALSE;
    }

    /* create and init new family */

    /* Family names are added for non-specific locale, represented with empty string.
       Default family appears with empty family name. */
    create_localizedstrings(&names);
    if (!strcmpiW(keynameW, defaultfontW))
        add_localizedstring(names, emptyW, emptyW);
    else
        add_localizedstring(names, emptyW, keynameW);

    hr = init_fontfamily_data(names, &family_data);
    IDWriteLocalizedStrings_Release(names);
    if (hr != S_OK) {
        IDWriteFontFile_Release(file);
        return hr;
    }

    /* fill with faces */
    for (i = 0; i < face_count; i++) {
        struct dwrite_font_data *font_data;
        struct fontface_desc desc;

        /* alloc and init new font data structure */
        desc.factory = factory;
        desc.face_type = face_type;
        desc.index = i;
        desc.files = &file;
        desc.files_number = 1;
        desc.simulations = DWRITE_FONT_SIMULATIONS_NONE;
        desc.font_data = NULL;

        hr = init_font_data(&desc, &names, &font_data);
        if (FAILED(hr))
            continue;

        IDWriteLocalizedStrings_Release(names);

        /* add font to family */
        hr = fontfamily_add_font(family_data, font_data);
        if (hr != S_OK)
            release_font_data(font_data);
    }

    /* add family to collection */
    hr = fontcollection_add_family(collection, family_data);
    if (FAILED(hr))
        release_fontfamily_data(family_data);
    IDWriteFontFile_Release(file);

    return hr;
}

HRESULT get_eudc_fontcollection(IDWriteFactory4 *factory, IDWriteFontCollection **ret)
{
    static const WCHAR eudckeyfmtW[] = {'E','U','D','C','\\','%','u',0};
    struct dwrite_fontcollection *collection;
    static const WCHAR emptyW[] = {0};
    WCHAR eudckeypathW[16];
    HKEY eudckey;
    DWORD index;
    BOOL exists;
    LONG retval;
    HRESULT hr;
    UINT32 i;

    TRACE("building EUDC font collection for factory %p, ACP %u\n", factory, GetACP());

    *ret = NULL;

    collection = heap_alloc(sizeof(struct dwrite_fontcollection));
    if (!collection) return E_OUTOFMEMORY;

    hr = init_font_collection(collection, FALSE);
    if (FAILED(hr)) {
        heap_free(collection);
        return hr;
    }

    *ret = (IDWriteFontCollection*)&collection->IDWriteFontCollection1_iface;

    /* return empty collection if EUDC fonts are not configured */
    sprintfW(eudckeypathW, eudckeyfmtW, GetACP());
    if (RegOpenKeyExW(HKEY_CURRENT_USER, eudckeypathW, 0, GENERIC_READ, &eudckey))
        return S_OK;

    retval = ERROR_SUCCESS;
    index = 0;
    while (retval != ERROR_NO_MORE_ITEMS) {
        WCHAR keynameW[64], pathW[MAX_PATH];
        DWORD type, path_len, name_len;

        path_len = sizeof(pathW)/sizeof(*pathW);
        name_len = sizeof(keynameW)/sizeof(*keynameW);
        retval = RegEnumValueW(eudckey, index++, keynameW, &name_len, NULL, &type, (BYTE*)pathW, &path_len);
        if (retval || type != REG_SZ)
            continue;

        hr = eudc_collection_add_family(factory, collection, keynameW, pathW);
        if (hr != S_OK)
            WARN("failed to add family %s, path %s\n", debugstr_w(keynameW), debugstr_w(pathW));
    }
    RegCloseKey(eudckey);

    /* try to add global default if not defined for specific codepage */
    exists = FALSE;
    hr = IDWriteFontCollection1_FindFamilyName(&collection->IDWriteFontCollection1_iface, emptyW,
        &index, &exists);
    if (FAILED(hr) || !exists) {
        const WCHAR globaldefaultW[] = {'E','U','D','C','.','T','T','E',0};
        hr = eudc_collection_add_family(factory, collection, emptyW, globaldefaultW);
        if (hr != S_OK)
            WARN("failed to add global default EUDC font, 0x%08x\n", hr);
    }

    /* EUDC collection offers simulated faces too */
    for (i = 0; i < collection->family_count; i++) {
        fontfamily_add_bold_simulated_face(collection->family_data[i]);
        fontfamily_add_oblique_simulated_face(collection->family_data[i]);
    }

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_QueryInterface(IDWriteFontFile *iface, REFIID riid, void **obj)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFile))
    {
        *obj = iface;
        IDWriteFontFile_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI dwritefontfile_AddRef(IDWriteFontFile *iface)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI dwritefontfile_Release(IDWriteFontFile *iface)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        IDWriteFontFileLoader_Release(This->loader);
        if (This->stream) IDWriteFontFileStream_Release(This->stream);
        heap_free(This->reference_key);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI dwritefontfile_GetReferenceKey(IDWriteFontFile *iface, const void **fontFileReferenceKey, UINT32 *fontFileReferenceKeySize)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    TRACE("(%p)->(%p, %p)\n", This, fontFileReferenceKey, fontFileReferenceKeySize);
    *fontFileReferenceKey = This->reference_key;
    *fontFileReferenceKeySize = This->key_size;

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_GetLoader(IDWriteFontFile *iface, IDWriteFontFileLoader **fontFileLoader)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    TRACE("(%p)->(%p)\n", This, fontFileLoader);
    *fontFileLoader = This->loader;
    IDWriteFontFileLoader_AddRef(This->loader);

    return S_OK;
}

static HRESULT WINAPI dwritefontfile_Analyze(IDWriteFontFile *iface, BOOL *isSupportedFontType, DWRITE_FONT_FILE_TYPE *fontFileType,
    DWRITE_FONT_FACE_TYPE *fontFaceType, UINT32 *numberOfFaces)
{
    struct dwrite_fontfile *This = impl_from_IDWriteFontFile(iface);
    IDWriteFontFileStream *stream;
    HRESULT hr;

    TRACE("(%p)->(%p, %p, %p, %p)\n", This, isSupportedFontType, fontFileType, fontFaceType, numberOfFaces);

    *isSupportedFontType = FALSE;
    *fontFileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    if (fontFaceType)
        *fontFaceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    *numberOfFaces = 0;

    hr = IDWriteFontFileLoader_CreateStreamFromKey(This->loader, This->reference_key, This->key_size, &stream);
    if (FAILED(hr))
        return hr;

    hr = opentype_analyze_font(stream, numberOfFaces, fontFileType, fontFaceType, isSupportedFontType);

    /* TODO: Further Analysis */
    IDWriteFontFileStream_Release(stream);
    return S_OK;
}

static const IDWriteFontFileVtbl dwritefontfilevtbl = {
    dwritefontfile_QueryInterface,
    dwritefontfile_AddRef,
    dwritefontfile_Release,
    dwritefontfile_GetReferenceKey,
    dwritefontfile_GetLoader,
    dwritefontfile_Analyze,
};

HRESULT create_font_file(IDWriteFontFileLoader *loader, const void *reference_key, UINT32 key_size, IDWriteFontFile **font_file)
{
    struct dwrite_fontfile *This;

    This = heap_alloc(sizeof(struct dwrite_fontfile));
    if (!This) return E_OUTOFMEMORY;

    This->IDWriteFontFile_iface.lpVtbl = &dwritefontfilevtbl;
    This->ref = 1;
    IDWriteFontFileLoader_AddRef(loader);
    This->loader = loader;
    This->stream = NULL;
    This->reference_key = heap_alloc(key_size);
    memcpy(This->reference_key, reference_key, key_size);
    This->key_size = key_size;

    *font_file = &This->IDWriteFontFile_iface;

    return S_OK;
}

static HRESULT get_stream_from_file(IDWriteFontFile *file, IDWriteFontFileStream **stream)
{
    IDWriteFontFileLoader *loader;
    UINT32 key_size;
    const void *key;
    HRESULT hr;

    *stream = NULL;
    hr = IDWriteFontFile_GetLoader(file, &loader);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    if (FAILED(hr)) {
        IDWriteFontFileLoader_Release(loader);
        return hr;
    }

    hr = IDWriteFontFileLoader_CreateStreamFromKey(loader, key, key_size, stream);
    IDWriteFontFileLoader_Release(loader);

    return hr;
}

HRESULT create_fontface(const struct fontface_desc *desc, IDWriteFontFace4 **ret)
{
    struct file_stream_desc stream_desc;
    struct dwrite_fontface *fontface;
    HRESULT hr = S_OK;
    BOOL is_symbol;
    int i;

    *ret = NULL;

    fontface = heap_alloc(sizeof(struct dwrite_fontface));
    if (!fontface)
        return E_OUTOFMEMORY;

    fontface->files = heap_alloc_zero(sizeof(*fontface->files) * desc->files_number);
    fontface->streams = heap_alloc_zero(sizeof(*fontface->streams) * desc->files_number);

    if (!fontface->files || !fontface->streams) {
        heap_free(fontface->files);
        heap_free(fontface->streams);
        heap_free(fontface);
        return E_OUTOFMEMORY;
    }

    fontface->IDWriteFontFace4_iface.lpVtbl = &dwritefontfacevtbl;
    fontface->ref = 1;
    fontface->type = desc->face_type;
    fontface->file_count = desc->files_number;
    memset(&fontface->cmap, 0, sizeof(fontface->cmap));
    memset(&fontface->vdmx, 0, sizeof(fontface->vdmx));
    memset(&fontface->gasp, 0, sizeof(fontface->gasp));
    memset(&fontface->cpal, 0, sizeof(fontface->cpal));
    memset(&fontface->colr, 0, sizeof(fontface->colr));
    fontface->cmap.exists = TRUE;
    fontface->vdmx.exists = TRUE;
    fontface->gasp.exists = TRUE;
    fontface->cpal.exists = TRUE;
    fontface->colr.exists = TRUE;
    fontface->index = desc->index;
    fontface->simulations = desc->simulations;
    memset(fontface->glyphs, 0, sizeof(fontface->glyphs));

    for (i = 0; i < fontface->file_count; i++) {
        hr = get_stream_from_file(desc->files[i], &fontface->streams[i]);
        if (FAILED(hr)) {
            IDWriteFontFace4_Release(&fontface->IDWriteFontFace4_iface);
            return hr;
        }

        fontface->files[i] = desc->files[i];
        IDWriteFontFile_AddRef(fontface->files[i]);
    }

    stream_desc.stream = fontface->streams[0];
    stream_desc.face_type = desc->face_type;
    stream_desc.face_index = desc->index;
    opentype_get_font_metrics(&stream_desc, &fontface->metrics, &fontface->caret);
    if (desc->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
        /* TODO: test what happens if caret is already slanted */
        if (fontface->caret.slopeRise == 1) {
            fontface->caret.slopeRise = fontface->metrics.designUnitsPerEm;
            fontface->caret.slopeRun = fontface->caret.slopeRise / 3;
        }
    }

    fontface->flags = 0;
    fontface->charmap = freetype_get_charmap_index(&fontface->IDWriteFontFace4_iface, &is_symbol);
    if (is_symbol)
        fontface->flags |= FONTFACE_IS_SYMBOL;
    if (freetype_has_kerning_pairs(&fontface->IDWriteFontFace4_iface))
        fontface->flags |= FONTFACE_HAS_KERN_PAIRS;
    if (freetype_is_monospaced(&fontface->IDWriteFontFace4_iface))
        fontface->flags |= FONTFACE_IS_MONOSPACED;
    if (opentype_has_vertical_variants(&fontface->IDWriteFontFace4_iface))
        fontface->flags |= FONTFACE_HAS_VERTICAL_VARIANTS;

    /* Font properties are reused from font object when 'normal' face creation path is used:
       collection -> family -> matching font -> fontface.

       If face is created directly from factory we have to go through properties resolution.
    */
    if (desc->font_data) {
        fontface->weight = desc->font_data->weight;
        fontface->style = desc->font_data->style;
        fontface->stretch = desc->font_data->stretch;
        fontface->panose = desc->font_data->panose;
        fontface->lf = desc->font_data->lf;
    }
    else {
        IDWriteLocalizedStrings *names;
        struct dwrite_font_data *data;

        hr = init_font_data(desc, &names, &data);
        if (FAILED(hr)) {
            IDWriteFontFace4_Release(&fontface->IDWriteFontFace4_iface);
            return hr;
        }

        fontface->weight = data->weight;
        fontface->style = data->style;
        fontface->stretch = data->stretch;
        fontface->panose = data->panose;
        fontface->lf = data->lf;

        IDWriteLocalizedStrings_Release(names);
        release_font_data(data);
    }

    *ret = &fontface->IDWriteFontFace4_iface;
    return S_OK;
}

/* IDWriteLocalFontFileLoader and its required IDWriteFontFileStream */
struct local_refkey
{
    FILETIME writetime;
    WCHAR name[1];
};

struct local_cached_stream
{
    struct list entry;
    IDWriteFontFileStream *stream;
    struct local_refkey *key;
    UINT32 key_size;
};

struct dwrite_localfontfilestream
{
    IDWriteFontFileStream IDWriteFontFileStream_iface;
    LONG ref;

    struct local_cached_stream *entry;
    const void *file_ptr;
    UINT64 size;
};

struct dwrite_localfontfileloader {
    IDWriteLocalFontFileLoader IDWriteLocalFontFileLoader_iface;
    LONG ref;

    struct list streams;
};

static inline struct dwrite_localfontfileloader *impl_from_IDWriteLocalFontFileLoader(IDWriteLocalFontFileLoader *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_localfontfileloader, IDWriteLocalFontFileLoader_iface);
}

static inline struct dwrite_localfontfilestream *impl_from_IDWriteFontFileStream(IDWriteFontFileStream *iface)
{
    return CONTAINING_RECORD(iface, struct dwrite_localfontfilestream, IDWriteFontFileStream_iface);
}

static HRESULT WINAPI localfontfilestream_QueryInterface(IDWriteFontFileStream *iface, REFIID riid, void **obj)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    TRACE_(dwrite_file)("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileStream))
    {
        *obj = iface;
        IDWriteFontFileStream_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI localfontfilestream_AddRef(IDWriteFontFileStream *iface)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE_(dwrite_file)("(%p)->(%d)\n", This, ref);
    return ref;
}

static inline void release_cached_stream(struct local_cached_stream *stream)
{
    list_remove(&stream->entry);
    heap_free(stream->key);
    heap_free(stream);
}

static ULONG WINAPI localfontfilestream_Release(IDWriteFontFileStream *iface)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE_(dwrite_file)("(%p)->(%d)\n", This, ref);

    if (!ref) {
        UnmapViewOfFile(This->file_ptr);
        release_cached_stream(This->entry);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI localfontfilestream_ReadFileFragment(IDWriteFontFileStream *iface, void const **fragment_start,
    UINT64 offset, UINT64 fragment_size, void **fragment_context)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);

    TRACE_(dwrite_file)("(%p)->(%p, 0x%s, 0x%s, %p)\n", This, fragment_start,
          wine_dbgstr_longlong(offset), wine_dbgstr_longlong(fragment_size), fragment_context);

    *fragment_context = NULL;

    if ((offset >= This->size - 1) || (fragment_size > This->size - offset)) {
        *fragment_start = NULL;
        return E_FAIL;
    }

    *fragment_start = (char*)This->file_ptr + offset;
    return S_OK;
}

static void WINAPI localfontfilestream_ReleaseFileFragment(IDWriteFontFileStream *iface, void *fragment_context)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    TRACE_(dwrite_file)("(%p)->(%p)\n", This, fragment_context);
}

static HRESULT WINAPI localfontfilestream_GetFileSize(IDWriteFontFileStream *iface, UINT64 *size)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    TRACE_(dwrite_file)("(%p)->(%p)\n", This, size);
    *size = This->size;
    return S_OK;
}

static HRESULT WINAPI localfontfilestream_GetLastWriteTime(IDWriteFontFileStream *iface, UINT64 *last_writetime)
{
    struct dwrite_localfontfilestream *This = impl_from_IDWriteFontFileStream(iface);
    ULARGE_INTEGER li;

    TRACE_(dwrite_file)("(%p)->(%p)\n", This, last_writetime);

    li.u.LowPart = This->entry->key->writetime.dwLowDateTime;
    li.u.HighPart = This->entry->key->writetime.dwHighDateTime;
    *last_writetime = li.QuadPart;

    return S_OK;
}

static const IDWriteFontFileStreamVtbl localfontfilestreamvtbl =
{
    localfontfilestream_QueryInterface,
    localfontfilestream_AddRef,
    localfontfilestream_Release,
    localfontfilestream_ReadFileFragment,
    localfontfilestream_ReleaseFileFragment,
    localfontfilestream_GetFileSize,
    localfontfilestream_GetLastWriteTime
};

static HRESULT create_localfontfilestream(const void *file_ptr, UINT64 size, struct local_cached_stream *entry, IDWriteFontFileStream **ret)
{
    struct dwrite_localfontfilestream *This;

    *ret = NULL;

    This = heap_alloc(sizeof(struct dwrite_localfontfilestream));
    if (!This)
        return E_OUTOFMEMORY;

    This->IDWriteFontFileStream_iface.lpVtbl = &localfontfilestreamvtbl;
    This->ref = 1;

    This->file_ptr = file_ptr;
    This->size = size;
    This->entry = entry;

    *ret = &This->IDWriteFontFileStream_iface;
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_QueryInterface(IDWriteLocalFontFileLoader *iface, REFIID riid, void **obj)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileLoader) || IsEqualIID(riid, &IID_IDWriteLocalFontFileLoader))
    {
        *obj = iface;
        IDWriteLocalFontFileLoader_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI localfontfileloader_AddRef(IDWriteLocalFontFileLoader *iface)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%d)\n", This, ref);
    return ref;
}

static ULONG WINAPI localfontfileloader_Release(IDWriteLocalFontFileLoader *iface)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref) {
        struct local_cached_stream *stream, *stream2;

        /* This will detach all entries from cache. Entries are released together with streams,
           so stream controls cache entry lifetime. */
        LIST_FOR_EACH_ENTRY_SAFE(stream, stream2, &This->streams, struct local_cached_stream, entry)
            list_init(&stream->entry);

        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI localfontfileloader_CreateStreamFromKey(IDWriteLocalFontFileLoader *iface, const void *key, UINT32 key_size, IDWriteFontFileStream **ret)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    const struct local_refkey *refkey = key;
    struct local_cached_stream *stream;
    IDWriteFontFileStream *filestream;
    HANDLE file, mapping;
    LARGE_INTEGER size;
    void *file_ptr;
    HRESULT hr;

    TRACE("(%p)->(%p, %i, %p)\n", This, key, key_size, ret);
    TRACE("name: %s\n", debugstr_w(refkey->name));

    /* search cache first */
    LIST_FOR_EACH_ENTRY(stream, &This->streams, struct local_cached_stream, entry) {
        if (key_size == stream->key_size && !memcmp(stream->key, key, key_size)) {
            *ret = stream->stream;
            IDWriteFontFileStream_AddRef(*ret);
            return S_OK;
        }
    }

    *ret = NULL;

    file = CreateFileW(refkey->name, GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE,
                         NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return E_FAIL;

    GetFileSizeEx(file, &size);
    mapping = CreateFileMappingW(file, NULL, PAGE_READONLY, 0, 0, NULL);
    CloseHandle(file);
    if (!mapping)
        return E_FAIL;

    file_ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!file_ptr) {
        ERR("mapping failed, file size %s, error %d\n", wine_dbgstr_longlong(size.QuadPart), GetLastError());
        return E_FAIL;
    }

    stream = heap_alloc(sizeof(*stream));
    if (!stream) {
        UnmapViewOfFile(file_ptr);
        return E_OUTOFMEMORY;
    }

    stream->key = heap_alloc(key_size);
    if (!stream->key) {
        UnmapViewOfFile(file_ptr);
        heap_free(stream);
        return E_OUTOFMEMORY;
    }

    stream->key_size = key_size;
    memcpy(stream->key, key, key_size);

    hr = create_localfontfilestream(file_ptr, size.QuadPart, stream, &filestream);
    if (FAILED(hr)) {
        UnmapViewOfFile(file_ptr);
        heap_free(stream->key);
        heap_free(stream);
        return hr;
    }

    stream->stream = filestream;
    list_add_head(&This->streams, &stream->entry);

    *ret = stream->stream;

    return S_OK;
}

static HRESULT WINAPI localfontfileloader_GetFilePathLengthFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, UINT32 *length)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    const struct local_refkey *refkey = key;

    TRACE("(%p)->(%p, %i, %p)\n", This, key, key_size, length);

    *length = strlenW(refkey->name);
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_GetFilePathFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, WCHAR *path, UINT32 length)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    const struct local_refkey *refkey = key;

    TRACE("(%p)->(%p, %i, %p, %i)\n", This, key, key_size, path, length);

    if (length < strlenW(refkey->name))
        return E_INVALIDARG;

    strcpyW(path, refkey->name);
    return S_OK;
}

static HRESULT WINAPI localfontfileloader_GetLastWriteTimeFromKey(IDWriteLocalFontFileLoader *iface, void const *key, UINT32 key_size, FILETIME *writetime)
{
    struct dwrite_localfontfileloader *This = impl_from_IDWriteLocalFontFileLoader(iface);
    const struct local_refkey *refkey = key;

    TRACE("(%p)->(%p, %i, %p)\n", This, key, key_size, writetime);

    *writetime = refkey->writetime;
    return S_OK;
}

static const struct IDWriteLocalFontFileLoaderVtbl localfontfileloadervtbl = {
    localfontfileloader_QueryInterface,
    localfontfileloader_AddRef,
    localfontfileloader_Release,
    localfontfileloader_CreateStreamFromKey,
    localfontfileloader_GetFilePathLengthFromKey,
    localfontfileloader_GetFilePathFromKey,
    localfontfileloader_GetLastWriteTimeFromKey
};

HRESULT create_localfontfileloader(IDWriteLocalFontFileLoader **ret)
{
    struct dwrite_localfontfileloader *This;

    *ret = NULL;

    This = heap_alloc(sizeof(struct dwrite_localfontfileloader));
    if (!This)
        return E_OUTOFMEMORY;

    This->IDWriteLocalFontFileLoader_iface.lpVtbl = &localfontfileloadervtbl;
    This->ref = 1;
    list_init(&This->streams);

    *ret = &This->IDWriteLocalFontFileLoader_iface;
    return S_OK;
}

HRESULT get_local_refkey(const WCHAR *path, const FILETIME *writetime, void **key, UINT32 *size)
{
    struct local_refkey *refkey;

    if (!path)
        return E_INVALIDARG;

    *size = FIELD_OFFSET(struct local_refkey, name) + (strlenW(path)+1)*sizeof(WCHAR);
    *key = NULL;

    refkey = heap_alloc(*size);
    if (!refkey)
        return E_OUTOFMEMORY;

    if (writetime)
        refkey->writetime = *writetime;
    else {
        WIN32_FILE_ATTRIBUTE_DATA info;

        if (GetFileAttributesExW(path, GetFileExInfoStandard, &info))
            refkey->writetime = info.ftLastWriteTime;
        else
            memset(&refkey->writetime, 0, sizeof(refkey->writetime));
    }
    strcpyW(refkey->name, path);

    *key = refkey;

    return S_OK;
}

/* IDWriteGlyphRunAnalysis */
static HRESULT WINAPI glyphrunanalysis_QueryInterface(IDWriteGlyphRunAnalysis *iface, REFIID riid, void **ppv)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);

    if (IsEqualIID(riid, &IID_IDWriteGlyphRunAnalysis) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *ppv = iface;
        IDWriteGlyphRunAnalysis_AddRef(iface);
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI glyphrunanalysis_AddRef(IDWriteGlyphRunAnalysis *iface)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%u)\n", This, ref);
    return ref;
}

static ULONG WINAPI glyphrunanalysis_Release(IDWriteGlyphRunAnalysis *iface)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%u)\n", This, ref);

    if (!ref) {
        if (This->run.fontFace)
            IDWriteFontFace_Release(This->run.fontFace);
        heap_free(This->glyphs);
        heap_free(This->advances);
        heap_free(This->advanceoffsets);
        heap_free(This->ascenderoffsets);
        heap_free(This->bitmap);
        heap_free(This);
    }

    return ref;
}

static BOOL is_natural_rendering_mode(DWRITE_RENDERING_MODE1 mode)
{
    switch (mode)
    {
    case DWRITE_RENDERING_MODE1_NATURAL:
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC:
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED:
        return TRUE;
    default:
        return FALSE;
    }
}

static void glyphrunanalysis_get_texturebounds(struct dwrite_glyphrunanalysis *analysis, RECT *bounds)
{
    struct dwrite_glyphbitmap glyph_bitmap;
    IDWriteFontFace4 *fontface3;
    D2D_POINT_2F origin;
    BOOL is_rtl;
    HRESULT hr;
    UINT32 i;

    if (analysis->flags & RUNANALYSIS_BOUNDS_READY) {
        *bounds = analysis->bounds;
        return;
    }

    if (analysis->run.isSideways)
        FIXME("sideways runs are not supported.\n");

    hr = IDWriteFontFace_QueryInterface(analysis->run.fontFace, &IID_IDWriteFontFace4, (void**)&fontface3);
    if (FAILED(hr))
        WARN("failed to get IDWriteFontFace4, 0x%08x\n", hr);

    /* Start with empty bounds at (0,0) origin, returned bounds are not translated back to (0,0), e.g. for
       RTL run negative left bound is returned, same goes for vertical direction - top bound will be negative
       for any non-zero glyph ascender */
    origin.x = origin.y = 0.0f;
    is_rtl = analysis->run.bidiLevel & 1;

    memset(&glyph_bitmap, 0, sizeof(glyph_bitmap));
    glyph_bitmap.fontface = fontface3;
    glyph_bitmap.emsize = analysis->run.fontEmSize * analysis->ppdip;
    glyph_bitmap.nohint = is_natural_rendering_mode(analysis->rendering_mode);
    if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
        glyph_bitmap.m = &analysis->m;

    for (i = 0; i < analysis->run.glyphCount; i++) {
        const D2D_POINT_2F *advanceoffset = analysis->advanceoffsets ? analysis->advanceoffsets + i : NULL;
        const D2D_POINT_2F *ascenderoffset = analysis->ascenderoffsets ? analysis->ascenderoffsets + i : NULL;
        const D2D_POINT_2F *advance = analysis->advances + i;
        RECT *bbox = &glyph_bitmap.bbox;

        glyph_bitmap.index = analysis->run.glyphIndices[i];
        freetype_get_glyph_bbox(&glyph_bitmap);

        if (is_rtl)
            OffsetRect(bbox, origin.x + advance->x, origin.y + advance->y);
        else
            OffsetRect(bbox, origin.x, origin.y);

        if (advanceoffset)
            OffsetRect(bbox, advanceoffset->x + ascenderoffset->x, advanceoffset->y + ascenderoffset->y);

        UnionRect(&analysis->bounds, &analysis->bounds, bbox);
        origin.x += advance->x;
        origin.y += advance->y;
    }

    IDWriteFontFace4_Release(fontface3);

    /* translate to given run origin */
    OffsetRect(&analysis->bounds, analysis->origin.x, analysis->origin.y);
    if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
        OffsetRect(&analysis->bounds, analysis->m.dx, analysis->m.dy);

    analysis->flags |= RUNANALYSIS_BOUNDS_READY;
    *bounds = analysis->bounds;
}

static HRESULT WINAPI glyphrunanalysis_GetAlphaTextureBounds(IDWriteGlyphRunAnalysis *iface, DWRITE_TEXTURE_TYPE type, RECT *bounds)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);

    TRACE("(%p)->(%d %p)\n", This, type, bounds);

    if ((UINT32)type > DWRITE_TEXTURE_CLEARTYPE_3x1) {
        memset(bounds, 0, sizeof(*bounds));
        return E_INVALIDARG;
    }

    if ((type == DWRITE_TEXTURE_ALIASED_1x1 && This->rendering_mode != DWRITE_RENDERING_MODE1_ALIASED) ||
        (type == DWRITE_TEXTURE_CLEARTYPE_3x1 && This->rendering_mode == DWRITE_RENDERING_MODE1_ALIASED)) {
        memset(bounds, 0, sizeof(*bounds));
        return S_OK;
    }

    glyphrunanalysis_get_texturebounds(This, bounds);
    return S_OK;
}

static inline int get_dib_stride( int width, int bpp )
{
    return ((width * bpp + 31) >> 3) & ~3;
}

static inline BYTE *get_pixel_ptr(BYTE *ptr, DWRITE_TEXTURE_TYPE type, const RECT *runbounds, const RECT *bounds)
{
    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        return ptr + (runbounds->top - bounds->top) * (bounds->right - bounds->left) * 3 +
            (runbounds->left - bounds->left) * 3;
    else
        return ptr + (runbounds->top - bounds->top) * (bounds->right - bounds->left) +
            runbounds->left - bounds->left;
}

static void glyphrunanalysis_render(struct dwrite_glyphrunanalysis *analysis, DWRITE_TEXTURE_TYPE type)
{
    static const BYTE masks[8] = {0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};
    struct dwrite_glyphbitmap glyph_bitmap;
    IDWriteFontFace4 *fontface2;
    D2D_POINT_2F origin;
    UINT32 i, size;
    BOOL is_rtl;
    HRESULT hr;
    RECT *bbox;

    hr = IDWriteFontFace_QueryInterface(analysis->run.fontFace, &IID_IDWriteFontFace4, (void**)&fontface2);
    if (FAILED(hr)) {
        WARN("failed to get IDWriteFontFace4, 0x%08x\n", hr);
        return;
    }

    size = (analysis->bounds.right - analysis->bounds.left)*(analysis->bounds.bottom - analysis->bounds.top);
    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        size *= 3;
    analysis->bitmap = heap_alloc_zero(size);

    origin.x = origin.y = 0.0f;
    is_rtl = analysis->run.bidiLevel & 1;

    memset(&glyph_bitmap, 0, sizeof(glyph_bitmap));
    glyph_bitmap.fontface = fontface2;
    glyph_bitmap.emsize = analysis->run.fontEmSize * analysis->ppdip;
    glyph_bitmap.nohint = is_natural_rendering_mode(analysis->rendering_mode);
    glyph_bitmap.type = type;
    if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
        glyph_bitmap.m = &analysis->m;
    bbox = &glyph_bitmap.bbox;

    for (i = 0; i < analysis->run.glyphCount; i++) {
        const D2D_POINT_2F *advanceoffset = analysis->advanceoffsets ? analysis->advanceoffsets + i : NULL;
        const D2D_POINT_2F *ascenderoffset = analysis->ascenderoffsets ? analysis->ascenderoffsets + i : NULL;
        const D2D_POINT_2F *advance = analysis->advances + i;
        int x, y, width, height;
        BYTE *src, *dst;
        BOOL is_1bpp;

        glyph_bitmap.index = analysis->run.glyphIndices[i];
        freetype_get_glyph_bbox(&glyph_bitmap);

        if (IsRectEmpty(bbox)) {
            origin.x += advance->x;
            origin.y += advance->y;
            continue;
        }

        width = bbox->right - bbox->left;
        height = bbox->bottom - bbox->top;

        if (type == DWRITE_TEXTURE_CLEARTYPE_3x1)
            glyph_bitmap.pitch = (width + 3) / 4 * 4;
        else
            glyph_bitmap.pitch = ((width + 31) >> 5) << 2;

        glyph_bitmap.buf = src = heap_alloc_zero(height * glyph_bitmap.pitch);
        is_1bpp = freetype_get_glyph_bitmap(&glyph_bitmap);

        if (is_rtl)
            OffsetRect(bbox, origin.x + advance->x, origin.y + advance->y);
        else
            OffsetRect(bbox, origin.x, origin.y);

        if (advanceoffset)
            OffsetRect(bbox, advanceoffset->x + ascenderoffset->x, advanceoffset->y + ascenderoffset->y);

        OffsetRect(bbox, analysis->origin.x, analysis->origin.y);
        if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
            OffsetRect(bbox, analysis->m.dx, analysis->m.dy);

        /* blit to analysis bitmap */
        dst = get_pixel_ptr(analysis->bitmap, type, bbox, &analysis->bounds);

        if (is_1bpp) {
            /* convert 1bpp to 8bpp/24bpp */
            if (type == DWRITE_TEXTURE_CLEARTYPE_3x1) {
                for (y = 0; y < height; y++) {
                    for (x = 0; x < width; x++)
                        if (src[x / 8] & masks[x % 8])
                            dst[3*x] = dst[3*x+1] = dst[3*x+2] = DWRITE_ALPHA_MAX;
                    src += glyph_bitmap.pitch;
                    dst += (analysis->bounds.right - analysis->bounds.left) * 3;
                }
            }
            else {
                for (y = 0; y < height; y++) {
                    for (x = 0; x < width; x++)
                        if (src[x / 8] & masks[x % 8])
                            dst[x] = DWRITE_ALPHA_MAX;
                    src += get_dib_stride(width, 1);
                    dst += analysis->bounds.right - analysis->bounds.left;
                }
            }
        }
        else {
            /* at this point it's DWRITE_TEXTURE_CLEARTYPE_3x1 with 8bpp src bitmap */
            for (y = 0; y < height; y++) {
                for (x = 0; x < width; x++)
                    dst[3*x] = dst[3*x+1] = dst[3*x+2] = src[x] | dst[3*x];
                src += glyph_bitmap.pitch;
                dst += (analysis->bounds.right - analysis->bounds.left) * 3;
            }
        }

        heap_free(glyph_bitmap.buf);

        origin.x += advance->x;
        origin.y += advance->y;
    }

    IDWriteFontFace4_Release(fontface2);

    analysis->flags |= RUNANALYSIS_BITMAP_READY;

    /* we don't need this anymore */
    heap_free(analysis->glyphs);
    heap_free(analysis->advances);
    heap_free(analysis->advanceoffsets);
    heap_free(analysis->ascenderoffsets);
    IDWriteFontFace_Release(analysis->run.fontFace);

    analysis->glyphs = NULL;
    analysis->advances = NULL;
    analysis->advanceoffsets = NULL;
    analysis->ascenderoffsets = NULL;
    analysis->run.glyphIndices = NULL;
    analysis->run.fontFace = NULL;
}

static HRESULT WINAPI glyphrunanalysis_CreateAlphaTexture(IDWriteGlyphRunAnalysis *iface, DWRITE_TEXTURE_TYPE type,
    RECT const *bounds, BYTE *bitmap, UINT32 size)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);
    UINT32 required;
    RECT runbounds;

    TRACE("(%p)->(%d %s %p %u)\n", This, type, wine_dbgstr_rect(bounds), bitmap, size);

    if (!bounds || !bitmap || (UINT32)type > DWRITE_TEXTURE_CLEARTYPE_3x1)
        return E_INVALIDARG;

    /* make sure buffer is large enough for requested texture type */
    required = (bounds->right - bounds->left) * (bounds->bottom - bounds->top);
    if (type == DWRITE_TEXTURE_CLEARTYPE_3x1)
        required *= 3;

    if (size < required)
        return E_NOT_SUFFICIENT_BUFFER;

    /* validate requested texture type with rendering mode */
    switch (This->rendering_mode)
    {
    case DWRITE_RENDERING_MODE1_ALIASED:
        if (type != DWRITE_TEXTURE_ALIASED_1x1)
            return DWRITE_E_UNSUPPORTEDOPERATION;
        break;
    case DWRITE_RENDERING_MODE1_GDI_CLASSIC:
    case DWRITE_RENDERING_MODE1_GDI_NATURAL:
    case DWRITE_RENDERING_MODE1_NATURAL:
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC:
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED:
        if (type != DWRITE_TEXTURE_CLEARTYPE_3x1)
            return DWRITE_E_UNSUPPORTEDOPERATION;
        break;
    default:
        ;
    }

    memset(bitmap, 0, size);
    glyphrunanalysis_get_texturebounds(This, &runbounds);
    if (IntersectRect(&runbounds, &runbounds, bounds)) {
        int pixel_size = type == DWRITE_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
        int src_width = (This->bounds.right - This->bounds.left) * pixel_size;
        int dst_width = (bounds->right - bounds->left) * pixel_size;
        int draw_width = (runbounds.right - runbounds.left) * pixel_size;
        BYTE *src, *dst;
        int y;

        if (!(This->flags & RUNANALYSIS_BITMAP_READY))
            glyphrunanalysis_render(This, type);

        src = get_pixel_ptr(This->bitmap, type, &runbounds, &This->bounds);
        dst = get_pixel_ptr(bitmap, type, &runbounds, bounds);

        for (y = 0; y < runbounds.bottom - runbounds.top; y++) {
            memcpy(dst, src, draw_width);
            src += src_width;
            dst += dst_width;
        }
    }

    return S_OK;
}

static HRESULT WINAPI glyphrunanalysis_GetAlphaBlendParams(IDWriteGlyphRunAnalysis *iface, IDWriteRenderingParams *params,
    FLOAT *gamma, FLOAT *contrast, FLOAT *cleartypelevel)
{
    struct dwrite_glyphrunanalysis *This = impl_from_IDWriteGlyphRunAnalysis(iface);

    TRACE("(%p)->(%p %p %p %p)\n", This, params, gamma, contrast, cleartypelevel);

    if (!params)
        return E_INVALIDARG;

    switch (This->rendering_mode)
    {
    case DWRITE_RENDERING_MODE1_GDI_CLASSIC:
    case DWRITE_RENDERING_MODE1_GDI_NATURAL:
    {
        UINT value = 0;
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &value, 0);
        *gamma = (FLOAT)value / 1000.0f;
        *contrast = 0.0f;
        *cleartypelevel = 1.0f;
        break;
    }
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED:
        WARN("Downsampled mode is ignored.\n");
        /* fallthrough */
    case DWRITE_RENDERING_MODE1_ALIASED:
    case DWRITE_RENDERING_MODE1_NATURAL:
    case DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC:
        *gamma = IDWriteRenderingParams_GetGamma(params);
        *contrast = IDWriteRenderingParams_GetEnhancedContrast(params);
        *cleartypelevel = IDWriteRenderingParams_GetClearTypeLevel(params);
        break;
    default:
        ;
    }

    return S_OK;
}

static const struct IDWriteGlyphRunAnalysisVtbl glyphrunanalysisvtbl = {
    glyphrunanalysis_QueryInterface,
    glyphrunanalysis_AddRef,
    glyphrunanalysis_Release,
    glyphrunanalysis_GetAlphaTextureBounds,
    glyphrunanalysis_CreateAlphaTexture,
    glyphrunanalysis_GetAlphaBlendParams
};

static inline void init_2d_vec(D2D_POINT_2F *vec, FLOAT length, BOOL is_vertical)
{
    if (is_vertical) {
        vec->x = 0.0f;
        vec->y = length;
    }
    else {
        vec->x = length;
        vec->y = 0.0f;
    }
}

static inline void transform_2d_vec(D2D_POINT_2F *vec, const DWRITE_MATRIX *m)
{
    D2D_POINT_2F ret;
    ret.x = vec->x * m->m11 + vec->y * m->m21;
    ret.y = vec->x * m->m12 + vec->y * m->m22;
    *vec = ret;
}

HRESULT create_glyphrunanalysis(const struct glyphrunanalysis_desc *desc, IDWriteGlyphRunAnalysis **ret)
{
    struct dwrite_glyphrunanalysis *analysis;
    FLOAT rtl_factor;
    UINT32 i;

    *ret = NULL;

    /* check for valid rendering mode */
    if ((UINT32)desc->rendering_mode >= DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED ||
            desc->rendering_mode == DWRITE_RENDERING_MODE1_OUTLINE ||
            desc->rendering_mode == DWRITE_RENDERING_MODE1_DEFAULT)
        return E_INVALIDARG;

    analysis = heap_alloc(sizeof(*analysis));
    if (!analysis)
        return E_OUTOFMEMORY;

    analysis->IDWriteGlyphRunAnalysis_iface.lpVtbl = &glyphrunanalysisvtbl;
    analysis->ref = 1;
    analysis->rendering_mode = desc->rendering_mode;
    analysis->flags = 0;
    analysis->bitmap = NULL;
    analysis->ppdip = desc->ppdip;
    analysis->origin.x = desc->origin_x * desc->ppdip;
    analysis->origin.y = desc->origin_y * desc->ppdip;
    SetRectEmpty(&analysis->bounds);
    analysis->run = *desc->run;
    IDWriteFontFace_AddRef(analysis->run.fontFace);
    analysis->glyphs = heap_alloc(desc->run->glyphCount*sizeof(*desc->run->glyphIndices));
    analysis->advances = heap_alloc(desc->run->glyphCount*sizeof(*analysis->advances));
    if (desc->run->glyphOffsets) {
        analysis->advanceoffsets = heap_alloc(desc->run->glyphCount*sizeof(*analysis->advanceoffsets));
        analysis->ascenderoffsets = heap_alloc(desc->run->glyphCount*sizeof(*analysis->ascenderoffsets));
    }
    else {
        analysis->advanceoffsets = NULL;
        analysis->ascenderoffsets = NULL;
    }

    if (!analysis->glyphs || !analysis->advances || ((!analysis->advanceoffsets || !analysis->ascenderoffsets) && desc->run->glyphOffsets)) {
        heap_free(analysis->glyphs);
        heap_free(analysis->advances);
        heap_free(analysis->advanceoffsets);
        heap_free(analysis->ascenderoffsets);

        analysis->glyphs = NULL;
        analysis->advances = NULL;
        analysis->advanceoffsets = NULL;
        analysis->ascenderoffsets = NULL;

        IDWriteGlyphRunAnalysis_Release(&analysis->IDWriteGlyphRunAnalysis_iface);
        return E_OUTOFMEMORY;
    }

    /* check if transform is usable */
    if (desc->transform && memcmp(desc->transform, &identity, sizeof(*desc->transform))) {
        analysis->m = *desc->transform;
        analysis->flags |= RUNANALYSIS_USE_TRANSFORM;
    }
    else
        memset(&analysis->m, 0, sizeof(analysis->m));

    analysis->run.glyphIndices = analysis->glyphs;
    analysis->run.glyphAdvances = NULL;
    analysis->run.glyphOffsets = NULL;

    rtl_factor = desc->run->bidiLevel & 1 ? -1.0f : 1.0f;

    if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
        transform_2d_vec(&analysis->origin, &analysis->m);

    memcpy(analysis->glyphs, desc->run->glyphIndices, desc->run->glyphCount*sizeof(*desc->run->glyphIndices));

    if (desc->run->glyphAdvances) {
        for (i = 0; i < desc->run->glyphCount; i++) {
            init_2d_vec(analysis->advances + i, rtl_factor * desc->run->glyphAdvances[i] * desc->ppdip, desc->run->isSideways);
            if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
                transform_2d_vec(analysis->advances + i, &analysis->m);
        }
    }
    else {
        DWRITE_FONT_METRICS metrics;
        IDWriteFontFace1 *fontface1;

        IDWriteFontFace_GetMetrics(desc->run->fontFace, &metrics);
        IDWriteFontFace_QueryInterface(desc->run->fontFace, &IID_IDWriteFontFace1, (void**)&fontface1);

        for (i = 0; i < desc->run->glyphCount; i++) {
            HRESULT hr;
            INT32 a;

            switch (desc->measuring_mode)
            {
            case DWRITE_MEASURING_MODE_NATURAL:
                hr = IDWriteFontFace1_GetDesignGlyphAdvances(fontface1, 1, desc->run->glyphIndices + i, &a, desc->run->isSideways);
                if (FAILED(hr))
                    a = 0;
                init_2d_vec(analysis->advances + i, rtl_factor * get_scaled_advance_width(a, desc->run->fontEmSize, &metrics) * desc->ppdip,
                    desc->run->isSideways);
                break;
            case DWRITE_MEASURING_MODE_GDI_CLASSIC:
            case DWRITE_MEASURING_MODE_GDI_NATURAL:
                hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, desc->run->fontEmSize, desc->ppdip, desc->transform,
                    desc->measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL, desc->run->isSideways, 1, desc->run->glyphIndices + i, &a);
                if (FAILED(hr))
                    init_2d_vec(analysis->advances + i, 0.0f, FALSE);
                else
                    init_2d_vec(analysis->advances + i, rtl_factor * floorf(a * desc->run->fontEmSize * desc->ppdip / metrics.designUnitsPerEm + 0.5f),
                        desc->run->isSideways);
                break;
            default:
                ;
            }

            if (analysis->flags & RUNANALYSIS_USE_TRANSFORM)
                transform_2d_vec(analysis->advances + i, &analysis->m);
        }

        IDWriteFontFace1_Release(fontface1);
    }

    if (desc->run->glyphOffsets) {
        for (i = 0; i < desc->run->glyphCount; i++) {
            init_2d_vec(analysis->advanceoffsets + i, rtl_factor * desc->run->glyphOffsets[i].advanceOffset * desc->ppdip, desc->run->isSideways);
            /* Positive ascender offset moves glyph up. Keep it orthogonal to advance direction. */
            init_2d_vec(analysis->ascenderoffsets + i, -desc->run->glyphOffsets[i].ascenderOffset * desc->ppdip, !desc->run->isSideways);
            if (analysis->flags & RUNANALYSIS_USE_TRANSFORM) {
                transform_2d_vec(analysis->advanceoffsets + i, &analysis->m);
                transform_2d_vec(analysis->ascenderoffsets + i, &analysis->m);
            }
        }
    }

    *ret = &analysis->IDWriteGlyphRunAnalysis_iface;
    return S_OK;
}

/* IDWriteColorGlyphRunEnumerator */
static HRESULT WINAPI colorglyphenum_QueryInterface(IDWriteColorGlyphRunEnumerator *iface, REFIID riid, void **ppv)
{
    struct dwrite_colorglyphenum *This = impl_from_IDWriteColorGlyphRunEnumerator(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppv);

    if (IsEqualIID(riid, &IID_IDWriteColorGlyphRunEnumerator) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *ppv = iface;
        IDWriteColorGlyphRunEnumerator_AddRef(iface);
        return S_OK;
    }

    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI colorglyphenum_AddRef(IDWriteColorGlyphRunEnumerator *iface)
{
    struct dwrite_colorglyphenum *This = impl_from_IDWriteColorGlyphRunEnumerator(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%u)\n", This, ref);
    return ref;
}

static ULONG WINAPI colorglyphenum_Release(IDWriteColorGlyphRunEnumerator *iface)
{
    struct dwrite_colorglyphenum *This = impl_from_IDWriteColorGlyphRunEnumerator(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%u)\n", This, ref);

    if (!ref) {
        heap_free(This->advances);
        heap_free(This->color_advances);
        heap_free(This->offsets);
        heap_free(This->color_offsets);
        heap_free(This->glyphindices);
        heap_free(This->glyphs);
        if (This->colr.context)
            IDWriteFontFace4_ReleaseFontTable(This->fontface, This->colr.context);
        IDWriteFontFace4_Release(This->fontface);
        heap_free(This);
    }

    return ref;
}

static FLOAT get_glyph_origin(const struct dwrite_colorglyphenum *glyphenum, UINT32 g)
{
    BOOL is_rtl = glyphenum->run.bidiLevel & 1;
    FLOAT origin = 0.0f;

    if (g == 0)
        return 0.0f;

    while (g--)
        origin += is_rtl ? -glyphenum->advances[g] : glyphenum->advances[g];
    return origin;
}

static BOOL colorglyphenum_build_color_run(struct dwrite_colorglyphenum *glyphenum)
{
    DWRITE_COLOR_GLYPH_RUN *colorrun = &glyphenum->colorrun;
    FLOAT advance_adj = 0.0f;
    BOOL got_palette_index;
    UINT32 g;

    /* start with regular glyphs */
    if (glyphenum->current_layer == 0 && glyphenum->has_regular_glyphs) {
        UINT32 first_glyph = 0;

        for (g = 0; g < glyphenum->run.glyphCount; g++) {
            if (glyphenum->glyphs[g].num_layers == 0) {
                glyphenum->glyphindices[g] = glyphenum->glyphs[g].glyph;
                first_glyph = min(first_glyph, g);
            }
            else
                glyphenum->glyphindices[g] = 1;
            glyphenum->color_advances[g] = glyphenum->advances[g];
            if (glyphenum->color_offsets)
                glyphenum->color_offsets[g] = glyphenum->offsets[g];
        }

        colorrun->baselineOriginX = glyphenum->origin_x + get_glyph_origin(glyphenum, first_glyph);
        colorrun->baselineOriginY = glyphenum->origin_y;
        colorrun->glyphRun.glyphCount = glyphenum->run.glyphCount;
        colorrun->paletteIndex = 0xffff;
        memset(&colorrun->runColor, 0, sizeof(colorrun->runColor));
        glyphenum->has_regular_glyphs = FALSE;
        return TRUE;
    }
    else {
        colorrun->glyphRun.glyphCount = 0;
        got_palette_index = FALSE;
    }

    advance_adj = 0.0f;
    for (g = 0; g < glyphenum->run.glyphCount; g++) {

        glyphenum->glyphindices[g] = 1;

        /* all glyph layers were returned */
        if (glyphenum->glyphs[g].layer == glyphenum->glyphs[g].num_layers) {
            advance_adj += glyphenum->advances[g];
            continue;
        }

        if (glyphenum->current_layer == glyphenum->glyphs[g].layer && (!got_palette_index || colorrun->paletteIndex == glyphenum->glyphs[g].palette_index)) {
            UINT32 index = colorrun->glyphRun.glyphCount;
            if (!got_palette_index) {
                colorrun->paletteIndex = glyphenum->glyphs[g].palette_index;
                /* use foreground color or request one from the font */
                memset(&colorrun->runColor, 0, sizeof(colorrun->runColor));
                if (colorrun->paletteIndex != 0xffff) {
                    HRESULT hr = IDWriteFontFace4_GetPaletteEntries(glyphenum->fontface, glyphenum->palette, colorrun->paletteIndex,
                        1, &colorrun->runColor);
                    if (FAILED(hr))
                        WARN("failed to get palette entry, fontface %p, palette %u, index %u, 0x%08x\n", glyphenum->fontface,
                            glyphenum->palette, colorrun->paletteIndex, hr);
                }
                /* found a glyph position new color run starts from, origin is "original origin + distance to this glyph" */
                colorrun->baselineOriginX = glyphenum->origin_x + get_glyph_origin(glyphenum, g);
                colorrun->baselineOriginY = glyphenum->origin_y;
                glyphenum->color_advances[index] = glyphenum->advances[g];
                got_palette_index = TRUE;
            }

            glyphenum->glyphindices[index] = glyphenum->glyphs[g].glyph;
            /* offsets are relative to glyph origin, nothing to fix up */
            if (glyphenum->color_offsets)
                glyphenum->color_offsets[index] = glyphenum->offsets[g];
            opentype_colr_next_glyph(glyphenum->colr.data, glyphenum->glyphs + g);
            if (index)
                glyphenum->color_advances[index-1] += advance_adj;
            colorrun->glyphRun.glyphCount++;
            advance_adj = 0.0f;
        }
        else
            advance_adj += glyphenum->advances[g];
    }

    /* reset last advance */
    if (colorrun->glyphRun.glyphCount)
        glyphenum->color_advances[colorrun->glyphRun.glyphCount-1] = 0.0f;

    return colorrun->glyphRun.glyphCount > 0;
}

static HRESULT WINAPI colorglyphenum_MoveNext(IDWriteColorGlyphRunEnumerator *iface, BOOL *has_run)
{
    struct dwrite_colorglyphenum *This = impl_from_IDWriteColorGlyphRunEnumerator(iface);

    TRACE("(%p)->(%p)\n", This, has_run);

    *has_run = FALSE;

    This->colorrun.glyphRun.glyphCount = 0;
    while (This->current_layer < This->max_layer_num) {
        if (colorglyphenum_build_color_run(This))
            break;
        else
            This->current_layer++;
    }

    *has_run = This->colorrun.glyphRun.glyphCount > 0;

    return S_OK;
}

static HRESULT WINAPI colorglyphenum_GetCurrentRun(IDWriteColorGlyphRunEnumerator *iface, DWRITE_COLOR_GLYPH_RUN const **run)
{
    struct dwrite_colorglyphenum *This = impl_from_IDWriteColorGlyphRunEnumerator(iface);

    TRACE("(%p)->(%p)\n", This, run);

    if (This->colorrun.glyphRun.glyphCount == 0) {
        *run = NULL;
        return E_NOT_VALID_STATE;
    }

    *run = &This->colorrun;
    return S_OK;
}

static const IDWriteColorGlyphRunEnumeratorVtbl colorglyphenumvtbl = {
    colorglyphenum_QueryInterface,
    colorglyphenum_AddRef,
    colorglyphenum_Release,
    colorglyphenum_MoveNext,
    colorglyphenum_GetCurrentRun
};

HRESULT create_colorglyphenum(FLOAT originX, FLOAT originY, const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *rundescr,
    DWRITE_MEASURING_MODE measuring_mode, const DWRITE_MATRIX *transform, UINT32 palette, IDWriteColorGlyphRunEnumerator **ret)
{
    struct dwrite_colorglyphenum *colorglyphenum;
    BOOL colorfont, has_colored_glyph;
    IDWriteFontFace4 *fontface;
    HRESULT hr;
    UINT32 i;

    *ret = NULL;

    hr = IDWriteFontFace_QueryInterface(run->fontFace, &IID_IDWriteFontFace4, (void**)&fontface);
    if (FAILED(hr)) {
        WARN("failed to get IDWriteFontFace4, 0x%08x\n", hr);
        return hr;
    }

    colorfont = IDWriteFontFace4_IsColorFont(fontface) && IDWriteFontFace4_GetColorPaletteCount(fontface) > palette;
    if (!colorfont) {
        hr = DWRITE_E_NOCOLOR;
        goto failed;
    }

    colorglyphenum = heap_alloc_zero(sizeof(*colorglyphenum));
    if (!colorglyphenum) {
        hr = E_OUTOFMEMORY;
        goto failed;
    }

    colorglyphenum->IDWriteColorGlyphRunEnumerator_iface.lpVtbl = &colorglyphenumvtbl;
    colorglyphenum->ref = 1;
    colorglyphenum->origin_x = originX;
    colorglyphenum->origin_y = originY;
    colorglyphenum->fontface = fontface;
    colorglyphenum->glyphs = NULL;
    colorglyphenum->run = *run;
    colorglyphenum->run.glyphIndices = NULL;
    colorglyphenum->run.glyphAdvances = NULL;
    colorglyphenum->run.glyphOffsets = NULL;
    colorglyphenum->palette = palette;
    memset(&colorglyphenum->colr, 0, sizeof(colorglyphenum->colr));
    colorglyphenum->colr.exists = TRUE;
    get_fontface_table(fontface, MS_COLR_TAG, &colorglyphenum->colr);
    colorglyphenum->current_layer = 0;
    colorglyphenum->max_layer_num = 0;

    colorglyphenum->glyphs = heap_alloc_zero(run->glyphCount * sizeof(*colorglyphenum->glyphs));

    has_colored_glyph = FALSE;
    colorglyphenum->has_regular_glyphs = FALSE;
    for (i = 0; i < run->glyphCount; i++) {
        if (opentype_get_colr_glyph(colorglyphenum->colr.data, run->glyphIndices[i], colorglyphenum->glyphs + i) == S_OK) {
            colorglyphenum->max_layer_num = max(colorglyphenum->max_layer_num, colorglyphenum->glyphs[i].num_layers);
            has_colored_glyph = TRUE;
        }
        if (colorglyphenum->glyphs[i].num_layers == 0)
            colorglyphenum->has_regular_glyphs = TRUE;
    }

    /* It's acceptable to have a subset of glyphs mapped to color layers, for regular runs client
       is supposed to proceed normally, like if font had no color info at all. */
    if (!has_colored_glyph) {
        IDWriteColorGlyphRunEnumerator_Release(&colorglyphenum->IDWriteColorGlyphRunEnumerator_iface);
        return DWRITE_E_NOCOLOR;
    }

    colorglyphenum->advances = heap_alloc(run->glyphCount * sizeof(FLOAT));
    colorglyphenum->color_advances = heap_alloc(run->glyphCount * sizeof(FLOAT));
    colorglyphenum->glyphindices = heap_alloc(run->glyphCount * sizeof(UINT16));
    if (run->glyphOffsets) {
        colorglyphenum->offsets = heap_alloc(run->glyphCount * sizeof(*colorglyphenum->offsets));
        colorglyphenum->color_offsets = heap_alloc(run->glyphCount * sizeof(*colorglyphenum->color_offsets));
        memcpy(colorglyphenum->offsets, run->glyphOffsets, run->glyphCount * sizeof(*run->glyphOffsets));
    }

    colorglyphenum->colorrun.glyphRun.fontFace = (IDWriteFontFace*)fontface;
    colorglyphenum->colorrun.glyphRun.fontEmSize = run->fontEmSize;
    colorglyphenum->colorrun.glyphRun.glyphIndices = colorglyphenum->glyphindices;
    colorglyphenum->colorrun.glyphRun.glyphAdvances = colorglyphenum->color_advances;
    colorglyphenum->colorrun.glyphRun.glyphOffsets = colorglyphenum->color_offsets;
    colorglyphenum->colorrun.glyphRunDescription = NULL; /* FIXME */

    if (run->glyphAdvances)
        memcpy(colorglyphenum->advances, run->glyphAdvances, run->glyphCount * sizeof(FLOAT));
    else {
        DWRITE_FONT_METRICS metrics;

        IDWriteFontFace_GetMetrics(run->fontFace, &metrics);
        for (i = 0; i < run->glyphCount; i++) {
            HRESULT hr;
            INT32 a;

            switch (measuring_mode)
            {
            case DWRITE_MEASURING_MODE_NATURAL:
                hr = IDWriteFontFace4_GetDesignGlyphAdvances(fontface, 1, run->glyphIndices + i, &a, run->isSideways);
                if (FAILED(hr))
                    a = 0;
                colorglyphenum->advances[i] = get_scaled_advance_width(a, run->fontEmSize, &metrics);
                break;
            case DWRITE_MEASURING_MODE_GDI_CLASSIC:
            case DWRITE_MEASURING_MODE_GDI_NATURAL:
                hr = IDWriteFontFace4_GetGdiCompatibleGlyphAdvances(fontface, run->fontEmSize, 1.0f, transform,
                    measuring_mode == DWRITE_MEASURING_MODE_GDI_NATURAL, run->isSideways, 1, run->glyphIndices + i, &a);
                if (FAILED(hr))
                    colorglyphenum->advances[i] = 0.0f;
                else
                    colorglyphenum->advances[i] = floorf(a * run->fontEmSize / metrics.designUnitsPerEm + 0.5f);
                break;
            default:
                ;
            }
        }
    }

    *ret = &colorglyphenum->IDWriteColorGlyphRunEnumerator_iface;
    return S_OK;

failed:
    IDWriteFontFace4_Release(fontface);
    return hr;
}

/* IDWriteFontFaceReference */
static HRESULT WINAPI fontfacereference_QueryInterface(IDWriteFontFaceReference *iface, REFIID riid, void **obj)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), obj);

    if (IsEqualIID(riid, &IID_IDWriteFontFaceReference) || IsEqualIID(riid, &IID_IUnknown)) {
        *obj = iface;
        IDWriteFontFaceReference_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;

    return E_NOINTERFACE;
}

static ULONG WINAPI fontfacereference_AddRef(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    TRACE("(%p)->(%u)\n", This, ref);
    return ref;
}

static ULONG WINAPI fontfacereference_Release(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%u)\n", This, ref);

    if (!ref) {
        IDWriteFontFile_Release(This->file);
        IDWriteFactory4_Release(This->factory);
        heap_free(This);
    }

    return ref;
}

static HRESULT WINAPI fontfacereference_CreateFontFace(IDWriteFontFaceReference *iface, IDWriteFontFace3 **fontface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    TRACE("(%p)->(%p)\n", This, fontface);

    return IDWriteFontFaceReference_CreateFontFaceWithSimulations(iface, This->simulations, fontface);
}

static HRESULT WINAPI fontfacereference_CreateFontFaceWithSimulations(IDWriteFontFaceReference *iface,
    DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace3 **ret)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    IDWriteFontFace *fontface;
    BOOL is_supported;
    UINT32 face_num;
    HRESULT hr;

    TRACE("(%p)->(%d %p)\n", This, simulations, ret);

    hr = IDWriteFontFile_Analyze(This->file, &is_supported, &file_type, &face_type, &face_num);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFactory4_CreateFontFace(This->factory, face_type, 1, &This->file, This->index, simulations, &fontface);
    if (SUCCEEDED(hr)) {
        hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace4, (void**)ret);
        IDWriteFontFace_Release(fontface);
    }

    return hr;
}

static BOOL WINAPI fontfacereference_Equals(IDWriteFontFaceReference *iface, IDWriteFontFaceReference *ref)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);
    IDWriteFontFile *file;
    BOOL ret;

    TRACE("(%p)->(%p)\n", This, ref);

    if (FAILED(IDWriteFontFaceReference_GetFontFile(ref, &file)))
        return FALSE;

    ret = is_same_fontfile(This->file, file) &&
        This->index == IDWriteFontFaceReference_GetFontFaceIndex(ref) &&
        This->simulations == IDWriteFontFaceReference_GetSimulations(ref);
    IDWriteFontFile_Release(file);

    return ret;
}

static UINT32 WINAPI fontfacereference_GetFontFaceIndex(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    TRACE("(%p)\n", This);

    return This->index;
}

static DWRITE_FONT_SIMULATIONS WINAPI fontfacereference_GetSimulations(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    TRACE("(%p)\n", This);

    return This->simulations;
}

static HRESULT WINAPI fontfacereference_GetFontFile(IDWriteFontFaceReference *iface, IDWriteFontFile **file)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);
    IDWriteFontFileLoader *loader;
    const void *key;
    UINT32 key_size;
    HRESULT hr;

    TRACE("(%p)->(%p)\n", This, file);

    hr = IDWriteFontFile_GetReferenceKey(This->file, &key, &key_size);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFontFile_GetLoader(This->file, &loader);
    if (FAILED(hr))
        return hr;

    hr = IDWriteFactory4_CreateCustomFontFileReference(This->factory, key, key_size, loader, file);
    IDWriteFontFileLoader_Release(loader);

    return hr;
}

static UINT64 WINAPI fontfacereference_GetLocalFileSize(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p): stub\n", This);

    return 0;
}

static UINT64 WINAPI fontfacereference_GetFileSize(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p): stub\n", This);

    return 0;
}

static HRESULT WINAPI fontfacereference_GetFileTime(IDWriteFontFaceReference *iface, FILETIME *writetime)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p)->(%p): stub\n", This, writetime);

    return E_NOTIMPL;
}

static DWRITE_LOCALITY WINAPI fontfacereference_GetLocality(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p): stub\n", This);

    return DWRITE_LOCALITY_LOCAL;
}

static HRESULT WINAPI fontfacereference_EnqueueFontDownloadRequest(IDWriteFontFaceReference *iface)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p): stub\n", This);

    return E_NOTIMPL;
}

static HRESULT WINAPI fontfacereference_EnqueueCharacterDownloadRequest(IDWriteFontFaceReference *iface, WCHAR const *chars,
    UINT32 count)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p)->(%s:%u): stub\n", This, debugstr_wn(chars, count), count);

    return E_NOTIMPL;
}

static HRESULT WINAPI fontfacereference_EnqueueGlyphDownloadRequest(IDWriteFontFaceReference *iface, UINT16 const *glyphs,
    UINT32 count)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p)->(%p %u): stub\n", This, glyphs, count);

    return E_NOTIMPL;
}

static HRESULT WINAPI fontfacereference_EnqueueFileFragmentDownloadRequest(IDWriteFontFaceReference *iface,
    UINT64 offset, UINT64 size)
{
    struct dwrite_fontfacereference *This = impl_from_IDWriteFontFaceReference(iface);

    FIXME("(%p)->(0x%s 0x%s): stub\n", This, wine_dbgstr_longlong(offset), wine_dbgstr_longlong(size));

    return E_NOTIMPL;
}

static const IDWriteFontFaceReferenceVtbl fontfacereferencevtbl = {
    fontfacereference_QueryInterface,
    fontfacereference_AddRef,
    fontfacereference_Release,
    fontfacereference_CreateFontFace,
    fontfacereference_CreateFontFaceWithSimulations,
    fontfacereference_Equals,
    fontfacereference_GetFontFaceIndex,
    fontfacereference_GetSimulations,
    fontfacereference_GetFontFile,
    fontfacereference_GetLocalFileSize,
    fontfacereference_GetFileSize,
    fontfacereference_GetFileTime,
    fontfacereference_GetLocality,
    fontfacereference_EnqueueFontDownloadRequest,
    fontfacereference_EnqueueCharacterDownloadRequest,
    fontfacereference_EnqueueGlyphDownloadRequest,
    fontfacereference_EnqueueFileFragmentDownloadRequest
};

HRESULT create_fontfacereference(IDWriteFactory4 *factory, IDWriteFontFile *file, UINT32 index,
    DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFaceReference **ret)
{
    struct dwrite_fontfacereference *ref;

    *ret = NULL;

    if (!is_simulation_valid(simulations))
        return E_INVALIDARG;

    ref = heap_alloc(sizeof(*ref));
    if (!ref)
        return E_OUTOFMEMORY;

    ref->IDWriteFontFaceReference_iface.lpVtbl = &fontfacereferencevtbl;
    ref->ref = 1;

    ref->factory = factory;
    IDWriteFactory4_AddRef(ref->factory);
    ref->file = file;
    IDWriteFontFile_AddRef(ref->file);
    ref->index = index;
    ref->simulations = simulations;
    *ret = &ref->IDWriteFontFaceReference_iface;

    return S_OK;
}
