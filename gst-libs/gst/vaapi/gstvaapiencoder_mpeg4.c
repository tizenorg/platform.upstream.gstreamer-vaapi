/*
 *  gstvaapiencoder_mpeg4.c - MPEG-4 encoder
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

#include "gstvaapiencoder_mpeg4.h"

#include <string.h>
#include "gst/gstclock.h"

#include "gstvaapiobject.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapidisplay_priv.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_mpeg4_encoder_debug);
#define GST_CAT_DEFAULT gst_vaapi_mpeg4_encoder_debug

#define GST_VAAPI_ENCODER_MPEG4_CAST(encoder)    ((GstVaapiEncoderMpeg4 *)(encoder))

#define VISUAL_OBJECT_SEQUENCE_START_CODE  0x000001B0
#define VISUAL_OBJECT_SEQUENCE_END_CODE    0x000001B1
#define VISUAL_OBJECT_START_CODE           0x000001B5
#define VIDEO_OBJECT_PLANE_START_CODE      0x000001B6
/* Video Object Start Code range */
#define VIDEO_OBJECT_START_CODE_MIN        0x00000100
#define VIDEO_OBJECT_START_CODE_MAX        0x0000011F
/* Video Object Layer Start Code range 0x00000120 ~ 0x0000012F*/
#define VIDEO_OBJECT_LAYER_START_CODE      0x00000120
#define VIDEO_OBJECT_LAYER_START_CODE_MASK 0xFFFFFFF0


struct _GstVaapiEncoderMpeg4Private {
  GstVaapiSurface  *ref_surface;  /* reference buffer*/
  GstVaapiSurface  *recon_surface; /* reconstruct buffer*/

  VABufferID        seq_param_id;
  VABufferID        pic_param_id;
  VABufferID        slice_param_id;

  GstBuffer        *codec_data;
};

G_DEFINE_TYPE(GstVaapiEncoderMpeg4, gst_vaapi_encoder_mpeg4, GST_TYPE_VAAPI_BASE_ENCODER)

GstVaapiEncoderMpeg4 *
gst_vaapi_encoder_mpeg4_new(void)
{
  return GST_VAAPI_ENCODER_MPEG4_CAST(
             g_object_new(GST_TYPE_VAAPI_ENCODER_MPEG4, NULL));
}

gboolean
gst_vaapi_encoder_mpeg4_validate_attributes(
    GstVaapiBaseEncoder *base
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);

  if (!ENCODER_WIDTH(encoder) ||
      !ENCODER_HEIGHT(encoder) ||
      !ENCODER_FPS(encoder)) {
    return FALSE;
  }
  if (VAProfileMPEG4Simple != encoder->profile &&
      VAProfileMPEG4AdvancedSimple != encoder->profile) {
    return FALSE;
  }
  gst_vaapi_base_encoder_set_va_profile(base, encoder->profile);

  if (!encoder->intra_period) {
    encoder->intra_period = MPEG4_DEFAULT_INTRA_PERIOD;
  }
  if (-1 == encoder->init_qp) {
    encoder->init_qp = MPEG4_DEFAULT_INIT_QP;
  }
  if (-1 == encoder->min_qp) {
    encoder->min_qp = MPEG4_DEFAULT_MIN_QP;
  }

  /* default compress ratio 1: (4*8*1.5) */
  if (!encoder->bitrate) {
    encoder->bitrate =
        ENCODER_WIDTH(encoder)*ENCODER_HEIGHT(encoder)*ENCODER_FPS(encoder)/4/1024;
  }
  return TRUE;

}

static void
mpeg4_release_parameters(
    GstVaapiEncoderMpeg4 *encoder
)
{
  GstVaapiEncoderMpeg4Private *priv = encoder->priv;
  VADisplay va_dpy = ENCODER_DISPLAY(encoder);
  VAStatus va_status = VA_STATUS_SUCCESS;

  VAAPI_UNUSED_ARG(va_status);

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
}

static gboolean
gst_vaapi_encoder_mpeg4_release_resource(
    GstVaapiBaseEncoder* base
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);
  GstVaapiEncoderMpeg4Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);

  mpeg4_release_parameters(encoder);

  /*remove ref_surface*/
  if (priv->ref_surface) {
    if (context) {
      gst_vaapi_context_put_surface(context, priv->ref_surface);
    } else {
      g_object_unref(priv->ref_surface);
    }
    priv->ref_surface = NULL;
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

  if (priv->codec_data) {
    gst_buffer_unref(priv->codec_data);
    priv->codec_data = NULL;
  }

  return TRUE;
}

static guint32
mpeg4_get_profile_level_indication(guint32 profile)
{
  switch(profile) {
  case VAProfileMPEG4Simple:
    return MPEG4_DEFAULT_SIMPLE_PROFILE_AND_LEVEL;
  case VAProfileMPEG4AdvancedSimple:
    return MPEG4_DEFAULT_ADVANCED_SIMPLE_PROFILE_AND_LEVEL;
  default:
    return 0;
  }
  return 0;
}

static EncoderStatus
gst_vaapi_encoder_mpeg4_rendering(
    GstVaapiBaseEncoder *base,
    GstVaapiSurface *surface,
    guint frame_index,
    VABufferID coded_buf,
    gboolean *is_key
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);
  GstVaapiEncoderMpeg4Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);
  VADisplay va_dpy = ENCODER_VA_DISPLAY(encoder);
  VAContextID context_id = ENCODER_VA_CONTEXT(encoder);
  VABufferID va_buffers[64];
  guint32    va_buffers_count = 0;

  VAStatus va_status = VA_STATUS_SUCCESS;
  EncoderStatus ret = ENCODER_NO_ERROR;

  *is_key = (frame_index % encoder->intra_period == 0);

  /* initialize sequence parameter set, only first time */
  if (VA_INVALID_ID == priv->seq_param_id) { /*only the first time*/
    VAEncSequenceParameterBufferMPEG4 seq_param = {0};

    seq_param.profile_and_level_indication =
        mpeg4_get_profile_level_indication(encoder->profile);
    seq_param.intra_period = encoder->intra_period;
    seq_param.video_object_layer_width = ENCODER_WIDTH(encoder);
    seq_param.video_object_layer_height = ENCODER_HEIGHT(encoder);
    seq_param.vop_time_increment_resolution = ENCODER_FPS(encoder);
    seq_param.fixed_vop_rate = MPEG4_DEFAULT_FIXED_VOP_RATE;
    if (seq_param.fixed_vop_rate) {
      seq_param.fixed_vop_time_increment = 1;
    }
    seq_param.bits_per_second = encoder->bitrate * 1024;
    seq_param.frame_rate = ENCODER_FPS(encoder);
    seq_param.initial_qp = encoder->init_qp;
    seq_param.min_qp = encoder->min_qp; //mpeg4_encoder->min_qp;

    va_status = vaCreateBuffer(va_dpy, context_id,
                               VAEncSequenceParameterBufferType,
                               sizeof(seq_param), 1,
                               &seq_param,
                               &priv->seq_param_id);
    ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                         ENCODER_ENC_RES_ERR,
                         "mpeg4 alloc seq-buffer failed.");
    va_buffers[va_buffers_count++] = priv->seq_param_id;
  }

  /* set reference and reconstructed surfaces */
  if (!priv->ref_surface) {
    priv->ref_surface = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->ref_surface,
                         ENCODER_SURFACE_ERR,
                         "mpeg4 reference surface, mpeg4_pop_free_surface failed.");
  }
  if (!priv->recon_surface) {
    priv->recon_surface = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->recon_surface,
                         ENCODER_SURFACE_ERR,
                         "mpeg4 reconstructed surface, mpeg4_pop_free_surface failed.");
  }

  /* initialize picture, every time, every frame */
  VAEncPictureParameterBufferMPEG4 pic_param = {0};
  pic_param.reference_picture = GST_VAAPI_OBJECT_ID(priv->ref_surface);
  pic_param.reconstructed_picture = GST_VAAPI_OBJECT_ID(priv->recon_surface);
  pic_param.coded_buf = coded_buf;
  pic_param.picture_width = ENCODER_WIDTH(encoder);
  pic_param.picture_height = ENCODER_HEIGHT(encoder);
  if (0 == frame_index) {
    pic_param.modulo_time_base = 0;
  } else {
    pic_param.modulo_time_base =
        ((frame_index%ENCODER_FPS(encoder)) == 0 ? 1 : 0);
  }
  pic_param.vop_time_increment = 301%ENCODER_FPS(encoder);
  pic_param.picture_type =
    (*is_key ? VAEncPictureTypeIntra : VAEncPictureTypePredictive);

  if (VA_INVALID_ID != priv->pic_param_id) { /* destroy first*/
    va_status = vaDestroyBuffer(va_dpy, priv->pic_param_id);
    priv->pic_param_id = VA_INVALID_ID;
  }

  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncPictureParameterBufferType,
                             sizeof(pic_param), 1,
                             &pic_param,
                             &priv->pic_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS ==va_status,
                       ENCODER_ENC_RES_ERR,
                       "mpeg4 creating pic-param buffer failed.");

  va_buffers[va_buffers_count++] = priv->pic_param_id;

  /*initialize slice parameters, only ONE slice for mpeg4*/
  VAEncSliceParameterBuffer slice_param = { 0 };
  slice_param.start_row_number = 0;
  slice_param.slice_height = (ENCODER_HEIGHT(encoder)+15)/16; /*MB?*/
  slice_param.slice_flags.bits.is_intra = *is_key;
  slice_param.slice_flags.bits.disable_deblocking_filter_idc = 0;
  if (VA_INVALID_ID != priv->slice_param_id) {
    vaDestroyBuffer(va_dpy, priv->slice_param_id);
    priv->slice_param_id = VA_INVALID_ID;
  }

  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncSliceParameterBufferType,
                             sizeof(slice_param),
                             1,
                             &slice_param,
                             &priv->slice_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       ENCODER_ENC_RES_ERR,
                       "mpeg4 creating slice-parameters buffer failed.");

  va_buffers[va_buffers_count++] = priv->slice_param_id;

  ret = gst_vaapi_encoder_render_picture(GST_VAAPI_ENCODER_CAST(encoder),
                                         surface,
                                         va_buffers,
                                         va_buffers_count);
  ENCODER_CHECK_STATUS(ret == ENCODER_NO_ERROR,
                       ENCODER_PICTURE_ERR,
                       "mpeg4 rendering slice-parameters buffer failed.");

  /*swap ref_surface and recon_surface */
  GstVaapiSurface *swap = priv->ref_surface;
  priv->ref_surface = priv->recon_surface;
  priv->recon_surface = swap;

end:
  return ret;
}

#if 0
static GstBuffer *
gst_vaapi_encoder_mpeg4_copy_coded_buffer(GstVaapiBaseEncoder *encoder,
            guint8 *frame, guint32 frame_size, VABufferID *coded_buf)

{
   /*process data*/
  GstBuffer* buffer = gst_buffer_new_and_alloc(frame_size);
  memcpy(GST_BUFFER_DATA(buffer), frame, frame_size);

  #if 0
  GstVaapiEncoderMpeg4 *mpeg4_encoder = GST_VAAPI_ENCODER_MPEG4_CAST(encoder);
  if (mpeg4_encoder->profile == VAProfileMPEG4AdvancedSimple) {
    guint8 *start_code = GST_BUFFER_DATA(buffer)+16; /*fix old issue of ASP in mrst platform*/
    if (start_code[0] == 0x01 && start_code[1] == 0x20
        && start_code[-1] == 0x00 && start_code[-2] == 0x00)
    {
      start_code[2] = 0x08;
    }
  }
  #endif

  return buffer;
}
#endif

static gboolean
find_video_object_configuration_info(
    const guint8 *in_buffer,
    guint32 in_size,
    const guint8 **out_buffer,
    guint32 *out_size
)
{
  guint32 value = 0x00;
  const guint8 *end = in_buffer + in_size;

  while(in_buffer < end) {
    value = ((value<<8)|(*in_buffer));
    if (VISUAL_OBJECT_SEQUENCE_START_CODE == value) {
      *out_buffer = in_buffer - 3;
      ++in_buffer;
      break;
    }
    ++in_buffer;
  }
  if (in_buffer >= end)
    return FALSE;

  while(in_buffer < end) {
    value = ((value<<8)|(*in_buffer));
    if (VIDEO_OBJECT_PLANE_START_CODE == value) {
      *out_size = (in_buffer - 3 - *out_buffer);
      return TRUE;
    }
    ++in_buffer;
  }
  return FALSE;
}

static gboolean
mpeg4_encoder_generate_codec_data(
    const guint8 *in_buffer,
    guint32 in_size,
    GstBuffer **out_buffer
)
{
  const guint8 *codec_buffer = NULL;
  guint32 codec_size = 0;
  guint8 *visual_obj_seq_end = NULL;

  if (!find_video_object_configuration_info(in_buffer,
                                            in_size,
                                            &codec_buffer,
                                            &codec_size)
     ) {
    return FALSE;
  }
  ENCODER_ASSERT(codec_size);
  *out_buffer = gst_buffer_new_and_alloc(codec_size+4);
  memcpy(GST_BUFFER_DATA(*out_buffer), codec_buffer, codec_size);
  visual_obj_seq_end = GST_BUFFER_DATA(*out_buffer) + codec_size;
  visual_obj_seq_end[0] = (VISUAL_OBJECT_SEQUENCE_END_CODE>>24);
  visual_obj_seq_end[1] = (VISUAL_OBJECT_SEQUENCE_END_CODE>>16);
  visual_obj_seq_end[2] = (VISUAL_OBJECT_SEQUENCE_END_CODE>>8);
  visual_obj_seq_end[3] = (guint8)VISUAL_OBJECT_SEQUENCE_END_CODE;
  return TRUE;
}

static void
gst_vaapi_encoder_mpeg4_notify_frame(
    GstVaapiBaseEncoder *base,
    guint8 *buf,
    guint32 size
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);
  GstVaapiEncoderMpeg4Private *priv = encoder->priv;
  if (!priv->codec_data) {
    if (!mpeg4_encoder_generate_codec_data(buf, size, &priv->codec_data)) {
      ENCODER_LOG_ERROR("mpeg4 encoder coded data error,"
                        "please check <mpeg4_encoder_generate_codec_data>.");
    }
  }
  if (priv->codec_data) {
    gst_vaapi_base_encoder_set_frame_notify(base, FALSE);
  }
}

static EncoderStatus
gst_vaapi_encoder_mpeg4_flush(
    GstVaapiEncoder* base
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);

  mpeg4_release_parameters(encoder);
  return ENCODER_NO_ERROR;
}

static EncoderStatus
gst_vaapi_encoder_mpeg4_get_codec_data(
    GstVaapiEncoder *base,
    GstBuffer **buffer
)
{
  GstVaapiEncoderMpeg4 *encoder = GST_VAAPI_ENCODER_MPEG4_CAST(base);
  GstVaapiEncoderMpeg4Private *priv = encoder->priv;

  if (!priv->codec_data)
    return ENCODER_DATA_NOT_READY;
  *buffer = gst_buffer_ref(priv->codec_data);
  return ENCODER_NO_ERROR;
}

static void
gst_vaapi_encoder_mpeg4_init(GstVaapiEncoderMpeg4 *encoder)
{
  GstVaapiEncoderMpeg4Private *priv = GST_VAAPI_ENCODER_MPEG4_GET_PRIVATE(encoder);
  ENCODER_ASSERT(priv);
  encoder->priv = priv;

  /* init public */
  encoder->profile = VAProfileMPEG4Simple;
  encoder->bitrate = 0;
  encoder->intra_period = MPEG4_DEFAULT_INTRA_PERIOD;
  encoder->init_qp = MPEG4_DEFAULT_INIT_QP;
  encoder->min_qp = MPEG4_DEFAULT_MIN_QP;

  gst_vaapi_base_encoder_set_frame_notify(GST_VAAPI_BASE_ENCODER(encoder), TRUE);
  /* init private */
  priv->ref_surface = NULL;
  priv->recon_surface = NULL;

  priv->seq_param_id = VA_INVALID_ID;
  priv->pic_param_id = VA_INVALID_ID;
  priv->slice_param_id = VA_INVALID_ID;

  priv->codec_data = NULL;
}

static void
gst_vaapi_encoder_mpeg4_finalize(GObject *object)
{
  /*free private buffers*/
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER(object);

  if (gst_vaapi_encoder_get_state(encoder) != VAAPI_ENC_NULL) {
    gst_vaapi_encoder_uninitialize(encoder);
  }
  G_OBJECT_CLASS(gst_vaapi_encoder_mpeg4_parent_class)->finalize(object);
}

static void
gst_vaapi_encoder_mpeg4_class_init(GstVaapiEncoderMpeg4Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstVaapiEncoderClass * const encoder_class = GST_VAAPI_ENCODER_CLASS(klass);
  GstVaapiBaseEncoderClass * const base_class = GST_VAAPI_BASE_ENCODER_CLASS(klass);

  g_type_class_add_private(klass, sizeof(GstVaapiEncoderMpeg4Private));

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_mpeg4_encoder_debug, "gst_va_mpeg4_encoder", 0,
      "gst_va_mpeg4_encoder element");

  object_class->finalize = gst_vaapi_encoder_mpeg4_finalize;

  base_class->validate_attributes = gst_vaapi_encoder_mpeg4_validate_attributes;
  base_class->pre_alloc_resource  = NULL;
  base_class->release_resource    = gst_vaapi_encoder_mpeg4_release_resource;
  base_class->render_frame = gst_vaapi_encoder_mpeg4_rendering;
  base_class->notify_buffer = gst_vaapi_encoder_mpeg4_notify_frame;
  base_class->wrap_buffer = NULL;

  encoder_class->flush = gst_vaapi_encoder_mpeg4_flush;
  encoder_class->get_codec_data = gst_vaapi_encoder_mpeg4_get_codec_data;
}
