/*
 *  gstvaapiencoder_mpeg4.h - MPEG-4 encoder
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

#ifndef GST_VAAPI_ENCODER_MPEG4_H
#define GST_VAAPI_ENCODER_MPEG4_H

#include "gst/vaapi/gstvaapisurfacepool.h"
#include "gst/vaapi/gstvaapibaseencoder.h"

G_BEGIN_DECLS

#define MPEG4_DEFAULT_INTRA_PERIOD 30
#define MPEG4_DEFAULT_INIT_QP      15
#define MPEG4_DEFAULT_MIN_QP       1
#define MPEG4_DEFAULT_SIMPLE_PROFILE_AND_LEVEL          0x03
#define MPEG4_DEFAULT_ADVANCED_SIMPLE_PROFILE_AND_LEVEL 0xF3

#define MPEG4_DEFAULT_FIXED_VOP_RATE FALSE


typedef struct _GstVaapiEncoderMpeg4              GstVaapiEncoderMpeg4;
typedef struct _GstVaapiEncoderMpeg4Private       GstVaapiEncoderMpeg4Private;
typedef struct _GstVaapiEncoderMpeg4Class         GstVaapiEncoderMpeg4Class;


#define GST_TYPE_VAAPI_ENCODER_MPEG4 \
    (gst_vaapi_encoder_mpeg4_get_type())

#define GST_IS_VAAPI_ENCODER_MPEG4(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODER_MPEG4))

#define GST_IS_VAAPI_ENCODER_MPEG4_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODER_MPEG4))

#define GST_VAAPI_ENCODER_MPEG4_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                         \
                                GST_TYPE_VAAPI_ENCODER_MPEG4,  \
                                GstVaapiEncoderMpeg4Class))

#define GST_VAAPI_ENCODER_MPEG4(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_MPEG4, \
                                 GstVaapiEncoderMpeg4))

#define GST_VAAPI_ENCODER_MPEG4_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                         \
                              GST_TYPE_VAAPI_ENCODER_MPEG4,    \
                              GstVaapiEncoderMpeg4Class))

#define GST_VAAPI_ENCODER_MPEG4_GET_PRIVATE(obj)               \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_MPEG4, \
                                 GstVaapiEncoderMpeg4Private))

struct _GstVaapiEncoderMpeg4 {
  GstVaapiBaseEncoder parent;   /*based on gobject*/
  VAProfile profile;  /* VAProfileMPEG4Simple, VAProfileMPEG4AdvancedSimple */
  guint32   bitrate;
  guint32   intra_period;
  guint32   init_qp;  /*default 15, 1~31*/
  guint32   min_qp;   /*default 1, 1~31*/

  GstVaapiEncoderMpeg4Private *priv;
};

struct _GstVaapiEncoderMpeg4Class {
    GstVaapiBaseEncoderClass parent_class;
};

GType
gst_vaapi_encoder_mpeg4_get_type(void);

GstVaapiEncoderMpeg4 *
gst_vaapi_encoder_mpeg4_new(void);

static inline void
gst_vaapi_encoder_mpeg4_unref (GstVaapiEncoderMpeg4 * encoder)
{
  g_object_unref (encoder);
}


G_END_DECLS

#endif /* GST_VAAPI_ENCODER_MPEG4_H */
