/*
 *  gstvaapiencoder_h263.c - H.263 encoder
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

#include "gstvaapiencoder_h263.h"

#include <string.h>
#include <gst/gstclock.h>

#include "gstvaapiobject.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapidisplay_priv.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h263_encoder_debug);
#define GST_CAT_DEFAULT gst_vaapi_h263_encoder_debug

#define GST_VAAPI_ENCODER_H263_CAST(encoder)    ((GstVaapiEncoderH263 *)(encoder))

struct _GstVaapiEncoderH263Private {
  GstVaapiSurface  *ref_surface;  /* reference buffer*/
  GstVaapiSurface  *recon_surface; /* reconstruct buffer*/

  VABufferID        seq_param_id;
  VABufferID        pic_param_id;
  VABufferID        slic_param_id;
};

G_DEFINE_TYPE(GstVaapiEncoderH263, gst_vaapi_encoder_h263, GST_TYPE_VAAPI_BASE_ENCODER)

GstVaapiEncoderH263 *
gst_vaapi_encoder_h263_new(void)
{
  return GST_VAAPI_ENCODER_H263_CAST(g_object_new(GST_TYPE_VAAPI_ENCODER_H263, NULL));
}

static gboolean
gst_vaapi_encoder_h263_validate_attributes(
    GstVaapiBaseEncoder* base
)
{
  GstVaapiEncoderH263 *encoder = GST_VAAPI_ENCODER_H263_CAST(base);
  if (!ENCODER_WIDTH(encoder) ||
      !ENCODER_HEIGHT(encoder) ||
      !ENCODER_FPS(encoder))
  {
    return FALSE;
  }
  if (!encoder->intra_period) {
    encoder->intra_period = H263_DEFAULT_INTRA_PERIOD;
  }
  if (-1 == encoder->init_qp) {
    encoder->init_qp = H263_DEFAULT_INIT_QP;
  }
  if (-1 == encoder->min_qp) {
    encoder->min_qp = H263_DEFAULT_MIN_QP;
  }

  /* default compress ratio 1: (4*8*1.5) */
  if (!encoder->bitrate) {
    encoder->bitrate =
        ENCODER_WIDTH(encoder)*ENCODER_HEIGHT(encoder)*ENCODER_FPS(encoder)/4/1024;
  }
  gst_vaapi_base_encoder_set_va_profile(GST_VAAPI_BASE_ENCODER(encoder),
                                        VAProfileH263Baseline);
  return TRUE;

}


static void
h263_release_parameters(
    GstVaapiEncoderH263 *encoder
)
{
  GstVaapiEncoderH263Private *priv = encoder->priv;
  VADisplay va_dpy = ENCODER_VA_DISPLAY(encoder);
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
  if (VA_INVALID_ID != priv->slic_param_id) {
    va_status = vaDestroyBuffer(va_dpy, priv->slic_param_id);
    priv->slic_param_id = VA_INVALID_ID;
  }

}

static gboolean
gst_vaapi_encoder_h263_release_resource(
    GstVaapiBaseEncoder* base
)
{
  GstVaapiEncoderH263 *encoder = GST_VAAPI_ENCODER_H263_CAST(base);
  GstVaapiEncoderH263Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);

  h263_release_parameters(encoder);

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

  return TRUE;
}

static EncoderStatus
gst_vaapi_encoder_h263_rendering(
    GstVaapiBaseEncoder *base,
    GstVaapiSurface *surface,
    guint frame_index,
    VABufferID coded_buf,
    gboolean *is_key
)

{
  GstVaapiEncoderH263 *encoder = GST_VAAPI_ENCODER_H263_CAST(base);
  GstVaapiEncoderH263Private *priv = encoder->priv;
  GstVaapiContext *context = ENCODER_CONTEXT(base);
  VADisplay va_dpy = ENCODER_VA_DISPLAY(base);
  VAContextID context_id = ENCODER_VA_CONTEXT(base);
  VABufferID va_buffers[64];
  guint32    va_buffers_count = 0;

  VAStatus va_status = VA_STATUS_SUCCESS;
  EncoderStatus ret = ENCODER_NO_ERROR;

  *is_key = (frame_index % encoder->intra_period == 0);

  /* initialize sequence parameter set, only first time */
  if (VA_INVALID_ID == priv->seq_param_id) { /*only the first time*/
    VAEncSequenceParameterBufferH263 seq_param = {0};

    seq_param.intra_period = encoder->intra_period;
    seq_param.bits_per_second = encoder->bitrate * 1024;
    seq_param.frame_rate = ENCODER_FPS(encoder);
    seq_param.initial_qp = encoder->init_qp;
    seq_param.min_qp = encoder->min_qp;

    va_status = vaCreateBuffer(va_dpy, context_id,
                               VAEncSequenceParameterBufferType,
                               sizeof(seq_param), 1,
                               &seq_param,
                               &priv->seq_param_id);
    ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                         ENCODER_ENC_RES_ERR,
                         "h263 alloc seq-buffer failed.");
    va_buffers[va_buffers_count++] = priv->seq_param_id;
  }

  /* set reference and reconstructed surfaces */
  if (!priv->ref_surface) {
    priv->ref_surface = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->ref_surface,
                         ENCODER_SURFACE_ERR,
                         "h263 reference surface, h263_pop_free_surface failed.");
  }
  if (!priv->recon_surface) {
    priv->recon_surface = gst_vaapi_context_get_surface(context);
    ENCODER_CHECK_STATUS(priv->recon_surface,
                         ENCODER_SURFACE_ERR,
                         "h263 reconstructed surface, h263_pop_free_surface failed.");
  }

  /* initialize picture, every time, every frame */
  VAEncPictureParameterBufferH263 pic_param = {0};
  pic_param.reference_picture = GST_VAAPI_OBJECT_ID(priv->ref_surface);
  pic_param.reconstructed_picture = GST_VAAPI_OBJECT_ID(priv->recon_surface);
  pic_param.coded_buf = coded_buf;
  pic_param.picture_width = ENCODER_WIDTH(encoder);
  pic_param.picture_height = ENCODER_HEIGHT(encoder);
  pic_param.picture_type = (*is_key) ? VAEncPictureTypeIntra : VAEncPictureTypePredictive;
  if (VA_INVALID_ID != priv->pic_param_id) { /* destroy first*/
    va_status = vaDestroyBuffer(va_dpy, priv->pic_param_id);
    priv->pic_param_id = VA_INVALID_ID;
  }

  va_status = vaCreateBuffer(va_dpy, context_id, VAEncPictureParameterBufferType,
                               sizeof(pic_param), 1, &pic_param, &priv->pic_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS ==va_status,
                       ENCODER_ENC_RES_ERR, "h263 creating pic-param buffer failed.");
  va_buffers[va_buffers_count++] = priv->pic_param_id;

  /*initialize slice parameters, only ONE slice for h263*/
  VAEncSliceParameterBuffer slice_param = { 0 };
  slice_param.start_row_number = 0;
  slice_param.slice_height = (ENCODER_HEIGHT(encoder)+15)/16; /*MB?*/
  slice_param.slice_flags.bits.is_intra = *is_key;
  slice_param.slice_flags.bits.disable_deblocking_filter_idc = 0;
  if (VA_INVALID_ID != priv->slic_param_id) {
    vaDestroyBuffer(va_dpy, priv->slic_param_id);
    priv->slic_param_id = VA_INVALID_ID;
  }

  va_status = vaCreateBuffer(va_dpy,
                             context_id,
                             VAEncSliceParameterBufferType,
                             sizeof(slice_param),
                             1,
                             &slice_param,
                             &priv->slic_param_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS ==va_status,
                       ENCODER_ENC_RES_ERR,
                       "h263 creating slice-parameters buffer failed.");
  va_buffers[va_buffers_count++] = priv->slic_param_id;

  ret = gst_vaapi_encoder_render_picture(GST_VAAPI_ENCODER_CAST(encoder),
                                         surface,
                                         va_buffers,
                                         va_buffers_count);
  ENCODER_CHECK_STATUS(ret == ENCODER_NO_ERROR,
                       ENCODER_PICTURE_ERR,
                       "h263 rendering slice-parameters buffer failed.");

  /*swap ref_surface and recon_surface */
  GstVaapiSurface *swap = priv->ref_surface;
  priv->ref_surface = priv->recon_surface;
  priv->recon_surface = swap;

end:
  return ret;
}

static void
gst_vaapi_encoder_h263_init(GstVaapiEncoderH263 *encoder)
{
  GstVaapiEncoderH263Private *priv = GST_VAAPI_ENCODER_H263_GET_PRIVATE(encoder);
  encoder->priv = priv;
  ENCODER_ASSERT(priv);

  /* init public */
  encoder->bitrate = 0;
  encoder->intra_period = H263_DEFAULT_INTRA_PERIOD;
  encoder->init_qp = H263_DEFAULT_INIT_QP;
  encoder->min_qp = H263_DEFAULT_MIN_QP;

  /* init private */
  priv->ref_surface = NULL;
  priv->recon_surface = NULL;

  priv->seq_param_id = VA_INVALID_ID;
  priv->pic_param_id = VA_INVALID_ID;
  priv->slic_param_id = VA_INVALID_ID;
}

static void
gst_vaapi_encoder_h263_finalize(GObject *object)
{
  /*free private buffers*/
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER(object);

  if (gst_vaapi_encoder_get_state(encoder) != VAAPI_ENC_NULL) {
    gst_vaapi_encoder_uninitialize(encoder);
  }
  G_OBJECT_CLASS(gst_vaapi_encoder_h263_parent_class)->finalize(object);
}

static void
gst_vaapi_encoder_h263_class_init(GstVaapiEncoderH263Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstVaapiBaseEncoderClass * const base_class = GST_VAAPI_BASE_ENCODER_CLASS(klass);
  GstVaapiEncoderClass * const encoder_class = GST_VAAPI_ENCODER_CLASS(klass);
  g_type_class_add_private(klass, sizeof(GstVaapiEncoderH263Private));

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h263_encoder_debug, "gst_va_h263_encoder", 0,
      "gst_va_h263_encoder element");

  object_class->finalize = gst_vaapi_encoder_h263_finalize;

  base_class->validate_attributes = gst_vaapi_encoder_h263_validate_attributes;
  base_class->pre_alloc_resource  = NULL;
  base_class->release_resource    = gst_vaapi_encoder_h263_release_resource;
  base_class->render_frame = gst_vaapi_encoder_h263_rendering;
  base_class->notify_buffer = NULL;
  base_class->wrap_buffer = NULL;

  /*
  encoder_class->flush = gst_vaapi_encoder_h263_flush;
  */
  encoder_class->get_codec_data = NULL;

}
