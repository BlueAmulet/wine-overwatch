/*
 * WineCfg Staging panel
 *
 * Copyright 2014 Michael Müller
 * Copyright 2015 Sebastian Lackner
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

#define COBJMACROS

#include "config.h"

#include <windows.h>
#include <wine/unicode.h>

#include "resource.h"
#include "winecfg.h"

/*
 * Command stream multithreading
 */
static BOOL csmt_get(void)
{
    BOOL ret;
    char *value = get_reg_key(config_key, keypath("DllRedirects"), "wined3d", NULL);
    ret = (value && !strcmp(value, "wined3d-csmt.dll"));
    HeapFree(GetProcessHeap(), 0, value);
    return ret;
}
static void csmt_set(BOOL status)
{
    set_reg_key(config_key, keypath("DllRedirects"), "wined3d", status ? "wined3d-csmt.dll" : NULL);
}


static void load_staging_settings(HWND dialog)
{
    CheckDlgButton(dialog, IDC_ENABLE_CSMT, csmt_get() ? BST_CHECKED : BST_UNCHECKED);
}

INT_PTR CALLBACK StagingDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        break;

    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->code == PSN_SETACTIVE)
            load_staging_settings(hDlg);
        break;

    case WM_SHOWWINDOW:
        set_window_title(hDlg);
        break;

    case WM_DESTROY:
        break;

    case WM_COMMAND:
        if (HIWORD(wParam) != BN_CLICKED) break;
        switch (LOWORD(wParam))
        {
        case IDC_ENABLE_CSMT:
            csmt_set(IsDlgButtonChecked(hDlg, IDC_ENABLE_CSMT) == BST_CHECKED);
            SendMessageW(GetParent(hDlg), PSM_CHANGED, 0, 0);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
