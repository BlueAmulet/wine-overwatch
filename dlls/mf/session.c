/*
 * Copyright (C) 2016 Michael Müller
 *
 * iface library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * iface library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wine/debug.h"
#include "wine/unicode.h"

#define COBJMACROS
#include "mf_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mf);

struct media_session
{
    IMFMediaSession IMFMediaSession_iface;
    LONG ref;
};

static inline struct media_session *impl_from_IMFMediaSession(IMFMediaSession *iface)
{
    return CONTAINING_RECORD(iface, struct media_session, IMFMediaSession_iface);
}

static HRESULT WINAPI session_QueryInterface(IMFMediaSession *iface, REFIID riid, void **ppvObject)
{
    struct media_session *This = impl_from_IMFMediaSession(iface);

    TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), ppvObject);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IMFMediaEventGenerator) ||
        IsEqualGUID(riid, &IID_IMFMediaSession))
    {
        *ppvObject = iface;
    }
    else
    {
        TRACE("Unsupported interface %s\n", debugstr_guid(riid));
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    IMFMediaSession_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI session_AddRef(IMFMediaSession *iface)
{
    struct media_session *This = impl_from_IMFMediaSession(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    return ref;
}

static ULONG WINAPI session_Release(IMFMediaSession *iface)
{
    struct media_session *This = impl_from_IMFMediaSession(iface);
    ULONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p)->(%d)\n", This, ref);

    if (!ref)
    {
        HeapFree(GetProcessHeap(), 0, This);
    }

    return ref;
}

static HRESULT WINAPI session_GetEvent(IMFMediaSession *iface, DWORD flags, IMFMediaEvent **event)
{
    FIXME("(%p, %d, %p): stub", iface, flags, event);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_BeginGetEvent(IMFMediaSession *iface, IMFAsyncCallback *callback, IUnknown *unk_state)
{
    FIXME("(%p, %p, %p): stub", iface, callback, unk_state);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_EndGetEvent(IMFMediaSession *iface, IMFAsyncResult *result, IMFMediaEvent **event)
{
    FIXME("(%p, %p, %p): stub", iface, result, event);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_QueueEvent(IMFMediaSession *iface, MediaEventType met, REFGUID extended_type, HRESULT status, const PROPVARIANT *value)
{
    FIXME("(%p, %d, %s, %x, %p): stub", iface, met, wine_dbgstr_guid(extended_type), status, value);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_SetTopology(IMFMediaSession *iface, DWORD flags, IMFTopology *topology)
{
    FIXME("(%p, %d, %p): stub", iface, flags, topology);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_ClearTopologies(IMFMediaSession *iface)
{
    FIXME("(%p): stub", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_Start(IMFMediaSession *iface, const GUID *format, const PROPVARIANT *start)
{
    FIXME("(%p, %s, %p): stub", iface, wine_dbgstr_guid(format), start);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_Pause(IMFMediaSession *iface)
{
    FIXME("(%p): stub", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_Stop(IMFMediaSession *iface)
{
    FIXME("(%p): stub", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_Close(IMFMediaSession *iface)
{
    FIXME("(%p): stub", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_Shutdown(IMFMediaSession *iface)
{
    FIXME("(%p): stub", iface);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_GetClock(IMFMediaSession *iface, IMFClock **clock)
{
    FIXME("(%p, %p): stub", iface, clock);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_GetSessionCapabilities(IMFMediaSession *iface, DWORD *caps)
{
    FIXME("(%p, %p): stub", iface, caps);

    return E_NOTIMPL;
}

static HRESULT WINAPI session_GetFullTopology(IMFMediaSession *iface, DWORD flags, TOPOID id, IMFTopology **topology)
{
    FIXME("(%p, %d, %s, %p): stub", iface, flags, wine_dbgstr_longlong(id), topology);

    return E_NOTIMPL;
}

static const IMFMediaSessionVtbl sessionVtbl =
{
    session_QueryInterface,
    session_AddRef,
    session_Release,
    session_GetEvent,
    session_BeginGetEvent,
    session_EndGetEvent,
    session_QueueEvent,
    session_SetTopology,
    session_ClearTopologies,
    session_Start,
    session_Pause,
    session_Stop,
    session_Close,
    session_Shutdown,
    session_GetClock,
    session_GetSessionCapabilities,
    session_GetFullTopology
};

HRESULT create_session(IMFMediaSession **iface)
{
    struct media_session *This;

    This = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*This));
    if (!This) return E_OUTOFMEMORY;

    This->IMFMediaSession_iface.lpVtbl = &sessionVtbl;
    This->ref = 1;

    *iface = &This->IMFMediaSession_iface;
    return S_OK;
}
