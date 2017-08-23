/* Unit test suite for SHLWAPI Compact List and IStream ordinal functions
 *
 * Copyright 2002 Jon Griffiths
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
#include <stdarg.h>

#include "wine/test.h"
#include "windef.h"
#include "winbase.h"
#include "objbase.h"

typedef struct tagSHLWAPI_CLIST
{
  ULONG ulSize;
  ULONG ulId;
} SHLWAPI_CLIST, *LPSHLWAPI_CLIST;

typedef const SHLWAPI_CLIST* LPCSHLWAPI_CLIST;

/* Items to add */
static const SHLWAPI_CLIST SHLWAPI_CLIST_items[] =
{
  {4, 1},
  {8, 3},
  {12, 2},
  {16, 8},
  {20, 9},
  {3, 11},
  {9, 82},
  {33, 16},
  {32, 55},
  {24, 100},
  {39, 116},
  { 0, 0}
};

/* Dummy IStream object for testing calls */
struct dummystream
{
  IStream IStream_iface;
  LONG  ref;
  int   readcalls;
  BOOL  failreadcall;
  BOOL  failreadsize;
  BOOL  readbeyondend;
  BOOL  readreturnlarge;
  int   writecalls;
  BOOL  failwritecall;
  BOOL  failwritesize;
  int   seekcalls;
  int   statcalls;
  BOOL  failstatcall;
  LPCSHLWAPI_CLIST item;
  ULARGE_INTEGER   pos;
};

static inline struct dummystream *impl_from_IStream(IStream *iface)
{
    return CONTAINING_RECORD(iface, struct dummystream, IStream_iface);
}

static HRESULT WINAPI QueryInterface(IStream *iface, REFIID riid, void **ret_iface)
{
    if (IsEqualGUID(&IID_IUnknown, riid) || IsEqualGUID(&IID_IStream, riid)) {
        *ret_iface = iface;
        IStream_AddRef(iface);
        return S_OK;
    }
    trace("Unexpected REFIID %s\n", wine_dbgstr_guid(riid));
    *ret_iface = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI AddRef(IStream *iface)
{
    struct dummystream *This = impl_from_IStream(iface);

    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI Release(IStream *iface)
{
    struct dummystream *This = impl_from_IStream(iface);

    return InterlockedDecrement(&This->ref);
}

static HRESULT WINAPI Read(IStream *iface, void *lpMem, ULONG ulSize, ULONG *lpRead)
{
  struct dummystream *This = impl_from_IStream(iface);
  HRESULT hRet = S_OK;

  ++This->readcalls;
  if (This->failreadcall)
  {
    return STG_E_ACCESSDENIED;
  }
  else if (This->failreadsize)
  {
    *lpRead = ulSize + 8;
    return S_OK;
  }
  else if (This->readreturnlarge)
  {
    *((ULONG*)lpMem) = 0xffff01;
    *lpRead = ulSize;
    This->readreturnlarge = FALSE;
    return S_OK;
  }
  if (ulSize == sizeof(ULONG))
  {
    /* Read size of item */
    *((ULONG*)lpMem) = This->item->ulSize ? This->item->ulSize + sizeof(SHLWAPI_CLIST) : 0;
    *lpRead = ulSize;
  }
  else
  {
    unsigned int i;
    char* buff = lpMem;

    /* Read item data */
    if (!This->item->ulSize)
    {
      This->readbeyondend = TRUE;
      *lpRead = 0;
      return E_FAIL; /* Should never happen */
    }
    *((ULONG*)lpMem) = This->item->ulId;
    *lpRead = ulSize;

    for (i = 0; i < This->item->ulSize; i++)
      buff[4+i] = i*2;

    This->item++;
  }
  return hRet;
}

static HRESULT WINAPI Write(IStream *iface, const void *lpMem, ULONG ulSize, ULONG *lpWritten)
{
  struct dummystream *This = impl_from_IStream(iface);
  HRESULT hRet = S_OK;

  ++This->writecalls;
  if (This->failwritecall)
  {
    return STG_E_ACCESSDENIED;
  }
  else if (This->failwritesize)
  {
    *lpWritten = 0;
  }
  else
    *lpWritten = ulSize;
  return hRet;
}

static HRESULT WINAPI Seek(IStream *iface, LARGE_INTEGER dlibMove, DWORD dwOrigin,
        ULARGE_INTEGER *plibNewPosition)
{
  struct dummystream *This = impl_from_IStream(iface);

  ++This->seekcalls;
  This->pos.QuadPart = dlibMove.QuadPart;
  if (plibNewPosition)
    plibNewPosition->QuadPart = dlibMove.QuadPart;
  return S_OK;
}

static HRESULT WINAPI Stat(IStream *iface, STATSTG *pstatstg, DWORD grfStatFlag)
{
  struct dummystream *This = impl_from_IStream(iface);

  ++This->statcalls;
  if (This->failstatcall)
    return E_FAIL;
  if (pstatstg)
    pstatstg->cbSize.QuadPart = This->pos.QuadPart;
  return S_OK;
}

/* VTable */
static IStreamVtbl iclvt =
{
  QueryInterface,
  AddRef,
  Release,
  Read,
  Write,
  Seek,
  NULL, /* SetSize */
  NULL, /* CopyTo */
  NULL, /* Commit */
  NULL, /* Revert */
  NULL, /* LockRegion */
  NULL, /* UnlockRegion */
  Stat,
  NULL  /* Clone */
};

/* Function ptrs for ordinal calls */
static HMODULE SHLWAPI_hshlwapi = 0;

static VOID    (WINAPI *pSHLWAPI_19)(LPSHLWAPI_CLIST);
static BOOL    (WINAPI *pSHLWAPI_20)(LPSHLWAPI_CLIST*,LPCSHLWAPI_CLIST);
static BOOL    (WINAPI *pSHLWAPI_21)(LPSHLWAPI_CLIST*,ULONG);
static LPSHLWAPI_CLIST (WINAPI *pSHLWAPI_22)(LPSHLWAPI_CLIST,ULONG);
static HRESULT (WINAPI *pSHLWAPI_17)(IStream*, SHLWAPI_CLIST*);
static HRESULT (WINAPI *pSHLWAPI_18)(IStream*, SHLWAPI_CLIST**);

static BOOL    (WINAPI *pSHLWAPI_166)(IStream*);
static HRESULT (WINAPI *pSHLWAPI_184)(IStream*, void*, ULONG);
static HRESULT (WINAPI *pSHLWAPI_212)(IStream*, const void*, ULONG);
static HRESULT (WINAPI *pSHLWAPI_213)(IStream*);
static HRESULT (WINAPI *pSHLWAPI_214)(IStream*, ULARGE_INTEGER*);


static BOOL InitFunctionPtrs(void)
{
  SHLWAPI_hshlwapi = GetModuleHandleA("shlwapi.dll");

  /* SHCreateStreamOnFileEx was introduced in shlwapi v6.0 */
  if(!GetProcAddress(SHLWAPI_hshlwapi, "SHCreateStreamOnFileEx")){
      win_skip("Too old shlwapi version\n");
      return FALSE;
  }

  pSHLWAPI_17 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)17);
  ok(pSHLWAPI_17 != 0, "No Ordinal 17\n");
  pSHLWAPI_18 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)18);
  ok(pSHLWAPI_18 != 0, "No Ordinal 18\n");
  pSHLWAPI_19 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)19);
  ok(pSHLWAPI_19 != 0, "No Ordinal 19\n");
  pSHLWAPI_20 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)20);
  ok(pSHLWAPI_20 != 0, "No Ordinal 20\n");
  pSHLWAPI_21 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)21);
  ok(pSHLWAPI_21 != 0, "No Ordinal 21\n");
  pSHLWAPI_22 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)22);
  ok(pSHLWAPI_22 != 0, "No Ordinal 22\n");
  pSHLWAPI_166 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)166);
  ok(pSHLWAPI_166 != 0, "No Ordinal 166\n");
  pSHLWAPI_184 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)184);
  ok(pSHLWAPI_184 != 0, "No Ordinal 184\n");
  pSHLWAPI_212 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)212);
  ok(pSHLWAPI_212 != 0, "No Ordinal 212\n");
  pSHLWAPI_213 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)213);
  ok(pSHLWAPI_213 != 0, "No Ordinal 213\n");
  pSHLWAPI_214 = (void *)GetProcAddress( SHLWAPI_hshlwapi, (LPSTR)214);
  ok(pSHLWAPI_214 != 0, "No Ordinal 214\n");

  return TRUE;
}

static void InitDummyStream(struct dummystream *obj)
{
    obj->IStream_iface.lpVtbl = &iclvt;
    obj->ref = 1;
    obj->readcalls = 0;
    obj->failreadcall = FALSE;
    obj->failreadsize = FALSE;
    obj->readbeyondend = FALSE;
    obj->readreturnlarge = FALSE;
    obj->writecalls = 0;
    obj->failwritecall = FALSE;
    obj->failwritesize = FALSE;
    obj->seekcalls = 0;
    obj->statcalls = 0;
    obj->failstatcall = FALSE;
    obj->item = SHLWAPI_CLIST_items;
    obj->pos.QuadPart = 0;
}


static void test_CList(void)
{
  struct dummystream streamobj;
  LPSHLWAPI_CLIST list = NULL;
  LPCSHLWAPI_CLIST item = SHLWAPI_CLIST_items;
  BOOL bRet;
  HRESULT hRet;
  LPSHLWAPI_CLIST inserted;
  BYTE buff[64];
  unsigned int i;

  if (!pSHLWAPI_17 || !pSHLWAPI_18 || !pSHLWAPI_19 || !pSHLWAPI_20 ||
      !pSHLWAPI_21 || !pSHLWAPI_22)
    return;

  /* Populate a list and test the items are added correctly */
  while (item->ulSize)
  {
    /* Create item and fill with data */
    inserted = (LPSHLWAPI_CLIST)buff;
    inserted->ulSize = item->ulSize + sizeof(SHLWAPI_CLIST);
    inserted->ulId = item->ulId;
    for (i = 0; i < item->ulSize; i++)
      buff[sizeof(SHLWAPI_CLIST)+i] = i*2;

    /* Add it */
    bRet = pSHLWAPI_20(&list, inserted);
    ok(bRet == TRUE, "failed list add\n");

    if (bRet == TRUE)
    {
      ok(list && list->ulSize, "item not added\n");

      /* Find it */
      inserted = pSHLWAPI_22(list, item->ulId);
      ok(inserted != NULL, "lost after adding\n");

      ok(!inserted || inserted->ulId != ~0U, "find returned a container\n");

      /* Check size */
      if (inserted && inserted->ulSize & 0x3)
      {
        /* Contained */
        ok(inserted[-1].ulId == ~0U, "invalid size is not countained\n");
        ok(inserted[-1].ulSize > inserted->ulSize+sizeof(SHLWAPI_CLIST),
           "container too small\n");
      }
      else if (inserted)
      {
        ok(inserted->ulSize==item->ulSize+sizeof(SHLWAPI_CLIST),
           "id %d wrong size %d\n", inserted->ulId, inserted->ulSize);
      }
      if (inserted)
      {
        BOOL bDataOK = TRUE;
        LPBYTE bufftest = (LPBYTE)inserted;

        for (i = 0; i < inserted->ulSize - sizeof(SHLWAPI_CLIST); i++)
          if (bufftest[sizeof(SHLWAPI_CLIST)+i] != i*2)
            bDataOK = FALSE;

        ok(bDataOK == TRUE, "data corrupted on insert\n");
      }
      ok(!inserted || inserted->ulId==item->ulId, "find got wrong item\n");
    }
    item++;
  }

  /* Write the list */
  InitDummyStream(&streamobj);

  hRet = pSHLWAPI_17(&streamobj.IStream_iface, list);
  ok(hRet == S_OK, "write failed\n");
  if (hRet == S_OK)
  {
    /* 1 call for each element, + 1 for OK (use our null element for this) */
    ok(streamobj.writecalls == sizeof(SHLWAPI_CLIST_items)/sizeof(SHLWAPI_CLIST),
       "wrong call count\n");
    ok(streamobj.readcalls == 0,"called Read() in write\n");
    ok(streamobj.seekcalls == 0,"called Seek() in write\n");
  }

  /* Failure cases for writing */
  InitDummyStream(&streamobj);
  streamobj.failwritecall = TRUE;
  hRet = pSHLWAPI_17(&streamobj.IStream_iface, list);
  ok(hRet == STG_E_ACCESSDENIED, "changed object failure return\n");
  ok(streamobj.writecalls == 1, "called object after failure\n");
  ok(streamobj.readcalls == 0,"called Read() after failure\n");
  ok(streamobj.seekcalls == 0,"called Seek() after failure\n");

  InitDummyStream(&streamobj);
  streamobj.failwritesize = TRUE;
  hRet = pSHLWAPI_17(&streamobj.IStream_iface, list);
  ok(hRet == STG_E_MEDIUMFULL || broken(hRet == E_FAIL) /* Win7 */,
     "changed size failure return\n");
  ok(streamobj.writecalls == 1, "called object after size failure\n");
  ok(streamobj.readcalls == 0,"called Read() after failure\n");
  ok(streamobj.seekcalls == 0,"called Seek() after failure\n");

  /* Invalid inputs for adding */
  inserted = (LPSHLWAPI_CLIST)buff;
  inserted->ulSize = sizeof(SHLWAPI_CLIST) -1;
  inserted->ulId = 33;
  bRet = pSHLWAPI_20(&list, inserted);
  ok(bRet == FALSE, "Expected failure\n");

  inserted = pSHLWAPI_22(list, 33);
  ok(inserted == NULL, "inserted bad element size\n");

  inserted = (LPSHLWAPI_CLIST)buff;
  inserted->ulSize = 44;
  inserted->ulId = ~0U;
  bRet = pSHLWAPI_20(&list, inserted);
  ok(bRet == FALSE, "Expected failure\n");

  item = SHLWAPI_CLIST_items;

  /* Look for nonexistent item in populated list */
  inserted = pSHLWAPI_22(list, 99999999);
  ok(inserted == NULL, "found a nonexistent item\n");

  while (item->ulSize)
  {
    /* Delete items */
    BOOL bRet = pSHLWAPI_21(&list, item->ulId);
    ok(bRet == TRUE, "couldn't find item to delete\n");
    item++;
  }

  /* Look for nonexistent item in empty list */
  inserted = pSHLWAPI_22(list, 99999999);
  ok(inserted == NULL, "found an item in empty list\n");

  /* Create a list by reading in data */
  InitDummyStream(&streamobj);

  hRet = pSHLWAPI_18(&streamobj.IStream_iface, &list);
  ok(hRet == S_OK, "failed create from Read()\n");
  if (hRet == S_OK)
  {
    ok(streamobj.readbeyondend == FALSE, "read beyond end\n");
    /* 2 calls per item, but only 1 for the terminator */
    ok(streamobj.readcalls == sizeof(SHLWAPI_CLIST_items)/sizeof(SHLWAPI_CLIST)*2-1,
       "wrong call count\n");
    ok(streamobj.writecalls == 0, "called Write() from create\n");
    ok(streamobj.seekcalls == 0,"called Seek() from create\n");

    item = SHLWAPI_CLIST_items;

    /* Check the items were added correctly */
    while (item->ulSize)
    {
      inserted = pSHLWAPI_22(list, item->ulId);
      ok(inserted != NULL, "lost after adding\n");

      ok(!inserted || inserted->ulId != ~0U, "find returned a container\n");

      /* Check size */
      if (inserted && inserted->ulSize & 0x3)
      {
        /* Contained */
        ok(inserted[-1].ulId == ~0U, "invalid size is not countained\n");
        ok(inserted[-1].ulSize > inserted->ulSize+sizeof(SHLWAPI_CLIST),
           "container too small\n");
      }
      else if (inserted)
      {
        ok(inserted->ulSize==item->ulSize+sizeof(SHLWAPI_CLIST),
           "id %d wrong size %d\n", inserted->ulId, inserted->ulSize);
      }
      ok(!inserted || inserted->ulId==item->ulId, "find got wrong item\n");
      if (inserted)
      {
        BOOL bDataOK = TRUE;
        LPBYTE bufftest = (LPBYTE)inserted;

        for (i = 0; i < inserted->ulSize - sizeof(SHLWAPI_CLIST); i++)
          if (bufftest[sizeof(SHLWAPI_CLIST)+i] != i*2)
            bDataOK = FALSE;

        ok(bDataOK == TRUE, "data corrupted on insert\n");
      }
      item++;
    }
  }

  /* Failure cases for reading */
  InitDummyStream(&streamobj);
  streamobj.failreadcall = TRUE;
  hRet = pSHLWAPI_18(&streamobj.IStream_iface, &list);
  ok(hRet == STG_E_ACCESSDENIED, "changed object failure return\n");
  ok(streamobj.readbeyondend == FALSE, "read beyond end\n");
  ok(streamobj.readcalls == 1, "called object after read failure\n");
  ok(streamobj.writecalls == 0,"called Write() after read failure\n");
  ok(streamobj.seekcalls == 0,"called Seek() after read failure\n");

  /* Read returns large object */
  InitDummyStream(&streamobj);
  streamobj.readreturnlarge = TRUE;
  hRet = pSHLWAPI_18(&streamobj.IStream_iface, &list);
  ok(hRet == S_OK, "failed create from Read() with large item\n");
  ok(streamobj.readbeyondend == FALSE, "read beyond end\n");
  ok(streamobj.readcalls == 1,"wrong call count\n");
  ok(streamobj.writecalls == 0,"called Write() after read failure\n");
  ok(streamobj.seekcalls == 2,"wrong Seek() call count (%d)\n", streamobj.seekcalls);

  pSHLWAPI_19(list);
}

static BOOL test_SHLWAPI_166(void)
{
  struct dummystream streamobj;
  BOOL bRet;

  if (!pSHLWAPI_166)
    return FALSE;

  InitDummyStream(&streamobj);
  bRet = pSHLWAPI_166(&streamobj.IStream_iface);

  if (bRet != TRUE)
    return FALSE; /* This version doesn't support stream ops on clists */

  ok(streamobj.readcalls == 0, "called Read()\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 0, "called Seek()\n");
  ok(streamobj.statcalls == 1, "wrong call count\n");

  streamobj.statcalls = 0;
  streamobj.pos.QuadPart = 50001;

  bRet = pSHLWAPI_166(&streamobj.IStream_iface);

  ok(bRet == FALSE, "failed after seek adjusted\n");
  ok(streamobj.readcalls == 0, "called Read()\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 0, "called Seek()\n");
  ok(streamobj.statcalls == 1, "wrong call count\n");

  /* Failure cases */
  InitDummyStream(&streamobj);
  streamobj.pos.QuadPart = 50001;
  streamobj.failstatcall = TRUE; /* 1: Stat() Bad, Read() OK */
  bRet = pSHLWAPI_166(&streamobj.IStream_iface);
  ok(bRet == FALSE, "should be FALSE after read is OK\n");
  ok(streamobj.readcalls == 1, "wrong call count\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 1, "wrong call count\n");
  ok(streamobj.statcalls == 1, "wrong call count\n");
  ok(streamobj.pos.QuadPart == 0, "Didn't seek to start\n");

  InitDummyStream(&streamobj);
  streamobj.pos.QuadPart = 50001;
  streamobj.failstatcall = TRUE;
  streamobj.failreadcall = TRUE; /* 2: Stat() Bad, Read() Bad Also */
  bRet = pSHLWAPI_166(&streamobj.IStream_iface);
  ok(bRet == TRUE, "Should be true after read fails\n");
  ok(streamobj.readcalls == 1, "wrong call count\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 0, "Called Seek()\n");
  ok(streamobj.statcalls == 1, "wrong call count\n");
  ok(streamobj.pos.QuadPart == 50001, "called Seek() after read failed\n");
  return TRUE;
}

static void test_SHLWAPI_184(void)
{
  struct dummystream streamobj;
  char buff[256];
  HRESULT hRet;

  if (!pSHLWAPI_184)
    return;

  InitDummyStream(&streamobj);
  hRet = pSHLWAPI_184(&streamobj.IStream_iface, buff, sizeof(buff));

  ok(hRet == S_OK, "failed Read()\n");
  ok(streamobj.readcalls == 1, "wrong call count\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 0, "called Seek()\n");
}

static void test_SHLWAPI_212(void)
{
  struct dummystream streamobj;
  char buff[256];
  HRESULT hRet;

  if (!pSHLWAPI_212)
    return;

  InitDummyStream(&streamobj);
  hRet = pSHLWAPI_212(&streamobj.IStream_iface, buff, sizeof(buff));

  ok(hRet == S_OK, "failed Write()\n");
  ok(streamobj.readcalls == 0, "called Read()\n");
  ok(streamobj.writecalls == 1, "wrong call count\n");
  ok(streamobj.seekcalls == 0, "called Seek()\n");
}

static void test_SHLWAPI_213(void)
{
  struct dummystream streamobj;
  ULARGE_INTEGER ul;
  LARGE_INTEGER ll;
  HRESULT hRet;

  if (!pSHLWAPI_213 || !pSHLWAPI_214)
    return;

  InitDummyStream(&streamobj);
  ll.QuadPart = 5000l;
  Seek(&streamobj.IStream_iface, ll, 0, NULL); /* Seek to 5000l */

  streamobj.seekcalls = 0;
  pSHLWAPI_213(&streamobj.IStream_iface); /* Should rewind */
  ok(streamobj.statcalls == 0, "called Stat()\n");
  ok(streamobj.readcalls == 0, "called Read()\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 1, "wrong call count\n");

  ul.QuadPart = 50001;
  hRet = pSHLWAPI_214(&streamobj.IStream_iface, &ul);
  ok(hRet == S_OK, "failed Stat()\n");
  ok(ul.QuadPart == 0, "213 didn't rewind stream\n");
}

static void test_SHLWAPI_214(void)
{
  struct dummystream streamobj;
  ULARGE_INTEGER ul;
  LARGE_INTEGER ll;
  HRESULT hRet;

  if (!pSHLWAPI_214)
    return;

  InitDummyStream(&streamobj);
  ll.QuadPart = 5000l;
  Seek(&streamobj.IStream_iface, ll, 0, NULL);
  ul.QuadPart = 0;
  streamobj.seekcalls = 0;
  hRet = pSHLWAPI_214(&streamobj.IStream_iface, &ul);

  ok(hRet == S_OK, "failed Stat()\n");
  ok(streamobj.statcalls == 1, "wrong call count\n");
  ok(streamobj.readcalls == 0, "called Read()\n");
  ok(streamobj.writecalls == 0, "called Write()\n");
  ok(streamobj.seekcalls == 0, "called Seek()\n");
  ok(ul.QuadPart == 5000l, "Stat gave wrong size\n");
}

START_TEST(clist)
{
  if(!InitFunctionPtrs())
    return;

  test_CList();

  /* Test streaming if this version supports it */
  if (test_SHLWAPI_166())
  {
    test_SHLWAPI_184();
    test_SHLWAPI_212();
    test_SHLWAPI_213();
    test_SHLWAPI_214();
  }
}
