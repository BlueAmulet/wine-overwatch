/*
 * USER Input processing
 *
 * Copyright 1993 Bob Amstadt
 * Copyright 1996 Albrecht Kleine
 * Copyright 1997 David Faure
 * Copyright 1998 Morten Welinder
 * Copyright 1998 Ulrich Weigand
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

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#define NONAMELESSUNION

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winnls.h"
#include "winternl.h"
#include "winerror.h"
#include "win.h"
#include "user_private.h"
#include "wine/server.h"
#include "wine/debug.h"
#include "wine/unicode.h"

WINE_DEFAULT_DEBUG_CHANNEL(win);
WINE_DECLARE_DEBUG_CHANNEL(keyboard);

INT global_key_state_counter = 0;

/***********************************************************************
 *           get_key_state
 */
static WORD get_key_state(void)
{
    WORD ret = 0;

    if (GetSystemMetrics( SM_SWAPBUTTON ))
    {
        if (GetAsyncKeyState(VK_RBUTTON) & 0x80) ret |= MK_LBUTTON;
        if (GetAsyncKeyState(VK_LBUTTON) & 0x80) ret |= MK_RBUTTON;
    }
    else
    {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x80) ret |= MK_LBUTTON;
        if (GetAsyncKeyState(VK_RBUTTON) & 0x80) ret |= MK_RBUTTON;
    }
    if (GetAsyncKeyState(VK_MBUTTON) & 0x80)  ret |= MK_MBUTTON;
    if (GetAsyncKeyState(VK_SHIFT) & 0x80)    ret |= MK_SHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x80)  ret |= MK_CONTROL;
    if (GetAsyncKeyState(VK_XBUTTON1) & 0x80) ret |= MK_XBUTTON1;
    if (GetAsyncKeyState(VK_XBUTTON2) & 0x80) ret |= MK_XBUTTON2;
    return ret;
}


/**********************************************************************
 *		set_capture_window
 */
BOOL set_capture_window( HWND hwnd, UINT gui_flags, HWND *prev_ret )
{
    HWND previous = 0;
    UINT flags = 0;
    BOOL ret;

    if (gui_flags & GUI_INMENUMODE) flags |= CAPTURE_MENU;
    if (gui_flags & GUI_INMOVESIZE) flags |= CAPTURE_MOVESIZE;

    SERVER_START_REQ( set_capture_window )
    {
        req->handle = wine_server_user_handle( hwnd );
        req->flags  = flags;
        if ((ret = !wine_server_call_err( req )))
        {
            previous = wine_server_ptr_handle( reply->previous );
            hwnd = wine_server_ptr_handle( reply->full_handle );
        }
    }
    SERVER_END_REQ;

    if (ret)
    {
        USER_Driver->pSetCapture( hwnd, gui_flags );

        if (previous)
            SendMessageW( previous, WM_CAPTURECHANGED, 0, (LPARAM)hwnd );

        if (prev_ret) *prev_ret = previous;
    }
    return ret;
}


/***********************************************************************
 *		__wine_send_input  (USER32.@)
 *
 * Internal SendInput function to allow the graphics driver to inject real events.
 */
BOOL CDECL __wine_send_input( HWND hwnd, const INPUT *input )
{
    NTSTATUS status = send_hardware_message( hwnd, input, 0 );
    if (status) SetLastError( RtlNtStatusToDosError(status) );
    return !status;
}


/***********************************************************************
 *		update_mouse_coords
 *
 * Helper for SendInput.
 */
static void update_mouse_coords( INPUT *input )
{
    if (!(input->u.mi.dwFlags & MOUSEEVENTF_MOVE)) return;

    if (input->u.mi.dwFlags & MOUSEEVENTF_ABSOLUTE)
    {
        input->u.mi.dx = (input->u.mi.dx * GetSystemMetrics( SM_CXSCREEN )) >> 16;
        input->u.mi.dy = (input->u.mi.dy * GetSystemMetrics( SM_CYSCREEN )) >> 16;
    }
    else
    {
        int accel[3];

        /* dx and dy can be negative numbers for relative movements */
        SystemParametersInfoW(SPI_GETMOUSE, 0, accel, 0);

        if (!accel[2]) return;

        if (abs(input->u.mi.dx) > accel[0])
        {
            input->u.mi.dx *= 2;
            if ((abs(input->u.mi.dx) > accel[1]) && (accel[2] == 2)) input->u.mi.dx *= 2;
        }
        if (abs(input->u.mi.dy) > accel[0])
        {
            input->u.mi.dy *= 2;
            if ((abs(input->u.mi.dy) > accel[1]) && (accel[2] == 2)) input->u.mi.dy *= 2;
        }
    }
}

/***********************************************************************
 *		SendInput  (USER32.@)
 */
UINT WINAPI SendInput( UINT count, LPINPUT inputs, int size )
{
    UINT i;
    NTSTATUS status;

    for (i = 0; i < count; i++)
    {
        if (inputs[i].type == INPUT_MOUSE)
        {
            /* we need to update the coordinates to what the server expects */
            INPUT input = inputs[i];
            update_mouse_coords( &input );
            status = send_hardware_message( 0, &input, SEND_HWMSG_INJECTED );
        }
        else status = send_hardware_message( 0, &inputs[i], SEND_HWMSG_INJECTED );

        if (status)
        {
            SetLastError( RtlNtStatusToDosError(status) );
            break;
        }
    }

    return i;
}


/***********************************************************************
 *		keybd_event (USER32.@)
 */
void WINAPI keybd_event( BYTE bVk, BYTE bScan,
                         DWORD dwFlags, ULONG_PTR dwExtraInfo )
{
    INPUT input;

    input.type = INPUT_KEYBOARD;
    input.u.ki.wVk = bVk;
    input.u.ki.wScan = bScan;
    input.u.ki.dwFlags = dwFlags;
    input.u.ki.time = 0;
    input.u.ki.dwExtraInfo = dwExtraInfo;
    SendInput( 1, &input, sizeof(input) );
}


/***********************************************************************
 *		mouse_event (USER32.@)
 */
void WINAPI mouse_event( DWORD dwFlags, DWORD dx, DWORD dy,
                         DWORD dwData, ULONG_PTR dwExtraInfo )
{
    INPUT input;

    input.type = INPUT_MOUSE;
    input.u.mi.dx = dx;
    input.u.mi.dy = dy;
    input.u.mi.mouseData = dwData;
    input.u.mi.dwFlags = dwFlags;
    input.u.mi.time = 0;
    input.u.mi.dwExtraInfo = dwExtraInfo;
    SendInput( 1, &input, sizeof(input) );
}


/***********************************************************************
 *		GetCursorPos (USER32.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetCursorPos( POINT *pt )
{
    BOOL ret;
    DWORD last_change;

    if (!pt) return FALSE;

    SERVER_START_REQ( set_cursor )
    {
        if ((ret = !wine_server_call( req )))
        {
            pt->x = reply->new_x;
            pt->y = reply->new_y;
            last_change = reply->last_change;
        }
    }
    SERVER_END_REQ;

    /* query new position from graphics driver if we haven't updated recently */
    if (ret && GetTickCount() - last_change > 100) ret = USER_Driver->pGetCursorPos( pt );
    return ret;
}


/***********************************************************************
 *		GetCursorInfo (USER32.@)
 */
BOOL WINAPI GetCursorInfo( PCURSORINFO pci )
{
    BOOL ret;

    if (!pci) return FALSE;

    SERVER_START_REQ( get_thread_input )
    {
        req->tid = 0;
        if ((ret = !wine_server_call( req )))
        {
            pci->hCursor = wine_server_ptr_handle( reply->cursor );
            pci->flags = (reply->show_count >= 0) ? CURSOR_SHOWING : 0;
        }
    }
    SERVER_END_REQ;
    GetCursorPos(&pci->ptScreenPos);
    return ret;
}


/***********************************************************************
 * GetPhysicalCursorPos (USER32.@)
*/

BOOL WINAPI GetPhysicalCursorPos(POINT *point)
{
    FIXME("(%p) semi-stub: forwarding to GetCursorPos\n", point);
    return GetCursorPos(point);
}

/***********************************************************************
 *		SetCursorPos (USER32.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH SetCursorPos( INT x, INT y )
{
    BOOL ret;
    INT prev_x, prev_y, new_x, new_y;

    SERVER_START_REQ( set_cursor )
    {
        req->flags = SET_CURSOR_POS;
        req->x     = x;
        req->y     = y;
        if ((ret = !wine_server_call( req )))
        {
            prev_x = reply->prev_x;
            prev_y = reply->prev_y;
            new_x  = reply->new_x;
            new_y  = reply->new_y;
        }
    }
    SERVER_END_REQ;
    if (ret && (prev_x != new_x || prev_y != new_y)) USER_Driver->pSetCursorPos( new_x, new_y );
    return ret;
}

/***********************************************************************
 * SetPhysicalCursorPos (USER32.@)
*/

BOOL WINAPI SetPhysicalCursorPos(INT x, INT y)
{
    FIXME("(%u %u) semi-stub: forwarding to SetCursorPos\n", x, y);
    return SetCursorPos(x, y);
}

/**********************************************************************
 *		SetCapture (USER32.@)
 */
HWND WINAPI DECLSPEC_HOTPATCH SetCapture( HWND hwnd )
{
    HWND previous = 0;

    set_capture_window( hwnd, 0, &previous );
    return previous;
}


/**********************************************************************
 *		ReleaseCapture (USER32.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH ReleaseCapture(void)
{
    BOOL ret = set_capture_window( 0, 0, NULL );

    /* Somebody may have missed some mouse movements */
    if (ret) mouse_event( MOUSEEVENTF_MOVE, 0, 0, 0, 0 );

    return ret;
}


/**********************************************************************
 *		GetCapture (USER32.@)
 */
HWND WINAPI GetCapture(void)
{
    shmlocal_t *shm = wine_get_shmlocal();
    HWND ret = 0;

    if (shm) return wine_server_ptr_handle( shm->input_capture );
    SERVER_START_REQ( get_thread_input )
    {
        req->tid = GetCurrentThreadId();
        if (!wine_server_call_err( req )) ret = wine_server_ptr_handle( reply->capture );
    }
    SERVER_END_REQ;
    return ret;
}


static void check_for_events( UINT flags )
{
    if (USER_Driver->pMsgWaitForMultipleObjectsEx( 0, NULL, 0, flags, 0 ) == WAIT_TIMEOUT)
        flush_window_surfaces( TRUE );
}

/**********************************************************************
 *		GetAsyncKeyState (USER32.@)
 *
 *	Determine if a key is or was pressed.  retval has high-order
 * bit set to 1 if currently pressed, low-order bit set to 1 if key has
 * been pressed.
 */
SHORT WINAPI DECLSPEC_HOTPATCH GetAsyncKeyState( INT key )
{
    struct user_key_state_info *key_state_info = get_user_thread_info()->key_state;
    INT counter = global_key_state_counter;
    BYTE prev_key_state;
    SHORT ret;

    if (key < 0 || key >= 256) return 0;

    check_for_events( QS_INPUT );

    if ((ret = USER_Driver->pGetAsyncKeyState( key )) == -1)
    {
        if (key_state_info &&
            !(key_state_info->state[key] & 0xc0) &&
            key_state_info->counter == counter &&
            GetTickCount() - key_state_info->time < 50)
        {
            /* use cached value */
            return 0;
        }
        else if (!key_state_info)
        {
            key_state_info = HeapAlloc( GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*key_state_info) );
            get_user_thread_info()->key_state = key_state_info;
        }

        ret = 0;
        SERVER_START_REQ( get_key_state )
        {
            req->tid = 0;
            req->key = key;
            if (key_state_info)
            {
                prev_key_state = key_state_info->state[key];
                wine_server_set_reply( req, key_state_info->state, sizeof(key_state_info->state) );
            }
            if (!wine_server_call( req ))
            {
                if (reply->state & 0x40) ret |= 0x0001;
                if (reply->state & 0x80) ret |= 0x8000;
                if (key_state_info)
                {
                    /* force refreshing the key state cache - some multithreaded programs
                     * (like Adobe Photoshop CS5) expect that changes to the async key state
                     * are also immediately available in other threads. */
                    if (prev_key_state != key_state_info->state[key])
                        counter = interlocked_xchg_add( &global_key_state_counter, 1 ) + 1;

                    key_state_info->time    = GetTickCount();
                    key_state_info->counter = counter;
                }
            }
        }
        SERVER_END_REQ;
    }
    return ret;
}


/***********************************************************************
 *		GetQueueStatus (USER32.@)
 */
DWORD WINAPI GetQueueStatus( UINT flags )
{
    DWORD ret;

    if (flags & ~(QS_ALLINPUT | QS_ALLPOSTMESSAGE | QS_SMRESULT))
    {
        SetLastError( ERROR_INVALID_FLAGS );
        return 0;
    }

    check_for_events( flags );

    SERVER_START_REQ( get_queue_status )
    {
        req->clear_bits = flags;
        wine_server_call( req );
        ret = MAKELONG( reply->changed_bits & flags, reply->wake_bits & flags );
    }
    SERVER_END_REQ;
    return ret;
}


/***********************************************************************
 *		GetInputState   (USER32.@)
 */
BOOL WINAPI GetInputState(void)
{
    shmlocal_t *shm = wine_get_shmlocal();
    DWORD ret;

    check_for_events( QS_INPUT );

    /* req->clear is not set, so we can safely get the
     * wineserver status without an additional call. */
    if (shm)
    {
        ret = shm->queue_bits;
        goto done;
    }

    SERVER_START_REQ( get_queue_status )
    {
        req->clear_bits = 0;
        wine_server_call( req );
        ret = reply->wake_bits;
    }
    SERVER_END_REQ;

done:
    ret &= (QS_KEY | QS_MOUSEBUTTON);
    return ret;
}


/******************************************************************
 *              GetLastInputInfo (USER32.@)
 */
BOOL WINAPI GetLastInputInfo(PLASTINPUTINFO plii)
{
    BOOL ret;
    shmglobal_t *shm = wine_get_shmglobal();

    TRACE("%p\n", plii);

    if (plii->cbSize != sizeof (*plii) )
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (shm)
    {
        plii->dwTime = shm->last_input_time;
        return TRUE;
    }

    SERVER_START_REQ( get_last_input_time )
    {
        ret = !wine_server_call_err( req );
        if (ret)
            plii->dwTime = reply->time;
    }
    SERVER_END_REQ;
    return ret;
}


/******************************************************************
*		GetRawInputDeviceList (USER32.@)
*/
UINT WINAPI GetRawInputDeviceList(RAWINPUTDEVICELIST *devices, UINT *device_count, UINT size)
{
    TRACE("devices %p, device_count %p, size %u.\n", devices, device_count, size);

    if (size != sizeof(*devices))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return ~0U;
    }

    if (!device_count)
    {
        SetLastError(ERROR_NOACCESS);
        return ~0U;
    }

    if (!devices)
    {
        *device_count = 2;
        return 0;
    }

    if (*device_count < 2)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        *device_count = 2;
        return ~0U;
    }

    devices[0].hDevice = WINE_MOUSE_HANDLE;
    devices[0].dwType = RIM_TYPEMOUSE;
    devices[1].hDevice = WINE_KEYBOARD_HANDLE;
    devices[1].dwType = RIM_TYPEKEYBOARD;

    return 2;
}


/******************************************************************
*		RegisterRawInputDevices (USER32.@)
*/
BOOL WINAPI DECLSPEC_HOTPATCH RegisterRawInputDevices(RAWINPUTDEVICE *devices, UINT device_count, UINT size)
{
    struct rawinput_device *d;
    BOOL ret;
    UINT i;

    TRACE("devices %p, device_count %u, size %u.\n", devices, device_count, size);

    if (size != sizeof(*devices))
    {
        WARN("Invalid structure size %u.\n", size);
        return FALSE;
    }

    if (!(d = HeapAlloc( GetProcessHeap(), 0, device_count * sizeof(*d) ))) return FALSE;

    for (i = 0; i < device_count; ++i)
    {
        TRACE("device %u: page %#x, usage %#x, flags %#x, target %p.\n",
                i, devices[i].usUsagePage, devices[i].usUsage,
                devices[i].dwFlags, devices[i].hwndTarget);
        if (devices[i].dwFlags & ~RIDEV_REMOVE)
            FIXME("Unhandled flags %#x for device %u.\n", devices[i].dwFlags, i);

        d[i].usage_page = devices[i].usUsagePage;
        d[i].usage = devices[i].usUsage;
        d[i].flags = devices[i].dwFlags;
        d[i].target = wine_server_user_handle( devices[i].hwndTarget );
    }

    SERVER_START_REQ( update_rawinput_devices )
    {
        wine_server_add_data( req, d, device_count * sizeof(*d) );
        ret = !wine_server_call( req );
    }
    SERVER_END_REQ;

    HeapFree( GetProcessHeap(), 0, d );

    return ret;
}


/******************************************************************
*		GetRawInputData (USER32.@)
*/
UINT WINAPI GetRawInputData(HRAWINPUT rawinput, UINT command, void *data, UINT *data_size, UINT header_size)
{
    RAWINPUT *ri = (RAWINPUT *)rawinput;
    UINT s;

    TRACE("rawinput %p, command %#x, data %p, data_size %p, header_size %u.\n",
            rawinput, command, data, data_size, header_size);

    if (header_size != sizeof(RAWINPUTHEADER))
    {
        WARN("Invalid structure size %u.\n", header_size);
        return ~0U;
    }

    switch (command)
    {
    case RID_INPUT:
        s = ri->header.dwSize;
        break;
    case RID_HEADER:
        s = sizeof(RAWINPUTHEADER);
        break;
    default:
        return ~0U;
    }

    if (!data)
    {
        *data_size = s;
        return 0;
    }

    if (*data_size < s) return ~0U;
    memcpy(data, ri, s);
    return s;
}


/******************************************************************
*		GetRawInputBuffer (USER32.@)
*/
UINT WINAPI DECLSPEC_HOTPATCH GetRawInputBuffer(PRAWINPUT pData, PUINT pcbSize, UINT cbSizeHeader)
{
    FIXME("(pData=%p, pcbSize=%p, cbSizeHeader=%d) stub!\n", pData, pcbSize, cbSizeHeader);

    return 0;
}


/******************************************************************
*		GetRawInputDeviceInfoA (USER32.@)
*/
UINT WINAPI GetRawInputDeviceInfoA(HANDLE device, UINT command, void *data, UINT *data_size)
{
    UINT ret;

    TRACE("device %p, command %u, data %p, data_size %p.\n", device, command, data, data_size);

    ret = GetRawInputDeviceInfoW(device, command, data, data_size);
    if (command == RIDI_DEVICENAME && ret && ret != ~0U)
        ret = WideCharToMultiByte(CP_ACP, 0, data, -1, data, *data_size, NULL, NULL);

    return ret;
}


/******************************************************************
*		GetRawInputDeviceInfoW (USER32.@)
*/
UINT WINAPI GetRawInputDeviceInfoW(HANDLE device, UINT command, void *data, UINT *data_size)
{
    /* FIXME: Most of this is made up. */
    static const WCHAR keyboard_name[] = {'\\','\\','?','\\','W','I','N','E','_','K','E','Y','B','O','A','R','D',0};
    static const WCHAR mouse_name[] = {'\\','\\','?','\\','W','I','N','E','_','M','O','U','S','E',0};
    static const RID_DEVICE_INFO_KEYBOARD keyboard_info = {0, 0, 1, 12, 3, 101};
    static const RID_DEVICE_INFO_MOUSE mouse_info = {1, 5, 0, FALSE};
    const WCHAR *name = NULL;
    RID_DEVICE_INFO *info;
    UINT s;

    TRACE("device %p, command %u, data %p, data_size %p.\n", device, command, data, data_size);

    if (!data_size || (device != WINE_MOUSE_HANDLE && device != WINE_KEYBOARD_HANDLE)) return ~0U;

    switch (command)
    {
    case RIDI_DEVICENAME:
        if (device == WINE_MOUSE_HANDLE)
        {
            s = sizeof(mouse_name);
            name = mouse_name;
        }
        else
        {
            s = sizeof(keyboard_name);
            name = keyboard_name;
        }
        break;
    case RIDI_DEVICEINFO:
        s = sizeof(*info);
        break;
    default:
        return ~0U;
    }

    if (!data)
    {
        *data_size = s;
        return 0;
    }

    if (*data_size < s)
    {
        *data_size = s;
        return ~0U;
    }

    if (command == RIDI_DEVICENAME)
    {
        memcpy(data, name, s);
        return s;
    }

    info = data;
    info->cbSize = sizeof(*info);
    if (device == WINE_MOUSE_HANDLE)
    {
        info->dwType = RIM_TYPEMOUSE;
        info->u.mouse = mouse_info;
    }
    else
    {
        info->dwType = RIM_TYPEKEYBOARD;
        info->u.keyboard = keyboard_info;
    }
    return s;
}


/******************************************************************
*		GetRegisteredRawInputDevices (USER32.@)
*/
UINT WINAPI DECLSPEC_HOTPATCH GetRegisteredRawInputDevices(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize)
{
    FIXME("(pRawInputDevices=%p, puiNumDevices=%p, cbSize=%d) stub!\n", pRawInputDevices, puiNumDevices, cbSize);

    return 0;
}


/******************************************************************
*		DefRawInputProc (USER32.@)
*/
LRESULT WINAPI DefRawInputProc(PRAWINPUT *paRawInput, INT nInput, UINT cbSizeHeader)
{
    FIXME("(paRawInput=%p, nInput=%d, cbSizeHeader=%d) stub!\n", *paRawInput, nInput, cbSizeHeader);

    return 0;
}


/**********************************************************************
 *		AttachThreadInput (USER32.@)
 *
 * Attaches the input processing mechanism of one thread to that of
 * another thread.
 */
BOOL WINAPI AttachThreadInput( DWORD from, DWORD to, BOOL attach )
{
    BOOL ret;

    SERVER_START_REQ( attach_thread_input )
    {
        req->tid_from = from;
        req->tid_to   = to;
        req->attach   = attach;
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 *		GetKeyState (USER32.@)
 *
 * An application calls the GetKeyState function in response to a
 * keyboard-input message.  This function retrieves the state of the key
 * at the time the input message was generated.
 */
SHORT WINAPI DECLSPEC_HOTPATCH GetKeyState(INT vkey)
{
    SHORT retval = 0;

    SERVER_START_REQ( get_key_state )
    {
        req->tid = GetCurrentThreadId();
        req->key = vkey;
        if (!wine_server_call( req )) retval = (signed char)reply->state;
    }
    SERVER_END_REQ;
    TRACE("key (0x%x) -> %x\n", vkey, retval);
    return retval;
}


/**********************************************************************
 *		GetKeyboardState (USER32.@)
 */
BOOL WINAPI DECLSPEC_HOTPATCH GetKeyboardState( LPBYTE state )
{
    BOOL ret;

    TRACE("(%p)\n", state);

    memset( state, 0, 256 );
    SERVER_START_REQ( get_key_state )
    {
        req->tid = GetCurrentThreadId();
        req->key = -1;
        wine_server_set_reply( req, state, 256 );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 *		SetKeyboardState (USER32.@)
 */
BOOL WINAPI SetKeyboardState( LPBYTE state )
{
    BOOL ret;

    SERVER_START_REQ( set_key_state )
    {
        req->tid = GetCurrentThreadId();
        wine_server_add_data( req, state, 256 );
        ret = !wine_server_call_err( req );
    }
    SERVER_END_REQ;
    return ret;
}


/**********************************************************************
 *		VkKeyScanA (USER32.@)
 *
 * VkKeyScan translates an ANSI character to a virtual-key and shift code
 * for the current keyboard.
 * high-order byte yields :
 *	0	Unshifted
 *	1	Shift
 *	2	Ctrl
 *	3-5	Shift-key combinations that are not used for characters
 *	6	Ctrl-Alt
 *	7	Ctrl-Alt-Shift
 *	I.e. :	Shift = 1, Ctrl = 2, Alt = 4.
 * FIXME : works ok except for dead chars :
 * VkKeyScan '^'(0x5e, 94) ... got keycode 00 ... returning 00
 * VkKeyScan '`'(0x60, 96) ... got keycode 00 ... returning 00
 */
SHORT WINAPI VkKeyScanA(CHAR cChar)
{
    WCHAR wChar;

    if (IsDBCSLeadByte(cChar)) return -1;

    MultiByteToWideChar(CP_ACP, 0, &cChar, 1, &wChar, 1);
    return VkKeyScanW(wChar);
}

/******************************************************************************
 *		VkKeyScanW (USER32.@)
 */
SHORT WINAPI VkKeyScanW(WCHAR cChar)
{
    return VkKeyScanExW(cChar, GetKeyboardLayout(0));
}

/**********************************************************************
 *		VkKeyScanExA (USER32.@)
 */
WORD WINAPI VkKeyScanExA(CHAR cChar, HKL dwhkl)
{
    WCHAR wChar;

    if (IsDBCSLeadByte(cChar)) return -1;

    MultiByteToWideChar(CP_ACP, 0, &cChar, 1, &wChar, 1);
    return VkKeyScanExW(wChar, dwhkl);
}

/******************************************************************************
 *		VkKeyScanExW (USER32.@)
 */
WORD WINAPI VkKeyScanExW(WCHAR cChar, HKL dwhkl)
{
    return USER_Driver->pVkKeyScanEx(cChar, dwhkl);
}

/**********************************************************************
 *		OemKeyScan (USER32.@)
 */
DWORD WINAPI OemKeyScan( WORD oem )
{
    WCHAR wchr;
    DWORD vkey, scan;
    char oem_char = LOBYTE( oem );

    if (!OemToCharBuffW( &oem_char, &wchr, 1 ))
        return -1;

    vkey = VkKeyScanW( wchr );
    scan = MapVirtualKeyW( LOBYTE( vkey ), MAPVK_VK_TO_VSC );
    if (!scan) return -1;

    vkey &= 0xff00;
    vkey <<= 8;
    return vkey | scan;
}

/******************************************************************************
 *		GetKeyboardType (USER32.@)
 */
INT WINAPI GetKeyboardType(INT nTypeFlag)
{
    TRACE_(keyboard)("(%d)\n", nTypeFlag);
    switch(nTypeFlag)
    {
    case 0:      /* Keyboard type */
        return 4;    /* AT-101 */
    case 1:      /* Keyboard Subtype */
        return 0;    /* There are no defined subtypes */
    case 2:      /* Number of F-keys */
        return 12;   /* We're doing an 101 for now, so return 12 F-keys */
    default:
        WARN_(keyboard)("Unknown type\n");
        return 0;    /* The book says 0 here, so 0 */
    }
}

/******************************************************************************
 *		MapVirtualKeyA (USER32.@)
 */
UINT WINAPI MapVirtualKeyA(UINT code, UINT maptype)
{
    return MapVirtualKeyExA( code, maptype, GetKeyboardLayout(0) );
}

/******************************************************************************
 *		MapVirtualKeyW (USER32.@)
 */
UINT WINAPI MapVirtualKeyW(UINT code, UINT maptype)
{
    return MapVirtualKeyExW(code, maptype, GetKeyboardLayout(0));
}

/******************************************************************************
 *		MapVirtualKeyExA (USER32.@)
 */
UINT WINAPI MapVirtualKeyExA(UINT code, UINT maptype, HKL hkl)
{
    UINT ret;

    ret = MapVirtualKeyExW( code, maptype, hkl );
    if (maptype == MAPVK_VK_TO_CHAR)
    {
        BYTE ch = 0;
        WCHAR wch = ret;

        WideCharToMultiByte( CP_ACP, 0, &wch, 1, (LPSTR)&ch, 1, NULL, NULL );
        ret = ch;
    }
    return ret;
}

/******************************************************************************
 *		MapVirtualKeyExW (USER32.@)
 */
UINT WINAPI MapVirtualKeyExW(UINT code, UINT maptype, HKL hkl)
{
    TRACE_(keyboard)("(%X, %d, %p)\n", code, maptype, hkl);

    return USER_Driver->pMapVirtualKeyEx(code, maptype, hkl);
}

/****************************************************************************
 *		GetKBCodePage (USER32.@)
 */
UINT WINAPI GetKBCodePage(void)
{
    return GetOEMCP();
}

/***********************************************************************
 *		GetKeyboardLayout (USER32.@)
 *
 *        - device handle for keyboard layout defaulted to
 *          the language id. This is the way Windows default works.
 *        - the thread identifier is also ignored.
 */
HKL WINAPI GetKeyboardLayout(DWORD thread_id)
{
    return USER_Driver->pGetKeyboardLayout(thread_id);
}

/****************************************************************************
 *		GetKeyboardLayoutNameA (USER32.@)
 */
BOOL WINAPI GetKeyboardLayoutNameA(LPSTR pszKLID)
{
    WCHAR buf[KL_NAMELENGTH];

    if (GetKeyboardLayoutNameW(buf))
        return WideCharToMultiByte( CP_ACP, 0, buf, -1, pszKLID, KL_NAMELENGTH, NULL, NULL ) != 0;
    return FALSE;
}

/****************************************************************************
 *		GetKeyboardLayoutNameW (USER32.@)
 */
BOOL WINAPI GetKeyboardLayoutNameW(LPWSTR pwszKLID)
{
    if (!pwszKLID)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    return USER_Driver->pGetKeyboardLayoutName(pwszKLID);
}

/****************************************************************************
 *		GetKeyNameTextA (USER32.@)
 */
INT WINAPI GetKeyNameTextA(LONG lParam, LPSTR lpBuffer, INT nSize)
{
    WCHAR buf[256];
    INT ret;

    if (!nSize || !GetKeyNameTextW(lParam, buf, 256))
    {
        lpBuffer[0] = 0;
        return 0;
    }
    ret = WideCharToMultiByte(CP_ACP, 0, buf, -1, lpBuffer, nSize, NULL, NULL);
    if (!ret && nSize)
    {
        ret = nSize - 1;
        lpBuffer[ret] = 0;
    }
    else ret--;

    return ret;
}

/****************************************************************************
 *		GetKeyNameTextW (USER32.@)
 */
INT WINAPI GetKeyNameTextW(LONG lParam, LPWSTR lpBuffer, INT nSize)
{
    if (!lpBuffer || !nSize) return 0;
    return USER_Driver->pGetKeyNameText( lParam, lpBuffer, nSize );
}

/****************************************************************************
 *		ToUnicode (USER32.@)
 */
INT WINAPI ToUnicode(UINT virtKey, UINT scanCode, const BYTE *lpKeyState,
		     LPWSTR lpwStr, int size, UINT flags)
{
    return ToUnicodeEx(virtKey, scanCode, lpKeyState, lpwStr, size, flags, GetKeyboardLayout(0));
}

/****************************************************************************
 *		ToUnicodeEx (USER32.@)
 */
INT WINAPI ToUnicodeEx(UINT virtKey, UINT scanCode, const BYTE *lpKeyState,
		       LPWSTR lpwStr, int size, UINT flags, HKL hkl)
{
    if (!lpKeyState) return 0;
    return USER_Driver->pToUnicodeEx(virtKey, scanCode, lpKeyState, lpwStr, size, flags, hkl);
}

/****************************************************************************
 *		ToAscii (USER32.@)
 */
INT WINAPI ToAscii( UINT virtKey, UINT scanCode, const BYTE *lpKeyState,
                    LPWORD lpChar, UINT flags )
{
    return ToAsciiEx(virtKey, scanCode, lpKeyState, lpChar, flags, GetKeyboardLayout(0));
}

/****************************************************************************
 *		ToAsciiEx (USER32.@)
 */
INT WINAPI ToAsciiEx( UINT virtKey, UINT scanCode, const BYTE *lpKeyState,
                      LPWORD lpChar, UINT flags, HKL dwhkl )
{
    WCHAR uni_chars[2];
    INT ret, n_ret;

    ret = ToUnicodeEx(virtKey, scanCode, lpKeyState, uni_chars, 2, flags, dwhkl);
    if (ret < 0) n_ret = 1; /* FIXME: make ToUnicode return 2 for dead chars */
    else n_ret = ret;
    WideCharToMultiByte(CP_ACP, 0, uni_chars, n_ret, (LPSTR)lpChar, 2, NULL, NULL);
    return ret;
}

/**********************************************************************
 *		ActivateKeyboardLayout (USER32.@)
 */
HKL WINAPI ActivateKeyboardLayout(HKL hLayout, UINT flags)
{
    TRACE_(keyboard)("(%p, %d)\n", hLayout, flags);

    return USER_Driver->pActivateKeyboardLayout(hLayout, flags);
}

/**********************************************************************
 *		BlockInput (USER32.@)
 */
BOOL WINAPI BlockInput(BOOL fBlockIt)
{
    FIXME_(keyboard)("(%d): stub\n", fBlockIt);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

    return FALSE;
}

/***********************************************************************
 *		GetKeyboardLayoutList (USER32.@)
 *
 * Return number of values available if either input parm is
 *  0, per MS documentation.
 */
UINT WINAPI GetKeyboardLayoutList(INT nBuff, HKL *layouts)
{
    TRACE_(keyboard)( "(%d, %p)\n", nBuff, layouts );

    return USER_Driver->pGetKeyboardLayoutList( nBuff, layouts );
}


/***********************************************************************
 *		RegisterHotKey (USER32.@)
 */
BOOL WINAPI RegisterHotKey(HWND hwnd,INT id,UINT modifiers,UINT vk)
{
    BOOL ret;
    int replaced=0;

    TRACE_(keyboard)("(%p,%d,0x%08x,%X)\n",hwnd,id,modifiers,vk);

    if ((hwnd == NULL || WIN_IsCurrentThread(hwnd)) &&
        !USER_Driver->pRegisterHotKey(hwnd, modifiers, vk))
        return FALSE;

    SERVER_START_REQ( register_hotkey )
    {
        req->window = wine_server_user_handle( hwnd );
        req->id = id;
        req->flags = modifiers;
        req->vkey = vk;
        if ((ret = !wine_server_call_err( req )))
        {
            replaced = reply->replaced;
            modifiers = reply->flags;
            vk = reply->vkey;
        }
    }
    SERVER_END_REQ;

    if (ret && replaced)
        USER_Driver->pUnregisterHotKey(hwnd, modifiers, vk);

    return ret;
}

/***********************************************************************
 *		UnregisterHotKey (USER32.@)
 */
BOOL WINAPI UnregisterHotKey(HWND hwnd,INT id)
{
    BOOL ret;
    UINT modifiers, vk;

    TRACE_(keyboard)("(%p,%d)\n",hwnd,id);

    SERVER_START_REQ( unregister_hotkey )
    {
        req->window = wine_server_user_handle( hwnd );
        req->id = id;
        if ((ret = !wine_server_call_err( req )))
        {
            modifiers = reply->flags;
            vk = reply->vkey;
        }
    }
    SERVER_END_REQ;

    if (ret)
        USER_Driver->pUnregisterHotKey(hwnd, modifiers, vk);

    return ret;
}

/***********************************************************************
 *		LoadKeyboardLayoutW (USER32.@)
 */
HKL WINAPI LoadKeyboardLayoutW(LPCWSTR pwszKLID, UINT Flags)
{
    TRACE_(keyboard)("(%s, %d)\n", debugstr_w(pwszKLID), Flags);

    return USER_Driver->pLoadKeyboardLayout(pwszKLID, Flags);
}

/***********************************************************************
 *		LoadKeyboardLayoutA (USER32.@)
 */
HKL WINAPI LoadKeyboardLayoutA(LPCSTR pwszKLID, UINT Flags)
{
    HKL ret;
    UNICODE_STRING pwszKLIDW;

    if (pwszKLID) RtlCreateUnicodeStringFromAsciiz(&pwszKLIDW, pwszKLID);
    else pwszKLIDW.Buffer = NULL;

    ret = LoadKeyboardLayoutW(pwszKLIDW.Buffer, Flags);
    RtlFreeUnicodeString(&pwszKLIDW);
    return ret;
}


/***********************************************************************
 *		UnloadKeyboardLayout (USER32.@)
 */
BOOL WINAPI UnloadKeyboardLayout(HKL hkl)
{
    TRACE_(keyboard)("(%p)\n", hkl);

    return USER_Driver->pUnloadKeyboardLayout(hkl);
}

typedef struct __TRACKINGLIST {
    TRACKMOUSEEVENT tme;
    POINT pos; /* center of hover rectangle */
} _TRACKINGLIST;

/* FIXME: move tracking stuff into a per thread data */
static _TRACKINGLIST tracking_info;
static UINT_PTR timer;

static void check_mouse_leave(HWND hwnd, int hittest)
{
    if (tracking_info.tme.hwndTrack != hwnd)
    {
        if (tracking_info.tme.dwFlags & TME_NONCLIENT)
            PostMessageW(tracking_info.tme.hwndTrack, WM_NCMOUSELEAVE, 0, 0);
        else
            PostMessageW(tracking_info.tme.hwndTrack, WM_MOUSELEAVE, 0, 0);

        /* remove the TME_LEAVE flag */
        tracking_info.tme.dwFlags &= ~TME_LEAVE;
    }
    else
    {
        if (hittest == HTCLIENT)
        {
            if (tracking_info.tme.dwFlags & TME_NONCLIENT)
            {
                PostMessageW(tracking_info.tme.hwndTrack, WM_NCMOUSELEAVE, 0, 0);
                /* remove the TME_LEAVE flag */
                tracking_info.tme.dwFlags &= ~TME_LEAVE;
            }
        }
        else
        {
            if (!(tracking_info.tme.dwFlags & TME_NONCLIENT))
            {
                PostMessageW(tracking_info.tme.hwndTrack, WM_MOUSELEAVE, 0, 0);
                /* remove the TME_LEAVE flag */
                tracking_info.tme.dwFlags &= ~TME_LEAVE;
            }
        }
    }
}

static void CALLBACK TrackMouseEventProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent,
                                         DWORD dwTime)
{
    POINT pos;
    INT hoverwidth = 0, hoverheight = 0, hittest;

    TRACE("hwnd %p, msg %04x, id %04lx, time %u\n", hwnd, uMsg, idEvent, dwTime);

    GetCursorPos(&pos);
    hwnd = WINPOS_WindowFromPoint(hwnd, pos, &hittest);

    TRACE("point %s hwnd %p hittest %d\n", wine_dbgstr_point(&pos), hwnd, hittest);

    SystemParametersInfoW(SPI_GETMOUSEHOVERWIDTH, 0, &hoverwidth, 0);
    SystemParametersInfoW(SPI_GETMOUSEHOVERHEIGHT, 0, &hoverheight, 0);

    TRACE("tracked pos %s, current pos %s, hover width %d, hover height %d\n",
           wine_dbgstr_point(&tracking_info.pos), wine_dbgstr_point(&pos),
           hoverwidth, hoverheight);

    /* see if this tracking event is looking for TME_LEAVE and that the */
    /* mouse has left the window */
    if (tracking_info.tme.dwFlags & TME_LEAVE)
    {
        check_mouse_leave(hwnd, hittest);
    }

    if (tracking_info.tme.hwndTrack != hwnd)
    {
        /* mouse is gone, stop tracking mouse hover */
        tracking_info.tme.dwFlags &= ~TME_HOVER;
    }

    /* see if we are tracking hovering for this hwnd */
    if (tracking_info.tme.dwFlags & TME_HOVER)
    {
        /* has the cursor moved outside the rectangle centered around pos? */
        if ((abs(pos.x - tracking_info.pos.x) > (hoverwidth / 2)) ||
            (abs(pos.y - tracking_info.pos.y) > (hoverheight / 2)))
        {
            /* record this new position as the current position */
            tracking_info.pos = pos;
        }
        else
        {
            if (hittest == HTCLIENT)
            {
                ScreenToClient(hwnd, &pos);
                TRACE("client cursor pos %s\n", wine_dbgstr_point(&pos));

                PostMessageW(tracking_info.tme.hwndTrack, WM_MOUSEHOVER,
                             get_key_state(), MAKELPARAM( pos.x, pos.y ));
            }
            else
            {
                if (tracking_info.tme.dwFlags & TME_NONCLIENT)
                    PostMessageW(tracking_info.tme.hwndTrack, WM_NCMOUSEHOVER,
                                 hittest, MAKELPARAM( pos.x, pos.y ));
            }

            /* stop tracking mouse hover */
            tracking_info.tme.dwFlags &= ~TME_HOVER;
        }
    }

    /* stop the timer if the tracking list is empty */
    if (!(tracking_info.tme.dwFlags & (TME_HOVER | TME_LEAVE)))
    {
        KillSystemTimer(tracking_info.tme.hwndTrack, timer);
        timer = 0;
        tracking_info.tme.hwndTrack = 0;
        tracking_info.tme.dwFlags = 0;
        tracking_info.tme.dwHoverTime = 0;
    }
}


/***********************************************************************
 * TrackMouseEvent [USER32]
 *
 * Requests notification of mouse events
 *
 * During mouse tracking WM_MOUSEHOVER or WM_MOUSELEAVE events are posted
 * to the hwnd specified in the ptme structure.  After the event message
 * is posted to the hwnd, the entry in the queue is removed.
 *
 * If the current hwnd isn't ptme->hwndTrack the TME_HOVER flag is completely
 * ignored. The TME_LEAVE flag results in a WM_MOUSELEAVE message being posted
 * immediately and the TME_LEAVE flag being ignored.
 *
 * PARAMS
 *     ptme [I,O] pointer to TRACKMOUSEEVENT information structure.
 *
 * RETURNS
 *     Success: non-zero
 *     Failure: zero
 *
 */

BOOL WINAPI
TrackMouseEvent (TRACKMOUSEEVENT *ptme)
{
    HWND hwnd;
    POINT pos;
    DWORD hover_time;
    INT hittest;

    TRACE("%x, %x, %p, %u\n", ptme->cbSize, ptme->dwFlags, ptme->hwndTrack, ptme->dwHoverTime);

    if (ptme->cbSize != sizeof(TRACKMOUSEEVENT)) {
        WARN("wrong TRACKMOUSEEVENT size from app\n");
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    /* fill the TRACKMOUSEEVENT struct with the current tracking for the given hwnd */
    if (ptme->dwFlags & TME_QUERY )
    {
        *ptme = tracking_info.tme;
        /* set cbSize in the case it's not initialized yet */
        ptme->cbSize = sizeof(TRACKMOUSEEVENT);

        return TRUE; /* return here, TME_QUERY is retrieving information */
    }

    if (!IsWindow(ptme->hwndTrack))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }

    hover_time = (ptme->dwFlags & TME_HOVER) ? ptme->dwHoverTime : HOVER_DEFAULT;

    /* if HOVER_DEFAULT was specified replace this with the system's current value.
     * TME_LEAVE doesn't need to specify hover time so use default */
    if (hover_time == HOVER_DEFAULT || hover_time == 0)
        SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &hover_time, 0);

    GetCursorPos(&pos);
    hwnd = WINPOS_WindowFromPoint(ptme->hwndTrack, pos, &hittest);
    TRACE("point %s hwnd %p hittest %d\n", wine_dbgstr_point(&pos), hwnd, hittest);

    if (ptme->dwFlags & ~(TME_CANCEL | TME_HOVER | TME_LEAVE | TME_NONCLIENT))
        FIXME("Unknown flag(s) %08x\n", ptme->dwFlags & ~(TME_CANCEL | TME_HOVER | TME_LEAVE | TME_NONCLIENT));

    if (ptme->dwFlags & TME_CANCEL)
    {
        if (tracking_info.tme.hwndTrack == ptme->hwndTrack)
        {
            tracking_info.tme.dwFlags &= ~(ptme->dwFlags & ~TME_CANCEL);

            /* if we aren't tracking on hover or leave remove this entry */
            if (!(tracking_info.tme.dwFlags & (TME_HOVER | TME_LEAVE)))
            {
                KillSystemTimer(tracking_info.tme.hwndTrack, timer);
                timer = 0;
                tracking_info.tme.hwndTrack = 0;
                tracking_info.tme.dwFlags = 0;
                tracking_info.tme.dwHoverTime = 0;
            }
        }
    } else {
        /* In our implementation it's possible that another window will receive a
         * WM_MOUSEMOVE and call TrackMouseEvent before TrackMouseEventProc is
         * called. In such a situation post the WM_MOUSELEAVE now */
        if (tracking_info.tme.dwFlags & TME_LEAVE && tracking_info.tme.hwndTrack != NULL)
            check_mouse_leave(hwnd, hittest);

        if (timer)
        {
            KillSystemTimer(tracking_info.tme.hwndTrack, timer);
            timer = 0;
            tracking_info.tme.hwndTrack = 0;
            tracking_info.tme.dwFlags = 0;
            tracking_info.tme.dwHoverTime = 0;
        }

        if (ptme->hwndTrack == hwnd)
        {
            /* Adding new mouse event to the tracking list */
            tracking_info.tme = *ptme;
            tracking_info.tme.dwHoverTime = hover_time;

            /* Initialize HoverInfo variables even if not hover tracking */
            tracking_info.pos = pos;

            timer = SetSystemTimer(tracking_info.tme.hwndTrack, (UINT_PTR)&tracking_info.tme, hover_time, TrackMouseEventProc);
        }
    }

    return TRUE;
}

/***********************************************************************
 * GetMouseMovePointsEx [USER32]
 *
 * RETURNS
 *     Success: count of point set in the buffer
 *     Failure: -1
 */
int WINAPI GetMouseMovePointsEx(UINT size, LPMOUSEMOVEPOINT ptin, LPMOUSEMOVEPOINT ptout, int count, DWORD res) {

    if((size != sizeof(MOUSEMOVEPOINT)) || (count < 0) || (count > 64)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return -1;
    }

    if(!ptin || (!ptout && count)) {
        SetLastError(ERROR_NOACCESS);
        return -1;
    }

    FIXME("(%d %p %p %d %d) stub\n", size, ptin, ptout, count, res);

    SetLastError(ERROR_POINT_NOT_FOUND);
    return -1;
}
