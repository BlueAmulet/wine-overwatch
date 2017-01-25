/*
 * Copyright 2009 Vincent Povirk
 * Copyright 2016 Dmitry Timoshkov
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

#include <stdarg.h>
#include <math.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "objbase.h"

#include "wincodecs_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wincodecs);

struct FormatConverter;

enum pixelformat {
    format_1bppIndexed,
    format_2bppIndexed,
    format_4bppIndexed,
    format_8bppIndexed,
    format_BlackWhite,
    format_2bppGray,
    format_4bppGray,
    format_8bppGray,
    format_16bppGray,
    format_16bppBGR555,
    format_16bppBGR565,
    format_16bppBGRA5551,
    format_24bppBGR,
    format_24bppRGB,
    format_32bppGrayFloat,
    format_32bppBGR,
    format_32bppBGRA,
    format_32bppPBGRA,
    format_48bppRGB,
    format_64bppRGBA,
    format_32bppCMYK,
};

typedef HRESULT (*copyfunc)(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format);

struct pixelformatinfo {
    enum pixelformat format;
    const WICPixelFormatGUID *guid;
    copyfunc copy_function;
};

typedef struct FormatConverter {
    IWICFormatConverter IWICFormatConverter_iface;
    LONG ref;
    IWICBitmapSource *source;
    const struct pixelformatinfo *dst_format, *src_format;
    WICBitmapDitherType dither;
    double alpha_threshold;
    WICBitmapPaletteType palette_type;
    CRITICAL_SECTION lock; /* must be held when initialized */
} FormatConverter;

/* https://www.w3.org/Graphics/Color/srgb */
static inline float from_sRGB_component(float f)
{
    if (f <= 0.04045f) return f / 12.92f;
    return powf((f + 0.055f) / 1.055f, 2.4f);
}

static inline float to_sRGB_component(float f)
{
    if (f <= 0.0031308f) return 12.92f * f;
    return 1.055f * powf(f, 1.0f/2.4f) - 0.055f;
}

#if 0 /* FIXME: enable once needed */
static void from_sRGB(BYTE *bgr)
{
    float r, g, b;

    r = bgr[2] / 255.0f;
    g = bgr[1] / 255.0f;
    b = bgr[0] / 255.0f;

    r = from_sRGB_component(r);
    g = from_sRGB_component(g);
    b = from_sRGB_component(b);

    bgr[2] = (BYTE)(r * 255.0f);
    bgr[1] = (BYTE)(g * 255.0f);
    bgr[0] = (BYTE)(b * 255.0f);
}

static void to_sRGB(BYTE *bgr)
{
    float r, g, b;

    r = bgr[2] / 255.0f;
    g = bgr[1] / 255.0f;
    b = bgr[0] / 255.0f;

    r = to_sRGB_component(r);
    g = to_sRGB_component(g);
    b = to_sRGB_component(b);

    bgr[2] = (BYTE)(r * 255.0f);
    bgr[1] = (BYTE)(g * 255.0f);
    bgr[0] = (BYTE)(b * 255.0f);
}
#endif

static inline FormatConverter *impl_from_IWICFormatConverter(IWICFormatConverter *iface)
{
    return CONTAINING_RECORD(iface, FormatConverter, IWICFormatConverter_iface);
}

static HRESULT copypixels_to_32bppBGRA(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    switch (source_format)
    {
    case format_1bppIndexed:
    case format_BlackWhite:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;
            WICColor colors[2];
            IWICPalette *palette;
            UINT actualcolors;

            res = PaletteImpl_Create(&palette);
            if (FAILED(res)) return res;

            if (source_format == format_1bppIndexed)
                res = IWICBitmapSource_CopyPalette(This->source, palette);
            else
                res = IWICPalette_InitializePredefined(palette, WICBitmapPaletteTypeFixedBW, FALSE);

            if (SUCCEEDED(res))
                res = IWICPalette_GetColors(palette, 2, colors, &actualcolors);

            IWICPalette_Release(palette);
            if (FAILED(res)) return res;

            srcstride = (prc->Width+7)/8;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x+=8) {
                        BYTE srcval;
                        srcval=*srcbyte++;
                        *dstpixel++ = colors[srcval>>7&1];
                        if (x+1 < prc->Width) *dstpixel++ = colors[srcval>>6&1];
                        if (x+2 < prc->Width) *dstpixel++ = colors[srcval>>5&1];
                        if (x+3 < prc->Width) *dstpixel++ = colors[srcval>>4&1];
                        if (x+4 < prc->Width) *dstpixel++ = colors[srcval>>3&1];
                        if (x+5 < prc->Width) *dstpixel++ = colors[srcval>>2&1];
                        if (x+6 < prc->Width) *dstpixel++ = colors[srcval>>1&1];
                        if (x+7 < prc->Width) *dstpixel++ = colors[srcval&1];
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_2bppIndexed:
    case format_2bppGray:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;
            WICColor colors[4];
            IWICPalette *palette;
            UINT actualcolors;

            res = PaletteImpl_Create(&palette);
            if (FAILED(res)) return res;

            if (source_format == format_2bppIndexed)
                res = IWICBitmapSource_CopyPalette(This->source, palette);
            else
                res = IWICPalette_InitializePredefined(palette, WICBitmapPaletteTypeFixedGray4, FALSE);

            if (SUCCEEDED(res))
                res = IWICPalette_GetColors(palette, 4, colors, &actualcolors);

            IWICPalette_Release(palette);
            if (FAILED(res)) return res;

            srcstride = (prc->Width+3)/4;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x+=4) {
                        BYTE srcval;
                        srcval=*srcbyte++;
                        *dstpixel++ = colors[srcval>>6];
                        if (x+1 < prc->Width) *dstpixel++ = colors[srcval>>4&0x3];
                        if (x+2 < prc->Width) *dstpixel++ = colors[srcval>>2&0x3];
                        if (x+3 < prc->Width) *dstpixel++ = colors[srcval&0x3];
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_4bppIndexed:
    case format_4bppGray:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;
            WICColor colors[16];
            IWICPalette *palette;
            UINT actualcolors;

            res = PaletteImpl_Create(&palette);
            if (FAILED(res)) return res;

            if (source_format == format_4bppIndexed)
                res = IWICBitmapSource_CopyPalette(This->source, palette);
            else
                res = IWICPalette_InitializePredefined(palette, WICBitmapPaletteTypeFixedGray16, FALSE);

            if (SUCCEEDED(res))
                res = IWICPalette_GetColors(palette, 16, colors, &actualcolors);

            IWICPalette_Release(palette);
            if (FAILED(res)) return res;

            srcstride = (prc->Width+1)/2;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x+=2) {
                        BYTE srcval;
                        srcval=*srcbyte++;
                        *dstpixel++ = colors[srcval>>4];
                        if (x+1 < prc->Width) *dstpixel++ = colors[srcval&0xf];
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_8bppGray:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++)
                    {
                        *dstpixel++ = 0xff000000|(*srcbyte<<16)|(*srcbyte<<8)|*srcbyte;
                        srcbyte++;
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_8bppIndexed:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;
            WICColor colors[256];
            IWICPalette *palette;
            UINT actualcolors;

            res = PaletteImpl_Create(&palette);
            if (FAILED(res)) return res;

            res = IWICBitmapSource_CopyPalette(This->source, palette);
            if (SUCCEEDED(res))
                res = IWICPalette_GetColors(palette, 256, colors, &actualcolors);

            IWICPalette_Release(palette);

            if (FAILED(res)) return res;

            srcstride = prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++)
                        *dstpixel++ = colors[*srcbyte++];
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_16bppGray:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcbyte;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = prc->Width * 2;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcbyte = srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++)
                    {
                        *dstpixel++ = 0xff000000|(*srcbyte<<16)|(*srcbyte<<8)|*srcbyte;
                        srcbyte+=2;
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_16bppBGR555:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const WORD *srcpixel;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = 2 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=(const WORD*)srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++) {
                        WORD srcval;
                        srcval=*srcpixel++;
                        *dstpixel++=0xff000000 | /* constant 255 alpha */
                                    ((srcval << 9) & 0xf80000) | /* r */
                                    ((srcval << 4) & 0x070000) | /* r - 3 bits */
                                    ((srcval << 6) & 0x00f800) | /* g */
                                    ((srcval << 1) & 0x000700) | /* g - 3 bits */
                                    ((srcval << 3) & 0x0000f8) | /* b */
                                    ((srcval >> 2) & 0x000007);  /* b - 3 bits */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_16bppBGR565:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const WORD *srcpixel;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = 2 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=(const WORD*)srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++) {
                        WORD srcval;
                        srcval=*srcpixel++;
                        *dstpixel++=0xff000000 | /* constant 255 alpha */
                                    ((srcval << 8) & 0xf80000) | /* r */
                                    ((srcval << 3) & 0x070000) | /* r - 3 bits */
                                    ((srcval << 5) & 0x00fc00) | /* g */
                                    ((srcval >> 1) & 0x000300) | /* g - 2 bits */
                                    ((srcval << 3) & 0x0000f8) | /* b */
                                    ((srcval >> 2) & 0x000007);  /* b - 3 bits */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_16bppBGRA5551:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const WORD *srcpixel;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = 2 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=(const WORD*)srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++) {
                        WORD srcval;
                        srcval=*srcpixel++;
                        *dstpixel++=((srcval & 0x8000) ? 0xff000000 : 0) | /* alpha */
                                    ((srcval << 9) & 0xf80000) | /* r */
                                    ((srcval << 4) & 0x070000) | /* r - 3 bits */
                                    ((srcval << 6) & 0x00f800) | /* g */
                                    ((srcval << 1) & 0x000700) | /* g - 3 bits */
                                    ((srcval << 3) & 0x0000f8) | /* b */
                                    ((srcval >> 2) & 0x000007);  /* b - 3 bits */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_24bppBGR:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            BYTE *dstpixel;

            srcstride = 3 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=dstrow;
                    for (x=0; x<prc->Width; x++) {
                        *dstpixel++=*srcpixel++; /* blue */
                        *dstpixel++=*srcpixel++; /* green */
                        *dstpixel++=*srcpixel++; /* red */
                        *dstpixel++=255; /* alpha */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_24bppRGB:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            BYTE *dstpixel;
            BYTE tmppixel[3];

            srcstride = 3 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=dstrow;
                    for (x=0; x<prc->Width; x++) {
                        tmppixel[0]=*srcpixel++; /* red */
                        tmppixel[1]=*srcpixel++; /* green */
                        tmppixel[2]=*srcpixel++; /* blue */

                        *dstpixel++=tmppixel[2]; /* blue */
                        *dstpixel++=tmppixel[1]; /* green */
                        *dstpixel++=tmppixel[0]; /* red */
                        *dstpixel++=255; /* alpha */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_32bppBGR:
        if (prc)
        {
            HRESULT res;
            INT x, y;

            res = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            if (FAILED(res)) return res;

            /* set all alpha values to 255 */
            for (y=0; y<prc->Height; y++)
                for (x=0; x<prc->Width; x++)
                    pbBuffer[cbStride*y+4*x+3] = 0xff;
        }
        return S_OK;
    case format_32bppBGRA:
        if (prc)
            return IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
        return S_OK;
    case format_32bppPBGRA:
        if (prc)
        {
            HRESULT res;
            INT x, y;

            res = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            if (FAILED(res)) return res;

            for (y=0; y<prc->Height; y++)
                for (x=0; x<prc->Width; x++)
                {
                    BYTE alpha = pbBuffer[cbStride*y+4*x+3];
                    if (alpha != 0 && alpha != 255)
                    {
                        pbBuffer[cbStride*y+4*x] = pbBuffer[cbStride*y+4*x] * 255 / alpha;
                        pbBuffer[cbStride*y+4*x+1] = pbBuffer[cbStride*y+4*x+1] * 255 / alpha;
                        pbBuffer[cbStride*y+4*x+2] = pbBuffer[cbStride*y+4*x+2] * 255 / alpha;
                    }
                }
        }
        return S_OK;
    case format_48bppRGB:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = 6 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++) {
                        BYTE red, green, blue;
                        red = *srcpixel++; srcpixel++;
                        green = *srcpixel++; srcpixel++;
                        blue = *srcpixel++; srcpixel++;
                        *dstpixel++=0xff000000|red<<16|green<<8|blue;
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_64bppRGBA:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            DWORD *dstpixel;

            srcstride = 8 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=(DWORD*)dstrow;
                    for (x=0; x<prc->Width; x++) {
                        BYTE red, green, blue, alpha;
                        red = *srcpixel++; srcpixel++;
                        green = *srcpixel++; srcpixel++;
                        blue = *srcpixel++; srcpixel++;
                        alpha = *srcpixel++; srcpixel++;
                        *dstpixel++=alpha<<24|red<<16|green<<8|blue;
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    case format_32bppCMYK:
        if (prc)
        {
            HRESULT res;
            UINT x, y;

            res = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            if (FAILED(res)) return res;

            for (y=0; y<prc->Height; y++)
                for (x=0; x<prc->Width; x++)
                {
                    BYTE *pixel = pbBuffer+cbStride*y+4*x;
                    BYTE c=pixel[0], m=pixel[1], y=pixel[2], k=pixel[3];
                    pixel[0] = (255-y)*(255-k)/255; /* blue */
                    pixel[1] = (255-m)*(255-k)/255; /* green */
                    pixel[2] = (255-c)*(255-k)/255; /* red */
                    pixel[3] = 255; /* alpha */
                }
        }
        return S_OK;
    default:
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
}

static HRESULT copypixels_to_32bppBGR(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    switch (source_format)
    {
    case format_32bppBGR:
    case format_32bppBGRA:
    case format_32bppPBGRA:
        if (prc)
            return IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
        return S_OK;
    default:
        return copypixels_to_32bppBGRA(This, prc, cbStride, cbBufferSize, pbBuffer, source_format);
    }
}

static HRESULT copypixels_to_32bppPBGRA(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    HRESULT hr;

    switch (source_format)
    {
    case format_32bppPBGRA:
        if (prc)
            return IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
        return S_OK;
    default:
        hr = copypixels_to_32bppBGRA(This, prc, cbStride, cbBufferSize, pbBuffer, source_format);
        if (SUCCEEDED(hr) && prc)
        {
            INT x, y;

            for (y=0; y<prc->Height; y++)
                for (x=0; x<prc->Width; x++)
                {
                    BYTE alpha = pbBuffer[cbStride*y+4*x+3];
                    if (alpha != 255)
                    {
                        pbBuffer[cbStride*y+4*x] = pbBuffer[cbStride*y+4*x] * alpha / 255;
                        pbBuffer[cbStride*y+4*x+1] = pbBuffer[cbStride*y+4*x+1] * alpha / 255;
                        pbBuffer[cbStride*y+4*x+2] = pbBuffer[cbStride*y+4*x+2] * alpha / 255;
                    }
                }
        }
        return hr;
    }
}

static HRESULT copypixels_to_24bppBGR(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    HRESULT hr;

    switch (source_format)
    {
    case format_24bppBGR:
    case format_24bppRGB:
        if (prc)
        {
            hr = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            if (SUCCEEDED(hr) && source_format == format_24bppRGB)
              reverse_bgr8(3, pbBuffer, prc->Width, prc->Height, cbStride);
            return hr;
        }
        return S_OK;
    case format_32bppBGR:
    case format_32bppBGRA:
    case format_32bppPBGRA:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            BYTE *dstpixel;

            srcstride = 4 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=dstrow;
                    for (x=0; x<prc->Width; x++) {
                        *dstpixel++=*srcpixel++; /* blue */
                        *dstpixel++=*srcpixel++; /* green */
                        *dstpixel++=*srcpixel++; /* red */
                        srcpixel++; /* alpha */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;

    case format_32bppGrayFloat:
        if (prc)
        {
            BYTE *srcdata;
            UINT srcstride, srcdatasize;

            srcstride = 4 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            hr = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(hr))
            {
                INT x, y;
                BYTE *src = srcdata, *dst = pbBuffer;

                for (y = 0; y < prc->Height; y++)
                {
                    float *gray_float = (float *)src;
                    BYTE *bgr = dst;

                    for (x = 0; x < prc->Width; x++)
                    {
                        BYTE gray = (BYTE)floorf(to_sRGB_component(gray_float[x]) * 255.0f + 0.51f);
                        *bgr++ = gray;
                        *bgr++ = gray;
                        *bgr++ = gray;
                    }
                    src += srcstride;
                    dst += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return hr;
        }
        return S_OK;

    default:
        FIXME("Unimplemented conversion path!\n");
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
}

static HRESULT copypixels_to_24bppRGB(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    HRESULT hr;

    switch (source_format)
    {
    case format_24bppBGR:
    case format_24bppRGB:
        if (prc)
        {
            hr = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            if (SUCCEEDED(hr) && source_format == format_24bppBGR)
              reverse_bgr8(3, pbBuffer, prc->Width, prc->Height, cbStride);
            return hr;
        }
        return S_OK;
    case format_32bppBGR:
    case format_32bppBGRA:
    case format_32bppPBGRA:
        if (prc)
        {
            HRESULT res;
            INT x, y;
            BYTE *srcdata;
            UINT srcstride, srcdatasize;
            const BYTE *srcrow;
            const BYTE *srcpixel;
            BYTE *dstrow;
            BYTE *dstpixel;
            BYTE tmppixel[3];

            srcstride = 4 * prc->Width;
            srcdatasize = srcstride * prc->Height;

            srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
            if (!srcdata) return E_OUTOFMEMORY;

            res = IWICBitmapSource_CopyPixels(This->source, prc, srcstride, srcdatasize, srcdata);

            if (SUCCEEDED(res))
            {
                srcrow = srcdata;
                dstrow = pbBuffer;
                for (y=0; y<prc->Height; y++) {
                    srcpixel=srcrow;
                    dstpixel=dstrow;
                    for (x=0; x<prc->Width; x++) {
                        tmppixel[0]=*srcpixel++; /* blue */
                        tmppixel[1]=*srcpixel++; /* green */
                        tmppixel[2]=*srcpixel++; /* red */
                        srcpixel++; /* alpha */

                        *dstpixel++=tmppixel[2]; /* red */
                        *dstpixel++=tmppixel[1]; /* green */
                        *dstpixel++=tmppixel[0]; /* blue */
                    }
                    srcrow += srcstride;
                    dstrow += cbStride;
                }
            }

            HeapFree(GetProcessHeap(), 0, srcdata);

            return res;
        }
        return S_OK;
    default:
        FIXME("Unimplemented conversion path!\n");
        return WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }
}

static HRESULT copypixels_to_32bppGrayFloat(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    HRESULT hr;

    switch (source_format)
    {
    case format_32bppBGR:
    case format_32bppBGRA:
    case format_32bppPBGRA:
    case format_32bppGrayFloat:
        if (prc)
        {
            hr = IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);
            break;
        }
        return S_OK;

    default:
        hr = copypixels_to_32bppBGRA(This, prc, cbStride, cbBufferSize, pbBuffer, source_format);
        break;
    }

    if (SUCCEEDED(hr) && prc && source_format != format_32bppGrayFloat)
    {
        INT x, y;
        BYTE *p = pbBuffer;

        for (y = 0; y < prc->Height; y++)
        {
            BYTE *bgr = p;
            for (x = 0; x < prc->Width; x++)
            {
                float gray = (bgr[2] * 0.2126f + bgr[1] * 0.7152f + bgr[0] * 0.0722f) / 255.0f;
                *(float *)bgr = gray;
                bgr += 4;
            }
            p += cbStride;
        }
    }
    return hr;
}

static HRESULT copypixels_to_8bppGray(struct FormatConverter *This, const WICRect *prc,
    UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer, enum pixelformat source_format)
{
    HRESULT hr;
    BYTE *srcdata;
    UINT srcstride, srcdatasize;

    if (source_format == format_8bppGray)
    {
        if (prc)
            return IWICBitmapSource_CopyPixels(This->source, prc, cbStride, cbBufferSize, pbBuffer);

        return S_OK;
    }

    srcstride = 3 * prc->Width;
    srcdatasize = srcstride * prc->Height;

    srcdata = HeapAlloc(GetProcessHeap(), 0, srcdatasize);
    if (!srcdata) return E_OUTOFMEMORY;

    hr = copypixels_to_24bppBGR(This, prc, srcstride, srcdatasize, srcdata, source_format);
    if (SUCCEEDED(hr) && prc)
    {
        INT x, y;
        BYTE *src = srcdata, *dst = pbBuffer;

        for (y = 0; y < prc->Height; y++)
        {
            BYTE *bgr = src;

            for (x = 0; x < prc->Width; x++)
            {
                float gray = (bgr[2] * 0.2126f + bgr[1] * 0.7152f + bgr[0] * 0.0722f) / 255.0f;

                /* conversion from 32bppGrayFloat to 24bppBGR has already applied sRGB gamma */
                if (source_format == format_32bppGrayFloat)
                    gray *= 255.0f;
                else
                    gray = to_sRGB_component(gray) * 255.0f;

                dst[x] = (BYTE)floorf(gray + 0.51f);
                bgr += 3;
            }
            src += srcstride;
            dst += cbStride;
        }
    }

    HeapFree(GetProcessHeap(), 0, srcdata);
    return hr;
}

static const struct pixelformatinfo supported_formats[] = {
    {format_1bppIndexed, &GUID_WICPixelFormat1bppIndexed, NULL},
    {format_2bppIndexed, &GUID_WICPixelFormat2bppIndexed, NULL},
    {format_4bppIndexed, &GUID_WICPixelFormat4bppIndexed, NULL},
    {format_8bppIndexed, &GUID_WICPixelFormat8bppIndexed, NULL},
    {format_BlackWhite, &GUID_WICPixelFormatBlackWhite, NULL},
    {format_2bppGray, &GUID_WICPixelFormat2bppGray, NULL},
    {format_4bppGray, &GUID_WICPixelFormat4bppGray, NULL},
    {format_8bppGray, &GUID_WICPixelFormat8bppGray, copypixels_to_8bppGray},
    {format_16bppGray, &GUID_WICPixelFormat16bppGray, NULL},
    {format_16bppBGR555, &GUID_WICPixelFormat16bppBGR555, NULL},
    {format_16bppBGR565, &GUID_WICPixelFormat16bppBGR565, NULL},
    {format_16bppBGRA5551, &GUID_WICPixelFormat16bppBGRA5551, NULL},
    {format_24bppBGR, &GUID_WICPixelFormat24bppBGR, copypixels_to_24bppBGR},
    {format_24bppRGB, &GUID_WICPixelFormat24bppRGB, copypixels_to_24bppRGB},
    {format_32bppGrayFloat, &GUID_WICPixelFormat32bppGrayFloat, copypixels_to_32bppGrayFloat},
    {format_32bppBGR, &GUID_WICPixelFormat32bppBGR, copypixels_to_32bppBGR},
    {format_32bppBGRA, &GUID_WICPixelFormat32bppBGRA, copypixels_to_32bppBGRA},
    {format_32bppPBGRA, &GUID_WICPixelFormat32bppPBGRA, copypixels_to_32bppPBGRA},
    {format_48bppRGB, &GUID_WICPixelFormat48bppRGB, NULL},
    {format_64bppRGBA, &GUID_WICPixelFormat64bppRGBA, NULL},
    {format_32bppCMYK, &GUID_WICPixelFormat32bppCMYK, NULL},
    {0}
};

static const struct pixelformatinfo *get_formatinfo(const WICPixelFormatGUID *format)
{
    UINT i;

    for (i=0; supported_formats[i].guid; i++)
        if (IsEqualGUID(supported_formats[i].guid, format)) return &supported_formats[i];

    return NULL;
}

static HRESULT WINAPI FormatConverter_QueryInterface(IWICFormatConverter *iface, REFIID iid,
    void **ppv)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    TRACE("(%p,%s,%p)\n", iface, debugstr_guid(iid), ppv);

    if (!ppv) return E_INVALIDARG;

    if (IsEqualIID(&IID_IUnknown, iid) ||
        IsEqualIID(&IID_IWICBitmapSource, iid) ||
        IsEqualIID(&IID_IWICFormatConverter, iid))
    {
        *ppv = &This->IWICFormatConverter_iface;
    }
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }

    IUnknown_AddRef((IUnknown*)*ppv);
    return S_OK;
}

static ULONG WINAPI FormatConverter_AddRef(IWICFormatConverter *iface)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) refcount=%u\n", iface, ref);

    return ref;
}

static ULONG WINAPI FormatConverter_Release(IWICFormatConverter *iface)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) refcount=%u\n", iface, ref);

    if (ref == 0)
    {
        This->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->lock);
        if (This->source) IWICBitmapSource_Release(This->source);
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

static HRESULT WINAPI FormatConverter_GetSize(IWICFormatConverter *iface,
    UINT *puiWidth, UINT *puiHeight)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);

    TRACE("(%p,%p,%p)\n", iface, puiWidth, puiHeight);

    if (This->source)
        return IWICBitmapSource_GetSize(This->source, puiWidth, puiHeight);
    else
        return WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT WINAPI FormatConverter_GetPixelFormat(IWICFormatConverter *iface,
    WICPixelFormatGUID *pPixelFormat)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);

    TRACE("(%p,%p)\n", iface, pPixelFormat);

    if (This->source)
        memcpy(pPixelFormat, This->dst_format->guid, sizeof(GUID));
    else
        return WINCODEC_ERR_NOTINITIALIZED;

    return S_OK;
}

static HRESULT WINAPI FormatConverter_GetResolution(IWICFormatConverter *iface,
    double *pDpiX, double *pDpiY)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);

    TRACE("(%p,%p,%p)\n", iface, pDpiX, pDpiY);

    if (This->source)
        return IWICBitmapSource_GetResolution(This->source, pDpiX, pDpiY);
    else
        return WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT WINAPI FormatConverter_CopyPalette(IWICFormatConverter *iface,
    IWICPalette *pIPalette)
{
    FIXME("(%p,%p): stub\n", iface, pIPalette);
    return E_NOTIMPL;
}

static HRESULT WINAPI FormatConverter_CopyPixels(IWICFormatConverter *iface,
    const WICRect *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    WICRect rc;
    HRESULT hr;
    TRACE("(%p,%p,%u,%u,%p)\n", iface, prc, cbStride, cbBufferSize, pbBuffer);

    if (This->source)
    {
        if (!prc)
        {
            UINT width, height;
            hr = IWICBitmapSource_GetSize(This->source, &width, &height);
            if (FAILED(hr)) return hr;
            rc.X = 0;
            rc.Y = 0;
            rc.Width = width;
            rc.Height = height;
            prc = &rc;
        }

        return This->dst_format->copy_function(This, prc, cbStride, cbBufferSize,
            pbBuffer, This->src_format->format);
    }
    else
        return WINCODEC_ERR_NOTINITIALIZED;
}

static HRESULT WINAPI FormatConverter_Initialize(IWICFormatConverter *iface,
    IWICBitmapSource *pISource, REFWICPixelFormatGUID dstFormat, WICBitmapDitherType dither,
    IWICPalette *pIPalette, double alphaThresholdPercent, WICBitmapPaletteType paletteTranslate)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    const struct pixelformatinfo *srcinfo, *dstinfo;
    static INT fixme=0;
    GUID srcFormat;
    HRESULT res=S_OK;

    TRACE("(%p,%p,%s,%u,%p,%0.1f,%u)\n", iface, pISource, debugstr_guid(dstFormat),
        dither, pIPalette, alphaThresholdPercent, paletteTranslate);

    if (pIPalette && !fixme++) FIXME("ignoring palette\n");

    EnterCriticalSection(&This->lock);

    if (This->source)
    {
        res = WINCODEC_ERR_WRONGSTATE;
        goto end;
    }

    res = IWICBitmapSource_GetPixelFormat(pISource, &srcFormat);
    if (FAILED(res)) goto end;

    srcinfo = get_formatinfo(&srcFormat);
    if (!srcinfo)
    {
        res = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        FIXME("Unsupported source format %s\n", debugstr_guid(&srcFormat));
        goto end;
    }

    dstinfo = get_formatinfo(dstFormat);
    if (!dstinfo)
    {
        res = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
        FIXME("Unsupported destination format %s\n", debugstr_guid(dstFormat));
        goto end;
    }

    if (dstinfo->copy_function)
    {
        IWICBitmapSource_AddRef(pISource);
        This->src_format = srcinfo;
        This->dst_format = dstinfo;
        This->dither = dither;
        This->alpha_threshold = alphaThresholdPercent;
        This->palette_type = paletteTranslate;
        This->source = pISource;
    }
    else
    {
        FIXME("Unsupported conversion %s -> %s\n", debugstr_guid(&srcFormat), debugstr_guid(dstFormat));
        res = WINCODEC_ERR_UNSUPPORTEDOPERATION;
    }

end:

    LeaveCriticalSection(&This->lock);

    return res;
}

static HRESULT WINAPI FormatConverter_CanConvert(IWICFormatConverter *iface,
    REFWICPixelFormatGUID srcPixelFormat, REFWICPixelFormatGUID dstPixelFormat,
    BOOL *pfCanConvert)
{
    FormatConverter *This = impl_from_IWICFormatConverter(iface);
    const struct pixelformatinfo *srcinfo, *dstinfo;

    TRACE("(%p,%s,%s,%p)\n", iface, debugstr_guid(srcPixelFormat),
        debugstr_guid(dstPixelFormat), pfCanConvert);

    srcinfo = get_formatinfo(srcPixelFormat);
    if (!srcinfo)
    {
        FIXME("Unsupported source format %s\n", debugstr_guid(srcPixelFormat));
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }

    dstinfo = get_formatinfo(dstPixelFormat);
    if (!dstinfo)
    {
        FIXME("Unsupported destination format %s\n", debugstr_guid(dstPixelFormat));
        return WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
    }

    if (dstinfo->copy_function &&
        SUCCEEDED(dstinfo->copy_function(This, NULL, 0, 0, NULL, dstinfo->format)))
        *pfCanConvert = TRUE;
    else
    {
        FIXME("Unsupported conversion %s -> %s\n", debugstr_guid(srcPixelFormat), debugstr_guid(dstPixelFormat));
        *pfCanConvert = FALSE;
    }

    return S_OK;
}

static const IWICFormatConverterVtbl FormatConverter_Vtbl = {
    FormatConverter_QueryInterface,
    FormatConverter_AddRef,
    FormatConverter_Release,
    FormatConverter_GetSize,
    FormatConverter_GetPixelFormat,
    FormatConverter_GetResolution,
    FormatConverter_CopyPalette,
    FormatConverter_CopyPixels,
    FormatConverter_Initialize,
    FormatConverter_CanConvert
};

HRESULT FormatConverter_CreateInstance(REFIID iid, void** ppv)
{
    FormatConverter *This;
    HRESULT ret;

    TRACE("(%s,%p)\n", debugstr_guid(iid), ppv);

    *ppv = NULL;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(FormatConverter));
    if (!This) return E_OUTOFMEMORY;

    This->IWICFormatConverter_iface.lpVtbl = &FormatConverter_Vtbl;
    This->ref = 1;
    This->source = NULL;
    InitializeCriticalSection(&This->lock);
    This->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": FormatConverter.lock");

    ret = IWICFormatConverter_QueryInterface(&This->IWICFormatConverter_iface, iid, ppv);
    IWICFormatConverter_Release(&This->IWICFormatConverter_iface);

    return ret;
}
