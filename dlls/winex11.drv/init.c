/*
 * X11 graphics driver initialisation functions
 *
 * Copyright 1996 Alexandre Julliard
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
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "x11drv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(x11drv);

Display *gdi_display;  /* display to use for all GDI functions */

/* a few dynamic device caps */
static int log_pixels_x;  /* pixels per logical inch in x direction */
static int log_pixels_y;  /* pixels per logical inch in y direction */
static int palette_size;

static Pixmap stock_bitmap_pixmap;  /* phys bitmap for the default stock bitmap */

static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

static const WCHAR dpi_key_name[] = {'S','o','f','t','w','a','r','e','\\','F','o','n','t','s','\0'};
static const WCHAR dpi_value_name[] = {'L','o','g','P','i','x','e','l','s','\0'};

static const struct gdi_dc_funcs x11drv_funcs;
static const struct gdi_dc_funcs *xrender_funcs;

/******************************************************************************
 *      get_dpi
 *
 * get the dpi from the registry
 */
static DWORD get_dpi( void )
{
    DWORD dpi = 96;
    HKEY hkey;

    if (RegOpenKeyW(HKEY_CURRENT_CONFIG, dpi_key_name, &hkey) == ERROR_SUCCESS)
    {
        DWORD type, size, new_dpi;

        size = sizeof(new_dpi);
        if(RegQueryValueExW(hkey, dpi_value_name, NULL, &type, (void *)&new_dpi, &size) == ERROR_SUCCESS)
        {
            if(type == REG_DWORD && new_dpi != 0)
                dpi = new_dpi;
        }
        RegCloseKey(hkey);
    }
    return dpi;
}

/**********************************************************************
 *	     device_init
 *
 * Perform initializations needed upon creation of the first device.
 */
static BOOL WINAPI device_init( INIT_ONCE *once, void *param, void **context )
{
    /* Initialize XRender */
    xrender_funcs = X11DRV_XRender_Init();

    /* Init Xcursor */
    X11DRV_Xcursor_Init();

    palette_size = X11DRV_PALETTE_Init();

    stock_bitmap_pixmap = XCreatePixmap( gdi_display, root_window, 1, 1, 1 );

    /* Initialize device caps */
    log_pixels_x = log_pixels_y = get_dpi();
    return TRUE;
}


static X11DRV_PDEVICE *create_x11_physdev( Drawable drawable )
{
    X11DRV_PDEVICE *physDev;

    InitOnceExecuteOnce( &init_once, device_init, NULL, NULL );

    if (!(physDev = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*physDev) ))) return NULL;

    physDev->drawable = drawable;
    physDev->gc = XCreateGC( gdi_display, drawable, 0, NULL );
    XSetGraphicsExposures( gdi_display, physDev->gc, False );
    XSetSubwindowMode( gdi_display, physDev->gc, IncludeInferiors );
    XFlush( gdi_display );
    return physDev;
}

/**********************************************************************
 *	     X11DRV_CreateDC
 */
static BOOL X11DRV_CreateDC( PHYSDEV *pdev, LPCWSTR driver, LPCWSTR device,
                             LPCWSTR output, const DEVMODEW* initData )
{
    X11DRV_PDEVICE *physDev = create_x11_physdev( root_window );

    if (!physDev) return FALSE;

    physDev->depth         = default_visual.depth;
    physDev->color_shifts  = &X11DRV_PALETTE_default_shifts;
    physDev->dc_rect       = get_virtual_screen_rect();
    OffsetRect( &physDev->dc_rect, -physDev->dc_rect.left, -physDev->dc_rect.top );
    push_dc_driver( pdev, &physDev->dev, &x11drv_funcs );
    if (xrender_funcs && !xrender_funcs->pCreateDC( pdev, driver, device, output, initData )) return FALSE;
    return TRUE;
}


/**********************************************************************
 *	     X11DRV_CreateCompatibleDC
 */
static BOOL X11DRV_CreateCompatibleDC( PHYSDEV orig, PHYSDEV *pdev )
{
    X11DRV_PDEVICE *physDev = create_x11_physdev( stock_bitmap_pixmap );

    if (!physDev) return FALSE;

    physDev->depth  = 1;
    SetRect( &physDev->dc_rect, 0, 0, 1, 1 );
    push_dc_driver( pdev, &physDev->dev, &x11drv_funcs );
    if (orig) return TRUE;  /* we already went through Xrender if we have an orig device */
    if (xrender_funcs && !xrender_funcs->pCreateCompatibleDC( NULL, pdev )) return FALSE;
    return TRUE;
}


/**********************************************************************
 *	     X11DRV_DeleteDC
 */
static BOOL X11DRV_DeleteDC( PHYSDEV dev )
{
    X11DRV_PDEVICE *physDev = get_x11drv_dev( dev );

    XFreeGC( gdi_display, physDev->gc );
    HeapFree( GetProcessHeap(), 0, physDev );
    return TRUE;
}


void add_device_bounds( X11DRV_PDEVICE *dev, const RECT *rect )
{
    RECT rc;

    if (!dev->bounds) return;
    if (dev->region && GetRgnBox( dev->region, &rc ))
    {
        if (IntersectRect( &rc, &rc, rect )) add_bounds_rect( dev->bounds, &rc );
    }
    else add_bounds_rect( dev->bounds, rect );
}

/***********************************************************************
 *           X11DRV_SetBoundsRect
 */
static UINT X11DRV_SetBoundsRect( PHYSDEV dev, RECT *rect, UINT flags )
{
    X11DRV_PDEVICE *pdev = get_x11drv_dev( dev );

    if (flags & DCB_DISABLE) pdev->bounds = NULL;
    else if (flags & DCB_ENABLE) pdev->bounds = rect;
    return DCB_RESET;  /* we don't have device-specific bounds */
}


/***********************************************************************
 *           GetDeviceCaps    (X11DRV.@)
 */
static INT X11DRV_GetDeviceCaps( PHYSDEV dev, INT cap )
{
    switch(cap)
    {
    case DRIVERVERSION:
        return 0x300;
    case TECHNOLOGY:
        return DT_RASDISPLAY;
    case HORZSIZE:
    {
        RECT primary_rect = get_primary_monitor_rect();
        return MulDiv( primary_rect.right - primary_rect.left, 254, log_pixels_x * 10 );
    }
    case VERTSIZE:
    {
        RECT primary_rect = get_primary_monitor_rect();
        return MulDiv( primary_rect.bottom - primary_rect.top, 254, log_pixels_y * 10 );
    }
    case HORZRES:
    {
        RECT primary_rect = get_primary_monitor_rect();
        return primary_rect.right - primary_rect.left;
    }
    case VERTRES:
    {
        RECT primary_rect = get_primary_monitor_rect();
        return primary_rect.bottom - primary_rect.top;
    }
    case DESKTOPHORZRES:
    {
        RECT virtual_rect = get_virtual_screen_rect();
        return virtual_rect.right - virtual_rect.left;
    }
    case DESKTOPVERTRES:
    {
        RECT virtual_rect = get_virtual_screen_rect();
        return virtual_rect.bottom - virtual_rect.top;
    }
    case BITSPIXEL:
        return screen_bpp;
    case PLANES:
        return 1;
    case NUMBRUSHES:
        return -1;
    case NUMPENS:
        return -1;
    case NUMMARKERS:
        return 0;
    case NUMFONTS:
        return 0;
    case NUMCOLORS:
        /* MSDN: Number of entries in the device's color table, if the device has
         * a color depth of no more than 8 bits per pixel.For devices with greater
         * color depths, -1 is returned. */
        return (default_visual.depth > 8) ? -1 : (1 << default_visual.depth);
    case PDEVICESIZE:
        return sizeof(X11DRV_PDEVICE);
    case CURVECAPS:
        return (CC_CIRCLES | CC_PIE | CC_CHORD | CC_ELLIPSES | CC_WIDE |
                CC_STYLED | CC_WIDESTYLED | CC_INTERIORS | CC_ROUNDRECT);
    case LINECAPS:
        return (LC_POLYLINE | LC_MARKER | LC_POLYMARKER | LC_WIDE |
                LC_STYLED | LC_WIDESTYLED | LC_INTERIORS);
    case POLYGONALCAPS:
        return (PC_POLYGON | PC_RECTANGLE | PC_WINDPOLYGON | PC_SCANLINE |
                PC_WIDE | PC_STYLED | PC_WIDESTYLED | PC_INTERIORS);
    case TEXTCAPS:
        return (TC_OP_CHARACTER | TC_OP_STROKE | TC_CP_STROKE |
                TC_CR_ANY | TC_SF_X_YINDEP | TC_SA_DOUBLE | TC_SA_INTEGER |
                TC_SA_CONTIN | TC_UA_ABLE | TC_SO_ABLE | TC_RA_ABLE | TC_VA_ABLE);
    case CLIPCAPS:
        return CP_REGION;
    case COLORRES:
        /* The observed correspondence between BITSPIXEL and COLORRES is:
         * BITSPIXEL: 8  -> COLORRES: 18
         * BITSPIXEL: 16 -> COLORRES: 16
         * BITSPIXEL: 24 -> COLORRES: 24
         * BITSPIXEL: 32 -> COLORRES: 24 */
        return (screen_bpp <= 8) ? 18 : min( 24, screen_bpp );
    case RASTERCAPS:
        return (RC_BITBLT | RC_BANDING | RC_SCALING | RC_BITMAP64 | RC_DI_BITMAP |
                RC_DIBTODEV | RC_BIGFONT | RC_STRETCHBLT | RC_STRETCHDIB | RC_DEVBITS |
                (palette_size ? RC_PALETTE : 0));
    case SHADEBLENDCAPS:
        return (SB_GRAD_RECT | SB_GRAD_TRI | SB_CONST_ALPHA | SB_PIXEL_ALPHA);
    case ASPECTX:
    case ASPECTY:
        return 36;
    case ASPECTXY:
        return 51;
    case LOGPIXELSX:
        return log_pixels_x;
    case LOGPIXELSY:
        return log_pixels_y;
    case CAPS1:
        FIXME("(%p): CAPS1 is unimplemented, will return 0\n", dev->hdc );
        /* please see wingdi.h for the possible bit-flag values that need
           to be returned. */
        return 0;
    case SIZEPALETTE:
        return palette_size;
    case NUMRESERVED:
    case PHYSICALWIDTH:
    case PHYSICALHEIGHT:
    case PHYSICALOFFSETX:
    case PHYSICALOFFSETY:
    case SCALINGFACTORX:
    case SCALINGFACTORY:
    case VREFRESH:
    case BLTALIGNMENT:
        return 0;
    default:
        FIXME("(%p): unsupported capability %d, will return 0\n", dev->hdc, cap );
        return 0;
    }
}


/***********************************************************************
 *           SelectFont
 */
static HFONT X11DRV_SelectFont( PHYSDEV dev, HFONT hfont, UINT *aa_flags )
{
    if (default_visual.depth <= 8) *aa_flags = GGO_BITMAP;  /* no anti-aliasing on <= 8bpp */
    dev = GET_NEXT_PHYSDEV( dev, pSelectFont );
    return dev->funcs->pSelectFont( dev, hfont, aa_flags );
}

/**********************************************************************
 *           ExtEscape  (X11DRV.@)
 */
static INT X11DRV_ExtEscape( PHYSDEV dev, INT escape, INT in_count, LPCVOID in_data,
                      INT out_count, LPVOID out_data )
{
    X11DRV_PDEVICE *physDev = get_x11drv_dev( dev );

    switch(escape)
    {
    case QUERYESCSUPPORT:
        if (in_data && in_count >= sizeof(DWORD))
        {
            switch (*(const INT *)in_data)
            {
            case X11DRV_ESCAPE:
                return TRUE;
            }
        }
        break;

    case X11DRV_ESCAPE:
        if (in_data && in_count >= sizeof(enum x11drv_escape_codes))
        {
            switch(*(const enum x11drv_escape_codes *)in_data)
            {
            case X11DRV_SET_DRAWABLE:
                if (in_count >= sizeof(struct x11drv_escape_set_drawable))
                {
                    const struct x11drv_escape_set_drawable *data = in_data;
                    physDev->dc_rect = data->dc_rect;
                    physDev->drawable = data->drawable;
                    XFreeGC( gdi_display, physDev->gc );
                    physDev->gc = XCreateGC( gdi_display, physDev->drawable, 0, NULL );
                    XSetGraphicsExposures( gdi_display, physDev->gc, False );
                    XSetSubwindowMode( gdi_display, physDev->gc, data->mode );
                    TRACE( "SET_DRAWABLE hdc %p drawable %lx dc_rect %s\n",
                           dev->hdc, physDev->drawable, wine_dbgstr_rect(&physDev->dc_rect) );
                    return TRUE;
                }
                break;
            case X11DRV_GET_DRAWABLE:
                if (out_count >= sizeof(struct x11drv_escape_get_drawable))
                {
                    struct x11drv_escape_get_drawable *data = out_data;
                    data->drawable = physDev->drawable;
                    return TRUE;
                }
                break;
            case X11DRV_FLUSH_GL_DRAWABLE:
                if (in_count >= sizeof(struct x11drv_escape_flush_gl_drawable))
                {
                    const struct x11drv_escape_flush_gl_drawable *data = in_data;
                    RECT rect = physDev->dc_rect;

                    OffsetRect( &rect, -physDev->dc_rect.left, -physDev->dc_rect.top );
                    /* The GL drawable may be lagged behind if we don't flush first, so
                     * flush the display make sure we copy up-to-date data */
                    XFlush( gdi_display );
                    XSetFunction( gdi_display, physDev->gc, GXcopy );
                    XCopyArea( gdi_display, data->gl_drawable, physDev->drawable, physDev->gc,
                               0, 0, rect.right, rect.bottom,
                               physDev->dc_rect.left, physDev->dc_rect.top );
                    add_device_bounds( physDev, &rect );
                    return TRUE;
                }
                break;
            case X11DRV_START_EXPOSURES:
                XSetGraphicsExposures( gdi_display, physDev->gc, True );
                physDev->exposures = 0;
                return TRUE;
            case X11DRV_END_EXPOSURES:
                if (out_count >= sizeof(HRGN))
                {
                    HRGN hrgn = 0, tmp = 0;

                    XSetGraphicsExposures( gdi_display, physDev->gc, False );
                    if (physDev->exposures)
                    {
                        for (;;)
                        {
                            XEvent event;

                            XWindowEvent( gdi_display, physDev->drawable, ~0, &event );
                            if (event.type == NoExpose) break;
                            if (event.type == GraphicsExpose)
                            {
                                RECT rect;

                                rect.left   = event.xgraphicsexpose.x - physDev->dc_rect.left;
                                rect.top    = event.xgraphicsexpose.y - physDev->dc_rect.top;
                                rect.right  = rect.left + event.xgraphicsexpose.width;
                                rect.bottom = rect.top + event.xgraphicsexpose.height;
                                if (GetLayout( dev->hdc ) & LAYOUT_RTL)
                                    mirror_rect( &physDev->dc_rect, &rect );

                                TRACE( "got %s count %d\n", wine_dbgstr_rect(&rect),
                                       event.xgraphicsexpose.count );

                                if (!tmp) tmp = CreateRectRgnIndirect( &rect );
                                else SetRectRgn( tmp, rect.left, rect.top, rect.right, rect.bottom );
                                if (hrgn) CombineRgn( hrgn, hrgn, tmp, RGN_OR );
                                else
                                {
                                    hrgn = tmp;
                                    tmp = 0;
                                }
                                if (!event.xgraphicsexpose.count) break;
                            }
                            else
                            {
                                ERR( "got unexpected event %d\n", event.type );
                                break;
                            }
                        }
                        if (tmp) DeleteObject( tmp );
                    }
                    *(HRGN *)out_data = hrgn;
                    return TRUE;
                }
                break;
            case X11DRV_FLUSH_GDI_DISPLAY:
                XFlush( gdi_display );
                return TRUE;
            default:
                break;
            }
        }
        break;
    }
    return 0;
}

/**********************************************************************
 *           X11DRV_wine_get_wgl_driver
 */
static struct opengl_funcs * X11DRV_wine_get_wgl_driver( PHYSDEV dev, UINT version )
{
    struct opengl_funcs *ret;

    if (!(ret = get_glx_driver( version )))
    {
        dev = GET_NEXT_PHYSDEV( dev, wine_get_wgl_driver );
        ret = dev->funcs->wine_get_wgl_driver( dev, version );
    }
    return ret;
}


static const struct gdi_dc_funcs x11drv_funcs =
{
    NULL,                               /* pAbortDoc */
    NULL,                               /* pAbortPath */
    NULL,                               /* pAlphaBlend */
    NULL,                               /* pAngleArc */
    X11DRV_Arc,                         /* pArc */
    NULL,                               /* pArcTo */
    NULL,                               /* pBeginPath */
    NULL,                               /* pBlendImage */
    X11DRV_Chord,                       /* pChord */
    NULL,                               /* pCloseFigure */
    X11DRV_CreateCompatibleDC,          /* pCreateCompatibleDC */
    X11DRV_CreateDC,                    /* pCreateDC */
    X11DRV_DeleteDC,                    /* pDeleteDC */
    NULL,                               /* pDeleteObject */
    NULL,                               /* pDeviceCapabilities */
    X11DRV_Ellipse,                     /* pEllipse */
    NULL,                               /* pEndDoc */
    NULL,                               /* pEndPage */
    NULL,                               /* pEndPath */
    NULL,                               /* pEnumFonts */
    X11DRV_EnumICMProfiles,             /* pEnumICMProfiles */
    NULL,                               /* pExcludeClipRect */
    NULL,                               /* pExtDeviceMode */
    X11DRV_ExtEscape,                   /* pExtEscape */
    X11DRV_ExtFloodFill,                /* pExtFloodFill */
    NULL,                               /* pExtSelectClipRgn */
    NULL,                               /* pExtTextOut */
    X11DRV_FillPath,                    /* pFillPath */
    NULL,                               /* pFillRgn */
    NULL,                               /* pFlattenPath */
    NULL,                               /* pFontIsLinked */
    NULL,                               /* pFrameRgn */
    NULL,                               /* pGdiComment */
    NULL,                               /* pGetBoundsRect */
    NULL,                               /* pGetCharABCWidths */
    NULL,                               /* pGetCharABCWidthsI */
    NULL,                               /* pGetCharWidth */
    X11DRV_GetDeviceCaps,               /* pGetDeviceCaps */
    X11DRV_GetDeviceGammaRamp,          /* pGetDeviceGammaRamp */
    NULL,                               /* pGetFontData */
    NULL,                               /* pGetFontRealizationInfo */
    NULL,                               /* pGetFontUnicodeRanges */
    NULL,                               /* pGetGlyphIndices */
    NULL,                               /* pGetGlyphOutline */
    X11DRV_GetICMProfile,               /* pGetICMProfile */
    X11DRV_GetImage,                    /* pGetImage */
    NULL,                               /* pGetKerningPairs */
    X11DRV_GetNearestColor,             /* pGetNearestColor */
    NULL,                               /* pGetOutlineTextMetrics */
    NULL,                               /* pGetPixel */
    X11DRV_GetSystemPaletteEntries,     /* pGetSystemPaletteEntries */
    NULL,                               /* pGetTextCharsetInfo */
    NULL,                               /* pGetTextExtentExPoint */
    NULL,                               /* pGetTextExtentExPointI */
    NULL,                               /* pGetTextFace */
    NULL,                               /* pGetTextMetrics */
    X11DRV_GradientFill,                /* pGradientFill */
    NULL,                               /* pIntersectClipRect */
    NULL,                               /* pInvertRgn */
    X11DRV_LineTo,                      /* pLineTo */
    NULL,                               /* pModifyWorldTransform */
    NULL,                               /* pMoveTo */
    NULL,                               /* pOffsetClipRgn */
    NULL,                               /* pOffsetViewportOrg */
    NULL,                               /* pOffsetWindowOrg */
    X11DRV_PaintRgn,                    /* pPaintRgn */
    X11DRV_PatBlt,                      /* pPatBlt */
    X11DRV_Pie,                         /* pPie */
    NULL,                               /* pPolyBezier */
    NULL,                               /* pPolyBezierTo */
    NULL,                               /* pPolyDraw */
    X11DRV_PolyPolygon,                 /* pPolyPolygon */
    X11DRV_PolyPolyline,                /* pPolyPolyline */
    X11DRV_Polygon,                     /* pPolygon */
    NULL,                               /* pPolyline */
    NULL,                               /* pPolylineTo */
    X11DRV_PutImage,                    /* pPutImage */
    X11DRV_RealizeDefaultPalette,       /* pRealizeDefaultPalette */
    X11DRV_RealizePalette,              /* pRealizePalette */
    X11DRV_Rectangle,                   /* pRectangle */
    NULL,                               /* pResetDC */
    NULL,                               /* pRestoreDC */
    X11DRV_RoundRect,                   /* pRoundRect */
    NULL,                               /* pSaveDC */
    NULL,                               /* pScaleViewportExt */
    NULL,                               /* pScaleWindowExt */
    NULL,                               /* pSelectBitmap */
    X11DRV_SelectBrush,                 /* pSelectBrush */
    NULL,                               /* pSelectClipPath */
    X11DRV_SelectFont,                  /* pSelectFont */
    NULL,                               /* pSelectPalette */
    X11DRV_SelectPen,                   /* pSelectPen */
    NULL,                               /* pSetArcDirection */
    NULL,                               /* pSetBkColor */
    NULL,                               /* pSetBkMode */
    X11DRV_SetBoundsRect,               /* pSetBoundsRect */
    X11DRV_SetDCBrushColor,             /* pSetDCBrushColor */
    X11DRV_SetDCPenColor,               /* pSetDCPenColor */
    NULL,                               /* pSetDIBitsToDevice */
    X11DRV_SetDeviceClipping,           /* pSetDeviceClipping */
    X11DRV_SetDeviceGammaRamp,          /* pSetDeviceGammaRamp */
    NULL,                               /* pSetLayout */
    NULL,                               /* pSetMapMode */
    NULL,                               /* pSetMapperFlags */
    X11DRV_SetPixel,                    /* pSetPixel */
    NULL,                               /* pSetPolyFillMode */
    NULL,                               /* pSetROP2 */
    NULL,                               /* pSetRelAbs */
    NULL,                               /* pSetStretchBltMode */
    NULL,                               /* pSetTextAlign */
    NULL,                               /* pSetTextCharacterExtra */
    NULL,                               /* pSetTextColor */
    NULL,                               /* pSetTextJustification */
    NULL,                               /* pSetViewportExt */
    NULL,                               /* pSetViewportOrg */
    NULL,                               /* pSetWindowExt */
    NULL,                               /* pSetWindowOrg */
    NULL,                               /* pSetWorldTransform */
    NULL,                               /* pStartDoc */
    NULL,                               /* pStartPage */
    X11DRV_StretchBlt,                  /* pStretchBlt */
    NULL,                               /* pStretchDIBits */
    X11DRV_StrokeAndFillPath,           /* pStrokeAndFillPath */
    X11DRV_StrokePath,                  /* pStrokePath */
    X11DRV_UnrealizePalette,            /* pUnrealizePalette */
    NULL,                               /* pWidenPath */
    X11DRV_wine_get_wgl_driver,         /* wine_get_wgl_driver */
    GDI_PRIORITY_GRAPHICS_DRV           /* priority */
};


/******************************************************************************
 *      X11DRV_get_gdi_driver
 */
const struct gdi_dc_funcs * CDECL X11DRV_get_gdi_driver( unsigned int version )
{
    if (version != WINE_GDI_DRIVER_VERSION)
    {
        ERR( "version mismatch, gdi32 wants %u but winex11 has %u\n", version, WINE_GDI_DRIVER_VERSION );
        return NULL;
    }
    return &x11drv_funcs;
}
