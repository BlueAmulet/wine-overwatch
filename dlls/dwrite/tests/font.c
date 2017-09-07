/*
 *    Font related tests
 *
 * Copyright 2012, 2014-2017 Nikolay Sivov for CodeWeavers
 * Copyright 2014 Aric Stewart for CodeWeavers
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

#include <math.h>
#include <limits.h>

#define COBJMACROS

#include "windows.h"
#include "winternl.h"
#include "dwrite_3.h"
#include "initguid.h"
#include "d2d1.h"

#include "wine/test.h"

#define MS_CMAP_TAG DWRITE_MAKE_OPENTYPE_TAG('c','m','a','p')
#define MS_VDMX_TAG DWRITE_MAKE_OPENTYPE_TAG('V','D','M','X')
#define MS_GASP_TAG DWRITE_MAKE_OPENTYPE_TAG('g','a','s','p')
#define MS_CPAL_TAG DWRITE_MAKE_OPENTYPE_TAG('C','P','A','L')
#define MS_0S2_TAG  DWRITE_MAKE_OPENTYPE_TAG('O','S','/','2')
#define MS_HEAD_TAG DWRITE_MAKE_OPENTYPE_TAG('h','e','a','d')
#define MS_HHEA_TAG DWRITE_MAKE_OPENTYPE_TAG('h','h','e','a')
#define MS_POST_TAG DWRITE_MAKE_OPENTYPE_TAG('p','o','s','t')
#define MS_GSUB_TAG DWRITE_MAKE_OPENTYPE_TAG('G','S','U','B')
#define MS_KERN_TAG DWRITE_MAKE_OPENTYPE_TAG('k','e','r','n')
#define MS_GLYF_TAG DWRITE_MAKE_OPENTYPE_TAG('g','l','y','f')
#define MS_CFF__TAG DWRITE_MAKE_OPENTYPE_TAG('C','F','F',' ')
#define MS_COLR_TAG DWRITE_MAKE_OPENTYPE_TAG('C','O','L','R')
#define MS_SVG__TAG DWRITE_MAKE_OPENTYPE_TAG('S','V','G',' ')
#define MS_SBIX_TAG DWRITE_MAKE_OPENTYPE_TAG('s','b','i','x')
#define MS_MAXP_TAG DWRITE_MAKE_OPENTYPE_TAG('m','a','x','p')

/* 'sbix' formats */
#define MS_PNG__TAG DWRITE_MAKE_OPENTYPE_TAG('p','n','g',' ')
#define MS_JPG__TAG DWRITE_MAKE_OPENTYPE_TAG('j','p','g',' ')
#define MS_TIFF_TAG DWRITE_MAKE_OPENTYPE_TAG('t','i','f','f')

#ifdef WORDS_BIGENDIAN
#define GET_BE_WORD(x) (x)
#define GET_BE_DWORD(x) (x)
#else
#define GET_BE_WORD(x) RtlUshortByteSwap(x)
#define GET_BE_DWORD(x) RtlUlongByteSwap(x)
#endif

#define EXPECT_HR(hr,hr_exp) \
    ok(hr == hr_exp, "got 0x%08x, expected 0x%08x\n", hr, hr_exp)

#define DEFINE_EXPECT(func) \
    static BOOL expect_ ## func = FALSE, called_ ## func = FALSE

#define SET_EXPECT(func) \
    do { called_ ## func = FALSE; expect_ ## func = TRUE; } while(0)

#define CHECK_EXPECT2(func) \
    do { \
        ok(expect_ ##func, "unexpected call " #func "\n"); \
        called_ ## func = TRUE; \
    }while(0)

#define CHECK_EXPECT(func) \
    do { \
        CHECK_EXPECT2(func); \
        expect_ ## func = FALSE; \
    }while(0)

#define CHECK_CALLED(func) \
    do { \
        ok(called_ ## func, "expected " #func "\n"); \
        expect_ ## func = called_ ## func = FALSE; \
    }while(0)

#define CLEAR_CALLED(func) \
    expect_ ## func = called_ ## func = FALSE

DEFINE_EXPECT(setfillmode);

#define EXPECT_REF(obj,ref) _expect_ref((IUnknown*)obj, ref, __LINE__)
static void _expect_ref(IUnknown* obj, ULONG ref, int line)
{
    ULONG rc;
    IUnknown_AddRef(obj);
    rc = IUnknown_Release(obj);
    ok_(__FILE__,line)(rc == ref, "expected refcount %d, got %d\n", ref, rc);
}

#define EXPECT_REF_BROKEN(obj,ref,brokenref) _expect_ref_broken((IUnknown*)obj, ref, brokenref, __LINE__)
static void _expect_ref_broken(IUnknown* obj, ULONG ref, ULONG brokenref, int line)
{
    ULONG rc;
    IUnknown_AddRef(obj);
    rc = IUnknown_Release(obj);
    ok_(__FILE__,line)(rc == ref || broken(rc == brokenref), "expected refcount %d, got %d\n", ref, rc);
}

static inline void* __WINE_ALLOC_SIZE(1) heap_alloc(size_t size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline BOOL heap_free(void *mem)
{
    return HeapFree(GetProcessHeap(), 0, mem);
}

static const WCHAR test_fontfile[] = {'w','i','n','e','_','t','e','s','t','_','f','o','n','t','.','t','t','f',0};
static const WCHAR tahomaW[] = {'T','a','h','o','m','a',0};
static const WCHAR arialW[] = {'A','r','i','a','l',0};
static const WCHAR tahomaUppercaseW[] = {'T','A','H','O','M','A',0};
static const WCHAR tahomaStrangecaseW[] = {'t','A','h','O','m','A',0};
static const WCHAR blahW[]  = {'B','l','a','h','!',0};
static const WCHAR emojiW[] = {'S','e','g','o','e',' ','U','I',' ','E','m','o','j','i',0};

/* PANOSE is 10 bytes in size, need to pack the structure properly */
#include "pshpack2.h"
typedef struct
{
    USHORT majorVersion;
    USHORT minorVersion;
    ULONG revision;
    ULONG checksumadj;
    ULONG magic;
    USHORT flags;
    USHORT unitsPerEm;
    ULONGLONG created;
    ULONGLONG modified;
    SHORT xMin;
    SHORT yMin;
    SHORT xMax;
    SHORT yMax;
    USHORT macStyle;
    USHORT lowestRecPPEM;
    SHORT direction_hint;
    SHORT index_format;
    SHORT glyphdata_format;
} TT_HEAD;

enum TT_HEAD_MACSTYLE
{
    TT_HEAD_MACSTYLE_BOLD      = 1 << 0,
    TT_HEAD_MACSTYLE_ITALIC    = 1 << 1,
    TT_HEAD_MACSTYLE_UNDERLINE = 1 << 2,
    TT_HEAD_MACSTYLE_OUTLINE   = 1 << 3,
    TT_HEAD_MACSTYLE_SHADOW    = 1 << 4,
    TT_HEAD_MACSTYLE_CONDENSED = 1 << 5,
    TT_HEAD_MACSTYLE_EXTENDED  = 1 << 6,
};

typedef struct
{
    USHORT version;
    SHORT xAvgCharWidth;
    USHORT usWeightClass;
    USHORT usWidthClass;
    SHORT fsType;
    SHORT ySubscriptXSize;
    SHORT ySubscriptYSize;
    SHORT ySubscriptXOffset;
    SHORT ySubscriptYOffset;
    SHORT ySuperscriptXSize;
    SHORT ySuperscriptYSize;
    SHORT ySuperscriptXOffset;
    SHORT ySuperscriptYOffset;
    SHORT yStrikeoutSize;
    SHORT yStrikeoutPosition;
    SHORT sFamilyClass;
    PANOSE panose;
    ULONG ulUnicodeRange1;
    ULONG ulUnicodeRange2;
    ULONG ulUnicodeRange3;
    ULONG ulUnicodeRange4;
    CHAR achVendID[4];
    USHORT fsSelection;
    USHORT usFirstCharIndex;
    USHORT usLastCharIndex;
    /* According to the Apple spec, original version didn't have the below fields,
     * version numbers were taken from the OpenType spec.
     */
    /* version 0 (TrueType 1.5) */
    USHORT sTypoAscender;
    USHORT sTypoDescender;
    USHORT sTypoLineGap;
    USHORT usWinAscent;
    USHORT usWinDescent;
    /* version 1 (TrueType 1.66) */
    ULONG ulCodePageRange1;
    ULONG ulCodePageRange2;
    /* version 2 (OpenType 1.2) */
    SHORT sxHeight;
    SHORT sCapHeight;
    USHORT usDefaultChar;
    USHORT usBreakChar;
    USHORT usMaxContext;
} TT_OS2_V2;

enum OS2_FSSELECTION {
    OS2_FSSELECTION_ITALIC           = 1 << 0,
    OS2_FSSELECTION_UNDERSCORE       = 1 << 1,
    OS2_FSSELECTION_NEGATIVE         = 1 << 2,
    OS2_FSSELECTION_OUTLINED         = 1 << 3,
    OS2_FSSELECTION_STRIKEOUT        = 1 << 4,
    OS2_FSSELECTION_BOLD             = 1 << 5,
    OS2_FSSELECTION_REGULAR          = 1 << 6,
    OS2_FSSELECTION_USE_TYPO_METRICS = 1 << 7,
    OS2_FSSELECTION_WWS              = 1 << 8,
    OS2_FSSELECTION_OBLIQUE          = 1 << 9
};

typedef struct {
    ULONG Version;
    ULONG italicAngle;
    SHORT underlinePosition;
    SHORT underlineThickness;
    ULONG fixed_pitch;
    ULONG minmemType42;
    ULONG maxmemType42;
    ULONG minmemType1;
    ULONG maxmemType1;
} TT_POST;

typedef struct {
    USHORT majorVersion;
    USHORT minorVersion;
    SHORT  ascender;
    SHORT  descender;
    SHORT  linegap;
    USHORT advanceWidthMax;
    SHORT  minLeftSideBearing;
    SHORT  minRightSideBearing;
    SHORT  xMaxExtent;
    SHORT  caretSlopeRise;
    SHORT  caretSlopeRun;
    SHORT  caretOffset;
    SHORT  reserved[4];
    SHORT  metricDataFormat;
    USHORT numberOfHMetrics;
} TT_HHEA;

typedef struct {
    DWORD version;
    WORD ScriptList;
    WORD FeatureList;
    WORD LookupList;
} GSUB_Header;

typedef struct {
    CHAR FeatureTag[4];
    WORD Feature;
} OT_FeatureRecord;

typedef struct {
    WORD FeatureCount;
    OT_FeatureRecord FeatureRecord[1];
} OT_FeatureList;

typedef struct {
    WORD FeatureParams;
    WORD LookupCount;
    WORD LookupListIndex[1];
} OT_Feature;

typedef struct {
    WORD LookupCount;
    WORD Lookup[1];
} OT_LookupList;

typedef struct {
    WORD LookupType;
    WORD LookupFlag;
    WORD SubTableCount;
    WORD SubTable[1];
} OT_LookupTable;

typedef struct {
    WORD SubstFormat;
    WORD Coverage;
    WORD DeltaGlyphID;
} GSUB_SingleSubstFormat1;

typedef struct {
    WORD SubstFormat;
    WORD Coverage;
    WORD GlyphCount;
    WORD Substitute[1];
} GSUB_SingleSubstFormat2;

typedef struct {
    WORD SubstFormat;
    WORD ExtensionLookupType;
    DWORD ExtensionOffset;
} GSUB_ExtensionPosFormat1;

typedef struct {
    WORD version;
    WORD flags;
    DWORD numStrikes;
    DWORD strikeOffset[1];
} sbix_header;

typedef struct {
    WORD ppem;
    WORD ppi;
    DWORD glyphDataOffsets[1];
} sbix_strike;

typedef struct {
    WORD originOffsetX;
    WORD originOffsetY;
    DWORD graphicType;
    BYTE data[1];
} sbix_glyph_data;

typedef struct {
    DWORD version;
    WORD numGlyphs;
} maxp;

#include "poppack.h"

static IDWriteFactory *create_factory(void)
{
    IDWriteFactory *factory;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown**)&factory);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    return factory;
}

static IDWriteFontFace *create_fontface(IDWriteFactory *factory)
{
    static const WCHAR tahomaW[] = {'T','a','h','o','m','a',0};
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFont *font;
    LOGFONTW logfont;
    HRESULT hr;

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);

    return fontface;
}

static IDWriteFont *get_font(IDWriteFactory *factory, const WCHAR *name, DWRITE_FONT_STYLE style)
{
    IDWriteFontCollection *collection;
    IDWriteFontFamily *family;
    IDWriteFont *font = NULL;
    UINT32 index;
    BOOL exists;
    HRESULT hr;

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    index = ~0;
    exists = FALSE;
    hr = IDWriteFontCollection_FindFamilyName(collection, name, &index, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    if (!exists) goto not_found;

    hr = IDWriteFontCollection_GetFontFamily(collection, index, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, style, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFamily_Release(family);
not_found:
    IDWriteFontCollection_Release(collection);
    return font;
}

static IDWriteFont *get_tahoma_instance(IDWriteFactory *factory, DWRITE_FONT_STYLE style)
{
    IDWriteFont *font = get_font(factory, tahomaW, style);
    ok(font != NULL, "failed to get Tahoma\n");
    return font;
}

static WCHAR *create_testfontfile(const WCHAR *filename)
{
    static WCHAR pathW[MAX_PATH];
    DWORD written;
    HANDLE file;
    HRSRC res;
    void *ptr;

    GetTempPathW(sizeof(pathW)/sizeof(WCHAR), pathW);
    lstrcatW(pathW, filename);

    file = CreateFileW(pathW, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, 0);
    ok(file != INVALID_HANDLE_VALUE, "file creation failed, at %s, error %d\n", wine_dbgstr_w(pathW),
        GetLastError());

    res = FindResourceA(GetModuleHandleA(NULL), (LPCSTR)MAKEINTRESOURCE(1), (LPCSTR)RT_RCDATA);
    ok( res != 0, "couldn't find resource\n" );
    ptr = LockResource( LoadResource( GetModuleHandleA(NULL), res ));
    WriteFile( file, ptr, SizeofResource( GetModuleHandleA(NULL), res ), &written, NULL );
    ok( written == SizeofResource( GetModuleHandleA(NULL), res ), "couldn't write resource\n" );
    CloseHandle( file );

    return pathW;
}

#define DELETE_FONTFILE(filename) _delete_testfontfile(filename, __LINE__)
static void _delete_testfontfile(const WCHAR *filename, int line)
{
    BOOL ret = DeleteFileW(filename);
    ok_(__FILE__,line)(ret, "failed to delete file %s, error %d\n", wine_dbgstr_w(filename), GetLastError());
}

struct test_fontenumerator
{
    IDWriteFontFileEnumerator IDWriteFontFileEnumerator_iface;
    LONG ref;

    DWORD index;
    IDWriteFontFile *font_file;
};

static inline struct test_fontenumerator *impl_from_IDWriteFontFileEnumerator(IDWriteFontFileEnumerator* iface)
{
    return CONTAINING_RECORD(iface, struct test_fontenumerator, IDWriteFontFileEnumerator_iface);
}

static HRESULT WINAPI singlefontfileenumerator_QueryInterface(IDWriteFontFileEnumerator *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileEnumerator))
    {
        *obj = iface;
        IDWriteFontFileEnumerator_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI singlefontfileenumerator_AddRef(IDWriteFontFileEnumerator *iface)
{
    struct test_fontenumerator *This = impl_from_IDWriteFontFileEnumerator(iface);
    return InterlockedIncrement(&This->ref);
}

static ULONG WINAPI singlefontfileenumerator_Release(IDWriteFontFileEnumerator *iface)
{
    struct test_fontenumerator *This = impl_from_IDWriteFontFileEnumerator(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (!ref) {
        IDWriteFontFile_Release(This->font_file);
        heap_free(This);
    }
    return ref;
}

static HRESULT WINAPI singlefontfileenumerator_GetCurrentFontFile(IDWriteFontFileEnumerator *iface, IDWriteFontFile **font_file)
{
    struct test_fontenumerator *This = impl_from_IDWriteFontFileEnumerator(iface);
    IDWriteFontFile_AddRef(This->font_file);
    *font_file = This->font_file;
    return S_OK;
}

static HRESULT WINAPI singlefontfileenumerator_MoveNext(IDWriteFontFileEnumerator *iface, BOOL *current)
{
    struct test_fontenumerator *This = impl_from_IDWriteFontFileEnumerator(iface);

    if (This->index > 1) {
        *current = FALSE;
        return S_OK;
    }

    This->index++;
    *current = TRUE;
    return S_OK;
}

static const struct IDWriteFontFileEnumeratorVtbl singlefontfileenumeratorvtbl =
{
    singlefontfileenumerator_QueryInterface,
    singlefontfileenumerator_AddRef,
    singlefontfileenumerator_Release,
    singlefontfileenumerator_MoveNext,
    singlefontfileenumerator_GetCurrentFontFile
};

static HRESULT create_enumerator(IDWriteFontFile *font_file, IDWriteFontFileEnumerator **ret)
{
    struct test_fontenumerator *enumerator;

    enumerator = heap_alloc(sizeof(struct test_fontenumerator));
    if (!enumerator)
        return E_OUTOFMEMORY;

    enumerator->IDWriteFontFileEnumerator_iface.lpVtbl = &singlefontfileenumeratorvtbl;
    enumerator->ref = 1;
    enumerator->index = 0;
    enumerator->font_file = font_file;
    IDWriteFontFile_AddRef(font_file);

    *ret = &enumerator->IDWriteFontFileEnumerator_iface;
    return S_OK;
}

struct test_fontcollectionloader
{
    IDWriteFontCollectionLoader IDWriteFontFileCollectionLoader_iface;
    IDWriteFontFileLoader *loader;
};

static inline struct test_fontcollectionloader *impl_from_IDWriteFontFileCollectionLoader(IDWriteFontCollectionLoader* iface)
{
    return CONTAINING_RECORD(iface, struct test_fontcollectionloader, IDWriteFontFileCollectionLoader_iface);
}

static HRESULT WINAPI resourcecollectionloader_QueryInterface(IDWriteFontCollectionLoader *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontCollectionLoader))
    {
        *obj = iface;
        IDWriteFontCollectionLoader_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI resourcecollectionloader_AddRef(IDWriteFontCollectionLoader *iface)
{
    return 2;
}

static ULONG WINAPI resourcecollectionloader_Release(IDWriteFontCollectionLoader *iface)
{
    return 1;
}

static HRESULT WINAPI resourcecollectionloader_CreateEnumeratorFromKey(IDWriteFontCollectionLoader *iface, IDWriteFactory *factory,
    const void * collectionKey, UINT32  collectionKeySize, IDWriteFontFileEnumerator ** fontFileEnumerator)
{
    struct test_fontcollectionloader *This = impl_from_IDWriteFontFileCollectionLoader(iface);
    IDWriteFontFile *font_file;
    HRESULT hr;

    IDWriteFactory_CreateCustomFontFileReference(factory, collectionKey, collectionKeySize, This->loader, &font_file);

    hr = create_enumerator(font_file, fontFileEnumerator);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFile_Release(font_file);
    return hr;
}

static const struct IDWriteFontCollectionLoaderVtbl resourcecollectionloadervtbl = {
    resourcecollectionloader_QueryInterface,
    resourcecollectionloader_AddRef,
    resourcecollectionloader_Release,
    resourcecollectionloader_CreateEnumeratorFromKey
};

/* Here is a functional custom font set of interfaces */
struct test_fontdatastream
{
    IDWriteFontFileStream IDWriteFontFileStream_iface;
    LONG ref;

    LPVOID data;
    DWORD size;
};

static inline struct test_fontdatastream *impl_from_IDWriteFontFileStream(IDWriteFontFileStream* iface)
{
    return CONTAINING_RECORD(iface, struct test_fontdatastream, IDWriteFontFileStream_iface);
}

static HRESULT WINAPI fontdatastream_QueryInterface(IDWriteFontFileStream *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileStream))
    {
        *obj = iface;
        IDWriteFontFileStream_AddRef(iface);
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontdatastream_AddRef(IDWriteFontFileStream *iface)
{
    struct test_fontdatastream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedIncrement(&This->ref);
    return ref;
}

static ULONG WINAPI fontdatastream_Release(IDWriteFontFileStream *iface)
{
    struct test_fontdatastream *This = impl_from_IDWriteFontFileStream(iface);
    ULONG ref = InterlockedDecrement(&This->ref);
    if (ref == 0)
        HeapFree(GetProcessHeap(), 0, This);
    return ref;
}

static HRESULT WINAPI fontdatastream_ReadFileFragment(IDWriteFontFileStream *iface, void const **fragment_start, UINT64 offset, UINT64 fragment_size, void **fragment_context)
{
    struct test_fontdatastream *This = impl_from_IDWriteFontFileStream(iface);
    *fragment_context = NULL;
    if (offset+fragment_size > This->size)
    {
        *fragment_start = NULL;
        return E_FAIL;
    }
    else
    {
        *fragment_start = (BYTE*)This->data + offset;
        return S_OK;
    }
}

static void WINAPI fontdatastream_ReleaseFileFragment(IDWriteFontFileStream *iface, void *fragment_context)
{
    /* Do Nothing */
}

static HRESULT WINAPI fontdatastream_GetFileSize(IDWriteFontFileStream *iface, UINT64 *size)
{
    struct test_fontdatastream *This = impl_from_IDWriteFontFileStream(iface);
    *size = This->size;
    return S_OK;
}

static HRESULT WINAPI fontdatastream_GetLastWriteTime(IDWriteFontFileStream *iface, UINT64 *last_writetime)
{
    return E_NOTIMPL;
}

static const IDWriteFontFileStreamVtbl fontdatastreamvtbl =
{
    fontdatastream_QueryInterface,
    fontdatastream_AddRef,
    fontdatastream_Release,
    fontdatastream_ReadFileFragment,
    fontdatastream_ReleaseFileFragment,
    fontdatastream_GetFileSize,
    fontdatastream_GetLastWriteTime
};

static HRESULT create_fontdatastream(LPVOID data, UINT size, IDWriteFontFileStream** iface)
{
    struct test_fontdatastream *This = HeapAlloc(GetProcessHeap(), 0, sizeof(struct test_fontdatastream));
    if (!This)
        return E_OUTOFMEMORY;

    This->data = data;
    This->size = size;
    This->ref = 1;
    This->IDWriteFontFileStream_iface.lpVtbl = &fontdatastreamvtbl;

    *iface = &This->IDWriteFontFileStream_iface;
    return S_OK;
}

static HRESULT WINAPI resourcefontfileloader_QueryInterface(IDWriteFontFileLoader *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileLoader))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI resourcefontfileloader_AddRef(IDWriteFontFileLoader *iface)
{
    return 2;
}

static ULONG WINAPI resourcefontfileloader_Release(IDWriteFontFileLoader *iface)
{
    return 1;
}

static HRESULT WINAPI resourcefontfileloader_CreateStreamFromKey(IDWriteFontFileLoader *iface, const void *ref_key, UINT32 key_size,
    IDWriteFontFileStream **stream)
{
    LPVOID data;
    DWORD size;
    HGLOBAL mem;

    mem = LoadResource(GetModuleHandleA(NULL), *(HRSRC*)ref_key);
    ok(mem != NULL, "Failed to lock font resource\n");
    if (mem)
    {
        size = SizeofResource(GetModuleHandleA(NULL), *(HRSRC*)ref_key);
        data = LockResource(mem);
        return create_fontdatastream(data, size, stream);
    }
    return E_FAIL;
}

static const struct IDWriteFontFileLoaderVtbl resourcefontfileloadervtbl = {
    resourcefontfileloader_QueryInterface,
    resourcefontfileloader_AddRef,
    resourcefontfileloader_Release,
    resourcefontfileloader_CreateStreamFromKey
};

static IDWriteFontFileLoader rloader = { &resourcefontfileloadervtbl };

static D2D1_POINT_2F g_startpoints[2];
static int g_startpoint_count;

static HRESULT WINAPI test_geometrysink_QueryInterface(ID2D1SimplifiedGeometrySink *iface, REFIID riid, void **ret)
{
    if (IsEqualIID(riid, &IID_ID2D1SimplifiedGeometrySink) ||
        IsEqualIID(riid, &IID_IUnknown))
    {
        *ret = iface;
        ID2D1SimplifiedGeometrySink_AddRef(iface);
        return S_OK;
    }

    *ret = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI test_geometrysink_AddRef(ID2D1SimplifiedGeometrySink *iface)
{
    return 2;
}

static ULONG WINAPI test_geometrysink_Release(ID2D1SimplifiedGeometrySink *iface)
{
    return 1;
}

static void WINAPI test_geometrysink_SetFillMode(ID2D1SimplifiedGeometrySink *iface, D2D1_FILL_MODE mode)
{
    CHECK_EXPECT(setfillmode);
    ok(mode == D2D1_FILL_MODE_WINDING, "fill mode %d\n", mode);
}

static void WINAPI test_geometrysink_SetSegmentFlags(ID2D1SimplifiedGeometrySink *iface, D2D1_PATH_SEGMENT flags)
{
    ok(0, "unexpected SetSegmentFlags() - flags %d\n", flags);
}

static void WINAPI test_geometrysink_BeginFigure(ID2D1SimplifiedGeometrySink *iface,
    D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin)
{
    ok(figureBegin == D2D1_FIGURE_BEGIN_FILLED, "begin figure %d\n", figureBegin);
    if (g_startpoint_count < sizeof(g_startpoints)/sizeof(g_startpoints[0]))
        g_startpoints[g_startpoint_count] = startPoint;
    g_startpoint_count++;
}

static void WINAPI test_geometrysink_AddLines(ID2D1SimplifiedGeometrySink *iface,
    const D2D1_POINT_2F *points, UINT32 count)
{
}

static void WINAPI test_geometrysink_AddBeziers(ID2D1SimplifiedGeometrySink *iface,
    const D2D1_BEZIER_SEGMENT *beziers, UINT32 count)
{
}

static void WINAPI test_geometrysink_EndFigure(ID2D1SimplifiedGeometrySink *iface, D2D1_FIGURE_END figureEnd)
{
    ok(figureEnd == D2D1_FIGURE_END_CLOSED, "end figure %d\n", figureEnd);
}

static HRESULT WINAPI test_geometrysink_Close(ID2D1SimplifiedGeometrySink *iface)
{
    ok(0, "unexpected Close()\n");
    return E_NOTIMPL;
}

static const ID2D1SimplifiedGeometrySinkVtbl test_geometrysink_vtbl = {
    test_geometrysink_QueryInterface,
    test_geometrysink_AddRef,
    test_geometrysink_Release,
    test_geometrysink_SetFillMode,
    test_geometrysink_SetSegmentFlags,
    test_geometrysink_BeginFigure,
    test_geometrysink_AddLines,
    test_geometrysink_AddBeziers,
    test_geometrysink_EndFigure,
    test_geometrysink_Close
};

static void WINAPI test_geometrysink2_BeginFigure(ID2D1SimplifiedGeometrySink *iface,
    D2D1_POINT_2F startPoint, D2D1_FIGURE_BEGIN figureBegin)
{
    ok(0, "unexpected call\n");
}

static void WINAPI test_geometrysink2_AddLines(ID2D1SimplifiedGeometrySink *iface,
    const D2D1_POINT_2F *points, UINT32 count)
{
    ok(0, "unexpected call\n");
}

static void WINAPI test_geometrysink2_AddBeziers(ID2D1SimplifiedGeometrySink *iface,
    const D2D1_BEZIER_SEGMENT *beziers, UINT32 count)
{
    ok(0, "unexpected call\n");
}

static void WINAPI test_geometrysink2_EndFigure(ID2D1SimplifiedGeometrySink *iface, D2D1_FIGURE_END figureEnd)
{
    ok(0, "unexpected call\n");
}

static const ID2D1SimplifiedGeometrySinkVtbl test_geometrysink2_vtbl = {
    test_geometrysink_QueryInterface,
    test_geometrysink_AddRef,
    test_geometrysink_Release,
    test_geometrysink_SetFillMode,
    test_geometrysink_SetSegmentFlags,
    test_geometrysink2_BeginFigure,
    test_geometrysink2_AddLines,
    test_geometrysink2_AddBeziers,
    test_geometrysink2_EndFigure,
    test_geometrysink_Close
};

static ID2D1SimplifiedGeometrySink test_geomsink = { &test_geometrysink_vtbl };
static ID2D1SimplifiedGeometrySink test_geomsink2 = { &test_geometrysink2_vtbl };

static void test_CreateFontFromLOGFONT(void)
{
    static const WCHAR tahomaspW[] = {'T','a','h','o','m','a',' ',0};
    IDWriteGdiInterop1 *interop1;
    IDWriteGdiInterop *interop;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    IDWriteFont *font;
    LOGFONTW logfont;
    LONG weights[][2] = {
        {FW_NORMAL, DWRITE_FONT_WEIGHT_NORMAL},
        {FW_BOLD, DWRITE_FONT_WEIGHT_BOLD},
        {  0, DWRITE_FONT_WEIGHT_NORMAL},
        { 50, DWRITE_FONT_WEIGHT_NORMAL},
        {150, DWRITE_FONT_WEIGHT_NORMAL},
        {250, DWRITE_FONT_WEIGHT_NORMAL},
        {350, DWRITE_FONT_WEIGHT_NORMAL},
        {450, DWRITE_FONT_WEIGHT_NORMAL},
        {650, DWRITE_FONT_WEIGHT_BOLD},
        {750, DWRITE_FONT_WEIGHT_BOLD},
        {850, DWRITE_FONT_WEIGHT_BOLD},
        {950, DWRITE_FONT_WEIGHT_BOLD},
        {960, DWRITE_FONT_WEIGHT_BOLD},
    };
    OUTLINETEXTMETRICW otm;
    IDWriteFactory *factory;
    HRESULT hr;
    BOOL ret;
    HDC hdc;
    HFONT hfont;
    BOOL exists;
    ULONG ref;
    int i;
    UINT r;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got %#x\n", hr);

    if (0)
        /* null out parameter crashes this call */
        hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, NULL, NULL);

    font = (void*)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, NULL, &font);
    EXPECT_HR(hr, E_INVALIDARG);
    ok(font == NULL, "got %p\n", font);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    EXPECT_HR(hr, S_OK);

    hfont = CreateFontIndirectW(&logfont);
    hdc = CreateCompatibleDC(0);
    SelectObject(hdc, hfont);

    otm.otmSize = sizeof(otm);
    r = GetOutlineTextMetricsW(hdc, otm.otmSize, &otm);
    ok(r, "got %d\n", r);
    DeleteDC(hdc);
    DeleteObject(hfont);

    exists = TRUE;
    hr = IDWriteFont_HasCharacter(font, 0xd800, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == FALSE, "got %d\n", exists);

    exists = FALSE;
    hr = IDWriteFont_HasCharacter(font, 0x20, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == TRUE, "got %d\n", exists);

    /* now check properties */
    weight = IDWriteFont_GetWeight(font);
    ok(weight == DWRITE_FONT_WEIGHT_NORMAL, "got %d\n", weight);

    style = IDWriteFont_GetStyle(font);
    ok(style == DWRITE_FONT_STYLE_OBLIQUE, "got %d\n", style);
    ok(otm.otmfsSelection & 1, "got 0x%08x\n", otm.otmfsSelection);

    ret = IDWriteFont_IsSymbolFont(font);
    ok(!ret, "got %d\n", ret);

    IDWriteFont_Release(font);

    /* weight values */
    for (i = 0; i < sizeof(weights)/(2*sizeof(LONG)); i++)
    {
        memset(&logfont, 0, sizeof(logfont));
        logfont.lfHeight = 12;
        logfont.lfWidth  = 12;
        logfont.lfWeight = weights[i][0];
        lstrcpyW(logfont.lfFaceName, tahomaW);

        hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
        EXPECT_HR(hr, S_OK);

        weight = IDWriteFont_GetWeight(font);
        ok(weight == weights[i][1],
            "%d: got %d, expected %d\n", i, weight, weights[i][1]);

        IDWriteFont_Release(font);
    }

    /* weight not from enum */
    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = 550;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    font = NULL;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    weight = IDWriteFont_GetWeight(font);
    ok(weight == DWRITE_FONT_WEIGHT_NORMAL || weight == DWRITE_FONT_WEIGHT_BOLD,
        "got %d\n", weight);

    IDWriteFont_Release(font);

    /* empty or nonexistent face name */
    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    lstrcpyW(logfont.lfFaceName, blahW);

    font = (void*)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == DWRITE_E_NOFONT, "got 0x%08x\n", hr);
    ok(font == NULL, "got %p\n", font);

    /* Try with name 'Tahoma ' */
    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    lstrcpyW(logfont.lfFaceName, tahomaspW);

    font = (void*)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == DWRITE_E_NOFONT, "got 0x%08x\n", hr);
    ok(font == NULL, "got %p\n", font);

    /* empty string as a facename */
    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;

    font = (void*)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == DWRITE_E_NOFONT, "got 0x%08x\n", hr);
    ok(font == NULL, "got %p\n", font);

    /* IDWriteGdiInterop1::CreateFontFromLOGFONT() */
    hr = IDWriteGdiInterop_QueryInterface(interop, &IID_IDWriteGdiInterop1, (void**)&interop1);
    if (hr == S_OK) {
        memset(&logfont, 0, sizeof(logfont));
        logfont.lfHeight = 12;
        logfont.lfWidth  = 12;
        logfont.lfWeight = FW_NORMAL;
        logfont.lfItalic = 1;
        lstrcpyW(logfont.lfFaceName, tahomaW);

        hr = IDWriteGdiInterop1_CreateFontFromLOGFONT(interop1, &logfont, NULL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        IDWriteFont_Release(font);
        IDWriteGdiInterop1_Release(interop1);
    }
    else
        win_skip("IDWriteGdiInterop1 is not supported, skipping CreateFontFromLOGFONT() tests.\n");

    ref = IDWriteGdiInterop_Release(interop);
    ok(ref == 0, "interop is not released, %u\n", ref);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory is not released, %u\n", ref);
}

static void test_CreateBitmapRenderTarget(void)
{
    IDWriteBitmapRenderTarget *target, *target2;
    IDWriteBitmapRenderTarget1 *target1;
    IDWriteRenderingParams *params;
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    HBITMAP hbm, hbm2;
    UINT16 glyphs[2];
    DWRITE_MATRIX m;
    DIBSECTION ds;
    XFORM xform;
    COLORREF c;
    HRESULT hr;
    FLOAT pdip;
    SIZE size;
    ULONG ref;
    UINT32 ch;
    HDC hdc;
    int ret;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    EXPECT_HR(hr, S_OK);

    target = NULL;
    hr = IDWriteGdiInterop_CreateBitmapRenderTarget(interop, NULL, 0, 0, &target);
    EXPECT_HR(hr, S_OK);

    if (0) /* crashes on native */
        hr = IDWriteBitmapRenderTarget_GetSize(target, NULL);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    EXPECT_HR(hr, S_OK);
    ok(size.cx == 0, "got %d\n", size.cx);
    ok(size.cy == 0, "got %d\n", size.cy);

    target2 = NULL;
    hr = IDWriteGdiInterop_CreateBitmapRenderTarget(interop, NULL, 0, 0, &target2);
    EXPECT_HR(hr, S_OK);
    ok(target != target2, "got %p, %p\n", target2, target);
    IDWriteBitmapRenderTarget_Release(target2);

    hdc = IDWriteBitmapRenderTarget_GetMemoryDC(target);
    ok(hdc != NULL, "got %p\n", hdc);

    /* test mode */
    ret = GetGraphicsMode(hdc);
    ok(ret == GM_ADVANCED, "got %d\n", ret);

    hbm = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm != NULL, "got %p\n", hbm);

    /* check DIB properties */
    ret = GetObjectW(hbm, sizeof(ds), &ds);
    ok(ret == sizeof(BITMAP), "got %d\n", ret);
    ok(ds.dsBm.bmWidth == 1, "got %d\n", ds.dsBm.bmWidth);
    ok(ds.dsBm.bmHeight == 1, "got %d\n", ds.dsBm.bmHeight);
    ok(ds.dsBm.bmPlanes == 1, "got %d\n", ds.dsBm.bmPlanes);
    ok(ds.dsBm.bmBitsPixel == 1, "got %d\n", ds.dsBm.bmBitsPixel);
    ok(!ds.dsBm.bmBits, "got %p\n", ds.dsBm.bmBits);

    IDWriteBitmapRenderTarget_Release(target);

    hbm = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(!hbm, "got %p\n", hbm);

    target = NULL;
    hr = IDWriteGdiInterop_CreateBitmapRenderTarget(interop, NULL, 10, 5, &target);
    EXPECT_HR(hr, S_OK);

    hdc = IDWriteBitmapRenderTarget_GetMemoryDC(target);
    ok(hdc != NULL, "got %p\n", hdc);

    /* test context settings */
    c = GetTextColor(hdc);
    ok(c == RGB(0, 0, 0), "got 0x%08x\n", c);
    ret = GetBkMode(hdc);
    ok(ret == OPAQUE, "got %d\n", ret);
    c = GetBkColor(hdc);
    ok(c == RGB(255, 255, 255), "got 0x%08x\n", c);

    hbm = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm != NULL, "got %p\n", hbm);

    /* check DIB properties */
    ret = GetObjectW(hbm, sizeof(ds), &ds);
    ok(ret == sizeof(ds), "got %d\n", ret);
    ok(ds.dsBm.bmWidth == 10, "got %d\n", ds.dsBm.bmWidth);
    ok(ds.dsBm.bmHeight == 5, "got %d\n", ds.dsBm.bmHeight);
    ok(ds.dsBm.bmPlanes == 1, "got %d\n", ds.dsBm.bmPlanes);
    ok(ds.dsBm.bmBitsPixel == 32, "got %d\n", ds.dsBm.bmBitsPixel);
    ok(ds.dsBm.bmBits != NULL, "got %p\n", ds.dsBm.bmBits);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    EXPECT_HR(hr, S_OK);
    ok(size.cx == 10, "got %d\n", size.cx);
    ok(size.cy == 5, "got %d\n", size.cy);

    /* resize to same size */
    hr = IDWriteBitmapRenderTarget_Resize(target, 10, 5);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hbm2 = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm2 == hbm, "got %p, %p\n", hbm2, hbm);

    /* shrink */
    hr = IDWriteBitmapRenderTarget_Resize(target, 5, 5);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size.cx == 5, "got %d\n", size.cx);
    ok(size.cy == 5, "got %d\n", size.cy);

    hbm2 = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm2 != hbm, "got %p, %p\n", hbm2, hbm);

    hr = IDWriteBitmapRenderTarget_Resize(target, 20, 5);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size.cx == 20, "got %d\n", size.cx);
    ok(size.cy == 5, "got %d\n", size.cy);

    hbm2 = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm2 != hbm, "got %p, %p\n", hbm2, hbm);

    hr = IDWriteBitmapRenderTarget_Resize(target, 1, 5);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size.cx == 1, "got %d\n", size.cx);
    ok(size.cy == 5, "got %d\n", size.cy);

    hbm2 = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm2 != hbm, "got %p, %p\n", hbm2, hbm);

    ret = GetObjectW(hbm2, sizeof(ds), &ds);
    ok(ret == sizeof(ds), "got %d\n", ret);
    ok(ds.dsBm.bmWidth == 1, "got %d\n", ds.dsBm.bmWidth);
    ok(ds.dsBm.bmHeight == 5, "got %d\n", ds.dsBm.bmHeight);
    ok(ds.dsBm.bmPlanes == 1, "got %d\n", ds.dsBm.bmPlanes);
    ok(ds.dsBm.bmBitsPixel == 32, "got %d\n", ds.dsBm.bmBitsPixel);
    ok(ds.dsBm.bmBits != NULL, "got %p\n", ds.dsBm.bmBits);

    /* empty rectangle */
    hr = IDWriteBitmapRenderTarget_Resize(target, 0, 5);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    size.cx = size.cy = -1;
    hr = IDWriteBitmapRenderTarget_GetSize(target, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size.cx == 0, "got %d\n", size.cx);
    ok(size.cy == 5, "got %d\n", size.cy);

    hbm2 = GetCurrentObject(hdc, OBJ_BITMAP);
    ok(hbm2 != hbm, "got %p, %p\n", hbm2, hbm);

    ret = GetObjectW(hbm2, sizeof(ds), &ds);
    ok(ret == sizeof(BITMAP), "got %d\n", ret);
    ok(ds.dsBm.bmWidth == 1, "got %d\n", ds.dsBm.bmWidth);
    ok(ds.dsBm.bmHeight == 1, "got %d\n", ds.dsBm.bmHeight);
    ok(ds.dsBm.bmPlanes == 1, "got %d\n", ds.dsBm.bmPlanes);
    ok(ds.dsBm.bmBitsPixel == 1, "got %d\n", ds.dsBm.bmBitsPixel);
    ok(!ds.dsBm.bmBits, "got %p\n", ds.dsBm.bmBits);

    /* transform tests, current hdc transform is not immediately affected */
    if (0) /* crashes on native */
        hr = IDWriteBitmapRenderTarget_GetCurrentTransform(target, NULL);

    memset(&m, 0xcc, sizeof(m));
    hr = IDWriteBitmapRenderTarget_GetCurrentTransform(target, &m);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(m.m11 == 1.0 && m.m22 == 1.0 && m.m12 == 0.0 && m.m21 == 0.0, "got %.1f,%.1f,%.1f,%.1f\n", m.m11, m.m22, m.m12, m.m21);
    ok(m.dx == 0.0 && m.dy == 0.0, "got %.1f,%.1f\n", m.dx, m.dy);
    ret = GetWorldTransform(hdc, &xform);
    ok(ret, "got %d\n", ret);
    ok(xform.eM11 == 1.0 && xform.eM22 == 1.0 && xform.eM12 == 0.0 && xform.eM21 == 0.0, "got wrong transform\n");
    ok(xform.eDx == 0.0 && xform.eDy == 0.0, "got %.1f,%.1f\n", xform.eDx, xform.eDy);

    memset(&m, 0, sizeof(m));
    hr = IDWriteBitmapRenderTarget_SetCurrentTransform(target, &m);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&m, 0xcc, sizeof(m));
    hr = IDWriteBitmapRenderTarget_GetCurrentTransform(target, &m);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(m.m11 == 0.0 && m.m22 == 0.0 && m.m12 == 0.0 && m.m21 == 0.0, "got %.1f,%.1f,%.1f,%.1f\n", m.m11, m.m22, m.m12, m.m21);
    ok(m.dx == 0.0 && m.dy == 0.0, "got %.1f,%.1f\n", m.dx, m.dy);
    ret = GetWorldTransform(hdc, &xform);
    ok(ret, "got %d\n", ret);
    ok(xform.eM11 == 1.0 && xform.eM22 == 1.0 && xform.eM12 == 0.0 && xform.eM21 == 0.0, "got wrong transform\n");
    ok(xform.eDx == 0.0 && xform.eDy == 0.0, "got %.1f,%.1f\n", xform.eDx, xform.eDy);

    memset(&m, 0, sizeof(m));
    m.m11 = 2.0; m.m22 = 1.0;
    hr = IDWriteBitmapRenderTarget_SetCurrentTransform(target, &m);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ret = GetWorldTransform(hdc, &xform);
    ok(ret, "got %d\n", ret);
    ok(xform.eM11 == 1.0 && xform.eM22 == 1.0 && xform.eM12 == 0.0 && xform.eM21 == 0.0, "got wrong transform\n");
    ok(xform.eDx == 0.0 && xform.eDy == 0.0, "got %.1f,%.1f\n", xform.eDx, xform.eDy);

    hr = IDWriteBitmapRenderTarget_SetCurrentTransform(target, NULL);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&m, 0xcc, sizeof(m));
    hr = IDWriteBitmapRenderTarget_GetCurrentTransform(target, &m);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(m.m11 == 1.0 && m.m22 == 1.0 && m.m12 == 0.0 && m.m21 == 0.0, "got %.1f,%.1f,%.1f,%.1f\n", m.m11, m.m22, m.m12, m.m21);
    ok(m.dx == 0.0 && m.dy == 0.0, "got %.1f,%.1f\n", m.dx, m.dy);

    /* pixels per dip */
    pdip = IDWriteBitmapRenderTarget_GetPixelsPerDip(target);
    ok(pdip == 1.0, "got %.2f\n", pdip);

    hr = IDWriteBitmapRenderTarget_SetPixelsPerDip(target, 2.0);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteBitmapRenderTarget_SetPixelsPerDip(target, -1.0);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteBitmapRenderTarget_SetPixelsPerDip(target, 0.0);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    pdip = IDWriteBitmapRenderTarget_GetPixelsPerDip(target);
    ok(pdip == 2.0, "got %.2f\n", pdip);

    hr = IDWriteBitmapRenderTarget_QueryInterface(target, &IID_IDWriteBitmapRenderTarget1, (void**)&target1);
    if (hr == S_OK) {
        DWRITE_TEXT_ANTIALIAS_MODE mode;

        mode = IDWriteBitmapRenderTarget1_GetTextAntialiasMode(target1);
        ok(mode == DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE, "got %d\n", mode);

        hr = IDWriteBitmapRenderTarget1_SetTextAntialiasMode(target1, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE+1);
        ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

        mode = IDWriteBitmapRenderTarget1_GetTextAntialiasMode(target1);
        ok(mode == DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE, "got %d\n", mode);

        hr = IDWriteBitmapRenderTarget1_SetTextAntialiasMode(target1, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        mode = IDWriteBitmapRenderTarget1_GetTextAntialiasMode(target1);
        ok(mode == DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, "got %d\n", mode);

        IDWriteBitmapRenderTarget1_Release(target1);
    }
    else
        win_skip("IDWriteBitmapRenderTarget1 is not supported.\n");

    /* DrawGlyphRun() argument validation. */
    hr = IDWriteBitmapRenderTarget_Resize(target, 16, 16);
    ok(hr == S_OK, "Failed to resize target, hr %#x.\n", hr);

    fontface = create_fontface(factory);

    ch = 'A';
    glyphs[0] = 0;
    hr = IDWriteFontFace_GetGlyphIndices(fontface, &ch, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyphs[0] > 0, "got %u\n", glyphs[0]);
    glyphs[1] = glyphs[0];

    memset(&run, 0, sizeof(run));
    run.fontFace = fontface;
    run.fontEmSize = 12.0f;
    run.glyphCount = 2;
    run.glyphIndices = glyphs;

    hr = IDWriteFactory_CreateCustomRenderingParams(factory, 1.0f, 0.0f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
            DWRITE_RENDERING_MODE_DEFAULT, &params);
    ok(hr == S_OK, "Failed to create rendering params, hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_NATURAL,
        &run, NULL, RGB(255, 0, 0), NULL);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_GDI_NATURAL + 1,
        &run, NULL, RGB(255, 0, 0), NULL);
    ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_GDI_NATURAL + 1,
        &run, params, RGB(255, 0, 0), NULL);
    ok(hr == E_INVALIDARG || broken(hr == S_OK) /* Vista */, "Unexpected hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_GDI_NATURAL,
        &run, params, RGB(255, 0, 0), NULL);
    ok(hr == S_OK, "Failed to draw a run, hr %#x.\n", hr);

    IDWriteRenderingParams_Release(params);

    /* Zero sized target returns earlier. */
    hr = IDWriteBitmapRenderTarget_Resize(target, 0, 16);
    ok(hr == S_OK, "Failed to resize target, hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_NATURAL,
        &run, NULL, RGB(255, 0, 0), NULL);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    hr = IDWriteBitmapRenderTarget_DrawGlyphRun(target, 0.0f, 0.0f, DWRITE_MEASURING_MODE_GDI_NATURAL + 1,
        &run, params, RGB(255, 0, 0), NULL);
    ok(hr == S_OK, "Unexpected hr %#x.\n", hr);

    IDWriteFontFace_Release(fontface);

    ref = IDWriteBitmapRenderTarget_Release(target);
    ok(ref == 0, "render target not released, %u\n", ref);
    ref = IDWriteGdiInterop_Release(interop);
    ok(ref == 0, "interop not released, %u\n", ref);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetFontFamily(void)
{
    IDWriteFontCollection *collection, *collection2;
    IDWriteFontCollection *syscoll;
    IDWriteFontFamily *family, *family2;
    IDWriteFontFamily1 *family1;
    IDWriteGdiInterop *interop;
    IDWriteFont *font, *font2;
    IDWriteFactory *factory;
    LOGFONTW logfont;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    EXPECT_HR(hr, S_OK);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscoll, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font2 != font, "got %p, %p\n", font2, font);

if (0) /* crashes on native */
    hr = IDWriteFont_GetFontFamily(font, NULL);

    EXPECT_REF(font, 1);
    hr = IDWriteFont_GetFontFamily(font, &family);
    EXPECT_HR(hr, S_OK);
    EXPECT_REF(font, 1);
    EXPECT_REF(family, 2);

    hr = IDWriteFont_GetFontFamily(font, &family2);
    EXPECT_HR(hr, S_OK);
    ok(family2 == family, "got %p, previous %p\n", family2, family);
    EXPECT_REF(font, 1);
    EXPECT_REF(family, 3);
    IDWriteFontFamily_Release(family2);

    hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFontFamily, (void**)&family2);
    EXPECT_HR(hr, E_NOINTERFACE);
    ok(family2 == NULL, "got %p\n", family2);

    hr = IDWriteFont_GetFontFamily(font2, &family2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(family2 != family, "got %p, %p\n", family2, family);

    collection = NULL;
    hr = IDWriteFontFamily_GetFontCollection(family, &collection);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    collection2 = NULL;
    hr = IDWriteFontFamily_GetFontCollection(family2, &collection2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(collection == collection2, "got %p, %p\n", collection, collection2);
    ok(collection == syscoll, "got %p, %p\n", collection, syscoll);

    IDWriteFont_Release(font);
    IDWriteFont_Release(font2);

    hr = IDWriteFontFamily_QueryInterface(family, &IID_IDWriteFontFamily1, (void**)&family1);
    if (hr == S_OK) {
        IDWriteFontFaceReference *ref, *ref1;
        IDWriteFontList *fontlist;
        IDWriteFont3 *font3;
        IDWriteFont1 *font1;

        font3 = (void*)0xdeadbeef;
        hr = IDWriteFontFamily1_GetFont(family1, ~0u, &font3);
        ok(hr == E_FAIL, "got 0x%08x\n", hr);
        ok(font3 == NULL, "got %p\n", font3);

        hr = IDWriteFontFamily1_GetFont(family1, 0, &font3);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFont3_QueryInterface(font3, &IID_IDWriteFont, (void**)&font);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFont_Release(font);

        hr = IDWriteFont3_QueryInterface(font3, &IID_IDWriteFont1, (void**)&font1);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFont1_Release(font1);

        hr = IDWriteFontFamily1_QueryInterface(family1, &IID_IDWriteFontList1, (void**)&fontlist);
        ok(hr == E_NOINTERFACE, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily1_QueryInterface(family1, &IID_IDWriteFontList, (void**)&fontlist);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFontList_Release(fontlist);

        IDWriteFont3_Release(font3);

        hr = IDWriteFontFamily1_GetFontFaceReference(family1, 0, &ref);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily1_GetFontFaceReference(family1, 0, &ref1);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(ref != ref1, "got %p, %p\n", ref, ref1);

        IDWriteFontFaceReference_Release(ref);
        IDWriteFontFaceReference_Release(ref1);

        IDWriteFontFamily1_Release(family1);
    }
    else
        win_skip("IDWriteFontFamily1 is not supported.\n");

    IDWriteFontCollection_Release(syscoll);
    IDWriteFontCollection_Release(collection2);
    IDWriteFontCollection_Release(collection);
    IDWriteFontFamily_Release(family2);
    IDWriteFontFamily_Release(family);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetFamilyNames(void)
{
    IDWriteFontFamily *family;
    IDWriteLocalizedStrings *names, *names2;
    IDWriteGdiInterop *interop;
    IDWriteFactory *factory;
    IDWriteFont *font;
    LOGFONTW logfont;
    WCHAR buffer[100];
    HRESULT hr;
    UINT32 len;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    EXPECT_HR(hr, S_OK);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    EXPECT_HR(hr, S_OK);

    hr = IDWriteFont_GetFontFamily(font, &family);
    EXPECT_HR(hr, S_OK);

    if (0) /* crashes on native */
        hr = IDWriteFontFamily_GetFamilyNames(family, NULL);

    hr = IDWriteFontFamily_GetFamilyNames(family, &names);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(names, 1);

    hr = IDWriteFontFamily_GetFamilyNames(family, &names2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(names2, 1);
    ok(names != names2, "got %p, was %p\n", names2, names);

    IDWriteLocalizedStrings_Release(names2);

    /* GetStringLength */
    if (0) /* crashes on native */
        hr = IDWriteLocalizedStrings_GetStringLength(names, 0, NULL);

    len = 100;
    hr = IDWriteLocalizedStrings_GetStringLength(names, 10, &len);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(len == (UINT32)-1, "got %u\n", len);

    len = 0;
    hr = IDWriteLocalizedStrings_GetStringLength(names, 0, &len);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(len > 0, "got %u\n", len);

    /* GetString */
    hr = IDWriteLocalizedStrings_GetString(names, 0, NULL, 0);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);

    hr = IDWriteLocalizedStrings_GetString(names, 10, NULL, 0);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);

    if (0)
        hr = IDWriteLocalizedStrings_GetString(names, 0, NULL, 100);

    buffer[0] = 1;
    hr = IDWriteLocalizedStrings_GetString(names, 10, buffer, 100);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(buffer[0] == 0, "got %x\n", buffer[0]);

    buffer[0] = 1;
    hr = IDWriteLocalizedStrings_GetString(names, 0, buffer, len-1);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buffer[0] == 0, "got %x\n", buffer[0]);

    buffer[0] = 1;
    hr = IDWriteLocalizedStrings_GetString(names, 0, buffer, len);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buffer[0] == 0, "got %x\n", buffer[0]);

    buffer[0] = 0;
    hr = IDWriteLocalizedStrings_GetString(names, 0, buffer, len+1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(buffer[0] != 0, "got %x\n", buffer[0]);

    IDWriteLocalizedStrings_Release(names);

    IDWriteFontFamily_Release(family);
    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateFontFace(void)
{
    IDWriteFontFace *fontface, *fontface2;
    IDWriteFontCollection *collection;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    IDWriteGdiInterop *interop;
    IDWriteFont *font, *font2;
    IDWriteFontFamily *family;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    LOGFONTW logfont;
    BOOL supported;
    UINT32 count;
    WCHAR *path;
    HRESULT hr;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    EXPECT_HR(hr, S_OK);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    font = NULL;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    font2 = NULL;
    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font != font2, "got %p, %p\n", font, font2);

    hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFontFace, (void**)&fontface);
    ok(hr == E_NOINTERFACE, "got 0x%08x\n", hr);

    if (0) /* crashes on native */
        hr = IDWriteFont_CreateFontFace(font, NULL);

    fontface = NULL;
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    fontface2 = NULL;
    hr = IDWriteFont_CreateFontFace(font, &fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface == fontface2, "got %p, was %p\n", fontface2, fontface);
    IDWriteFontFace_Release(fontface2);

    fontface2 = NULL;
    hr = IDWriteFont_CreateFontFace(font2, &fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface == fontface2, "got %p, was %p\n", fontface2, fontface);
    IDWriteFontFace_Release(fontface2);

    IDWriteFont_Release(font2);
    IDWriteFont_Release(font);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFont, (void**)&font);
    ok(hr == E_NOINTERFACE || broken(hr == E_NOTIMPL), "got 0x%08x\n", hr);

    IDWriteFontFace_Release(fontface);
    IDWriteGdiInterop_Release(interop);

    /* Create from system collection */
    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    font = NULL;
    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    font2 = NULL;
    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font != font2, "got %p, %p\n", font, font2);

    fontface = NULL;
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    fontface2 = NULL;
    hr = IDWriteFont_CreateFontFace(font2, &fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface == fontface2, "got %p, was %p\n", fontface2, fontface);

    IDWriteFontFace_Release(fontface);
    IDWriteFontFace_Release(fontface2);
    IDWriteFont_Release(font2);
    IDWriteFont_Release(font);
    IDWriteFontFamily_Release(family);
    IDWriteFontCollection_Release(collection);

    /* IDWriteFactory::CreateFontFace() */
    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    supported = FALSE;
    file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    face_type = DWRITE_FONT_FACE_TYPE_CFF;
    count = 0;
    hr = IDWriteFontFile_Analyze(file, &supported, &file_type, &face_type, &count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(supported == TRUE, "got %i\n", supported);
    ok(file_type == DWRITE_FONT_FILE_TYPE_TRUETYPE, "got %i\n", file_type);
    ok(face_type == DWRITE_FONT_FACE_TYPE_TRUETYPE, "got %i\n", face_type);
    ok(count == 1, "got %i\n", count);

    /* invalid simulation flags */
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_CFF, 1, &file, 0, ~0u, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_CFF, 1, &file, 0, 0xf, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* try mismatching face type, the one that's not supported */
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_CFF, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == DWRITE_E_FILEFORMAT, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION, 1, &file, 0,
        DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == DWRITE_E_FILEFORMAT || broken(hr == E_FAIL) /* < win10 */, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_RAW_CFF, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
todo_wine
    ok(hr == DWRITE_E_UNSUPPORTEDOPERATION || broken(hr == E_INVALIDARG) /* older versions */, "got 0x%08x\n", hr);

    fontface = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TYPE1, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    fontface = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_VECTOR, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    fontface = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_BITMAP, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    fontface = NULL;
    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_UNKNOWN, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
todo_wine
    ok(hr == S_OK || broken(hr == E_INVALIDARG) /* < win10 */, "got 0x%08x\n", hr);
    if (hr == S_OK) {
        ok(fontface != NULL, "got %p\n", fontface);
        face_type = IDWriteFontFace_GetType(fontface);
        ok(face_type == DWRITE_FONT_FACE_TYPE_TRUETYPE, "got %d\n", face_type);
        IDWriteFontFace_Release(fontface);
    }

    IDWriteFontFile_Release(file);
    IDWriteFactory_Release(factory);
    DELETE_FONTFILE(path);
}

static void get_expected_font_metrics(IDWriteFontFace *fontface, DWRITE_FONT_METRICS1 *metrics)
{
    void *os2_context, *head_context, *post_context, *hhea_context;
    const TT_OS2_V2 *tt_os2;
    const TT_HEAD *tt_head;
    const TT_POST *tt_post;
    const TT_HHEA *tt_hhea;
    UINT32 size;
    BOOL exists;
    HRESULT hr;

    memset(metrics, 0, sizeof(*metrics));

    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_0S2_TAG, (const void**)&tt_os2, &size, &os2_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_HEAD_TAG, (const void**)&tt_head, &size, &head_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_HHEA_TAG, (const void**)&tt_hhea, &size, &hhea_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_POST_TAG, (const void**)&tt_post, &size, &post_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    if (tt_head) {
        metrics->designUnitsPerEm = GET_BE_WORD(tt_head->unitsPerEm);
        metrics->glyphBoxLeft = GET_BE_WORD(tt_head->xMin);
        metrics->glyphBoxTop = GET_BE_WORD(tt_head->yMax);
        metrics->glyphBoxRight = GET_BE_WORD(tt_head->xMax);
        metrics->glyphBoxBottom = GET_BE_WORD(tt_head->yMin);
    }

    if (tt_os2) {
        if (GET_BE_WORD(tt_os2->fsSelection) & OS2_FSSELECTION_USE_TYPO_METRICS) {
            SHORT descent = GET_BE_WORD(tt_os2->sTypoDescender);
            metrics->ascent = GET_BE_WORD(tt_os2->sTypoAscender);
            metrics->descent = descent < 0 ? -descent : 0;
            metrics->lineGap = GET_BE_WORD(tt_os2->sTypoLineGap);
            metrics->hasTypographicMetrics = TRUE;
        }
        else {
            metrics->ascent  = GET_BE_WORD(tt_os2->usWinAscent);
            /* Some fonts have usWinDescent value stored as signed short, which could be wrongly
               interpreted as large unsigned value. */
            metrics->descent = abs((SHORT)GET_BE_WORD(tt_os2->usWinDescent));

            if (tt_hhea) {
                SHORT descender = (SHORT)GET_BE_WORD(tt_hhea->descender);
                INT32 linegap;

                linegap = GET_BE_WORD(tt_hhea->ascender) + abs(descender) + GET_BE_WORD(tt_hhea->linegap) -
                    metrics->ascent - metrics->descent;
                metrics->lineGap = linegap > 0 ? linegap : 0;
            }
        }

        metrics->strikethroughPosition  = GET_BE_WORD(tt_os2->yStrikeoutPosition);
        metrics->strikethroughThickness = GET_BE_WORD(tt_os2->yStrikeoutSize);

        metrics->subscriptPositionX = GET_BE_WORD(tt_os2->ySubscriptXOffset);
        metrics->subscriptPositionY = -GET_BE_WORD(tt_os2->ySubscriptYOffset);
        metrics->subscriptSizeX = GET_BE_WORD(tt_os2->ySubscriptXSize);
        metrics->subscriptSizeY = GET_BE_WORD(tt_os2->ySubscriptYSize);
        metrics->superscriptPositionX = GET_BE_WORD(tt_os2->ySuperscriptXOffset);
        metrics->superscriptPositionY = GET_BE_WORD(tt_os2->ySuperscriptYOffset);
        metrics->superscriptSizeX = GET_BE_WORD(tt_os2->ySuperscriptXSize);
        metrics->superscriptSizeY = GET_BE_WORD(tt_os2->ySuperscriptYSize);
    }

    if (tt_post) {
        metrics->underlinePosition = GET_BE_WORD(tt_post->underlinePosition);
        metrics->underlineThickness = GET_BE_WORD(tt_post->underlineThickness);
    }

    if (metrics->underlineThickness == 0)
        metrics->underlineThickness = metrics->designUnitsPerEm / 14;
    if (metrics->strikethroughThickness == 0)
        metrics->strikethroughThickness = metrics->underlineThickness;

    if (tt_os2)
        IDWriteFontFace_ReleaseFontTable(fontface, os2_context);
    if (tt_head)
        IDWriteFontFace_ReleaseFontTable(fontface, head_context);
    if (tt_hhea)
        IDWriteFontFace_ReleaseFontTable(fontface, hhea_context);
    if (tt_post)
        IDWriteFontFace_ReleaseFontTable(fontface, post_context);
}

static void check_font_metrics(const WCHAR *nameW, BOOL has_metrics1, const DWRITE_FONT_METRICS *got,
    const DWRITE_FONT_METRICS1 *expected)
{
    ok(got->designUnitsPerEm == expected->designUnitsPerEm, "font %s: designUnitsPerEm %u, expected %u\n",
        wine_dbgstr_w(nameW), got->designUnitsPerEm, expected->designUnitsPerEm);
    ok(got->ascent == expected->ascent, "font %s: ascent %u, expected %u\n", wine_dbgstr_w(nameW), got->ascent,
        expected->ascent);
    ok(got->descent == expected->descent, "font %s: descent %u, expected %u\n", wine_dbgstr_w(nameW), got->descent,
        expected->descent);
    ok(got->lineGap == expected->lineGap, "font %s: lineGap %d, expected %d\n", wine_dbgstr_w(nameW), got->lineGap,
        expected->lineGap);
    ok(got->underlinePosition == expected->underlinePosition, "font %s: underlinePosition %d, expected %d\n",
        wine_dbgstr_w(nameW), got->underlinePosition, expected->underlinePosition);
    ok(got->underlineThickness == expected->underlineThickness, "font %s: underlineThickness %u, "
        "expected %u\n", wine_dbgstr_w(nameW), got->underlineThickness, expected->underlineThickness);
    ok(got->strikethroughPosition == expected->strikethroughPosition, "font %s: strikethroughPosition %d, expected %d\n",
        wine_dbgstr_w(nameW), got->strikethroughPosition, expected->strikethroughPosition);
    ok(got->strikethroughThickness == expected->strikethroughThickness, "font %s: strikethroughThickness %u, "
        "expected %u\n", wine_dbgstr_w(nameW), got->strikethroughThickness, expected->strikethroughThickness);

    if (has_metrics1) {
        const DWRITE_FONT_METRICS1 *m1 = (const DWRITE_FONT_METRICS1*)got;
        ok(m1->hasTypographicMetrics == expected->hasTypographicMetrics, "font %s: hasTypographicMetrics %d, "
            "expected %d\n", wine_dbgstr_w(nameW), m1->hasTypographicMetrics, expected->hasTypographicMetrics);
        ok(m1->glyphBoxLeft == expected->glyphBoxLeft, "font %s: glyphBoxLeft %d, expected %d\n", wine_dbgstr_w(nameW),
            m1->glyphBoxLeft, expected->glyphBoxLeft);
        ok(m1->glyphBoxTop == expected->glyphBoxTop, "font %s: glyphBoxTop %d, expected %d\n", wine_dbgstr_w(nameW),
            m1->glyphBoxTop, expected->glyphBoxTop);
        ok(m1->glyphBoxRight == expected->glyphBoxRight, "font %s: glyphBoxRight %d, expected %d\n", wine_dbgstr_w(nameW),
            m1->glyphBoxRight, expected->glyphBoxRight);
        ok(m1->glyphBoxBottom == expected->glyphBoxBottom, "font %s: glyphBoxBottom %d, expected %d\n", wine_dbgstr_w(nameW),
            m1->glyphBoxBottom, expected->glyphBoxBottom);

        ok(m1->subscriptPositionX == expected->subscriptPositionX, "font %s: subscriptPositionX %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->subscriptPositionX, expected->subscriptPositionX);
        ok(m1->subscriptPositionY == expected->subscriptPositionY, "font %s: subscriptPositionY %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->subscriptPositionY, expected->subscriptPositionY);
        ok(m1->subscriptSizeX == expected->subscriptSizeX, "font %s: subscriptSizeX %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->subscriptSizeX, expected->subscriptSizeX);
        ok(m1->subscriptSizeY == expected->subscriptSizeY, "font %s: subscriptSizeY %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->subscriptSizeY, expected->subscriptSizeY);
        ok(m1->superscriptPositionX == expected->superscriptPositionX, "font %s: superscriptPositionX %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->superscriptPositionX, expected->superscriptPositionX);
        ok(m1->superscriptPositionY == expected->superscriptPositionY, "font %s: superscriptPositionY %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->superscriptPositionY, expected->superscriptPositionY);
        ok(m1->superscriptSizeX == expected->superscriptSizeX, "font %s: superscriptSizeX %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->superscriptSizeX, expected->superscriptSizeX);
        ok(m1->superscriptSizeY == expected->superscriptSizeY, "font %s: superscriptSizeY %d, expected %d\n",
            wine_dbgstr_w(nameW), m1->superscriptSizeY, expected->superscriptSizeY);
    }
}

static void get_enus_string(IDWriteLocalizedStrings *strings, WCHAR *buff, UINT32 size)
{
    static const WCHAR enusW[] = {'e','n','-','u','s',0};
    BOOL exists = FALSE;
    UINT32 index;
    HRESULT hr;

    hr = IDWriteLocalizedStrings_FindLocaleName(strings, enusW, &index, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists, "got %d\n", exists);

    hr = IDWriteLocalizedStrings_GetString(strings, index, buff, size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
}

static void test_GetMetrics(void)
{
    DWRITE_FONT_METRICS metrics, metrics2;
    IDWriteFontCollection *syscollection;
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    OUTLINETEXTMETRICW otm;
    IDWriteFontFile *file;
    IDWriteFont1 *font1;
    IDWriteFont *font;
    LOGFONTW logfont;
    UINT32 count, i;
    HRESULT hr;
    HDC hdc;
    HFONT hfont;
    ULONG ref;
    int ret;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    EXPECT_HR(hr, S_OK);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hfont = CreateFontIndirectW(&logfont);
    hdc = CreateCompatibleDC(0);
    SelectObject(hdc, hfont);

    otm.otmSize = sizeof(otm);
    ret = GetOutlineTextMetricsW(hdc, otm.otmSize, &otm);
    ok(ret, "got %d\n", ret);
    DeleteDC(hdc);
    DeleteObject(hfont);

    if (0) /* crashes on native */
        IDWriteFont_GetMetrics(font, NULL);

    memset(&metrics, 0, sizeof(metrics));
    IDWriteFont_GetMetrics(font, &metrics);

    ok(metrics.designUnitsPerEm != 0, "designUnitsPerEm %u\n", metrics.designUnitsPerEm);
    ok(metrics.ascent != 0, "ascent %u\n", metrics.ascent);
    ok(metrics.descent != 0, "descent %u\n", metrics.descent);
    ok(metrics.lineGap == 0, "lineGap %d\n", metrics.lineGap);
    ok(metrics.capHeight, "capHeight %u\n", metrics.capHeight);
    ok(metrics.xHeight != 0, "xHeight %u\n", metrics.xHeight);
    ok(metrics.underlinePosition < 0, "underlinePosition %d\n", metrics.underlinePosition);
    ok(metrics.underlineThickness != 0, "underlineThickness %u\n", metrics.underlineThickness);
    ok(metrics.strikethroughPosition > 0, "strikethroughPosition %d\n", metrics.strikethroughPosition);
    ok(metrics.strikethroughThickness != 0, "strikethroughThickness %u\n", metrics.strikethroughThickness);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&metrics, 0, sizeof(metrics));
    IDWriteFontFace_GetMetrics(fontface, &metrics);

    ok(metrics.designUnitsPerEm != 0, "designUnitsPerEm %u\n", metrics.designUnitsPerEm);
    ok(metrics.ascent != 0, "ascent %u\n", metrics.ascent);
    ok(metrics.descent != 0, "descent %u\n", metrics.descent);
    ok(metrics.lineGap == 0, "lineGap %d\n", metrics.lineGap);
    ok(metrics.capHeight, "capHeight %u\n", metrics.capHeight);
    ok(metrics.xHeight != 0, "xHeight %u\n", metrics.xHeight);
    ok(metrics.underlinePosition < 0, "underlinePosition %d\n", metrics.underlinePosition);
    ok(metrics.underlineThickness != 0, "underlineThickness %u\n", metrics.underlineThickness);
    ok(metrics.strikethroughPosition > 0, "strikethroughPosition %d\n", metrics.strikethroughPosition);
    ok(metrics.strikethroughThickness != 0, "strikethroughThickness %u\n", metrics.strikethroughThickness);

    hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont1, (void**)&font1);
    if (hr == S_OK) {
        DWRITE_FONT_METRICS1 metrics1;
        IDWriteFontFace1 *fontface1;

        memset(&metrics1, 0, sizeof(metrics1));
        IDWriteFont1_GetMetrics(font1, &metrics1);

        ok(metrics1.designUnitsPerEm != 0, "designUnitsPerEm %u\n", metrics1.designUnitsPerEm);
        ok(metrics1.ascent != 0, "ascent %u\n", metrics1.ascent);
        ok(metrics1.descent != 0, "descent %u\n", metrics1.descent);
        ok(metrics1.lineGap == 0, "lineGap %d\n", metrics1.lineGap);
        ok(metrics1.capHeight, "capHeight %u\n", metrics1.capHeight);
        ok(metrics1.xHeight != 0, "xHeight %u\n", metrics1.xHeight);
        ok(metrics1.underlinePosition < 0, "underlinePosition %d\n", metrics1.underlinePosition);
        ok(metrics1.underlineThickness != 0, "underlineThickness %u\n", metrics1.underlineThickness);
        ok(metrics1.strikethroughPosition > 0, "strikethroughPosition %d\n", metrics1.strikethroughPosition);
        ok(metrics1.strikethroughThickness != 0, "strikethroughThickness %u\n", metrics1.strikethroughThickness);
        ok(metrics1.glyphBoxLeft < 0, "glyphBoxLeft %d\n", metrics1.glyphBoxLeft);
        ok(metrics1.glyphBoxTop > 0, "glyphBoxTop %d\n", metrics1.glyphBoxTop);
        ok(metrics1.glyphBoxRight > 0, "glyphBoxRight %d\n", metrics1.glyphBoxRight);
        ok(metrics1.glyphBoxBottom < 0, "glyphBoxBottom %d\n", metrics1.glyphBoxBottom);
        ok(metrics1.subscriptPositionY < 0, "subscriptPositionY %d\n", metrics1.subscriptPositionY);
        ok(metrics1.subscriptSizeX > 0, "subscriptSizeX %d\n", metrics1.subscriptSizeX);
        ok(metrics1.subscriptSizeY > 0, "subscriptSizeY %d\n", metrics1.subscriptSizeY);
        ok(metrics1.superscriptPositionY > 0, "superscriptPositionY %d\n", metrics1.superscriptPositionY);
        ok(metrics1.superscriptSizeX > 0, "superscriptSizeX %d\n", metrics1.superscriptSizeX);
        ok(metrics1.superscriptSizeY > 0, "superscriptSizeY %d\n", metrics1.superscriptSizeY);
        ok(!metrics1.hasTypographicMetrics, "hasTypographicMetrics %d\n", metrics1.hasTypographicMetrics);

        hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        memset(&metrics1, 0, sizeof(metrics1));
        IDWriteFontFace1_GetMetrics(fontface1, &metrics1);

        ok(metrics1.designUnitsPerEm != 0, "designUnitsPerEm %u\n", metrics1.designUnitsPerEm);
        ok(metrics1.ascent != 0, "ascent %u\n", metrics1.ascent);
        ok(metrics1.descent != 0, "descent %u\n", metrics1.descent);
        ok(metrics1.lineGap == 0, "lineGap %d\n", metrics1.lineGap);
        ok(metrics1.capHeight, "capHeight %u\n", metrics1.capHeight);
        ok(metrics1.xHeight != 0, "xHeight %u\n", metrics1.xHeight);
        ok(metrics1.underlinePosition < 0, "underlinePosition %d\n", metrics1.underlinePosition);
        ok(metrics1.underlineThickness != 0, "underlineThickness %u\n", metrics1.underlineThickness);
        ok(metrics1.strikethroughPosition > 0, "strikethroughPosition %d\n", metrics1.strikethroughPosition);
        ok(metrics1.strikethroughThickness != 0, "strikethroughThickness %u\n", metrics1.strikethroughThickness);
        ok(metrics1.glyphBoxLeft < 0, "glyphBoxLeft %d\n", metrics1.glyphBoxLeft);
        ok(metrics1.glyphBoxTop > 0, "glyphBoxTop %d\n", metrics1.glyphBoxTop);
        ok(metrics1.glyphBoxRight > 0, "glyphBoxRight %d\n", metrics1.glyphBoxRight);
        ok(metrics1.glyphBoxBottom < 0, "glyphBoxBottom %d\n", metrics1.glyphBoxBottom);
        ok(metrics1.subscriptPositionY < 0, "subscriptPositionY %d\n", metrics1.subscriptPositionY);
        ok(metrics1.subscriptSizeX > 0, "subscriptSizeX %d\n", metrics1.subscriptSizeX);
        ok(metrics1.subscriptSizeY > 0, "subscriptSizeY %d\n", metrics1.subscriptSizeY);
        ok(metrics1.superscriptPositionY > 0, "superscriptPositionY %d\n", metrics1.superscriptPositionY);
        ok(metrics1.superscriptSizeX > 0, "superscriptSizeX %d\n", metrics1.superscriptSizeX);
        ok(metrics1.superscriptSizeY > 0, "superscriptSizeY %d\n", metrics1.superscriptSizeY);
        ok(!metrics1.hasTypographicMetrics, "hasTypographicMetrics %d\n", metrics1.hasTypographicMetrics);

        IDWriteFontFace1_Release(fontface1);
        IDWriteFont1_Release(font1);
    }
    else
        win_skip("DWRITE_FONT_METRICS1 is not supported.\n");

    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);

    /* bold simulation affects returned font metrics */
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_NORMAL);

    /* create regulat Tahoma with bold simulation */
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    count = 1;
    hr = IDWriteFontFace_GetFiles(fontface, &count, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFace_GetMetrics(fontface, &metrics);
    ok(IDWriteFontFace_GetSimulations(fontface) == 0, "wrong simulations flags\n");
    IDWriteFontFace_Release(fontface);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file,
        0, DWRITE_FONT_SIMULATIONS_BOLD, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFace_GetMetrics(fontface, &metrics2);
    ok(IDWriteFontFace_GetSimulations(fontface) == DWRITE_FONT_SIMULATIONS_BOLD, "wrong simulations flags\n");

    ok(metrics.ascent == metrics2.ascent, "got %u, %u\n", metrics2.ascent, metrics.ascent);
    ok(metrics.descent == metrics2.descent, "got %u, %u\n", metrics2.descent, metrics.descent);
    ok(metrics.lineGap == metrics2.lineGap, "got %d, %d\n", metrics2.lineGap, metrics.lineGap);
    ok(metrics.capHeight == metrics2.capHeight, "got %u, %u\n", metrics2.capHeight, metrics.capHeight);
    ok(metrics.xHeight == metrics2.xHeight, "got %u, %u\n", metrics2.xHeight, metrics.xHeight);
    ok(metrics.underlinePosition == metrics2.underlinePosition, "got %d, %d\n", metrics2.underlinePosition,
        metrics.underlinePosition);
    ok(metrics.underlineThickness == metrics2.underlineThickness, "got %u, %u\n", metrics2.underlineThickness,
        metrics.underlineThickness);
    ok(metrics.strikethroughPosition == metrics2.strikethroughPosition, "got %d, %d\n", metrics2.strikethroughPosition,
        metrics.strikethroughPosition);
    ok(metrics.strikethroughThickness == metrics2.strikethroughThickness, "got %u, %u\n", metrics2.strikethroughThickness,
        metrics.strikethroughThickness);

    IDWriteFontFile_Release(file);
    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font);

    /* test metrics for whole system collection */
    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        DWRITE_FONT_METRICS1 expected_metrics, metrics1;
        IDWriteLocalizedStrings *names;
        IDWriteFontFace1 *fontface1;
        IDWriteFontFamily *family;
        IDWriteFont *font;
        WCHAR nameW[256];

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFont_CreateFontFace(font, &fontface);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        fontface1 = NULL;
        IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, nameW, sizeof(nameW)/sizeof(nameW[0]));

        IDWriteLocalizedStrings_Release(names);
        IDWriteFont_Release(font);

        get_expected_font_metrics(fontface, &expected_metrics);
        if (fontface1) {
            IDWriteFontFace1_GetMetrics(fontface1, &metrics1);
            check_font_metrics(nameW, TRUE, (const DWRITE_FONT_METRICS*)&metrics1, &expected_metrics);
        }
        else {
            IDWriteFontFace_GetMetrics(fontface, &metrics);
            check_font_metrics(nameW, FALSE, &metrics, &expected_metrics);
        }

        if (fontface1)
            IDWriteFontFace1_Release(fontface1);
        IDWriteFontFace_Release(fontface);
        IDWriteFontFamily_Release(family);
    }
    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_system_fontcollection(void)
{
    IDWriteFontCollection *collection, *coll2;
    IDWriteLocalFontFileLoader *localloader;
    IDWriteFontCollection1 *collection1;
    IDWriteFactory *factory, *factory2;
    IDWriteFontFileLoader *loader;
    IDWriteFontFamily *family;
    IDWriteFontFace *fontface;
    IDWriteFontFile *file;
    IDWriteFont *font;
    HRESULT hr;
    ULONG ref;
    UINT32 i;
    BOOL ret;

    factory = create_factory();

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &coll2, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(coll2 == collection, "got %p, was %p\n", coll2, collection);
    IDWriteFontCollection_Release(coll2);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &coll2, TRUE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(coll2 == collection, "got %p, was %p\n", coll2, collection);
    IDWriteFontCollection_Release(coll2);

    factory2 = create_factory();
    hr = IDWriteFactory_GetSystemFontCollection(factory2, &coll2, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(coll2 != collection, "got %p, was %p\n", coll2, collection);
    IDWriteFontCollection_Release(coll2);
    IDWriteFactory_Release(factory2);

    i = IDWriteFontCollection_GetFontFamilyCount(collection);
    ok(i, "got %u\n", i);

    /* invalid index */
    family = (void*)0xdeadbeef;
    hr = IDWriteFontCollection_GetFontFamily(collection, i, &family);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(family == NULL, "got %p\n", family);

    ret = FALSE;
    i = (UINT32)-1;
    hr = IDWriteFontCollection_FindFamilyName(collection, tahomaW, &i, &ret);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(ret, "got %d\n", ret);
    ok(i != (UINT32)-1, "got %u\n", i);

    ret = FALSE;
    i = (UINT32)-1;
    hr = IDWriteFontCollection_FindFamilyName(collection, tahomaUppercaseW, &i, &ret);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(ret, "got %d\n", ret);
    ok(i != (UINT32)-1, "got %u\n", i);

    ret = FALSE;
    i = (UINT32)-1;
    hr = IDWriteFontCollection_FindFamilyName(collection, tahomaStrangecaseW, &i, &ret);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(ret, "got %d\n", ret);
    ok(i != (UINT32)-1, "got %u\n", i);

    /* get back local file loader */
    hr = IDWriteFontCollection_GetFontFamily(collection, i, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFamily_Release(family);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    i = 1;
    file = NULL;
    hr = IDWriteFontFace_GetFiles(fontface, &i, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(file != NULL, "got %p\n", file);
    IDWriteFontFace_Release(fontface);

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void**)&localloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteLocalFontFileLoader_Release(localloader);

    /* local loader is not registered by default */
    hr = IDWriteFactory_RegisterFontFileLoader(factory, loader);
    ok(hr == S_OK || broken(hr == DWRITE_E_ALREADYREGISTERED), "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory, loader);
    ok(hr == S_OK || broken(hr == E_INVALIDARG), "got 0x%08x\n", hr);

    /* try with a different factory */
    factory2 = create_factory();
    hr = IDWriteFactory_RegisterFontFileLoader(factory2, loader);
    ok(hr == S_OK || broken(hr == DWRITE_E_ALREADYREGISTERED), "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontFileLoader(factory2, loader);
    ok(hr == DWRITE_E_ALREADYREGISTERED, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory2, loader);
    ok(hr == S_OK || broken(hr == E_INVALIDARG), "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory2, loader);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    IDWriteFactory_Release(factory2);

    IDWriteFontFileLoader_Release(loader);

    ret = TRUE;
    i = 0;
    hr = IDWriteFontCollection_FindFamilyName(collection, blahW, &i, &ret);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!ret, "got %d\n", ret);
    ok(i == (UINT32)-1, "got %u\n", i);

    hr = IDWriteFontCollection_QueryInterface(collection, &IID_IDWriteFontCollection1, (void**)&collection1);
    if (hr == S_OK) {
        IDWriteFontSet *fontset, *fontset2;
        IDWriteFontFamily1 *family1;
        IDWriteFactory3 *factory3;

        hr = IDWriteFontCollection1_QueryInterface(collection1, &IID_IDWriteFontCollection, (void**)&coll2);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(coll2 == collection, "got %p, %p\n", collection, coll2);
        IDWriteFontCollection_Release(coll2);

        family1 = (void*)0xdeadbeef;
        hr = IDWriteFontCollection1_GetFontFamily(collection1, ~0u, &family1);
        ok(hr == E_FAIL, "got 0x%08x\n", hr);
        ok(family1 == NULL, "got %p\n", family1);

        hr = IDWriteFontCollection1_GetFontFamily(collection1, 0, &family1);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFontFamily1_Release(family1);

        /* system fontset */
        EXPECT_REF(collection1, 2);
        EXPECT_REF(factory, 2);
        hr = IDWriteFontCollection1_GetFontSet(collection1, &fontset);
    todo_wine
        ok(hr == S_OK, "Failed to get fontset, hr %#x.\n", hr);
    if (hr == S_OK) {
        EXPECT_REF(collection1, 2);
        EXPECT_REF(factory, 2);
        EXPECT_REF(fontset, 1);

        hr = IDWriteFontCollection1_GetFontSet(collection1, &fontset2);
        ok(hr == S_OK, "Failed to get fontset, hr %#x.\n", hr);
        ok(fontset != fontset2, "Expected new fontset instance.\n");
        EXPECT_REF(fontset2, 1);
        IDWriteFontSet_Release(fontset2);

        hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void **)&factory3);
        ok(hr == S_OK, "Failed to get IDWriteFactory3 interface, hr %#x.\n", hr);

        EXPECT_REF(factory, 3);
        hr = IDWriteFactory3_GetSystemFontSet(factory3, &fontset2);
        ok(hr == S_OK, "Failed to get system font set, hr %#x.\n", hr);
        ok(fontset != fontset2, "Expected new fontset instance.\n");
        EXPECT_REF(fontset2, 1);
        EXPECT_REF(factory, 4);

        IDWriteFontSet_Release(fontset2);
        IDWriteFontSet_Release(fontset);

        IDWriteFactory3_Release(factory3);
    }
        IDWriteFontCollection1_Release(collection1);
    }
    else
        win_skip("IDWriteFontCollection1 is not supported.\n");

    ref = IDWriteFontCollection_Release(collection);
    ok(ref == 0, "collection not released, %u\n", ref);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void get_logfont_from_font(IDWriteFont *font, LOGFONTW *logfont)
{
    void *os2_context, *head_context;
    IDWriteLocalizedStrings *names;
    DWRITE_FONT_SIMULATIONS sim;
    IDWriteFontFace *fontface;
    const TT_OS2_V2 *tt_os2;
    DWRITE_FONT_STYLE style;
    const TT_HEAD *tt_head;
    LONG weight;
    UINT32 size;
    BOOL exists;
    HRESULT hr;

    /* These are rendering time properties. */
    logfont->lfHeight = 0;
    logfont->lfWidth = 0;
    logfont->lfEscapement = 0;
    logfont->lfOrientation = 0;
    logfont->lfUnderline = 0;
    logfont->lfStrikeOut = 0;

    logfont->lfWeight = 0;
    logfont->lfItalic = 0;

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "Failed to create font face, %#x\n", hr);

    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_0S2_TAG, (const void **)&tt_os2, &size,
        &os2_context, &exists);
    ok(hr == S_OK, "Failed to get OS/2 table, %#x\n", hr);

    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_HEAD_TAG, (const void **)&tt_head, &size,
        &head_context, &exists);
    ok(hr == S_OK, "Failed to get head table, %#x\n", hr);

    sim = IDWriteFont_GetSimulations(font);

    /* lfWeight */
    weight = FW_REGULAR;
    if (tt_os2) {
        USHORT usWeightClass = GET_BE_WORD(tt_os2->usWeightClass);

        if (usWeightClass >= 1 && usWeightClass <= 9)
            usWeightClass *= 100;

        if (usWeightClass > DWRITE_FONT_WEIGHT_ULTRA_BLACK)
            weight = DWRITE_FONT_WEIGHT_ULTRA_BLACK;
        else if (usWeightClass > 0)
            weight = usWeightClass;
    }
    else if (tt_head) {
        USHORT macStyle = GET_BE_WORD(tt_head->macStyle);
        if (macStyle & TT_HEAD_MACSTYLE_BOLD)
            weight = DWRITE_FONT_WEIGHT_BOLD;
    }
    if (sim & DWRITE_FONT_SIMULATIONS_BOLD)
        weight += (FW_BOLD - FW_REGULAR) / 2 + 1;
    logfont->lfWeight = weight;

    /* lfItalic */
    if (IDWriteFont_GetSimulations(font) & DWRITE_FONT_SIMULATIONS_OBLIQUE)
        logfont->lfItalic = 1;

    style = IDWriteFont_GetStyle(font);
    if (!logfont->lfItalic && ((style == DWRITE_FONT_STYLE_ITALIC) || (style == DWRITE_FONT_STYLE_OBLIQUE))) {
        if (tt_os2) {
            USHORT fsSelection = GET_BE_WORD(tt_os2->fsSelection);
            logfont->lfItalic = !!(fsSelection & OS2_FSSELECTION_ITALIC);
        }
        else if (tt_head) {
            USHORT macStyle = GET_BE_WORD(tt_head->macStyle);
            logfont->lfItalic = !!(macStyle & TT_HEAD_MACSTYLE_ITALIC);
        }
    }

    /* lfFaceName */
    exists = FALSE;
    logfont->lfFaceName[0] = 0;
    hr = IDWriteFont_GetInformationalStrings(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &names, &exists);
    if (SUCCEEDED(hr)) {
        if (exists) {
            static const WCHAR enusW[] = {'e','n','-','u','s',0};
            WCHAR localeW[LOCALE_NAME_MAX_LENGTH];
            UINT32 index;

            /* Fallback to en-us if there's no string for user locale. */
            exists = FALSE;
            if (GetSystemDefaultLocaleName(localeW, sizeof(localeW)/sizeof(WCHAR)))
                IDWriteLocalizedStrings_FindLocaleName(names, localeW, &index, &exists);

            if (!exists)
                IDWriteLocalizedStrings_FindLocaleName(names, enusW, &index, &exists);

            if (exists)
                IDWriteLocalizedStrings_GetString(names, index, logfont->lfFaceName, sizeof(logfont->lfFaceName)/sizeof(WCHAR));
        }

        IDWriteLocalizedStrings_Release(names);
    }

    if (tt_os2)
        IDWriteFontFace_ReleaseFontTable(fontface, os2_context);
    if (tt_head)
        IDWriteFontFace_ReleaseFontTable(fontface, head_context);
    IDWriteFontFace_Release(fontface);
}

static void test_ConvertFontFaceToLOGFONT(void)
{
    IDWriteFontCollection *collection;
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    LOGFONTW logfont;
    UINT32 count, i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

if (0) /* crashes on native */
{
    hr = IDWriteGdiInterop_ConvertFontFaceToLOGFONT(interop, NULL, NULL);
    hr = IDWriteGdiInterop_ConvertFontFaceToLOGFONT(interop, fontface, NULL);
}
    memset(&logfont, 0xcc, sizeof(logfont));
    hr = IDWriteGdiInterop_ConvertFontFaceToLOGFONT(interop, NULL, &logfont);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(logfont.lfFaceName[0] == 0, "got face name %s\n", wine_dbgstr_w(logfont.lfFaceName));

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    count = IDWriteFontCollection_GetFontFamilyCount(collection);
    for (i = 0; i < count; i++) {
        WCHAR nameW[128], familynameW[64], facenameW[64];
        IDWriteLocalizedStrings *names;
        DWRITE_FONT_SIMULATIONS sim;
        IDWriteFontFamily *family;
        UINT32 font_count, j;
        IDWriteFont *font;
        LOGFONTW lf;

        hr = IDWriteFontCollection_GetFontFamily(collection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, familynameW, sizeof(familynameW)/sizeof(familynameW[0]));
        IDWriteLocalizedStrings_Release(names);

        font_count = IDWriteFontFamily_GetFontCount(family);

        for (j = 0; j < font_count; j++) {
            static const WCHAR spaceW[] = {' ', 0};
            IDWriteFontFace *fontface;

            hr = IDWriteFontFamily_GetFont(family, j, &font);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            hr = IDWriteFont_GetFaceNames(font, &names);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            get_enus_string(names, facenameW, sizeof(facenameW)/sizeof(facenameW[0]));
            IDWriteLocalizedStrings_Release(names);

            lstrcpyW(nameW, familynameW);
            lstrcatW(nameW, spaceW);
            lstrcatW(nameW, facenameW);

            hr = IDWriteFont_CreateFontFace(font, &fontface);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            memset(&logfont, 0xcc, sizeof(logfont));
            hr = IDWriteGdiInterop_ConvertFontFaceToLOGFONT(interop, fontface, &logfont);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            sim = IDWriteFontFace_GetSimulations(fontface);
            get_logfont_from_font(font, &lf);

            ok(logfont.lfWeight == lf.lfWeight, "%s: unexpected lfWeight %d, expected lfWeight %d, font weight %d, "
                "bold simulation %s\n", wine_dbgstr_w(nameW), logfont.lfWeight, lf.lfWeight, IDWriteFont_GetWeight(font),
                sim & DWRITE_FONT_SIMULATIONS_BOLD ? "yes" : "no");
            ok(logfont.lfItalic == lf.lfItalic, "%s: unexpected italic flag %d, oblique simulation %s\n",
                wine_dbgstr_w(nameW), logfont.lfItalic, sim & DWRITE_FONT_SIMULATIONS_OBLIQUE ? "yes" : "no");
            ok(!lstrcmpW(logfont.lfFaceName, lf.lfFaceName), "%s: unexpected facename %s, expected %s\n",
                wine_dbgstr_w(nameW), wine_dbgstr_w(logfont.lfFaceName), wine_dbgstr_w(lf.lfFaceName));

            ok(logfont.lfOutPrecision == OUT_OUTLINE_PRECIS, "%s: unexpected output precision %d\n", wine_dbgstr_w(nameW),
                logfont.lfOutPrecision);
            ok(logfont.lfClipPrecision == CLIP_DEFAULT_PRECIS, "%s: unexpected clipping precision %d\n", wine_dbgstr_w(nameW),
                logfont.lfClipPrecision);
            ok(logfont.lfQuality == DEFAULT_QUALITY, "%s: unexpected quality %d\n", wine_dbgstr_w(nameW), logfont.lfQuality);
            ok(logfont.lfPitchAndFamily == DEFAULT_PITCH, "%s: unexpected pitch %d\n", wine_dbgstr_w(nameW),
                logfont.lfPitchAndFamily);

            IDWriteFontFace_Release(fontface);
            IDWriteFont_Release(font);
        }

        IDWriteFontFamily_Release(family);
    }

    IDWriteFontCollection_Release(collection);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static HRESULT WINAPI fontfileenumerator_QueryInterface(IDWriteFontFileEnumerator *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileEnumerator))
    {
        *obj = iface;
        IDWriteFontFileEnumerator_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI fontfileenumerator_AddRef(IDWriteFontFileEnumerator *iface)
{
    return 2;
}

static ULONG WINAPI fontfileenumerator_Release(IDWriteFontFileEnumerator *iface)
{
    return 1;
}

static HRESULT WINAPI fontfileenumerator_GetCurrentFontFile(IDWriteFontFileEnumerator *iface, IDWriteFontFile **file)
{
    *file = NULL;
    return E_FAIL;
}

static HRESULT WINAPI fontfileenumerator_MoveNext(IDWriteFontFileEnumerator *iface, BOOL *current)
{
    *current = FALSE;
    return S_OK;
}

static const struct IDWriteFontFileEnumeratorVtbl dwritefontfileenumeratorvtbl =
{
    fontfileenumerator_QueryInterface,
    fontfileenumerator_AddRef,
    fontfileenumerator_Release,
    fontfileenumerator_MoveNext,
    fontfileenumerator_GetCurrentFontFile,
};

struct collection_loader
{
    IDWriteFontCollectionLoader IDWriteFontCollectionLoader_iface;
    LONG ref;
};

static inline struct collection_loader *impl_from_IDWriteFontCollectionLoader(IDWriteFontCollectionLoader *iface)
{
    return CONTAINING_RECORD(iface, struct collection_loader, IDWriteFontCollectionLoader_iface);
}

static HRESULT WINAPI fontcollectionloader_QueryInterface(IDWriteFontCollectionLoader *iface, REFIID riid, void **obj)
{
    struct collection_loader *loader = impl_from_IDWriteFontCollectionLoader(iface);

    if (IsEqualIID(&IID_IDWriteFontCollectionLoader, riid) ||
            IsEqualIID(&IID_IUnknown, riid))
    {
        *obj = &loader->IDWriteFontCollectionLoader_iface;
        IDWriteFontCollectionLoader_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontcollectionloader_AddRef(IDWriteFontCollectionLoader *iface)
{
    struct collection_loader *loader = impl_from_IDWriteFontCollectionLoader(iface);
    return InterlockedIncrement(&loader->ref);
}

static ULONG WINAPI fontcollectionloader_Release(IDWriteFontCollectionLoader *iface)
{
    struct collection_loader *loader = impl_from_IDWriteFontCollectionLoader(iface);
    ULONG ref = InterlockedDecrement(&loader->ref);

    if (!ref)
        heap_free(loader);

    return ref;
}

static HRESULT WINAPI fontcollectionloader_CreateEnumeratorFromKey(IDWriteFontCollectionLoader *iface, IDWriteFactory *factory, const void *key,
    UINT32 key_size, IDWriteFontFileEnumerator **ret)
{
    static IDWriteFontFileEnumerator enumerator = { &dwritefontfileenumeratorvtbl };
    *ret = &enumerator;
    return S_OK;
}

static const struct IDWriteFontCollectionLoaderVtbl dwritefontcollectionloadervtbl = {
    fontcollectionloader_QueryInterface,
    fontcollectionloader_AddRef,
    fontcollectionloader_Release,
    fontcollectionloader_CreateEnumeratorFromKey
};

static IDWriteFontCollectionLoader *create_collection_loader(void)
{
    struct collection_loader *loader = heap_alloc(sizeof(*loader));

    loader->IDWriteFontCollectionLoader_iface.lpVtbl = &dwritefontcollectionloadervtbl;
    loader->ref = 1;

    return &loader->IDWriteFontCollectionLoader_iface;
}

static void test_CustomFontCollection(void)
{
    static const WCHAR fontnameW[] = {'w','i','n','e','_','t','e','s','t',0};
    IDWriteFontCollectionLoader *loader, *loader2, *loader3;
    IDWriteFontCollection *font_collection = NULL;
    static IDWriteFontFileLoader rloader = { &resourcefontfileloadervtbl };
    struct test_fontcollectionloader resource_collection = { { &resourcecollectionloadervtbl }, &rloader };
    IDWriteFontFamily *family, *family2, *family3;
    IDWriteFontFace *idfontface, *idfontface2;
    IDWriteFontFile *fontfile, *fontfile2;
    IDWriteLocalizedStrings *string;
    IDWriteFont *idfont, *idfont2;
    IDWriteFactory *factory;
    UINT32 index, count;
    BOOL exists;
    HRESULT hr;
    HRSRC font;
    ULONG ref;

    factory = create_factory();

    loader = create_collection_loader();
    loader2 = create_collection_loader();
    loader3 = create_collection_loader();

    hr = IDWriteFactory_RegisterFontCollectionLoader(factory, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFactory_UnregisterFontCollectionLoader(factory, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    EXPECT_REF(loader, 1);
    EXPECT_REF(loader2, 1);

    hr = IDWriteFactory_RegisterFontCollectionLoader(factory, loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontCollectionLoader(factory, loader2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontCollectionLoader(factory, loader);
    ok(hr == DWRITE_E_ALREADYREGISTERED, "got 0x%08x\n", hr);

    EXPECT_REF(loader, 2);
    EXPECT_REF(loader2, 2);

    hr = IDWriteFactory_RegisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontCollectionLoader(factory, &resource_collection.IDWriteFontFileCollectionLoader_iface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* Loader wasn't registered. */
    font_collection = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateCustomFontCollection(factory, loader3, "Billy", 6, &font_collection);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(font_collection == NULL, "got %p\n", font_collection);

    EXPECT_REF(factory, 1);
    hr = IDWriteFactory_CreateCustomFontCollection(factory, loader, "Billy", 6, &font_collection);
    ok(hr == S_OK, "got 0x%08x\n", hr);
todo_wine
    EXPECT_REF(factory, 1);
    EXPECT_REF(loader, 2);
    IDWriteFontCollection_Release(font_collection);

    hr = IDWriteFactory_CreateCustomFontCollection(factory, loader2, "Billy", 6, &font_collection);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontCollection_Release(font_collection);

    font_collection = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateCustomFontCollection(factory, (IDWriteFontCollectionLoader*)0xdeadbeef, "Billy", 6, &font_collection);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(font_collection == NULL, "got %p\n", font_collection);

    font = FindResourceA(GetModuleHandleA(NULL), (LPCSTR)MAKEINTRESOURCE(1), (LPCSTR)RT_RCDATA);
    ok(font != NULL, "Failed to find font resource\n");

    hr = IDWriteFactory_CreateCustomFontCollection(factory, &resource_collection.IDWriteFontFileCollectionLoader_iface,
        &font, sizeof(HRSRC), &font_collection);
    ok(hr == S_OK, "got 0x%08x\n",hr);
    EXPECT_REF(font_collection, 1);

    index = 1;
    exists = FALSE;
    hr = IDWriteFontCollection_FindFamilyName(font_collection, fontnameW, &index, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(index == 0, "got index %i\n", index);
    ok(exists, "got exists %i\n", exists);

    count = IDWriteFontCollection_GetFontFamilyCount(font_collection);
    ok(count == 1, "got %u\n", count);

    family = NULL;
    hr = IDWriteFontCollection_GetFontFamily(font_collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(family, 1);

    family2 = NULL;
    hr = IDWriteFontCollection_GetFontFamily(font_collection, 0, &family2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(family2, 1);
    ok(family != family2, "got %p, %p\n", family, family2);

    hr = IDWriteFontFamily_GetFont(family, 0, &idfont);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(idfont, 1);
    EXPECT_REF(family, 2);
    hr = IDWriteFontFamily_GetFont(family, 0, &idfont2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(idfont2, 1);
    EXPECT_REF(family, 3);
    ok(idfont != idfont2, "got %p, %p\n", idfont, idfont2);
    IDWriteFont_Release(idfont2);

    hr = IDWriteFont_GetInformationalStrings(idfont, DWRITE_INFORMATIONAL_STRING_COPYRIGHT_NOTICE, &string, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists, "got %d\n", exists);
    EXPECT_REF(string, 1);
    IDWriteLocalizedStrings_Release(string);

    family3 = NULL;
    hr = IDWriteFont_GetFontFamily(idfont, &family3);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(family, 3);
    ok(family == family3, "got %p, %p\n", family, family3);
    IDWriteFontFamily_Release(family3);

    idfontface = NULL;
    hr = IDWriteFont_CreateFontFace(idfont, &idfontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(idfont, 1);

    idfont2 = NULL;
    hr = IDWriteFontFamily_GetFont(family2, 0, &idfont2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(idfont2, 1);
    EXPECT_REF(idfont, 1);
    ok(idfont2 != idfont, "Font instances should not match\n");

    idfontface2 = NULL;
    hr = IDWriteFont_CreateFontFace(idfont2, &idfontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(idfontface2 == idfontface, "fontfaces should match\n");

    index = 1;
    fontfile = NULL;
    hr = IDWriteFontFace_GetFiles(idfontface, &index, &fontfile);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    index = 1;
    fontfile2 = NULL;
    hr = IDWriteFontFace_GetFiles(idfontface2, &index, &fontfile2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontfile == fontfile2, "fontfiles should match\n");

    IDWriteFont_Release(idfont);
    IDWriteFont_Release(idfont2);
    IDWriteFontFile_Release(fontfile);
    IDWriteFontFile_Release(fontfile2);
    IDWriteFontFace_Release(idfontface);
    IDWriteFontFace_Release(idfontface2);
    IDWriteFontFamily_Release(family2);
    IDWriteFontFamily_Release(family);
    IDWriteFontCollection_Release(font_collection);

    hr = IDWriteFactory_UnregisterFontCollectionLoader(factory, loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontCollectionLoader(factory, loader);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontCollectionLoader(factory, loader2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontCollectionLoader(factory, &resource_collection.IDWriteFontFileCollectionLoader_iface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontCollectionLoader_Release(loader);
    IDWriteFontCollectionLoader_Release(loader2);
    IDWriteFontCollectionLoader_Release(loader3);

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static HRESULT WINAPI fontfileloader_QueryInterface(IDWriteFontFileLoader *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDWriteFontFileLoader))
    {
        *obj = iface;
        IDWriteFontFileLoader_AddRef(iface);
        return S_OK;
    }

    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI fontfileloader_AddRef(IDWriteFontFileLoader *iface)
{
    return 2;
}

static ULONG WINAPI fontfileloader_Release(IDWriteFontFileLoader *iface)
{
    return 1;
}

static HRESULT WINAPI fontfileloader_CreateStreamFromKey(IDWriteFontFileLoader *iface, const void *ref_key, UINT32 key_size,
    IDWriteFontFileStream **stream)
{
    return 0x8faecafe;
}

static const struct IDWriteFontFileLoaderVtbl dwritefontfileloadervtbl = {
    fontfileloader_QueryInterface,
    fontfileloader_AddRef,
    fontfileloader_Release,
    fontfileloader_CreateStreamFromKey
};

static void test_CreateCustomFontFileReference(void)
{
    IDWriteFontFileLoader floader = { &dwritefontfileloadervtbl };
    IDWriteFontFileLoader floader2 = { &dwritefontfileloadervtbl };
    IDWriteFontFileLoader floader3 = { &dwritefontfileloadervtbl };
    IDWriteFactory *factory, *factory2;
    IDWriteFontFileLoader *loader;
    IDWriteFontFile *file, *file2;
    BOOL support;
    DWRITE_FONT_FILE_TYPE file_type;
    DWRITE_FONT_FACE_TYPE face_type;
    UINT32 count;
    IDWriteFontFace *face, *face2;
    HRESULT hr;
    HRSRC fontrsrc;
    UINT32 codePoints[1] = {0xa8};
    UINT16 indices[2];
    const void *key;
    UINT32 key_size;
    WCHAR *path;
    ULONG ref;

    path = create_testfontfile(test_fontfile);

    factory = create_factory();
    factory2 = create_factory();

if (0) { /* crashes on win10 */
    hr = IDWriteFactory_RegisterFontFileLoader(factory, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
}
    /* local loader is accepted too */
    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateCustomFontFileReference(factory, key, key_size, loader, &file2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFile_Release(file2);
    IDWriteFontFile_Release(file);
    IDWriteFontFileLoader_Release(loader);

    hr = IDWriteFactory_RegisterFontFileLoader(factory, &floader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontFileLoader(factory, &floader2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontFileLoader(factory, &floader);
    ok(hr == DWRITE_E_ALREADYREGISTERED, "got 0x%08x\n", hr);
    hr = IDWriteFactory_RegisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    file = NULL;
    hr = IDWriteFactory_CreateCustomFontFileReference(factory, "test", 4, &floader, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    file = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateCustomFontFileReference(factory, "test", 4, &floader3, &file);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(file == NULL, "got %p\n", file);

    file = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateCustomFontFileReference(factory, "test", 4, NULL, &file);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(file == NULL, "got %p\n", file);

    file = NULL;
    hr = IDWriteFactory_CreateCustomFontFileReference(factory, "test", 4, &floader, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    file_type = DWRITE_FONT_FILE_TYPE_TRUETYPE;
    face_type = DWRITE_FONT_FACE_TYPE_TRUETYPE;
    support = TRUE;
    count = 1;
    hr = IDWriteFontFile_Analyze(file, &support, &file_type, &face_type, &count);
    ok(hr == 0x8faecafe, "got 0x%08x\n", hr);
    ok(support == FALSE, "got %i\n", support);
    ok(file_type == DWRITE_FONT_FILE_TYPE_UNKNOWN, "got %i\n", file_type);
    ok(face_type == DWRITE_FONT_FACE_TYPE_UNKNOWN, "got %i\n", face_type);
    ok(count == 0, "got %i\n", count);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_CFF, 1, &file, 0, 0, &face);
    ok(hr == 0x8faecafe, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    fontrsrc = FindResourceA(GetModuleHandleA(NULL), (LPCSTR)MAKEINTRESOURCE(1), (LPCSTR)RT_RCDATA);
    ok(fontrsrc != NULL, "Failed to find font resource\n");

    hr = IDWriteFactory_CreateCustomFontFileReference(factory, &fontrsrc, sizeof(HRSRC), &rloader, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    file_type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    face_type = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    support = FALSE;
    count = 0;
    hr = IDWriteFontFile_Analyze(file, &support, &file_type, &face_type, &count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(support == TRUE, "got %i\n", support);
    ok(file_type == DWRITE_FONT_FILE_TYPE_TRUETYPE, "got %i\n", file_type);
    ok(face_type == DWRITE_FONT_FACE_TYPE_TRUETYPE, "got %i\n", face_type);
    ok(count == 1, "got %i\n", count);

    /* invalid index */
    face = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateFontFace(factory, face_type, 1, &file, 1, DWRITE_FONT_SIMULATIONS_NONE, &face);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(face == NULL, "got %p\n", face);

    hr = IDWriteFactory_CreateFontFace(factory, face_type, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, face_type, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    /* fontface instances are reused starting with win7 */
    ok(face == face2 || broken(face != face2), "got %p, %p\n", face, face2);
    IDWriteFontFace_Release(face2);

    /* file was created with different factory */
    face2 = NULL;
    hr = IDWriteFactory_CreateFontFace(factory2, face_type, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face2);
todo_wine
    ok(hr == S_OK, "got 0x%08x\n", hr);
if (face2) {
    IDWriteFontFace_Release(face2);
}
    file2 = NULL;
    hr = IDWriteFactory_CreateCustomFontFileReference(factory, &fontrsrc, sizeof(HRSRC), &rloader, &file2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(file != file2, "got %p, %p\n", file, file2);

    hr = IDWriteFactory_CreateFontFace(factory, face_type, 1, &file2, 0, DWRITE_FONT_SIMULATIONS_NONE, &face2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    /* fontface instances are reused starting with win7 */
    ok(face == face2 || broken(face != face2), "got %p, %p\n", face, face2);
    IDWriteFontFace_Release(face2);
    IDWriteFontFile_Release(file2);

    hr = IDWriteFontFace_GetGlyphIndices(face, NULL, 0, NULL);
    ok(hr == E_INVALIDARG || broken(hr == S_OK) /* win8 */, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_GetGlyphIndices(face, codePoints, 0, NULL);
    ok(hr == E_INVALIDARG || broken(hr == S_OK) /* win8 */, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_GetGlyphIndices(face, codePoints, 0, indices);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_GetGlyphIndices(face, NULL, 0, indices);
    ok(hr == E_INVALIDARG || broken(hr == S_OK) /* win8 */, "got 0x%08x\n", hr);

    indices[0] = indices[1] = 11;
    hr = IDWriteFontFace_GetGlyphIndices(face, NULL, 1, indices);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(indices[0] == 0, "got index %i\n", indices[0]);
    ok(indices[1] == 11, "got index %i\n", indices[1]);

    if (0) /* crashes on native */
        hr = IDWriteFontFace_GetGlyphIndices(face, NULL, 1, NULL);

    hr = IDWriteFontFace_GetGlyphIndices(face, codePoints, 1, indices);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(indices[0] == 7, "Unexpected glyph index, %u.\n", indices[0]);
    IDWriteFontFace_Release(face);
    IDWriteFontFile_Release(file);

    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &floader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &floader);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &floader2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    ref = IDWriteFactory_Release(factory2);
    ok(ref == 0, "factory not released, %u\n", ref);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_CreateFontFileReference(void)
{
    HRESULT hr;
    IDWriteFontFile *ffile = NULL;
    BOOL support;
    DWRITE_FONT_FILE_TYPE type;
    DWRITE_FONT_FACE_TYPE face;
    UINT32 count;
    IDWriteFontFace *fface = NULL;
    IDWriteFactory *factory;
    WCHAR *path;
    ULONG ref;

    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    ffile = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateFontFileReference(factory, NULL, NULL, &ffile);
    ok(hr == E_INVALIDARG, "got 0x%08x\n",hr);
    ok(ffile == NULL, "got %p\n", ffile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &ffile);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    support = FALSE;
    type = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    face = DWRITE_FONT_FACE_TYPE_CFF;
    count = 0;
    hr = IDWriteFontFile_Analyze(ffile, &support, &type, &face, &count);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(support == TRUE, "got %i\n", support);
    ok(type == DWRITE_FONT_FILE_TYPE_TRUETYPE, "got %i\n", type);
    ok(face == DWRITE_FONT_FACE_TYPE_TRUETYPE, "got %i\n", face);
    ok(count == 1, "got %i\n", count);

    hr = IDWriteFactory_CreateFontFace(factory, face, 1, &ffile, 0, DWRITE_FONT_SIMULATIONS_NONE, &fface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFace_Release(fface);
    IDWriteFontFile_Release(ffile);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);

    DELETE_FONTFILE(path);
}

static void test_shared_isolated(void)
{
    IDWriteFactory *isolated, *isolated2;
    IDWriteFactory *shared, *shared2;
    HRESULT hr;
    ULONG ref;

    /* invalid type */
    shared = NULL;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED+1, &IID_IDWriteFactory, (IUnknown**)&shared);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(shared != NULL, "got %p\n", shared);
    IDWriteFactory_Release(shared);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown**)&shared);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown**)&shared2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(shared == shared2, "got %p, and %p\n", shared, shared2);
    IDWriteFactory_Release(shared2);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IUnknown, (IUnknown**)&shared2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(shared == shared2, "got %p, and %p\n", shared, shared2);

    IDWriteFactory_Release(shared);
    IDWriteFactory_Release(shared2);

    /* we got 2 references, released 2 - still same pointer is returned */
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown**)&shared2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(shared == shared2, "got %p, and %p\n", shared, shared2);
    IDWriteFactory_Release(shared2);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown**)&isolated);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown**)&isolated2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(isolated != isolated2, "got %p, and %p\n", isolated, isolated2);
    IDWriteFactory_Release(isolated2);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IUnknown, (IUnknown**)&isolated2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFactory_Release(isolated2);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED+1, &IID_IDWriteFactory, (IUnknown**)&isolated2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(shared != isolated2, "got %p, and %p\n", shared, isolated2);

    ref = IDWriteFactory_Release(isolated);
    ok(ref == 0, "factory not released, %u\n", ref);
    ref = IDWriteFactory_Release(isolated2);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetUnicodeRanges(void)
{
    DWRITE_UNICODE_RANGE *ranges, r;
    IDWriteFontFile *ffile = NULL;
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    UINT32 count;
    HRESULT hr;
    HRSRC font;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_RegisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    font = FindResourceA(GetModuleHandleA(NULL), (LPCSTR)MAKEINTRESOURCE(1), (LPCSTR)RT_RCDATA);
    ok(font != NULL, "Failed to find font resource\n");

    hr = IDWriteFactory_CreateCustomFontFileReference(factory, &font, sizeof(HRSRC), &rloader, &ffile);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &ffile, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(ffile);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    IDWriteFontFace_Release(fontface);
    if (hr != S_OK) {
        win_skip("GetUnicodeRanges() is not supported.\n");
        IDWriteFactory_Release(factory);
        return;
    }

    count = 0;
    hr = IDWriteFontFace1_GetUnicodeRanges(fontface1, 0, NULL, &count);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(count > 0, "got %u\n", count);

    count = 1;
    hr = IDWriteFontFace1_GetUnicodeRanges(fontface1, 1, NULL, &count);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(count == 0, "got %u\n", count);

    count = 0;
    hr = IDWriteFontFace1_GetUnicodeRanges(fontface1, 1, &r, &count);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(count > 1, "got %u\n", count);

    ranges = heap_alloc(count*sizeof(DWRITE_UNICODE_RANGE));
    hr = IDWriteFontFace1_GetUnicodeRanges(fontface1, count, ranges, &count);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    ranges[0].first = ranges[0].last = 0;
    hr = IDWriteFontFace1_GetUnicodeRanges(fontface1, 1, ranges, &count);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(ranges[0].first != 0 && ranges[0].last != 0, "got 0x%x-0x%0x\n", ranges[0].first, ranges[0].last);

    heap_free(ranges);

    hr = IDWriteFactory_UnregisterFontFileLoader(factory, &rloader);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    IDWriteFontFace1_Release(fontface1);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetFontFromFontFace(void)
{
    IDWriteFontFace *fontface, *fontface2;
    IDWriteFontCollection *collection;
    IDWriteFont *font, *font2, *font3;
    IDWriteFontFamily *family;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    font2 = NULL;
    hr = IDWriteFontCollection_GetFontFromFontFace(collection, fontface, &font2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font2 != font, "got %p, %p\n", font2, font);

    font3 = NULL;
    hr = IDWriteFontCollection_GetFontFromFontFace(collection, fontface, &font3);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font3 != font && font3 != font2, "got %p, %p, %p\n", font3, font2, font);

    hr = IDWriteFont_CreateFontFace(font2, &fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface2 == fontface, "got %p, %p\n", fontface2, fontface);
    IDWriteFontFace_Release(fontface2);

    hr = IDWriteFont_CreateFontFace(font3, &fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface2 == fontface, "got %p, %p\n", fontface2, fontface);
    IDWriteFontFace_Release(fontface2);
    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font3);
    IDWriteFactory_Release(factory);

    /* fontface that wasn't created from this collection */
    factory = create_factory();
    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontCollection_GetFontFromFontFace(collection, fontface, &font3);
    ok(hr == DWRITE_E_NOFONT, "got 0x%08x\n", hr);
    ok(font3 == NULL, "got %p\n", font3);
    IDWriteFontFace_Release(fontface);

    IDWriteFont_Release(font);
    IDWriteFont_Release(font2);
    IDWriteFontFamily_Release(family);
    IDWriteFontCollection_Release(collection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_GetFirstMatchingFont(void)
{
    DWRITE_FONT_SIMULATIONS simulations;
    IDWriteFontCollection *collection;
    IDWriteFontFamily *family;
    IDWriteFont *font, *font2;
    IDWriteFactory *factory;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(font != font2, "got %p, %p\n", font, font2);
    IDWriteFont_Release(font);
    IDWriteFont_Release(font2);

    /* out-of-range font props are allowed */
    hr = IDWriteFontFamily_GetFirstMatchingFont(family, 1000, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, 10, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    IDWriteFontFamily_Release(family);

    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_ITALIC);
    simulations = IDWriteFont_GetSimulations(font);
    ok(simulations == DWRITE_FONT_SIMULATIONS_OBLIQUE, "%d\n", simulations);
    IDWriteFont_Release(font);

    IDWriteFontCollection_Release(collection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetMatchingFonts(void)
{
    IDWriteFontCollection *collection;
    IDWriteFontFamily *family;
    IDWriteFactory *factory;
    IDWriteFontList *fontlist, *fontlist2;
    IDWriteFontList1 *fontlist1;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* out-of-range font props are allowed */
    hr = IDWriteFontFamily_GetMatchingFonts(family, 1000, DWRITE_FONT_STRETCH_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, &fontlist);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontList_Release(fontlist);

    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL, 10,
        DWRITE_FONT_STYLE_NORMAL, &fontlist);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontList_Release(fontlist);

    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10, &fontlist);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontList_Release(fontlist);

    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &fontlist);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &fontlist2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontlist != fontlist2, "got %p, %p\n", fontlist, fontlist2);
    IDWriteFontList_Release(fontlist2);

    hr = IDWriteFontList_QueryInterface(fontlist, &IID_IDWriteFontList1, (void**)&fontlist1);
    if (hr == S_OK) {
        IDWriteFontFaceReference *ref, *ref1;
        IDWriteFont3 *font;
        UINT32 count;

        count = IDWriteFontList1_GetFontCount(fontlist1);
        ok(count > 0, "got %u\n", count);

        font = (void*)0xdeadbeef;
        hr = IDWriteFontList1_GetFont(fontlist1, ~0u, &font);
        ok(hr == E_FAIL, "got 0x%08x\n", hr);
        ok(font == NULL, "got %p\n", font);

        font = (void*)0xdeadbeef;
        hr = IDWriteFontList1_GetFont(fontlist1, count, &font);
        ok(hr == E_FAIL, "got 0x%08x\n", hr);
        ok(font == NULL, "got %p\n", font);

        hr = IDWriteFontList1_GetFont(fontlist1, 0, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFont3_Release(font);

        hr = IDWriteFontList1_GetFontFaceReference(fontlist1, 0, &ref);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontList1_GetFontFaceReference(fontlist1, 0, &ref1);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(ref != ref1, "got %p, %p\n", ref, ref1);

        IDWriteFontFaceReference_Release(ref1);
        IDWriteFontFaceReference_Release(ref);
        IDWriteFontList1_Release(fontlist1);
    }
    else
        win_skip("IDWriteFontList1 is not supported.\n");

    IDWriteFontList_Release(fontlist);
    IDWriteFontFamily_Release(family);

    IDWriteFontCollection_Release(collection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetInformationalStrings(void)
{
    IDWriteLocalizedStrings *strings, *strings2;
    IDWriteFontCollection *collection;
    IDWriteFontFamily *family;
    IDWriteFactory *factory;
    IDWriteFont *font;
    BOOL exists;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    exists = TRUE;
    strings = (void*)0xdeadbeef;
    hr = IDWriteFont_GetInformationalStrings(font, DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_CID_NAME+1, &strings, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == FALSE, "got %d\n", exists);
    ok(strings == NULL, "got %p\n", strings);

    exists = TRUE;
    strings = NULL;
    hr = IDWriteFont_GetInformationalStrings(font, DWRITE_INFORMATIONAL_STRING_NONE, &strings, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == FALSE, "got %d\n", exists);

    exists = FALSE;
    strings = NULL;
    hr = IDWriteFont_GetInformationalStrings(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &strings, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == TRUE, "got %d\n", exists);

    /* strings instance is not reused */
    strings2 = NULL;
    hr = IDWriteFont_GetInformationalStrings(font, DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &strings2, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(strings2 != strings, "got %p, %p\n", strings2, strings);

    IDWriteLocalizedStrings_Release(strings);
    IDWriteLocalizedStrings_Release(strings2);
    IDWriteFont_Release(font);
    IDWriteFontFamily_Release(family);
    IDWriteFontCollection_Release(collection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetGdiInterop(void)
{
    IDWriteGdiInterop *interop, *interop2;
    IDWriteFactory *factory, *factory2;
    IDWriteFont *font;
    LOGFONTW logfont;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    interop = NULL;
    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    interop2 = NULL;
    hr = IDWriteFactory_GetGdiInterop(factory, &interop2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(interop == interop2, "got %p, %p\n", interop, interop2);
    IDWriteGdiInterop_Release(interop2);

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, &IID_IDWriteFactory, (IUnknown**)&factory2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* each factory gets its own interop */
    interop2 = NULL;
    hr = IDWriteFactory_GetGdiInterop(factory2, &interop2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(interop != interop2, "got %p, %p\n", interop, interop2);

    /* release factory - interop still works */
    IDWriteFactory_Release(factory2);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop2, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    IDWriteGdiInterop_Release(interop2);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateFontFaceFromHdc(void)
{
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    HFONT hfont, oldhfont;
    LOGFONTW logfont;
    LOGFONTA lf;
    HRESULT hr;
    ULONG ref;
    HDC hdc;

    factory = create_factory();

    interop = NULL;
    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* Invalid HDC. */
    fontface = (void*)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFaceFromHdc(interop, NULL, &fontface);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    fontface = (void *)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFaceFromHdc(interop, (HDC)0xdeadbeef, &fontface);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hfont = CreateFontIndirectW(&logfont);
    hdc = CreateCompatibleDC(0);
    oldhfont = SelectObject(hdc, hfont);

    fontface = NULL;
    hr = IDWriteGdiInterop_CreateFontFaceFromHdc(interop, hdc, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFace_Release(fontface);
    DeleteObject(SelectObject(hdc, oldhfont));

    /* Select bitmap font MS Sans Serif, format that's not supported by DirectWrite. */
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -12;
    strcpy(lf.lfFaceName, "MS Sans Serif");

    hfont = CreateFontIndirectA(&lf);
    oldhfont = SelectObject(hdc, hfont);

    fontface = (void *)0xdeadbeef;
    hr = IDWriteGdiInterop_CreateFontFaceFromHdc(interop, hdc, &fontface);
    ok(hr == DWRITE_E_FILEFORMAT || broken(hr == E_FAIL) /* Vista */, "got 0x%08x\n", hr);
    ok(fontface == NULL, "got %p\n", fontface);

    DeleteObject(SelectObject(hdc, oldhfont));
    DeleteDC(hdc);

    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetSimulations(void)
{
    DWRITE_FONT_SIMULATIONS simulations;
    IDWriteGdiInterop *interop;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFont *font;
    LOGFONTW logfont;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    simulations = IDWriteFont_GetSimulations(font);
    ok(simulations == DWRITE_FONT_SIMULATIONS_OBLIQUE, "got %d\n", simulations);
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    simulations = IDWriteFontFace_GetSimulations(fontface);
    ok(simulations == DWRITE_FONT_SIMULATIONS_OBLIQUE, "got %d\n", simulations);
    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 0;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    simulations = IDWriteFont_GetSimulations(font);
    ok(simulations == DWRITE_FONT_SIMULATIONS_NONE, "got %d\n", simulations);
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    simulations = IDWriteFontFace_GetSimulations(fontface);
    ok(simulations == DWRITE_FONT_SIMULATIONS_NONE, "got %d\n", simulations);
    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font);

    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetFaceNames(void)
{
    static const WCHAR obliqueW[] = {'O','b','l','i','q','u','e',0};
    static const WCHAR enus2W[] = {'e','n','-','U','s',0};
    static const WCHAR enusW[] = {'e','n','-','u','s',0};
    IDWriteLocalizedStrings *strings, *strings2;
    IDWriteGdiInterop *interop;
    IDWriteFactory *factory;
    UINT32 count, index;
    IDWriteFont *font;
    LOGFONTW logfont;
    WCHAR buffW[255];
    BOOL exists;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = 12;
    logfont.lfWidth  = 12;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfItalic = 1;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hr = IDWriteGdiInterop_CreateFontFromLOGFONT(interop, &logfont, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont_GetFaceNames(font, &strings);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont_GetFaceNames(font, &strings2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(strings != strings2, "got %p, %p\n", strings2, strings);
    IDWriteLocalizedStrings_Release(strings2);

    count = IDWriteLocalizedStrings_GetCount(strings);
    ok(count == 1, "got %d\n", count);

    index = 1;
    exists = FALSE;
    hr = IDWriteLocalizedStrings_FindLocaleName(strings, enus2W, &index, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(index == 0 && exists, "got %d, %d\n", index, exists);

    count = 0;
    hr = IDWriteLocalizedStrings_GetLocaleNameLength(strings, 1, &count);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(count == ~0, "got %d\n", count);

    /* for simulated faces names are also simulated */
    buffW[0] = 0;
    hr = IDWriteLocalizedStrings_GetLocaleName(strings, 0, buffW, sizeof(buffW)/sizeof(WCHAR));
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!lstrcmpW(buffW, enusW), "got %s\n", wine_dbgstr_w(buffW));

    buffW[0] = 0;
    hr = IDWriteLocalizedStrings_GetString(strings, 0, buffW, sizeof(buffW)/sizeof(WCHAR));
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!lstrcmpW(buffW, obliqueW), "got %s\n", wine_dbgstr_w(buffW));
    IDWriteLocalizedStrings_Release(strings);

    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

struct local_refkey
{
    FILETIME writetime;
    WCHAR name[1];
};

static void test_TryGetFontTable(void)
{
    IDWriteLocalFontFileLoader *localloader;
    WIN32_FILE_ATTRIBUTE_DATA info;
    const struct local_refkey *key;
    IDWriteFontFileLoader *loader;
    const void *table, *table2;
    IDWriteFontFace *fontface;
    void *context, *context2;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    WCHAR buffW[MAX_PATH];
    BOOL exists, ret;
    UINT32 size, len;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    path = create_testfontfile(test_fontfile);

    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    key = NULL;
    size = 0;
    hr = IDWriteFontFile_GetReferenceKey(file, (const void**)&key, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size != 0, "got %u\n", size);

    ret = GetFileAttributesExW(path, GetFileExInfoStandard, &info);
    ok(ret, "got %d\n", ret);
    ok(!memcmp(&info.ftLastWriteTime, &key->writetime, sizeof(key->writetime)), "got wrong write time\n");

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void**)&localloader);
    IDWriteFontFileLoader_Release(loader);

    hr = IDWriteLocalFontFileLoader_GetFilePathLengthFromKey(localloader, key, size, &len);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(lstrlenW(key->name) == len, "path length %d\n", len);

    hr = IDWriteLocalFontFileLoader_GetFilePathFromKey(localloader, key, size, buffW, sizeof(buffW)/sizeof(WCHAR));
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!lstrcmpW(buffW, key->name), "got %s, expected %s\n", wine_dbgstr_w(buffW), wine_dbgstr_w(key->name));
    IDWriteLocalFontFileLoader_Release(localloader);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, 0, &fontface);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    exists = FALSE;
    context = (void*)0xdeadbeef;
    table = NULL;
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_CMAP_TAG, &table, &size, &context, &exists);
    ok(hr == S_OK, "got 0x%08x\n",hr);
    ok(exists == TRUE, "got %d\n", exists);
    ok(context == NULL && table != NULL, "cmap: context %p, table %p\n", context, table);

    exists = FALSE;
    context2 = (void*)0xdeadbeef;
    table2 = NULL;
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_CMAP_TAG, &table2, &size, &context2, &exists);
    ok(hr == S_OK, "got 0x%08x\n",hr);
    ok(exists == TRUE, "got %d\n", exists);
    ok(context2 == context && table2 == table, "cmap: context2 %p, table2 %p\n", context2, table2);

    IDWriteFontFace_ReleaseFontTable(fontface, context2);
    IDWriteFontFace_ReleaseFontTable(fontface, context);

    /* table does not exist */
    exists = TRUE;
    context = (void*)0xdeadbeef;
    table = (void*)0xdeadbeef;
    hr = IDWriteFontFace_TryGetFontTable(fontface, 0xabababab, &table, &size, &context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists == FALSE, "got %d\n", exists);
    ok(context == NULL && table == NULL, "got context %p, table pointer %p\n", context, table);

    IDWriteFontFace_Release(fontface);
    IDWriteFontFile_Release(file);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_ConvertFontToLOGFONT(void)
{
    IDWriteFactory *factory, *factory2;
    IDWriteFontCollection *collection;
    IDWriteGdiInterop *interop;
    IDWriteFontFamily *family;
    IDWriteFont *font;
    LOGFONTW logfont;
    UINT32 i, count;
    BOOL system;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    factory2 = create_factory();

    interop = NULL;
    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_GetSystemFontCollection(factory2, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

if (0) { /* crashes on native */
    IDWriteGdiInterop_ConvertFontToLOGFONT(interop, NULL, NULL, NULL);
    IDWriteGdiInterop_ConvertFontToLOGFONT(interop, NULL, &logfont, NULL);
    IDWriteGdiInterop_ConvertFontToLOGFONT(interop, font, NULL, &system);
}

    memset(&logfont, 0xcc, sizeof(logfont));
    system = TRUE;
    hr = IDWriteGdiInterop_ConvertFontToLOGFONT(interop, NULL, &logfont, &system);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(!system, "got %d\n", system);
    ok(logfont.lfFaceName[0] == 0, "got face name %s\n", wine_dbgstr_w(logfont.lfFaceName));

    count = IDWriteFontCollection_GetFontFamilyCount(collection);
    for (i = 0; i < count; i++) {
        WCHAR nameW[128], familynameW[64], facenameW[64];
        IDWriteLocalizedStrings *names;
        DWRITE_FONT_SIMULATIONS sim;
        IDWriteFontFamily *family;
        UINT32 font_count, j;
        IDWriteFont *font;
        LOGFONTW lf;

        hr = IDWriteFontCollection_GetFontFamily(collection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, familynameW, sizeof(familynameW)/sizeof(familynameW[0]));
        IDWriteLocalizedStrings_Release(names);

        font_count = IDWriteFontFamily_GetFontCount(family);

        for (j = 0; j < font_count; j++) {
            static const WCHAR spaceW[] = {' ', 0};

            hr = IDWriteFontFamily_GetFont(family, j, &font);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            hr = IDWriteFont_GetFaceNames(font, &names);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            get_enus_string(names, facenameW, sizeof(facenameW)/sizeof(facenameW[0]));
            IDWriteLocalizedStrings_Release(names);

            lstrcpyW(nameW, familynameW);
            lstrcatW(nameW, spaceW);
            lstrcatW(nameW, facenameW);

            system = FALSE;
            memset(&logfont, 0xcc, sizeof(logfont));
            hr = IDWriteGdiInterop_ConvertFontToLOGFONT(interop, font, &logfont, &system);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(system, "got %d\n", system);

            sim = IDWriteFont_GetSimulations(font);

            get_logfont_from_font(font, &lf);
            ok(logfont.lfWeight == lf.lfWeight, "%s: unexpected lfWeight %d, expected lfWeight %d, font weight %d, "
                "bold simulation %s\n", wine_dbgstr_w(nameW), logfont.lfWeight, lf.lfWeight, IDWriteFont_GetWeight(font),
                sim & DWRITE_FONT_SIMULATIONS_BOLD ? "yes" : "no");
            ok(logfont.lfItalic == lf.lfItalic, "%s: unexpected italic flag %d, oblique simulation %s\n",
                wine_dbgstr_w(nameW), logfont.lfItalic, sim & DWRITE_FONT_SIMULATIONS_OBLIQUE ? "yes" : "no");
            ok(!lstrcmpW(logfont.lfFaceName, lf.lfFaceName), "%s: unexpected facename %s, expected %s\n",
                wine_dbgstr_w(nameW), wine_dbgstr_w(logfont.lfFaceName), wine_dbgstr_w(lf.lfFaceName));

            ok(logfont.lfOutPrecision == OUT_OUTLINE_PRECIS, "%s: unexpected output precision %d\n", wine_dbgstr_w(nameW),
                logfont.lfOutPrecision);
            ok(logfont.lfClipPrecision == CLIP_DEFAULT_PRECIS, "%s: unexpected clipping precision %d\n", wine_dbgstr_w(nameW),
                logfont.lfClipPrecision);
            ok(logfont.lfQuality == DEFAULT_QUALITY, "%s: unexpected quality %d\n", wine_dbgstr_w(nameW), logfont.lfQuality);
            ok(logfont.lfPitchAndFamily == DEFAULT_PITCH, "%s: unexpected pitch %d\n", wine_dbgstr_w(nameW),
                logfont.lfPitchAndFamily);

            IDWriteFont_Release(font);
        }

        IDWriteFontFamily_Release(family);
    }

    IDWriteFactory_Release(factory2);

    IDWriteFontCollection_Release(collection);
    IDWriteFontFamily_Release(family);
    IDWriteFont_Release(font);
    IDWriteGdiInterop_Release(interop);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateStreamFromKey(void)
{
    IDWriteLocalFontFileLoader *localloader;
    IDWriteFontFileStream *stream, *stream2;
    IDWriteFontFileLoader *loader;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    UINT64 writetime;
    WCHAR *path;
    void *key;
    UINT32 size;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    key = NULL;
    size = 0;
    hr = IDWriteFontFile_GetReferenceKey(file, (const void**)&key, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size != 0, "got %u\n", size);

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void**)&localloader);
    IDWriteFontFileLoader_Release(loader);

    hr = IDWriteLocalFontFileLoader_CreateStreamFromKey(localloader, key, size, &stream);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(stream, 1);

    hr = IDWriteLocalFontFileLoader_CreateStreamFromKey(localloader, key, size, &stream2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(stream == stream2 || broken(stream != stream2) /* Win7 SP0 */, "got %p, %p\n", stream, stream2);
    if (stream == stream2)
        EXPECT_REF(stream, 2);
    IDWriteFontFileStream_Release(stream);
    IDWriteFontFileStream_Release(stream2);

    hr = IDWriteLocalFontFileLoader_CreateStreamFromKey(localloader, key, size, &stream);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(stream, 1);

    writetime = 0;
    hr = IDWriteFontFileStream_GetLastWriteTime(stream, &writetime);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(writetime != 0, "got %s\n", wine_dbgstr_longlong(writetime));

    IDWriteFontFileStream_Release(stream);
    IDWriteFontFile_Release(file);

    IDWriteLocalFontFileLoader_Release(localloader);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_ReadFileFragment(void)
{
    IDWriteLocalFontFileLoader *localloader;
    IDWriteFontFileStream *stream;
    IDWriteFontFileLoader *loader;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    const void *fragment, *fragment2;
    void *key, *context, *context2;
    UINT64 filesize;
    UINT32 size;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    key = NULL;
    size = 0;
    hr = IDWriteFontFile_GetReferenceKey(file, (const void**)&key, &size);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(size != 0, "got %u\n", size);

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteLocalFontFileLoader, (void**)&localloader);
    IDWriteFontFileLoader_Release(loader);

    hr = IDWriteLocalFontFileLoader_CreateStreamFromKey(localloader, key, size, &stream);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFileStream_GetFileSize(stream, &filesize);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* reading past the end of the stream */
    fragment = (void*)0xdeadbeef;
    context = (void*)0xdeadbeef;
    hr = IDWriteFontFileStream_ReadFileFragment(stream, &fragment, 0, filesize+1, &context);
    ok(hr == E_FAIL, "got 0x%08x\n", hr);
    ok(context == NULL, "got %p\n", context);
    ok(fragment == NULL, "got %p\n", fragment);

    fragment = (void*)0xdeadbeef;
    context = (void*)0xdeadbeef;
    hr = IDWriteFontFileStream_ReadFileFragment(stream, &fragment, 0, filesize, &context);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(context == NULL, "got %p\n", context);
    ok(fragment != NULL, "got %p\n", fragment);

    fragment2 = (void*)0xdeadbeef;
    context2 = (void*)0xdeadbeef;
    hr = IDWriteFontFileStream_ReadFileFragment(stream, &fragment2, 0, filesize, &context2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(context2 == NULL, "got %p\n", context2);
    ok(fragment == fragment2, "got %p, %p\n", fragment, fragment2);

    IDWriteFontFileStream_ReleaseFileFragment(stream, context);
    IDWriteFontFileStream_ReleaseFileFragment(stream, context2);

    /* fragment is released, try again */
    fragment = (void*)0xdeadbeef;
    context = (void*)0xdeadbeef;
    hr = IDWriteFontFileStream_ReadFileFragment(stream, &fragment, 0, filesize, &context);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(context == NULL, "got %p\n", context);
    ok(fragment == fragment2, "got %p, %p\n", fragment, fragment2);
    IDWriteFontFileStream_ReleaseFileFragment(stream, context);

    IDWriteFontFile_Release(file);
    IDWriteFontFileStream_Release(stream);
    IDWriteLocalFontFileLoader_Release(localloader);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_GetDesignGlyphMetrics(void)
{
    DWRITE_GLYPH_METRICS metrics[2];
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    UINT16 indices[2];
    UINT32 codepoint;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file,
        0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n",hr);
    IDWriteFontFile_Release(file);

    codepoint = 'A';
    indices[0] = 0;
    hr = IDWriteFontFace_GetGlyphIndices(fontface, &codepoint, 1, &indices[0]);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(indices[0] > 0, "got %u\n", indices[0]);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, NULL, 0, metrics, FALSE);
    ok(hr == E_INVALIDARG, "got 0x%08x\n",hr);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, NULL, 1, metrics, FALSE);
    ok(hr == E_INVALIDARG, "got 0x%08x\n",hr);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, indices, 0, metrics, FALSE);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    /* missing glyphs are ignored */
    indices[1] = 1;
    memset(metrics, 0xcc, sizeof(metrics));
    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, indices, 2, metrics, FALSE);
    ok(hr == S_OK, "got 0x%08x\n",hr);
    ok(metrics[0].advanceWidth == 1000, "got %d\n", metrics[0].advanceWidth);
    ok(metrics[1].advanceWidth == 0, "got %d\n", metrics[1].advanceWidth);

    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_IsMonospacedFont(void)
{
    static const WCHAR courierW[] = {'C','o','u','r','i','e','r',' ','N','e','w',0};
    IDWriteFontCollection *collection;
    IDWriteFactory *factory;
    UINT32 index;
    BOOL exists;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    exists = FALSE;
    hr = IDWriteFontCollection_FindFamilyName(collection, courierW, &index, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    if (exists) {
        IDWriteFontFamily *family;
        IDWriteFont1 *font1;
        IDWriteFont *font;

        hr = IDWriteFontCollection_GetFontFamily(collection, index, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFontFamily_Release(family);

        hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont1, (void**)&font1);
        if (hr == S_OK) {
            IDWriteFontFace1 *fontface1;
            IDWriteFontFace *fontface;
            BOOL is_monospaced;

            is_monospaced = IDWriteFont1_IsMonospacedFont(font1);
            ok(is_monospaced, "got %d\n", is_monospaced);

            hr = IDWriteFont1_CreateFontFace(font1, &fontface);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            is_monospaced = IDWriteFontFace1_IsMonospacedFont(fontface1);
            ok(is_monospaced, "got %d\n", is_monospaced);
            IDWriteFontFace1_Release(fontface1);

            IDWriteFontFace_Release(fontface);
            IDWriteFont1_Release(font1);
        }
        else
            win_skip("IsMonospacedFont() is not supported.\n");

        IDWriteFont_Release(font);
    }
    else
        skip("Courier New font not found.\n");

    ref = IDWriteFontCollection_Release(collection);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetDesignGlyphAdvances(void)
{
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file,
        0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    if (hr == S_OK) {
        UINT32 codepoint;
        UINT16 index;
        INT32 advance;

        codepoint = 'A';
        index = 0;
        hr = IDWriteFontFace1_GetGlyphIndices(fontface1, &codepoint, 1, &index);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(index > 0, "got %u\n", index);

        advance = 0;
        hr = IDWriteFontFace1_GetDesignGlyphAdvances(fontface1, 1, &index, &advance, FALSE);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(advance == 1000, "got %i\n", advance);

        advance = 0;
        hr = IDWriteFontFace1_GetDesignGlyphAdvances(fontface1, 1, &index, &advance, TRUE);
        ok(hr == S_OK, "got 0x%08x\n", hr);
    todo_wine
        ok(advance == 2048, "got %i\n", advance);

        IDWriteFontFace1_Release(fontface1);
    }
    else
        win_skip("GetDesignGlyphAdvances() is not supported.\n");

    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_GetGlyphRunOutline(void)
{
    DWRITE_GLYPH_OFFSET offsets[2];
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    IDWriteFontFace *face;
    UINT32 codepoint;
    FLOAT advances[2];
    UINT16 glyphs[2];
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n",hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &face);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    codepoint = 'A';
    glyphs[0] = 0;
    hr = IDWriteFontFace_GetGlyphIndices(face, &codepoint, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyphs[0] > 0, "got %u\n", glyphs[0]);
    glyphs[1] = glyphs[0];

    hr = IDWriteFontFace_GetGlyphRunOutline(face, 2048.0, glyphs, advances, offsets, 1, FALSE, FALSE, NULL);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_GetGlyphRunOutline(face, 2048.0, NULL, NULL, offsets, 1, FALSE, FALSE, &test_geomsink);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    advances[0] = 1.0;
    advances[1] = 0.0;

    offsets[0].advanceOffset = 1.0;
    offsets[0].ascenderOffset = 1.0;
    offsets[1].advanceOffset = 0.0;
    offsets[1].ascenderOffset = 0.0;

    /* default advances, no offsets */
    memset(g_startpoints, 0, sizeof(g_startpoints));
    g_startpoint_count = 0;
    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, NULL, 2, FALSE, FALSE, &test_geomsink);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(g_startpoint_count == 2, "got %d\n", g_startpoint_count);
    if (g_startpoint_count == 2) {
        /* glyph advance of 500 is applied */
        ok(g_startpoints[0].x == 229.5 && g_startpoints[0].y == -629.0, "0: got (%.2f,%.2f)\n", g_startpoints[0].x, g_startpoints[0].y);
        ok(g_startpoints[1].x == 729.5 && g_startpoints[1].y == -629.0, "1: got (%.2f,%.2f)\n", g_startpoints[1].x, g_startpoints[1].y);
    }
    CHECK_CALLED(setfillmode);

    /* default advances, no offsets, RTL */
    memset(g_startpoints, 0, sizeof(g_startpoints));
    g_startpoint_count = 0;
    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, NULL, 2, FALSE, TRUE, &test_geomsink);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(g_startpoint_count == 2, "got %d\n", g_startpoint_count);
    if (g_startpoint_count == 2) {
        /* advance is -500 now */
        ok(g_startpoints[0].x == -270.5 && g_startpoints[0].y == -629.0, "0: got (%.2f,%.2f)\n", g_startpoints[0].x, g_startpoints[0].y);
        ok(g_startpoints[1].x == -770.5 && g_startpoints[1].y == -629.0, "1: got (%.2f,%.2f)\n", g_startpoints[1].x, g_startpoints[1].y);
    }
    CHECK_CALLED(setfillmode);

    /* default advances, additional offsets */
    memset(g_startpoints, 0, sizeof(g_startpoints));
    g_startpoint_count = 0;
    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, offsets, 2, FALSE, FALSE, &test_geomsink);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(g_startpoint_count == 2, "got %d\n", g_startpoint_count);
    if (g_startpoint_count == 2) {
        /* offsets applied to first contour */
        ok(g_startpoints[0].x == 230.5 && g_startpoints[0].y == -630.0, "0: got (%.2f,%.2f)\n", g_startpoints[0].x, g_startpoints[0].y);
        ok(g_startpoints[1].x == 729.5 && g_startpoints[1].y == -629.0, "1: got (%.2f,%.2f)\n", g_startpoints[1].x, g_startpoints[1].y);
    }
    CHECK_CALLED(setfillmode);

    /* default advances, additional offsets, RTL */
    memset(g_startpoints, 0, sizeof(g_startpoints));
    g_startpoint_count = 0;
    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, offsets, 2, FALSE, TRUE, &test_geomsink);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(g_startpoint_count == 2, "got %d\n", g_startpoint_count);
    if (g_startpoint_count == 2) {
        ok(g_startpoints[0].x == -271.5 && g_startpoints[0].y == -630.0, "0: got (%.2f,%.2f)\n", g_startpoints[0].x, g_startpoints[0].y);
        ok(g_startpoints[1].x == -770.5 && g_startpoints[1].y == -629.0, "1: got (%.2f,%.2f)\n", g_startpoints[1].x, g_startpoints[1].y);
    }
    CHECK_CALLED(setfillmode);

    /* custom advances and offsets, offset turns total advance value to zero */
    memset(g_startpoints, 0, sizeof(g_startpoints));
    g_startpoint_count = 0;
    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, advances, offsets, 2, FALSE, FALSE, &test_geomsink);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(g_startpoint_count == 2, "got %d\n", g_startpoint_count);
    if (g_startpoint_count == 2) {
        ok(g_startpoints[0].x == 230.5 && g_startpoints[0].y == -630.0, "0: got (%.2f,%.2f)\n", g_startpoints[0].x, g_startpoints[0].y);
        ok(g_startpoints[1].x == 230.5 && g_startpoints[1].y == -629.0, "1: got (%.2f,%.2f)\n", g_startpoints[1].x, g_startpoints[1].y);
    }
    CHECK_CALLED(setfillmode);

    /* 0 glyph count */
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, NULL, 0, FALSE, FALSE, &test_geomsink2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* Glyph with open figure, single contour point. */
    codepoint = 'B';
    glyphs[0] = 0;
    hr = IDWriteFontFace_GetGlyphIndices(face, &codepoint, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyphs[0] > 0, "got %u\n", glyphs[0]);

    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, NULL, 1, FALSE, FALSE, &test_geomsink2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    CHECK_CALLED(setfillmode);

    IDWriteFactory_Release(factory);
    IDWriteFontFace_Release(face);
    DELETE_FONTFILE(path);

    /* space glyph */
    factory = create_factory();
    face = create_fontface(factory);

    codepoint = ' ';
    glyphs[0] = 0;
    hr = IDWriteFontFace_GetGlyphIndices(face, &codepoint, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyphs[0] > 0, "got %u\n", glyphs[0]);

    SET_EXPECT(setfillmode);
    hr = IDWriteFontFace_GetGlyphRunOutline(face, 1024.0, glyphs, NULL, NULL, 1, FALSE, FALSE, &test_geomsink2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    CHECK_CALLED(setfillmode);

    IDWriteFontFace_Release(face);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetEudcFontCollection(void)
{
    IDWriteFontCollection *coll, *coll2;
    IDWriteFactory1 *factory1;
    IDWriteFactory *factory;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory1, (void**)&factory1);
    IDWriteFactory_Release(factory);
    if (hr != S_OK) {
        win_skip("GetEudcFontCollection() is not supported.\n");
        return;
    }

    EXPECT_REF(factory1, 1);
    hr = IDWriteFactory1_GetEudcFontCollection(factory1, &coll, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(factory1, 2);
    hr = IDWriteFactory1_GetEudcFontCollection(factory1, &coll2, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(factory1, 2);
    ok(coll == coll2, "got %p, %p\n", coll, coll2);
    IDWriteFontCollection_Release(coll);
    IDWriteFontCollection_Release(coll2);

    ref = IDWriteFactory1_Release(factory1);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_GetCaretMetrics(void)
{
    DWRITE_FONT_METRICS1 metrics;
    IDWriteFontFace1 *fontface1;
    DWRITE_CARET_METRICS caret;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    IDWriteFont *font;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    IDWriteFontFace_Release(fontface);
    if (hr != S_OK) {
        win_skip("GetCaretMetrics() is not supported.\n");
        ref = IDWriteFactory_Release(factory);
        ok(ref == 0, "factory not released, %u\n", ref);
        DELETE_FONTFILE(path);
        return;
    }

    memset(&caret, 0xcc, sizeof(caret));
    IDWriteFontFace1_GetCaretMetrics(fontface1, &caret);
    ok(caret.slopeRise == 1, "got %d\n", caret.slopeRise);
    ok(caret.slopeRun == 0, "got %d\n", caret.slopeRun);
    ok(caret.offset == 0, "got %d\n", caret.offset);
    IDWriteFontFace1_Release(fontface1);
    IDWriteFactory_Release(factory);

    /* now with Tahoma Normal */
    factory = create_factory();
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_NORMAL);
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFace_Release(fontface);

    memset(&caret, 0xcc, sizeof(caret));
    IDWriteFontFace1_GetCaretMetrics(fontface1, &caret);
    ok(caret.slopeRise == 1, "got %d\n", caret.slopeRise);
    ok(caret.slopeRun == 0, "got %d\n", caret.slopeRun);
    ok(caret.offset == 0, "got %d\n", caret.offset);
    IDWriteFontFace1_Release(fontface1);

    /* simulated italic */
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_ITALIC);
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFace_Release(fontface);

    IDWriteFontFace1_GetMetrics(fontface1, &metrics);

    memset(&caret, 0xcc, sizeof(caret));
    IDWriteFontFace1_GetCaretMetrics(fontface1, &caret);
    ok(caret.slopeRise == metrics.designUnitsPerEm, "got %d\n", caret.slopeRise);
    ok(caret.slopeRun > 0, "got %d\n", caret.slopeRun);
    ok(caret.offset == 0, "got %d\n", caret.offset);
    IDWriteFontFace1_Release(fontface1);

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_GetGlyphCount(void)
{
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    UINT16 count;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    count = IDWriteFontFace_GetGlyphCount(fontface);
    ok(count == 8, "got %u\n", count);

    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_GetKerningPairAdjustments(void)
{
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFontFile *file;
    WCHAR *path;
    HRESULT hr;
    ULONG ref;

    path = create_testfontfile(test_fontfile);
    factory = create_factory();

    hr = IDWriteFactory_CreateFontFileReference(factory, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory_CreateFontFace(factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, &file, 0, DWRITE_FONT_SIMULATIONS_NONE, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    if (hr == S_OK) {
        INT32 adjustments[1];

        hr = IDWriteFontFace1_GetKerningPairAdjustments(fontface1, 0, NULL, NULL);
        ok(hr == E_INVALIDARG || broken(hr == S_OK) /* win8 */, "got 0x%08x\n", hr);

        if (0) /* crashes on native */
            hr = IDWriteFontFace1_GetKerningPairAdjustments(fontface1, 1, NULL, NULL);

        adjustments[0] = 1;
        hr = IDWriteFontFace1_GetKerningPairAdjustments(fontface1, 1, NULL, adjustments);
        ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
        ok(adjustments[0] == 0, "got %d\n", adjustments[0]);

        IDWriteFontFace1_Release(fontface1);
    }
    else
        win_skip("GetKerningPairAdjustments() is not supported.\n");

    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
    DELETE_FONTFILE(path);
}

static void test_CreateRenderingParams(void)
{
    IDWriteRenderingParams2 *params2;
    IDWriteRenderingParams1 *params1;
    IDWriteRenderingParams *params;
    DWRITE_RENDERING_MODE mode;
    IDWriteFactory3 *factory3;
    IDWriteFactory *factory;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_CreateCustomRenderingParams(factory, 1.0, 0.0, 0.0, DWRITE_PIXEL_GEOMETRY_FLAT,
        DWRITE_RENDERING_MODE_DEFAULT, &params);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams1, (void**)&params1);
    if (hr == S_OK) {
        FLOAT enhcontrast;

        /* test what enhanced contrast setting set by default to */
        enhcontrast = IDWriteRenderingParams1_GetGrayscaleEnhancedContrast(params1);
        ok(enhcontrast == 1.0, "got %.2f\n", enhcontrast);
        IDWriteRenderingParams1_Release(params1);

        hr = IDWriteRenderingParams_QueryInterface(params, &IID_IDWriteRenderingParams2, (void**)&params2);
        if (hr == S_OK) {
            DWRITE_GRID_FIT_MODE gridfit;

            /* default gridfit mode */
            gridfit = IDWriteRenderingParams2_GetGridFitMode(params2);
            ok(gridfit == DWRITE_GRID_FIT_MODE_DEFAULT, "got %d\n", gridfit);

            IDWriteRenderingParams2_Release(params2);
        }
        else
            win_skip("IDWriteRenderingParams2 not supported.\n");
    }
    else
        win_skip("IDWriteRenderingParams1 not supported.\n");

    IDWriteRenderingParams_Release(params);

    hr = IDWriteFactory_CreateRenderingParams(factory, &params);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    mode = IDWriteRenderingParams_GetRenderingMode(params);
    ok(mode == DWRITE_RENDERING_MODE_DEFAULT, "got %d\n", mode);
    IDWriteRenderingParams_Release(params);

    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void**)&factory3);
    if (hr == S_OK) {
        IDWriteRenderingParams3 *params3;

        hr = IDWriteFactory3_CreateCustomRenderingParams(factory3, 1.0f, 0.0f, 0.0f, 1.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
            DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED, DWRITE_GRID_FIT_MODE_DEFAULT, &params3);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteRenderingParams3_QueryInterface(params3, &IID_IDWriteRenderingParams, (void**)&params);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        mode = IDWriteRenderingParams_GetRenderingMode(params);
        ok(mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, "got %d\n", mode);

        IDWriteRenderingParams_Release(params);
        IDWriteRenderingParams3_Release(params3);
        IDWriteFactory3_Release(factory3);
    }
    else
        win_skip("IDWriteRenderingParams3 not supported.\n");

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateGlyphRunAnalysis(void)
{
    static const DWRITE_RENDERING_MODE rendermodes[] = {
        DWRITE_RENDERING_MODE_ALIASED,
        DWRITE_RENDERING_MODE_GDI_CLASSIC,
        DWRITE_RENDERING_MODE_GDI_NATURAL,
        DWRITE_RENDERING_MODE_NATURAL,
        DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
    };

    IDWriteGlyphRunAnalysis *analysis, *analysis2;
    IDWriteRenderingParams *params;
    IDWriteFactory3 *factory3;
    IDWriteFactory2 *factory2;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    IDWriteFontFace *face;
    UINT16 glyph, glyphs[10];
    FLOAT advances[2];
    HRESULT hr;
    UINT32 ch;
    RECT rect, rect2;
    DWRITE_GLYPH_OFFSET offsets[2];
    DWRITE_GLYPH_METRICS metrics;
    DWRITE_FONT_METRICS fm;
    DWRITE_MATRIX m;
    ULONG size;
    BYTE *bits;
    ULONG ref;
    int i;

    factory = create_factory();
    face = create_fontface(factory);

    ch = 'A';
    glyph = 0;
    hr = IDWriteFontFace_GetGlyphIndices(face, &ch, 1, &glyph);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyph > 0, "got %u\n", glyph);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(face, &glyph, 1, &metrics, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    advances[0] = metrics.advanceWidth;

    offsets[0].advanceOffset = 0.0;
    offsets[0].ascenderOffset = 0.0;

    run.fontFace = face;
    run.fontEmSize = 24.0;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = advances;
    run.glyphOffsets = offsets;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    /* zero ppdip */
    analysis = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 0.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(analysis == NULL, "got %p\n", analysis);

    /* negative ppdip */
    analysis = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, -1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(analysis == NULL, "got %p\n", analysis);

    /* default mode is not allowed */
    analysis = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_DEFAULT, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(analysis == NULL, "got %p\n", analysis);

    /* outline too */
    analysis = (void*)0xdeadbeef;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_OUTLINE, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(analysis == NULL, "got %p\n", analysis);

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* invalid texture type */
    memset(&rect, 0xcc, sizeof(rect));
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1+1, &rect);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(rect.left == 0 && rect.right == 0 &&
       rect.top == 0 && rect.bottom == 0, "unexpected rect\n");

    /* check how origin affects bounds */
    SetRectEmpty(&rect);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect), "got empty rect\n");
    IDWriteGlyphRunAnalysis_Release(analysis);

    /* doubled ppdip */
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 2.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    SetRectEmpty(&rect2);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(rect.right - rect.left < rect2.right - rect2.left, "expected wider rect\n");
    ok(rect.bottom - rect.top < rect2.bottom - rect2.top, "expected taller rect\n");
    IDWriteGlyphRunAnalysis_Release(analysis);

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        10.0, -5.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect2);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect2), "got empty rect\n");
    IDWriteGlyphRunAnalysis_Release(analysis);

    ok(!EqualRect(&rect, &rect2), "got equal bounds\n");
    OffsetRect(&rect, 10, -5);
    ok(EqualRect(&rect, &rect2), "got different bounds\n");

    for (i = 0; i < sizeof(rendermodes)/sizeof(rendermodes[0]); i++) {
        hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
            rendermodes[i], DWRITE_MEASURING_MODE_NATURAL,
            0.0, 0.0, &analysis);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        if (rendermodes[i] == DWRITE_RENDERING_MODE_ALIASED) {
            SetRectEmpty(&rect);
            hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(!IsRectEmpty(&rect), "got empty rect\n");

            SetRect(&rect, 0, 0, 1, 1);
            hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(IsRectEmpty(&rect), "unexpected empty rect\n");
        }
        else {
            SetRect(&rect, 0, 0, 1, 1);
            hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(IsRectEmpty(&rect), "got empty rect\n");

            SetRectEmpty(&rect);
            hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(!IsRectEmpty(&rect), "got empty rect\n");
        }

        IDWriteGlyphRunAnalysis_Release(analysis);
    }

    IDWriteFontFace_GetMetrics(run.fontFace, &fm);

    /* check bbox for a single glyph run */
    for (run.fontEmSize = 1.0; run.fontEmSize <= 100.0; run.fontEmSize += 1.0) {
        DWRITE_GLYPH_METRICS gm;
        LONG bboxX, bboxY;

        hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
            DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
            0.0, 0.0, &analysis);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        SetRectEmpty(&rect);
        hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFace_GetGdiCompatibleGlyphMetrics(run.fontFace, run.fontEmSize, 1.0, NULL,
             DWRITE_MEASURING_MODE_GDI_CLASSIC, run.glyphIndices, 1, &gm, run.isSideways);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        /* metrics are in design units */
        bboxX = (int)floorf((gm.advanceWidth - gm.leftSideBearing - gm.rightSideBearing) * run.fontEmSize / fm.designUnitsPerEm + 0.5f);
        bboxY = (int)floorf((gm.advanceHeight - gm.topSideBearing - gm.bottomSideBearing) * run.fontEmSize / fm.designUnitsPerEm + 0.5f);

        rect.right -= rect.left;
        rect.bottom -= rect.top;
        ok(abs(bboxX - rect.right) <= 2, "%.0f: bbox width %d, from metrics %d\n", run.fontEmSize, rect.right, bboxX);
        ok(abs(bboxY - rect.bottom) <= 2, "%.0f: bbox height %d, from metrics %d\n", run.fontEmSize, rect.bottom, bboxY);

        IDWriteGlyphRunAnalysis_Release(analysis);
    }

    /* without offsets */
    run.fontFace = face;
    run.fontEmSize = 24.0;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = advances;
    run.glyphOffsets = NULL;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect), "got empty bounds\n");

    IDWriteGlyphRunAnalysis_Release(analysis);

    /* without explicit advances */
    run.fontFace = face;
    run.fontEmSize = 24.0;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = NULL;
    run.glyphOffsets = NULL;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect), "got empty bounds\n");

    IDWriteGlyphRunAnalysis_Release(analysis);

    /* test that advances are scaled according to ppdip too */
    glyphs[0] = glyphs[1] = glyph;
    advances[0] = advances[1] = 100.0f;
    run.fontFace = face;
    run.fontEmSize = 24.0;
    run.glyphCount = 2;
    run.glyphIndices = glyphs;
    run.glyphAdvances = advances;
    run.glyphOffsets = NULL;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect2);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect2), "got empty bounds\n");
    ok(!EqualRect(&rect, &rect2), "got wrong rect2\n");
    ok((rect2.right - rect.left) > advances[0], "got rect width %d for advance %f\n", rect.right - rect.left, advances[0]);
    IDWriteGlyphRunAnalysis_Release(analysis);

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 2.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok((rect.right - rect.left) > 2 * advances[0], "got rect width %d for advance %f\n", rect.right - rect.left, advances[0]);
    IDWriteGlyphRunAnalysis_Release(analysis);

    /* with scaling transform */
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&rect), "got rect width %d\n", rect.right - rect.left);
    IDWriteGlyphRunAnalysis_Release(analysis);

    memset(&m, 0, sizeof(m));
    m.m11 = 2.0;
    m.m22 = 1.0;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, &m,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&rect2);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok((rect2.right - rect2.left) > (rect.right - rect.left), "got rect width %d\n", rect2.right - rect2.left);

    /* instances are not reused for same runs */
    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, &m,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(analysis2 != analysis, "got %p, previous instance %p\n", analysis2, analysis);
    IDWriteGlyphRunAnalysis_Release(analysis2);

    IDWriteGlyphRunAnalysis_Release(analysis);

    if (IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory2, (void **)&factory2) == S_OK) {
        FLOAT gamma, contrast, cleartype_level;

        /* Invalid antialias mode. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_ALIASED,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE + 1,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid grid fit mode. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_ALIASED,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED + 1, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid rendering mode. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_OUTLINE,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid measuring mode. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_ALIASED,
                DWRITE_MEASURING_MODE_GDI_NATURAL + 1, DWRITE_GRID_FIT_MODE_ENABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Win8 does not accept default grid fitting mode. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_NATURAL,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f,  0.0f, &analysis);
        ok(hr == S_OK || broken(hr == E_INVALIDARG) /* Win8 */, "Failed to create analysis, hr %#x.\n", hr);
        if (hr == S_OK)
            IDWriteGlyphRunAnalysis_Release(analysis);

        /* Natural mode, grayscale antialiased. */
        hr = IDWriteFactory2_CreateGlyphRunAnalysis(factory2, &run, NULL, DWRITE_RENDERING_MODE_NATURAL,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DISABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f,  0.0f, &analysis);
        ok(hr == S_OK, "Failed to create analysis, hr %#x.\n", hr);

        SetRect(&rect, 0, 1, 0, 1);
        hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
        ok(hr == S_OK, "Failed to get texture bounds, hr %#x.\n", hr);
        ok(IsRectEmpty(&rect), "Expected empty bbox.\n");

        SetRectEmpty(&rect);
        hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
        ok(hr == S_OK, "Failed to get texture bounds, hr %#x.\n", hr);
        ok(!IsRectEmpty(&rect), "Unexpected empty bbox.\n");

        size = (rect.right - rect.left) * (rect.bottom - rect.top);
        bits = HeapAlloc(GetProcessHeap(), 0, size);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect, bits, size);
        ok(hr == S_OK, "Failed to get alpha texture, hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect, bits, size - 1);
        ok(hr == E_NOT_SUFFICIENT_BUFFER, "Unexpected hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect, bits, size);
        ok(hr == DWRITE_E_UNSUPPORTEDOPERATION, "Unexpected hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect, bits, size - 1);
    todo_wine
        ok(hr == DWRITE_E_UNSUPPORTEDOPERATION, "Unexpected hr %#x.\n", hr);

        HeapFree(GetProcessHeap(), 0, bits);

        hr = IDWriteFactory_CreateCustomRenderingParams(factory, 0.1f, 0.0f, 1.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                DWRITE_RENDERING_MODE_NATURAL, &params);
        ok(hr == S_OK, "Failed to create custom parameters, hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_GetAlphaBlendParams(analysis, params, &gamma, &contrast, &cleartype_level);
        ok(hr == S_OK, "Failed to get alpha blend params, hr %#x.\n", hr);
    todo_wine
        ok(cleartype_level == 0.0f, "Unexpected cleartype level %f.\n", cleartype_level);

        IDWriteRenderingParams_Release(params);
        IDWriteGlyphRunAnalysis_Release(analysis);

        IDWriteFactory2_Release(factory2);
    }

    if (IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void **)&factory3) == S_OK) {

        /* Invalid antialias mode. */
        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_ALIASED,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE + 1,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid grid fit mode. */
        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_ALIASED,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED + 1, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid rendering mode. */
        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_OUTLINE,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_ENABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        /* Invalid measuring mode. */
        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_ALIASED,
                DWRITE_MEASURING_MODE_GDI_NATURAL + 1, DWRITE_GRID_FIT_MODE_ENABLED,
                DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE, 0.0f, 0.0f, &analysis);
        ok(hr == E_INVALIDARG, "Unexpected hr %#x.\n", hr);

        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_NATURAL,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DEFAULT, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f,  0.0f, &analysis);
        ok(hr == S_OK, "Failed to create analysis, hr %#x.\n", hr);
        IDWriteGlyphRunAnalysis_Release(analysis);

        /* Natural mode, grayscale antialiased. */
        hr = IDWriteFactory3_CreateGlyphRunAnalysis(factory3, &run, NULL, DWRITE_RENDERING_MODE1_NATURAL,
                DWRITE_MEASURING_MODE_NATURAL, DWRITE_GRID_FIT_MODE_DISABLED, DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE,
                0.0f,  0.0f, &analysis);
        ok(hr == S_OK, "Failed to create analysis, hr %#x.\n", hr);

        SetRect(&rect, 0, 1, 0, 1);
        hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect);
        ok(hr == S_OK, "Failed to get texture bounds, hr %#x.\n", hr);
        ok(IsRectEmpty(&rect), "Expected empty bbox.\n");

        SetRectEmpty(&rect);
        hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect);
        ok(hr == S_OK, "Failed to get texture bounds, hr %#x.\n", hr);
        ok(!IsRectEmpty(&rect), "Unexpected empty bbox.\n");

        size = (rect.right - rect.left) * (rect.bottom - rect.top);
        bits = HeapAlloc(GetProcessHeap(), 0, size);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect, bits, size);
        ok(hr == S_OK, "Failed to get alpha texture, hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &rect, bits, size - 1);
        ok(hr == E_NOT_SUFFICIENT_BUFFER, "Unexpected hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect, bits, size);
        ok(hr == DWRITE_E_UNSUPPORTEDOPERATION, "Unexpected hr %#x.\n", hr);

        hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &rect, bits, size - 1);
    todo_wine
        ok(hr == DWRITE_E_UNSUPPORTEDOPERATION, "Unexpected hr %#x.\n", hr);

        HeapFree(GetProcessHeap(), 0, bits);

        IDWriteGlyphRunAnalysis_Release(analysis);

        IDWriteFactory3_Release(factory3);
    }

    IDWriteFontFace_Release(face);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

#define round(x) ((int)floor((x) + 0.5))

struct VDMX_Header
{
    WORD version;
    WORD numRecs;
    WORD numRatios;
};

struct VDMX_Ratio
{
    BYTE bCharSet;
    BYTE xRatio;
    BYTE yStartRatio;
    BYTE yEndRatio;
};

struct VDMX_group
{
    WORD recs;
    BYTE startsz;
    BYTE endsz;
};

struct VDMX_vTable
{
    WORD yPelHeight;
    SHORT yMax;
    SHORT yMin;
};

static const struct VDMX_group *find_vdmx_group(const struct VDMX_Header *hdr)
{
    WORD num_ratios, i, group_offset = 0;
    struct VDMX_Ratio *ratios = (struct VDMX_Ratio*)(hdr + 1);
    BYTE dev_x_ratio = 1, dev_y_ratio = 1;

    num_ratios = GET_BE_WORD(hdr->numRatios);

    for (i = 0; i < num_ratios; i++)
    {
        if (!ratios[i].bCharSet) continue;

        if ((ratios[i].xRatio == 0 && ratios[i].yStartRatio == 0 &&
             ratios[i].yEndRatio == 0) ||
	   (ratios[i].xRatio == dev_x_ratio && ratios[i].yStartRatio <= dev_y_ratio &&
            ratios[i].yEndRatio >= dev_y_ratio))
        {
            group_offset = GET_BE_WORD(*((WORD *)(ratios + num_ratios) + i));
            break;
        }
    }
    if (group_offset)
        return (const struct VDMX_group *)((BYTE *)hdr + group_offset);
    return NULL;
}

static BOOL get_vdmx_size(const struct VDMX_group *group, int emsize, int *a, int *d)
{
    WORD recs, i;
    const struct VDMX_vTable *tables;

    if (!group) return FALSE;

    recs = GET_BE_WORD(group->recs);
    if (emsize < group->startsz || emsize >= group->endsz) return FALSE;

    tables = (const struct VDMX_vTable *)(group + 1);
    for (i = 0; i < recs; i++)
    {
        WORD ppem = GET_BE_WORD(tables[i].yPelHeight);
        if (ppem > emsize)
        {
            /* FIXME: Supposed to interpolate */
            trace("FIXME interpolate %d\n", emsize);
            return FALSE;
        }

        if (ppem == emsize)
        {
            *a = (SHORT)GET_BE_WORD(tables[i].yMax);
            *d = -(SHORT)GET_BE_WORD(tables[i].yMin);
            return TRUE;
        }
    }
    return FALSE;
}

static void test_metrics_cmp(FLOAT emsize, const DWRITE_FONT_METRICS *metrics, const DWRITE_FONT_METRICS1 *expected)
{
    ok(metrics->designUnitsPerEm == expected->designUnitsPerEm, "%.2f: emsize: got %u expect %u\n",
           emsize, metrics->designUnitsPerEm, expected->designUnitsPerEm);
    ok(metrics->ascent == expected->ascent, "%.2f a: got %u expect %u\n",
           emsize, metrics->ascent, expected->ascent);
    ok(metrics->descent == expected->descent, "%.2f d: got %u expect %u\n",
           emsize, metrics->descent, expected->descent);
    ok(metrics->lineGap == expected->lineGap, "%.2f lg: got %d expect %d\n",
           emsize, metrics->lineGap, expected->lineGap);
    ok(metrics->capHeight == expected->capHeight, "%.2f capH: got %u expect %u\n",
           emsize, metrics->capHeight, expected->capHeight);
    ok(metrics->xHeight == expected->xHeight, "%.2f xH: got %u expect %u\n",
           emsize, metrics->xHeight, expected->xHeight);
    ok(metrics->underlinePosition == expected->underlinePosition, "%.2f ulP: got %d expect %d\n",
            emsize, metrics->underlinePosition, expected->underlinePosition);
    ok(metrics->underlineThickness == expected->underlineThickness, "%.2f ulTh: got %u expect %u\n",
           emsize, metrics->underlineThickness, expected->underlineThickness);
    ok(metrics->strikethroughPosition == expected->strikethroughPosition, "%.2f stP: got %d expect %d\n",
           emsize, metrics->strikethroughPosition, expected->strikethroughPosition);
    ok(metrics->strikethroughThickness == expected->strikethroughThickness, "%.2f stTh: got %u expect %u\n",
           emsize, metrics->strikethroughThickness, expected->strikethroughThickness);
}

static void test_metrics1_cmp(FLOAT emsize, const DWRITE_FONT_METRICS1 *metrics, const DWRITE_FONT_METRICS1 *expected)
{
    ok(metrics->designUnitsPerEm == expected->designUnitsPerEm, "%.2f: emsize: got %u expect %u\n",
           emsize, metrics->designUnitsPerEm, expected->designUnitsPerEm);
    ok(metrics->ascent == expected->ascent, "%.2f a: got %u expect %u\n",
           emsize, metrics->ascent, expected->ascent);
    ok(metrics->descent == expected->descent, "%.2f d: got %u expect %u\n",
           emsize, metrics->descent, expected->descent);
    ok(metrics->lineGap == expected->lineGap, "%.2f lg: got %d expect %d\n",
           emsize, metrics->lineGap, expected->lineGap);
    ok(metrics->capHeight == expected->capHeight, "%.2f capH: got %u expect %u\n",
           emsize, metrics->capHeight, expected->capHeight);
    ok(metrics->xHeight == expected->xHeight, "%.2f xH: got %u expect %u\n",
           emsize, metrics->xHeight, expected->xHeight);
    ok(metrics->underlinePosition == expected->underlinePosition, "%.2f ulP: got %d expect %d\n",
            emsize, metrics->underlinePosition, expected->underlinePosition);
    ok(metrics->underlineThickness == expected->underlineThickness, "%.2f ulTh: got %u expect %u\n",
           emsize, metrics->underlineThickness, expected->underlineThickness);
    ok(metrics->strikethroughPosition == expected->strikethroughPosition, "%.2f stP: got %d expect %d\n",
           emsize, metrics->strikethroughPosition, expected->strikethroughPosition);
    ok(metrics->strikethroughThickness == expected->strikethroughThickness, "%.2f stTh: got %u expect %u\n",
           emsize, metrics->strikethroughThickness, expected->strikethroughThickness);
    ok(metrics->glyphBoxLeft == expected->glyphBoxLeft, "%.2f box left: got %d expect %d\n",
           emsize, metrics->glyphBoxLeft, expected->glyphBoxLeft);
if (0) { /* this is not consistent */
    ok(metrics->glyphBoxTop == expected->glyphBoxTop, "%.2f box top: got %d expect %d\n",
           emsize, metrics->glyphBoxTop, expected->glyphBoxTop);
    ok(metrics->glyphBoxRight == expected->glyphBoxRight, "%.2f box right: got %d expect %d\n",
           emsize, metrics->glyphBoxRight, expected->glyphBoxRight);
}
    ok(metrics->glyphBoxBottom == expected->glyphBoxBottom, "%.2f box bottom: got %d expect %d\n",
           emsize, metrics->glyphBoxBottom, expected->glyphBoxBottom);
    ok(metrics->subscriptPositionX == expected->subscriptPositionX, "%.2f subX: got %d expect %d\n",
           emsize, metrics->subscriptPositionX, expected->subscriptPositionX);
    ok(metrics->subscriptPositionY == expected->subscriptPositionY, "%.2f subY: got %d expect %d\n",
           emsize, metrics->subscriptPositionY, expected->subscriptPositionY);
    ok(metrics->subscriptSizeX == expected->subscriptSizeX, "%.2f subsizeX: got %d expect %d\n",
           emsize, metrics->subscriptSizeX, expected->subscriptSizeX);
    ok(metrics->subscriptPositionY == expected->subscriptPositionY, "%.2f subsizeY: got %d expect %d\n",
           emsize, metrics->subscriptSizeY, expected->subscriptSizeY);
    ok(metrics->superscriptPositionX == expected->superscriptPositionX, "%.2f supX: got %d expect %d\n",
           emsize, metrics->superscriptPositionX, expected->superscriptPositionX);
if (0)
    ok(metrics->superscriptPositionY == expected->superscriptPositionY, "%.2f supY: got %d expect %d\n",
           emsize, metrics->superscriptPositionY, expected->superscriptPositionY);
    ok(metrics->superscriptSizeX == expected->superscriptSizeX, "%.2f supsizeX: got %d expect %d\n",
           emsize, metrics->superscriptSizeX, expected->superscriptSizeX);
    ok(metrics->superscriptSizeY == expected->superscriptSizeY, "%.2f supsizeY: got %d expect %d\n",
           emsize, metrics->superscriptSizeY, expected->superscriptSizeY);
    ok(metrics->hasTypographicMetrics == expected->hasTypographicMetrics, "%.2f hastypo: got %d expect %d\n",
           emsize, metrics->hasTypographicMetrics, expected->hasTypographicMetrics);
}

struct compatmetrics_test {
    DWRITE_MATRIX m;
    FLOAT ppdip;
    FLOAT emsize;
};

static struct compatmetrics_test compatmetrics_tests[] = {
    { { 0.0, 0.0, 0.0,  1.0, 0.0, 0.0 }, 1.0, 5.0 },
    { { 0.0, 0.0, 0.0, -1.0, 0.0, 0.0 }, 1.0, 5.0 },
    { { 0.0, 0.0, 0.0, -1.0, 0.0, 0.0 }, 2.0, 5.0 },
    { { 0.0, 0.0, 0.0,  3.0, 0.0, 0.0 }, 2.0, 5.0 },
    { { 0.0, 0.0, 0.0, -3.0, 0.0, 0.0 }, 2.0, 5.0 },
    { { 1.0, 0.0, 0.0,  1.0, 0.0, 0.0 }, 2.0, 5.0 },
    { { 1.0, 0.0, 0.0,  1.0, 5.0, 0.0 }, 2.0, 5.0 },
    { { 1.0, 0.0, 0.0,  1.0, 0.0, 5.0 }, 2.0, 5.0 },
};

static void get_expected_metrics(IDWriteFontFace *fontface, struct compatmetrics_test *ptr,
    DWRITE_FONT_METRICS *expected)
{
    HRESULT hr;

    memset(expected, 0, sizeof(*expected));
    hr = IDWriteFontFace_GetGdiCompatibleMetrics(fontface, ptr->ppdip * fabsf(ptr->m.m22) * ptr->emsize, 1.0, NULL, expected);
    ok(hr == S_OK, "got %08x\n", hr);
}

static void test_gdicompat_metrics(IDWriteFontFace *face)
{
    IDWriteFontFace1 *fontface1 = NULL;
    HRESULT hr;
    DWRITE_FONT_METRICS design_metrics, comp_metrics;
    DWRITE_FONT_METRICS1 design_metrics1, expected;
    FLOAT emsize, scale;
    int ascent, descent;
    const struct VDMX_Header *vdmx;
    UINT32 vdmx_len;
    void *vdmx_ctx;
    BOOL exists;
    const struct VDMX_group *vdmx_group = NULL;
    int i;

    hr = IDWriteFontFace_QueryInterface(face, &IID_IDWriteFontFace1, (void**)&fontface1);
    if (hr != S_OK)
        win_skip("gdi compatible DWRITE_FONT_METRICS1 are not supported.\n");

    if (fontface1) {
        IDWriteFontFace1_GetMetrics(fontface1, &design_metrics1);
        memcpy(&design_metrics, &design_metrics1, sizeof(design_metrics));
    }
    else
        IDWriteFontFace_GetMetrics(face, &design_metrics);

    hr = IDWriteFontFace_TryGetFontTable(face, MS_VDMX_TAG, (const void **)&vdmx,
                                         &vdmx_len, &vdmx_ctx, &exists);
    if (hr != S_OK || !exists)
        vdmx = NULL;
    else
        vdmx_group = find_vdmx_group(vdmx);

    /* negative emsize */
    memset(&comp_metrics, 0xcc, sizeof(comp_metrics));
    memset(&expected, 0, sizeof(expected));
    hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, -10.0, 1.0, NULL, &comp_metrics);
    ok(hr == E_INVALIDARG, "got %08x\n", hr);
    test_metrics_cmp(0.0, &comp_metrics, &expected);

    /* zero emsize */
    memset(&comp_metrics, 0xcc, sizeof(comp_metrics));
    memset(&expected, 0, sizeof(expected));
    hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, 0.0, 1.0, NULL, &comp_metrics);
    ok(hr == E_INVALIDARG, "got %08x\n", hr);
    test_metrics_cmp(0.0, &comp_metrics, &expected);

    /* zero pixels per dip */
    memset(&comp_metrics, 0xcc, sizeof(comp_metrics));
    memset(&expected, 0, sizeof(expected));
    hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, 5.0, 0.0, NULL, &comp_metrics);
    ok(hr == E_INVALIDARG, "got %08x\n", hr);
    test_metrics_cmp(5.0, &comp_metrics, &expected);

    memset(&comp_metrics, 0xcc, sizeof(comp_metrics));
    hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, 5.0, -1.0, NULL, &comp_metrics);
    ok(hr == E_INVALIDARG, "got %08x\n", hr);
    test_metrics_cmp(5.0, &comp_metrics, &expected);

    for (i = 0; i < sizeof(compatmetrics_tests)/sizeof(compatmetrics_tests[0]); i++) {
        struct compatmetrics_test *ptr = &compatmetrics_tests[i];

        get_expected_metrics(face, ptr, (DWRITE_FONT_METRICS*)&expected);
        hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, ptr->emsize, ptr->ppdip, &ptr->m, &comp_metrics);
        ok(hr == S_OK, "got %08x\n", hr);
        test_metrics_cmp(ptr->emsize, &comp_metrics, &expected);
    }

    for (emsize = 5; emsize <= design_metrics.designUnitsPerEm; emsize++)
    {
        DWRITE_FONT_METRICS1 comp_metrics1, expected;

        if (fontface1) {
            hr = IDWriteFontFace1_GetGdiCompatibleMetrics(fontface1, emsize, 1.0, NULL, &comp_metrics1);
            ok(hr == S_OK, "got %08x\n", hr);
        }
        else {
            hr = IDWriteFontFace_GetGdiCompatibleMetrics(face, emsize, 1.0, NULL, &comp_metrics);
            ok(hr == S_OK, "got %08x\n", hr);
        }

        scale = emsize / design_metrics.designUnitsPerEm;
        if (!get_vdmx_size(vdmx_group, emsize, &ascent, &descent))
        {
            ascent = round(design_metrics.ascent * scale);
            descent = round(design_metrics.descent * scale);
        }

        expected.designUnitsPerEm = design_metrics.designUnitsPerEm;
        expected.ascent = round(ascent / scale );
        expected.descent = round(descent / scale );
        expected.lineGap = round(round(design_metrics.lineGap * scale) / scale);
        expected.capHeight = round(round(design_metrics.capHeight * scale) / scale);
        expected.xHeight = round(round(design_metrics.xHeight * scale) / scale);
        expected.underlinePosition = round(round(design_metrics.underlinePosition * scale) / scale);
        expected.underlineThickness = round(round(design_metrics.underlineThickness * scale) / scale);
        expected.strikethroughPosition = round(round(design_metrics.strikethroughPosition * scale) / scale);
        expected.strikethroughThickness = round(round(design_metrics.strikethroughThickness * scale) / scale);

        if (fontface1) {
            expected.glyphBoxLeft = round(round(design_metrics1.glyphBoxLeft * scale) / scale);

        if (0) { /* those two fail on Tahoma and Win7 */
            expected.glyphBoxTop = round(round(design_metrics1.glyphBoxTop * scale) / scale);
            expected.glyphBoxRight = round(round(design_metrics1.glyphBoxRight * scale) / scale);
        }
            expected.glyphBoxBottom = round(round(design_metrics1.glyphBoxBottom * scale) / scale);
            expected.subscriptPositionX = round(round(design_metrics1.subscriptPositionX * scale) / scale);
            expected.subscriptPositionY = round(round(design_metrics1.subscriptPositionY * scale) / scale);
            expected.subscriptSizeX = round(round(design_metrics1.subscriptSizeX * scale) / scale);
            expected.subscriptSizeY = round(round(design_metrics1.subscriptSizeY * scale) / scale);
            expected.superscriptPositionX = round(round(design_metrics1.superscriptPositionX * scale) / scale);
        if (0) /* this fails for 3 emsizes, Tahoma from [5, 2048] range */ {
            expected.superscriptPositionY = round(round(design_metrics1.superscriptPositionY * scale) / scale);
        }
            expected.superscriptSizeX = round(round(design_metrics1.superscriptSizeX * scale) / scale);
            expected.superscriptSizeY = round(round(design_metrics1.superscriptSizeY * scale) / scale);
            expected.hasTypographicMetrics = design_metrics1.hasTypographicMetrics;

            test_metrics1_cmp(emsize, &comp_metrics1, &expected);
        }
        else
            test_metrics_cmp(emsize, &comp_metrics, &expected);

    }

    if (fontface1)
        IDWriteFontFace1_Release(fontface1);
    if (vdmx) IDWriteFontFace_ReleaseFontTable(face, vdmx_ctx);
}

static void test_GetGdiCompatibleMetrics(void)
{
    IDWriteFactory *factory;
    IDWriteFont *font;
    IDWriteFontFace *fontface;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    font = get_font(factory, tahomaW, DWRITE_FONT_STYLE_NORMAL);
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);
    test_gdicompat_metrics(fontface);
    IDWriteFontFace_Release(fontface);

    font = get_font(factory, arialW, DWRITE_FONT_STYLE_NORMAL);
    if (!font)
        skip("Skipping tests with Arial\n");
    else
    {
        hr = IDWriteFont_CreateFontFace(font, &fontface);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFont_Release(font);

        test_gdicompat_metrics(fontface);
        IDWriteFontFace_Release(fontface);
    }

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void get_expected_panose(IDWriteFont1 *font, DWRITE_PANOSE *panose)
{
    IDWriteFontFace *fontface;
    const TT_OS2_V2 *tt_os2;
    void *os2_context;
    UINT32 size;
    BOOL exists;
    HRESULT hr;

    memset(panose, 0, sizeof(*panose));

    hr = IDWriteFont1_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_0S2_TAG, (const void **)&tt_os2, &size, &os2_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    if (tt_os2) {
        memcpy(panose, &tt_os2->panose, sizeof(*panose));
        IDWriteFontFace_ReleaseFontTable(fontface, os2_context);
    }

    IDWriteFontFace_Release(fontface);
}

static void test_GetPanose(void)
{
    IDWriteFontCollection *syscollection;
    IDWriteFactory *factory;
    IDWriteFont1 *font1;
    IDWriteFont *font;
    UINT count, i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_NORMAL);

    hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont1, (void **)&font1);
    IDWriteFont_Release(font);

    if (FAILED(hr)) {
        ref = IDWriteFactory_Release(factory);
        ok(ref == 0, "factory not released, %u\n", ref);
        win_skip("GetPanose() is not supported.\n");
        return;
    }
    IDWriteFont1_Release(font1);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        DWRITE_PANOSE panose, expected_panose;
        IDWriteLocalizedStrings *names;
        IDWriteFontFace3 *fontface3;
        IDWriteFontFace *fontface;
        IDWriteFontFamily *family;
        IDWriteFont1 *font1;
        IDWriteFont *font;
        WCHAR nameW[256];

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont1, (void **)&font1);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFont_Release(font);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, nameW, sizeof(nameW)/sizeof(nameW[0]));

        IDWriteLocalizedStrings_Release(names);

        IDWriteFont1_GetPanose(font1, &panose);
        get_expected_panose(font1, &expected_panose);

        ok(panose.values[0] == expected_panose.values[0], "%s: values[0] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[0], expected_panose.values[0]);
        ok(panose.values[1] == expected_panose.values[1], "%s: values[1] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[1], expected_panose.values[1]);
        ok(panose.values[2] == expected_panose.values[2], "%s: values[2] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[2], expected_panose.values[2]);
        ok(panose.values[3] == expected_panose.values[3], "%s: values[3] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[3], expected_panose.values[3]);
        ok(panose.values[4] == expected_panose.values[4], "%s: values[4] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[4], expected_panose.values[4]);
        ok(panose.values[5] == expected_panose.values[5], "%s: values[5] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[5], expected_panose.values[5]);
        ok(panose.values[6] == expected_panose.values[6], "%s: values[6] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[6], expected_panose.values[6]);
        ok(panose.values[7] == expected_panose.values[7], "%s: values[7] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[7], expected_panose.values[7]);
        ok(panose.values[8] == expected_panose.values[8], "%s: values[8] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[8], expected_panose.values[8]);
        ok(panose.values[9] == expected_panose.values[9], "%s: values[9] %#x, expected %#x.\n", wine_dbgstr_w(nameW),
            panose.values[9], expected_panose.values[9]);

        hr = IDWriteFont1_CreateFontFace(font1, &fontface);
        ok(hr == S_OK, "Failed to create a font face, %#x.\n", hr);
        if (IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace3, (void **)&fontface3) == S_OK) {
            ok(!memcmp(&panose, &expected_panose, sizeof(panose)), "%s: Unexpected panose from font face.\n",
                wine_dbgstr_w(nameW));
            IDWriteFontFace3_Release(fontface3);
        }
        IDWriteFontFace_Release(fontface);

        IDWriteFont1_Release(font1);
        IDWriteFontFamily_Release(family);
    }

    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static INT32 get_gdi_font_advance(HDC hdc, FLOAT emsize)
{
    LOGFONTW logfont;
    HFONT hfont;
    BOOL ret;
    ABC abc;

    memset(&logfont, 0, sizeof(logfont));
    logfont.lfHeight = (LONG)-emsize;
    logfont.lfWeight = FW_NORMAL;
    logfont.lfQuality = CLEARTYPE_QUALITY;
    lstrcpyW(logfont.lfFaceName, tahomaW);

    hfont = CreateFontIndirectW(&logfont);
    SelectObject(hdc, hfont);

    ret = GetCharABCWidthsW(hdc, 'A', 'A', &abc);
    ok(ret, "got %d\n", ret);

    DeleteObject(hfont);

    return abc.abcA + abc.abcB + abc.abcC;
}

static void test_GetGdiCompatibleGlyphAdvances(void)
{
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFont *font;
    HRESULT hr;
    HDC hdc;
    UINT32 codepoint;
    UINT16 glyph;
    FLOAT emsize;
    DWRITE_FONT_METRICS1 fm;
    INT32 advance;
    ULONG ref;

    factory = create_factory();
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_NORMAL);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void **)&fontface1);
    IDWriteFontFace_Release(fontface);

    if (hr != S_OK) {
        ref = IDWriteFactory_Release(factory);
        ok(ref == 0, "factory not released, %u\n", ref);
        win_skip("GetGdiCompatibleGlyphAdvances() is not supported\n");
        return;
    }

    codepoint = 'A';
    glyph = 0;
    hr = IDWriteFontFace1_GetGlyphIndices(fontface1, &codepoint, 1, &glyph);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyph > 0, "got %u\n", glyph);

    /* zero emsize */
    advance = 1;
    hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, 0.0,
        1.0, NULL, FALSE, FALSE, 1, &glyph, &advance);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(advance == 0, "got %d\n", advance);

    /* negative emsize */
    advance = 1;
    hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, -1.0,
        1.0, NULL, FALSE, FALSE, 1, &glyph, &advance);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(advance == 0, "got %d\n", advance);

    /* zero ppdip */
    advance = 1;
    hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, 1.0,
        0.0, NULL, FALSE, FALSE, 1, &glyph, &advance);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(advance == 0, "got %d\n", advance);

    /* negative ppdip */
    advance = 1;
    hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, 1.0,
        -1.0, NULL, FALSE, FALSE, 1, &glyph, &advance);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(advance == 0, "got %d\n", advance);

    IDWriteFontFace1_GetMetrics(fontface1, &fm);

    hdc = CreateCompatibleDC(0);

    for (emsize = 1.0; emsize <= fm.designUnitsPerEm; emsize += 1.0) {
        INT32 gdi_advance;

        gdi_advance = get_gdi_font_advance(hdc, emsize);
        hr = IDWriteFontFace1_GetGdiCompatibleGlyphAdvances(fontface1, emsize,
            1.0, NULL, FALSE, FALSE, 1, &glyph, &advance);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        /* advance is in design units */
        advance = (int)floorf(emsize * advance / fm.designUnitsPerEm + 0.5f);
        ok((advance - gdi_advance) <= 2, "%.0f: got advance %d, expected %d\n", emsize, advance, gdi_advance);
    }

    DeleteObject(hdc);

    IDWriteFontFace1_Release(fontface1);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static WORD get_gasp_flags(IDWriteFontFace *fontface, FLOAT emsize, FLOAT ppdip)
{
    WORD num_recs, version;
    const WORD *ptr;
    WORD flags = 0;
    UINT32 size;
    BOOL exists;
    void *ctxt;
    HRESULT hr;

    emsize *= ppdip;

    exists = FALSE;
    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_GASP_TAG,
        (const void**)&ptr, &size, &ctxt, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    if (!exists)
        goto done;

    version  = GET_BE_WORD( *ptr++ );
    num_recs = GET_BE_WORD( *ptr++ );
    if (version > 1 || size < (num_recs * 2 + 2) * sizeof(WORD)) {
        ok(0, "unsupported gasp table: ver %d size %d recs %d\n", version, size, num_recs);
        goto done;
    }

    while (num_recs--)
    {
        flags = GET_BE_WORD( *(ptr + 1) );
        if (emsize <= GET_BE_WORD( *ptr )) break;
        ptr += 2;
    }

done:
    IDWriteFontFace_ReleaseFontTable(fontface, ctxt);
    return flags;
}

#define GASP_GRIDFIT             0x0001
#define GASP_DOGRAY              0x0002
#define GASP_SYMMETRIC_GRIDFIT   0x0004
#define GASP_SYMMETRIC_SMOOTHING 0x0008

static BOOL g_is_vista;
static DWRITE_RENDERING_MODE get_expected_rendering_mode(FLOAT emsize, WORD gasp, DWRITE_MEASURING_MODE mode,
    DWRITE_OUTLINE_THRESHOLD threshold)
{
    static const FLOAT aa_threshold = 100.0f;
    static const FLOAT a_threshold = 350.0f;
    static const FLOAT naturalemsize = 20.0f;
    FLOAT v;

    /* outline threshold */
    if (g_is_vista)
        v = mode == DWRITE_MEASURING_MODE_NATURAL ? aa_threshold : a_threshold;
    else
        v = threshold == DWRITE_OUTLINE_THRESHOLD_ANTIALIASED ? aa_threshold : a_threshold;

    if (emsize >= v)
        return DWRITE_RENDERING_MODE_OUTLINE;

    switch (mode)
    {
    case DWRITE_MEASURING_MODE_NATURAL:
        if (!(gasp & GASP_SYMMETRIC_SMOOTHING) && (emsize <= naturalemsize))
            return DWRITE_RENDERING_MODE_NATURAL;
        else
            return DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
    case DWRITE_MEASURING_MODE_GDI_CLASSIC:
        return DWRITE_RENDERING_MODE_GDI_CLASSIC;
    case DWRITE_MEASURING_MODE_GDI_NATURAL:
        return DWRITE_RENDERING_MODE_GDI_NATURAL;
    default:
        ;
    }

    /* should be unreachable */
    return DWRITE_RENDERING_MODE_DEFAULT;
}

static DWRITE_GRID_FIT_MODE get_expected_gridfit_mode(FLOAT emsize, WORD gasp, DWRITE_MEASURING_MODE mode,
    DWRITE_OUTLINE_THRESHOLD threshold)
{
    static const FLOAT aa_threshold = 100.0f;
    static const FLOAT a_threshold = 350.0f;
    FLOAT v;

    v = threshold == DWRITE_OUTLINE_THRESHOLD_ANTIALIASED ? aa_threshold : a_threshold;
    if (emsize >= v)
        return DWRITE_GRID_FIT_MODE_DISABLED;

    if (mode == DWRITE_MEASURING_MODE_GDI_CLASSIC || mode == DWRITE_MEASURING_MODE_GDI_NATURAL)
        return DWRITE_GRID_FIT_MODE_ENABLED;

    return (gasp & (GASP_GRIDFIT|GASP_SYMMETRIC_GRIDFIT)) ? DWRITE_GRID_FIT_MODE_ENABLED : DWRITE_GRID_FIT_MODE_DISABLED;
}

struct recommendedmode_test
{
    DWRITE_MEASURING_MODE measuring;
    DWRITE_OUTLINE_THRESHOLD threshold;
};

static const struct recommendedmode_test recmode_tests[] = {
    { DWRITE_MEASURING_MODE_NATURAL,     DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
    { DWRITE_MEASURING_MODE_GDI_CLASSIC, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
    { DWRITE_MEASURING_MODE_GDI_NATURAL, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
};

static const struct recommendedmode_test recmode_tests1[] = {
    { DWRITE_MEASURING_MODE_NATURAL,     DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
    { DWRITE_MEASURING_MODE_GDI_CLASSIC, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
    { DWRITE_MEASURING_MODE_GDI_NATURAL, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED },
    { DWRITE_MEASURING_MODE_NATURAL,     DWRITE_OUTLINE_THRESHOLD_ALIASED },
    { DWRITE_MEASURING_MODE_GDI_CLASSIC, DWRITE_OUTLINE_THRESHOLD_ALIASED },
    { DWRITE_MEASURING_MODE_GDI_NATURAL, DWRITE_OUTLINE_THRESHOLD_ALIASED },
};

static void test_GetRecommendedRenderingMode(void)
{
    IDWriteRenderingParams *params;
    IDWriteFontFace3 *fontface3;
    IDWriteFontFace2 *fontface2;
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace  *fontface;
    DWRITE_RENDERING_MODE mode;
    IDWriteFactory *factory;
    FLOAT emsize;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    fontface = create_fontface(factory);

    fontface1 = NULL;
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    if (hr != S_OK)
        win_skip("IDWriteFontFace1::GetRecommendedRenderingMode() is not supported.\n");

    fontface2 = NULL;
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace2, (void**)&fontface2);
    if (hr != S_OK)
        win_skip("IDWriteFontFace2::GetRecommendedRenderingMode() is not supported.\n");

    fontface3 = NULL;
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace3, (void**)&fontface3);
    if (hr != S_OK)
        win_skip("IDWriteFontFace3::GetRecommendedRenderingMode() is not supported.\n");

    if (0) /* crashes on native */
        hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, 3.0, 1.0,
            DWRITE_MEASURING_MODE_GDI_CLASSIC, NULL, NULL);

    mode = 10;
    hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, 3.0, 1.0,
        DWRITE_MEASURING_MODE_GDI_CLASSIC, NULL, &mode);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(mode == DWRITE_RENDERING_MODE_DEFAULT, "got %d\n", mode);

    hr = IDWriteFactory_CreateRenderingParams(factory, &params);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* detect old dwrite version, that is using higher threshold value */
    g_is_vista = fontface1 == NULL;

    for (emsize = 1.0; emsize < 500.0; emsize += 1.0) {
        DWRITE_RENDERING_MODE expected;
        FLOAT ppdip;
        WORD gasp;
        int i;

        for (i = 0; i < sizeof(recmode_tests)/sizeof(recmode_tests[0]); i++) {
            ppdip = 1.0f;
            mode = 10;
            gasp = get_gasp_flags(fontface, emsize, ppdip);
            expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests[i].measuring, recmode_tests[i].threshold);
            hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, emsize, ppdip, recmode_tests[i].measuring, params, &mode);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(mode == expected, "%.2f/%d: got %d, ppdip %f, flags 0x%04x, expected %d\n", emsize, i, mode, ppdip, gasp, expected);

            /* some ppdip variants */
            ppdip = 0.5f;
            mode = 10;
            gasp = get_gasp_flags(fontface, emsize, ppdip);
            expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests[i].measuring, recmode_tests[i].threshold);
            hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, emsize, ppdip, recmode_tests[i].measuring, params, &mode);
            ok(hr == S_OK, "got 0x%08x\n", hr);
            ok(mode == expected, "%.2f/%d: got %d, ppdip %f, flags 0x%04x, expected %d\n", emsize, i, mode, ppdip, gasp, expected);

            /* Only test larger sizes to workaround Win7 differences, where unscaled natural emsize threshold is used;
               Win8 and Win10 handle this as expected. */
            if (emsize > 20.0f) {
                ppdip = 1.5f;
                mode = 10;
                gasp = get_gasp_flags(fontface, emsize, ppdip);
                expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests[i].measuring, recmode_tests[i].threshold);
                hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, emsize, ppdip, recmode_tests[i].measuring, params, &mode);
                ok(hr == S_OK, "got 0x%08x\n", hr);
                ok(mode == expected, "%.2f/%d: got %d, ppdip %f, flags 0x%04x, expected %d\n", emsize, i, mode, ppdip, gasp, expected);

                ppdip = 2.0f;
                mode = 10;
                gasp = get_gasp_flags(fontface, emsize, ppdip);
                expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests[i].measuring, recmode_tests[i].threshold);
                hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, emsize, ppdip, recmode_tests[i].measuring, params, &mode);
                ok(hr == S_OK, "got 0x%08x\n", hr);
                ok(mode == expected, "%.2f/%d: got %d, ppdip %f, flags 0x%04x, expected %d\n", emsize, i, mode, ppdip, gasp, expected);
            }
        }

        /* IDWriteFontFace1 offers another variant of this method */
        if (fontface1) {
            for (i = 0; i < sizeof(recmode_tests1)/sizeof(recmode_tests1[0]); i++) {
                FLOAT dpi;

                ppdip = 1.0f;
                dpi = 96.0f * ppdip;
                mode = 10;
                gasp = get_gasp_flags(fontface, emsize, ppdip);
                expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi, dpi,
                    NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                ok(hr == S_OK, "got 0x%08x\n", hr);
                ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                /* Only test larger sizes to workaround Win7 differences, where unscaled natural emsize threshold is used;
                   Win8 and Win10 handle this as expected. */
                if (emsize > 20.0f) {
                    ppdip = 2.0f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi, dpi,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                    ppdip = 0.5f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi, dpi,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                    /* try different dpis for X and Y direction */
                    ppdip = 1.0f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi * 0.5f, dpi,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                    ppdip = 1.0f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi, dpi * 0.5f,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                    ppdip = 2.0f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi * 0.5f, dpi,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);

                    ppdip = 2.0f;
                    dpi = 96.0f * ppdip;
                    mode = 10;
                    gasp = get_gasp_flags(fontface, emsize, ppdip);
                    expected = get_expected_rendering_mode(emsize * ppdip, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                    hr = IDWriteFontFace1_GetRecommendedRenderingMode(fontface1, emsize, dpi, dpi * 0.5f,
                        NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, &mode);
                    ok(hr == S_OK, "got 0x%08x\n", hr);
                    ok(mode == expected, "%.2f/%d: got %d, dpi %f, flags 0x%04x, expected %d\n", emsize, i, mode, dpi, gasp, expected);
                }
            }
        }

        /* IDWriteFontFace2 - another one */
        if (fontface2) {
            DWRITE_GRID_FIT_MODE gridfit, expected_gridfit;

            gasp = get_gasp_flags(fontface, emsize, 1.0f);
            for (i = 0; i < sizeof(recmode_tests1)/sizeof(recmode_tests1[0]); i++) {
                mode = 10;
                expected = get_expected_rendering_mode(emsize, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                expected_gridfit = get_expected_gridfit_mode(emsize, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                hr = IDWriteFontFace2_GetRecommendedRenderingMode(fontface2, emsize, 96.0f, 96.0f,
                    NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, params, &mode, &gridfit);
                ok(hr == S_OK, "got 0x%08x\n", hr);
                ok(mode == expected, "%.2f: got %d, flags 0x%04x, expected %d\n", emsize, mode, gasp, expected);
                ok(gridfit == expected_gridfit, "%.2f/%d: gridfit: got %d, flags 0x%04x, expected %d\n", emsize, i, gridfit,
                    gasp, expected_gridfit);
            }
        }

        /* IDWriteFontFace3 - and another one */
        if (fontface3) {
            DWRITE_GRID_FIT_MODE gridfit, expected_gridfit;
            DWRITE_RENDERING_MODE1 mode1, expected1;

            gasp = get_gasp_flags(fontface, emsize, 1.0f);
            for (i = 0; i < sizeof(recmode_tests1)/sizeof(recmode_tests1[0]); i++) {
                mode1 = 10;
                expected1 = get_expected_rendering_mode(emsize, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                expected_gridfit = get_expected_gridfit_mode(emsize, gasp, recmode_tests1[i].measuring, recmode_tests1[i].threshold);
                hr = IDWriteFontFace3_GetRecommendedRenderingMode(fontface3, emsize, 96.0f, 96.0f,
                    NULL, FALSE, recmode_tests1[i].threshold, recmode_tests1[i].measuring, params, &mode1, &gridfit);
                ok(hr == S_OK, "got 0x%08x\n", hr);
                ok(mode1 == expected1, "%.2f: got %d, flags 0x%04x, expected %d\n", emsize, mode1, gasp, expected1);
                ok(gridfit == expected_gridfit, "%.2f/%d: gridfit: got %d, flags 0x%04x, expected %d\n", emsize, i, gridfit,
                    gasp, expected_gridfit);
            }
        }
    }

    IDWriteRenderingParams_Release(params);

    /* test how parameters override returned modes */
    hr = IDWriteFactory_CreateCustomRenderingParams(factory, 1.0, 0.0, 0.0, DWRITE_PIXEL_GEOMETRY_FLAT,
        DWRITE_RENDERING_MODE_GDI_CLASSIC, &params);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    mode = 10;
    hr = IDWriteFontFace_GetRecommendedRenderingMode(fontface, 500.0, 1.0, DWRITE_MEASURING_MODE_NATURAL, params, &mode);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(mode == DWRITE_RENDERING_MODE_GDI_CLASSIC, "got %d\n", mode);

    IDWriteRenderingParams_Release(params);

    if (fontface2) {
        IDWriteRenderingParams2 *params2;
        IDWriteFactory2 *factory2;
        DWRITE_GRID_FIT_MODE gridfit;

        hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory2, (void**)&factory2);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFactory2_CreateCustomRenderingParams(factory2, 1.0, 0.0, 0.0, 0.5, DWRITE_PIXEL_GEOMETRY_FLAT,
            DWRITE_RENDERING_MODE_OUTLINE, DWRITE_GRID_FIT_MODE_ENABLED, &params2);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        mode = 10;
        gridfit = 10;
        hr = IDWriteFontFace2_GetRecommendedRenderingMode(fontface2, 5.0, 96.0, 96.0,
            NULL, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
            NULL, &mode, &gridfit);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(mode == DWRITE_RENDERING_MODE_GDI_CLASSIC, "got %d\n", mode);
        ok(gridfit == DWRITE_GRID_FIT_MODE_ENABLED, "got %d\n", gridfit);

        mode = 10;
        gridfit = 10;
        hr = IDWriteFontFace2_GetRecommendedRenderingMode(fontface2, 5.0, 96.0, 96.0,
            NULL, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
            (IDWriteRenderingParams*)params2, &mode, &gridfit);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(mode == DWRITE_RENDERING_MODE_OUTLINE, "got %d\n", mode);
        ok(gridfit == DWRITE_GRID_FIT_MODE_ENABLED, "got %d\n", gridfit);

        IDWriteRenderingParams2_Release(params2);
        IDWriteFactory2_Release(factory2);
    }

    if (fontface3) {
        IDWriteRenderingParams3 *params3;
        IDWriteRenderingParams2 *params2;
        IDWriteRenderingParams *params;
        IDWriteFactory3 *factory3;
        DWRITE_GRID_FIT_MODE gridfit;
        DWRITE_RENDERING_MODE1 mode1;

        hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void**)&factory3);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFactory3_CreateCustomRenderingParams(factory3, 1.0f, 0.0f, 0.0f, 0.5f, DWRITE_PIXEL_GEOMETRY_FLAT,
            DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED, DWRITE_GRID_FIT_MODE_ENABLED, &params3);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        mode1 = IDWriteRenderingParams3_GetRenderingMode1(params3);
        ok(mode1 == DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED, "got %d\n", mode1);

        mode = IDWriteRenderingParams3_GetRenderingMode(params3);
        ok(mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, "got %d\n", mode);

        hr = IDWriteRenderingParams3_QueryInterface(params3, &IID_IDWriteRenderingParams, (void**)&params);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(params == (IDWriteRenderingParams*)params3, "got %p, %p\n", params3, params);
        mode = IDWriteRenderingParams_GetRenderingMode(params);
        ok(mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, "got %d\n", mode);
        IDWriteRenderingParams_Release(params);

        hr = IDWriteRenderingParams3_QueryInterface(params3, &IID_IDWriteRenderingParams2, (void**)&params2);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(params2 == (IDWriteRenderingParams2*)params3, "got %p, %p\n", params3, params2);
        mode = IDWriteRenderingParams2_GetRenderingMode(params2);
        ok(mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, "got %d\n", mode);
        IDWriteRenderingParams2_Release(params2);

        mode = 10;
        gridfit = 10;
        hr = IDWriteFontFace2_GetRecommendedRenderingMode(fontface2, 5.0f, 96.0f, 96.0f,
            NULL, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
            NULL, &mode, &gridfit);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(mode == DWRITE_RENDERING_MODE_GDI_CLASSIC, "got %d\n", mode);
        ok(gridfit == DWRITE_GRID_FIT_MODE_ENABLED, "got %d\n", gridfit);

        mode = 10;
        gridfit = 10;
        hr = IDWriteFontFace2_GetRecommendedRenderingMode(fontface2, 5.0f, 96.0f, 96.0f,
            NULL, FALSE, DWRITE_OUTLINE_THRESHOLD_ANTIALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
            (IDWriteRenderingParams*)params3, &mode, &gridfit);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(mode == DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC, "got %d\n", mode);
        ok(gridfit == DWRITE_GRID_FIT_MODE_ENABLED, "got %d\n", gridfit);

        IDWriteRenderingParams3_Release(params3);
        IDWriteFactory3_Release(factory3);
    }

    if (fontface3)
        IDWriteFontFace3_Release(fontface3);
    if (fontface2)
        IDWriteFontFace2_Release(fontface2);
    if (fontface1)
        IDWriteFontFace1_Release(fontface1);
    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static inline BOOL float_eq(FLOAT left, FLOAT right)
{
    int x = *(int *)&left;
    int y = *(int *)&right;

    if (x < 0)
        x = INT_MIN - x;
    if (y < 0)
        y = INT_MIN - y;

    return abs(x - y) <= 8;
}

static void test_GetAlphaBlendParams(void)
{
    static const DWRITE_RENDERING_MODE rendermodes[] = {
        DWRITE_RENDERING_MODE_ALIASED,
        DWRITE_RENDERING_MODE_GDI_CLASSIC,
        DWRITE_RENDERING_MODE_GDI_NATURAL,
        DWRITE_RENDERING_MODE_NATURAL,
        DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,
    };

    IDWriteGlyphRunAnalysis *analysis;
    FLOAT gamma, contrast, ctlevel;
    IDWriteRenderingParams *params;
    DWRITE_GLYPH_METRICS metrics;
    DWRITE_GLYPH_OFFSET offset;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    FLOAT advance, expected_gdi_gamma;
    UINT value = 0;
    UINT16 glyph;
    UINT32 ch, i;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    factory = create_factory();
    fontface = create_fontface(factory);

    ch = 'A';
    glyph = 0;
    hr = IDWriteFontFace_GetGlyphIndices(fontface, &ch, 1, &glyph);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyph > 0, "got %u\n", glyph);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, &glyph, 1, &metrics, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    advance = metrics.advanceWidth;

    offset.advanceOffset = 0.0;
    offset.ascenderOffset = 0.0;

    run.fontFace = fontface;
    run.fontEmSize = 24.0;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    hr = IDWriteFactory_CreateCustomRenderingParams(factory, 0.9, 0.3, 0.1, DWRITE_PIXEL_GEOMETRY_RGB,
        DWRITE_RENDERING_MODE_DEFAULT, &params);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    value = 0;
    ret = SystemParametersInfoW(SPI_GETFONTSMOOTHINGCONTRAST, 0, &value, 0);
    ok(ret, "got %d\n", ret);
    expected_gdi_gamma = (FLOAT)(value / 1000.0);

    for (i = 0; i < sizeof(rendermodes)/sizeof(rendermodes[0]); i++) {
        hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
            rendermodes[i], DWRITE_MEASURING_MODE_NATURAL,
            0.0, 0.0, &analysis);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        gamma = contrast = ctlevel = -1.0;
        hr = IDWriteGlyphRunAnalysis_GetAlphaBlendParams(analysis, NULL, &gamma, &contrast, &ctlevel);
        ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
        ok(gamma == -1.0, "got %.2f\n", gamma);
        ok(contrast == -1.0, "got %.2f\n", contrast);
        ok(ctlevel == -1.0, "got %.2f\n", ctlevel);

        gamma = contrast = ctlevel = -1.0;
        hr = IDWriteGlyphRunAnalysis_GetAlphaBlendParams(analysis, params, &gamma, &contrast, &ctlevel);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        if (rendermodes[i] == DWRITE_RENDERING_MODE_GDI_CLASSIC || rendermodes[i] == DWRITE_RENDERING_MODE_GDI_NATURAL) {
            ok(float_eq(gamma, expected_gdi_gamma), "got %.2f, expected %.2f\n", gamma, expected_gdi_gamma);
            ok(contrast == 0.0f, "got %.2f\n", contrast);
            ok(ctlevel == 1.0f, "got %.2f\n", ctlevel);
        }
        else {
            ok(gamma == 0.9f, "got %.2f\n", gamma);
            ok(contrast == 0.3f, "got %.2f\n", contrast);
            ok(ctlevel == 0.1f, "got %.2f\n", ctlevel);
        }

        IDWriteGlyphRunAnalysis_Release(analysis);
    }

    IDWriteRenderingParams_Release(params);
    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateAlphaTexture(void)
{
    IDWriteGlyphRunAnalysis *analysis;
    DWRITE_GLYPH_METRICS metrics;
    DWRITE_GLYPH_OFFSET offset;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    UINT32 ch, size;
    BYTE buff[1024];
    RECT bounds, r;
    FLOAT advance;
    UINT16 glyph;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    fontface = create_fontface(factory);

    ch = 'A';
    glyph = 0;
    hr = IDWriteFontFace_GetGlyphIndices(fontface, &ch, 1, &glyph);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(glyph > 0, "got %u\n", glyph);

    hr = IDWriteFontFace_GetDesignGlyphMetrics(fontface, &glyph, 1, &metrics, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    advance = metrics.advanceWidth;

    offset.advanceOffset = 0.0;
    offset.ascenderOffset = 0.0;

    run.fontFace = fontface;
    run.fontEmSize = 24.0;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&bounds);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&bounds), "got empty rect\n");
    size = (bounds.right - bounds.left)*(bounds.bottom - bounds.top)*3;
    ok(sizeof(buff) >= size, "required %u\n", size);

    /* invalid type value */
    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1+1, &bounds, buff, sizeof(buff));
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &bounds, buff, 2);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    /* vista version allows texture type mismatch, mark it broken for now */
    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &bounds, buff, sizeof(buff));
    ok(hr == DWRITE_E_UNSUPPORTEDOPERATION || broken(hr == S_OK), "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf || broken(buff[0] == 0), "got %1x\n", buff[0]);

    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, buff, size-1);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    IDWriteGlyphRunAnalysis_Release(analysis);

    hr = IDWriteFactory_CreateGlyphRunAnalysis(factory, &run, 1.0, NULL,
        DWRITE_RENDERING_MODE_ALIASED, DWRITE_MEASURING_MODE_GDI_CLASSIC,
        0.0, 0.0, &analysis);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    SetRectEmpty(&bounds);
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(analysis, DWRITE_TEXTURE_ALIASED_1x1, &bounds);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(!IsRectEmpty(&bounds), "got empty rect\n");
    size = (bounds.right - bounds.left)*(bounds.bottom - bounds.top);
    ok(sizeof(buff) >= size, "required %u\n", size);

    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, NULL, buff, sizeof(buff));
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, NULL, NULL, sizeof(buff));
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, NULL, buff, 0);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    /* buffer size is not enough */
    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &bounds, buff, size-1);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    /* request texture for rectangle that doesn't intersect */
    memset(buff, 0xcf, sizeof(buff));
    r = bounds;
    OffsetRect(&r, (bounds.right - bounds.left)*2, 0);
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &r, buff, sizeof(buff));
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(buff[0] == 0, "got %1x\n", buff[0]);

    memset(buff, 0xcf, sizeof(buff));
    r = bounds;
    OffsetRect(&r, (bounds.right - bounds.left)*2, 0);
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &r, buff, sizeof(buff));
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(buff[0] == 0, "got %1x\n", buff[0]);

    /* request texture for rectangle that doesn't intersect, small buffer */
    memset(buff, 0xcf, sizeof(buff));
    r = bounds;
    OffsetRect(&r, (bounds.right - bounds.left)*2, 0);
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_ALIASED_1x1, &r, buff, size-1);
    ok(hr == E_NOT_SUFFICIENT_BUFFER, "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf, "got %1x\n", buff[0]);

    /* vista version allows texture type mismatch, mark it broken for now */
    memset(buff, 0xcf, sizeof(buff));
    hr = IDWriteGlyphRunAnalysis_CreateAlphaTexture(analysis, DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, buff, sizeof(buff));
    ok(hr == DWRITE_E_UNSUPPORTEDOPERATION || broken(hr == S_OK), "got 0x%08x\n", hr);
    ok(buff[0] == 0xcf || broken(buff[0] == 0), "got %1x\n", buff[0]);

    IDWriteGlyphRunAnalysis_Release(analysis);
    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_IsSymbolFont(void)
{
    static const WCHAR symbolW[] = {'S','y','m','b','o','l',0};
    IDWriteFontCollection *collection;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFont *font;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    factory = create_factory();

    /* Tahoma */
    fontface = create_fontface(factory);
    ret = IDWriteFontFace_IsSymbolFont(fontface);
    ok(!ret, "got %d\n", ret);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontCollection_GetFontFromFontFace(collection, fontface, &font);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    ret = IDWriteFont_IsSymbolFont(font);
    ok(!ret, "got %d\n", ret);

    IDWriteFontCollection_Release(collection);
    IDWriteFont_Release(font);
    IDWriteFontFace_Release(fontface);

    /* Symbol */
    font = get_font(factory, symbolW, DWRITE_FONT_STYLE_NORMAL);
    ret = IDWriteFont_IsSymbolFont(font);
    ok(ret, "got %d\n", ret);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ret = IDWriteFontFace_IsSymbolFont(fontface);
    ok(ret, "got %d\n", ret);
    IDWriteFontFace_Release(fontface);
    IDWriteFont_Release(font);

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

struct CPAL_Header_0
{
    USHORT version;
    USHORT numPaletteEntries;
    USHORT numPalette;
    USHORT numColorRecords;
    ULONG  offsetFirstColorRecord;
    USHORT colorRecordIndices[1];
};

static void test_GetPaletteEntries(void)
{
    IDWriteFontFace2 *fontface2;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    IDWriteFont *font;
    DWRITE_COLOR_F color;
    UINT32 palettecount, entrycount, size, colorrecords;
    void *ctxt;
    const struct CPAL_Header_0 *cpal_header;
    HRESULT hr;
    BOOL exists;
    ULONG ref;

    factory = create_factory();

    /* Tahoma, no color support */
    fontface = create_fontface(factory);
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace2, (void**)&fontface2);
    IDWriteFontFace_Release(fontface);
    if (hr != S_OK) {
        ref = IDWriteFactory_Release(factory);
        ok(ref == 0, "factory not released, %u\n", ref);
        win_skip("GetPaletteEntries() is not supported.\n");
        return;
    }

    hr = IDWriteFontFace2_GetPaletteEntries(fontface2, 0, 0, 1, &color);
    ok(hr == DWRITE_E_NOCOLOR, "got 0x%08x\n", hr);
    IDWriteFontFace2_Release(fontface2);

    /* Segoe UI Emoji, with color support */
    font = get_font(factory, emojiW, DWRITE_FONT_STYLE_NORMAL);
    if (!font) {
        ref = IDWriteFactory_Release(factory);
        ok(ref == 0, "factory not released, %u\n", ref);
        skip("Segoe UI Emoji font not found.\n");
        return;
    }

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace2, (void**)&fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFace_Release(fontface);

    palettecount = IDWriteFontFace2_GetColorPaletteCount(fontface2);
    ok(palettecount >= 1, "got %u\n", palettecount);

    entrycount = IDWriteFontFace2_GetPaletteEntryCount(fontface2);
    ok(entrycount >= 1, "got %u\n", entrycount);

    exists = FALSE;
    hr = IDWriteFontFace2_TryGetFontTable(fontface2, MS_CPAL_TAG, (const void**)&cpal_header, &size, &ctxt, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(exists, "got %d\n", exists);
    colorrecords = GET_BE_WORD(cpal_header->numColorRecords);
    ok(colorrecords >= 1, "got %u\n", colorrecords);

    /* invalid palette index */
    color.r = color.g = color.b = color.a = 123.0;
    hr = IDWriteFontFace2_GetPaletteEntries(fontface2, palettecount, 0, 1, &color);
    ok(hr == DWRITE_E_NOCOLOR, "got 0x%08x\n", hr);
    ok(color.r == 123.0 && color.g == 123.0 && color.b == 123.0 && color.a == 123.0,
        "got wrong color %.2fx%.2fx%.2fx%.2f\n", color.r, color.g, color.b, color.a);

    /* invalid entry index */
    color.r = color.g = color.b = color.a = 123.0;
    hr = IDWriteFontFace2_GetPaletteEntries(fontface2, 0, entrycount, 1, &color);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);
    ok(color.r == 123.0 && color.g == 123.0 && color.b == 123.0 && color.a == 123.0,
        "got wrong color %.2fx%.2fx%.2fx%.2f\n", color.r, color.g, color.b, color.a);

    color.r = color.g = color.b = color.a = 123.0;
    hr = IDWriteFontFace2_GetPaletteEntries(fontface2, 0, entrycount - 1, 1, &color);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(color.r != 123.0 && color.g != 123.0 && color.b != 123.0 && color.a != 123.0,
        "got wrong color %.2fx%.2fx%.2fx%.2f\n", color.r, color.g, color.b, color.a);

    /* zero return length */
    color.r = color.g = color.b = color.a = 123.0;
    hr = IDWriteFontFace2_GetPaletteEntries(fontface2, 0, 0, 0, &color);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(color.r == 123.0 && color.g == 123.0 && color.b == 123.0 && color.a == 123.0,
        "got wrong color %.2fx%.2fx%.2fx%.2f\n", color.r, color.g, color.b, color.a);

    IDWriteFontFace2_Release(fontface2);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_TranslateColorGlyphRun(void)
{
    IDWriteColorGlyphRunEnumerator *layers;
    const DWRITE_COLOR_GLYPH_RUN *colorrun;
    IDWriteFontFace2 *fontface2;
    IDWriteFontFace *fontface;
    IDWriteFactory2 *factory2;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    UINT32 codepoints[2];
    IDWriteFont *font;
    UINT16 glyphs[2];
    BOOL hasrun;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory2, (void**)&factory2);
    IDWriteFactory_Release(factory);
    if (hr != S_OK) {
        win_skip("TranslateColorGlyphRun() is not supported.\n");
        return;
    }

    /* Tahoma, no color support */
    fontface = create_fontface((IDWriteFactory*)factory2);

    codepoints[0] = 'A';
    hr = IDWriteFontFace_GetGlyphIndices(fontface, codepoints, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    run.fontFace = fontface;
    run.fontEmSize = 20.0f;
    run.glyphCount = 1;
    run.glyphIndices = glyphs;
    run.glyphAdvances = NULL;
    run.glyphOffsets = NULL;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    layers = (void*)0xdeadbeef;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0, 0.0, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, 0, &layers);
    ok(hr == DWRITE_E_NOCOLOR, "got 0x%08x\n", hr);
    ok(layers == NULL, "got %p\n", layers);
    IDWriteFontFace_Release(fontface);

    /* Segoe UI Emoji, with color support */
    font = get_font((IDWriteFactory*)factory2, emojiW, DWRITE_FONT_STYLE_NORMAL);
    if (!font) {
        IDWriteFactory2_Release(factory2);
        skip("Segoe UI Emoji font not found.\n");
        return;
    }

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    codepoints[0] = 0x26c4;
    hr = IDWriteFontFace_GetGlyphIndices(fontface, codepoints, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    run.fontFace = fontface;

    layers = NULL;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0, 0.0, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, 0, &layers);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(layers != NULL, "got %p\n", layers);

    while (1) {
        hasrun = FALSE;
        hr = IDWriteColorGlyphRunEnumerator_MoveNext(layers, &hasrun);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        if (!hasrun)
            break;

        hr = IDWriteColorGlyphRunEnumerator_GetCurrentRun(layers, &colorrun);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        ok(colorrun->glyphRun.fontFace != NULL, "got fontface %p\n", colorrun->glyphRun.fontFace);
        ok(colorrun->glyphRun.fontEmSize == 20.0f, "got wrong font size %f\n", colorrun->glyphRun.fontEmSize);
        ok(colorrun->glyphRun.glyphCount > 0, "got wrong glyph count %u\n", colorrun->glyphRun.glyphCount);
        ok(colorrun->glyphRun.glyphIndices != NULL, "got null glyph indices %p\n", colorrun->glyphRun.glyphIndices);
        ok(colorrun->glyphRun.glyphAdvances != NULL, "got null glyph advances %p\n", colorrun->glyphRun.glyphAdvances);
    }

    /* iterated all way through */
    hr = IDWriteColorGlyphRunEnumerator_GetCurrentRun(layers, &colorrun);
    ok(hr == E_NOT_VALID_STATE, "got 0x%08x\n", hr);

    IDWriteColorGlyphRunEnumerator_Release(layers);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace2, (void**)&fontface2);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    /* invalid palette index */
    layers = (void*)0xdeadbeef;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0f, 0.0f, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, IDWriteFontFace2_GetColorPaletteCount(fontface2),
        &layers);
    ok(hr == DWRITE_E_NOCOLOR, "got 0x%08x\n", hr);
    ok(layers == NULL, "got %p\n", layers);

    layers = NULL;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0f, 0.0f, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, IDWriteFontFace2_GetColorPaletteCount(fontface2) - 1,
        &layers);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteColorGlyphRunEnumerator_Release(layers);

    /* color font, glyph without color info */
    codepoints[0] = 'A';
    hr = IDWriteFontFace_GetGlyphIndices(fontface, codepoints, 1, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    layers = (void*)0xdeadbeef;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0, 0.0, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, 0, &layers);
    ok(hr == DWRITE_E_NOCOLOR, "got 0x%08x\n", hr);
    ok(layers == NULL, "got %p\n", layers);

    /* one glyph with, one without */
    codepoints[0] = 'A';
    codepoints[1] = 0x26c4;

    hr = IDWriteFontFace_GetGlyphIndices(fontface, codepoints, 2, glyphs);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    run.glyphCount = 2;

    layers = NULL;
    hr = IDWriteFactory2_TranslateColorGlyphRun(factory2, 0.0, 0.0, &run, NULL,
        DWRITE_MEASURING_MODE_NATURAL, NULL, 0, &layers);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(layers != NULL, "got %p\n", layers);
    IDWriteColorGlyphRunEnumerator_Release(layers);

    IDWriteFontFace2_Release(fontface2);
    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory2_Release(factory2);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_HasCharacter(void)
{
    IDWriteFactory3 *factory3;
    IDWriteFactory *factory;
    IDWriteFont3 *font3;
    IDWriteFont *font;
    HRESULT hr;
    ULONG ref;
    BOOL ret;

    factory = create_factory();

    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_NORMAL);
    ok(font != NULL, "failed to create font\n");

    /* Win8 is broken, QI claims to support IDWriteFont3, but in fact it does not */
    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void**)&factory3);
    if (hr == S_OK) {
        hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont3, (void**)&font3);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        ret = IDWriteFont3_HasCharacter(font3, 'A');
        ok(ret, "got %d\n", ret);

        IDWriteFont3_Release(font3);
        IDWriteFactory3_Release(factory3);
    }
    else
        win_skip("IDWriteFont3 is not supported.\n");

    IDWriteFont_Release(font);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateFontFaceReference(void)
{
    static const WCHAR dummyW[] = {'d','u','m','m','y',0};
    IDWriteFontFace3 *fontface, *fontface1;
    IDWriteFontFaceReference *ref, *ref1;
    IDWriteFontFile *file, *file1;
    IDWriteFactory3 *factory3;
    IDWriteFactory *factory;
    IDWriteFont3 *font3;
    IDWriteFont *font;
    ULONG refcount;
    UINT32 index;
    WCHAR *path;
    HRESULT hr;
    BOOL ret;

    factory = create_factory();

    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory3, (void**)&factory3);
    IDWriteFactory_Release(factory);
    if (FAILED(hr)) {
        win_skip("CreateFontFaceReference() is not supported.\n");
        return;
    }

    path = create_testfontfile(test_fontfile);

    hr = IDWriteFactory3_CreateFontFaceReference(factory3, NULL, NULL, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* out of range simulation flags */
    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 0, ~0u, &ref);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    /* test file is not a collection, but reference could still be created with non-zero face index */
    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 1, DWRITE_FONT_SIMULATIONS_NONE, &ref);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    index = IDWriteFontFaceReference_GetFontFaceIndex(ref);
    ok(index == 1, "got %u\n", index);

    hr = IDWriteFontFaceReference_GetFontFile(ref, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFaceReference_CreateFontFace(ref, &fontface);
todo_wine
    ok(hr == DWRITE_E_FILEFORMAT, "got 0x%08x\n", hr);

    IDWriteFontFaceReference_Release(ref);

    /* path however has to be valid */
    hr = IDWriteFactory3_CreateFontFaceReference(factory3, dummyW, NULL, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref);
todo_wine
    ok(hr == DWRITE_E_FILENOTFOUND, "got 0x%08x\n", hr);
    if (hr == S_OK)
        IDWriteFontFaceReference_Release(ref);

    EXPECT_REF(factory3, 1);
    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    EXPECT_REF(factory3, 2);

    /* new file is returned */
    hr = IDWriteFontFaceReference_GetFontFile(ref, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFaceReference_GetFontFile(ref, &file1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(file != file1, "got %p, previous file %p\n", file1, file);

    IDWriteFontFile_Release(file);
    IDWriteFontFile_Release(file1);

    /* references are not reused */
    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(ref1 != ref, "got %p, previous ref %p\n", ref1, ref);

    /* created fontfaces are cached */
    hr = IDWriteFontFaceReference_CreateFontFace(ref, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFaceReference_CreateFontFace(ref1, &fontface1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface == fontface1, "got %p, expected %p\n", fontface1, fontface);
    IDWriteFontFace3_Release(fontface);
    IDWriteFontFace3_Release(fontface1);

    /* reference equality */
    ret = IDWriteFontFaceReference_Equals(ref, ref1);
    ok(ret, "got %d\n", ret);
    IDWriteFontFaceReference_Release(ref1);

    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 1, DWRITE_FONT_SIMULATIONS_NONE, &ref1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ret = IDWriteFontFaceReference_Equals(ref, ref1);
    ok(!ret, "got %d\n", ret);
    IDWriteFontFaceReference_Release(ref1);

    hr = IDWriteFactory3_CreateFontFaceReference(factory3, path, NULL, 0, DWRITE_FONT_SIMULATIONS_BOLD, &ref1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ret = IDWriteFontFaceReference_Equals(ref, ref1);
    ok(!ret, "got %d\n", ret);
    IDWriteFontFaceReference_Release(ref1);

    IDWriteFontFaceReference_Release(ref);

    /* create reference from a file */
    hr = IDWriteFactory3_CreateFontFileReference(factory3, path, NULL, &file);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFactory3_CreateFontFaceReference_(factory3, file, 0, DWRITE_FONT_SIMULATIONS_NONE, &ref);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFaceReference_GetFontFile(ref, &file1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(file != file1, "got %p, previous file %p\n", file1, file);

    IDWriteFontFaceReference_Release(ref);
    IDWriteFontFile_Release(file);
    IDWriteFontFile_Release(file1);

    /* references returned from IDWriteFont3 */
    font = get_tahoma_instance((IDWriteFactory*)factory3, DWRITE_FONT_STYLE_NORMAL);
    hr = IDWriteFont_QueryInterface(font, &IID_IDWriteFont3, (void**)&font3);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    IDWriteFont_Release(font);

    hr = IDWriteFont3_GetFontFaceReference(font3, &ref);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFont3_GetFontFaceReference(font3, &ref1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(ref != ref1, "got %p, %p\n", ref1, ref);

    IDWriteFontFaceReference_Release(ref);
    IDWriteFontFaceReference_Release(ref1);

    /* references returned from IDWriteFontFace3 */
    hr = IDWriteFont3_CreateFontFace(font3, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace3_GetFontFaceReference(fontface, &ref);
todo_wine
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace3_GetFontFaceReference(fontface, &ref1);
todo_wine
    ok(hr == S_OK, "got 0x%08x\n", hr);
if (hr == S_OK)
    ok(ref == ref1, "got %p, %p\n", ref1, ref);

if (hr == S_OK) {
    hr = IDWriteFontFaceReference_CreateFontFace(ref, &fontface1);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(fontface1 == fontface, "got %p, %p\n", fontface1, fontface);
    IDWriteFontFace3_Release(fontface1);

    IDWriteFontFaceReference_Release(ref);
    IDWriteFontFaceReference_Release(ref1);
}
    IDWriteFontFace3_Release(fontface);
    IDWriteFont3_Release(font3);

    refcount = IDWriteFactory3_Release(factory3);
    ok(refcount == 0, "factory not released, %u\n", refcount);
    DELETE_FONTFILE(path);
}

static void get_expected_fontsig(IDWriteFont *font, FONTSIGNATURE *fontsig)
{
    void *os2_context;
    IDWriteFontFace *fontface;
    const TT_OS2_V2 *tt_os2;
    UINT32 size;
    BOOL exists;
    HRESULT hr;

    memset(fontsig, 0, sizeof(*fontsig));

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_TryGetFontTable(fontface, MS_0S2_TAG, (const void**)&tt_os2, &size, &os2_context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    if (tt_os2) {
        fontsig->fsUsb[0] = GET_BE_DWORD(tt_os2->ulUnicodeRange1);
        fontsig->fsUsb[1] = GET_BE_DWORD(tt_os2->ulUnicodeRange2);
        fontsig->fsUsb[2] = GET_BE_DWORD(tt_os2->ulUnicodeRange3);
        fontsig->fsUsb[3] = GET_BE_DWORD(tt_os2->ulUnicodeRange4);

        if (GET_BE_WORD(tt_os2->version) == 0) {
            fontsig->fsCsb[0] = 0;
            fontsig->fsCsb[1] = 0;
        }
        else {
            fontsig->fsCsb[0] = GET_BE_DWORD(tt_os2->ulCodePageRange1);
            fontsig->fsCsb[1] = GET_BE_DWORD(tt_os2->ulCodePageRange2);
        }

        IDWriteFontFace_ReleaseFontTable(fontface, os2_context);
    }

    IDWriteFontFace_Release(fontface);
}

static void test_GetFontSignature(void)
{
    IDWriteFontCollection *syscollection;
    IDWriteGdiInterop1 *interop1;
    IDWriteGdiInterop *interop;
    IDWriteFactory *factory;
    FONTSIGNATURE fontsig;
    UINT count, i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteGdiInterop_QueryInterface(interop, &IID_IDWriteGdiInterop1, (void**)&interop1);
    IDWriteGdiInterop_Release(interop);
    if (FAILED(hr)) {
        win_skip("GetFontSignature() is not supported.\n");
        IDWriteGdiInterop_Release(interop);
        IDWriteFactory_Release(factory);
        return;
    };
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteGdiInterop1_GetFontSignature(interop1, NULL, &fontsig);
    ok(hr == E_INVALIDARG, "got 0x%08x\n", hr);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        FONTSIGNATURE expected_signature;
        IDWriteLocalizedStrings *names;
        IDWriteFontFamily *family;
        IDWriteFont *font;
        WCHAR nameW[256];

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, nameW, sizeof(nameW)/sizeof(nameW[0]));

        IDWriteLocalizedStrings_Release(names);

        hr = IDWriteGdiInterop1_GetFontSignature(interop1, font, &fontsig);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_expected_fontsig(font, &expected_signature);

        ok(fontsig.fsUsb[0] == expected_signature.fsUsb[0], "%s: fsUsb[0] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsUsb[0], expected_signature.fsUsb[0]);
        ok(fontsig.fsUsb[1] == expected_signature.fsUsb[1], "%s: fsUsb[1] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsUsb[1], expected_signature.fsUsb[1]);
        ok(fontsig.fsUsb[2] == expected_signature.fsUsb[2], "%s: fsUsb[2] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsUsb[2], expected_signature.fsUsb[2]);
        ok(fontsig.fsUsb[3] == expected_signature.fsUsb[3], "%s: fsUsb[3] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsUsb[3], expected_signature.fsUsb[3]);

        ok(fontsig.fsCsb[0] == expected_signature.fsCsb[0], "%s: fsCsb[0] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsCsb[0], expected_signature.fsCsb[0]);
        ok(fontsig.fsCsb[1] == expected_signature.fsCsb[1], "%s: fsCsb[1] %#x, expected %#x\n", wine_dbgstr_w(nameW),
            fontsig.fsCsb[1], expected_signature.fsCsb[1]);

        IDWriteFont_Release(font);
        IDWriteFontFamily_Release(family);
    }

    IDWriteGdiInterop1_Release(interop1);
    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_font_properties(void)
{
    IDWriteFontFace3 *fontface3;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    DWRITE_FONT_STYLE style;
    IDWriteFont *font;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    /* this creates simulated font */
    font = get_tahoma_instance(factory, DWRITE_FONT_STYLE_ITALIC);

    style = IDWriteFont_GetStyle(font);
    ok(style == DWRITE_FONT_STYLE_OBLIQUE, "got %u\n", style);

    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace3, (void**)&fontface3);
    IDWriteFontFace_Release(fontface);
    if (hr == S_OK) {
        style = IDWriteFontFace3_GetStyle(fontface3);
        ok(style == DWRITE_FONT_STYLE_OBLIQUE, "got %u\n", style);

        IDWriteFontFace3_Release(fontface3);
    }

    IDWriteFont_Release(font);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static BOOL has_vertical_glyph_variants(IDWriteFontFace1 *fontface)
{
    const OT_FeatureList *featurelist;
    const OT_LookupList *lookup_list;
    BOOL exists = FALSE, ret = FALSE;
    const GSUB_Header *header;
    const void *data;
    void *context;
    UINT32 size;
    HRESULT hr;
    UINT16 i;

    hr = IDWriteFontFace1_TryGetFontTable(fontface, MS_GSUB_TAG, &data, &size, &context, &exists);
    ok(hr == S_OK, "got 0x%08x\n", hr);

    if (!exists)
        return FALSE;

    header = data;
    featurelist = (OT_FeatureList*)((BYTE*)header + GET_BE_WORD(header->FeatureList));
    lookup_list = (const OT_LookupList*)((BYTE*)header + GET_BE_WORD(header->LookupList));

    for (i = 0; i < GET_BE_WORD(featurelist->FeatureCount); i++) {
        if (*(UINT32*)featurelist->FeatureRecord[i].FeatureTag == DWRITE_FONT_FEATURE_TAG_VERTICAL_WRITING) {
            const OT_Feature *feature = (const OT_Feature*)((BYTE*)featurelist + GET_BE_WORD(featurelist->FeatureRecord[i].Feature));
            UINT16 lookup_count = GET_BE_WORD(feature->LookupCount), i, index, count, type;
            const GSUB_SingleSubstFormat2 *subst2;
            const OT_LookupTable *lookup_table;
            UINT32 offset;

            if (lookup_count == 0)
                continue;

            for (i = 0; i < lookup_count; i++) {
                /* check if lookup is empty */
                index = GET_BE_WORD(feature->LookupListIndex[i]);
                lookup_table = (const OT_LookupTable*)((BYTE*)lookup_list + GET_BE_WORD(lookup_list->Lookup[index]));

                type = GET_BE_WORD(lookup_table->LookupType);
                ok(type == 1 || type == 7, "got unexpected lookup type %u\n", type);

                count = GET_BE_WORD(lookup_table->SubTableCount);
                if (count == 0)
                    continue;

                ok(count > 0, "got unexpected subtable count %u\n", count);

                offset = GET_BE_WORD(lookup_table->SubTable[0]);
                if (type == 7) {
                    const GSUB_ExtensionPosFormat1 *ext = (const GSUB_ExtensionPosFormat1 *)((const BYTE *)lookup_table + offset);
                    if (GET_BE_WORD(ext->SubstFormat) == 1)
                        offset += GET_BE_DWORD(ext->ExtensionOffset);
                    else
                        ok(0, "Unhandled Extension Substitution Format %u\n", GET_BE_WORD(ext->SubstFormat));
                }

                subst2 = (const GSUB_SingleSubstFormat2*)((BYTE*)lookup_table + offset);
                index = GET_BE_WORD(subst2->SubstFormat);
                if (index == 1)
                    ok(0, "validate Single Substitution Format 1\n");
                else if (index == 2) {
                    /* SimSun-ExtB has 0 glyph count for this substitution */
                    if (GET_BE_WORD(subst2->GlyphCount) > 0) {
                        ret = TRUE;
                        break;
                    }
                }
                else
                    ok(0, "unknown Single Substitution Format, %u\n", index);
            }
        }
    }

    IDWriteFontFace1_ReleaseFontTable(fontface, context);

    return ret;
}

static void test_HasVerticalGlyphVariants(void)
{
    IDWriteFontCollection *syscollection;
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    UINT32 count, i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    fontface = create_fontface(factory);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    IDWriteFontFace_Release(fontface);
    if (hr != S_OK) {
        win_skip("HasVerticalGlyphVariants() is not supported.\n");
        IDWriteFactory_Release(factory);
        return;
    }
    IDWriteFontFace1_Release(fontface1);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        IDWriteLocalizedStrings *names;
        BOOL expected_vert, has_vert;
        IDWriteFontFamily *family;
        IDWriteFont *font;
        WCHAR nameW[256];

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFont_CreateFontFace(font, &fontface);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, nameW, sizeof(nameW)/sizeof(nameW[0]));

        expected_vert = has_vertical_glyph_variants(fontface1);
        has_vert = IDWriteFontFace1_HasVerticalGlyphVariants(fontface1);

        ok(expected_vert == has_vert, "%s: expected vertical feature %d, got %d\n",
            wine_dbgstr_w(nameW), expected_vert, has_vert);

        IDWriteLocalizedStrings_Release(names);
        IDWriteFont_Release(font);

        IDWriteFontFace1_Release(fontface1);
        IDWriteFontFace_Release(fontface);
        IDWriteFontFamily_Release(family);
    }

    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_HasKerningPairs(void)
{
    IDWriteFontCollection *syscollection;
    IDWriteFontFace1 *fontface1;
    IDWriteFontFace *fontface;
    IDWriteFactory *factory;
    UINT32 count, i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    fontface = create_fontface(factory);

    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void**)&fontface1);
    IDWriteFontFace_Release(fontface);
    if (hr != S_OK) {
        win_skip("HasKerningPairs() is not supported.\n");
        IDWriteFactory_Release(factory);
        return;
    }
    IDWriteFontFace1_Release(fontface1);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        IDWriteLocalizedStrings *names;
        BOOL exists, has_kerningpairs;
        IDWriteFontFamily *family;
        IDWriteFont *font;
        WCHAR nameW[256];
        const void *data;
        void *context;
        UINT32 size;

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFont_CreateFontFace(font, &fontface);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace1, (void **)&fontface1);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, nameW, sizeof(nameW)/sizeof(nameW[0]));

        exists = FALSE;
        hr = IDWriteFontFace1_TryGetFontTable(fontface1, MS_KERN_TAG, &data, &size, &context, &exists);
        ok(hr == S_OK, "got 0x%08x\n", hr);
        IDWriteFontFace1_ReleaseFontTable(fontface1, context);

        has_kerningpairs = IDWriteFontFace1_HasKerningPairs(fontface1);
        if (!exists)
            ok(!has_kerningpairs, "%s: expected %d, got %d\n", wine_dbgstr_w(nameW), exists, has_kerningpairs);

        IDWriteLocalizedStrings_Release(names);
        IDWriteFont_Release(font);

        IDWriteFontFace1_Release(fontface1);
        IDWriteFontFace_Release(fontface);
        IDWriteFontFamily_Release(family);
    }

    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_ComputeGlyphOrigins(void)
{
    IDWriteFactory4 *factory4;
    IDWriteFactory *factory;
    DWRITE_GLYPH_RUN run;
    HRESULT hr;
    D2D1_POINT_2F origins[2];
    D2D1_POINT_2F baseline_origin;
    UINT16 glyphs[2];
    FLOAT advances[2];
    DWRITE_MATRIX m;
    ULONG ref;

    factory = create_factory();
    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory4, (void **)&factory4);
    IDWriteFactory_Release(factory);
    if (FAILED(hr)) {
        win_skip("ComputeGlyphOrigins() is not supported.\n");
        return;
    }

    advances[0] = 10.0f;
    advances[1] = 20.0f;

    run.fontFace = NULL;
    run.fontEmSize = 16.0f;
    run.glyphCount = 2;
    run.glyphIndices = glyphs;
    run.glyphAdvances = advances;
    run.glyphOffsets = NULL;
    run.isSideways = FALSE;
    run.bidiLevel = 0;

    baseline_origin.x = 123.0f;
    baseline_origin.y = 321.0f;

    memset(origins, 0, sizeof(origins));
    hr = IDWriteFactory4_ComputeGlyphOrigins_(factory4, &run, baseline_origin, origins);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    ok(origins[0].x == 123.0f && origins[0].y == 321.0f, "origins[0] %f,%f\n", origins[0].x, origins[0].y);
    ok(origins[1].x == 133.0f && origins[1].y == 321.0f, "origins[1] %f,%f\n", origins[1].x, origins[1].y);

    memset(origins, 0, sizeof(origins));
    hr = IDWriteFactory4_ComputeGlyphOrigins(factory4, &run, DWRITE_MEASURING_MODE_NATURAL, baseline_origin,
        NULL, origins);
    ok(origins[0].x == 123.0f && origins[0].y == 321.0f, "origins[0] %f,%f\n", origins[0].x, origins[0].y);
    ok(origins[1].x == 133.0f && origins[1].y == 321.0f, "origins[1] %f,%f\n", origins[1].x, origins[1].y);

    /* transform is not applied to returned origins */
    m.m11 = 2.0f;
    m.m12 = 0.0f;
    m.m21 = 0.0f;
    m.m22 = 1.0f;
    m.dx = 0.0f;
    m.dy = 0.0f;

    memset(origins, 0, sizeof(origins));
    hr = IDWriteFactory4_ComputeGlyphOrigins(factory4, &run, DWRITE_MEASURING_MODE_NATURAL, baseline_origin,
        &m, origins);
    ok(origins[0].x == 123.0f && origins[0].y == 321.0f, "origins[0] %f,%f\n", origins[0].x, origins[0].y);
    ok(origins[1].x == 133.0f && origins[1].y == 321.0f, "origins[1] %f,%f\n", origins[1].x, origins[1].y);

    ref = IDWriteFactory4_Release(factory4);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_object_lifetime(void)
{
    IDWriteFontCollection *collection, *collection2;
    IDWriteFontList *fontlist, *fontlist2;
    IDWriteGdiInterop *interop, *interop2;
    IDWriteFontFamily *family, *family2;
    IDWriteFontFace *fontface;
    IDWriteFont *font, *font2;
    IDWriteFactory *factory;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();
    EXPECT_REF(factory, 1);

    /* system collection takes factory reference */
    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(collection, 1);
    EXPECT_REF(factory, 2);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection2, FALSE);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(collection2 == collection, "expected same collection\n");

    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    IDWriteFontCollection_Release(collection2);

    IDWriteFontCollection_AddRef(collection);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);
    IDWriteFontCollection_Release(collection);

    EXPECT_REF(collection, 1);

    /* family takes collection reference */
    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(family, 1);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    hr = IDWriteFontCollection_GetFontFamily(collection, 0, &family2);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(family2, 1);
    EXPECT_REF(collection, 3);
    EXPECT_REF(factory, 2);

    IDWriteFontFamily_Release(family2);

    EXPECT_REF(family, 1);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    /* font takes family reference */
    hr = IDWriteFontFamily_GetFirstMatchingFont(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(family, 2);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    hr = IDWriteFont_GetFontFamily(font, &family2);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(family2 == family, "unexpected family pointer\n");
    IDWriteFontFamily_Release(family2);

    EXPECT_REF(font, 1);
    EXPECT_REF(factory, 2);

    /* Fontface takes factory reference and nothing else. */
    hr = IDWriteFont_CreateFontFace(font, &fontface);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(font, 1);
    EXPECT_REF_BROKEN(fontface, 1, 2);
    EXPECT_REF(family, 2);
    EXPECT_REF(collection, 2);
    EXPECT_REF_BROKEN(factory, 3, 2);

    /* get font from fontface */
    hr = IDWriteFontCollection_GetFontFromFontFace(collection, fontface, &font2);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(font, 1);
    EXPECT_REF(font2, 1);
    EXPECT_REF_BROKEN(fontface, 1, 2);
    EXPECT_REF(family, 2);
    EXPECT_REF(collection, 3);
    EXPECT_REF_BROKEN(factory, 3, 2);

    IDWriteFont_Release(font2);
    IDWriteFontFace_Release(fontface);

    EXPECT_REF(font, 1);
    EXPECT_REF(family, 2);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    IDWriteFont_Release(font);

    EXPECT_REF(family, 1);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    /* Matching fonts list takes family reference. */
    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &fontlist);
    ok(hr == S_OK, "got %#x\n", hr);

    EXPECT_REF(family, 2);
    EXPECT_REF(collection, 2);
    EXPECT_REF(factory, 2);

    hr = IDWriteFontFamily_GetMatchingFonts(family, DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &fontlist2);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(fontlist2 != fontlist, "unexpected font list\n");
    IDWriteFontList_Release(fontlist2);

    IDWriteFontList_Release(fontlist);

    IDWriteFontFamily_Release(family);
    EXPECT_REF(collection, 1);

    EXPECT_REF(factory, 2);
    ref = IDWriteFontCollection_Release(collection);
    ok(ref == 0, "collection not released, %u\n", ref);
    EXPECT_REF(factory, 1);

    /* GDI interop object takes factory reference */
    hr = IDWriteFactory_GetGdiInterop(factory, &interop);
    ok(hr == S_OK, "got %#x\n", hr);
    EXPECT_REF(interop, 1);
    EXPECT_REF(factory, 2);

    hr = IDWriteFactory_GetGdiInterop(factory, &interop2);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(interop == interop2, "got unexpected interop pointer\n");

    EXPECT_REF(interop, 2);
    EXPECT_REF(factory, 2);

    IDWriteGdiInterop_Release(interop2);
    ref = IDWriteGdiInterop_Release(interop);
    ok(ref == 0, "interop not released, %u\n", ref);

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_inmemory_file_loader(void)
{
    IDWriteFontFileLoader *loader, *loader2;
    IDWriteInMemoryFontFileLoader *inmemory;
    IDWriteFontFile *file, *file2;
    IDWriteFontFileStream *stream;
    IDWriteFontFace *fontface;
    IDWriteFactory5 *factory5;
    IDWriteFactory *factory;
    const void *key, *data;
    UINT32 count, key_size;
    UINT64 file_size;
    void *context;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    hr = IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory5, (void **)&factory5);
    IDWriteFactory_Release(factory);
    if (FAILED(hr)) {
        win_skip("CreateInMemoryFontFileLoader is not supported\n");
        return;
    }

    hr = IDWriteFactory5_CreateInMemoryFontFileLoader(factory5, &loader);
todo_wine
    ok(hr == S_OK, "got %#x\n", hr);

    if (FAILED(hr)) {
        IDWriteFactory5_Release(factory5);
        return;
    }

    fontface = create_fontface((IDWriteFactory *)factory5);

    hr = IDWriteFactory5_CreateInMemoryFontFileLoader(factory5, &loader2);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(loader != loader2, "unexpected pointer\n");
    IDWriteFontFileLoader_Release(loader2);

    hr = IDWriteFontFileLoader_QueryInterface(loader, &IID_IDWriteInMemoryFontFileLoader, (void **)&inmemory);
    ok(hr == S_OK, "got %#x\n", hr);
    IDWriteFontFileLoader_Release(loader);

    /* Use whole font blob to construct in-memory file. */
    count = 1;
    hr = IDWriteFontFace_GetFiles(fontface, &count, &file);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteFontFile_GetLoader(file, &loader);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteFontFileLoader_CreateStreamFromKey(loader, key, key_size, &stream);
    ok(hr == S_OK, "got %#x\n", hr);
    IDWriteFontFileLoader_Release(loader);
    IDWriteFontFile_Release(file);

    hr = IDWriteFontFileStream_GetFileSize(stream, &file_size);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteFontFileStream_ReadFileFragment(stream, &data, 0, file_size, &context);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteInMemoryFontFileLoader_CreateInMemoryFontFileReference(inmemory, (IDWriteFactory *)factory5, data,
        file_size, NULL, &file);
    ok(hr == E_INVALIDARG, "got %#x\n", hr);

    hr = IDWriteFactory5_RegisterFontFileLoader(factory5, (IDWriteFontFileLoader *)inmemory);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteInMemoryFontFileLoader_CreateInMemoryFontFileReference(inmemory, (IDWriteFactory *)factory5, data,
        file_size, NULL, &file);
    ok(hr == S_OK, "got %#x\n", hr);

    hr = IDWriteInMemoryFontFileLoader_CreateInMemoryFontFileReference(inmemory, (IDWriteFactory *)factory5, data,
        file_size, NULL, &file2);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(file2 != file, "got unexpected file\n");

    /* Check in-memory reference key format. */
    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    ok(hr == S_OK, "got %#x\n", hr);

    ok(key && *(DWORD*)key == 1, "got wrong ref key\n");
    ok(key_size == 4, "ref key size %u\n", key_size);

    hr = IDWriteFontFile_GetReferenceKey(file2, &key, &key_size);
    ok(hr == S_OK, "got %#x\n", hr);

    ok(key && *(DWORD*)key == 2, "got wrong ref key\n");
    ok(key_size == 4, "ref key size %u\n", key_size);

    /* Release file and index 1, create new one to see if index is reused. */
    IDWriteFontFile_Release(file);

    hr = IDWriteInMemoryFontFileLoader_CreateInMemoryFontFileReference(inmemory, (IDWriteFactory *)factory5, data,
        file_size, NULL, &file);
    ok(hr == S_OK, "got %#x\n", hr);
    ok(file2 != file, "got unexpected file\n");

    hr = IDWriteFontFile_GetReferenceKey(file, &key, &key_size);
    ok(hr == S_OK, "got %#x\n", hr);

    ok(key && *(DWORD*)key == 3, "got wrong ref key\n");
    ok(key_size == 4, "ref key size %u\n", key_size);

    IDWriteFontFile_Release(file);
    IDWriteFontFile_Release(file2);

    hr = IDWriteFactory5_UnregisterFontFileLoader(factory5, (IDWriteFontFileLoader *)inmemory);
    ok(hr == S_OK, "got %#x\n", hr);

    IDWriteFontFileStream_ReleaseFileFragment(stream, context);
    IDWriteFontFileStream_Release(stream);

    IDWriteInMemoryFontFileLoader_Release(inmemory);

    IDWriteFontFace_Release(fontface);
    ref = IDWriteFactory5_Release(factory5);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static BOOL face_has_table(IDWriteFontFace4 *fontface, UINT32 tag)
{
    BOOL exists = FALSE;
    const void *data;
    void *context;
    UINT32 size;
    HRESULT hr;

    hr = IDWriteFontFace4_TryGetFontTable(fontface, tag, &data, &size, &context, &exists);
    ok(hr == S_OK, "TryGetFontTable() failed, %#x\n", hr);
    if (exists)
        IDWriteFontFace4_ReleaseFontTable(fontface, context);

    return exists;
}

static DWORD get_sbix_formats(IDWriteFontFace4 *fontface)
{
    UINT32 size, s, num_strikes;
    const sbix_header *header;
    UINT16 g, num_glyphs;
    BOOL exists = FALSE;
    const maxp *maxp;
    const void *data;
    DWORD ret = 0;
    void *context;
    HRESULT hr;

    hr = IDWriteFontFace4_TryGetFontTable(fontface, MS_MAXP_TAG, &data, &size, &context, &exists);
    ok(hr == S_OK, "TryGetFontTable() failed, %#x\n", hr);
    ok(exists, "Expected maxp table\n");

    if (!exists)
        return 0;

    maxp = data;
    num_glyphs = GET_BE_WORD(maxp->numGlyphs);

    hr = IDWriteFontFace4_TryGetFontTable(fontface, MS_SBIX_TAG, &data, &size, &context, &exists);
    ok(hr == S_OK, "TryGetFontTable() failed, %#x\n", hr);
    ok(exists, "Expected sbix table\n");

    header = data;
    num_strikes = GET_BE_DWORD(header->numStrikes);

    for (s = 0; s < num_strikes; s++) {
        sbix_strike *strike = (sbix_strike *)((BYTE *)header + GET_BE_DWORD(header->strikeOffset[s]));

        for (g = 0; g < num_glyphs; g++) {
            DWORD offset = GET_BE_DWORD(strike->glyphDataOffsets[g]);
            DWORD offset_next = GET_BE_DWORD(strike->glyphDataOffsets[g + 1]);
            sbix_glyph_data *glyph_data;
            DWORD format;

            if (offset == offset_next)
                continue;

            glyph_data = (sbix_glyph_data *)((BYTE *)strike + offset);
            switch (format = glyph_data->graphicType)
            {
            case MS_PNG__TAG:
                ret |= DWRITE_GLYPH_IMAGE_FORMATS_PNG;
                break;
            case MS_JPG__TAG:
                ret |= DWRITE_GLYPH_IMAGE_FORMATS_JPEG;
                break;
            case MS_TIFF_TAG:
                ret |= DWRITE_GLYPH_IMAGE_FORMATS_TIFF;
                break;
            default:
                ok(0, "unexpected format, %#x\n", GET_BE_DWORD(format));
            }
        }
    }

    IDWriteFontFace4_ReleaseFontTable(fontface, context);

    return ret;
}

static DWORD get_face_glyph_image_formats(IDWriteFontFace4 *fontface)
{
    DWORD ret = DWRITE_GLYPH_IMAGE_FORMATS_NONE;

    if (face_has_table(fontface, MS_GLYF_TAG))
        ret |= DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE;

    if (face_has_table(fontface, MS_CFF__TAG))
        ret |= DWRITE_GLYPH_IMAGE_FORMATS_CFF;

    if (face_has_table(fontface, MS_COLR_TAG))
        ret |= DWRITE_GLYPH_IMAGE_FORMATS_COLR;

    if (face_has_table(fontface, MS_SVG__TAG))
        ret |= DWRITE_GLYPH_IMAGE_FORMATS_SVG;

    if (face_has_table(fontface, MS_SBIX_TAG))
        ret |= get_sbix_formats(fontface);

    /* TODO: handle embedded bitmaps tables */
    return ret;
}

static void test_GetGlyphImageFormats(void)
{
    IDWriteFontCollection *syscollection;
    IDWriteFactory *factory;
    UINT32 i, count;
    HRESULT hr;
    ULONG ref;
    IDWriteFontFace *fontface;
    IDWriteFontFace4 *fontface4;

    factory = create_factory();

    fontface = create_fontface(factory);
    hr = IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace4, (void **)&fontface4);
    IDWriteFontFace_Release(fontface);
    if (FAILED(hr)) {
        win_skip("GetGlyphImageFormats() is not supported\n");
        IDWriteFactory_Release(factory);
        return;
    }
    IDWriteFontFace4_Release(fontface4);

    hr = IDWriteFactory_GetSystemFontCollection(factory, &syscollection, FALSE);
    ok(hr == S_OK, "got 0x%08x\n", hr);
    count = IDWriteFontCollection_GetFontFamilyCount(syscollection);

    for (i = 0; i < count; i++) {
        WCHAR familynameW[256], facenameW[128];
        IDWriteLocalizedStrings *names;
        IDWriteFontFamily *family;
        UINT32 j, fontcount;
        IDWriteFont *font;

        hr = IDWriteFontCollection_GetFontFamily(syscollection, i, &family);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        hr = IDWriteFontFamily_GetFamilyNames(family, &names);
        ok(hr == S_OK, "got 0x%08x\n", hr);

        get_enus_string(names, familynameW, sizeof(familynameW)/sizeof(*familynameW));
        IDWriteLocalizedStrings_Release(names);

        fontcount = IDWriteFontFamily_GetFontCount(family);
        for (j = 0; j < fontcount; j++) {
            DWORD formats, expected_formats;

            hr = IDWriteFontFamily_GetFont(family, j, &font);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            hr = IDWriteFont_CreateFontFace(font, &fontface);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            hr = IDWriteFont_GetFaceNames(font, &names);
            ok(hr == S_OK, "got 0x%08x\n", hr);

            get_enus_string(names, facenameW, sizeof(facenameW)/sizeof(*facenameW));

            IDWriteLocalizedStrings_Release(names);

            IDWriteFontFace_QueryInterface(fontface, &IID_IDWriteFontFace4, (void **)&fontface4);

            /* Mask describes font as a whole. */
            formats = IDWriteFontFace4_GetGlyphImageFormats(fontface4);
            expected_formats = get_face_glyph_image_formats(fontface4);
            ok(formats == expected_formats, "%s - %s, expected formats %#x, got formats %#x.\n",
                wine_dbgstr_w(familynameW), wine_dbgstr_w(facenameW), expected_formats, formats);

            IDWriteFontFace4_Release(fontface4);
            IDWriteFontFace_Release(fontface);
            IDWriteFont_Release(font);
        }

        IDWriteFontFamily_Release(family);
    }

    IDWriteFontCollection_Release(syscollection);
    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

static void test_CreateCustomRenderingParams(void)
{
    static const struct custom_params_test
    {
        FLOAT gamma;
        FLOAT contrast;
        FLOAT cleartype_level;
        DWRITE_PIXEL_GEOMETRY geometry;
        DWRITE_RENDERING_MODE rendering_mode;
        HRESULT hr;
    } params_tests[] =
    {
        {  0.0f,  0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.0f,  0.1f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.0f,  0.0f,  0.1f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        { -0.1f,  0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.1f, -0.1f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.1f,  0.0f, -0.1f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.1f,  0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL },
        {  0.01f, 0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_NATURAL },
        {  0.1f,  0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_BGR + 1, DWRITE_RENDERING_MODE_NATURAL, E_INVALIDARG },
        {  0.1f,  0.0f,  0.0f, DWRITE_PIXEL_GEOMETRY_BGR, DWRITE_RENDERING_MODE_OUTLINE + 1, E_INVALIDARG },
        {  0.1f,  0.0f,  2.0f, DWRITE_PIXEL_GEOMETRY_BGR, DWRITE_RENDERING_MODE_NATURAL },
    };
    IDWriteFactory *factory;
    unsigned int i;
    HRESULT hr;
    ULONG ref;

    factory = create_factory();

    for (i = 0; i < sizeof(params_tests)/sizeof(*params_tests); i++) {
        IDWriteRenderingParams *params;

        params = (void *)0xdeadbeef;
        hr = IDWriteFactory_CreateCustomRenderingParams(factory, params_tests[i].gamma, params_tests[i].contrast,
                params_tests[i].cleartype_level, params_tests[i].geometry, params_tests[i].rendering_mode, &params);
        ok(hr == params_tests[i].hr, "%u: unexpected hr %#x, expected %#x.\n", i, hr, params_tests[i].hr);

        if (hr == S_OK) {
            ok(params_tests[i].gamma == IDWriteRenderingParams_GetGamma(params), "%u: unexpected gamma %f, expected %f.\n",
                    i, IDWriteRenderingParams_GetGamma(params), params_tests[i].gamma);
            ok(params_tests[i].contrast == IDWriteRenderingParams_GetEnhancedContrast(params),
                    "%u: unexpected contrast %f, expected %f.\n",
                    i, IDWriteRenderingParams_GetEnhancedContrast(params), params_tests[i].contrast);
            ok(params_tests[i].cleartype_level == IDWriteRenderingParams_GetClearTypeLevel(params),
                    "%u: unexpected ClearType level %f, expected %f.\n",
                    i, IDWriteRenderingParams_GetClearTypeLevel(params), params_tests[i].cleartype_level);
            ok(params_tests[i].geometry == IDWriteRenderingParams_GetPixelGeometry(params),
                    "%u: unexpected pixel geometry %u, expected %u.\n", i, IDWriteRenderingParams_GetPixelGeometry(params),
                    params_tests[i].geometry);
            ok(params_tests[i].rendering_mode == IDWriteRenderingParams_GetRenderingMode(params),
                    "%u: unexpected rendering mode %u, expected %u.\n", i, IDWriteRenderingParams_GetRenderingMode(params),
                    params_tests[i].rendering_mode);
            IDWriteRenderingParams_Release(params);
        }
        else
            ok(params == NULL, "%u: expected NULL interface pointer on failure.\n", i);
    }

    ref = IDWriteFactory_Release(factory);
    ok(ref == 0, "factory not released, %u\n", ref);
}

START_TEST(font)
{
    IDWriteFactory *factory;

    if (!(factory = create_factory())) {
        win_skip("failed to create factory\n");
        return;
    }

    test_object_lifetime();
    test_CreateFontFromLOGFONT();
    test_CreateBitmapRenderTarget();
    test_GetFontFamily();
    test_GetFamilyNames();
    test_CreateFontFace();
    test_GetMetrics();
    test_system_fontcollection();
    test_ConvertFontFaceToLOGFONT();
    test_CustomFontCollection();
    test_CreateCustomFontFileReference();
    test_CreateFontFileReference();
    test_shared_isolated();
    test_GetUnicodeRanges();
    test_GetFontFromFontFace();
    test_GetFirstMatchingFont();
    test_GetMatchingFonts();
    test_GetInformationalStrings();
    test_GetGdiInterop();
    test_CreateFontFaceFromHdc();
    test_GetSimulations();
    test_GetFaceNames();
    test_TryGetFontTable();
    test_ConvertFontToLOGFONT();
    test_CreateStreamFromKey();
    test_ReadFileFragment();
    test_GetDesignGlyphMetrics();
    test_GetDesignGlyphAdvances();
    test_IsMonospacedFont();
    test_GetGlyphRunOutline();
    test_GetEudcFontCollection();
    test_GetCaretMetrics();
    test_GetGlyphCount();
    test_GetKerningPairAdjustments();
    test_CreateRenderingParams();
    test_CreateGlyphRunAnalysis();
    test_GetGdiCompatibleMetrics();
    test_GetPanose();
    test_GetGdiCompatibleGlyphAdvances();
    test_GetRecommendedRenderingMode();
    test_GetAlphaBlendParams();
    test_CreateAlphaTexture();
    test_IsSymbolFont();
    test_GetPaletteEntries();
    test_TranslateColorGlyphRun();
    test_HasCharacter();
    test_CreateFontFaceReference();
    test_GetFontSignature();
    test_font_properties();
    test_HasVerticalGlyphVariants();
    test_HasKerningPairs();
    test_ComputeGlyphOrigins();
    test_inmemory_file_loader();
    test_GetGlyphImageFormats();
    test_CreateCustomRenderingParams();

    IDWriteFactory_Release(factory);
}
