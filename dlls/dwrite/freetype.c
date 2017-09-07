/*
 *    FreeType integration
 *
 * Copyright 2014-2017 Nikolay Sivov for CodeWeavers
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

#include "config.h"
#include "wine/port.h"

#ifdef HAVE_FT2BUILD_H
#include <ft2build.h>
#include FT_CACHE_H
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#endif /* HAVE_FT2BUILD_H */

#include "windef.h"
#include "wine/library.h"
#include "wine/debug.h"

#include "dwrite_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(dwrite);

#ifdef HAVE_FREETYPE

static CRITICAL_SECTION freetype_cs;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &freetype_cs,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": freetype_cs") }
};
static CRITICAL_SECTION freetype_cs = { &critsect_debug, -1, 0, 0, 0, 0 };

static void *ft_handle = NULL;
static FT_Library library = 0;
static FTC_Manager cache_manager = 0;
static FTC_CMapCache cmap_cache = 0;
static FTC_ImageCache image_cache = 0;
typedef struct
{
    FT_Int major;
    FT_Int minor;
    FT_Int patch;
} FT_Version_t;

#define MAKE_FUNCPTR(f) static typeof(f) * p##f = NULL
MAKE_FUNCPTR(FT_Done_FreeType);
MAKE_FUNCPTR(FT_Done_Glyph);
MAKE_FUNCPTR(FT_Get_First_Char);
MAKE_FUNCPTR(FT_Get_Kerning);
MAKE_FUNCPTR(FT_Get_Sfnt_Table);
MAKE_FUNCPTR(FT_Glyph_Copy);
MAKE_FUNCPTR(FT_Glyph_Get_CBox);
MAKE_FUNCPTR(FT_Glyph_Transform);
MAKE_FUNCPTR(FT_Init_FreeType);
MAKE_FUNCPTR(FT_Library_Version);
MAKE_FUNCPTR(FT_Load_Glyph);
MAKE_FUNCPTR(FT_Matrix_Multiply);
MAKE_FUNCPTR(FT_New_Memory_Face);
MAKE_FUNCPTR(FT_Outline_Copy);
MAKE_FUNCPTR(FT_Outline_Decompose);
MAKE_FUNCPTR(FT_Outline_Done);
MAKE_FUNCPTR(FT_Outline_Embolden);
MAKE_FUNCPTR(FT_Outline_Get_Bitmap);
MAKE_FUNCPTR(FT_Outline_New);
MAKE_FUNCPTR(FT_Outline_Transform);
MAKE_FUNCPTR(FT_Outline_Translate);
MAKE_FUNCPTR(FTC_CMapCache_Lookup);
MAKE_FUNCPTR(FTC_CMapCache_New);
MAKE_FUNCPTR(FTC_ImageCache_Lookup);
MAKE_FUNCPTR(FTC_ImageCache_New);
MAKE_FUNCPTR(FTC_Manager_New);
MAKE_FUNCPTR(FTC_Manager_Done);
MAKE_FUNCPTR(FTC_Manager_LookupFace);
MAKE_FUNCPTR(FTC_Manager_LookupSize);
MAKE_FUNCPTR(FTC_Manager_RemoveFaceID);
#undef MAKE_FUNCPTR
static FT_Error (*pFT_Outline_EmboldenXY)(FT_Outline *, FT_Pos, FT_Pos);

struct face_finalizer_data
{
    IDWriteFontFileStream *stream;
    void *context;
};

static void face_finalizer(void *object)
{
    FT_Face face = object;
    struct face_finalizer_data *data = (struct face_finalizer_data *)face->generic.data;

    IDWriteFontFileStream_ReleaseFileFragment(data->stream, data->context);
    IDWriteFontFileStream_Release(data->stream);
    heap_free(data);
}

static FT_Error face_requester(FTC_FaceID face_id, FT_Library library, FT_Pointer request_data, FT_Face *face)
{
    IDWriteFontFace *fontface = (IDWriteFontFace*)face_id;
    IDWriteFontFileStream *stream;
    IDWriteFontFile *file;
    const void *data_ptr;
    UINT32 index, count;
    FT_Error fterror;
    UINT64 data_size;
    void *context;
    HRESULT hr;

    *face = NULL;

    if (!fontface) {
        WARN("NULL fontface requested.\n");
        return FT_Err_Ok;
    }

    count = 1;
    hr = IDWriteFontFace_GetFiles(fontface, &count, &file);
    if (FAILED(hr))
        return FT_Err_Ok;

    hr = get_filestream_from_file(file, &stream);
    IDWriteFontFile_Release(file);
    if (FAILED(hr))
        return FT_Err_Ok;

    hr = IDWriteFontFileStream_GetFileSize(stream, &data_size);
    if (FAILED(hr)) {
        fterror = FT_Err_Invalid_Stream_Read;
        goto fail;
    }

    hr = IDWriteFontFileStream_ReadFileFragment(stream, &data_ptr, 0, data_size, &context);
    if (FAILED(hr)) {
        fterror = FT_Err_Invalid_Stream_Read;
        goto fail;
    }

    index = IDWriteFontFace_GetIndex(fontface);
    fterror = pFT_New_Memory_Face(library, data_ptr, data_size, index, face);
    if (fterror == FT_Err_Ok) {
        struct face_finalizer_data *data;

        data = heap_alloc(sizeof(*data));
        data->stream = stream;
        data->context = context;

        (*face)->generic.data = data;
        (*face)->generic.finalizer = face_finalizer;
        return fterror;
    }
    else
        IDWriteFontFileStream_ReleaseFileFragment(stream, context);

fail:
    IDWriteFontFileStream_Release(stream);

    return fterror;
}

BOOL init_freetype(void)
{
    FT_Version_t FT_Version;

    ft_handle = wine_dlopen(SONAME_LIBFREETYPE, RTLD_NOW, NULL, 0);
    if (!ft_handle) {
        WINE_MESSAGE("Wine cannot find the FreeType font library.\n");
	return FALSE;
    }

#define LOAD_FUNCPTR(f) if((p##f = wine_dlsym(ft_handle, #f, NULL, 0)) == NULL){WARN("Can't find symbol %s\n", #f); goto sym_not_found;}
    LOAD_FUNCPTR(FT_Done_FreeType)
    LOAD_FUNCPTR(FT_Done_Glyph)
    LOAD_FUNCPTR(FT_Get_First_Char)
    LOAD_FUNCPTR(FT_Get_Kerning)
    LOAD_FUNCPTR(FT_Get_Sfnt_Table)
    LOAD_FUNCPTR(FT_Glyph_Copy)
    LOAD_FUNCPTR(FT_Glyph_Get_CBox)
    LOAD_FUNCPTR(FT_Glyph_Transform)
    LOAD_FUNCPTR(FT_Init_FreeType)
    LOAD_FUNCPTR(FT_Library_Version)
    LOAD_FUNCPTR(FT_Load_Glyph)
    LOAD_FUNCPTR(FT_Matrix_Multiply)
    LOAD_FUNCPTR(FT_New_Memory_Face)
    LOAD_FUNCPTR(FT_Outline_Copy)
    LOAD_FUNCPTR(FT_Outline_Decompose)
    LOAD_FUNCPTR(FT_Outline_Done)
    LOAD_FUNCPTR(FT_Outline_Embolden)
    LOAD_FUNCPTR(FT_Outline_Get_Bitmap)
    LOAD_FUNCPTR(FT_Outline_New)
    LOAD_FUNCPTR(FT_Outline_Transform)
    LOAD_FUNCPTR(FT_Outline_Translate)
    LOAD_FUNCPTR(FTC_CMapCache_Lookup)
    LOAD_FUNCPTR(FTC_CMapCache_New)
    LOAD_FUNCPTR(FTC_ImageCache_Lookup)
    LOAD_FUNCPTR(FTC_ImageCache_New)
    LOAD_FUNCPTR(FTC_Manager_New)
    LOAD_FUNCPTR(FTC_Manager_Done)
    LOAD_FUNCPTR(FTC_Manager_LookupFace)
    LOAD_FUNCPTR(FTC_Manager_LookupSize)
    LOAD_FUNCPTR(FTC_Manager_RemoveFaceID)
#undef LOAD_FUNCPTR
    pFT_Outline_EmboldenXY = wine_dlsym(ft_handle, "FT_Outline_EmboldenXY", NULL, 0);

    if (pFT_Init_FreeType(&library) != 0) {
        ERR("Can't init FreeType library\n");
	wine_dlclose(ft_handle, NULL, 0);
        ft_handle = NULL;
	return FALSE;
    }
    pFT_Library_Version(library, &FT_Version.major, &FT_Version.minor, &FT_Version.patch);

    /* init cache manager */
    if (pFTC_Manager_New(library, 0, 0, 0, &face_requester, NULL, &cache_manager) != 0 ||
        pFTC_CMapCache_New(cache_manager, &cmap_cache) != 0 ||
        pFTC_ImageCache_New(cache_manager, &image_cache) != 0) {

        ERR("Failed to init FreeType cache\n");
        pFTC_Manager_Done(cache_manager);
        pFT_Done_FreeType(library);
        wine_dlclose(ft_handle, NULL, 0);
        ft_handle = NULL;
        return FALSE;
    }

    TRACE("FreeType version is %d.%d.%d\n", FT_Version.major, FT_Version.minor, FT_Version.patch);
    return TRUE;

sym_not_found:
    WINE_MESSAGE("Wine cannot find certain functions that it needs from FreeType library.\n");
    wine_dlclose(ft_handle, NULL, 0);
    ft_handle = NULL;
    return FALSE;
}

void release_freetype(void)
{
    pFTC_Manager_Done(cache_manager);
    pFT_Done_FreeType(library);
}

void freetype_notify_cacheremove(IDWriteFontFace4 *fontface)
{
    EnterCriticalSection(&freetype_cs);
    pFTC_Manager_RemoveFaceID(cache_manager, fontface);
    LeaveCriticalSection(&freetype_cs);
}

HRESULT freetype_get_design_glyph_metrics(IDWriteFontFace4 *fontface, UINT16 unitsperEm, UINT16 glyph, DWRITE_GLYPH_METRICS *ret)
{
    FTC_ScalerRec scaler;
    FT_Size size;

    scaler.face_id = fontface;
    scaler.width  = unitsperEm;
    scaler.height = unitsperEm;
    scaler.pixel = 1;
    scaler.x_res = 0;
    scaler.y_res = 0;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupSize(cache_manager, &scaler, &size) == 0) {
         if (pFT_Load_Glyph(size->face, glyph, FT_LOAD_NO_SCALE) == 0) {
             USHORT simulations = IDWriteFontFace4_GetSimulations(fontface);
             FT_Glyph_Metrics *metrics = &size->face->glyph->metrics;

             ret->leftSideBearing = metrics->horiBearingX;
             ret->advanceWidth = metrics->horiAdvance;
             ret->rightSideBearing = metrics->horiAdvance - metrics->horiBearingX - metrics->width;
             ret->topSideBearing = metrics->vertBearingY;
             ret->advanceHeight = metrics->vertAdvance;
             ret->bottomSideBearing = metrics->vertAdvance - metrics->vertBearingY - metrics->height;
             ret->verticalOriginY = metrics->height + metrics->vertBearingY;

             /* Adjust in case of bold simulation, glyphs without contours are ignored. */
             if (simulations & DWRITE_FONT_SIMULATIONS_BOLD && size->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE &&
                     size->face->glyph->outline.n_contours != 0) {
                 if (ret->advanceWidth)
                     ret->advanceWidth += (unitsperEm + 49) / 50;
             }
         }
    }
    LeaveCriticalSection(&freetype_cs);

    return S_OK;
}

BOOL freetype_is_monospaced(IDWriteFontFace4 *fontface)
{
    BOOL is_monospaced = FALSE;
    FT_Face face;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0)
        is_monospaced = !!FT_IS_FIXED_WIDTH(face);
    LeaveCriticalSection(&freetype_cs);

    return is_monospaced;
}

struct decompose_context {
    IDWriteGeometrySink *sink;
    FLOAT xoffset;
    FLOAT yoffset;
    BOOL figure_started;
    BOOL move_to;     /* last call was 'move_to' */
    FT_Vector origin; /* 'pen' position from last call */
};

static inline void ft_vector_to_d2d_point(const FT_Vector *v, FLOAT xoffset, FLOAT yoffset, D2D1_POINT_2F *p)
{
    p->x = (v->x / 64.0f) + xoffset;
    p->y = (v->y / 64.0f) + yoffset;
}

static void decompose_beginfigure(struct decompose_context *ctxt)
{
    D2D1_POINT_2F point;

    if (!ctxt->move_to)
        return;

    ft_vector_to_d2d_point(&ctxt->origin, ctxt->xoffset, ctxt->yoffset, &point);
    ID2D1SimplifiedGeometrySink_BeginFigure(ctxt->sink, point, D2D1_FIGURE_BEGIN_FILLED);

    ctxt->figure_started = TRUE;
    ctxt->move_to = FALSE;
}

static int decompose_move_to(const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context*)user;

    if (ctxt->figure_started) {
        ID2D1SimplifiedGeometrySink_EndFigure(ctxt->sink, D2D1_FIGURE_END_CLOSED);
        ctxt->figure_started = FALSE;
    }

    ctxt->move_to = TRUE;
    ctxt->origin = *to;
    return 0;
}

static int decompose_line_to(const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context*)user;
    D2D1_POINT_2F point;

    /* Special case for empty contours, in a way freetype returns them. */
    if (ctxt->move_to && !memcmp(to, &ctxt->origin, sizeof(*to)))
        return 0;

    decompose_beginfigure(ctxt);

    ft_vector_to_d2d_point(to, ctxt->xoffset, ctxt->yoffset, &point);
    ID2D1SimplifiedGeometrySink_AddLines(ctxt->sink, &point, 1);

    ctxt->origin = *to;
    return 0;
}

static int decompose_conic_to(const FT_Vector *control, const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context*)user;
    D2D1_POINT_2F points[3];
    FT_Vector cubic[3];

    decompose_beginfigure(ctxt);

    /* convert from quadratic to cubic */

    /*
       The parametric eqn for a cubic Bezier is, from PLRM:
       r(t) = at^3 + bt^2 + ct + r0
       with the control points:
       r1 = r0 + c/3
       r2 = r1 + (c + b)/3
       r3 = r0 + c + b + a

       A quadratic Bezier has the form:
       p(t) = (1-t)^2 p0 + 2(1-t)t p1 + t^2 p2

       So equating powers of t leads to:
       r1 = 2/3 p1 + 1/3 p0
       r2 = 2/3 p1 + 1/3 p2
       and of course r0 = p0, r3 = p2
    */

    /* r1 = 1/3 p0 + 2/3 p1
       r2 = 1/3 p2 + 2/3 p1 */
    cubic[0].x = (2 * control->x + 1) / 3;
    cubic[0].y = (2 * control->y + 1) / 3;
    cubic[1] = cubic[0];
    cubic[0].x += (ctxt->origin.x + 1) / 3;
    cubic[0].y += (ctxt->origin.y + 1) / 3;
    cubic[1].x += (to->x + 1) / 3;
    cubic[1].y += (to->y + 1) / 3;
    cubic[2] = *to;

    ft_vector_to_d2d_point(cubic, ctxt->xoffset, ctxt->yoffset, points);
    ft_vector_to_d2d_point(cubic + 1, ctxt->xoffset, ctxt->yoffset, points + 1);
    ft_vector_to_d2d_point(cubic + 2, ctxt->xoffset, ctxt->yoffset, points + 2);
    ID2D1SimplifiedGeometrySink_AddBeziers(ctxt->sink, (D2D1_BEZIER_SEGMENT*)points, 1);
    ctxt->origin = *to;
    return 0;
}

static int decompose_cubic_to(const FT_Vector *control1, const FT_Vector *control2,
    const FT_Vector *to, void *user)
{
    struct decompose_context *ctxt = (struct decompose_context*)user;
    D2D1_POINT_2F points[3];

    decompose_beginfigure(ctxt);

    ft_vector_to_d2d_point(control1, ctxt->xoffset, ctxt->yoffset, points);
    ft_vector_to_d2d_point(control2, ctxt->xoffset, ctxt->yoffset, points + 1);
    ft_vector_to_d2d_point(to, ctxt->xoffset, ctxt->yoffset, points + 2);
    ID2D1SimplifiedGeometrySink_AddBeziers(ctxt->sink, (D2D1_BEZIER_SEGMENT*)points, 1);
    ctxt->origin = *to;
    return 0;
}

static void decompose_outline(FT_Outline *outline, FLOAT xoffset, FLOAT yoffset, IDWriteGeometrySink *sink)
{
    static const FT_Outline_Funcs decompose_funcs = {
        decompose_move_to,
        decompose_line_to,
        decompose_conic_to,
        decompose_cubic_to,
        0,
        0
    };
    struct decompose_context context;

    context.sink = sink;
    context.xoffset = xoffset;
    context.yoffset = yoffset;
    context.figure_started = FALSE;
    context.move_to = FALSE;
    context.origin.x = 0;
    context.origin.y = 0;

    pFT_Outline_Decompose(outline, &decompose_funcs, &context);

    if (context.figure_started)
        ID2D1SimplifiedGeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
}

static void embolden_glyph_outline(FT_Outline *outline, FLOAT emsize)
{
    FT_Pos strength;

    strength = MulDiv(emsize, 1 << 6, 24);
    if (pFT_Outline_EmboldenXY)
        pFT_Outline_EmboldenXY(outline, strength, 0);
    else
        pFT_Outline_Embolden(outline, strength);
}

static void embolden_glyph(FT_Glyph glyph, FLOAT emsize)
{
    FT_OutlineGlyph outline_glyph = (FT_OutlineGlyph)glyph;

    if (glyph->format != FT_GLYPH_FORMAT_OUTLINE)
        return;

    embolden_glyph_outline(&outline_glyph->outline, emsize);
}

HRESULT freetype_get_glyphrun_outline(IDWriteFontFace4 *fontface, FLOAT emSize, UINT16 const *glyphs,
    FLOAT const *advances, DWRITE_GLYPH_OFFSET const *offsets, UINT32 count, BOOL is_rtl, IDWriteGeometrySink *sink)
{
    FTC_ScalerRec scaler;
    USHORT simulations;
    HRESULT hr = S_OK;
    FT_Size size;

    if (!count)
        return S_OK;

    ID2D1SimplifiedGeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);

    simulations = IDWriteFontFace4_GetSimulations(fontface);

    scaler.face_id = fontface;
    scaler.width  = emSize;
    scaler.height = emSize;
    scaler.pixel = 1;
    scaler.x_res = 0;
    scaler.y_res = 0;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupSize(cache_manager, &scaler, &size) == 0) {
        FLOAT advance = 0.0f;
        UINT32 g;

        for (g = 0; g < count; g++) {
            if (pFT_Load_Glyph(size->face, glyphs[g], FT_LOAD_NO_BITMAP) == 0) {
                FLOAT ft_advance = size->face->glyph->metrics.horiAdvance >> 6;
                FT_Outline *outline = &size->face->glyph->outline;
                FLOAT xoffset = 0.0f, yoffset = 0.0f;
                FT_Matrix m;

                if (simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                    embolden_glyph_outline(outline, emSize);

                m.xx = 1 << 16;
                m.xy = simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE ? (1 << 16) / 3 : 0;
                m.yx = 0;
                m.yy = -(1 << 16); /* flip Y axis */

                pFT_Outline_Transform(outline, &m);

                /* glyph offsets act as current glyph adjustment */
                if (offsets) {
                    xoffset += is_rtl ? -offsets[g].advanceOffset : offsets[g].advanceOffset;
                    yoffset -= offsets[g].ascenderOffset;
                }

                if (g == 0 && is_rtl)
                    advance = advances ? -advances[g] : -ft_advance;

                xoffset += advance;
                decompose_outline(outline, xoffset, yoffset, sink);

                /* update advance to next glyph */
                if (advances)
                    advance += is_rtl ? -advances[g] : advances[g];
                else
                    advance += is_rtl ? -ft_advance : ft_advance;
            }
        }
    }
    else
        hr = E_FAIL;
    LeaveCriticalSection(&freetype_cs);

    return hr;
}

UINT16 freetype_get_glyphcount(IDWriteFontFace4 *fontface)
{
    UINT16 count = 0;
    FT_Face face;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0)
        count = face->num_glyphs;
    LeaveCriticalSection(&freetype_cs);

    return count;
}

void freetype_get_glyphs(IDWriteFontFace4 *fontface, INT charmap, UINT32 const *codepoints, UINT32 count,
    UINT16 *glyphs)
{
    UINT32 i;

    EnterCriticalSection(&freetype_cs);
    for (i = 0; i < count; i++) {
        if (charmap == -1)
            glyphs[i] = pFTC_CMapCache_Lookup(cmap_cache, fontface, charmap, codepoints[i]);
        else {
            UINT32 codepoint = codepoints[i];
            /* special handling for symbol fonts */
            if (codepoint < 0x100) codepoint += 0xf000;
            glyphs[i] = pFTC_CMapCache_Lookup(cmap_cache, fontface, charmap, codepoint);
            if (!glyphs[i])
                glyphs[i] = pFTC_CMapCache_Lookup(cmap_cache, fontface, charmap, codepoint - 0xf000);
        }
    }
    LeaveCriticalSection(&freetype_cs);
}

BOOL freetype_has_kerning_pairs(IDWriteFontFace4 *fontface)
{
    BOOL has_kerning_pairs = FALSE;
    FT_Face face;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0)
        has_kerning_pairs = !!FT_HAS_KERNING(face);
    LeaveCriticalSection(&freetype_cs);

    return has_kerning_pairs;
}

INT32 freetype_get_kerning_pair_adjustment(IDWriteFontFace4 *fontface, UINT16 left, UINT16 right)
{
    INT32 adjustment = 0;
    FT_Face face;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0) {
        FT_Vector kern;
        if (FT_HAS_KERNING(face)) {
            pFT_Get_Kerning(face, left, right, FT_KERNING_UNSCALED, &kern);
            adjustment = kern.x;
        }
    }
    LeaveCriticalSection(&freetype_cs);

    return adjustment;
}

static inline void ft_matrix_from_dwrite_matrix(const DWRITE_MATRIX *m, FT_Matrix *ft_matrix)
{
    ft_matrix->xx =  m->m11 * 0x10000;
    ft_matrix->xy = -m->m21 * 0x10000;
    ft_matrix->yx = -m->m12 * 0x10000;
    ft_matrix->yy =  m->m22 * 0x10000;
}

/* Should be used only while holding 'freetype_cs' */
static BOOL is_face_scalable(IDWriteFontFace4 *fontface)
{
    FT_Face face;
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0)
        return FT_IS_SCALABLE(face);
    else
        return FALSE;
}

static BOOL get_glyph_transform(struct dwrite_glyphbitmap *bitmap, FT_Matrix *ret)
{
    FT_Matrix m;

    ret->xx = 1 << 16;
    ret->xy = 0;
    ret->yx = 0;
    ret->yy = 1 << 16;

    /* Some fonts provide mostly bitmaps and very few outlines, for example for .notdef.
       Disable transform if that's the case. */
    if (!is_face_scalable(bitmap->fontface) || (!bitmap->m && bitmap->simulations == 0))
        return FALSE;

    if (bitmap->simulations & DWRITE_FONT_SIMULATIONS_OBLIQUE) {
        m.xx =  1 << 16;
        m.xy = (1 << 16) / 3;
        m.yx =  0;
        m.yy =  1 << 16;
        pFT_Matrix_Multiply(&m, ret);
    }

    if (bitmap->m) {
        ft_matrix_from_dwrite_matrix(bitmap->m, &m);
        pFT_Matrix_Multiply(&m, ret);
    }

    return TRUE;
}

void freetype_get_glyph_bbox(struct dwrite_glyphbitmap *bitmap)
{
    FTC_ImageTypeRec imagetype;
    FT_BBox bbox = { 0 };
    BOOL needs_transform;
    FT_Glyph glyph;
    FT_Matrix m;

    EnterCriticalSection(&freetype_cs);

    needs_transform = get_glyph_transform(bitmap, &m);

    imagetype.face_id = bitmap->fontface;
    imagetype.width = 0;
    imagetype.height = bitmap->emsize;
    imagetype.flags = needs_transform ? FT_LOAD_NO_BITMAP : FT_LOAD_DEFAULT;

    if (pFTC_ImageCache_Lookup(image_cache, &imagetype, bitmap->index, &glyph, NULL) == 0) {
        if (needs_transform) {
            FT_Glyph glyph_copy;

            if (pFT_Glyph_Copy(glyph, &glyph_copy) == 0) {
                if (bitmap->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                    embolden_glyph(glyph_copy, bitmap->emsize);

                /* Includes oblique and user transform. */
                pFT_Glyph_Transform(glyph_copy, &m, NULL);
                pFT_Glyph_Get_CBox(glyph_copy, FT_GLYPH_BBOX_PIXELS, &bbox);
                pFT_Done_Glyph(glyph_copy);
            }
        }
        else
            pFT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_PIXELS, &bbox);
    }

    LeaveCriticalSection(&freetype_cs);

    /* flip Y axis */
    SetRect(&bitmap->bbox, bbox.xMin, -bbox.yMax, bbox.xMax, -bbox.yMin);
}

void freetype_get_design_glyph_bbox(IDWriteFontFace4 *fontface, UINT16 unitsperEm, UINT16 glyph, RECT *bbox)
{
    FTC_ScalerRec scaler;
    FT_Size size;

    scaler.face_id = fontface;
    scaler.width  = unitsperEm;
    scaler.height = unitsperEm;
    scaler.pixel = 1;
    scaler.x_res = 0;
    scaler.y_res = 0;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupSize(cache_manager, &scaler, &size) == 0) {
         if (pFT_Load_Glyph(size->face, glyph, FT_LOAD_NO_SCALE) == 0) {
             FT_Glyph_Metrics *metrics = &size->face->glyph->metrics;

             bbox->left = metrics->horiBearingX;
             bbox->right = bbox->left + metrics->horiAdvance;
             bbox->top = -metrics->horiBearingY;
             bbox->bottom = bbox->top + metrics->height;
         }
    }
    LeaveCriticalSection(&freetype_cs);
}

static BOOL freetype_get_aliased_glyph_bitmap(struct dwrite_glyphbitmap *bitmap, FT_Glyph glyph)
{
    const RECT *bbox = &bitmap->bbox;
    int width = bbox->right - bbox->left;
    int height = bbox->bottom - bbox->top;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_OutlineGlyph outline = (FT_OutlineGlyph)glyph;
        const FT_Outline *src = &outline->outline;
        FT_Bitmap ft_bitmap;
        FT_Outline copy;

        ft_bitmap.width = width;
        ft_bitmap.rows = height;
        ft_bitmap.pitch = bitmap->pitch;
        ft_bitmap.pixel_mode = FT_PIXEL_MODE_MONO;
        ft_bitmap.buffer = bitmap->buf;

        /* Note: FreeType will only set 'black' bits for us. */
        if (pFT_Outline_New(library, src->n_points, src->n_contours, &copy) == 0) {
            pFT_Outline_Copy(src, &copy);
            pFT_Outline_Translate(&copy, -bbox->left << 6, bbox->bottom << 6);
            pFT_Outline_Get_Bitmap(library, &copy, &ft_bitmap);
            pFT_Outline_Done(library, &copy);
        }
    }
    else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        FT_Bitmap *ft_bitmap = &((FT_BitmapGlyph)glyph)->bitmap;
        BYTE *src = ft_bitmap->buffer, *dst = bitmap->buf;
        int w = min(bitmap->pitch, (ft_bitmap->width + 7) >> 3);
        int h = min(height, ft_bitmap->rows);

        while (h--) {
            memcpy(dst, src, w);
            src += ft_bitmap->pitch;
            dst += bitmap->pitch;
        }
    }
    else
        FIXME("format %x not handled\n", glyph->format);

    return TRUE;
}

static BOOL freetype_get_aa_glyph_bitmap(struct dwrite_glyphbitmap *bitmap, FT_Glyph glyph)
{
    const RECT *bbox = &bitmap->bbox;
    int width = bbox->right - bbox->left;
    int height = bbox->bottom - bbox->top;
    BOOL ret = FALSE;

    if (glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
        FT_OutlineGlyph outline = (FT_OutlineGlyph)glyph;
        const FT_Outline *src = &outline->outline;
        FT_Bitmap ft_bitmap;
        FT_Outline copy;

        ft_bitmap.width = width;
        ft_bitmap.rows = height;
        ft_bitmap.pitch = bitmap->pitch;
        ft_bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
        ft_bitmap.buffer = bitmap->buf;

        /* Note: FreeType will only set 'black' bits for us. */
        if (pFT_Outline_New(library, src->n_points, src->n_contours, &copy) == 0) {
            pFT_Outline_Copy(src, &copy);
            pFT_Outline_Translate(&copy, -bbox->left << 6, bbox->bottom << 6);
            pFT_Outline_Get_Bitmap(library, &copy, &ft_bitmap);
            pFT_Outline_Done(library, &copy);
        }
    }
    else if (glyph->format == FT_GLYPH_FORMAT_BITMAP) {
        FT_Bitmap *ft_bitmap = &((FT_BitmapGlyph)glyph)->bitmap;
        BYTE *src = ft_bitmap->buffer, *dst = bitmap->buf;
        int w = min(bitmap->pitch, (ft_bitmap->width + 7) >> 3);
        int h = min(height, ft_bitmap->rows);

        while (h--) {
            memcpy(dst, src, w);
            src += ft_bitmap->pitch;
            dst += bitmap->pitch;
        }

        ret = TRUE;
    }
    else
        FIXME("format %x not handled\n", glyph->format);

    return ret;
}

BOOL freetype_get_glyph_bitmap(struct dwrite_glyphbitmap *bitmap)
{
    FTC_ImageTypeRec imagetype;
    BOOL needs_transform;
    BOOL ret = FALSE;
    FT_Glyph glyph;
    FT_Matrix m;

    EnterCriticalSection(&freetype_cs);

    needs_transform = get_glyph_transform(bitmap, &m);

    imagetype.face_id = bitmap->fontface;
    imagetype.width = 0;
    imagetype.height = bitmap->emsize;
    imagetype.flags = needs_transform ? FT_LOAD_NO_BITMAP : FT_LOAD_DEFAULT;

    if (pFTC_ImageCache_Lookup(image_cache, &imagetype, bitmap->index, &glyph, NULL) == 0) {
        FT_Glyph glyph_copy;

        if (needs_transform) {
            if (pFT_Glyph_Copy(glyph, &glyph_copy) == 0) {
                if (bitmap->simulations & DWRITE_FONT_SIMULATIONS_BOLD)
                    embolden_glyph(glyph_copy, bitmap->emsize);

                /* Includes oblique and user transform. */
                pFT_Glyph_Transform(glyph_copy, &m, NULL);
                glyph = glyph_copy;
            }
        }
        else
            glyph_copy = NULL;

        if (bitmap->aliased)
            ret = freetype_get_aliased_glyph_bitmap(bitmap, glyph);
        else
            ret = freetype_get_aa_glyph_bitmap(bitmap, glyph);

        if (glyph_copy)
            pFT_Done_Glyph(glyph_copy);
    }

    LeaveCriticalSection(&freetype_cs);

    return ret;
}

INT freetype_get_charmap_index(IDWriteFontFace4 *fontface, BOOL *is_symbol)
{
    INT charmap_index = -1;
    FT_Face face;

    *is_symbol = FALSE;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_Manager_LookupFace(cache_manager, fontface, &face) == 0) {
        TT_OS2 *os2 = pFT_Get_Sfnt_Table(face, ft_sfnt_os2);
        FT_Int i;

        if (os2) {
            FT_UInt dummy;
            if (os2->version == 0)
                *is_symbol = pFT_Get_First_Char(face, &dummy) >= 0x100;
            else
                *is_symbol = !!(os2->ulCodePageRange1 & FS_SYMBOL);
        }

        for (i = 0; i < face->num_charmaps; i++)
            if (face->charmaps[i]->encoding == FT_ENCODING_MS_SYMBOL) {
                *is_symbol = TRUE;
                charmap_index = i;
                break;
            }
    }
    LeaveCriticalSection(&freetype_cs);

    return charmap_index;
}

INT32 freetype_get_glyph_advance(IDWriteFontFace4 *fontface, FLOAT emSize, UINT16 index, DWRITE_MEASURING_MODE mode,
    BOOL *has_contours)
{
    FTC_ImageTypeRec imagetype;
    FT_Glyph glyph;
    INT32 advance;

    imagetype.face_id = fontface;
    imagetype.width = 0;
    imagetype.height = emSize;
    imagetype.flags = FT_LOAD_DEFAULT;
    if (mode == DWRITE_MEASURING_MODE_NATURAL)
        imagetype.flags |= FT_LOAD_NO_HINTING;

    EnterCriticalSection(&freetype_cs);
    if (pFTC_ImageCache_Lookup(image_cache, &imagetype, index, &glyph, NULL) == 0) {
        *has_contours = glyph->format == FT_GLYPH_FORMAT_OUTLINE && ((FT_OutlineGlyph)glyph)->outline.n_contours;
        advance = glyph->advance.x >> 16;
    }
    else {
        *has_contours = FALSE;
        advance = 0;
    }
    LeaveCriticalSection(&freetype_cs);

    return advance;
}

#else /* HAVE_FREETYPE */

BOOL init_freetype(void)
{
    return FALSE;
}

void release_freetype(void)
{
}

void freetype_notify_cacheremove(IDWriteFontFace4 *fontface)
{
}

HRESULT freetype_get_design_glyph_metrics(IDWriteFontFace4 *fontface, UINT16 unitsperEm, UINT16 glyph, DWRITE_GLYPH_METRICS *ret)
{
    return E_NOTIMPL;
}

BOOL freetype_is_monospaced(IDWriteFontFace4 *fontface)
{
    return FALSE;
}

HRESULT freetype_get_glyphrun_outline(IDWriteFontFace4 *fontface, FLOAT emSize, UINT16 const *glyphs, FLOAT const *advances,
    DWRITE_GLYPH_OFFSET const *offsets, UINT32 count, BOOL is_rtl, IDWriteGeometrySink *sink)
{
    return E_NOTIMPL;
}

UINT16 freetype_get_glyphcount(IDWriteFontFace4 *fontface)
{
    return 0;
}

void freetype_get_glyphs(IDWriteFontFace4 *fontface, INT charmap, UINT32 const *codepoints, UINT32 count,
    UINT16 *glyphs)
{
    memset(glyphs, 0, count * sizeof(*glyphs));
}

BOOL freetype_has_kerning_pairs(IDWriteFontFace4 *fontface)
{
    return FALSE;
}

INT32 freetype_get_kerning_pair_adjustment(IDWriteFontFace4 *fontface, UINT16 left, UINT16 right)
{
    return 0;
}

void freetype_get_glyph_bbox(struct dwrite_glyphbitmap *bitmap)
{
    SetRectEmpty(&bitmap->bbox);
}

void freetype_get_design_glyph_bbox(IDWriteFontFace4 *fontface, UINT16 unitsperEm, UINT16 glyph, RECT *bbox)
{
    SetRectEmpty(bbox);
}

BOOL freetype_get_glyph_bitmap(struct dwrite_glyphbitmap *bitmap)
{
    return FALSE;
}

INT freetype_get_charmap_index(IDWriteFontFace4 *fontface, BOOL *is_symbol)
{
    *is_symbol = FALSE;
    return -1;
}

INT32 freetype_get_glyph_advance(IDWriteFontFace4 *fontface, FLOAT emSize, UINT16 index, DWRITE_MEASURING_MODE mode,
    BOOL *has_contours)
{
    *has_contours = FALSE;
    return 0;
}

#endif /* HAVE_FREETYPE */
