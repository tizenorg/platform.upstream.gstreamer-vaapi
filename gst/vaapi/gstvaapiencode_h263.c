/*
 *  gstvaapiencode_h263.c - VA-API H.263 encoder
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

#include "gstvaapiencode_h263.h"
#include "gst/vaapi/gstvaapiencoder_h263.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h263_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_h263_encode_debug

#define GST_VAAPI_ENCODE_GET_PRIVATE(obj)                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj),                   \
                                  GST_TYPE_VAAPI_ENCODE,   \
                                  GstVaapiEncodePrivate))

static const char gst_vaapi_encode_h263_sink_caps_str[] =
    GST_VAAPI_SURFACE_CAPS "; "
    GST_VAAPI_BUFFER_SHARING_CAPS;

static const GstElementDetails gst_vaapi_encode_h263_details =
    GST_ELEMENT_DETAILS(
      "VA-API h263 encoder",
      "Codec/Encoder/Video",
      "A VA-API based h263 encoder",
      "Feng Yuan <feng.yuan@intel.com>");


static const char gst_vaapi_encode_h263_src_caps_str[] =
    GST_CAPS_CODEC("video/x-h263");

static GstStaticPadTemplate gst_vaapi_encode_h263_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_h263_sink_caps_str));

static GstStaticPadTemplate gst_vaapi_encode_h263_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_h263_src_caps_str));

/* h263 encode */
GST_BOILERPLATE(
    GstVaapiEncodeH263,
    gst_vaapi_encode_h263,
    GstVaapiEncode,
    GST_TYPE_VAAPI_ENCODE)

enum {
    H263_PROP_0,
    H263_PROP_BITRATE,
    H263_PROP_INTRA_PERIOD,
    H263_PROP_INIT_QP,
    H263_PROP_MIN_QP,
};

static void
gst_vaapi_encode_h263_init(
    GstVaapiEncodeH263 *h263_encode,
    GstVaapiEncodeH263Class *klass
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(h263_encode);
  encode->encoder = GST_VAAPI_ENCODER(gst_vaapi_encoder_h263_new());
  ENCODER_ASSERT(encode->encoder);
}

static void
gst_vaapi_encode_h263_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec
)
{
  GstVaapiEncode *base = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderH263 *encoder = GST_VAAPI_ENCODER_H263(base->encoder);

  ENCODER_ASSERT(encoder);

  switch (prop_id) {
    case H263_PROP_BITRATE: {
      encoder->bitrate = g_value_get_uint(value);
    }
      break;

    case H263_PROP_INTRA_PERIOD: {
      encoder->intra_period = g_value_get_uint(value);
    }
      break;

    case H263_PROP_INIT_QP: {
      encoder->init_qp = g_value_get_uint(value);
    }
      break;

    case H263_PROP_MIN_QP: {
      encoder->min_qp = g_value_get_uint(value);
    }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encode_h263_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
  GstVaapiEncode *base = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderH263 *encoder = GST_VAAPI_ENCODER_H263(base->encoder);
  ENCODER_ASSERT(encoder);

  switch (prop_id) {
    case H263_PROP_BITRATE:
      g_value_set_uint (value, encoder->bitrate);
      break;

    case H263_PROP_INTRA_PERIOD:
      g_value_set_uint (value, encoder->intra_period);
      break;

    case H263_PROP_INIT_QP:
      g_value_set_uint (value, encoder->init_qp);
      break;

    case H263_PROP_MIN_QP:
      g_value_set_uint (value, encoder->min_qp);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encode_h263_base_init(gpointer klass)
{
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details(element_class, &gst_vaapi_encode_h263_details);

  /* sink pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_h263_sink_factory)
  );

  /* src pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_h263_src_factory)
  );
}

static void
gst_vaapi_encode_h263_class_init(GstVaapiEncodeH263Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h263_encode_debug,
                           "vaapih263encode",
                           0,
                           "vaapih263encode element");

  /* object_class->finalize      = gst_vaapi_encode_h263_finalize; */
  object_class->set_property  = gst_vaapi_encode_h263_set_property;
  object_class->get_property  = gst_vaapi_encode_h263_get_property;

  g_object_class_install_property (
      object_class,
      H263_PROP_BITRATE,
      g_param_spec_uint (
          "bitrate",
          "H263 encoding bitrate(kpbs)",
          "H263 encoding bitrate(kbps), (0, auto-calculate)",
          0,
          100*1024,
          0,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H263_PROP_INTRA_PERIOD,
      g_param_spec_uint (
          "intra-period",
          "H263 encoding intra-period",
          "H263 encoding intra-period",
          1,
          300,
          H263_DEFAULT_INTRA_PERIOD,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H263_PROP_INIT_QP,
      g_param_spec_uint (
          "init-qp",
          "H263 init-qp",
          "H263 init-qp",
          1,
          51,
          H263_DEFAULT_INIT_QP,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H263_PROP_MIN_QP,
      g_param_spec_uint (
          "min-qp",
          "H263 min-qp",
          "H263 min-qp",
          1,
          51,
          H263_DEFAULT_MIN_QP,
          G_PARAM_READWRITE));

}
