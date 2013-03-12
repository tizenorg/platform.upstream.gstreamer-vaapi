/*
 *  gstvaapiencoder_h263.h - H.263 encoder
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

#ifndef GST_VAAPI_ENCODER_H263_H
#define GST_VAAPI_ENCODER_H263_H

#include "gst/vaapi/gstvaapisurfacepool.h"
#include "gst/vaapi/gstvaapibaseencoder.h"

G_BEGIN_DECLS

#define H263_DEFAULT_INTRA_PERIOD 30
#define H263_DEFAULT_INIT_QP      15
#define H263_DEFAULT_MIN_QP       1


typedef struct _GstVaapiEncoderH263              GstVaapiEncoderH263;
typedef struct _GstVaapiEncoderH263Private       GstVaapiEncoderH263Private;
typedef struct _GstVaapiEncoderH263Class         GstVaapiEncoderH263Class;


#define GST_TYPE_VAAPI_ENCODER_H263 \
    (gst_vaapi_encoder_h263_get_type())

#define GST_IS_VAAPI_ENCODER_H263(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODER_H263))

#define GST_IS_VAAPI_ENCODER_H263_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODER_H263))

#define GST_VAAPI_ENCODER_H263_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                         \
                                GST_TYPE_VAAPI_ENCODER_H263,   \
                                GstVaapiEncoderH263Class))

#define GST_VAAPI_ENCODER_H263(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_H263,  \
                                 GstVaapiEncoderH263))

#define GST_VAAPI_ENCODER_H263_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                         \
                              GST_TYPE_VAAPI_ENCODER_H263,     \
                              GstVaapiEncoderH263Class))

#define GST_VAAPI_ENCODER_H263_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_H263,  \
                                 GstVaapiEncoderH263Private))

struct _GstVaapiEncoderH263 {
  GstVaapiBaseEncoder parent;   /*based on gobject*/

  guint32  bitrate;  /*kbps*/
  guint32  intra_period;
  guint32  init_qp;  /*default 15, 1~31*/
  guint32  min_qp;   /*default 1, 1~31*/

  GstVaapiEncoderH263Private *priv;
};

struct _GstVaapiEncoderH263Class {
    GstVaapiBaseEncoderClass parent_class;
};


GType
gst_vaapi_encoder_h263_get_type(void);

GstVaapiEncoderH263 *
gst_vaapi_encoder_h263_new(void);

static inline void
gst_vaapi_encoder_h263_unref (GstVaapiEncoderH263 * encoder)
{
  g_object_unref (encoder);
}


G_END_DECLS

#endif /* GST_VAAPI_ENCODER_H263_H */
