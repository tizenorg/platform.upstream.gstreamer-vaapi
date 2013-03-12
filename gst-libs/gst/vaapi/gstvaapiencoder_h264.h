/*
 *  gstvaapiencoder_h264.h -  H.264 encoder
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

#ifndef GST_VAAPI_ENCODER_H264_H
#define GST_VAAPI_ENCODER_H264_H

#include "gst/vaapi/gstvaapisurfacepool.h"
#include "gst/vaapi/gstvaapibaseencoder.h"

G_BEGIN_DECLS

typedef struct _GstVaapiEncoderH264              GstVaapiEncoderH264;
typedef struct _GstVaapiEncoderH264Private       GstVaapiEncoderH264Private;
typedef struct _GstVaapiEncoderH264Class         GstVaapiEncoderH264Class;

#define GST_TYPE_VAAPI_ENCODER_H264 \
    (gst_vaapi_encoder_h264_get_type())

#define GST_IS_VAAPI_ENCODER_H264(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODER_H264))

#define GST_IS_VAAPI_ENCODER_H264_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODER_H264))

#define GST_VAAPI_ENCODER_H264_GET_CLASS(obj)                  \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                         \
                                GST_TYPE_VAAPI_ENCODER_H264,   \
                                GstVaapiEncoderH264Class))

#define GST_VAAPI_ENCODER_H264(obj)                            \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_H264,  \
                                 GstVaapiEncoderH264))

#define GST_VAAPI_ENCODER_H264_CLASS(klass)                    \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                         \
                              GST_TYPE_VAAPI_ENCODER_H264,     \
                              GstVaapiEncoderH264Class))

#define GST_VAAPI_ENCODER_H264_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                        \
                                 GST_TYPE_VAAPI_ENCODER_H264,  \
                                 GstVaapiEncoderH264Private))

typedef enum {
  H264_PROFILE_BASELINE = 66,
  H264_PROFILE_MAIN     = 77,
  H264_PROFILE_EXTENDED = 88,
  H264_PROFILE_HIGH    = 100,
  H264_PROFILE_HIGH10  = 110,
  H264_PROFILE_HIGH422 = 122,
  H264_PROFILE_HIGH444 = 144,
  H264_PROFILE_HIGH444_PREDICTIVE = 244,
} H264_Profile;

typedef enum {
  H264_LEVEL_10 = 10,  /* QCIF format, < 380160 samples/sec */
  H264_LEVEL_11 = 11,  /* CIF format,   < 768000 samples/sec */
  H264_LEVEL_12 = 12,  /* CIF format,   < 1536000  samples/sec */
  H264_LEVEL_13 = 13,  /* CIF format,   < 3041280  samples/sec */
  H264_LEVEL_20 = 20,  /* CIF format,   < 3041280  samples/sec */
  H264_LEVEL_21 = 21,  /* HHR format,  < 5068800  samples/sec */
  H264_LEVEL_22 = 22,  /* SD/4CIF format,     < 5184000      samples/sec */
  H264_LEVEL_30 = 30,  /* SD/4CIF format,     < 10368000    samples/sec */
  H264_LEVEL_31 = 31,  /* 720pHD format,      < 27648000    samples/sec */
  H264_LEVEL_32 = 32,  /* SXGA  format,         < 55296000    samples/sec */
  H264_LEVEL_40 = 40,  /* 2Kx1K format,         < 62914560    samples/sec */
  H264_LEVEL_41 = 41,  /* 2Kx1K format,         < 62914560    samples/sec */
  H264_LEVEL_42 = 42,  /* 2Kx1K format,         < 125829120  samples/sec */
  H264_LEVEL_50 = 50,  /* 3672x1536 format,  < 150994944  samples/sec */
  H264_LEVEL_51 = 51,  /* 4096x2304 format,  < 251658240  samples/sec */
} H264_Level;

#define H264_DEFAULT_PROFILE      H264_PROFILE_BASELINE
#define H264_DEFAULT_LEVEL        H264_LEVEL_30
#define H264_DEFAULT_INIT_QP      26
#define H264_DEFAULT_MIN_QP       1
#define H264_DEFAULT_INTRA_PERIOD 30
#define H264_DEFAULT_FPS          30
#define H264_DEFAULT_SLICE_NUM    1

struct _GstVaapiEncoderH264 {
  GstVaapiBaseEncoder parent;   /*based on gobject*/

  GstVaapiEncoderH264Private *priv;

  guint32         profile;
  guint32         level;
  guint32         bitrate;  /*kbps*/
  guint32         intra_period;
  guint32         init_qp;  /*default 24*/
  guint32         min_qp;   /*default 1*/
  guint32         slice_num;
  guint32         b_frame_num;
};

struct _GstVaapiEncoderH264Class {
    GstVaapiBaseEncoderClass parent_class;
};


GType
gst_vaapi_encoder_h264_get_type(void);

GstVaapiEncoderH264 *
gst_vaapi_encoder_h264_new(void);

static inline void
gst_vaapi_encoder_h264_unref (GstVaapiEncoderH264 * encoder)
{
  g_object_unref (encoder);
}

void
gst_vaapi_encoder_h264_set_avc_flag(
    GstVaapiEncoderH264* encoder,
    gboolean avc
);

gboolean
gst_vaapi_encoder_h264_get_avc_flag(GstVaapiEncoderH264* encoder);

G_END_DECLS

#endif /*GST_VAAPI_ENCODER_H264_H */
