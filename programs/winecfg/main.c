/*
 * WineCfg main entry point
 *
 * Copyright 2002 Jaco Greeff
 * Copyright 2003 Dimitrie O. Paun
 * Copyright 2003 Mike Hearn
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
 *
 */

#define WIN32_LEAN_AND_MEAN
#define NONAMELESSUNION

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <wine/debug.h>

#include "resource.h"
#include "winecfg.h"

WINE_DEFAULT_DEBUG_CHANNEL(winecfg);

static INT CALLBACK
PropSheetCallback (HWND hWnd, UINT uMsg, LPARAM lParam)
{
    switch (uMsg)
    {
	/*
	 * hWnd = NULL, lParam == dialog resource
	 */
    case PSCB_PRECREATE:
	break;

    case PSCB_INITIALIZED:
        /* Set the window icon */
        SendMessageW( hWnd, WM_SETICON, ICON_BIG,
                      (LPARAM)LoadIconW( (HINSTANCE)GetWindowLongPtrW(hWnd, GWLP_HINSTANCE),
                                         MAKEINTRESOURCEW(IDI_WINECFG) ));
	break;

    default:
	break;
    }
    return 0;
}

#define NUM_PROPERTY_PAGES 8

static INT_PTR
doPropertySheet (HINSTANCE hInstance, HWND hOwner)
{
    PROPSHEETPAGEW psp[NUM_PROPERTY_PAGES];
    PROPSHEETHEADERW psh;
    int pg = 0; /* start with page 0 */

    /*
     * Fill out the (Applications) PROPSHEETPAGE data structure 
     * for the property sheet
     */
    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_APPCFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = AppDlgProc;
    psp[pg].pszTitle = load_string (IDS_TAB_APPLICATIONS);
    psp[pg].lParam = 0;
    pg++;

    /*
     * Fill out the (Libraries) PROPSHEETPAGE data structure 
     * for the property sheet
     */
    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_DLLCFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = LibrariesDlgProc;
    psp[pg].pszTitle = load_string (IDS_TAB_DLLS);
    psp[pg].lParam = 0;
    pg++;
    
    /*
     * Fill out the (X11Drv) PROPSHEETPAGE data structure 
     * for the property sheet
     */
    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_GRAPHCFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = GraphDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_GRAPHICS);
    psp[pg].lParam = 0;
    pg++;

    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_DESKTOP_INTEGRATION);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = ThemeDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_DESKTOP_INTEGRATION);
    psp[pg].lParam = 0;
    pg++;

    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_DRIVECFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = DriveDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_DRIVES);
    psp[pg].lParam = 0;
    pg++;

    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_AUDIOCFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = AudioDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_AUDIO);
    psp[pg].lParam = 0;
    pg++;

    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_STAGING);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = StagingDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_STAGING);
    psp[pg].lParam = 0;
    pg++;

    /*
     * Fill out the (General) PROPSHEETPAGE data structure 
     * for the property sheet
     */
    psp[pg].dwSize = sizeof (PROPSHEETPAGEW);
    psp[pg].dwFlags = PSP_USETITLE;
    psp[pg].hInstance = hInstance;
    psp[pg].u.pszTemplate = MAKEINTRESOURCEW (IDD_ABOUTCFG);
    psp[pg].u2.pszIcon = NULL;
    psp[pg].pfnDlgProc = AboutDlgProc;
    psp[pg].pszTitle =  load_string (IDS_TAB_ABOUT);
    psp[pg].lParam = 0;
    pg++;

    /*
     * Fill out the PROPSHEETHEADER
     */
    psh.dwSize = sizeof (PROPSHEETHEADERW);
    psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USEICONID | PSH_USECALLBACK;
    psh.hwndParent = hOwner;
    psh.hInstance = hInstance;
    psh.u.pszIcon = MAKEINTRESOURCEW (IDI_WINECFG);
    psh.pszCaption =  load_string (IDS_WINECFG_TITLE);
    psh.nPages = NUM_PROPERTY_PAGES;
    psh.u3.ppsp = psp;
    psh.pfnCallback = PropSheetCallback;
    psh.u2.nStartPage = 0;

    /*
     * Display the modal property sheet
     */
    return PropertySheetW (&psh);
}

/******************************************************************************
 * Name       : ProcessCmdLine
 * Description: Checks command line parameters for 'autodetect drives' option
 * Parameters : lpCmdLine - the command line
 * Returns    : TRUE - if '/D' was found. Drive autodetection was carried out.
 *              FALSE - no '/D' option found in command line
 * Notes      : This is a very simple implementation, which only works 
 *              correctly if the one and only cmd line option is '/D' or
 *              no option at all. Has to be reworked, if more options are to
 *              be supported.
 */
static BOOL
ProcessCmdLine(LPSTR lpCmdLine)
{
    if ((lpCmdLine[0] == '/' || lpCmdLine[0] == '-') && 
        (lpCmdLine[1] == 'D' || lpCmdLine[1] == 'd')) 
    {
        gui_mode = FALSE;
        if (autodetect_drives()) {
            apply_drive_changes();
        }
        return TRUE;
    }

    return FALSE;
}

/*****************************************************************************
 * Name       : WinMain
 * Description: Main windows entry point
 * Parameters : hInstance
 *              hPrev
 *              szCmdLine
 *              nShow
 * Returns    : Program exit code
 */
int WINAPI
WinMain (HINSTANCE hInstance, HINSTANCE hPrev, LPSTR szCmdLine, int nShow)
{
    BOOL is_wow64;

    if (IsWow64Process( GetCurrentProcess(), &is_wow64 ) && is_wow64)
    {
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        WCHAR filename[MAX_PATH];
        void *redir;
        DWORD exit_code;

        memset( &si, 0, sizeof(si) );
        si.cb = sizeof(si);
        GetModuleFileNameW( 0, filename, MAX_PATH );

        Wow64DisableWow64FsRedirection( &redir );
        if (CreateProcessW( filename, GetCommandLineW(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            WINE_TRACE( "restarting %s\n", wine_dbgstr_w(filename) );
            WaitForSingleObject( pi.hProcess, INFINITE );
            GetExitCodeProcess( pi.hProcess, &exit_code );
            ExitProcess( exit_code );
        }
        else WINE_ERR( "failed to restart 64-bit %s, err %d\n", wine_dbgstr_w(filename), GetLastError() );
        Wow64RevertWow64FsRedirection( redir );
    }

    if (ProcessCmdLine(szCmdLine)) {
        return 0;
    }

    if (initialize(hInstance)) {
	WINE_ERR("initialization failed, aborting\n");
	ExitProcess(1);
    }
    
    /*
     * The next 9 lines should be all that is needed
     * for the Wine Configuration property sheet
     */
    InitCommonControls ();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (doPropertySheet (hInstance, NULL) > 0) {
	WINE_TRACE("OK\n");
    } else {
	WINE_TRACE("Cancel\n");
    }
    CoUninitialize(); 
    ExitProcess (0);

    return 0;
}
