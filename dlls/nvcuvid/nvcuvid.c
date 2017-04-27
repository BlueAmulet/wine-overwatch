/*
 * Copyright (C) 2015 Michael Müller
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

#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/library.h"
#include "wine/debug.h"
#include "nvcuvid.h"

WINE_DEFAULT_DEBUG_CHANNEL(nvcuvid);

static CUresult (*pcuvidCreateDecoder)(CUvideodecoder *phDecoder, LINUX_CUVIDDECODECREATEINFO *pdci);
static CUresult (*pcuvidCreateVideoParser)(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams);
static CUresult (*pcuvidCreateVideoSource)(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams);
static CUresult (*pcuvidCtxLock)(CUvideoctxlock lck, unsigned int reserved_flags);
static CUresult (*pcuvidCtxLockCreate)(CUvideoctxlock *pLock, CUcontext ctx);
static CUresult (*pcuvidCtxLockDestroy)(CUvideoctxlock lck);
static CUresult (*pcuvidCtxUnlock)(CUvideoctxlock lck, unsigned int reserved_flags);
static CUresult (*pcuvidDecodePicture)(CUvideodecoder hDecoder, CUVIDPICPARAMS *pPicParams);
static CUresult (*pcuvidDestroyDecoder)(CUvideodecoder hDecoder);
static CUresult (*pcuvidDestroyVideoParser)(CUvideoparser obj);
static CUresult (*pcuvidDestroyVideoSource)(CUvideosource obj);
static CUresult (*pcuvidGetSourceAudioFormat)(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags);
static CUresult (*pcuvidGetSourceVideoFormat)(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags);
/* static CUresult (*pcuvidGetVideoFrameSurface)(CUvideodecoder hDecoder, int nPicIdx, void **pSrcSurface); */
static cudaVideoState (*pcuvidGetVideoSourceState)(CUvideosource obj);
static CUresult (*pcuvidMapVideoFrame)(CUvideodecoder hDecoder, int nPicIdx, unsigned int *pDevPtr, unsigned int *pPitch, CUVIDPROCPARAMS *pVPP);
static CUresult (*pcuvidParseVideoData)(CUvideoparser obj, LINUX_CUVIDSOURCEDATAPACKET *pPacket);
static CUresult (*pcuvidSetVideoSourceState)(CUvideosource obj, cudaVideoState state);
static CUresult (*pcuvidUnmapVideoFrame)(CUvideodecoder hDecoder, unsigned int DevPtr);

/* x86_64 */
#ifdef __x86_64__
static CUresult (*pcuvidMapVideoFrame64)(CUvideodecoder hDecoder, int nPicIdx, unsigned long long *pDevPtr,
                                         unsigned int *pPitch, CUVIDPROCPARAMS *pVPP);
static CUresult (*pcuvidUnmapVideoFrame64)(CUvideodecoder hDecoder, unsigned long long DevPtr);
#endif

static void *cuvid_handle = NULL;

static BOOL load_functions(void)
{
    cuvid_handle = wine_dlopen("libnvcuvid.so", RTLD_NOW, NULL, 0);

    if (!cuvid_handle)
    {
        FIXME("Wine cannot find the libnvcuvid.so library, CUDA gpu decoding support disabled.\n");
        return FALSE;
    }

    #define LOAD_FUNCPTR(f) if((p##f = wine_dlsym(cuvid_handle, #f, NULL, 0)) == NULL){FIXME("Can't find symbol %s\n", #f); return FALSE;}

    LOAD_FUNCPTR(cuvidCreateDecoder);
    LOAD_FUNCPTR(cuvidCreateVideoParser);
    LOAD_FUNCPTR(cuvidCreateVideoSource);
    LOAD_FUNCPTR(cuvidCtxLock);
    LOAD_FUNCPTR(cuvidCtxLockCreate);
    LOAD_FUNCPTR(cuvidCtxLockDestroy);
    LOAD_FUNCPTR(cuvidCtxUnlock);
    LOAD_FUNCPTR(cuvidDecodePicture);
    LOAD_FUNCPTR(cuvidDestroyDecoder);
    LOAD_FUNCPTR(cuvidDestroyVideoParser);
    LOAD_FUNCPTR(cuvidDestroyVideoSource);
    LOAD_FUNCPTR(cuvidGetSourceAudioFormat);
    LOAD_FUNCPTR(cuvidGetSourceVideoFormat);
    /* LOAD_FUNCPTR(cuvidGetVideoFrameSurface); */
    LOAD_FUNCPTR(cuvidGetVideoSourceState)
    LOAD_FUNCPTR(cuvidMapVideoFrame);
    LOAD_FUNCPTR(cuvidParseVideoData);
    LOAD_FUNCPTR(cuvidSetVideoSourceState);
    LOAD_FUNCPTR(cuvidUnmapVideoFrame);

    /* x86_64 */
#ifdef __x86_64__
    LOAD_FUNCPTR(cuvidMapVideoFrame64);
    LOAD_FUNCPTR(cuvidUnmapVideoFrame64);
#endif

    #undef LOAD_FUNCPTR

    return TRUE;
}

struct fake_parser
{
    CUvideoparser *orig_parser;
    int (WINAPI *orig_SequenceCallback)(void *, CUVIDEOFORMAT *);
    int (WINAPI *orig_DecodePicture)(void *, CUVIDPICPARAMS *);
    int (WINAPI *orig_DisplayPicture)(void *, CUVIDPARSERDISPINFO *);
    void *orig_data;
};

struct fake_source
{
    CUvideoparser *orig_source;
    int (WINAPI *orig_VideoDataHandler)(void *, CUVIDSOURCEDATAPACKET *);
    int (WINAPI *orig_AudioDataHandler)(void *, CUVIDSOURCEDATAPACKET *);
    void *orig_data;
};

static int relay_SequenceCallback(void *data, CUVIDEOFORMAT *fmt)
{
    struct fake_parser *parser = data;
    TRACE("(%p, %p)\n", data, fmt);
    return parser->orig_SequenceCallback(parser->orig_data, fmt);
}

static int relay_DecodePicture(void *data, CUVIDPICPARAMS *params)
{
    struct fake_parser *parser = data;
    TRACE("(%p, %p)\n", data, params);
    return parser->orig_DecodePicture(parser->orig_data, params);
}

static int relay_DisplayPicture(void *data, CUVIDPARSERDISPINFO *info)
{
    struct fake_parser *parser = data;
    TRACE("(%p, %p)\n", data, info);
    return parser->orig_DisplayPicture(parser->orig_data, info);
}

static int relay_VideoDataHandler(void *data, LINUX_CUVIDSOURCEDATAPACKET *pkt)
{
    CUVIDSOURCEDATAPACKET win_pkt;
    struct fake_source *source = data;
    TRACE("(%p, %p)\n", data, pkt);

    if (pkt)
    {
        memset(&win_pkt, 0, sizeof(win_pkt));
        win_pkt.flags          = pkt->flags;
        win_pkt.payload_size   = pkt->payload_size;
        win_pkt.payload        = pkt->payload;
        win_pkt.timestamp      = pkt->timestamp;
    }

    return source->orig_VideoDataHandler(source->orig_data, pkt ? &win_pkt : NULL);
}

static int relay_AudioDataHandler(void *data, LINUX_CUVIDSOURCEDATAPACKET *pkt)
{
    CUVIDSOURCEDATAPACKET win_pkt;
    struct fake_source *source = data;
    TRACE("(%p, %p)\n", data, pkt);

    if (pkt)
    {
        memset(&win_pkt, 0, sizeof(win_pkt));
        win_pkt.flags          = pkt->flags;
        win_pkt.payload_size   = pkt->payload_size;
        win_pkt.payload        = pkt->payload;
        win_pkt.timestamp      = pkt->timestamp;
    }

    return source->orig_AudioDataHandler(source->orig_data, pkt ? &win_pkt : NULL);
}

CUresult WINAPI wine_cuvidCreateDecoder(CUvideodecoder *phDecoder, CUVIDDECODECREATEINFO *pdci)
{
    LINUX_CUVIDDECODECREATEINFO linux_dci;

    TRACE("(%p, %p)\n", phDecoder, pdci);

    if (pdci)
    {
        memset(&linux_dci, 0, sizeof(linux_dci));
        linux_dci.ulWidth               = pdci->ulWidth;
        linux_dci.ulHeight              = pdci->ulHeight;
        linux_dci.ulNumDecodeSurfaces   = pdci->ulNumDecodeSurfaces;
        linux_dci.CodecType             = pdci->CodecType;
        linux_dci.ChromaFormat          = pdci->ChromaFormat;
        linux_dci.ulCreationFlags       = pdci->ulCreationFlags;
        linux_dci.Reserved1[0]          = pdci->Reserved1[0];
        linux_dci.Reserved1[1]          = pdci->Reserved1[1];
        linux_dci.Reserved1[2]          = pdci->Reserved1[2];
        linux_dci.Reserved1[3]          = pdci->Reserved1[3];
        linux_dci.Reserved1[4]          = pdci->Reserved1[4];
        linux_dci.display_area.left     = pdci->display_area.left;
        linux_dci.display_area.top      = pdci->display_area.top;
        linux_dci.display_area.right    = pdci->display_area.right;
        linux_dci.display_area.bottom   = pdci->display_area.bottom;
        linux_dci.OutputFormat          = pdci->OutputFormat;
        linux_dci.DeinterlaceMode       = pdci->DeinterlaceMode;
        linux_dci.ulTargetWidth         = pdci->ulTargetWidth;
        linux_dci.ulTargetHeight        = pdci->ulTargetHeight;
        linux_dci.ulNumOutputSurfaces   = pdci->ulNumOutputSurfaces;
        linux_dci.vidLock               = pdci->vidLock;
        linux_dci.target_rect.left      = pdci->target_rect.left;
        linux_dci.target_rect.top       = pdci->target_rect.top;
        linux_dci.target_rect.right     = pdci->target_rect.right;
        linux_dci.target_rect.bottom    = pdci->target_rect.bottom;
        linux_dci.Reserved2[0]          = pdci->Reserved2[0];
        linux_dci.Reserved2[1]          = pdci->Reserved2[1];
        linux_dci.Reserved2[2]          = pdci->Reserved2[2];
        linux_dci.Reserved2[3]          = pdci->Reserved2[3];
        linux_dci.Reserved2[4]          = pdci->Reserved2[4];
    }

    return pcuvidCreateDecoder(phDecoder, pdci ? &linux_dci : NULL);
}

CUresult WINAPI wine_cuvidCreateVideoParser(CUvideoparser *pObj, CUVIDPARSERPARAMS *pParams)
{
    struct fake_parser *parser;
    CUVIDPARSERPARAMS fake_params;
    CUresult ret;

    TRACE("(%p, %p)\n", pObj, pParams);

    /* FIXME: check error codes */
    if (!pObj || !pParams)
        return CUDA_ERROR_INVALID_VALUE;

    parser = HeapAlloc(GetProcessHeap(), 0, sizeof(*parser));
    if (!parser)
        return CUDA_ERROR_OUT_OF_MEMORY;

    memcpy(&fake_params, pParams, sizeof(fake_params));

    if (pParams->pfnSequenceCallback)
    {
        parser->orig_SequenceCallback = pParams->pfnSequenceCallback;
        fake_params.pfnSequenceCallback = &relay_SequenceCallback;
    }

    if (pParams->pfnDecodePicture)
    {
        parser->orig_DecodePicture = pParams->pfnDecodePicture;
        fake_params.pfnDecodePicture = &relay_DecodePicture;
    }

    if (pParams->pfnDisplayPicture)
    {
        parser->orig_DisplayPicture = pParams->pfnDisplayPicture;
        fake_params.pfnDisplayPicture = &relay_DisplayPicture;
    }

    parser->orig_data = pParams->pUserData;
    fake_params.pUserData = parser;

    ret = pcuvidCreateVideoParser((void *)&parser->orig_parser, &fake_params);
    if (ret)
    {
        HeapFree(GetProcessHeap(), 0, parser);
        return ret;
    }

    *pObj = (void *)parser;
    return CUDA_SUCCESS;
}

/* FIXME: Should we pay attention to AreFileApisANSI() ? */
static BOOL get_unix_path(ANSI_STRING *unix_name, const char *filename)
{
    UNICODE_STRING dospathW, ntpathW;
    ANSI_STRING dospath;
    NTSTATUS status;

    RtlInitAnsiString(&dospath, filename);

    if (RtlAnsiStringToUnicodeString(&dospathW, &dospath, TRUE))
        return FALSE;

    if (!RtlDosPathNameToNtPathName_U(dospathW.Buffer, &ntpathW, NULL, NULL))
    {
        RtlFreeUnicodeString(&dospathW);
        return FALSE;
    }

    status = wine_nt_to_unix_file_name(&ntpathW, unix_name, FILE_OPEN, FALSE);

    RtlFreeUnicodeString(&ntpathW);
    RtlFreeUnicodeString(&dospathW);
    return !status;
}

CUresult WINAPI wine_cuvidCreateVideoSource(CUvideosource *pObj, const char *pszFileName, CUVIDSOURCEPARAMS *pParams)
{
    struct fake_source *source;
    CUVIDSOURCEPARAMS fake_params;
    ANSI_STRING unix_name;
    CUresult ret;

    TRACE("(%p, %s, %p)\n", pObj, pszFileName, pParams);

    /* FIXME: check error codes */
    if (!pObj || !pParams)
        return CUDA_ERROR_INVALID_VALUE;

    if (!pszFileName)
        return CUDA_ERROR_UNKNOWN;

    if (!get_unix_path(&unix_name, pszFileName))
        return CUDA_ERROR_UNKNOWN;

    source = HeapAlloc(GetProcessHeap(), 0, sizeof(*source));
    if (!source)
    {
        RtlFreeAnsiString(&unix_name);
        return CUDA_ERROR_OUT_OF_MEMORY;
    }

    memcpy(&fake_params, pParams, sizeof(fake_params));

    if (pParams->pfnVideoDataHandler)
    {
        source->orig_VideoDataHandler = pParams->pfnVideoDataHandler;
        fake_params.pfnVideoDataHandler = &relay_VideoDataHandler;
    }

    if (pParams->pfnAudioDataHandler)
    {
        source->orig_AudioDataHandler = pParams->pfnAudioDataHandler;
        fake_params.pfnAudioDataHandler = &relay_AudioDataHandler;
    }

    source->orig_data = pParams->pUserData;
    fake_params.pUserData = source;

    ret = pcuvidCreateVideoSource((void *)&source->orig_source, unix_name.Buffer, &fake_params);
    RtlFreeAnsiString(&unix_name);

    if (ret)
    {
        HeapFree( GetProcessHeap(), 0, source );
        return ret;
    }

    *pObj = (void *)source;
    return ret;
}

CUresult WINAPI wine_cuvidCtxLock(CUvideoctxlock lck, unsigned int reserved_flags)
{
    TRACE("(%p, %u)\n", lck, reserved_flags);
    return pcuvidCtxLock(lck, reserved_flags);
}

CUresult WINAPI wine_cuvidCtxLockCreate(CUvideoctxlock *pLock, CUcontext ctx)
{
    TRACE("(%p, %p)\n", pLock, ctx);
    return pcuvidCtxLockCreate(pLock, ctx);
}

CUresult WINAPI wine_cuvidCtxLockDestroy(CUvideoctxlock lck)
{
    TRACE("(%p)\n", lck);
    return pcuvidCtxLockDestroy(lck);
}

CUresult WINAPI wine_cuvidCtxUnlock(CUvideoctxlock lck, unsigned int reserved_flags)
{
    TRACE("(%p, %u)\n", lck, reserved_flags);
    return pcuvidCtxUnlock(lck, reserved_flags);
}

CUresult WINAPI wine_cuvidDecodePicture(CUvideodecoder hDecoder, CUVIDPICPARAMS *pPicParams)
{
    TRACE("(%p, %p)\n", hDecoder, pPicParams);

    /* The size of CUVIDH264PICPARAMS is actually 4 byte larger on Windows than on Linux,
     * but, as it is contained only as a subfield in CUVIDPICPARAMS the total size matches. */

    return pcuvidDecodePicture(hDecoder, pPicParams);
}

CUresult WINAPI wine_cuvidDestroyDecoder(CUvideodecoder hDecoder)
{
    TRACE("(%p)\n", hDecoder);
    return pcuvidDestroyDecoder(hDecoder);
}

CUresult WINAPI wine_cuvidDestroyVideoParser(CUvideoparser obj)
{
    struct fake_parser *parser = (void *)obj;
    CUresult ret;

    TRACE("(%p)\n", obj);

    if (!parser) return CUDA_ERROR_INVALID_VALUE; /* FIXME */
    ret = pcuvidDestroyVideoParser(parser->orig_parser);

    HeapFree(GetProcessHeap(), 0, parser);
    return ret;
}

CUresult WINAPI wine_cuvidDestroyVideoSource(CUvideosource obj)
{
    struct fake_source *source = (void *)obj;
    CUresult ret;

    TRACE("(%p)\n", obj);

    if (!source) return CUDA_ERROR_INVALID_VALUE; /* FIXME */
    ret = pcuvidDestroyVideoSource(source->orig_source);

    HeapFree(GetProcessHeap(), 0, source);
    return ret;
}

CUresult WINAPI wine_cuvidGetSourceAudioFormat(CUvideosource obj, CUAUDIOFORMAT *paudfmt, unsigned int flags)
{
    struct fake_source *source = (void *)obj;
    TRACE("(%p, %p, %u)\n", obj, paudfmt, flags);
    if (!source) return CUDA_ERROR_INVALID_VALUE; /* FIXME */
    return pcuvidGetSourceAudioFormat(source->orig_source, paudfmt, flags);
}

CUresult WINAPI wine_cuvidGetSourceVideoFormat(CUvideosource obj, CUVIDEOFORMAT *pvidfmt, unsigned int flags)
{
    struct fake_source *source = (void *)obj;
    TRACE("(%p, %p, %u)\n", obj, pvidfmt, flags);
    if (!source) return CUDA_ERROR_INVALID_VALUE; /* FIXME */
    return pcuvidGetSourceVideoFormat(source->orig_source, pvidfmt, flags);
}

/*
CUresult WINAPI wine_cuvidGetVideoFrameSurface(CUvideodecoder hDecoder, int nPicIdx, void **pSrcSurface)
{
    TRACE("(%p, %d, %p)\n", hDecoder, nPicIdx, pSrcSurface);
    return pcuvidGetVideoFrameSurface(hDecoder, nPicIdx, pSrcSurface);
}
*/

cudaVideoState WINAPI wine_cuvidGetVideoSourceState(CUvideosource obj)
{
    struct fake_source *source = (void *)obj;
    TRACE("(%p)\n", obj);
    if (!source) return CUDA_ERROR_INVALID_VALUE; /* FIXME */
    return pcuvidGetVideoSourceState(source->orig_source);
}

CUresult WINAPI wine_cuvidMapVideoFrame(CUvideodecoder hDecoder, int nPicIdx, unsigned int *pDevPtr, unsigned int *pPitch, CUVIDPROCPARAMS *pVPP)
{
    TRACE("(%p, %d, %p, %p, %p)\n", hDecoder, nPicIdx, pDevPtr, pPitch, pVPP);
    return pcuvidMapVideoFrame(hDecoder, nPicIdx, pDevPtr, pPitch, pVPP);
}

CUresult WINAPI wine_cuvidParseVideoData(CUvideoparser obj, CUVIDSOURCEDATAPACKET *pPacket)
{
    LINUX_CUVIDSOURCEDATAPACKET linux_packet;
    struct fake_parser *parser = (void *)obj;
    TRACE("(%p, %p)\n", obj, pPacket);
    if (!parser) return CUDA_ERROR_INVALID_VALUE; /* FIXME */

    if (pPacket)
    {
        memset(&linux_packet, 0, sizeof(linux_packet));
        linux_packet.flags          = pPacket->flags;
        linux_packet.payload_size   = pPacket->payload_size;
        linux_packet.payload        = pPacket->payload;
        linux_packet.timestamp      = pPacket->timestamp;
    }

    return pcuvidParseVideoData(parser->orig_parser, pPacket ? &linux_packet : NULL);
}

CUresult WINAPI wine_cuvidSetVideoSourceState(CUvideosource obj, cudaVideoState state)
{
    struct fake_source *source = (void *)obj;
    TRACE("(%p, %d)\n", obj, state);
    if (!source) return CUDA_ERROR_INVALID_VALUE;
    return pcuvidSetVideoSourceState(source->orig_source, state);
}

CUresult WINAPI wine_cuvidUnmapVideoFrame(CUvideodecoder hDecoder, unsigned int DevPtr)
{
    TRACE("(%p, %u)\n", hDecoder, DevPtr);
    return pcuvidUnmapVideoFrame(hDecoder, DevPtr);
}

/*
 * On Linux those functions are only exported x86_64, but on Windows they also exist in the 32-bit version.
 */

CUresult WINAPI wine_cuvidMapVideoFrame64(CUvideodecoder hDecoder, int nPicIdx, unsigned long long *pDevPtr,
                                          unsigned int *pPitch, CUVIDPROCPARAMS *pVPP)
{
    TRACE("(%p, %d, %p, %p, %p)\n", hDecoder, nPicIdx, pDevPtr, pPitch, pVPP);
#ifdef __x86_64__
    return pcuvidMapVideoFrame64(hDecoder, nPicIdx, pDevPtr, pPitch, pVPP);
#else
    FIXME("not supported\n");
    return CUDA_ERROR_NOT_SUPPORTED;
#endif
}

CUresult WINAPI wine_cuvidUnmapVideoFrame64(CUvideodecoder hDecoder, unsigned long long DevPtr)
{
    TRACE("(%p, %llu)\n", hDecoder, DevPtr);
#ifdef __x86_64__
    return pcuvidUnmapVideoFrame64(hDecoder, DevPtr);
#else
    FIXME("not supported\n");
    return CUDA_ERROR_NOT_SUPPORTED;
#endif
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    TRACE("(%p, %u, %p)\n", instance, reason, reserved);

    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(instance);
            if (!load_functions()) return FALSE;
            break;
        case DLL_PROCESS_DETACH:
            if (reserved) break;
            if (cuvid_handle) wine_dlclose(cuvid_handle, NULL, 0);
            break;
    }

    return TRUE;
}
