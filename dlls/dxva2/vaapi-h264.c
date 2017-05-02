/*
 * Copyright 2014-2015 Michael Müller for Pipelight
 * Copyright 2014-2015 Sebastian Lackner for Pipelight
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
#include <assert.h>
#include "windef.h"
#include "winbase.h"

#include "wine/debug.h"

#define COBJMACROS
#include "d3d9.h"
#include "dxva2api.h"
#include "dxva.h"
#include "dxva2_private.h"

#ifdef HAVE_VAAPI

WINE_DEFAULT_DEBUG_CHANNEL(dxva2);

#define MAX_SLICES 4096

#define SLICE_TYPE_P   0
#define SLICE_TYPE_B   1
#define SLICE_TYPE_I   2
#define SLICE_TYPE_SP  3
#define SLICE_TYPE_SI  4

static inline UINT estimate_maximum_slice_size( UINT width, UINT height )
{
    return (3 * width * height * min(width, height)) / max(width, height);
}

typedef struct
{
    IWineVideoDecoder IWineVideoDecoder_iface;
    LONG refCount;
    IWineVideoService *service;

    /* video attributes */
    UINT width;
    UINT height;
    D3DFORMAT format;
    DWORD maxSliceSize;
    VAImage vaImage;

    /* surfaces used by this decoder */
    UINT surfaceCount;
    VASurfaceID *surfaces;
    UINT currentSurface;

    /* configuration and context of the decoder */
    VAConfigID config;
    VAContextID context;

    /* slice buffer id */
    VABufferID vaBitstream;
    DXVA_PicParams_H264 d3dPictureParam;
    DXVA_Qmatrix_H264 d3dQMatrix;
    DXVA_Slice_H264_Long d3dSliceInfo[MAX_SLICES];
} WineVideoDecoderH264Impl;

/* caller has to hold the vaapi_lock */
static HRESULT process_picture_parameters( WineVideoDecoderH264Impl *This, const DXVA2_DecodeBufferDesc *desc )
{
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAPictureParameterBufferH264 params;
    VABufferID vaPictureParam;
    VAStatus status;
    unsigned int i;

    if (desc->DataSize != sizeof(This->d3dPictureParam))
        FIXME("unexpected picture parameter buffer size %u\n", desc->DataSize);

    /* check for valid parameters */
    if (This->d3dPictureParam.CurrPic.Index7Bits > This->surfaceCount)
    {
        ERR("invalid picture reference index\n");
        return E_FAIL;
    }

    if (!This->d3dPictureParam.ContinuationFlag)
        FIXME("ContinuationFlag = 0 unsupported, decoding may fail\n");


    memset(&params, 0, sizeof(params));

    params.CurrPic.picture_id = This->surfaces[This->d3dPictureParam.CurrPic.Index7Bits];
    params.CurrPic.frame_idx = This->d3dPictureParam.frame_num; /* FIXME - guessed */

    if (This->d3dPictureParam.field_pic_flag)
        params.CurrPic.flags = This->d3dPictureParam.CurrPic.AssociatedFlag ? VA_PICTURE_H264_BOTTOM_FIELD : VA_PICTURE_H264_TOP_FIELD;

    if (This->d3dPictureParam.RefPicFlag)
        params.CurrPic.flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        /* FIXME: Distinguish between short term and longterm references */

    params.CurrPic.TopFieldOrderCnt = This->d3dPictureParam.CurrFieldOrderCnt[0] & 0xffff;
    params.CurrPic.BottomFieldOrderCnt = This->d3dPictureParam.CurrFieldOrderCnt[1] & 0xffff;

    for (i = 0; i < 16; i++)
    {
        unsigned int flags = 0;

        if (This->d3dPictureParam.RefFrameList[i].Index7Bits == 127)
        {
            params.ReferenceFrames[i].picture_id = VA_INVALID_ID;
            params.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
            continue;
        }

        if (This->d3dPictureParam.RefFrameList[i].Index7Bits >= This->surfaceCount)
        {
            FIXME("Out of range reference picture!\n");
            params.ReferenceFrames[i].picture_id = VA_INVALID_ID;
            params.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
            continue;
        }

        params.ReferenceFrames[i].picture_id = This->surfaces[This->d3dPictureParam.RefFrameList[i].Index7Bits];
        params.ReferenceFrames[i].frame_idx  = This->d3dPictureParam.FrameNumList[i];

        /* TODO: check flag conversion again */
        if (This->d3dPictureParam.UsedForReferenceFlags & (1 << (2 * i + 0)))
            flags |= VA_PICTURE_H264_TOP_FIELD;

        if (This->d3dPictureParam.UsedForReferenceFlags & (1 << (2 * i + 1)))
            flags |= VA_PICTURE_H264_BOTTOM_FIELD;

        if (flags & VA_PICTURE_H264_TOP_FIELD || flags & VA_PICTURE_H264_BOTTOM_FIELD)
        {
            if (This->d3dPictureParam.RefFrameList[i].AssociatedFlag)
                flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
            else
                flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        }

        /* remove top and bottom field flag if we got a full frame */
        if ((flags & (VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD)) ==
            (VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD))
        {
            flags &= ~(VA_PICTURE_H264_TOP_FIELD | VA_PICTURE_H264_BOTTOM_FIELD);
        }

        params.ReferenceFrames[i].flags = flags;

        params.ReferenceFrames[i].TopFieldOrderCnt    = This->d3dPictureParam.FieldOrderCntList[i][0] & 0xFFFF;
        params.ReferenceFrames[i].BottomFieldOrderCnt = This->d3dPictureParam.FieldOrderCntList[i][1] & 0xFFFF;
    }

    params.picture_width_in_mbs_minus1                              = This->d3dPictureParam.wFrameWidthInMbsMinus1;
    params.picture_height_in_mbs_minus1                             = This->d3dPictureParam.wFrameHeightInMbsMinus1;
    params.bit_depth_luma_minus8                                    = This->d3dPictureParam.bit_depth_luma_minus8;
    params.bit_depth_chroma_minus8                                  = This->d3dPictureParam.bit_depth_chroma_minus8;
    params.num_ref_frames                                           = This->d3dPictureParam.num_ref_frames;

    params.seq_fields.bits.chroma_format_idc                        = This->d3dPictureParam.chroma_format_idc;
    params.seq_fields.bits.residual_colour_transform_flag           = This->d3dPictureParam.residual_colour_transform_flag;
    params.seq_fields.bits.gaps_in_frame_num_value_allowed_flag     = FALSE; /* FIXME */
    params.seq_fields.bits.frame_mbs_only_flag                      = This->d3dPictureParam.frame_mbs_only_flag;
    params.seq_fields.bits.mb_adaptive_frame_field_flag             = This->d3dPictureParam.MbaffFrameFlag;
    params.seq_fields.bits.direct_8x8_inference_flag                = This->d3dPictureParam.direct_8x8_inference_flag;
    params.seq_fields.bits.MinLumaBiPredSize8x8                     = This->d3dPictureParam.MinLumaBipredSize8x8Flag;
    params.seq_fields.bits.log2_max_frame_num_minus4                = This->d3dPictureParam.log2_max_frame_num_minus4;
    params.seq_fields.bits.pic_order_cnt_type                       = This->d3dPictureParam.pic_order_cnt_type;
    params.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4        = This->d3dPictureParam.log2_max_pic_order_cnt_lsb_minus4;
    params.seq_fields.bits.delta_pic_order_always_zero_flag         = This->d3dPictureParam.delta_pic_order_always_zero_flag;

    params.num_slice_groups_minus1                                  = This->d3dPictureParam.num_slice_groups_minus1;
    params.slice_group_map_type                                     = This->d3dPictureParam.slice_group_map_type;
    params.slice_group_change_rate_minus1                           = This->d3dPictureParam.slice_group_change_rate_minus1;
    params.pic_init_qp_minus26                                      = This->d3dPictureParam.pic_init_qp_minus26;
    params.pic_init_qs_minus26                                      = This->d3dPictureParam.pic_init_qs_minus26;
    params.chroma_qp_index_offset                                   = This->d3dPictureParam.chroma_qp_index_offset;
    params.second_chroma_qp_index_offset                            = This->d3dPictureParam.second_chroma_qp_index_offset;
    params.pic_fields.bits.entropy_coding_mode_flag                 = This->d3dPictureParam.entropy_coding_mode_flag;

    params.pic_fields.bits.weighted_pred_flag                       = This->d3dPictureParam.weighted_pred_flag;
    params.pic_fields.bits.weighted_bipred_idc                      = This->d3dPictureParam.weighted_bipred_idc;
    params.pic_fields.bits.transform_8x8_mode_flag                  = This->d3dPictureParam.transform_8x8_mode_flag;
    params.pic_fields.bits.field_pic_flag                           = This->d3dPictureParam.field_pic_flag;
    params.pic_fields.bits.constrained_intra_pred_flag              = This->d3dPictureParam.constrained_intra_pred_flag;
    params.pic_fields.bits.pic_order_present_flag                   = This->d3dPictureParam.pic_order_present_flag;
    params.pic_fields.bits.deblocking_filter_control_present_flag   = This->d3dPictureParam.deblocking_filter_control_present_flag;
    params.pic_fields.bits.redundant_pic_cnt_present_flag           = This->d3dPictureParam.redundant_pic_cnt_present_flag;
    params.pic_fields.bits.reference_pic_flag                       = This->d3dPictureParam.RefPicFlag;

    params.frame_num = This->d3dPictureParam.frame_num;

    status = pvaCreateBuffer(va_display, This->context, VAPictureParameterBufferType,
                             sizeof(params), 1, &params, &vaPictureParam);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to create picture parameter buffer: %s (0x%x)\n", pvaErrorStr(status), status);
        return E_FAIL;
    }

    status = pvaRenderPicture(va_display, This->context, &vaPictureParam, 1);
    pvaDestroyBuffer(va_display, vaPictureParam);

    if (status == VA_STATUS_SUCCESS)
        return S_OK;

    ERR("failed to process picture parameter buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    return E_FAIL;
}

/* caller has to hold the vaapi_lock */
static HRESULT process_quantization_matrix( WineVideoDecoderH264Impl *This, const DXVA2_DecodeBufferDesc *desc )
{
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAIQMatrixBufferH264 matrix;
    VABufferID vaIQMatrix;
    VAStatus status;

    if (desc->DataSize != sizeof(This->d3dQMatrix))
        FIXME("unexpected quantization matrix buffer size %u\n", desc->DataSize);

    memcpy(&matrix.ScalingList4x4, &This->d3dQMatrix.bScalingLists4x4, sizeof(matrix.ScalingList4x4));
    memcpy(&matrix.ScalingList8x8, &This->d3dQMatrix.bScalingLists8x8, sizeof(matrix.ScalingList8x8));

    status = pvaCreateBuffer(va_display, This->context, VAIQMatrixBufferType,
                             sizeof(matrix), 1, &matrix, &vaIQMatrix);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to create quantization matrix buffer: %s (0x%x)\n", pvaErrorStr(status), status);
        return E_FAIL;
    }

    status = pvaRenderPicture(va_display, This->context, &vaIQMatrix, 1);
    pvaDestroyBuffer(va_display, vaIQMatrix);

    if (status == VA_STATUS_SUCCESS)
        return S_OK;

    ERR("failed to process quantization matrix buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    return E_FAIL;
}

static void fill_reference_picture( WineVideoDecoderH264Impl *This, VAPictureH264 *pic, DXVA_PicEntry_H264 *dxva_pic )
{
    unsigned int i;

    pic->picture_id = dxva_pic->Index7Bits < This->surfaceCount ? This->surfaces[dxva_pic->Index7Bits] : VA_INVALID_ID;
    pic->frame_idx = 0;

    if (This->d3dPictureParam.field_pic_flag)
        pic->flags = dxva_pic->AssociatedFlag ? VA_PICTURE_H264_BOTTOM_FIELD : VA_PICTURE_H264_TOP_FIELD;
    else
        pic->flags = 0;

    pic->TopFieldOrderCnt = 0;
    pic->BottomFieldOrderCnt = 0;

    for (i = 0; i < 16; i++)
    {
        if (This->d3dPictureParam.RefFrameList[i].Index7Bits != dxva_pic->Index7Bits)
            continue;

        if ((This->d3dPictureParam.UsedForReferenceFlags & (1 << (2 * i + 0))) ||
            (This->d3dPictureParam.UsedForReferenceFlags & (1 << (2 * i + 1))))
        {
             if (This->d3dPictureParam.RefFrameList[i].AssociatedFlag)
                 pic->flags |= VA_PICTURE_H264_LONG_TERM_REFERENCE;
             else
                 pic->flags |= VA_PICTURE_H264_SHORT_TERM_REFERENCE;
        }

        pic->frame_idx           = This->d3dPictureParam.FrameNumList[i];
        pic->TopFieldOrderCnt    = This->d3dPictureParam.FieldOrderCntList[i][0] & 0xFFFF;
        pic->BottomFieldOrderCnt = This->d3dPictureParam.FieldOrderCntList[i][1] & 0xFFFF;

        return;
    }

    WARN("Reference not found!\n");
}

/* caller has to hold the vaapi_lock */
static HRESULT process_slice_control_buffer( WineVideoDecoderH264Impl *This, const DXVA2_DecodeBufferDesc *desc )
{
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VASliceParameterBufferH264 *slice = NULL;
    VABufferID vaSliceInfo;
    VAStatus status;
    unsigned int sliceCount, i, j;

    /* check for valid parameters */
    if (!desc->DataSize || desc->DataSize % sizeof(DXVA_Slice_H264_Long) != 0)
    {
        ERR("slice control buffer size is invalid (got %u, expected multiple of %lu)\n",
            desc->DataSize, (SIZE_T)sizeof(DXVA_Slice_H264_Long));
        return E_FAIL;
    }

    sliceCount = desc->DataSize / sizeof(DXVA_Slice_H264_Long);

    status = pvaCreateBuffer(va_display, This->context, VASliceParameterBufferType,
                             sizeof(VASliceParameterBufferH264), sliceCount, NULL, &vaSliceInfo);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to create slice control buffer: %s (0x%x)\n", pvaErrorStr(status), status);
        return E_FAIL;
    }

    status = pvaMapBuffer(va_display, vaSliceInfo, (void **)&slice);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to map slice control buffer: %s (0x%x)\n", pvaErrorStr(status), status);
        goto err;
    }

    for (i = 0; i < sliceCount; i++)
    {
        slice[i].slice_data_offset              = This->d3dSliceInfo[i].BSNALunitDataLocation + 3;
        slice[i].slice_data_size                = This->d3dSliceInfo[i].SliceBytesInBuffer - 3;
        slice[i].slice_data_flag                = VA_SLICE_DATA_FLAG_ALL;
        slice[i].slice_data_bit_offset          = This->d3dSliceInfo[i].BitOffsetToSliceData + 8;
        slice[i].first_mb_in_slice              = This->d3dSliceInfo[i].first_mb_in_slice;
        slice[i].slice_type                     = This->d3dSliceInfo[i].slice_type % 5;
        slice[i].direct_spatial_mv_pred_flag    = This->d3dSliceInfo[i].direct_spatial_mv_pred_flag;
        if (slice[i].slice_type == SLICE_TYPE_I)
        {
            slice[i].num_ref_idx_l0_active_minus1   = 0;
            slice[i].num_ref_idx_l1_active_minus1   = 0;
        }
        else
        {
            slice[i].num_ref_idx_l0_active_minus1   = This->d3dSliceInfo[i].num_ref_idx_l0_active_minus1;
            slice[i].num_ref_idx_l1_active_minus1   = This->d3dSliceInfo[i].num_ref_idx_l1_active_minus1;
        }
        slice[i].cabac_init_idc                 = This->d3dSliceInfo[i].cabac_init_idc;
        slice[i].slice_qp_delta                 = This->d3dSliceInfo[i].slice_qp_delta;
        slice[i].disable_deblocking_filter_idc  = This->d3dSliceInfo[i].disable_deblocking_filter_idc;
        slice[i].slice_alpha_c0_offset_div2     = This->d3dSliceInfo[i].slice_alpha_c0_offset_div2;
        slice[i].slice_beta_offset_div2         = This->d3dSliceInfo[i].slice_beta_offset_div2;

        for (j = 0; j < 32; j++)
        {
            if (This->d3dSliceInfo[i].RefPicList[0][j].Index7Bits < This->surfaceCount)
                fill_reference_picture(This, &slice[i].RefPicList0[j], &This->d3dSliceInfo[i].RefPicList[0][j]);
            else
            {
                slice[i].RefPicList0[j].picture_id  = VA_INVALID_ID;
                slice[i].RefPicList0[j].flags       = VA_PICTURE_H264_INVALID;
            }

            if (This->d3dSliceInfo[i].RefPicList[1][j].Index7Bits < This->surfaceCount)
                fill_reference_picture(This, &slice[i].RefPicList1[j], &This->d3dSliceInfo[i].RefPicList[1][j]);
            else
            {
                slice[i].RefPicList1[j].picture_id  = VA_INVALID_ID;
                slice[i].RefPicList1[j].flags       = VA_PICTURE_H264_INVALID;
            }

        }

        slice[i].luma_log2_weight_denom = This->d3dSliceInfo[i].luma_log2_weight_denom;
        slice[i].chroma_log2_weight_denom = This->d3dSliceInfo[i].chroma_log2_weight_denom;

        slice[i].luma_weight_l0_flag    = TRUE; /* FIXME */
        slice[i].chroma_weight_l0_flag  = TRUE; /* FIXME */
        slice[i].luma_weight_l1_flag    = TRUE; /* FIXME */
        slice[i].chroma_weight_l1_flag  = TRUE; /* FIXME */

        for (j = 0; j < 32; j++)
        {
            if (This->d3dSliceInfo[i].RefPicList[0][j].bPicEntry != 0xFF)
            {
                slice[i].luma_weight_l0[j]      = This->d3dSliceInfo[i].Weights[0][j][0][0];
                slice[i].luma_offset_l0[j]      = This->d3dSliceInfo[i].Weights[0][j][0][1];

                slice[i].chroma_weight_l0[j][0] = This->d3dSliceInfo[i].Weights[0][j][1][0];
                slice[i].chroma_offset_l0[j][0] = This->d3dSliceInfo[i].Weights[0][j][1][1];
                slice[i].chroma_weight_l0[j][1] = This->d3dSliceInfo[i].Weights[0][j][2][0];
                slice[i].chroma_offset_l0[j][1] = This->d3dSliceInfo[i].Weights[0][j][2][1];
            }
            else
            {
                slice[i].luma_weight_l0[j]      = 1 << This->d3dSliceInfo[i].luma_log2_weight_denom;
                slice[i].luma_offset_l0[j]      = 0;

                slice[i].chroma_weight_l0[j][0] = 1 << This->d3dSliceInfo[i].chroma_log2_weight_denom;
                slice[i].chroma_offset_l0[j][0] = 0;
                slice[i].chroma_weight_l0[j][1] = 1 << This->d3dSliceInfo[i].chroma_log2_weight_denom;
                slice[i].chroma_offset_l0[j][1] = 0;
            }

            if (This->d3dSliceInfo[i].RefPicList[1][j].bPicEntry != 0xFF)
            {
                slice[i].luma_weight_l1[j]      = This->d3dSliceInfo[i].Weights[1][j][0][0];
                slice[i].luma_offset_l1[j]      = This->d3dSliceInfo[i].Weights[1][j][0][1];

                slice[i].chroma_weight_l1[j][0] = This->d3dSliceInfo[i].Weights[1][j][1][0];
                slice[i].chroma_offset_l1[j][0] = This->d3dSliceInfo[i].Weights[1][j][1][1];
                slice[i].chroma_weight_l1[j][1] = This->d3dSliceInfo[i].Weights[1][j][2][0];
                slice[i].chroma_offset_l1[j][1] = This->d3dSliceInfo[i].Weights[1][j][2][1];
            }
            else
            {
                slice[i].luma_weight_l1[j]      = 1 << This->d3dSliceInfo[i].luma_log2_weight_denom;
                slice[i].luma_offset_l1[j]      = 0;

                slice[i].chroma_weight_l1[j][0] = 1 << This->d3dSliceInfo[i].chroma_log2_weight_denom;
                slice[i].chroma_offset_l1[j][0] = 0;
                slice[i].chroma_weight_l1[j][1] = 1 << This->d3dSliceInfo[i].chroma_log2_weight_denom;
                slice[i].chroma_offset_l1[j][1] = 0;
            }
        }
    }

    if (pvaUnmapBuffer(va_display, vaSliceInfo) != VA_STATUS_SUCCESS)
        goto err;

    status = pvaRenderPicture(va_display, This->context, &vaSliceInfo, 1);
    if (status == VA_STATUS_SUCCESS)
    {
        pvaDestroyBuffer(va_display, vaSliceInfo);
        return S_OK;
    }

    ERR("failed to process slice control buffer: %s (0x%x)\n", pvaErrorStr(status), status);

err:
    pvaDestroyBuffer(va_display, vaSliceInfo);
    return E_FAIL;
}

/* caller has to hold the vaapi_lock */
static HRESULT process_data_buffer( WineVideoDecoderH264Impl *This, const DXVA2_DecodeBufferDesc *desc )
{
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = S_OK;

    if (This->vaBitstream == VA_INVALID_ID)
        return E_FAIL;

    status = pvaRenderPicture(va_display, This->context, &This->vaBitstream, 1);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to process slice buffer: %s (0x%x)\n", pvaErrorStr(status), status);
        hr = E_FAIL;
    }

    pvaDestroyBuffer(va_display, This->vaBitstream);
    This->vaBitstream = VA_INVALID_ID;
    return hr;
}

static inline WineVideoDecoderH264Impl *impl_from_IWineVideoDecoder( IWineVideoDecoder *iface )
{
    return CONTAINING_RECORD(iface, WineVideoDecoderH264Impl, IWineVideoDecoder_iface);
}

/*****************************************************************************
 * IWineVideoDecoder interface
 */

static HRESULT WINAPI WineVideoDecoderH264_QueryInterface( IWineVideoDecoder *iface, REFIID riid, void **ppv )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    TRACE("(%p/%p)->(%s, %p)\n", iface, This, debugstr_guid(riid), ppv);

    *ppv = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IWineVideoDecoder))
        *ppv = &This->IWineVideoDecoder_iface;

    if (*ppv)
    {
        IUnknown_AddRef((IUnknown *)(*ppv));
        return S_OK;
    }

    FIXME("No interface for %s\n", debugstr_guid(riid));
    return E_NOINTERFACE;
}

static ULONG WINAPI WineVideoDecoderH264_AddRef( IWineVideoDecoder *iface )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    ULONG refCount = InterlockedIncrement(&This->refCount);

    TRACE("(%p)->() AddRef from %d\n", This, refCount - 1);

    return refCount;
}

static ULONG WINAPI WineVideoDecoderH264_Release( IWineVideoDecoder *iface )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    ULONG refCount = InterlockedDecrement(&This->refCount);

    TRACE("(%p)->() Release from %d\n", This, refCount + 1);

    if (!refCount)
    {
        VADisplay va_display = IWineVideoService_VADisplay(This->service);
        TRACE("Destroying\n");

        vaapi_lock();

        if (This->vaBitstream != VA_INVALID_ID)
            pvaDestroyBuffer(va_display, This->vaBitstream);

        pvaDestroySurfaces(va_display, This->surfaces, This->surfaceCount);
        HeapFree(GetProcessHeap(), 0, This->surfaces);

        pvaDestroyImage(va_display, This->vaImage.image_id);

        pvaDestroyContext(va_display, This->context);
        pvaDestroyConfig(va_display, This->config);

        vaapi_unlock();

        IWineVideoService_Release(This->service);
        CoTaskMemFree(This);
    }

    return refCount;
}

static HRESULT WINAPI WineVideoDecoderH264_LockBuffer( IWineVideoDecoder *iface, UINT type, void **buffer,
                                                       UINT *size )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;
    void *buf;

    TRACE("(%p, %u, %p, %p)\n", This, type, buffer, size);

    if (type == DXVA2_PictureParametersBufferType)
    {
        *buffer = &This->d3dPictureParam;
        *size = sizeof(This->d3dPictureParam);
        return S_OK;
    }
    else if(type == DXVA2_InverseQuantizationMatrixBufferType)
    {
        *buffer = &This->d3dQMatrix;
        *size = sizeof(This->d3dQMatrix);
        return S_OK;
    }
    else if (type == DXVA2_SliceControlBufferType)
    {
        *buffer = &This->d3dSliceInfo;
        *size = sizeof(This->d3dSliceInfo);
        return S_OK;
    }
    else if (type != DXVA2_BitStreamDateBufferType)
        return E_INVALIDARG;

    vaapi_lock();

    /* reuse existing vaSlice buffer if any */
    if (This->vaBitstream == VA_INVALID_ID)
    {
        status = pvaCreateBuffer(va_display, This->context, VASliceDataBufferType,
                                 This->maxSliceSize, 1, NULL, &This->vaBitstream);
        if (status != VA_STATUS_SUCCESS)
        {
            ERR("failed to create slice buffer: %s (0x%x)\n", pvaErrorStr(status), status);
            goto out;
        }
    }

    status = pvaMapBuffer(va_display, This->vaBitstream, &buf);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to map slice buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else
    {
        memset(buf, 0, This->maxSliceSize);
        *buffer = buf;
        *size   = This->maxSliceSize;
        hr = S_OK;
    }

out:
    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_UnlockBuffer( IWineVideoDecoder *iface, UINT type )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;

    TRACE("(%p, %u,)\n", This, type);

    if (type == DXVA2_PictureParametersBufferType ||
        type == DXVA2_InverseQuantizationMatrixBufferType ||
        type == DXVA2_SliceControlBufferType)
    {
        return S_OK;
    }
    else if (type != DXVA2_BitStreamDateBufferType)
        return E_INVALIDARG;

    vaapi_lock();

    if (This->vaBitstream == VA_INVALID_ID)
    {
        ERR("no slice buffer allocated\n");
        goto out;
    }

    status = pvaUnmapBuffer(va_display, This->vaBitstream);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to unmap slice buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else hr = S_OK;

out:
    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_ExecuteBuffer( IWineVideoDecoder *iface, DXVA2_DecodeBufferDesc *pictureParam,
        DXVA2_DecodeBufferDesc *qMatrix, DXVA2_DecodeBufferDesc *sliceInfo, DXVA2_DecodeBufferDesc *bitStream )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    HRESULT hr;

    TRACE("(%p, %p, %p, %p, %p)\n", This, pictureParam, qMatrix, sliceInfo, bitStream);

    if (!pictureParam || !qMatrix || !sliceInfo || !bitStream)
    {
        FIXME("not enough buffers to decode picture\n");
        return E_FAIL;
    }

    vaapi_lock();

    hr = process_picture_parameters(This, pictureParam);

    if (SUCCEEDED(hr))
        hr = process_quantization_matrix(This, qMatrix);

    if (SUCCEEDED(hr))
        hr = process_slice_control_buffer(This, sliceInfo);

    if (SUCCEEDED(hr))
        hr = process_data_buffer(This, bitStream);

    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_BeginFrame( IWineVideoDecoder *iface, UINT surfaceIndex )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;

    TRACE("(%p, %d)\n", This, surfaceIndex);

    if (surfaceIndex >= This->surfaceCount)
        return E_INVALIDARG;

    vaapi_lock();

    status = pvaBeginPicture(va_display, This->context, This->surfaces[surfaceIndex]);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to begin picture: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else
    {
        This->currentSurface = surfaceIndex;
        hr = S_OK;
    }

    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_EndFrame( IWineVideoDecoder *iface )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;

    TRACE("(%p)\n", This);

    vaapi_lock();

    status = pvaEndPicture(va_display, This->context);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("ending picture failed: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else hr = S_OK;

    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_LockImage( IWineVideoDecoder *iface, WineVideoImage *image )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;

    TRACE("(%p, %p)\n", This, image);

    vaapi_lock();

    pvaSyncSurface(va_display, This->surfaces[This->currentSurface]);

    status = pvaGetImage(va_display, This->surfaces[This->currentSurface], 0, 0,
                         This->width, This->height, This->vaImage.image_id);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to get image: %s (0x%x)\n", pvaErrorStr(status), status);
        goto out;
    }

    status = pvaMapBuffer(va_display, This->vaImage.buf, &image->buffer);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to map image buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else
    {
        image->format     = This->format;
        image->width      = This->vaImage.width;
        image->height     = This->vaImage.height;
        image->planeCount = This->vaImage.num_planes;
        image->offsets    = This->vaImage.offsets;
        image->pitches    = This->vaImage.pitches;
        hr = S_OK;
    }

out:
    vaapi_unlock();
    return hr;
}

static HRESULT WINAPI WineVideoDecoderH264_UnlockImage( IWineVideoDecoder *iface )
{
    WineVideoDecoderH264Impl *This = impl_from_IWineVideoDecoder(iface);
    VADisplay va_display = IWineVideoService_VADisplay(This->service);
    VAStatus status;
    HRESULT hr = E_FAIL;

    TRACE("(%p)\n", This);

    vaapi_lock();

    status = pvaUnmapBuffer(va_display, This->vaImage.buf);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to unmap image buffer: %s (0x%x)\n", pvaErrorStr(status), status);
    }
    else hr = S_OK;

    vaapi_unlock();
    return hr;
}

static const IWineVideoDecoderVtbl WineVideoDecoderH264_VTable =
{
    WineVideoDecoderH264_QueryInterface,
    WineVideoDecoderH264_AddRef,
    WineVideoDecoderH264_Release,
    WineVideoDecoderH264_LockBuffer,
    WineVideoDecoderH264_UnlockBuffer,
    WineVideoDecoderH264_ExecuteBuffer,
    WineVideoDecoderH264_BeginFrame,
    WineVideoDecoderH264_EndFrame,
    WineVideoDecoderH264_LockImage,
    WineVideoDecoderH264_UnlockImage
};

HRESULT vaapi_h264decoder_create( IWineVideoService *service, const DXVA2_VideoDesc *videoDesc,
                                  DXVA2_ConfigPictureDecode *config, UINT numSurfaces, IWineVideoDecoder **decoder )
{
    struct vaapi_format *format;
    struct vaapi_profile *profile;
    WineVideoDecoderH264Impl *h264decoder;
    VAConfigAttrib codecAttrib;
    VADisplay va_display;
    VAStatus status;

    if (!service || !videoDesc || !config || !decoder)
        return E_INVALIDARG;

    va_display = IWineVideoService_VADisplay(service);
    *decoder = NULL;

    /* H264 requires at least 17 frames - 16 reference frames + 1 target */
    if (numSurfaces < 17)
        WARN("decoder initialized with less than 16 + 1 frames\n");

    format  = vaapi_lookup_d3dformat(videoDesc->Format);
    profile = vaapi_lookup_guid(&DXVA2_ModeH264_E);
    if (!format || !profile)
        return E_INVALIDARG;

    if (!vaapi_is_format_supported(va_display, profile, format))
        return E_INVALIDARG;

    if (videoDesc->InputSampleFreq.Numerator * videoDesc->OutputFrameFreq.Denominator !=
        videoDesc->OutputFrameFreq.Numerator * videoDesc->InputSampleFreq.Denominator)
    {
        FIXME("Changing the framerate is not supported.\n");
        return E_INVALIDARG;
    }

    h264decoder = CoTaskMemAlloc(sizeof(*h264decoder));
    if (!h264decoder)
        return E_OUTOFMEMORY;

    memset(h264decoder, 0, sizeof(*h264decoder));

    h264decoder->IWineVideoDecoder_iface.lpVtbl = &WineVideoDecoderH264_VTable;
    h264decoder->refCount          = 1;
    h264decoder->service           = service;

    h264decoder->width             = videoDesc->SampleWidth;
    h264decoder->height            = videoDesc->SampleHeight;
    h264decoder->format            = videoDesc->Format;
    h264decoder->maxSliceSize      = estimate_maximum_slice_size(videoDesc->SampleWidth, videoDesc->SampleHeight);
    memset(&h264decoder->vaImage, 0, sizeof(h264decoder->vaImage));
    h264decoder->vaImage.image_id  = VA_INVALID_ID;

    h264decoder->surfaceCount      = numSurfaces;
    h264decoder->surfaces          = NULL;
    h264decoder->currentSurface    = 0;

    h264decoder->config            = 0;
    h264decoder->context           = 0;

    h264decoder->vaBitstream       = VA_INVALID_ID;

    vaapi_lock();

    codecAttrib.type  = VAConfigAttribRTFormat;
    codecAttrib.value = format->vaformat;

    status = pvaCreateConfig(va_display, profile->profile, profile->entryPoint,
                             &codecAttrib, 1, &h264decoder->config);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to create decoder config: %s (0x%x)\n", pvaErrorStr(status), status);
        goto err;
    }

    if (!vaapi_create_surfaces(va_display, format, h264decoder->width, h264decoder->height,
                               &h264decoder->vaImage, numSurfaces, &h264decoder->surfaces))
    {
        ERR("Failed to create image or surfaces\n");
        goto err;
    }

    status = pvaCreateContext(va_display, h264decoder->config, h264decoder->width, h264decoder->height,
                              VA_PROGRESSIVE, h264decoder->surfaces, numSurfaces, &h264decoder->context);
    if (status != VA_STATUS_SUCCESS)
    {
        ERR("failed to create context: %s (0x%x)\n", pvaErrorStr(status), status);
        goto err;
    }

    vaapi_unlock();

    IWineVideoService_AddRef(service);

    *decoder = &h264decoder->IWineVideoDecoder_iface;
    return S_OK;

err:
     if (h264decoder->surfaces)
    {
        pvaDestroySurfaces(va_display, h264decoder->surfaces, h264decoder->surfaceCount);
        HeapFree(GetProcessHeap(), 0, h264decoder->surfaces);
    }

    if (h264decoder->vaImage.image_id != VA_INVALID_ID)
        pvaDestroyImage(va_display, h264decoder->vaImage.image_id);

    if (h264decoder->config)
        pvaDestroyConfig(va_display, h264decoder->config);

    vaapi_unlock();
    CoTaskMemFree(h264decoder);
    return E_FAIL;
}

#endif /* HAVE_VAAPI */
