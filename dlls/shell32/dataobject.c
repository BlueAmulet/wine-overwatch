/*
 *	IEnumFORMATETC, IDataObject
 *
 * selecting and dropping objects within the shell and/or common dialogs
 *
 *	Copyright 1998, 1999	<juergen.schmied@metronet.de>
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
#include <string.h>

#define COBJMACROS
#define NONAMELESSUNION

#include "windef.h"
#include "wingdi.h"
#include "pidl.h"
#include "winerror.h"
#include "shell32_main.h"
#include "wine/debug.h"
#include "undocshell.h"

WINE_DEFAULT_DEBUG_CHANNEL(shell);

/***********************************************************************
*   IEnumFORMATETC implementation
*/

typedef struct
{
    /* IUnknown fields */
    IEnumFORMATETC IEnumFORMATETC_iface;
    LONG           ref;
    /* IEnumFORMATETC fields */
    UINT        posFmt;
    UINT        countFmt;
    LPFORMATETC pFmt;
} IEnumFORMATETCImpl;

static inline IEnumFORMATETCImpl *impl_from_IEnumFORMATETC(IEnumFORMATETC *iface)
{
	return CONTAINING_RECORD(iface, IEnumFORMATETCImpl, IEnumFORMATETC_iface);
}

static HRESULT WINAPI IEnumFORMATETC_fnQueryInterface(
               LPENUMFORMATETC iface, REFIID riid, LPVOID* ppvObj)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	TRACE("(%p)->(\n\tIID:\t%s,%p)\n",This,debugstr_guid(riid),ppvObj);

	*ppvObj = NULL;

	if(IsEqualIID(riid, &IID_IUnknown) ||
           IsEqualIID(riid, &IID_IEnumFORMATETC))
	{
	  *ppvObj = &This->IEnumFORMATETC_iface;
	}

	if(*ppvObj)
	{
	  IUnknown_AddRef((IUnknown*)(*ppvObj));
	  TRACE("-- Interface: (%p)->(%p)\n",ppvObj,*ppvObj);
	  return S_OK;
	}
	TRACE("-- Interface: E_NOINTERFACE\n");
	return E_NOINTERFACE;
}

static ULONG WINAPI IEnumFORMATETC_fnAddRef(LPENUMFORMATETC iface)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	ULONG refCount = InterlockedIncrement(&This->ref);

	TRACE("(%p)->(count=%u)\n", This, refCount - 1);

	return refCount;
}

static ULONG WINAPI IEnumFORMATETC_fnRelease(LPENUMFORMATETC iface)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	ULONG refCount = InterlockedDecrement(&This->ref);

	TRACE("(%p)->(%u)\n", This, refCount + 1);

	if (!refCount)
	{
	  TRACE(" destroying IEnumFORMATETC(%p)\n",This);
	  SHFree (This->pFmt);
	  HeapFree(GetProcessHeap(),0,This);
	  return 0;
	}
	return refCount;
}

static HRESULT WINAPI IEnumFORMATETC_fnNext(LPENUMFORMATETC iface, ULONG celt, FORMATETC *rgelt, ULONG *pceltFethed)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	UINT i;

	TRACE("(%p)->(%u,%p)\n", This, celt, rgelt);

	if(!This->pFmt)return S_FALSE;
	if(!rgelt) return E_INVALIDARG;
	if (pceltFethed)  *pceltFethed = 0;

	for(i = 0; This->posFmt < This->countFmt && celt > i; i++)
   	{
	  *rgelt++ = This->pFmt[This->posFmt++];
	}

	if (pceltFethed) *pceltFethed = i;

	return ((i == celt) ? S_OK : S_FALSE);
}

static HRESULT WINAPI IEnumFORMATETC_fnSkip(LPENUMFORMATETC iface, ULONG celt)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	TRACE("(%p)->(num=%u)\n", This, celt);

	if((This->posFmt + celt) >= This->countFmt) return S_FALSE;
	This->posFmt += celt;
	return S_OK;
}

static HRESULT WINAPI IEnumFORMATETC_fnReset(LPENUMFORMATETC iface)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	TRACE("(%p)->()\n", This);

        This->posFmt = 0;
        return S_OK;
}

static HRESULT WINAPI IEnumFORMATETC_fnClone(LPENUMFORMATETC iface, LPENUMFORMATETC* ppenum)
{
	IEnumFORMATETCImpl *This = impl_from_IEnumFORMATETC(iface);
	TRACE("(%p)->(ppenum=%p)\n", This, ppenum);

	if (!ppenum) return E_INVALIDARG;
	*ppenum = IEnumFORMATETC_Constructor(This->countFmt, This->pFmt);
        if(*ppenum)
           IEnumFORMATETC_fnSkip(*ppenum, This->posFmt);
	return S_OK;
}

static const IEnumFORMATETCVtbl efvt =
{
    IEnumFORMATETC_fnQueryInterface,
    IEnumFORMATETC_fnAddRef,
    IEnumFORMATETC_fnRelease,
    IEnumFORMATETC_fnNext,
    IEnumFORMATETC_fnSkip,
    IEnumFORMATETC_fnReset,
    IEnumFORMATETC_fnClone
};

LPENUMFORMATETC IEnumFORMATETC_Constructor(UINT cfmt, const FORMATETC afmt[])
{
    IEnumFORMATETCImpl* ef;
    DWORD size=cfmt * sizeof(FORMATETC);

    ef = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IEnumFORMATETCImpl));

    if(ef)
    {
        ef->ref=1;
        ef->IEnumFORMATETC_iface.lpVtbl = &efvt;

        ef->countFmt = cfmt;
        ef->pFmt = SHAlloc (size);

        if (ef->pFmt)
            memcpy(ef->pFmt, afmt, size);
    }

    TRACE("(%p)->(%u,%p)\n",ef, cfmt, afmt);
    return (LPENUMFORMATETC)ef;
}


/***********************************************************************
*   IDataObject implementation
*/

/* number of supported formats */
#define MAX_FORMATS 5

typedef struct
{
	/* IUnknown fields */
	IDataObject IDataObject_iface;
	LONG        ref;

	/* IDataObject fields */
	LPITEMIDLIST	pidl;
	LPITEMIDLIST *	apidl;
	UINT		cidl;
    DWORD       dropEffect;

	FORMATETC	pFormatEtc[MAX_FORMATS];
	UINT		cfShellIDList;
	UINT		cfFileNameA;
	UINT		cfFileNameW;
	UINT        cfDropEffect;
} IDataObjectImpl;

static inline IDataObjectImpl *impl_from_IDataObject(IDataObject *iface)
{
	return CONTAINING_RECORD(iface, IDataObjectImpl, IDataObject_iface);
}

/***************************************************************************
*  IDataObject_QueryInterface
*/
static HRESULT WINAPI IDataObject_fnQueryInterface(IDataObject *iface, REFIID riid, LPVOID * ppvObj)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	TRACE("(%p)->(\n\tIID:\t%s,%p)\n",This,debugstr_guid(riid),ppvObj);

	*ppvObj = NULL;

	if(IsEqualIID(riid, &IID_IUnknown) ||
           IsEqualIID(riid, &IID_IDataObject))
	{
	  *ppvObj = &This->IDataObject_iface;
	}

	if(*ppvObj)
	{
	  IUnknown_AddRef((IUnknown*)*ppvObj);
	  TRACE("-- Interface: (%p)->(%p)\n",ppvObj,*ppvObj);
	  return S_OK;
	}
	TRACE("-- Interface: E_NOINTERFACE\n");
	return E_NOINTERFACE;
}

/**************************************************************************
*  IDataObject_AddRef
*/
static ULONG WINAPI IDataObject_fnAddRef(IDataObject *iface)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	ULONG refCount = InterlockedIncrement(&This->ref);

	TRACE("(%p)->(count=%u)\n", This, refCount - 1);

	return refCount;
}

/**************************************************************************
*  IDataObject_Release
*/
static ULONG WINAPI IDataObject_fnRelease(IDataObject *iface)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	ULONG refCount = InterlockedDecrement(&This->ref);

	TRACE("(%p)->(%u)\n", This, refCount + 1);

	if (!refCount)
	{
	  TRACE(" destroying IDataObject(%p)\n",This);
	  _ILFreeaPidl(This->apidl, This->cidl);
	  ILFree(This->pidl),
	  HeapFree(GetProcessHeap(),0,This);
	}
	return refCount;
}

/**************************************************************************
* IDataObject_fnGetData
*/
static HRESULT WINAPI IDataObject_fnGetData(IDataObject *iface, LPFORMATETC pformatetcIn, STGMEDIUM *pmedium)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);

	char	szTemp[256];

	szTemp[0]=0;
	GetClipboardFormatNameA (pformatetcIn->cfFormat, szTemp, 256);
	TRACE("(%p)->(%p %p format=%s)\n", This, pformatetcIn, pmedium, szTemp);

	if (pformatetcIn->cfFormat == This->cfShellIDList)
	{
	  if (This->cidl < 1) return(E_UNEXPECTED);
	  pmedium->u.hGlobal = RenderSHELLIDLIST(This->pidl, This->apidl, This->cidl);
	}
	else if	(pformatetcIn->cfFormat == CF_HDROP)
	{
	  if (This->cidl < 1) return(E_UNEXPECTED);
	  pmedium->u.hGlobal = RenderHDROP(This->pidl, This->apidl, This->cidl);
	}
	else if	(pformatetcIn->cfFormat == This->cfFileNameA)
	{
	  if (This->cidl < 1) return(E_UNEXPECTED);
	  pmedium->u.hGlobal = RenderFILENAMEA(This->pidl, This->apidl, This->cidl);
	}
	else if	(pformatetcIn->cfFormat == This->cfFileNameW)
	{
	  if (This->cidl < 1) return(E_UNEXPECTED);
	  pmedium->u.hGlobal = RenderFILENAMEW(This->pidl, This->apidl, This->cidl);
	}
    else if (pformatetcIn->cfFormat == This->cfDropEffect)
    {
        pmedium->u.hGlobal = RenderPREFERREDDROPEFFECT(This->dropEffect);
    }
	else
	{
	  FIXME("-- expected clipformat not implemented\n");
	  return (E_INVALIDARG);
	}
	if (pmedium->u.hGlobal)
	{
	  pmedium->tymed = TYMED_HGLOBAL;
	  pmedium->pUnkForRelease = NULL;
	  return S_OK;
	}
	return E_OUTOFMEMORY;
}

static HRESULT WINAPI IDataObject_fnGetDataHere(IDataObject *iface, LPFORMATETC pformatetc, STGMEDIUM *pmedium)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	FIXME("(%p)->()\n", This);
	return E_NOTIMPL;
}

static HRESULT WINAPI IDataObject_fnQueryGetData(IDataObject *iface, LPFORMATETC pformatetc)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	UINT i;

	TRACE("(%p)->(fmt=0x%08x tym=0x%08x)\n", This, pformatetc->cfFormat, pformatetc->tymed);

	if(!(DVASPECT_CONTENT & pformatetc->dwAspect))
	  return DV_E_DVASPECT;

	/* check our formats table what we have */
	for (i=0; i<MAX_FORMATS; i++)
	{
	  if ((This->pFormatEtc[i].cfFormat == pformatetc->cfFormat)
	   && (This->pFormatEtc[i].tymed & pformatetc->tymed))
	  {
	    return S_OK;
	  }
	}

	return DV_E_TYMED;
}

static HRESULT WINAPI IDataObject_fnGetCanonicalFormatEtc(IDataObject *iface, LPFORMATETC pformatectIn, LPFORMATETC pformatetcOut)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	FIXME("(%p)->()\n", This);
	return E_NOTIMPL;
}

static HRESULT WINAPI IDataObject_fnSetData(IDataObject *iface, LPFORMATETC pformatetc, STGMEDIUM *pmedium, BOOL fRelease)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);

    FIXME("(%p)->(%p, %p, %u): semi-stub\n", This, pformatetc, pmedium, fRelease);

    if (pformatetc->cfFormat == This->cfDropEffect)
    {
        if (pmedium->tymed == TYMED_HGLOBAL)
            return GetPREFERREDDROPEFFECT(pmedium, &This->dropEffect);
        else
            return DV_E_TYMED;
    }

	return E_NOTIMPL;
}

static HRESULT WINAPI IDataObject_fnEnumFormatEtc(IDataObject *iface, DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);

	TRACE("(%p)->()\n", This);
	*ppenumFormatEtc=NULL;

	/* only get data */
	if (DATADIR_GET == dwDirection)
	{
	  *ppenumFormatEtc = IEnumFORMATETC_Constructor(MAX_FORMATS, This->pFormatEtc);
	  return (*ppenumFormatEtc) ? S_OK : E_FAIL;
	}

	return E_NOTIMPL;
}

static HRESULT WINAPI IDataObject_fnDAdvise(IDataObject *iface, FORMATETC *pformatetc, DWORD advf, IAdviseSink *pAdvSink, DWORD *pdwConnection)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	FIXME("(%p)->()\n", This);
	return E_NOTIMPL;
}
static HRESULT WINAPI IDataObject_fnDUnadvise(IDataObject *iface, DWORD dwConnection)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	FIXME("(%p)->()\n", This);
	return E_NOTIMPL;
}
static HRESULT WINAPI IDataObject_fnEnumDAdvise(IDataObject *iface, IEnumSTATDATA **ppenumAdvise)
{
	IDataObjectImpl *This = impl_from_IDataObject(iface);
	FIXME("(%p)->()\n", This);
	return E_NOTIMPL;
}

static const IDataObjectVtbl dtovt =
{
	IDataObject_fnQueryInterface,
	IDataObject_fnAddRef,
	IDataObject_fnRelease,
	IDataObject_fnGetData,
	IDataObject_fnGetDataHere,
	IDataObject_fnQueryGetData,
	IDataObject_fnGetCanonicalFormatEtc,
	IDataObject_fnSetData,
	IDataObject_fnEnumFormatEtc,
	IDataObject_fnDAdvise,
	IDataObject_fnDUnadvise,
	IDataObject_fnEnumDAdvise
};

/**************************************************************************
*  IDataObject_Constructor
*/
IDataObject* IDataObject_Constructor(HWND hwndOwner,
               LPCITEMIDLIST pMyPidl, LPCITEMIDLIST * apidl, UINT cidl)
{
    IDataObjectImpl* dto;

    dto = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IDataObjectImpl));

    if (dto)
    {
        dto->ref = 1;
        dto->IDataObject_iface.lpVtbl = &dtovt;
        dto->pidl = ILClone(pMyPidl);
        dto->apidl = _ILCopyaPidl(apidl, cidl);
        dto->cidl = cidl;
        dto->dropEffect = 0;

        dto->cfShellIDList = RegisterClipboardFormatW(CFSTR_SHELLIDLISTW);
        dto->cfFileNameA = RegisterClipboardFormatA(CFSTR_FILENAMEA);
        dto->cfFileNameW = RegisterClipboardFormatW(CFSTR_FILENAMEW);
        dto->cfDropEffect = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECTW);
        InitFormatEtc(dto->pFormatEtc[0], dto->cfShellIDList, TYMED_HGLOBAL);
        InitFormatEtc(dto->pFormatEtc[1], CF_HDROP, TYMED_HGLOBAL);
        InitFormatEtc(dto->pFormatEtc[2], dto->cfFileNameA, TYMED_HGLOBAL);
        InitFormatEtc(dto->pFormatEtc[3], dto->cfFileNameW, TYMED_HGLOBAL);
        InitFormatEtc(dto->pFormatEtc[4], dto->cfDropEffect, TYMED_HGLOBAL);
    }

    TRACE("(%p)->(apidl=%p cidl=%u)\n",dto, apidl, cidl);
    return &dto->IDataObject_iface;
}
