/*
 *  gstvaapibaseencoder.h - VA-API base encoder
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

#ifndef GST_VAAPI_BASE_ENCODER_H
#define GST_VAAPI_BASE_ENCODER_H

#include "gst/vaapi/gstvaapiencoder.h"
#include "gst/vaapi/gstvaapivideobuffer.h"

G_BEGIN_DECLS

typedef struct _GstVaapiBaseEncoder              GstVaapiBaseEncoder;
typedef struct _GstVaapiBaseEncoderPrivate       GstVaapiBaseEncoderPrivate;
typedef struct _GstVaapiBaseEncoderClass         GstVaapiBaseEncoderClass;

#define GST_TYPE_VAAPI_BASE_ENCODER \
    (gst_vaapi_base_encoder_get_type())

#define GST_IS_VAAPI_BASE_ENCODER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_BASE_ENCODER))

#define GST_IS_VAAPI_BASE_ENCODER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_BASE_ENCODER))

#define GST_VAAPI_BASE_ENCODER_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                         \
                                GST_TYPE_VAAPI_BASE_ENCODER,   \
                                GstVaapiBaseEncoderClass))

#define GST_VAAPI_BASE_ENCODER(obj)                            \
(G_TYPE_CHECK_INSTANCE_CAST ((obj),                            \
                             GST_TYPE_VAAPI_BASE_ENCODER,      \
                             GstVaapiBaseEncoder))

#define GST_VAAPI_BASE_ENCODER_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                         \
                              GST_TYPE_VAAPI_BASE_ENCODER,     \
                              GstVaapiBaseEncoderClass))

#define GST_VAAPI_BASE_ENCODER_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                        \
                                 GST_TYPE_VAAPI_BASE_ENCODER,  \
                                 GstVaapiBaseEncoderPrivate))

/* return: if true, continue; else, stop*/
typedef gboolean
  (*GstVaapiBaseEncoderNotifyStatus)(
       GstVaapiBaseEncoder* encoder,
       EncoderStatus status,
       void* userdata);

struct _GstVaapiBaseEncoder {
  GstVaapiEncoder parent;

  GstVaapiBaseEncoderPrivate *priv;
};

struct _GstVaapiBaseEncoderClass {
  GstVaapiEncoderClass parent_class;

  /* in <open> function*/
  gboolean (*validate_attributes)   (GstVaapiBaseEncoder* encoder);
  gboolean (*pre_alloc_resource)    (GstVaapiBaseEncoder *encoder,
                                     GstVaapiContext* context);

  /* in <close> function */
  gboolean (*release_resource)      (GstVaapiBaseEncoder* encoder);

  /* in <encode> function */
  EncoderStatus (*prepare_next_input_buffer)(GstVaapiBaseEncoder* encoder,
                                             GstBuffer *display_buf,
                                             gboolean need_flush,
                                             GstBuffer **out_buf);

  EncoderStatus (*render_frame)     (GstVaapiBaseEncoder *encoder,
                                     GstVaapiSurface *surface,
                                     guint frame_index,
                                     VABufferID coded_buf,
                                     gboolean *is_key);

  void (*encode_frame_failed)       (GstVaapiBaseEncoder *encoder,
                                     GstVaapiVideoBuffer* buffer);

  void (*notify_buffer)             (GstVaapiBaseEncoder *encoder,
                                     guint8 *buf, guint32 size);

  GstBuffer *(*wrap_buffer)         (GstVaapiBaseEncoder *encoder,
                                     GstBuffer *buf);
};

GType
gst_vaapi_base_encoder_get_type(void);

void
gst_vaapi_base_encoder_set_frame_notify(
    GstVaapiBaseEncoder *encoder,
    gboolean flag
);

gboolean
gst_vaapi_base_encoder_set_va_profile(
    GstVaapiBaseEncoder *encoder,
    guint profile
);
void
gst_vaapi_base_encoder_set_input_format(
    GstVaapiBaseEncoder* encoder,
    guint32 format
);

gboolean
gst_vaapi_base_encoder_set_notify_status(
    GstVaapiBaseEncoder *encoder,
    GstVaapiBaseEncoderNotifyStatus func,
    gpointer             user_data
);

void
gst_vaapi_base_encoder_set_buffer_sharing(
    GstVaapiBaseEncoder *encoder,
    gboolean is_buffer_sharing
);

G_END_DECLS

#endif /* GST_VAAPI_BASE_ENCODER_H */
