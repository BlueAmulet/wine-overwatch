/*
 *	shell icon cache (SIC)
 *
 * Copyright 1998, 1999 Juergen Schmied
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

#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winreg.h"
#include "wine/debug.h"

#include "shellapi.h"
#include "objbase.h"
#include "pidl.h"
#include "shell32_main.h"
#include "undocshell.h"
#include "shresdef.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/********************** THE ICON CACHE ********************************/

#define INVALID_INDEX -1

typedef struct
{
	LPWSTR sSourceFile;	/* file (not path!) containing the icon */
	DWORD dwSourceIndex;	/* index within the file, if it is a resource ID it will be negated */
	DWORD dwListIndex;	/* index within the iconlist */
	DWORD dwFlags;		/* GIL_* flags */
	DWORD dwAccessTime;
} SIC_ENTRY, * LPSIC_ENTRY;

static HDPA sic_hdpa;
static INIT_ONCE sic_init_once = INIT_ONCE_STATIC_INIT;
static HIMAGELIST ShellSmallIconList;
static HIMAGELIST ShellLargeIconList;
static HIMAGELIST ShellExtraLargeIconList;
static HIMAGELIST ShellJumboIconList;

static CRITICAL_SECTION SHELL32_SicCS;
static CRITICAL_SECTION_DEBUG critsect_debug =
{
    0, 0, &SHELL32_SicCS,
    { &critsect_debug.ProcessLocksList, &critsect_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": SHELL32_SicCS") }
};
static CRITICAL_SECTION SHELL32_SicCS = { &critsect_debug, -1, 0, 0, 0, 0 };


static const WCHAR WindowMetrics[] = {'C','o','n','t','r','o','l',' ','P','a','n','e','l','\\','D','e','s','k','t','o','p','\\',
                                      'W','i','n','d','o','w','M','e','t','r','i','c','s',0};
static const WCHAR ShellIconSize[] = {'S','h','e','l','l',' ','I','c','o','n',' ','S','i','z','e',0};

#define SIC_COMPARE_LISTINDEX 1

/*****************************************************************************
 * SIC_CompareEntries
 *
 * NOTES
 *  Callback for DPA_Search
 */
static INT CALLBACK SIC_CompareEntries( LPVOID p1, LPVOID p2, LPARAM lparam)
{
        LPSIC_ENTRY e1 = p1, e2 = p2;

	TRACE("%p %p %8lx\n", p1, p2, lparam);

	/* Icons in the cache are keyed by the name of the file they are
	 * loaded from, their resource index and the fact if they have a shortcut
	 * icon overlay or not. 
	 */

        if (lparam & SIC_COMPARE_LISTINDEX)
            return e1->dwListIndex != e2->dwListIndex;

	if (e1->dwSourceIndex != e2->dwSourceIndex || /* first the faster one */
	    (e1->dwFlags & GIL_FORSHORTCUT) != (e2->dwFlags & GIL_FORSHORTCUT)) 
	  return 1;

	if (strcmpiW(e1->sSourceFile,e2->sSourceFile))
	  return 1;

	return 0;
}

/**************************************************************************************
 *                      SIC_get_location
 *
 * Returns the source file and resource index of an icon with the given imagelist index
 */
HRESULT SIC_get_location( int list_idx, WCHAR *file, DWORD *size, int *res_idx )
{
    SIC_ENTRY seek, *found;
    DWORD needed;
    HRESULT hr = E_INVALIDARG;
    int dpa_idx;

    seek.dwListIndex = list_idx;

    EnterCriticalSection( &SHELL32_SicCS );

    dpa_idx = DPA_Search( sic_hdpa, &seek, 0, SIC_CompareEntries, SIC_COMPARE_LISTINDEX, 0 );
    if (dpa_idx != -1)
    {
        found = (SIC_ENTRY *)DPA_GetPtr( sic_hdpa, dpa_idx );
        needed = (strlenW( found->sSourceFile ) + 1) * sizeof(WCHAR);
        if (needed <= *size)
        {
            memcpy( file, found->sSourceFile, needed );
            *res_idx = found->dwSourceIndex;
            hr = S_OK;
        }
        else
        {
            *size = needed;
            hr = E_NOT_SUFFICIENT_BUFFER;
        }
    }
    LeaveCriticalSection( &SHELL32_SicCS );

    return hr;
}

/* declare SIC_LoadOverlayIcon() */
static int SIC_LoadOverlayIcon(int icon_idx);

/*****************************************************************************
 * SIC_OverlayShortcutImage			[internal]
 *
 * NOTES
 *  Creates a new icon as a copy of the passed-in icon, overlaid with a
 *  shortcut image. 
 */
static HICON SIC_OverlayShortcutImage(HICON SourceIcon, int type)
{	ICONINFO SourceIconInfo, ShortcutIconInfo, TargetIconInfo;
	HICON ShortcutIcon, TargetIcon;
	BITMAP SourceBitmapInfo, ShortcutBitmapInfo;
	HDC SourceDC = NULL,
	  ShortcutDC = NULL,
	  TargetDC = NULL,
	  ScreenDC = NULL;
	HBITMAP OldSourceBitmap = NULL,
	  OldShortcutBitmap = NULL,
	  OldTargetBitmap = NULL;

	static int s_imgListIdx = -1;

	/* Get information about the source icon and shortcut overlay */
	if (! GetIconInfo(SourceIcon, &SourceIconInfo)
	    || 0 == GetObjectW(SourceIconInfo.hbmColor, sizeof(BITMAP), &SourceBitmapInfo))
	{
	  return NULL;
	}

	/* search for the shortcut icon only once */
	if (s_imgListIdx == -1)
	    s_imgListIdx = SIC_LoadOverlayIcon(- IDI_SHELL_SHORTCUT);
                           /* FIXME should use icon index 29 instead of the
                              resource id, but not all icons are present yet
                              so we can't use icon indices */

	if (s_imgListIdx != -1)
	{
        if (type == SHIL_SMALL)
            ShortcutIcon = ImageList_GetIcon(ShellSmallIconList, s_imgListIdx, ILD_TRANSPARENT);
        else if (type == SHIL_LARGE)
            ShortcutIcon = ImageList_GetIcon(ShellLargeIconList, s_imgListIdx, ILD_TRANSPARENT);
        else if (type == SHIL_EXTRALARGE)
            ShortcutIcon = ImageList_GetIcon(ShellExtraLargeIconList, s_imgListIdx, ILD_TRANSPARENT);
        else if (type == SHIL_JUMBO)
            ShortcutIcon = ImageList_GetIcon(ShellJumboIconList, s_imgListIdx, ILD_TRANSPARENT);
        else
            ShortcutIcon = NULL;
	} else
	    ShortcutIcon = NULL;

	if (NULL == ShortcutIcon
	    || ! GetIconInfo(ShortcutIcon, &ShortcutIconInfo)
	    || 0 == GetObjectW(ShortcutIconInfo.hbmColor, sizeof(BITMAP), &ShortcutBitmapInfo))
	{
	  return NULL;
	}

	TargetIconInfo = SourceIconInfo;
	TargetIconInfo.hbmMask = NULL;
	TargetIconInfo.hbmColor = NULL;

	/* Setup the source, shortcut and target masks */
	SourceDC = CreateCompatibleDC(NULL);
	if (NULL == SourceDC) goto fail;
	OldSourceBitmap = SelectObject(SourceDC, SourceIconInfo.hbmMask);
	if (NULL == OldSourceBitmap) goto fail;

	ShortcutDC = CreateCompatibleDC(NULL);
	if (NULL == ShortcutDC) goto fail;
	OldShortcutBitmap = SelectObject(ShortcutDC, ShortcutIconInfo.hbmMask);
	if (NULL == OldShortcutBitmap) goto fail;

	TargetDC = CreateCompatibleDC(NULL);
	if (NULL == TargetDC) goto fail;
	TargetIconInfo.hbmMask = CreateCompatibleBitmap(TargetDC, SourceBitmapInfo.bmWidth,
	                                                SourceBitmapInfo.bmHeight);
	if (NULL == TargetIconInfo.hbmMask) goto fail;
	ScreenDC = GetDC(NULL);
	if (NULL == ScreenDC) goto fail;
	TargetIconInfo.hbmColor = CreateCompatibleBitmap(ScreenDC, SourceBitmapInfo.bmWidth,
	                                                 SourceBitmapInfo.bmHeight);
	ReleaseDC(NULL, ScreenDC);
	if (NULL == TargetIconInfo.hbmColor) goto fail;
	OldTargetBitmap = SelectObject(TargetDC, TargetIconInfo.hbmMask);
	if (NULL == OldTargetBitmap) goto fail;

	/* Create the target mask by ANDing the source and shortcut masks */
	if (! BitBlt(TargetDC, 0, 0, SourceBitmapInfo.bmWidth, SourceBitmapInfo.bmHeight,
	             SourceDC, 0, 0, SRCCOPY) ||
	    ! BitBlt(TargetDC, 0, SourceBitmapInfo.bmHeight - ShortcutBitmapInfo.bmHeight,
	             ShortcutBitmapInfo.bmWidth, ShortcutBitmapInfo.bmHeight,
	             ShortcutDC, 0, 0, SRCAND))
	{
	  goto fail;
	}

	/* Setup the source and target xor bitmap */
	if (NULL == SelectObject(SourceDC, SourceIconInfo.hbmColor) ||
	    NULL == SelectObject(TargetDC, TargetIconInfo.hbmColor))
	{
	  goto fail;
	}

	/* Copy the source xor bitmap to the target and clear out part of it by using
	   the shortcut mask */
	if (! BitBlt(TargetDC, 0, 0, SourceBitmapInfo.bmWidth, SourceBitmapInfo.bmHeight,
	             SourceDC, 0, 0, SRCCOPY) ||
	    ! BitBlt(TargetDC, 0, SourceBitmapInfo.bmHeight - ShortcutBitmapInfo.bmHeight,
	             ShortcutBitmapInfo.bmWidth, ShortcutBitmapInfo.bmHeight,
	             ShortcutDC, 0, 0, SRCAND))
	{
	  goto fail;
	}

	if (NULL == SelectObject(ShortcutDC, ShortcutIconInfo.hbmColor)) goto fail;

	/* Now put in the shortcut xor mask */
	if (! BitBlt(TargetDC, 0, SourceBitmapInfo.bmHeight - ShortcutBitmapInfo.bmHeight,
	             ShortcutBitmapInfo.bmWidth, ShortcutBitmapInfo.bmHeight,
	             ShortcutDC, 0, 0, SRCINVERT))
	{
	  goto fail;
	}

	/* Clean up, we're not goto'ing to 'fail' after this so we can be lazy and not set
	   handles to NULL */
	SelectObject(TargetDC, OldTargetBitmap);
	DeleteObject(TargetDC);
	SelectObject(ShortcutDC, OldShortcutBitmap);
	DeleteObject(ShortcutDC);
	SelectObject(SourceDC, OldSourceBitmap);
	DeleteObject(SourceDC);

	/* Create the icon using the bitmaps prepared earlier */
	TargetIcon = CreateIconIndirect(&TargetIconInfo);

	/* CreateIconIndirect copies the bitmaps, so we can release our bitmaps now */
	DeleteObject(TargetIconInfo.hbmColor);
	DeleteObject(TargetIconInfo.hbmMask);

	return TargetIcon;

fail:
	/* Clean up scratch resources we created */
	if (NULL != OldTargetBitmap) SelectObject(TargetDC, OldTargetBitmap);
	if (NULL != TargetIconInfo.hbmColor) DeleteObject(TargetIconInfo.hbmColor);
	if (NULL != TargetIconInfo.hbmMask) DeleteObject(TargetIconInfo.hbmMask);
	if (NULL != TargetDC) DeleteObject(TargetDC);
	if (NULL != OldShortcutBitmap) SelectObject(ShortcutDC, OldShortcutBitmap);
	if (NULL != ShortcutDC) DeleteObject(ShortcutDC);
	if (NULL != OldSourceBitmap) SelectObject(SourceDC, OldSourceBitmap);
	if (NULL != SourceDC) DeleteObject(SourceDC);

	return NULL;
}

/*****************************************************************************
 * SIC_IconAppend			[internal]
 *
 * NOTES
 *  appends an icon pair to the end of the cache
 */
static INT SIC_IconAppend(LPCWSTR sSourceFile, INT dwSourceIndex, HICON hSmallIcon,
                          HICON hLargeIcon, HICON hExtraLargeIcon, HICON hJumboIcon, DWORD dwFlags)
{
    LPSIC_ENTRY lpsice;
    INT ret, index, index1, index2, index3;
	WCHAR path[MAX_PATH];
    TRACE("%s %i %p %p %p %p %d\n", debugstr_w(sSourceFile), dwSourceIndex, hSmallIcon,
        hLargeIcon, hExtraLargeIcon, hJumboIcon, dwFlags);

	lpsice = SHAlloc(sizeof(SIC_ENTRY));

	GetFullPathNameW(sSourceFile, MAX_PATH, path, NULL);
	lpsice->sSourceFile = HeapAlloc( GetProcessHeap(), 0, (strlenW(path)+1)*sizeof(WCHAR) );
	strcpyW( lpsice->sSourceFile, path );

	lpsice->dwSourceIndex = dwSourceIndex;
	lpsice->dwFlags = dwFlags;

	EnterCriticalSection(&SHELL32_SicCS);

	index = DPA_InsertPtr(sic_hdpa, 0x7fff, lpsice);
	if ( INVALID_INDEX == index )
	{
	  HeapFree(GetProcessHeap(), 0, lpsice->sSourceFile);
	  SHFree(lpsice);
	  ret = INVALID_INDEX;
	}
	else
	{
        index  = ImageList_AddIcon( ShellSmallIconList, hSmallIcon );
        index1 = ImageList_AddIcon( ShellLargeIconList, hLargeIcon );
        index2 = ImageList_AddIcon( ShellExtraLargeIconList, hExtraLargeIcon );
        index3 = ImageList_AddIcon( ShellJumboIconList, hJumboIcon );

        if (index != index1 || index != index2 || index != index3)
            FIXME("iconlists out of sync 0x%x 0x%x 0x%x 0x%x\n", index, index1, index2, index3);

	  lpsice->dwListIndex = index;
	  ret = lpsice->dwListIndex;
	}

	LeaveCriticalSection(&SHELL32_SicCS);
	return ret;
}

static BOOL get_imagelist_icon_size(int list, SIZE *size)
{
    HIMAGELIST image_list;

    if (list < SHIL_LARGE || list > SHIL_SMALL) return FALSE;
    image_list = (list == SHIL_LARGE) ? ShellLargeIconList : ShellSmallIconList;

    return ImageList_GetIconSize( image_list, &size->cx, &size->cy );
}

/****************************************************************************
 * SIC_LoadIcon				[internal]
 *
 * NOTES
 *  gets small/big icon by number from a file
 */
static INT SIC_LoadIcon (LPCWSTR sSourceFile, INT dwSourceIndex, DWORD dwFlags)
{
    HICON   hiconSmall=0;
    HICON   hiconLarge=0;
    HICON   hiconExtraLarge=0;
    HICON   hiconJumbo=0;
    HICON   hiconSmallShortcut;
    HICON   hiconLargeShortcut;
    HICON   hiconExtraLargeShortcut;
    HICON   hiconJumboShortcut;
    int ret;
    SIZE size;

    get_imagelist_icon_size( SHIL_SMALL, &size );
    PrivateExtractIconsW( sSourceFile, dwSourceIndex, size.cx, size.cy, &hiconSmall, 0, 1, 0 );
    get_imagelist_icon_size( SHIL_LARGE, &size );
    PrivateExtractIconsW( sSourceFile, dwSourceIndex, size.cx, size.cy, &hiconLarge, 0, 1, 0 );
    PrivateExtractIconsW( sSourceFile, dwSourceIndex, 48, 48, &hiconExtraLarge, 0, 1, 0 );
    PrivateExtractIconsW( sSourceFile, dwSourceIndex, 256, 256, &hiconJumbo, 0, 1, 0 );

	if (!hiconSmall || !hiconLarge || !hiconExtraLarge || !hiconJumbo)
	{
	  WARN("failure loading icon %i from %s (%p %p)\n", dwSourceIndex, debugstr_w(sSourceFile), hiconLarge, hiconSmall);
	  return -1;
	}

	if (0 != (dwFlags & GIL_FORSHORTCUT))
	{
        hiconSmallShortcut = SIC_OverlayShortcutImage( hiconSmall, SHIL_SMALL );
        hiconLargeShortcut = SIC_OverlayShortcutImage( hiconLarge, SHIL_LARGE );
        hiconExtraLargeShortcut = SIC_OverlayShortcutImage( hiconExtraLarge, SHIL_EXTRALARGE );
        hiconJumboShortcut = SIC_OverlayShortcutImage( hiconJumbo, SHIL_JUMBO );

        if (NULL != hiconLargeShortcut && NULL != hiconSmallShortcut &&
            NULL != hiconExtraLargeShortcut && NULL != hiconJumboShortcut)
        {
            DestroyIcon( hiconSmall );
            DestroyIcon( hiconLarge );
            DestroyIcon( hiconExtraLarge );
            DestroyIcon( hiconJumbo );

            hiconSmall = hiconSmallShortcut;
            hiconLarge = hiconLargeShortcut;
            hiconExtraLarge = hiconExtraLargeShortcut;
            hiconJumbo = hiconJumboShortcut;
        }
        else
        {
            WARN("Failed to create shortcut overlaid icons\n");
            if (NULL != hiconSmallShortcut) DestroyIcon(hiconSmallShortcut);
            if (NULL != hiconLargeShortcut) DestroyIcon(hiconLargeShortcut);
            if (NULL != hiconExtraLargeShortcut) DestroyIcon(hiconExtraLargeShortcut);
            if (NULL != hiconJumboShortcut) DestroyIcon(hiconJumboShortcut);
            dwFlags &= ~ GIL_FORSHORTCUT;
        }
	}

    ret = SIC_IconAppend( sSourceFile, dwSourceIndex, hiconSmall, hiconLarge,
                          hiconExtraLarge, hiconJumbo, dwFlags );
    DestroyIcon( hiconSmall );
    DestroyIcon( hiconLarge );
    DestroyIcon( hiconExtraLarge );
    DestroyIcon( hiconJumbo );
    return ret;
}

static int get_shell_icon_size(void)
{
    WCHAR buf[32];
    DWORD value = 32, size = sizeof(buf), type;
    HKEY key;

    if (!RegOpenKeyW( HKEY_CURRENT_USER, WindowMetrics, &key ))
    {
        if (!RegQueryValueExW( key, ShellIconSize, NULL, &type, (BYTE *)buf, &size ) && type == REG_SZ)
        {
            if (size == sizeof(buf)) buf[size / sizeof(WCHAR) - 1] = 0;
            value = atoiW( buf );
        }
        RegCloseKey( key );
    }
    return value;
}

/*****************************************************************************
 * SIC_Initialize			[internal]
 */
static BOOL WINAPI SIC_Initialize( INIT_ONCE *once, void *param, void **context )
{
    HICON   hSm, hLg, hELg, hJb;
    int     cx_small, cy_small;
    int     cx_large, cy_large;
    int     cx_extralarge, cy_extralarge;
    int     cx_jumbo, cy_jumbo;

        if (!IsProcessDPIAware())
        {
            cx_large = cy_large = get_shell_icon_size();
            cx_small = GetSystemMetrics( SM_CXSMICON );
            cy_small = GetSystemMetrics( SM_CYSMICON );
        }
        else
        {
            cx_large = GetSystemMetrics( SM_CXICON );
            cy_large = GetSystemMetrics( SM_CYICON );
            cx_small = cx_large / 2;
            cy_small = cy_large / 2;
        }

	    cx_extralarge = (GetSystemMetrics( SM_CXICON ) * 3) / 2;
	    cy_extralarge = (GetSystemMetrics( SM_CYICON ) * 3) / 2;
	    cx_jumbo = 256;
	    cy_jumbo = 256;

        TRACE("large %dx%d small %dx%d\n", cx_large, cy_large, cx_small, cx_small);
        TRACE("extra %dx%d jumbo %dx%d\n", cx_extralarge, cy_extralarge, cx_jumbo, cy_jumbo);

	sic_hdpa = DPA_Create(16);

	if (!sic_hdpa)
	{
	  return(FALSE);
	}

    ShellSmallIconList = ImageList_Create( cx_small,cy_small,ILC_COLOR32|ILC_MASK,0,0x20 );
    ShellLargeIconList = ImageList_Create( cx_large,cy_large,ILC_COLOR32|ILC_MASK,0,0x20 );
    ShellExtraLargeIconList = ImageList_Create( cx_extralarge,cy_extralarge,ILC_COLOR32|ILC_MASK,0,0x20 );
    ShellJumboIconList = ImageList_Create( cx_jumbo,cy_jumbo,ILC_COLOR32|ILC_MASK,0,0x20 );

    ImageList_SetBkColor( ShellSmallIconList, CLR_NONE );
    ImageList_SetBkColor( ShellLargeIconList, CLR_NONE );
    ImageList_SetBkColor( ShellExtraLargeIconList, CLR_NONE );
    ImageList_SetBkColor( ShellJumboIconList, CLR_NONE );

    /* Load the document icon, which is used as the default if an icon isn't found. */
    hSm = LoadImageA(shell32_hInstance, MAKEINTRESOURCEA(IDI_SHELL_DOCUMENT),
                            IMAGE_ICON, cx_small, cy_small, LR_SHARED);
    hLg = LoadImageA(shell32_hInstance, MAKEINTRESOURCEA(IDI_SHELL_DOCUMENT),
                            IMAGE_ICON, cx_large, cy_large, LR_SHARED);
    hELg = LoadImageA(shell32_hInstance, MAKEINTRESOURCEA(IDI_SHELL_DOCUMENT),
                            IMAGE_ICON, cx_extralarge, cy_extralarge, LR_SHARED);
    hJb = LoadImageA(shell32_hInstance, MAKEINTRESOURCEA(IDI_SHELL_DOCUMENT),
                            IMAGE_ICON, cx_jumbo, cy_jumbo, LR_SHARED);
    if (!hSm || !hLg || !hELg || !hJb)
    {
      FIXME("Failed to load IDI_SHELL_DOCUMENT icon!\n");
      return FALSE;
    }

    SIC_IconAppend( swShell32Name, IDI_SHELL_DOCUMENT-1, hSm, hLg, hELg, hJb, 0 );
    SIC_IconAppend( swShell32Name, -IDI_SHELL_DOCUMENT, hSm, hLg, hELg, hJb, 0 );

    TRACE("hIconSmall=%p hIconLarge=%p hExtraLargeIcon=%p hJumboIcon=%p\n",
        ShellSmallIconList, ShellLargeIconList, ShellExtraLargeIconList, ShellJumboIconList);

	return TRUE;
}
/*************************************************************************
 * SIC_Destroy
 *
 * frees the cache
 */
static INT CALLBACK sic_free( LPVOID ptr, LPVOID lparam )
{
	HeapFree(GetProcessHeap(), 0, ((LPSIC_ENTRY)ptr)->sSourceFile);
	SHFree(ptr);
	return TRUE;
}

void SIC_Destroy(void)
{
	TRACE("\n");

	EnterCriticalSection(&SHELL32_SicCS);

	if (sic_hdpa) DPA_DestroyCallback(sic_hdpa, sic_free, NULL );

    if (ShellSmallIconList)
        ImageList_Destroy( ShellSmallIconList );
    if (ShellLargeIconList)
        ImageList_Destroy( ShellLargeIconList );
    if (ShellExtraLargeIconList)
        ImageList_Destroy( ShellExtraLargeIconList );
    if (ShellJumboIconList)
        ImageList_Destroy( ShellJumboIconList );

    LeaveCriticalSection(&SHELL32_SicCS);
    DeleteCriticalSection(&SHELL32_SicCS);
}

/*****************************************************************************
 * SIC_GetIconIndex			[internal]
 *
 * Parameters
 *	sSourceFile	[IN]	filename of file containing the icon
 *	index		[IN]	index/resID (negated) in this file
 *
 * NOTES
 *  look in the cache for a proper icon. if not available the icon is taken
 *  from the file and cached
 */
INT SIC_GetIconIndex (LPCWSTR sSourceFile, INT dwSourceIndex, DWORD dwFlags )
{
	SIC_ENTRY sice;
	INT ret, index = INVALID_INDEX;
	WCHAR path[MAX_PATH];

	TRACE("%s %i\n", debugstr_w(sSourceFile), dwSourceIndex);

	GetFullPathNameW(sSourceFile, MAX_PATH, path, NULL);
	sice.sSourceFile = path;
	sice.dwSourceIndex = dwSourceIndex;
	sice.dwFlags = dwFlags;

        InitOnceExecuteOnce( &sic_init_once, SIC_Initialize, NULL, NULL );

	EnterCriticalSection(&SHELL32_SicCS);

	if (NULL != DPA_GetPtr (sic_hdpa, 0))
	{
	  /* search linear from position 0*/
	  index = DPA_Search (sic_hdpa, &sice, 0, SIC_CompareEntries, 0, 0);
	}

	if ( INVALID_INDEX == index )
	{
          ret = SIC_LoadIcon (sSourceFile, dwSourceIndex, dwFlags);
	}
	else
	{
	  TRACE("-- found\n");
	  ret = ((LPSIC_ENTRY)DPA_GetPtr(sic_hdpa, index))->dwListIndex;
	}

	LeaveCriticalSection(&SHELL32_SicCS);
	return ret;
}

/*****************************************************************************
 * SIC_LoadOverlayIcon			[internal]
 *
 * Load a shell overlay icon and return its icon cache index.
 */
static int SIC_LoadOverlayIcon(int icon_idx)
{
	WCHAR buffer[1024], wszIdx[8];
	HKEY hKeyShellIcons;
	LPCWSTR iconPath;
	int iconIdx;

	static const WCHAR wszShellIcons[] = {
	    'S','o','f','t','w','a','r','e','\\','M','i','c','r','o','s','o','f','t','\\',
	    'W','i','n','d','o','w','s','\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\',
	    'E','x','p','l','o','r','e','r','\\','S','h','e','l','l',' ','I','c','o','n','s',0
	}; 
	static const WCHAR wszNumFmt[] = {'%','d',0};

	iconPath = swShell32Name;	/* default: load icon from shell32.dll */
	iconIdx = icon_idx;

	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, wszShellIcons, 0, KEY_READ, &hKeyShellIcons) == ERROR_SUCCESS)
	{
	    DWORD count = sizeof(buffer);

	    sprintfW(wszIdx, wszNumFmt, icon_idx);

	    /* read icon path and index */
	    if (RegQueryValueExW(hKeyShellIcons, wszIdx, NULL, NULL, (LPBYTE)buffer, &count) == ERROR_SUCCESS)
	    {
		LPWSTR p = strchrW(buffer, ',');

		if (!p)
		{
		    ERR("Icon index in %s/%s corrupted, no comma.\n", debugstr_w(wszShellIcons),debugstr_w(wszIdx));
		    RegCloseKey(hKeyShellIcons);
		    return -1;
		}
		*p++ = 0;
		iconPath = buffer;
		iconIdx = atoiW(p);
	    }

	    RegCloseKey(hKeyShellIcons);
	}

        InitOnceExecuteOnce( &sic_init_once, SIC_Initialize, NULL, NULL );

	return SIC_LoadIcon(iconPath, iconIdx, 0);
}

/*************************************************************************
 * Shell_GetImageLists			[SHELL32.71]
 *
 * PARAMETERS
 *  imglist[1|2] [OUT] pointer which receives imagelist handles
 *
 */
BOOL WINAPI Shell_GetImageLists(HIMAGELIST * lpBigList, HIMAGELIST * lpSmallList)
{
    TRACE("(%p,%p)\n",lpBigList,lpSmallList);
    InitOnceExecuteOnce( &sic_init_once, SIC_Initialize, NULL, NULL );
    if (lpBigList) *lpBigList = ShellLargeIconList;
    if (lpSmallList) *lpSmallList = ShellSmallIconList;
    return TRUE;
}

void SHELL_GetInternalImageLists(HIMAGELIST *lpSmallList, HIMAGELIST *lpLargeList,
    HIMAGELIST *lpExtraLargeList, HIMAGELIST *lpJumboList)
{
    InitOnceExecuteOnce( &sic_init_once, SIC_Initialize, NULL, NULL );
    if (lpSmallList) *lpSmallList = ShellSmallIconList;
    if (lpLargeList) *lpLargeList = ShellLargeIconList;
    if (lpExtraLargeList) *lpExtraLargeList = ShellExtraLargeIconList;
    if (lpJumboList) *lpJumboList = ShellJumboIconList;
}

/*************************************************************************
 * PidlToSicIndex			[INTERNAL]
 *
 * PARAMETERS
 *	sh	[IN]	IShellFolder
 *	pidl	[IN]
 *	bBigIcon [IN]
 *	uFlags	[IN]	GIL_*
 *	pIndex	[OUT]	index within the SIC
 *
 */
BOOL PidlToSicIndex (
	IShellFolder * sh,
	LPCITEMIDLIST pidl,
	BOOL bBigIcon,
	UINT uFlags,
	int * pIndex)
{
	IExtractIconW	*ei;
	WCHAR		szIconFile[MAX_PATH];	/* file containing the icon */
	INT		iSourceIndex;		/* index or resID(negated) in this file */
	BOOL		ret = FALSE;
	UINT		dwFlags = 0;
	int		iShortcutDefaultIndex = INVALID_INDEX;

	TRACE("sf=%p pidl=%p %s\n", sh, pidl, bBigIcon?"Big":"Small");

        InitOnceExecuteOnce( &sic_init_once, SIC_Initialize, NULL, NULL );

	if (SUCCEEDED (IShellFolder_GetUIObjectOf(sh, 0, 1, &pidl, &IID_IExtractIconW, 0, (void **)&ei)))
	{
	  if (SUCCEEDED(IExtractIconW_GetIconLocation(ei, uFlags, szIconFile, MAX_PATH, &iSourceIndex, &dwFlags)))
	  {
	    *pIndex = SIC_GetIconIndex(szIconFile, iSourceIndex, uFlags);
	    ret = TRUE;
	  }
	  IExtractIconW_Release(ei);
	}

	if (INVALID_INDEX == *pIndex)	/* default icon when failed */
	{
	  if (0 == (uFlags & GIL_FORSHORTCUT))
	  {
	    *pIndex = 0;
	  }
	  else
	  {
	    if (INVALID_INDEX == iShortcutDefaultIndex)
	    {
	      iShortcutDefaultIndex = SIC_LoadIcon(swShell32Name, 0, GIL_FORSHORTCUT);
	    }
	    *pIndex = (INVALID_INDEX != iShortcutDefaultIndex ? iShortcutDefaultIndex : 0);
	  }
	}

	return ret;

}

/*************************************************************************
 * SHMapPIDLToSystemImageListIndex	[SHELL32.77]
 *
 * PARAMETERS
 *	sh	[IN]		pointer to an instance of IShellFolder
 *	pidl	[IN]
 *	pIndex	[OUT][OPTIONAL]	SIC index for big icon
 *
 */
int WINAPI SHMapPIDLToSystemImageListIndex(
	IShellFolder *sh,
	LPCITEMIDLIST pidl,
	int *pIndex)
{
	int Index;
	UINT uGilFlags = 0;

	TRACE("(SF=%p,pidl=%p,%p)\n",sh,pidl,pIndex);
	pdump(pidl);

	if (SHELL_IsShortcut(pidl))
	    uGilFlags |= GIL_FORSHORTCUT;

	if (pIndex)
	    if (!PidlToSicIndex ( sh, pidl, 1, uGilFlags, pIndex))
	        *pIndex = -1;

	if (!PidlToSicIndex ( sh, pidl, 0, uGilFlags, &Index))
	    return -1;

	return Index;
}

/*************************************************************************
 * SHMapIDListToImageListIndexAsync  [SHELL32.148]
 */
HRESULT WINAPI SHMapIDListToImageListIndexAsync(IUnknown *pts, IShellFolder *psf,
                                                LPCITEMIDLIST pidl, UINT flags,
                                                void *pfn, void *pvData, void *pvHint,
                                                int *piIndex, int *piIndexSel)
{
    FIXME("(%p, %p, %p, 0x%08x, %p, %p, %p, %p, %p)\n",
            pts, psf, pidl, flags, pfn, pvData, pvHint, piIndex, piIndexSel);
    return E_FAIL;
}

/*************************************************************************
 * Shell_GetCachedImageIndex		[SHELL32.72]
 *
 */
static INT Shell_GetCachedImageIndexA(LPCSTR szPath, INT nIndex, BOOL bSimulateDoc)
{
	INT ret, len;
	LPWSTR szTemp;

	WARN("(%s,%08x,%08x) semi-stub.\n",debugstr_a(szPath), nIndex, bSimulateDoc);

	len = MultiByteToWideChar( CP_ACP, 0, szPath, -1, NULL, 0 );
	szTemp = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
	MultiByteToWideChar( CP_ACP, 0, szPath, -1, szTemp, len );

	ret = SIC_GetIconIndex( szTemp, nIndex, 0 );

	HeapFree( GetProcessHeap(), 0, szTemp );

	return ret;
}

static INT Shell_GetCachedImageIndexW(LPCWSTR szPath, INT nIndex, BOOL bSimulateDoc)
{
	WARN("(%s,%08x,%08x) semi-stub.\n",debugstr_w(szPath), nIndex, bSimulateDoc);

	return SIC_GetIconIndex(szPath, nIndex, 0);
}

INT WINAPI Shell_GetCachedImageIndexAW(LPCVOID szPath, INT nIndex, BOOL bSimulateDoc)
{	if( SHELL_OsIsUnicode())
	  return Shell_GetCachedImageIndexW(szPath, nIndex, bSimulateDoc);
	return Shell_GetCachedImageIndexA(szPath, nIndex, bSimulateDoc);
}

/*************************************************************************
 * ExtractIconExW			[SHELL32.@]
 * RETURNS
 *  0 no icon found
 *  -1 file is not valid
 *  or number of icons extracted
 */
UINT WINAPI ExtractIconExW(LPCWSTR lpszFile, INT nIconIndex, HICON * phiconLarge, HICON * phiconSmall, UINT nIcons)
{
	TRACE("%s %i %p %p %i\n", debugstr_w(lpszFile), nIconIndex, phiconLarge, phiconSmall, nIcons);

	return PrivateExtractIconExW(lpszFile, nIconIndex, phiconLarge, phiconSmall, nIcons);
}

/*************************************************************************
 * ExtractIconExA			[SHELL32.@]
 */
UINT WINAPI ExtractIconExA(LPCSTR lpszFile, INT nIconIndex, HICON * phiconLarge, HICON * phiconSmall, UINT nIcons)
{
    UINT ret = 0;
    INT len = MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, NULL, 0);
    LPWSTR lpwstrFile = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

    TRACE("%s %i %p %p %i\n", lpszFile, nIconIndex, phiconLarge, phiconSmall, nIcons);

    if (lpwstrFile)
    {
        MultiByteToWideChar(CP_ACP, 0, lpszFile, -1, lpwstrFile, len);
        ret = ExtractIconExW(lpwstrFile, nIconIndex, phiconLarge, phiconSmall, nIcons);
        HeapFree(GetProcessHeap(), 0, lpwstrFile);
    }
    return ret;
}

/*************************************************************************
 *				ExtractAssociatedIconA (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconA(HINSTANCE hInst, LPSTR lpIconPath, LPWORD lpiIcon)
{	
    HICON hIcon = NULL;
    INT len = MultiByteToWideChar(CP_ACP, 0, lpIconPath, -1, NULL, 0);
    /* Note that we need to allocate MAX_PATH, since we are supposed to fill
     * the correct executable if there is no icon in lpIconPath directly.
     * lpIconPath itself is supposed to be large enough, so make sure lpIconPathW
     * is large enough too. Yes, I am puking too.
     */
    LPWSTR lpIconPathW = HeapAlloc(GetProcessHeap(), 0, MAX_PATH * sizeof(WCHAR));

    TRACE("%p %s %p\n", hInst, debugstr_a(lpIconPath), lpiIcon);

    if (lpIconPathW)
    {
        MultiByteToWideChar(CP_ACP, 0, lpIconPath, -1, lpIconPathW, len);
        hIcon = ExtractAssociatedIconW(hInst, lpIconPathW, lpiIcon);
        WideCharToMultiByte(CP_ACP, 0, lpIconPathW, -1, lpIconPath, MAX_PATH , NULL, NULL);
        HeapFree(GetProcessHeap(), 0, lpIconPathW);
    }
    return hIcon;
}

/*************************************************************************
 *				ExtractAssociatedIconW (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconW(HINSTANCE hInst, LPWSTR lpIconPath, LPWORD lpiIcon)
{
    HICON hIcon = NULL;
    WORD wDummyIcon = 0;

    TRACE("%p %s %p\n", hInst, debugstr_w(lpIconPath), lpiIcon);

    if(lpiIcon == NULL)
        lpiIcon = &wDummyIcon;

    hIcon = ExtractIconW(hInst, lpIconPath, *lpiIcon);

    if( hIcon < (HICON)2 )
    { if( hIcon == (HICON)1 ) /* no icons found in given file */
      { WCHAR tempPath[MAX_PATH];
        HINSTANCE uRet = FindExecutableW(lpIconPath,NULL,tempPath);

        if( uRet > (HINSTANCE)32 && tempPath[0] )
        { lstrcpyW(lpIconPath,tempPath);
          hIcon = ExtractIconW(hInst, lpIconPath, *lpiIcon);
          if( hIcon > (HICON)2 )
            return hIcon;
        }
      }

      if( hIcon == (HICON)1 )
        *lpiIcon = 2;   /* MSDOS icon - we found .exe but no icons in it */
      else
        *lpiIcon = 6;   /* generic icon - found nothing */

      if (GetModuleFileNameW(hInst, lpIconPath, MAX_PATH))
        hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(*lpiIcon));
    }
    return hIcon;
}

/*************************************************************************
 *				ExtractAssociatedIconExW (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconExW(HINSTANCE hInst, LPWSTR lpIconPath, LPWORD lpiIconIdx, LPWORD lpiIconId)
{
  FIXME("%p %s %p %p): stub\n", hInst, debugstr_w(lpIconPath), lpiIconIdx, lpiIconId);
  return 0;
}

/*************************************************************************
 *				ExtractAssociatedIconExA (SHELL32.@)
 *
 * Return icon for given file (either from file itself or from associated
 * executable) and patch parameters if needed.
 */
HICON WINAPI ExtractAssociatedIconExA(HINSTANCE hInst, LPSTR lpIconPath, LPWORD lpiIconIdx, LPWORD lpiIconId)
{
  HICON ret;
  INT len = MultiByteToWideChar( CP_ACP, 0, lpIconPath, -1, NULL, 0 );
  LPWSTR lpwstrFile = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );

  TRACE("%p %s %p %p)\n", hInst, lpIconPath, lpiIconIdx, lpiIconId);

  MultiByteToWideChar( CP_ACP, 0, lpIconPath, -1, lpwstrFile, len );
  ret = ExtractAssociatedIconExW(hInst, lpwstrFile, lpiIconIdx, lpiIconId);
  HeapFree(GetProcessHeap(), 0, lpwstrFile);
  return ret;
}


/****************************************************************************
 * SHDefExtractIconW		[SHELL32.@]
 */
HRESULT WINAPI SHDefExtractIconW(LPCWSTR pszIconFile, int iIndex, UINT uFlags,
                                 HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
	UINT ret;
	HICON hIcons[2];
	WARN("%s %d 0x%08x %p %p %d, semi-stub\n", debugstr_w(pszIconFile), iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);

	ret = PrivateExtractIconsW(pszIconFile, iIndex, nIconSize, nIconSize, hIcons, NULL, 2, LR_DEFAULTCOLOR);
	/* FIXME: deal with uFlags parameter which contains GIL_ flags */
	if (ret == 0xFFFFFFFF)
	  return E_FAIL;
	if (ret > 0) {
	  if (phiconLarge)
	    *phiconLarge = hIcons[0];
	  else
	    DestroyIcon(hIcons[0]);
	  if (phiconSmall)
	    *phiconSmall = hIcons[1];
	  else
	    DestroyIcon(hIcons[1]);
	  return S_OK;
	}
	return S_FALSE;
}

/****************************************************************************
 * SHDefExtractIconA		[SHELL32.@]
 */
HRESULT WINAPI SHDefExtractIconA(LPCSTR pszIconFile, int iIndex, UINT uFlags,
                                 HICON* phiconLarge, HICON* phiconSmall, UINT nIconSize)
{
  HRESULT ret;
  INT len = MultiByteToWideChar(CP_ACP, 0, pszIconFile, -1, NULL, 0);
  LPWSTR lpwstrFile = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));

  TRACE("%s %d 0x%08x %p %p %d\n", pszIconFile, iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);

  MultiByteToWideChar(CP_ACP, 0, pszIconFile, -1, lpwstrFile, len);
  ret = SHDefExtractIconW(lpwstrFile, iIndex, uFlags, phiconLarge, phiconSmall, nIconSize);
  HeapFree(GetProcessHeap(), 0, lpwstrFile);
  return ret;
}


/****************************************************************************
 * SHGetIconOverlayIndexA    [SHELL32.@]
 *
 * Returns the index of the overlay icon in the system image list.
 */
INT WINAPI SHGetIconOverlayIndexA(LPCSTR pszIconPath, INT iIconIndex)
{
  FIXME("%s, %d\n", debugstr_a(pszIconPath), iIconIndex);

  return -1;
}

/****************************************************************************
 * SHGetIconOverlayIndexW    [SHELL32.@]
 *
 * Returns the index of the overlay icon in the system image list.
 */
INT WINAPI SHGetIconOverlayIndexW(LPCWSTR pszIconPath, INT iIconIndex)
{
  FIXME("%s, %d\n", debugstr_w(pszIconPath), iIconIndex);

  return -1;
}

/****************************************************************************
 * SHGetStockIconInfo [SHELL32.@]
 *
 * Receive information for builtin icons
 *
 * PARAMS
 *  id      [I]  selected icon-id to get information for
 *  flags   [I]  selects the information to receive
 *  sii     [IO] SHSTOCKICONINFO structure to fill
 *
 * RETURNS
 *  Success: S_OK
 *  Failure: A HRESULT failure code
 *
 */
HRESULT WINAPI SHGetStockIconInfo(SHSTOCKICONID id, UINT flags, SHSTOCKICONINFO *sii)
{
    static const WCHAR shell32dll[] = {'\\','s','h','e','l','l','3','2','.','d','l','l',0};

    FIXME("(%d, 0x%x, %p) semi-stub\n", id, flags, sii);
    if ((id < 0) || (id >= SIID_MAX_ICONS) || !sii || (sii->cbSize != sizeof(SHSTOCKICONINFO))) {
        return E_INVALIDARG;
    }

    GetSystemDirectoryW(sii->szPath, MAX_PATH);

    /* no icons defined: use default */
    sii->iIcon = -IDI_SHELL_DOCUMENT;
    lstrcatW(sii->szPath, shell32dll);

    if (flags)
        FIXME("flags 0x%x not implemented\n", flags);

    sii->hIcon = NULL;
    sii->iSysImageIndex = -1;

    TRACE("%3d: returning %s (%d)\n", id, debugstr_w(sii->szPath), sii->iIcon);

    return S_OK;
}
