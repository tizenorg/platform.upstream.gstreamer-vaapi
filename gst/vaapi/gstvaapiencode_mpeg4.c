/*
 *  gstvaapiencode_mpeg4.c - VA-API MPEG-4 encoder
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

#include "gstvaapiencode_mpeg4.h"
#include "gst/vaapi/gstvaapiencoder_mpeg4.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_encode_mpeg4_debug);
#define GST_CAT_DEFAULT gst_vaapi_encode_mpeg4_debug


static const char gst_vaapi_encode_mpeg4_sink_caps_str[] =
    GST_VAAPI_SURFACE_CAPS "; "
    GST_VAAPI_BUFFER_SHARING_CAPS;

static const GstElementDetails gst_vaapi_encode_mpeg4_details =
    GST_ELEMENT_DETAILS(
        "VA-API mpeg4 encoder",
        "Codec/Encoder/Video",
        "A VA-API based mpeg4 encoder",
        "Feng Yuan<feng.yuan@intel.com>");


static const char gst_vaapi_encode_mpeg4_src_caps_str[] =
    GST_CAPS_CODEC("video/mpeg, mpegversion=4");

static GstStaticPadTemplate gst_vaapi_encode_mpeg4_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_mpeg4_sink_caps_str));

static GstStaticPadTemplate gst_vaapi_encode_mpeg4_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_mpeg4_src_caps_str));


/* mpeg4 encode */
GST_BOILERPLATE(
    GstVaapiEncodeMpeg4,
    gst_vaapi_encode_mpeg4,
    GstVaapiEncode,
    GST_TYPE_VAAPI_ENCODE)

enum {
    MPEG4_PROP_0,
    MPEG4_PROP_PROFILE,
    MPEG4_PROP_BITRATE,
    MPEG4_PROP_INTRA_PERIOD,
    MPEG4_PROP_INIT_QP,
    MPEG4_PROP_MIN_QP,
};

static void
gst_vaapi_encode_mpeg4_init(
    GstVaapiEncodeMpeg4 *mpeg4_encode,
    GstVaapiEncodeMpeg4Class *klass)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(mpeg4_encode);
  encode->encoder = GST_VAAPI_ENCODER(gst_vaapi_encoder_mpeg4_new());
  ENCODER_ASSERT(encode->encoder);
}

static void
gst_vaapi_encode_mpeg4_finalize(GObject *object)
{
  //GstVaapiEncodeMpeg4 * const mpeg4_encode = GST_VAAPI_ENCODE_MPEG4(object);
  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapi_encode_mpeg4_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderMpeg4 *mpeg4encoder = GST_VAAPI_ENCODER_MPEG4(encode->encoder);

  ENCODER_ASSERT(mpeg4encoder);

  switch (prop_id) {
    case MPEG4_PROP_PROFILE: {
      mpeg4encoder->profile = g_value_get_uint(value);
    }
      break;

    case MPEG4_PROP_BITRATE: {
      mpeg4encoder->bitrate = g_value_get_uint(value);
    }
      break;

    case MPEG4_PROP_INTRA_PERIOD: {
      mpeg4encoder->intra_period = g_value_get_uint(value);
    }
      break;

    case MPEG4_PROP_INIT_QP: {
      mpeg4encoder->init_qp = g_value_get_uint(value);
    }
      break;

    case MPEG4_PROP_MIN_QP: {
      mpeg4encoder->min_qp = g_value_get_uint(value);
    }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encode_mpeg4_get_property(
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderMpeg4 *mpeg4encoder = GST_VAAPI_ENCODER_MPEG4(encode->encoder);
  ENCODER_ASSERT(mpeg4encoder);

  switch (prop_id) {
    case MPEG4_PROP_PROFILE:
      g_value_set_uint (value, mpeg4encoder->profile);
      break;

    case MPEG4_PROP_BITRATE:
      g_value_set_uint (value, mpeg4encoder->bitrate);
      break;

    case MPEG4_PROP_INTRA_PERIOD:
      g_value_set_uint (value, mpeg4encoder->intra_period);
      break;

    case MPEG4_PROP_INIT_QP:
      g_value_set_uint (value, mpeg4encoder->init_qp);
      break;

    case MPEG4_PROP_MIN_QP:
      g_value_set_uint (value, mpeg4encoder->min_qp);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encode_mpeg4_base_init(gpointer klass)
{
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details(element_class, &gst_vaapi_encode_mpeg4_details);

  /* sink pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_mpeg4_sink_factory)
  );

  /* src pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_mpeg4_src_factory)
  );
}

static void
gst_vaapi_encode_mpeg4_class_init(GstVaapiEncodeMpeg4Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_encode_mpeg4_debug,
                           "vaapimpeg4encode",
                           0,
                           "vaapimpeg4encode element");

  object_class->finalize      = gst_vaapi_encode_mpeg4_finalize;
  object_class->set_property  = gst_vaapi_encode_mpeg4_set_property;
  object_class->get_property  = gst_vaapi_encode_mpeg4_get_property;


  g_object_class_install_property (
      object_class,
      MPEG4_PROP_PROFILE,
      g_param_spec_uint (
          "profile",
          "MPEG4 Profile",
          "Profile supports: 2(Baseline), 3(ASP)",
          2,
          3,
          2,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      MPEG4_PROP_BITRATE,
      g_param_spec_uint (
          "bitrate",
          "MPEG4 encoding bitrate(kpbs)",
          "MPEG4 encoding bitrate(kpbs), (0, auto-calculate)",
          0,
          100*1024,
          0,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      MPEG4_PROP_INTRA_PERIOD,
      g_param_spec_uint (
          "intra-period",
          "MPEG4 encoding intra-period",
          "MPEG4 encoding intra-period",
          1,
          300,
          MPEG4_DEFAULT_INTRA_PERIOD,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      MPEG4_PROP_INIT_QP,
      g_param_spec_uint (
          "init-qp",
          "MPEG4 init-qp",
          "MPEG4 init-qp",
          1,
          51,
          MPEG4_DEFAULT_INIT_QP,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      MPEG4_PROP_MIN_QP,
      g_param_spec_uint (
          "min-qp",
          "MPEG4 min-qp",
          "MPEG4 min-qp",
          1,
          51,
          MPEG4_DEFAULT_MIN_QP,
          G_PARAM_READWRITE));
}
