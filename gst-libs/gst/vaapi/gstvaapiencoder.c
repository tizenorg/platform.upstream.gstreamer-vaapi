/*
 *  gstvaapiencoder.c - VA-API encoder interface
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

#include "gstvaapiencoder.h"

#include <string.h>

#include "gstvaapidisplay_x11.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapidisplay_priv.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_encoder_debug);
#define GST_CAT_DEFAULT gst_vaapi_encoder_debug


G_DEFINE_TYPE(GstVaapiEncoder, gst_vaapi_encoder, G_TYPE_OBJECT)

gboolean
gst_vaapi_encoder_set_display(
    GstVaapiEncoder* encoder,
    GstVaapiDisplay *display
)
{
  GstVaapiEncoderPrivate *priv = encoder->priv;
  if (display == priv->display) {
    return TRUE;
  }

  if (VAAPI_ENC_INIT < priv->state) {
    return FALSE;
  }
  if (priv->display) {
    g_object_unref(priv->display);
    priv->display = NULL;
    priv->va_display = NULL;
  }
  if (display) {
    priv->display = g_object_ref(display);
    priv->va_display = gst_vaapi_display_get_display(display);
  }
  return TRUE;
}

GstVaapiDisplay *
gst_vaapi_encoder_get_display(GstVaapiEncoder* encoder)
{
  GstVaapiEncoderPrivate *priv = encoder->priv;
  return (priv->display ? g_object_ref(priv->display) : NULL);
}

GstVaapiContext *
gst_vaapi_encoder_get_context(GstVaapiEncoder* encoder)
{
  GstVaapiEncoderPrivate *priv = encoder->priv;
  return (priv->context ? g_object_ref(priv->context) : NULL);
}


VAAPI_Encode_State
gst_vaapi_encoder_get_state(GstVaapiEncoder* encoder)
{
  GstVaapiEncoderPrivate *priv = encoder->priv;
  return priv->state;
}


EncoderStatus
gst_vaapi_encoder_initialize(GstVaapiEncoder* encoder)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  /* check state */
  if (VAAPI_ENC_INIT == priv->state) {
    return ENCODER_NO_ERROR;
  }
  ENCODER_ASSERT(VAAPI_ENC_NULL == priv->state);
  if (VAAPI_ENC_NULL != priv->state) {
    return ENCODER_STATE_ERR;
  }

  /* create va_dpy*/
  if (!priv->display) {
    priv->display = gst_vaapi_display_x11_new(NULL);
    ENCODER_CHECK_STATUS(priv->display,
                         ENCODER_DISPLAY_ERR,
                         "gst_vaapi_display_x11_new failed.");
  }
  priv->va_display = gst_vaapi_display_get_display(priv->display);

  if (encoder_class->initialize) {
    ret = encoder_class->initialize(encoder);
    ENCODER_CHECK_STATUS (ENCODER_NO_ERROR == ret,
                          ret,
                          "encoder <initialize> failed.");
  }
  priv->state = VAAPI_ENC_INIT;

end:
  return ret;
}

EncoderStatus
gst_vaapi_encoder_open(GstVaapiEncoder* encoder)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  /* check state */
  if (VAAPI_ENC_OPENED == priv->state) {
    return ENCODER_NO_ERROR;
  }
  ENCODER_ASSERT(VAAPI_ENC_INIT == priv->state);
  if (VAAPI_ENC_INIT != priv->state) {
    return ENCODER_STATE_ERR;
  }
  ENCODER_ASSERT(!priv->context);

  ENCODER_CHECK_STATUS(encoder_class->open,
                       ENCODER_FUNC_PTR_ERR,
                       "encoder <open> function pointer empty.");
  ret = encoder_class->open(encoder, &priv->context);
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR == ret,
                       ret,
                       "encoder <open> failed.");
  ENCODER_CHECK_STATUS(priv->context,
                       ENCODER_CONTEXT_ERR,
                       "encoder <open> context failed.");
  priv->va_context = gst_vaapi_context_get_id(priv->context);
  priv->state = VAAPI_ENC_OPENED;

end:
  return ret;
}

EncoderStatus
gst_vaapi_encoder_encode(
    GstVaapiEncoder* encoder,
    GstBuffer *pic
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  ENCODER_CHECK_STATUS(priv->state >= VAAPI_ENC_OPENED,
                       ENCODER_STATE_ERR,
                       "encoder was not opened before <encode>.");
  ENCODER_CHECK_STATUS(encoder_class->encode,
                       ENCODER_FUNC_PTR_ERR,
                       "encoder <encode> function pointer empty.");
  ret = encoder_class->encode(encoder, pic);
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR <= ret,
                       ret,
                       "encoder <encode> failed.");
  if (priv->state < VAAPI_ENC_ENCODING) {
    priv->state = VAAPI_ENC_ENCODING;
  }
end:
  return ret;
}

EncoderStatus
gst_vaapi_encoder_get_encoded_buffer(
    GstVaapiEncoder* encoder,
    GstBuffer **buf
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  ENCODER_CHECK_STATUS(priv->state >= VAAPI_ENC_OPENED,
                       ENCODER_STATE_ERR,
                       "encoder was not encoding <encode>.");
  ENCODER_CHECK_STATUS(encoder_class->get_buf,
                       ENCODER_FUNC_PTR_ERR,
                       "encoder <get_buf> function pointer empty.");
  ret = encoder_class->get_buf(encoder, buf);
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR <= ret,
                       ret,
                       "encoder <get_buf> failed.");
end:
  return ret;
}


EncoderStatus
gst_vaapi_encoder_get_codec_data(
    GstVaapiEncoder* encoder,
    GstBuffer **codec_data
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  ENCODER_CHECK_STATUS(priv->state >= VAAPI_ENC_OPENED,
                       ENCODER_STATE_ERR,
                       "encoder was not opened before <get_codec_data>.");
  if (!encoder_class->get_codec_data) {
    *codec_data = NULL;
    ENCODER_LOG_INFO("There's no codec_data");
    return ret;
  }
  ret = encoder_class->get_codec_data(encoder, codec_data);

end:
  return ret;
}

EncoderStatus
gst_vaapi_encoder_flush(
    GstVaapiEncoder* encoder
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  if (priv->state < VAAPI_ENC_OPENED) {
    return ENCODER_STATE_ERR;
  }
  ENCODER_CHECK_STATUS(encoder_class->flush,
                       ENCODER_FUNC_PTR_ERR,
                       "encoder <flush> function pointer empty.");
  ret = encoder_class->flush(encoder);
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR == ret,
                       ret,
                       "encoder <flush> failed.");
end:
  return ret;
}

EncoderStatus
gst_vaapi_encoder_close(GstVaapiEncoder* encoder)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  if (VAAPI_ENC_INIT >= priv->state) {
    return ENCODER_NO_ERROR;
  }
  ENCODER_CHECK_STATUS(encoder_class->close,
                       ENCODER_FUNC_PTR_ERR,
                       "encoder <close> function pointers empty.");
  ret = encoder_class->close(encoder);
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR == ret,
                       ret,
                       "encoder <close> failed.");
end:
  if (priv->context) {
    g_object_unref(priv->context);
    priv->context = NULL;
    priv->va_context = VA_INVALID_ID;
  }

  priv->state = VAAPI_ENC_INIT;
  return ret;
}

EncoderStatus
gst_vaapi_encoder_render_picture(
    GstVaapiEncoder *encoder,
    GstVaapiSurface *surface,
    VABufferID *bufs,
    guint num
)
{
  GstVaapiDisplay *display;
  VASurfaceID  surface_id;
  VAContextID context_id;
  VADisplay   va_display;
  VAStatus    va_status;
  gboolean is_locked = FALSE;
  EncoderStatus ret = ENCODER_NO_ERROR;

  display = ENCODER_DISPLAY(encoder);

  g_return_val_if_fail(surface, ENCODER_PARAMETER_ERR);
  g_return_val_if_fail(ENCODER_CONTEXT(encoder), ENCODER_PARAMETER_ERR);

  surface_id = (VASurfaceID)GST_VAAPI_OBJECT_ID(surface);
  context_id = ENCODER_VA_CONTEXT(encoder);
  va_display = ENCODER_VA_DISPLAY(encoder);
  g_return_val_if_fail(surface_id != VA_INVALID_SURFACE, ENCODER_PARAMETER_ERR);

  /* begin surface*/
  ENCODER_ACQUIRE_DISPLAY_LOCK(display);
  va_status = vaBeginPicture(va_display, context_id, surface_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       ENCODER_PARAMETER_ERR,
					   "vaBeginPicture failed");

  va_status = vaRenderPicture(va_display,
                              context_id,
                              bufs,
                              num);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       ENCODER_PARAMETER_ERR,
					   "vaRenderPicture failed");

  va_status = vaEndPicture(va_display, context_id);
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       ENCODER_PARAMETER_ERR,
					   "vaRenderPicture failed");

end:
  ENCODER_RELEASE_DISPLAY_LOCK(display);
  return ret;
}

EncoderStatus
gst_vaapi_encoder_uninitialize(GstVaapiEncoder* encoder)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiEncoderClass *encoder_class = GST_VAAPI_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderPrivate *priv = encoder->priv;

  if (VAAPI_ENC_NULL == priv->state) {
    return ENCODER_NO_ERROR;
  }

  if (VAAPI_ENC_INIT < priv->state) {
    ret = gst_vaapi_encoder_close(encoder);
  }
  ENCODER_ASSERT(VAAPI_ENC_INIT == priv->state);
  if (encoder_class->uninitialize) {
    ret = encoder_class->uninitialize(encoder);
    ENCODER_CHECK_STATUS(ENCODER_NO_ERROR == ret,
                         ret,
                         "encoder <uninitialize> failed.");
  }

end:
  if (priv->display) {
    g_object_unref(priv->display);
    priv->display = NULL;
    priv->va_display = NULL;
  }
  priv->state = VAAPI_ENC_NULL;
  return ret;

}

char *
vaapi_encoder_dump_bytes(const guint8 *buf, guint32 num)
{
  static char tmp[1024];
  guint32 i = 0;
  memset(tmp, 0, sizeof(tmp));

  char *p = tmp;
  for (i = 0; i < num; i++) {
    snprintf(p, 1024-(p-tmp), "%02x", (guint8)buf[i]);
    p += strlen(p);
  }
  return tmp;
}

static void
gst_vaapi_encoder_init(GstVaapiEncoder *encoder)
{
  GstVaapiEncoderPrivate *priv;

  encoder->priv = GST_VAAPI_ENCODER_GET_PRIVATE(encoder);
  priv = encoder->priv;
  ENCODER_ASSERT(priv);

  priv->display = NULL;
  priv->va_display = NULL;
  priv->context = NULL;
  priv->va_context = VA_INVALID_ID;
  priv->state = VAAPI_ENC_NULL;

  encoder->width = 0;
  encoder->height = 0;
  encoder->frame_rate = 0;
  encoder->rate_control = GST_VAAPI_RATECONTROL_NONE;
}

static void
gst_vaapi_encoder_finalize(GObject *object)
{
  GstVaapiEncoder* encoder = GST_VAAPI_ENCODER(object);
  GstVaapiEncoderPrivate *priv = encoder->priv;
  if (VAAPI_ENC_NULL != priv->state) {
    gst_vaapi_encoder_uninitialize(encoder);
  }

  if (priv->context) {
    g_object_unref(priv->context);
    priv->context = NULL;
    priv->va_context = VA_INVALID_ID;
  }

  if (priv->display) {
    g_object_unref(priv->display);
    priv->display = NULL;
    priv->va_display = NULL;
  }

  G_OBJECT_CLASS (gst_vaapi_encoder_parent_class)->finalize (object);
}

static void
gst_vaapi_encoder_class_init(GstVaapiEncoderClass *kclass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(kclass);
  g_type_class_add_private(kclass, sizeof(GstVaapiEncoderPrivate));


  GST_DEBUG_CATEGORY_INIT (gst_vaapi_encoder_debug,
                           "gst_va_encoder",
                           0,
                           "gst_va_encoder element");

  object_class->finalize = gst_vaapi_encoder_finalize;
}
