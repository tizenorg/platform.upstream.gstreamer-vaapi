/*
 *  gstvaapiencoder.h - VA-API encoder interface
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

#ifndef GST_VAAPI_ENCODER_H
#define GST_VAAPI_ENCODER_H

#include <stdio.h>
#include <stdint.h>

#include "gst/gstinfo.h"
#include "gst/gstbuffer.h"

#include "gst/vaapi/gstvaapidisplay.h"
#include "gst/vaapi/gstvaapicontext.h"

G_BEGIN_DECLS

#define ENCODER_NO_ERROR       0
#define ENCODER_NO_DATA        1
#define ENCODER_NO_BUSY_BUF    2
#define ENCODER_NO_IDLE_BUF    3
#define ENCODER_FRAME_IN_ORDER 4

#define ENCODER_MEM_ERR       -1
#define ENCODER_DISPLAY_ERR   -2
#define ENCODER_CONFIG_ERR    -3
#define ENCODER_CONTEXT_ERR    -3
#define ENCODER_STATE_ERR     -4
#define ENCODER_ENC_RES_ERR   -5
#define ENCODER_PICTURE_ERR   -6
#define ENCODER_SURFACE_ERR   -7
#define ENCODER_QUERY_STATUS_ERR -8
#define ENCODER_DATA_NOT_READY   -9
#define ENCODER_DATA_ERR      -10
#define ENCODER_PROFILE_ERR   -11
#define ENCODER_PARAMETER_ERR -12
#define ENCODER_FUNC_PTR_ERR  -13

#ifdef DEBUG
  #define ENCODER_LOG_ERROR(str_fmt,...)   \
      fprintf(stdout, str_fmt "\n", ## __VA_ARGS__)
  #define ENCODER_LOG_WARNING(str_fmt,...) \
      fprintf(stdout, str_fmt "\n", ## __VA_ARGS__)
  #define ENCODER_LOG_DEBUG(str_fmt,...)   \
      fprintf(stdout, str_fmt "\n", ## __VA_ARGS__)
  #define ENCODER_LOG_INFO(str_fmt,...)    \
      fprintf(stdout, str_fmt "\n", ## __VA_ARGS__)
#else
  #define ENCODER_LOG_ERROR(...)   GST_ERROR( __VA_ARGS__)
  #define ENCODER_LOG_WARNING(...) GST_WARNING( __VA_ARGS__)
  #define ENCODER_LOG_DEBUG(...)   GST_DEBUG( __VA_ARGS__)
  #define ENCODER_LOG_INFO(...)    GST_INFO( __VA_ARGS__)
#endif

#define VAAPI_UNUSED_ARG(arg) (void)(arg)

#ifdef DEBUG
#include <assert.h>
#define ENCODER_ASSERT(exp) assert(exp)
#else
#define ENCODER_ASSERT(exp) g_assert(exp)
#endif

#define ENCODER_CHECK_STATUS(exp, err_num, err_reason, ...)  \
  if (!(exp)) {                                   \
    ENCODER_ASSERT(FALSE);                         \
    ret = err_num;                                 \
    ENCODER_LOG_ERROR(err_reason, ## __VA_ARGS__); \
    goto end;                                      \
  }

/* must have <gboolean is_locked = FALSE;> declared first*/
#define ENCODER_ACQUIRE_DISPLAY_LOCK(display)    \
     if (!is_locked) {                     \
      GST_VAAPI_DISPLAY_LOCK(display);     \
      is_locked = TRUE;                    \
     }

#define ENCODER_RELEASE_DISPLAY_LOCK(display)    \
    if (is_locked) {                       \
     GST_VAAPI_DISPLAY_UNLOCK(display);    \
     is_locked = FALSE;                    \
    }


typedef enum {
  VAAPI_ENC_NULL,
  VAAPI_ENC_INIT,
  VAAPI_ENC_OPENED,
  VAAPI_ENC_ENCODING,
} VAAPI_Encode_State;

typedef int                                  EncoderStatus;
typedef struct _GstVaapiEncoder              GstVaapiEncoder;
typedef struct _GstVaapiEncoderPrivate       GstVaapiEncoderPrivate;
typedef struct _GstVaapiEncoderClass         GstVaapiEncoderClass;

#define GST_TYPE_VAAPI_ENCODER \
    (gst_vaapi_encoder_get_type())

#define GST_IS_VAAPI_ENCODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODER))

#define GST_IS_VAAPI_ENCODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODER))

#define GST_VAAPI_ENCODER_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                   \
                                GST_TYPE_VAAPI_ENCODER,  \
                                GstVaapiEncoderClass))

#define GST_VAAPI_ENCODER(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                  \
                                 GST_TYPE_VAAPI_ENCODER, \
                                 GstVaapiEncoder))

#define GST_VAAPI_ENCODER_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                   \
                              GST_TYPE_VAAPI_ENCODER,    \
                              GstVaapiEncoderClass))

#define GST_VAAPI_ENCODER_GET_PRIVATE(obj)               \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                  \
                                 GST_TYPE_VAAPI_ENCODER, \
                                 GstVaapiEncoderPrivate))

#define GST_VAAPI_ENCODER_CAST(encoder) \
    ((GstVaapiEncoder *)(encoder))

/* Get GstVaapiDisplay* */
#define ENCODER_DISPLAY(encoder) \
    (((GstVaapiEncoder*)(encoder))->priv->display)

/* Get VADisplay */
#define ENCODER_VA_DISPLAY(encoder) \
    (((GstVaapiEncoder*)(encoder))->priv->va_display)

/* Get GstVaapiContext* */
#define ENCODER_CONTEXT(encoder) \
    (((GstVaapiEncoder*)(encoder))->priv->context)

/* Get VAContext */
#define ENCODER_VA_CONTEXT(encoder) \
    (((GstVaapiEncoder*)(encoder))->priv->va_context)

#define ENCODER_WIDTH(encoder)   (((GstVaapiEncoder*)(encoder))->width)
#define ENCODER_HEIGHT(encoder)  (((GstVaapiEncoder*)(encoder))->height)
#define ENCODER_FPS(encoder)     (((GstVaapiEncoder*)(encoder))->frame_rate)
#define ENCODER_RATE_CONTROL(encoder)   \
    (((GstVaapiEncoder*)(encoder))->rate_control)

struct _GstVaapiEncoder {
  GObject parent;
  GstVaapiEncoderPrivate *priv;

  guint32 width;
  guint32 height;
  guint32 frame_rate;
  GstVaapiRateControl rate_control;
};

struct _GstVaapiEncoderClass {
  GObjectClass parent_class;

  EncoderStatus (*initialize)    (GstVaapiEncoder* encoder);  /* can be NULL */
  EncoderStatus (*uninitialize)  (GstVaapiEncoder* encoder);  /* can be NULL */

  /* context [out] */
  EncoderStatus (*open)          (GstVaapiEncoder* encoder,
                                  GstVaapiContext **context);

  EncoderStatus (*close)         (GstVaapiEncoder* encoder);
  EncoderStatus (*encode)        (GstVaapiEncoder* encoder,
                                  GstBuffer *pic);
  /* buf [out] */
  EncoderStatus (*get_buf)       (GstVaapiEncoder* encoder,
                                  GstBuffer **buf);
  EncoderStatus (*flush)         (GstVaapiEncoder* encoder);

  /* get_codec_data can be NULL */
  EncoderStatus (*get_codec_data)(GstVaapiEncoder* encoder, GstBuffer **codec_data);
};

struct _GstVaapiEncoderPrivate {
  GstVaapiDisplay     *display;
  VADisplay            va_display;
  GstVaapiContext     *context;
  VAContextID          va_context;
  VAAPI_Encode_State   state;
};

GType
gst_vaapi_encoder_get_type(void);

/* set/get display */
gboolean
gst_vaapi_encoder_set_display(
    GstVaapiEncoder* encoder,
    GstVaapiDisplay *display
);
GstVaapiDisplay *
gst_vaapi_encoder_get_display(GstVaapiEncoder* encoder);

/* get context */
GstVaapiContext *
gst_vaapi_encoder_get_context(GstVaapiEncoder* encoder);

/* get encoding state */
VAAPI_Encode_State
gst_vaapi_encoder_get_state(GstVaapiEncoder* encoder);

/* check/open display */
EncoderStatus
gst_vaapi_encoder_initialize(GstVaapiEncoder* encoder);

/* check/open context */
EncoderStatus
gst_vaapi_encoder_open(GstVaapiEncoder* encoder);

/* encode one frame */
EncoderStatus
gst_vaapi_encoder_encode(
    GstVaapiEncoder* encoder,
    GstBuffer *pic
);

EncoderStatus
gst_vaapi_encoder_get_encoded_buffer(
    GstVaapiEncoder* encoder,
    GstBuffer **buf
);

EncoderStatus
gst_vaapi_encoder_get_codec_data(
    GstVaapiEncoder* encoder,
    GstBuffer **codec_data
);

/* flush all frames */
EncoderStatus
gst_vaapi_encoder_flush(GstVaapiEncoder* encoder);

/* close context */
EncoderStatus
gst_vaapi_encoder_close(GstVaapiEncoder* encoder);

EncoderStatus
gst_vaapi_encoder_render_picture(
    GstVaapiEncoder *encoder,
    GstVaapiSurface *buffer_surface,
    VABufferID *bufs,
    guint num
);

/* close display */
EncoderStatus
gst_vaapi_encoder_uninitialize(GstVaapiEncoder* encoder);

static inline void
gst_vaapi_encoder_unref (GstVaapiEncoder *encoder)
{
  g_object_unref (encoder);
}

/* other functions */
char *vaapi_encoder_dump_bytes(const guint8 *buf, guint32 num);

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H */
