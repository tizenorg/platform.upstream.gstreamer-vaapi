/*
 *  gstvaapiencoder_h264.c -  H.264 encoder
 *
 *  Copyright (C) 2011 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gstvaapiencoder_h264.h"
#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <va/va.h>
#include <va/va_x11.h>
#if !HAVE_OLD_H264_ENCODER
#include <va/va_enc_h264.h>
#endif
#include <X11/Xlib.h>
#include <glib.h>

#include "gst/gstclock.h"
#include "gst/gstvalue.h"

#include "gstvaapiobject.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapidisplay_priv.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h264_encoder_debug);

#define GST_CAT_DEFAULT gst_vaapi_h264_encoder_debug

#define GST_VAAPI_ENCODER_H264_CAST(encoder)    ((GstVaapiEncoderH264 *)(encoder))

#define SHARE_CODED_BUF         0

#define DEFAULT_SURFACE_NUMBER  3
#define DEFAULT_CODEDBUF_NUM    5
#define DEFAULT_SID_INPUT       0 // suface_ids[0]

#define REF_RECON_SURFACE_NUM   2

#define ENTROPY_MODE_CAVLC      0
#define ENTROPY_MODE_CABAC      1

#define BR_CBR          0
#define BR_VBR          1
#define BR_CQP          2

#define NAL_REF_IDC_NONE        0
#define NAL_REF_IDC_LOW         1
#define NAL_REF_IDC_MEDIUM      2
#define NAL_REF_IDC_HIGH        3


typedef enum {
  NAL_UNKNOWN     = 0,
  NAL_NON_IDR     = 1,
  NAL_IDR         = 5,    /* ref_idc != 0 */
  NAL_SEI         = 6,    /* ref_idc == 0 */
  NAL_SPS         = 7,
  NAL_PPS         = 8,
  NAL_AUD         = 9,
  NAL_FILLER      = 12,
}H264_NAL_TYPE;


typedef enum {
  SLICE_TYPE_P  = 0,
  SLICE_TYPE_B  = 1,
  SLICE_TYPE_I  = 2
} H264_SLICE_TYPE;

struct _GstVaapiEncoderH264Private {
  GstVaapiEncoderH264   *public;
  guint32           format;   /*NV12, I420,*/
  gboolean          avc_flag;  /*elementary flag*/

  /* private data*/
  GQueue           *video_buffer_caches; /*not used for baseline*/

  GstVaapiSurface  *ref_surface1;  /* reference buffer*/
  GstVaapiSurface  *ref_surface2;  /* for B frames */
  GstVaapiSurface  *recon_surface; /* reconstruct buffer*/

  VABufferID        seq_param_id;
  VABufferID        pic_param_id;
  VABufferID        slice_param_id;
  VABufferID        misc_param_hdr_id;
  VABufferID        packed_seq_param_id;
  VABufferID        packed_seq_data_id;
  VABufferID        packed_pic_param_id;
  VABufferID        packed_pic_data_id;
  gboolean          is_seq_param_set;
#if HAVE_OLD_H264_ENCODER
  VAEncSliceParameterBuffer     *slice_param_buffers;
#else
  VAEncSliceParameterBufferH264 *slice_param_buffers;
#endif
  guint32           default_slice_height;
  guint32           slice_mod_mb_num;
  guint32           default_cts_offset;

  GstBuffer        *sps_data;
  GstBuffer        *pps_data;

  GQueue           *queued_buffers;  /* GstVaapiVideoBuffers with surface*/

  guint32           gop_count;
  guint32           cur_display_num;
  guint32           cur_decode_num;
  H264_SLICE_TYPE   cur_slice_type;
  guint64           last_decode_time;
  guint32           max_frame_num;
  guint32           max_pic_order_cnt;
  guint16           idr_num;
};

G_DEFINE_TYPE(GstVaapiEncoderH264, gst_vaapi_encoder_h264, GST_TYPE_VAAPI_BASE_ENCODER)

// 4096-1
#define H264_BITSTREAM_ALLOC_ALIGN_MASK 0x0FFF

#define BIT_STREAM_BUFFER(stream)    ((stream)->buffer)
#define BIT_STREAM_BIT_SIZE(stream)  ((stream)->bit_size)

struct _H264Bitstream {
  guint8   *buffer;
  guint32   bit_size;
  guint32   max_bit_capability;
};

typedef struct _H264Bitstream H264Bitstream;

static const guint8 h264_bit_mask[9] = {0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x7F, 0xFF};

/* h264 bitstream functions */
static void
h264_bitstream_init(H264Bitstream *bitstream, guint32 bit_capability);

static gboolean
h264_bitstream_write_uint(
    H264Bitstream *bitstream,
    guint32 value,
    guint32 bit_size
);

static gboolean
h264_bitstream_align(H264Bitstream *bitstream, guint32 value);

static gboolean
h264_bitstream_write_ue(H264Bitstream *bitstream, guint32 value);

static gboolean
h264_bitstream_write_se(H264Bitstream *bitstream, gint32 value);

static gboolean
h264_bitstream_write_trailing_bits(H264Bitstream *bitstream);

static gboolean
h264_bitstream_write_byte_array(
    H264Bitstream *bitstream,
    const guint8 *buf,
    guint32 byte_size
);

static void
h264_bitstream_destroy(H264Bitstream *bitstream, gboolean free_flag);

static gboolean
h264_bitstream_auto_grow(H264Bitstream *bitstream, guint32 extra_bit_size);

static gboolean
h264_bitstream_write_sps(
    H264Bitstream *bitstream,
    VAEncSequenceParameterBufferH264 *seq,
    H264_Profile profile
);
static gboolean
h264_bitstream_write_pps(
    H264Bitstream *bitstream,
    VAEncPictureParameterBufferH264 *pic
);

static gboolean
h264_bitstream_write_nal_header(
    H264Bitstream *bitstream,
    guint nal_ref_idc,
    guint nal_unit_type
);

static VAProfile
h264_get_va_profile(guint32 profile)
{
  switch (profile) {
    case H264_PROFILE_BASELINE:
      return VAProfileH264Baseline;

    case H264_PROFILE_MAIN:
      return VAProfileH264Main;

    case H264_PROFILE_HIGH:
      return VAProfileH264High;

    default:
      break;
  }
  return (-1);
}

GstVaapiEncoderH264 *
gst_vaapi_encoder_h264_new(void)
{
  return GST_VAAPI_ENCODER_H264_CAST(
             g_object_new(GST_TYPE_VAAPI_ENCODER_H264, NULL));
}

static void
gst_vaapi_encoder_h264_init_public_values(GstVaapiEncoderH264* encoder)
{
  encoder->profile = 0;
  encoder->level = 0;
  encoder->bitrate = 0;
  encoder->intra_period = 0;
  encoder->init_qp = -1;
  encoder->min_qp = -1;
  encoder->slice_num = 0;
  encoder->b_frame_num = 0;
}

void
gst_vaapi_encoder_h264_set_avc_flag(GstVaapiEncoderH264* encoder, gboolean avc)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  priv->avc_flag = avc;
}

gboolean
gst_vaapi_encoder_h264_get_avc_flag(GstVaapiEncoderH264* encoder)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  return priv->avc_flag;
}

gboolean
gst_vaapi_encoder_h264_validate_attributes(GstVaapiBaseEncoder *base)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;
  if (!ENCODER_WIDTH(encoder) ||
      !ENCODER_HEIGHT(encoder) ||
      !ENCODER_FPS(encoder)) {
    return FALSE;
  }
  if (!encoder->profile) {
    encoder->profile = H264_DEFAULT_PROFILE;
  }
  gst_vaapi_base_encoder_set_va_profile(base, h264_get_va_profile(encoder->profile));
  if (!encoder->level) {
    if (encoder->profile <= H264_PROFILE_BASELINE)
      encoder->level = H264_LEVEL_31;
    else
      encoder->level = H264_LEVEL_41;
  }
  if (!encoder->intra_period) {
    encoder->intra_period = H264_DEFAULT_INTRA_PERIOD;
  }
  if (-1 == encoder->init_qp) {
    encoder->init_qp = H264_DEFAULT_INIT_QP;
  }
  if (-1 == encoder->min_qp) {
    if (GST_VAAPI_RATECONTROL_CQP == ENCODER_RATE_CONTROL(encoder))
      encoder->min_qp = encoder->init_qp;
    else
      encoder->min_qp = H264_DEFAULT_MIN_QP;
  }

  if (encoder->min_qp > encoder->init_qp) {
    encoder->min_qp = encoder->init_qp;
  }

  /* default compress ratio 1: (4*8*1.5) */
  if (GST_VAAPI_RATECONTROL_CBR == ENCODER_RATE_CONTROL(encoder) ||
      GST_VAAPI_RATECONTROL_VBR == ENCODER_RATE_CONTROL(encoder) ||
      GST_VAAPI_RATECONTROL_VBR_CONSTRAINED == ENCODER_RATE_CONTROL(encoder))
  {
    if (!encoder->bitrate)
      encoder->bitrate = ENCODER_WIDTH(encoder) *
                         ENCODER_HEIGHT(encoder) *
                         ENCODER_FPS(encoder) / 4 / 1024;
  } else
    encoder->bitrate = 0;

  if (!encoder->slice_num) {
    encoder->slice_num = H264_DEFAULT_SLICE_NUM;
  }

  /* need  calculate slice-num and each slice-height
        suppose:  ((encoder->height+15)/16) = 13, slice_num = 8
        then: slice_1_height = 2
                 slice_2_height = 2
                 slice_3_height = 2
                 slice_4_height = 2
                 slice_5_height = 2
                 slice_6_height = 1
                 slice_7_height = 1
                 slice_8_height = 1
   */
  priv->default_slice_height = (ENCODER_HEIGHT(encoder)+15)/16/encoder->slice_num;
  if (0 == priv->default_slice_height) { /* special value */
    priv->default_slice_height = 1;
    priv->slice_mod_mb_num = 0;
    encoder->slice_num = (ENCODER_HEIGHT(encoder)+15)/16;
  } else {
    priv->slice_mod_mb_num = ((ENCODER_HEIGHT(encoder)+15)/16)%encoder->slice_num;
  }

  if (encoder->b_frame_num) {
    priv->default_cts_offset = GST_SECOND/ENCODER_FPS(encoder);
  } else {
    priv->default_cts_offset = 0;
  }
  return TRUE;
}


static gboolean
h264_encoder_release_parameters(GstVaapiEncoderH264 *encoder)
{
  VAStatus va_status = VA_STATUS_SUCCESS;
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstVaapiDisplay *display = ENCODER_DISPLAY(encoder);
  GstVaapiContext *context = ENCODER_CONTEXT(encoder);

  ENCODER_ASSERT(display);
  ENCODER_ASSERT(context);
  VAAPI_UNUSED_ARG(va_status);
  VADisplay va_dpy = gst_vaapi_display_get_display(display);

  if (VA_INVALID_ID != priv->seq_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->seq_param_id);
    priv->seq_param_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->pic_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->pic_param_id);
    priv->pic_param_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->slice_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->slice_param_id);
    priv->slice_param_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->misc_param_hdr_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->misc_param_hdr_id);
    priv->misc_param_hdr_id = VA_INVALID_ID;
  }

  if (VA_INVALID_ID != priv->packed_seq_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->packed_seq_param_id);
    priv->packed_seq_param_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->packed_seq_data_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->packed_seq_data_id);
    priv->packed_seq_data_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->packed_pic_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->packed_pic_param_id);
    priv->packed_pic_param_id = VA_INVALID_ID;
  }
  if (VA_INVALID_ID != priv->packed_pic_data_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->packed_pic_data_id);
    priv->packed_pic_data_id = VA_INVALID_ID;
  }

  if (priv->slice_param_buffers) {
    g_free(priv->slice_param_buffers);
    priv->slice_param_buffers = NULL;
  }

  if (priv->sps_data) {
    gst_buffer_unref(priv->sps_data);
    priv->sps_data = NULL;
  }
  if (priv->pps_data) {
    gst_buffer_unref(priv->pps_data);
    priv->pps_data = NULL;
  }

  return TRUE;
}

static void
h264_release_queued_buffers(GstVaapiEncoderH264Private *priv)
{
    while (!g_queue_is_empty(priv->queued_buffers)) {
    GstBuffer* tmp = g_queue_pop_head(priv->queued_buffers);
    if (tmp)
      gst_buffer_unref(tmp);
  }
}


static gboolean
gst_vaapi_encoder_h264_release_resource(
    GstVaapiBaseEncoder* base
)
{
  GstVaapiEncoderH264* encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  gboolean ret = TRUE;
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);

  /* release buffers first */
  h264_encoder_release_parameters(encoder);
  h264_release_queued_buffers(priv);
  priv->cur_display_num = 0;
  priv->cur_decode_num = 0;
  priv->cur_slice_type = SLICE_TYPE_I;
  priv->gop_count = 0;
  priv->last_decode_time = 0LL;
  priv->default_cts_offset = 0;
  priv->is_seq_param_set = FALSE;

  /*remove ref_surface1*/
  if (priv->ref_surface1) {
    if (context) {
      gst_vaapi_context_put_surface(context, priv->ref_surface1);
    } else {
      g_object_unref(priv->ref_surface1);
    }
    priv->ref_surface1 = NULL;
  }

  if (priv->ref_surface2) {
    if (context) {
      gst_vaapi_context_put_surface(context, priv->ref_surface2);
    } else {
      g_object_unref(priv->ref_surface2);
    }
    priv->ref_surface2 = NULL;
  }

  /*remove recon_surface*/
  if (priv->recon_surface) {
    if (context) {
      gst_vaapi_context_put_surface(context, priv->recon_surface);
    } else {
      g_object_unref(priv->recon_surface);
    }
    priv->recon_surface = NULL;
  }

  return ret;
}

static gboolean
gst_vaapi_encoder_h264_alloc_slices(
    GstVaapiBaseEncoder *base,
    GstVaapiContext *context
)
{
  gboolean ret = TRUE;
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;

  priv->slice_param_buffers =
#if HAVE_OLD_H264_ENCODER
  (VAEncSliceParameterBuffer*)
#else
  (VAEncSliceParameterBufferH264*)
#endif
          g_malloc0_n(encoder->slice_num,
              sizeof(priv->slice_param_buffers[0]));

  return ret;
}

static void
gst_vaapi_encoder_h264_frame_failed(
    GstVaapiBaseEncoder *base,
    GstVaapiVideoBuffer* buffer
)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;

  h264_release_queued_buffers(priv);
  priv->cur_display_num = 0;
  priv->cur_decode_num = 0;
  priv->cur_slice_type = SLICE_TYPE_I;
  priv->gop_count = 0;
  priv->last_decode_time = 0LL;
}

static EncoderStatus
gst_vaapi_encoder_h264_prepare_next_buffer(
    GstVaapiBaseEncoder* base,
    GstBuffer *display_buf,
    gboolean need_flush,
    GstBuffer **out_buf
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstBuffer  *return_buf = NULL;
  //guint64 pts = 0;

  if (NULL == display_buf && g_queue_is_empty(priv->queued_buffers)) {
    ret = ENCODER_FRAME_IN_ORDER;
    if (priv->gop_count >= encoder->intra_period || need_flush)
      priv->gop_count = 0;
    goto end;
  }

  if (display_buf) {
    ++priv->gop_count;
    gst_buffer_ref(GST_BUFFER_CAST(display_buf));
    priv->last_decode_time = GST_BUFFER_TIMESTAMP(display_buf);
  }

  /* first frame */
  if (priv->gop_count == 1) {
    ENCODER_ASSERT(display_buf);
    priv->cur_display_num = 0;
    priv->cur_decode_num = 0;
    priv->cur_slice_type = SLICE_TYPE_I;
    ++priv->idr_num;
    return_buf = display_buf;
    goto end;
  }

  if (display_buf) {
    if (encoder->b_frame_num &&
        priv->gop_count < encoder->intra_period &&
        g_queue_get_length(priv->queued_buffers) < encoder->b_frame_num
        )
    {
      g_queue_push_tail(priv->queued_buffers, display_buf);
      ret = ENCODER_FRAME_IN_ORDER;
      goto end;
    }
    priv->cur_slice_type = SLICE_TYPE_P;
    priv->cur_display_num = priv->gop_count-1;
    ++priv->cur_decode_num;
    return_buf = display_buf;
  } else {
    if (need_flush) {
      return_buf = (GstBuffer*)g_queue_pop_tail(priv->queued_buffers);
      priv->cur_slice_type = SLICE_TYPE_P;
      priv->cur_display_num = priv->gop_count - 1;
      ++priv->cur_decode_num;
    } else {
      return_buf = (GstBuffer*)g_queue_pop_head(priv->queued_buffers);
      priv->cur_slice_type = SLICE_TYPE_B;
      priv->cur_display_num =
        priv->gop_count - 2 - g_queue_get_length(priv->queued_buffers);
    }
  }

end:
  *out_buf = return_buf;

  return ret;
}

static inline void
h264_swap_surface(GstVaapiSurface **s1, GstVaapiSurface **s2)
{
  GstVaapiSurface *tmp;

  g_return_if_fail(s1 && s2);
  tmp = *s1;
  *s1 = *s2;
  *s2 = tmp;
}

static inline const char *
get_slice_type(H264_SLICE_TYPE type)
{
    switch (type) {
    case SLICE_TYPE_I:
        return "I";
    case SLICE_TYPE_P:
        return "P";
    case SLICE_TYPE_B:
        return "B";
    default:
        return "Unknown";
    }
}

#if HAVE_OLD_H264_ENCODER

static gboolean
set_sequence_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncSequenceParameterBufferH264 *seq_param
)
{
  seq_param->seq_parameter_set_id = 0;
  seq_param->level_idc = encoder->level; /* 3.0 */
  seq_param->intra_period = encoder->intra_period;
  seq_param->intra_idr_period = encoder->intra_period;
  seq_param->max_num_ref_frames = 1; /*Only I, P frames*/
  seq_param->picture_width_in_mbs = (ENCODER_WIDTH(encoder)+15)/16;
  seq_param->picture_height_in_mbs = (ENCODER_HEIGHT(encoder)+15)/16;

  seq_param->bits_per_second = encoder->bitrate * 1024;
  seq_param->frame_rate = ENCODER_FPS(encoder);
  seq_param->initial_qp = encoder->init_qp; /*qp_value; 15, 24, 26?*/
  seq_param->min_qp = encoder->min_qp;     /*1, 6, 10*/
  seq_param->basic_unit_size = 0;
  seq_param->vui_flag = FALSE;

  return TRUE;
}

static gboolean
set_picture_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncPictureParameterBufferH264 *pic_param,
    VABufferID coded_buf
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;

  pic_param->reference_picture = GST_VAAPI_OBJECT_ID(priv->ref_surface1);
  pic_param->reconstructed_picture = GST_VAAPI_OBJECT_ID(priv->recon_surface);
  pic_param->coded_buf = coded_buf;
  pic_param->picture_width = ENCODER_WIDTH(encoder);
  pic_param->picture_height = ENCODER_HEIGHT(encoder);
  pic_param->last_picture = 0; // last pic or not

  return TRUE;
}

static gboolean
set_slices_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncSliceParameterBuffer *slices,
    guint slice_num
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncSliceParameterBuffer *slice_param;

  int i = 0;
  guint32 last_row_num = 0;
  guint32 slice_mod_num = priv->slice_mod_mb_num;

  for (i = 0; i < slice_num; ++i) {
    slice_param = &slices[i];
    slice_param->start_row_number = last_row_num;               /* unit MB*/
    slice_param->slice_height = priv->default_slice_height; /* unit MB */
    if (slice_mod_num) {
      ++slice_param->slice_height;
      --slice_mod_num;
    }
    last_row_num += slice_param->slice_height;
    slice_param->slice_flags.bits.is_intra =
        (priv->cur_slice_type == SLICE_TYPE_I);
    slice_param->slice_flags.bits.disable_deblocking_filter_idc = FALSE;
    slice_param->slice_flags.bits.uses_long_term_ref = FALSE;
    slice_param->slice_flags.bits.is_long_term_ref = FALSE;
  }

  ENCODER_ASSERT(last_row_num == (ENCODER_HEIGHT(encoder)+15)/16);
  return TRUE;
}

#else  /* extended libva, new parameter structures*/

static gboolean
set_sequence_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncSequenceParameterBufferH264 *seq_param
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  guint width_in_mbs, height_in_mbs;

  width_in_mbs = (ENCODER_WIDTH(encoder)+15)/16;
  height_in_mbs = (ENCODER_HEIGHT(encoder)+15)/16;

  seq_param->seq_parameter_set_id = 0;
  seq_param->level_idc = encoder->level; /* 3.0 */
  seq_param->intra_period = encoder->intra_period;
  seq_param->ip_period = 0;           // ?
  if (encoder->bitrate> 0)
      seq_param->bits_per_second = encoder->bitrate * 1024;
  else
      seq_param->bits_per_second = 0;

  seq_param->max_num_ref_frames =
      (encoder->b_frame_num < 2 ? 3 : encoder->b_frame_num+1);  // ?, why 4
  seq_param->picture_width_in_mbs = width_in_mbs;
  seq_param->picture_height_in_mbs = height_in_mbs;

  /*sequence field values*/
  seq_param->seq_fields.value = 0;
  seq_param->seq_fields.bits.chroma_format_idc = 1;
  seq_param->seq_fields.bits.frame_mbs_only_flag = 1;
  seq_param->seq_fields.bits.mb_adaptive_frame_field_flag = FALSE;
  seq_param->seq_fields.bits.seq_scaling_matrix_present_flag = FALSE;
  /* direct_8x8_inference_flag default false */
  seq_param->seq_fields.bits.direct_8x8_inference_flag = FALSE;
  seq_param->seq_fields.bits.log2_max_frame_num_minus4 = 4; // log2(seq.intra_period)-3 : 0
  /* picture order count */
  seq_param->seq_fields.bits.pic_order_cnt_type = 0;
  seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 =
        seq_param->seq_fields.bits.log2_max_frame_num_minus4 + 2;
  seq_param->seq_fields.bits.delta_pic_order_always_zero_flag = TRUE;

  priv->max_frame_num =
      1<<(seq_param->seq_fields.bits.log2_max_frame_num_minus4 + 4);
  priv->max_pic_order_cnt =
      1 <<(seq_param->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 + 4);

  seq_param->bit_depth_luma_minus8 = 0;
  seq_param->bit_depth_chroma_minus8 = 0;

  /* not used if pic_order_cnt_type == 0 */
  seq_param->num_ref_frames_in_pic_order_cnt_cycle = 0;
  seq_param->offset_for_non_ref_pic = 0;
  seq_param->offset_for_top_to_bottom_field = 0;
  memset(seq_param->offset_for_ref_frame,
         0,
         sizeof(seq_param->offset_for_ref_frame));

  if (height_in_mbs*16 - ENCODER_HEIGHT(encoder)) {
    seq_param->frame_cropping_flag = 1;
    seq_param->frame_crop_left_offset = 0;
    seq_param->frame_crop_right_offset = 0;
    seq_param->frame_crop_top_offset = 0;
    seq_param->frame_crop_bottom_offset =
        ((height_in_mbs * 16 - ENCODER_HEIGHT(encoder))/
         (2 * (!seq_param->seq_fields.bits.frame_mbs_only_flag + 1)));
  }
#if 0
  if (h264_encoder->init_qp == -1)
      seq.rate_control_method = BR_CBR;
  else if (h264_encoder->init_qp == -2)
      seq.rate_control_method = BR_VBR;
  else {
      ENCODER_ASSERT(h264_encoder->init_qp >= 0 && h264_encoder->init_qp <= 51);
      seq.rate_control_method = BR_CQP;
  }
#endif

  /*vui not set*/
  seq_param->vui_parameters_present_flag = (encoder->bitrate> 0 ? TRUE : FALSE);
  if (seq_param->vui_parameters_present_flag) {
    seq_param->vui_fields.bits.aspect_ratio_info_present_flag = FALSE;
    seq_param->vui_fields.bits.bitstream_restriction_flag = FALSE;
    seq_param->vui_fields.bits.timing_info_present_flag = (encoder->bitrate> 0 ? TRUE : FALSE);
    if (seq_param->vui_fields.bits.timing_info_present_flag) {
      seq_param->num_units_in_tick = 100;
      seq_param->time_scale = ENCODER_FPS(encoder)*2*100;
    }
  }
  return TRUE;
}

static gboolean
ensure_packed_sps_data(
    GstVaapiEncoderH264 *encoder,
    VAEncSequenceParameterBufferH264 *seq_param
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  VADisplay va_dpy = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  guint32 length_in_bits;
  guint8 *packed_seq_buffer = NULL;
  H264Bitstream bitstream;
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  if (priv->sps_data) 
    return TRUE;

  h264_bitstream_init(&bitstream, 128*8);
  h264_bitstream_write_uint(&bitstream, 0x00000001, 32); /* start code*/
  h264_bitstream_write_nal_header(&bitstream, NAL_REF_IDC_HIGH, NAL_SPS);
  h264_bitstream_write_sps(&bitstream, seq_param, encoder->profile);
  ENCODER_ASSERT(BIT_STREAM_BIT_SIZE(&bitstream)%8 == 0);
  length_in_bits = BIT_STREAM_BIT_SIZE(&bitstream);
  packed_seq_buffer = BIT_STREAM_BUFFER(&bitstream);

  /* set codec data sps */
  priv->sps_data = gst_buffer_new_and_alloc((length_in_bits+7)/8);
  GST_BUFFER_SIZE(priv->sps_data) = (length_in_bits+7)/8-4; /* start code size == 4*/
  memcpy(GST_BUFFER_DATA(priv->sps_data),
         packed_seq_buffer+4,
         GST_BUFFER_SIZE(priv->sps_data));

  packed_header_param_buffer.type = VAEncPackedHeaderSequence;
  packed_header_param_buffer.bit_length = length_in_bits;
  packed_header_param_buffer.has_emulation_bytes = 0;
  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPackedHeaderParameterBufferType,
                             sizeof(packed_header_param_buffer), 1,
                             &packed_header_param_buffer,
                             &priv->packed_seq_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "EncPackedSeqHeaderParameterBuffer failed");
  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPackedHeaderDataBufferType,
                             (length_in_bits + 7) / 8, 1,
                             packed_seq_buffer,
                             &priv->packed_seq_data_id);
  h264_bitstream_destroy(&bitstream, TRUE);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "EncPackedSeqHeaderDataBuffer failed");
end:
    return ret;

}

static gboolean
set_picture_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncPictureParameterBufferH264 *pic_param,
    VABufferID coded_buf
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;

  pic_param->pic_parameter_set_id = 0;
  pic_param->seq_parameter_set_id = 0;
  pic_param->last_picture = 0; /* means last encoding picture */
  pic_param->frame_num = (priv->cur_slice_type == SLICE_TYPE_B ?
                       (priv->cur_decode_num + 1) : priv->cur_decode_num);
  //pic_param.coding_type = 0;
  pic_param->pic_init_qp = encoder->init_qp;
  pic_param->num_ref_idx_l0_active_minus1 = 0; /* only 1 reference */
  pic_param->num_ref_idx_l1_active_minus1 = 0; /* B frames only have 1 backward and 1 forward reference*/
  pic_param->chroma_qp_index_offset = 0;
  pic_param->second_chroma_qp_index_offset = 0;

  /* set picture fields */
  pic_param->pic_fields.value = 0;
  pic_param->pic_fields.bits.idr_pic_flag = (priv->cur_slice_type == SLICE_TYPE_I);
  pic_param->pic_fields.bits.reference_pic_flag = (priv->cur_slice_type != SLICE_TYPE_B);
  pic_param->pic_fields.bits.entropy_coding_mode_flag = ENTROPY_MODE_CABAC;
  pic_param->pic_fields.bits.weighted_pred_flag = FALSE;
  pic_param->pic_fields.bits.weighted_bipred_idc = 0;
  pic_param->pic_fields.bits.constrained_intra_pred_flag = 0;
  pic_param->pic_fields.bits.transform_8x8_mode_flag = TRUE; /* enable 8x8 */
  pic_param->pic_fields.bits.deblocking_filter_control_present_flag = TRUE; /* enable debloking */
  pic_param->pic_fields.bits.redundant_pic_cnt_present_flag = FALSE;
  /* bottom_field_pic_order_in_frame_present_flag */
  pic_param->pic_fields.bits.pic_order_present_flag = FALSE;
  pic_param->pic_fields.bits.pic_scaling_matrix_present_flag = FALSE;

  /* reference list,  */
  pic_param->CurrPic.picture_id = GST_VAAPI_OBJECT_ID(priv->recon_surface);
  pic_param->CurrPic.TopFieldOrderCnt = priv->cur_display_num * 2;   // ??? /**/
  pic_param->ReferenceFrames[0].picture_id = GST_VAAPI_OBJECT_ID(priv->ref_surface1);
  pic_param->ReferenceFrames[1].picture_id = GST_VAAPI_OBJECT_ID(priv->ref_surface2);
  pic_param->ReferenceFrames[2].picture_id = VA_INVALID_ID;
  pic_param->coded_buf = coded_buf;

  ENCODER_LOG_INFO("type:%s, frame_num:%d, display_num:%d",
    get_slice_type(priv->cur_slice_type),
    pic_param->frame_num,
    pic_param->CurrPic.TopFieldOrderCnt);
  return TRUE;
}

static gboolean
ensure_packed_pps_data(
    GstVaapiEncoderH264 *encoder,
    VAEncPictureParameterBufferH264 *pic_param
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncPackedHeaderParameterBuffer packed_header_param_buffer = { 0 };
  H264Bitstream bitstream;
  VADisplay va_dpy = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  guint32 length_in_bits;
  guint8 *packed_pic_buffer = NULL;
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  if (VA_INVALID_ID != priv->packed_pic_data_id)
      return TRUE;

  h264_bitstream_init(&bitstream, 128*8);
  h264_bitstream_write_uint(&bitstream, 0x00000001, 32); /* start code*/
  h264_bitstream_write_nal_header(&bitstream, NAL_REF_IDC_HIGH, NAL_PPS);
  h264_bitstream_write_pps(&bitstream, pic_param);
  ENCODER_ASSERT(BIT_STREAM_BIT_SIZE(&bitstream)%8 == 0);
  length_in_bits = BIT_STREAM_BIT_SIZE(&bitstream);
  packed_pic_buffer = BIT_STREAM_BUFFER(&bitstream);

  /*set codec data pps*/
  priv->pps_data = gst_buffer_new_and_alloc((length_in_bits+7)/8);
  GST_BUFFER_SIZE(priv->pps_data) = (length_in_bits+7)/8-4;
  memcpy(GST_BUFFER_DATA(priv->pps_data),
         packed_pic_buffer+4,
         GST_BUFFER_SIZE(priv->pps_data));

  packed_header_param_buffer.type = VAEncPackedHeaderPicture;
  packed_header_param_buffer.bit_length = length_in_bits;
  packed_header_param_buffer.has_emulation_bytes = 0;

  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPackedHeaderParameterBufferType,
                             sizeof(packed_header_param_buffer), 1,
                             &packed_header_param_buffer,
                             &priv->packed_pic_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "EncPackedPicHeaderParameterBuffer failed");

  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPackedHeaderDataBufferType,
                             (length_in_bits + 7) / 8, 1,
                             packed_pic_buffer,
                             &priv->packed_pic_data_id);
  h264_bitstream_destroy(&bitstream, TRUE);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "EncPackedPicHeaderDataBuffer failed");
end:
  return ret;
}

static gboolean
set_slices_parameters(
    GstVaapiEncoderH264 *encoder,
    VAEncSliceParameterBufferH264 *slices,
    guint slice_num
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncSliceParameterBufferH264 *slice_param;

  guint width_in_mbs = (ENCODER_WIDTH(encoder)+15)/16;
  int i = 0;
  guint32 last_row_num = 0;
  guint32 slice_mod_num = priv->slice_mod_mb_num;

  for (i = 0; i < slice_num; ++i) {
    int i_pic = 0;
    slice_param = slices + i;

    slice_param->macroblock_address = last_row_num*width_in_mbs;
    slice_param->num_macroblocks = width_in_mbs*priv->default_slice_height;
    last_row_num += priv->default_slice_height;
    if (slice_mod_num) {
      slice_param->num_macroblocks += width_in_mbs;
      ++last_row_num;
      --slice_mod_num;
    }
    slice_param->macroblock_info = VA_INVALID_ID;
    slice_param->slice_type = priv->cur_slice_type;
    slice_param->pic_parameter_set_id = 0;
    slice_param->idr_pic_id = priv->idr_num;
    slice_param->pic_order_cnt_lsb =
        (priv->cur_display_num*2) % priv->max_pic_order_cnt;

    /* not used if pic_order_cnt_type = 0 */
    slice_param->delta_pic_order_cnt_bottom = 0;
    memset(slice_param->delta_pic_order_cnt,
           0,
           sizeof(slice_param->delta_pic_order_cnt));

    /*only works for B frames*/
    slice_param->direct_spatial_mv_pred_flag = FALSE;
    /* default equal to picture parameters */
    slice_param->num_ref_idx_active_override_flag = FALSE;
    slice_param->num_ref_idx_l0_active_minus1 = 0;
    slice_param->num_ref_idx_l1_active_minus1 = 0;

    slice_param->RefPicList0[0].picture_id =
        GST_VAAPI_OBJECT_ID(priv->ref_surface1);
    for (i_pic = 1;
         i_pic < sizeof(slice_param->RefPicList0)/sizeof(slice_param->RefPicList0[0]);
         i_pic++) {
      slice_param->RefPicList0[i_pic].picture_id = VA_INVALID_ID;
    }

    if (SLICE_TYPE_B == priv->cur_slice_type) {
      slice_param->RefPicList1[0].picture_id =
          GST_VAAPI_OBJECT_ID(priv->ref_surface2);
      i_pic = 1;
    } else
      i_pic = 0;
    for (;
         i_pic < sizeof(slice_param->RefPicList1)/sizeof(slice_param->RefPicList1[0]);
         i_pic++)
      slice_param->RefPicList1[i_pic].picture_id = VA_INVALID_ID;

    /* not used if  pic_param.pic_fields.bits.weighted_pred_flag == FALSE */
    slice_param->luma_log2_weight_denom = 0;
    slice_param->chroma_log2_weight_denom = 0;
    slice_param->luma_weight_l0_flag = FALSE;
    memset(slice_param->luma_weight_l0, 0, sizeof(slice_param->luma_weight_l0));
    memset(slice_param->luma_offset_l0, 0, sizeof(slice_param->luma_offset_l0));
    slice_param->chroma_weight_l0_flag = FALSE;
    memset(slice_param->chroma_weight_l0, 0, sizeof(slice_param->chroma_weight_l0));
    memset(slice_param->chroma_offset_l0, 0, sizeof(slice_param->chroma_offset_l0));
    slice_param->luma_weight_l1_flag = FALSE;
    memset(slice_param->luma_weight_l1, 0, sizeof(slice_param->luma_weight_l1));
    memset(slice_param->luma_offset_l1, 0, sizeof(slice_param->luma_offset_l1));
    slice_param->chroma_weight_l1_flag = FALSE;
    memset(slice_param->chroma_weight_l1, 0, sizeof(slice_param->chroma_weight_l1));
    memset(slice_param->chroma_offset_l1, 0, sizeof(slice_param->chroma_offset_l1));

    slice_param->cabac_init_idc = 0;
    slice_param->slice_qp_delta = encoder->init_qp - encoder->min_qp;
    if (slice_param->slice_qp_delta > 4)
      slice_param->slice_qp_delta = 4;
    slice_param->disable_deblocking_filter_idc = 0;
    slice_param->slice_alpha_c0_offset_div2 = 2;
    slice_param->slice_beta_offset_div2 = 2;

  }
  ENCODER_ASSERT(last_row_num == (ENCODER_HEIGHT(encoder)+15)/16);
  return TRUE;
}

static gboolean
h264_fill_hdr_buffer(GstVaapiEncoderH264 *encoder)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncMiscParameterBuffer *misc_param;
  VAEncMiscParameterHRD *misc_hrd_param;
  VADisplay va_dpy       = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  if (VA_INVALID_ID != priv->misc_param_hdr_id) {
    vaDestroyBuffer(va_dpy, priv->misc_param_hdr_id);
    priv->misc_param_hdr_id = VA_INVALID_ID;
  }

  /*  hrd parameter */
  va_status = vaCreateBuffer(
      va_dpy,
      context_id,
      VAEncMiscParameterBufferType,
      sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl),
      1,
      NULL,
      &priv->misc_param_hdr_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "vaCreateEncMiscParameterBuffer failed");

  va_status = vaMapBuffer(va_dpy,
                          priv->misc_param_hdr_id,
                          (void **)&misc_param);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "H264 HDR buffer map failed");
  misc_param->type = VAEncMiscParameterTypeHRD;
  misc_hrd_param = (VAEncMiscParameterHRD *)misc_param->data;

  if (encoder->bitrate > 0) {
      misc_hrd_param->initial_buffer_fullness = encoder->bitrate * 1024 * 4;
      misc_hrd_param->buffer_size = encoder->bitrate * 1024 * 8;
  } else {
      misc_hrd_param->initial_buffer_fullness = 0;
      misc_hrd_param->buffer_size = 0;
  }

  vaUnmapBuffer(va_dpy, priv->misc_param_hdr_id);

end:
  return ret;
}
#endif

static gboolean
h264_fill_sequence_buffer(GstVaapiEncoderH264 *encoder)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncSequenceParameterBufferH264 seq_param = { 0 };
  VADisplay va_dpy       = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  /* only once */
  if (VA_INVALID_ID != priv->seq_param_id)
    return TRUE;

  set_sequence_parameters(encoder, &seq_param);
  va_status = vaCreateBuffer(va_dpy, context_id,
                             VAEncSequenceParameterBufferType,
                             sizeof(seq_param), 1,
                             &seq_param, &priv->seq_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "alloc seq-buffer failed.");

#if !HAVE_OLD_H264_ENCODER
  ensure_packed_sps_data(encoder, &seq_param);
#endif

end:
  return ret;
}

static gboolean
h264_fill_picture_buffer(
    GstVaapiEncoderH264 *encoder,
    VABufferID coded_buf
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VAEncPictureParameterBufferH264 pic_param;
  VADisplay va_dpy = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  VAAPI_UNUSED_ARG(va_status);
  memset(&pic_param, 0, sizeof(pic_param));
  set_picture_parameters(encoder, &pic_param, coded_buf);

  if (VA_INVALID_ID != priv->pic_param_id) { /* share the same pic_param_id*/
    vaDestroyBuffer(va_dpy, priv->pic_param_id);
    priv->pic_param_id = VA_INVALID_ID;
  }
  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPictureParameterBufferType,
                             sizeof(pic_param), 1,
                             &pic_param,
                             &priv->pic_param_id);

  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "creating pic-param buffer failed.");
#if !HAVE_OLD_H264_ENCODER
  ensure_packed_pps_data(encoder, &pic_param);
#endif

end:
  return ret;
}

static gboolean
h264_fill_slice_buffers(
    GstVaapiEncoderH264 *encoder
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  VADisplay va_dpy       = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  gboolean ret = TRUE;
  VAStatus va_status = VA_STATUS_SUCCESS;

  memset(priv->slice_param_buffers,
         0,
         encoder->slice_num * sizeof(priv->slice_param_buffers[0]));
  set_slices_parameters(encoder,
                        priv->slice_param_buffers,
                        encoder->slice_num);

  if (VA_INVALID_ID != priv->slice_param_id) {
    vaDestroyBuffer(va_dpy, priv->slice_param_id);
    priv->slice_param_id = VA_INVALID_ID;
  }
  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncSliceParameterBufferType,
                             sizeof(priv->slice_param_buffers[0]),
                             encoder->slice_num,
                             priv->slice_param_buffers,
                             &priv->slice_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "creating slice-parameters buffer failed.");

end:
  return ret;
}

static EncoderStatus
gst_vaapi_encoder_h264_rendering(
    GstVaapiBaseEncoder *base,
    GstVaapiSurface *surface,
    guint frame_index,
    VABufferID coded_buf,
    gboolean *is_key
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);
  VABufferID va_buffers[64];
  guint32    va_buffers_count = 0;
  gboolean is_params_ok = TRUE;

  ENCODER_ASSERT(context);

  *is_key = (priv->cur_slice_type == SLICE_TYPE_I);

  if (!priv->ref_surface1) {
    priv->ref_surface1 = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->ref_surface1,
                         ENCODER_SURFACE_ERR,
                         "reference surface, h264_pop_free_surface failed.");
  }
  if (!priv->ref_surface2) {
    priv->ref_surface2 = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->ref_surface2,
                         ENCODER_SURFACE_ERR,
                         "reference surface, h264_pop_free_surface failed.");
  }
  if (!priv->recon_surface) {
    priv->recon_surface = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->recon_surface,
                         ENCODER_SURFACE_ERR,
                         "reconstructed surface, h264_pop_free_surface failed.");
  }

  if (SLICE_TYPE_P == priv->cur_slice_type) {
    h264_swap_surface(&priv->ref_surface1, &priv->ref_surface2);
  }

  /* fill sequence parameters, need set every time */
  is_params_ok = h264_fill_sequence_buffer(encoder);
  ENCODER_CHECK_STATUS(is_params_ok,
                       ENCODER_PARAMETER_ERR,
                       "h264_recreate_seq_param failed");
  /* set pic_parameters*/
  is_params_ok = h264_fill_picture_buffer(encoder, coded_buf);
  ENCODER_CHECK_STATUS(is_params_ok,
                       ENCODER_PARAMETER_ERR,
                       "h264_recreate_pic_param failed");

#if !HAVE_OLD_H264_ENCODER
  /* set misc_hdr_parameters*/
  is_params_ok = h264_fill_hdr_buffer(encoder);
  ENCODER_CHECK_STATUS(is_params_ok,
                       ENCODER_PARAMETER_ERR,
                       "h264_fill_hdr__param failed");
#endif

  /* set slice parameters, support multiple slices */
  is_params_ok = h264_fill_slice_buffers(encoder);
  ENCODER_CHECK_STATUS(is_params_ok,
                       ENCODER_PARAMETER_ERR,
                       "h264_recreate_slice_param failed");

  /*render all buffers*/
  if (VA_INVALID_ID != priv->seq_param_id) {
  #if HAVE_OLD_H264_ENCODER
    if (!priv->is_seq_param_set) {
      priv->is_seq_param_set = TRUE;
      va_buffers[va_buffers_count++] = priv->seq_param_id;
    }
  #else
    va_buffers[va_buffers_count++] = priv->seq_param_id;
  #endif
  }
  if (VA_INVALID_ID != priv->pic_param_id) {
    va_buffers[va_buffers_count++] = priv->pic_param_id;
  }
  if (VA_INVALID_ID != priv->misc_param_hdr_id) {
    va_buffers[va_buffers_count++] = priv->misc_param_hdr_id;
  }
  if (VA_INVALID_ID != priv->slice_param_id) {
    va_buffers[va_buffers_count++] = priv->slice_param_id;
  }
  if (SLICE_TYPE_I == priv->cur_slice_type) {
    if (VA_INVALID_ID != priv->packed_seq_param_id) {
      va_buffers[va_buffers_count++] = priv->packed_seq_param_id;
    }
    if (VA_INVALID_ID != priv->packed_seq_data_id) {
      va_buffers[va_buffers_count++] = priv->packed_seq_data_id;
    }
    if (VA_INVALID_ID != priv->packed_pic_param_id) {
      va_buffers[va_buffers_count++] = priv->packed_pic_param_id;
    }
    if (VA_INVALID_ID != priv->packed_pic_data_id) {
      va_buffers[va_buffers_count++] = priv->packed_pic_data_id;
    }
  }
  ret = gst_vaapi_encoder_render_picture(GST_VAAPI_ENCODER_CAST(encoder),
                                         surface,
                                         va_buffers,
                                         va_buffers_count);
  ENCODER_CHECK_STATUS(ret == ENCODER_NO_ERROR,
                       ENCODER_PICTURE_ERR,
                       "vaRenderH264Picture failed.");

  /*after finished,  swap  recon and surface2*/
  if (SLICE_TYPE_P == priv->cur_slice_type ||
      SLICE_TYPE_I == priv->cur_slice_type) {
    h264_swap_surface(&priv->recon_surface, &priv->ref_surface2);
  }

  end:
  return ret;
}

static const guint8 *
h264_next_nal(const guint8 *buffer, guint32 len, guint32 *nal_size)
{
    const guint8 *cur = buffer;
    const guint8 *end = buffer + len;
    const guint8 *nal_start = NULL;
    guint32 flag = 0xFFFFFFFF;
    guint32 nal_start_len = 0;

    ENCODER_ASSERT(len >= 0 && buffer && nal_size);
    if (len < 3) {
        *nal_size = len;
        nal_start = (len ? buffer : NULL);
        return nal_start;
    }

    /*locate head postion*/
    if (!buffer[0] && !buffer[1]) {
        if (buffer[2] == 1) { // 0x000001
            nal_start_len = 3;
        } else if (!buffer[2] && len >=4 && buffer[3] == 1) { //0x00000001
            nal_start_len = 4;
        }
    }
    nal_start = buffer + nal_start_len;
    cur = nal_start;

    /*find next nal start position*/
    while (cur < end) {
        flag = ((flag<<8) | ((*cur++)&0xFF));
        if ((flag&0x00FFFFFF) == 0x00000001) {
            if (flag == 0x00000001)
                *nal_size = cur - 4 - nal_start;
            else
                *nal_size = cur - 3 - nal_start;
            break;
        }
    }
    if (cur >= end) {
      *nal_size = end - nal_start;
      if (nal_start >= end) {
        nal_start = NULL;
      }
    }
    return nal_start;
}

static GstBuffer *
gst_vaapi_encoder_h264_wrap_buffer(
    GstVaapiBaseEncoder *base,
    GstBuffer *buf
)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstBuffer *ret_buffer;
  guint32   nal_size;
  const guint8   *nal_start;
  guint8  *frame_end;
  H264Bitstream bitstream;

  if (!priv->avc_flag) {
    gst_buffer_ref(buf);
    return buf;
  }

  ret_buffer = gst_buffer_new();
  ENCODER_ASSERT(ret_buffer);
  h264_bitstream_init(&bitstream, (GST_BUFFER_SIZE(buf)+32)*8);
  h264_bitstream_align(&bitstream, 0);
  ENCODER_ASSERT(bitstream.bit_size == 0);


  frame_end = GST_BUFFER_DATA(buf) + GST_BUFFER_SIZE(buf);
  nal_start = GST_BUFFER_DATA(buf);
  nal_size = 0;
  while(NULL !=
        (nal_start = h264_next_nal(nal_start, frame_end-nal_start, &nal_size))
       ) {
    ENCODER_ASSERT(nal_size);
    if (!nal_size) {
      nal_start += nal_size;
      continue;
    }
    h264_bitstream_write_uint(&bitstream, nal_size, 32);
    h264_bitstream_write_byte_array(&bitstream, nal_start, nal_size);
    nal_start += nal_size;
  }
  h264_bitstream_align(&bitstream, 0);

  GST_BUFFER_MALLOCDATA(ret_buffer) =
        GST_BUFFER_DATA(ret_buffer) = BIT_STREAM_BUFFER(&bitstream);
  GST_BUFFER_SIZE(ret_buffer) = BIT_STREAM_BIT_SIZE(&bitstream)/8;
  h264_bitstream_destroy(&bitstream, FALSE);

  return ret_buffer;
}

static EncoderStatus
read_sps_pps(GstVaapiEncoderH264Private *priv, const guint8 *buf, guint32 size)
{
  const guint8 *end = buf + size;
  const guint8 *nal_start = buf;
  guint32 nal_size = 0;
  guint8 nal_type;
  GstBuffer *sps = NULL, *pps = NULL;

  while((!sps || !pps) &&
        (nal_start = h264_next_nal(nal_start, end-nal_start, &nal_size)) != NULL
       ) {
    if (!nal_size) {
      nal_start += nal_size;
      continue;
    }

    nal_type = (*nal_start)&0x1F;
    switch (nal_type) {
      case NAL_SPS: {
        sps = gst_buffer_new_and_alloc(nal_size);
        memcpy(GST_BUFFER_DATA(sps), nal_start, nal_size);
        gst_buffer_replace(&priv->sps_data, sps);
        gst_buffer_unref(sps); /*don't set to NULL*/
        break;
      }

      case NAL_PPS: {
        pps = gst_buffer_new_and_alloc(nal_size);
        memcpy(GST_BUFFER_DATA(pps), nal_start, nal_size);
        gst_buffer_replace(&priv->pps_data, pps);
        gst_buffer_unref(pps);
        break;
      }

      default:
        break;
    }
    nal_start += nal_size;
  }

  if (!sps || !pps) {
    return ENCODER_DATA_NOT_READY;
  }

  return ENCODER_NO_ERROR;
}

static void
gst_vaapi_encoder_h264_notify_frame(
    GstVaapiBaseEncoder *base,
    guint8 *buf,
    guint32 size
)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;
  if (!priv->sps_data || !priv->pps_data) {
    read_sps_pps(priv, buf, size);
  }
  if (priv->sps_data && priv->pps_data) {
    gst_vaapi_base_encoder_set_frame_notify(base, FALSE);
  }
}

static gboolean
read_sps_attributes(
    const guint8 *sps_data,
    guint32 sps_size,
    guint32 *profile_idc,
    guint32 *profile_comp,
    guint32 *level_idc
)
{
  ENCODER_ASSERT(profile_idc && profile_comp && level_idc);
  ENCODER_ASSERT(sps_size >= 4);
  if (sps_size < 4) {
    return FALSE;
  }
  /*skip sps_data[0], nal_type*/
  *profile_idc = sps_data[1];
  *profile_comp = sps_data[2];
  *level_idc = sps_data[3];
  return TRUE;
}

static EncoderStatus
gst_vaapi_encoder_h264_flush(
    GstVaapiEncoder* base
)
{
  GstVaapiEncoderH264* encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderH264Private *priv = encoder->priv;

  //priv->frame_count = 0;
  priv->cur_display_num = 0;
  priv->cur_decode_num = 0;
  priv->cur_slice_type = SLICE_TYPE_I;
  priv->gop_count = g_queue_get_length(priv->queued_buffers);
  //gst_vaapi_base_encoder_set_frame_notify((GST_VAAPI_BASE_ENCODER)encoder, TRUE);

  //end:
  return ret;
}

static EncoderStatus
gst_vaapi_encoder_h264_get_avcC_codec_data(
    GstVaapiEncoderH264 *encoder,
    GstBuffer **buffer
)
{
  GstVaapiEncoderH264Private *priv = encoder->priv;
  GstBuffer *avc_codec;
  const guint32 configuration_version = 0x01;
  const guint32 length_size_minus_one = 0x03;
  guint32 profile, profile_comp, level_idc;

  ENCODER_ASSERT(buffer);
  if (!priv->sps_data || !priv->pps_data) {
    return ENCODER_DATA_NOT_READY;
  }

  if (FALSE == read_sps_attributes(GST_BUFFER_DATA(priv->sps_data),
                                   GST_BUFFER_SIZE(priv->sps_data),
                                   &profile, &profile_comp, &level_idc))
  {
    ENCODER_ASSERT(0);
    return ENCODER_DATA_ERR;
  }

  H264Bitstream bitstream;
  h264_bitstream_init(&bitstream,
                      (GST_BUFFER_SIZE(priv->sps_data) +
                       GST_BUFFER_SIZE(priv->pps_data) + 32)*8);

  /*codec_data*/
  h264_bitstream_write_uint(&bitstream, configuration_version, 8);
  h264_bitstream_write_uint(&bitstream, profile, 8);
  h264_bitstream_write_uint(&bitstream, profile_comp, 8);
  h264_bitstream_write_uint(&bitstream, level_idc, 8);
  h264_bitstream_write_uint(&bitstream, h264_bit_mask[6], 6); /*111111*/
  h264_bitstream_write_uint(&bitstream, length_size_minus_one, 2);
  h264_bitstream_write_uint(&bitstream, h264_bit_mask[3], 3); /*111*/

  /*write sps*/
  h264_bitstream_write_uint(&bitstream, 1, 5);   /* sps count = 1*/
  ENCODER_ASSERT( BIT_STREAM_BIT_SIZE(&bitstream)%8 == 0);
  h264_bitstream_write_uint(&bitstream, GST_BUFFER_SIZE(priv->sps_data), 16);
  h264_bitstream_write_byte_array(&bitstream,
                                  GST_BUFFER_DATA(priv->sps_data),
                                  GST_BUFFER_SIZE(priv->sps_data));

  /*write pps*/
  h264_bitstream_write_uint(&bitstream, 1, 8); /*pps count = 1*/
  h264_bitstream_write_uint(&bitstream, GST_BUFFER_SIZE(priv->pps_data), 16);
  h264_bitstream_write_byte_array(&bitstream,
                                  GST_BUFFER_DATA(priv->pps_data),
                                  GST_BUFFER_SIZE(priv->pps_data));

  avc_codec = gst_buffer_new();
  GST_BUFFER_MALLOCDATA(avc_codec) =
         GST_BUFFER_DATA(avc_codec) =
         BIT_STREAM_BUFFER(&bitstream);
  GST_BUFFER_SIZE(avc_codec) = BIT_STREAM_BIT_SIZE(&bitstream)/8;
  h264_bitstream_destroy(&bitstream, FALSE);
  *buffer = avc_codec;

  return ENCODER_NO_ERROR;
}

static EncoderStatus
gst_vaapi_encoder_h264_get_codec_data(
    GstVaapiEncoder* base,
    GstBuffer **buffer)
{
  GstVaapiEncoderH264 *encoder = GST_VAAPI_ENCODER_H264_CAST(base);
  GstVaapiEncoderH264Private *priv = encoder->priv;

  if (priv->avc_flag)
    return gst_vaapi_encoder_h264_get_avcC_codec_data(encoder, buffer);
  return ENCODER_NO_DATA;
}

static void
gst_vaapi_encoder_h264_init(GstVaapiEncoderH264 *encoder)
{
  GstVaapiEncoderH264Private *priv = GST_VAAPI_ENCODER_H264_GET_PRIVATE(encoder);
  ENCODER_ASSERT(priv);
  priv->public = encoder;
  encoder->priv = priv;

  /* init public attributes */
  gst_vaapi_encoder_h264_init_public_values(encoder);
  gst_vaapi_base_encoder_set_frame_notify(GST_VAAPI_BASE_ENCODER(encoder), TRUE);

  /* init private values*/
  priv->format = GST_MAKE_FOURCC('N','V','1','2');
  priv->avc_flag = FALSE;

  priv->ref_surface1 = NULL;
  priv->ref_surface2 = NULL;
  priv->recon_surface = NULL;

  priv->seq_param_id = VA_INVALID_ID;
  priv->pic_param_id = VA_INVALID_ID;
  priv->slice_param_id = VA_INVALID_ID;
  priv->misc_param_hdr_id = VA_INVALID_ID;
  priv->packed_seq_param_id = VA_INVALID_ID;
  priv->packed_seq_data_id = VA_INVALID_ID;
  priv->packed_pic_param_id = VA_INVALID_ID;
  priv->packed_pic_data_id = VA_INVALID_ID;
  priv->is_seq_param_set = FALSE;
  priv->slice_param_buffers = NULL;
  priv->default_slice_height = 0;
  priv->slice_mod_mb_num = 0;

  priv->sps_data = NULL;
  priv->pps_data = NULL;

  priv->queued_buffers = g_queue_new();
  priv->gop_count = 0;
  priv->cur_display_num = 0;
  priv->cur_decode_num = 0;
  priv->cur_slice_type = SLICE_TYPE_I;
  priv->last_decode_time = 0LL;
  priv->default_cts_offset = 0;

  priv->max_frame_num = 0;
  priv->max_pic_order_cnt = 0;
  priv->idr_num = 0;
}

static void
gst_vaapi_encoder_h264_finalize(GObject *object)
{
  /*free private buffers*/
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER(object);
  GstVaapiEncoderH264Private *priv = GST_VAAPI_ENCODER_H264_GET_PRIVATE(object);

  if (gst_vaapi_encoder_get_state(encoder) != VAAPI_ENC_NULL) {
    gst_vaapi_encoder_uninitialize(encoder);
  }

  if (priv->sps_data) {
    gst_buffer_unref(priv->sps_data);
    priv->sps_data = NULL;
  }
  if (priv->pps_data) {
    gst_buffer_unref(priv->pps_data);
    priv->pps_data = NULL;
  }
  if (priv->slice_param_buffers) {
    g_free(priv->slice_param_buffers);
    priv->slice_param_buffers = NULL;
  }

  if (priv->queued_buffers) {
    ENCODER_ASSERT(g_queue_is_empty(priv->queued_buffers));
    g_queue_free(priv->queued_buffers);
    priv->queued_buffers = NULL;
  }

  G_OBJECT_CLASS(gst_vaapi_encoder_h264_parent_class)->finalize(object);
}

static void
gst_vaapi_encoder_h264_class_init(GstVaapiEncoderH264Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstVaapiEncoderClass * const encoder_class = GST_VAAPI_ENCODER_CLASS(klass);
  GstVaapiBaseEncoderClass * const base_class = GST_VAAPI_BASE_ENCODER_CLASS(klass);

  g_type_class_add_private(klass, sizeof(GstVaapiEncoderH264Private));

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h264_encoder_debug,
                           "gst_va_h264_encoder",
                           0,
                           "gst_va_h264_encoder element");

  object_class->finalize = gst_vaapi_encoder_h264_finalize;

  base_class->validate_attributes = gst_vaapi_encoder_h264_validate_attributes;
  base_class->pre_alloc_resource  = gst_vaapi_encoder_h264_alloc_slices;
  base_class->release_resource    = gst_vaapi_encoder_h264_release_resource;
  base_class->prepare_next_input_buffer =
      gst_vaapi_encoder_h264_prepare_next_buffer;
  base_class->render_frame = gst_vaapi_encoder_h264_rendering;
  base_class->notify_buffer = gst_vaapi_encoder_h264_notify_frame;
  base_class->wrap_buffer = gst_vaapi_encoder_h264_wrap_buffer;
  base_class->encode_frame_failed = gst_vaapi_encoder_h264_frame_failed;

  encoder_class->flush = gst_vaapi_encoder_h264_flush;
  encoder_class->get_codec_data = gst_vaapi_encoder_h264_get_codec_data;
}

static void
h264_bitstream_init(H264Bitstream *bitstream, guint32 bit_capability)
{
  bitstream->bit_size = 0;
  bitstream->buffer = NULL;
  bitstream->max_bit_capability = 0;
  if (bit_capability) {
    h264_bitstream_auto_grow(bitstream, bit_capability);
  }
}

static gboolean
h264_bitstream_write_uint(
    H264Bitstream *bitstream,
    guint32 value,
    guint32 bit_size
)
{
  gboolean ret = TRUE;
  guint32 byte_pos, bit_offset;
  guint8  *cur_byte;
  guint32 fill_bits;

  if(!bit_size) {
    return TRUE;
  }

  VAAPI_UNUSED_ARG(ret);
  ENCODER_CHECK_STATUS(h264_bitstream_auto_grow(bitstream, bit_size),
                       FALSE,
                       "h264_bitstream_auto_grow failed.");
  byte_pos = (bitstream->bit_size>>3);
  bit_offset = (bitstream->bit_size&0x07);
  cur_byte = bitstream->buffer + byte_pos;
  ENCODER_ASSERT(bit_offset < 8 &&
                 bitstream->bit_size <= bitstream->max_bit_capability);

  while (bit_size) {
    fill_bits = ((8-bit_offset) < bit_size ? (8-bit_offset) : bit_size);
    bit_size -= fill_bits;
    bitstream->bit_size += fill_bits;

    *cur_byte |=
        ((value>>bit_size) & h264_bit_mask[fill_bits])<<(8-bit_offset-fill_bits);
    ++cur_byte;
    bit_offset = 0;
  }
  ENCODER_ASSERT(cur_byte <=
                 (bitstream->buffer + bitstream->max_bit_capability/8));

  end:
  return ret;
}

static gboolean
h264_bitstream_align(H264Bitstream *bitstream, guint32 value)
{
  guint32 bit_offset, bit_left;

  bit_offset = (bitstream->bit_size&0x07);
  if (!bit_offset) {
    return TRUE;
  }
  bit_left = 8 - bit_offset;
  if (value) value = h264_bit_mask[bit_left];
  return h264_bitstream_write_uint(bitstream, value, bit_left);
}


static gboolean
h264_bitstream_write_byte_array(
    H264Bitstream *bitstream,
    const guint8 *buf,
    guint32 byte_size
)
{
  gboolean ret = TRUE;
  if (!byte_size) {
    return 0;
  }

  VAAPI_UNUSED_ARG(ret);
  ENCODER_CHECK_STATUS(h264_bitstream_auto_grow(bitstream, byte_size<<3),
                       FALSE,
                       "h264_bitstream_auto_grow failed.");
  if (0 == (bitstream->bit_size&0x07)) {
    memcpy(&bitstream->buffer[bitstream->bit_size>>3], buf, byte_size);
    bitstream->bit_size += (byte_size<<3);
  } else {
    ENCODER_ASSERT(0);
    while(byte_size) {
      h264_bitstream_write_uint(bitstream, *buf, 8);
      --byte_size;
      ++buf;
    }
  }

end:
  return ret;
}

static gboolean
h264_bitstream_write_ue(H264Bitstream *bitstream, guint32 value)
{
  gboolean ret = TRUE;
  guint32  size_in_bits = 0;
  guint32  tmp_value = ++value;
  while (tmp_value) {
    ++size_in_bits;
    tmp_value >>= 1;
  }
  ENCODER_CHECK_STATUS(h264_bitstream_write_uint(bitstream, 0, size_in_bits-1),
                       FALSE,
                       "h264_bitstream_write_ue failed.");
  ENCODER_CHECK_STATUS(h264_bitstream_write_uint(bitstream, value, size_in_bits),
                       FALSE,
                       "h264_bitstream_write_ue failed.");

end:
  return ret;
}

static gboolean
h264_bitstream_write_se(H264Bitstream *bitstream, gint32 value)
{
  gboolean ret = TRUE;
  guint32 new_val;

  if (value <= 0) {
    new_val = -(value<<1);
  } else {
    new_val = (value<<1) - 1;
  }

  ENCODER_CHECK_STATUS(h264_bitstream_write_ue(bitstream, new_val),
                       FALSE,
                       "h264_bitstream_write_se failed.");

end:
  return ret;
}

static gboolean
h264_bitstream_write_trailing_bits(H264Bitstream *bitstream)
{
    h264_bitstream_write_uint(bitstream, 1, 1);
    h264_bitstream_align(bitstream, 0);
    return TRUE;
}

static void
h264_bitstream_destroy(H264Bitstream *bitstream, gboolean free_flag)
{
  if (bitstream->buffer && free_flag) {
    free (bitstream->buffer);
  }
  bitstream->buffer = NULL;
  bitstream->bit_size = 0;
  bitstream->max_bit_capability = 0;
}

static gboolean
h264_bitstream_auto_grow(H264Bitstream *bitstream, guint32 extra_bit_size)
{
  guint32 new_bit_size = extra_bit_size + bitstream->bit_size;
  guint32 clear_pos;

  ENCODER_ASSERT(bitstream->bit_size <= bitstream->max_bit_capability);
  if (new_bit_size <= bitstream->max_bit_capability) {
    return TRUE;
  }

  new_bit_size = ((new_bit_size + H264_BITSTREAM_ALLOC_ALIGN_MASK)
                &(~H264_BITSTREAM_ALLOC_ALIGN_MASK));
  ENCODER_ASSERT(new_bit_size%(H264_BITSTREAM_ALLOC_ALIGN_MASK+1) == 0);
  clear_pos = ((bitstream->bit_size+7)>>3);
  bitstream->buffer = realloc(bitstream->buffer, new_bit_size>>3);
  memset(bitstream->buffer+clear_pos, 0, (new_bit_size>>3)-clear_pos);
  bitstream->max_bit_capability = new_bit_size;
  return TRUE;
}

static gboolean
h264_bitstream_write_nal_header(
    H264Bitstream *bitstream,
    guint nal_ref_idc,
    guint nal_unit_type
)
{
  h264_bitstream_write_uint(bitstream, 0, 1);
  h264_bitstream_write_uint(bitstream, nal_ref_idc, 2);
  h264_bitstream_write_uint(bitstream, nal_unit_type, 5);
  return TRUE;
}

#if !HAVE_OLD_H264_ENCODER

static gboolean
h264_bitstream_write_sps(
    H264Bitstream *bitstream,
    VAEncSequenceParameterBufferH264 *seq,
    H264_Profile profile
)
{
  guint32 constraint_set0_flag, constraint_set1_flag;
  guint32 constraint_set2_flag, constraint_set3_flag;
  guint32 gaps_in_frame_num_value_allowed_flag = 0; // ??
  gboolean nal_hrd_parameters_present_flag;

  guint32 b_qpprime_y_zero_transform_bypass = 0;
  guint32 residual_color_transform_flag = 0;
  guint32 pic_height_in_map_units =
    (seq->seq_fields.bits.frame_mbs_only_flag ?
     seq->picture_height_in_mbs :
     seq->picture_height_in_mbs/2);
  guint32 mb_adaptive_frame_field = !seq->seq_fields.bits.frame_mbs_only_flag;
  guint32 i = 0;

  constraint_set0_flag = profile == H264_PROFILE_BASELINE;
  constraint_set1_flag = profile <= H264_PROFILE_MAIN;
  constraint_set2_flag = 0;
  constraint_set3_flag = 0;

  /* profile_idc */
  h264_bitstream_write_uint(bitstream, profile, 8);
  /* constraint_set0_flag */
  h264_bitstream_write_uint(bitstream, constraint_set0_flag, 1);
  /* constraint_set1_flag */
  h264_bitstream_write_uint(bitstream, constraint_set1_flag, 1);
  /* constraint_set2_flag */
  h264_bitstream_write_uint(bitstream, constraint_set2_flag, 1);
  /* constraint_set3_flag */
  h264_bitstream_write_uint(bitstream, constraint_set3_flag, 1);
  /* reserved_zero_4bits */
  h264_bitstream_write_uint(bitstream, 0, 4);
  /* level_idc */
  h264_bitstream_write_uint(bitstream, seq->level_idc, 8);
  /* seq_parameter_set_id */
  h264_bitstream_write_ue(bitstream, seq->seq_parameter_set_id);

  if (profile >= H264_PROFILE_HIGH) {
    /* for high profile */
    ENCODER_ASSERT(0);
    /* chroma_format_idc  = 1, 4:2:0*/
    h264_bitstream_write_ue(bitstream, seq->seq_fields.bits.chroma_format_idc);
    if (3 == seq->seq_fields.bits.chroma_format_idc) {
      h264_bitstream_write_uint(bitstream, residual_color_transform_flag, 1);
    }
    /* bit_depth_luma_minus8 */
    h264_bitstream_write_ue(bitstream, seq->bit_depth_luma_minus8);
    /* bit_depth_chroma_minus8 */
    h264_bitstream_write_ue(bitstream, seq->bit_depth_chroma_minus8);
    /* b_qpprime_y_zero_transform_bypass */
    h264_bitstream_write_uint(bitstream, b_qpprime_y_zero_transform_bypass, 1);
    ENCODER_ASSERT(seq->seq_fields.bits.seq_scaling_matrix_present_flag == 0);
    /*seq_scaling_matrix_present_flag  */
    h264_bitstream_write_uint(bitstream,
                              seq->seq_fields.bits.seq_scaling_matrix_present_flag,
                              1);

    #if 0
    if (seq->seq_fields.bits.seq_scaling_matrix_present_flag) {
      for (i = 0; i < (seq->seq_fields.bits.chroma_format_idc != 3 ? 8 : 12); i++) {
        h264_bitstream_write_uint(bitstream, seq->seq_fields.bits.seq_scaling_list_present_flag, 1);
        if (seq->seq_fields.bits.seq_scaling_list_present_flag) {
          ENCODER_ASSERT(0);
          /* FIXME, need write scaling list if seq_scaling_matrix_present_flag ==1*/
        }
      }
    }
    #endif
  }

  /* log2_max_frame_num_minus4 */
  h264_bitstream_write_ue(bitstream,
                          seq->seq_fields.bits.log2_max_frame_num_minus4);
  /* pic_order_cnt_type */
  h264_bitstream_write_ue(bitstream, seq->seq_fields.bits.pic_order_cnt_type);

  if (seq->seq_fields.bits.pic_order_cnt_type == 0) {
    /* log2_max_pic_order_cnt_lsb_minus4 */
    h264_bitstream_write_ue(bitstream,
                            seq->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);
  } else if (seq->seq_fields.bits.pic_order_cnt_type == 1) {
    ENCODER_ASSERT(0);
    h264_bitstream_write_uint(bitstream,
                              seq->seq_fields.bits.delta_pic_order_always_zero_flag,
                              1);
    h264_bitstream_write_se(bitstream, seq->offset_for_non_ref_pic);
    h264_bitstream_write_se(bitstream,
                            seq->offset_for_top_to_bottom_field);
    h264_bitstream_write_ue(bitstream,
                            seq->num_ref_frames_in_pic_order_cnt_cycle);
    for ( i = 0; i < seq->num_ref_frames_in_pic_order_cnt_cycle; i++) {
      h264_bitstream_write_se(bitstream, seq->offset_for_ref_frame[i]);
    }
  }

  /* num_ref_frames */
  h264_bitstream_write_ue(bitstream, seq->max_num_ref_frames);
  /* gaps_in_frame_num_value_allowed_flag */
  h264_bitstream_write_uint(bitstream,
                            gaps_in_frame_num_value_allowed_flag,
                            1);

  /* pic_width_in_mbs_minus1 */
  h264_bitstream_write_ue(bitstream, seq->picture_width_in_mbs - 1);
  /* pic_height_in_map_units_minus1 */
  h264_bitstream_write_ue(bitstream, pic_height_in_map_units - 1);
  /* frame_mbs_only_flag */
  h264_bitstream_write_uint(bitstream,
                            seq->seq_fields.bits.frame_mbs_only_flag,
                            1);

  if (!seq->seq_fields.bits.frame_mbs_only_flag) { //ONLY mbs
      ENCODER_ASSERT(0);
      h264_bitstream_write_uint(bitstream, mb_adaptive_frame_field, 1);
  }

  /* direct_8x8_inference_flag */
  h264_bitstream_write_uint(bitstream, 0, 1);
  /* frame_cropping_flag */
  h264_bitstream_write_uint(bitstream, seq->frame_cropping_flag, 1);

  if (seq->frame_cropping_flag) {
      /* frame_crop_left_offset */
      h264_bitstream_write_ue(bitstream, seq->frame_crop_left_offset);
      /* frame_crop_right_offset */
      h264_bitstream_write_ue(bitstream, seq->frame_crop_right_offset);
      /* frame_crop_top_offset */
      h264_bitstream_write_ue(bitstream, seq->frame_crop_top_offset);
      /* frame_crop_bottom_offset */
      h264_bitstream_write_ue(bitstream, seq->frame_crop_bottom_offset);
  }

  /* vui_parameters_present_flag */
  h264_bitstream_write_uint(bitstream, seq->vui_parameters_present_flag, 1);
  if (seq->vui_parameters_present_flag) {
    /* aspect_ratio_info_present_flag */
    h264_bitstream_write_uint(bitstream,
                              seq->vui_fields.bits.aspect_ratio_info_present_flag,
                              1);
    if (seq->vui_fields.bits.aspect_ratio_info_present_flag) {
      h264_bitstream_write_uint(bitstream, seq->aspect_ratio_idc, 8);
      if (seq->aspect_ratio_idc == 0xFF) {
        h264_bitstream_write_uint(bitstream, seq->sar_width, 16);
        h264_bitstream_write_uint(bitstream, seq->sar_height, 16);
      }
    }

    /* overscan_info_present_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);
    /* video_signal_type_present_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);
    /* chroma_loc_info_present_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);

    /* timing_info_present_flag */
    h264_bitstream_write_uint(bitstream,
                              seq->vui_fields.bits.timing_info_present_flag,
                              1);
    if (seq->vui_fields.bits.timing_info_present_flag) {
      h264_bitstream_write_uint(bitstream, seq->num_units_in_tick, 32);
      h264_bitstream_write_uint(bitstream, seq->time_scale, 32);
      h264_bitstream_write_uint(bitstream, 1, 1); /* fixed_frame_rate_flag */
    }

    nal_hrd_parameters_present_flag = (seq->bits_per_second > 0 ? TRUE : FALSE);
    /* nal_hrd_parameters_present_flag */
    h264_bitstream_write_uint(bitstream, nal_hrd_parameters_present_flag, 1);
    if (nal_hrd_parameters_present_flag) {
      /* hrd_parameters */
      /* cpb_cnt_minus1 */
      h264_bitstream_write_ue(bitstream, 0);
      h264_bitstream_write_uint(bitstream, 4, 4); /* bit_rate_scale */
      h264_bitstream_write_uint(bitstream, 6, 4); /* cpb_size_scale */

      for (i = 0; i < 1; ++i) {
        /* bit_rate_value_minus1[0] */
        h264_bitstream_write_ue(bitstream, seq->bits_per_second/1024- 1);
        /* cpb_size_value_minus1[0] */
        h264_bitstream_write_ue(bitstream, seq->bits_per_second/1024*8 - 1);
        /* cbr_flag[0] */
        h264_bitstream_write_uint(bitstream, 1, 1);
      }
      /* initial_cpb_removal_delay_length_minus1 */
      h264_bitstream_write_uint(bitstream, 23, 5);
      /* cpb_removal_delay_length_minus1 */
      h264_bitstream_write_uint(bitstream, 23, 5);
      /* dpb_output_delay_length_minus1 */
      h264_bitstream_write_uint(bitstream, 23, 5);
      /* time_offset_length  */
      h264_bitstream_write_uint(bitstream, 23, 5);
    }
    /* vcl_hrd_parameters_present_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);
    if (nal_hrd_parameters_present_flag || 0/*vcl_hrd_parameters_present_flag*/) {
      /* low_delay_hrd_flag */
      h264_bitstream_write_uint(bitstream, 0, 1);
    }
    /* pic_struct_present_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);
    /* bitstream_restriction_flag */
    h264_bitstream_write_uint(bitstream, 0, 1);
  }
  /* rbsp_trailing_bits */
  h264_bitstream_write_trailing_bits(bitstream);
  return TRUE;
}

static gboolean
h264_bitstream_write_pps(
    H264Bitstream *bitstream,
    VAEncPictureParameterBufferH264 *pic
)
{
  guint32 num_slice_groups_minus1 = 0;
  guint32 pic_init_qs_minus26 = 0;
  guint32 redundant_pic_cnt_present_flag = 0;

  /* pic_parameter_set_id */
  h264_bitstream_write_ue(bitstream, pic->pic_parameter_set_id);
  /* seq_parameter_set_id */
  h264_bitstream_write_ue(bitstream, pic->seq_parameter_set_id);
  /* entropy_coding_mode_flag */
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.entropy_coding_mode_flag,
                            1);
  /* pic_order_present_flag */
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.pic_order_present_flag,
                            1);
  /*slice_groups-1*/
  h264_bitstream_write_ue(bitstream, num_slice_groups_minus1);

  if (num_slice_groups_minus1 > 0) {
    /*FIXME*/
    ENCODER_ASSERT(0);
  }
  h264_bitstream_write_ue(bitstream, pic->num_ref_idx_l0_active_minus1);
  h264_bitstream_write_ue(bitstream, pic->num_ref_idx_l1_active_minus1);
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.weighted_pred_flag,
                            1);
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.weighted_bipred_idc,
                            2);
  /* pic_init_qp_minus26 */
  h264_bitstream_write_se(bitstream, pic->pic_init_qp-26);
  /* pic_init_qs_minus26 */
  h264_bitstream_write_se(bitstream, pic_init_qs_minus26);
  /*chroma_qp_index_offset*/
  h264_bitstream_write_se(bitstream, pic->chroma_qp_index_offset);

  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.deblocking_filter_control_present_flag,
                            1);
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.constrained_intra_pred_flag,
                            1);
  h264_bitstream_write_uint(bitstream, redundant_pic_cnt_present_flag, 1);

  /*more_rbsp_data*/
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.transform_8x8_mode_flag,
                            1);
  h264_bitstream_write_uint(bitstream,
                            pic->pic_fields.bits.pic_scaling_matrix_present_flag,
                            1);
  if (pic->pic_fields.bits.pic_scaling_matrix_present_flag) {
    ENCODER_ASSERT(0);
    /* FIXME */
    /*
    for (i = 0; i <
      (6+(-( (chroma_format_idc ! = 3) ? 2 : 6) * -pic->pic_fields.bits.transform_8x8_mode_flag));
      i++) {
      h264_bitstream_write_uint(bitstream, pic->pic_fields.bits.pic_scaling_list_present_flag, 1);
    }
    */
  }

  h264_bitstream_write_se(bitstream, pic->second_chroma_qp_index_offset);
  h264_bitstream_write_trailing_bits(bitstream);
  return TRUE;
}
#endif
