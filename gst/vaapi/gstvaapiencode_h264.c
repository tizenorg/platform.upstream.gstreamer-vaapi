/*
 *  gstvaapiencode_h264.c - VA-API H.264 encoder
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

#include "gstvaapiencode_h264.h"
#include "gst/vaapi/gstvaapiencoder_h264.h"
#include "gst/vaapi/gstvaapivalue.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h264_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_h264_encode_debug

static const char gst_vaapi_encode_h264_sink_caps_str[] =
    GST_VAAPI_SURFACE_CAPS "; "
    GST_VAAPI_BUFFER_SHARING_CAPS;

static const GstElementDetails gst_vaapi_encode_h264_details =
    GST_ELEMENT_DETAILS(
        "VA-API h264 encoder",
        "Codec/Encoder/Video",
        "A VA-API based h264 encoder",
        "Feng Yuan<feng.yuan@intel.com>");


static const char gst_vaapi_encode_h264_src_caps_str[] =
    GST_CAPS_CODEC("video/x-h264");

static GstStaticPadTemplate gst_vaapi_encode_h264_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_h264_sink_caps_str));

static GstStaticPadTemplate gst_vaapi_encode_h264_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapi_encode_h264_src_caps_str));

/* h264 encode */
GST_BOILERPLATE(
    GstVaapiEncodeH264,
    gst_vaapi_encode_h264,
    GstVaapiEncode,
    GST_TYPE_VAAPI_ENCODE)

enum {
    H264_PROP_0,
    H264_PROP_PROFILE,
    H264_PROP_LEVEL,
    H264_PROP_RATE_CONTROL,
    H264_PROP_BITRATE,
    H264_PROP_INTRA_PERIOD,
    H264_PROP_INIT_QP,
    H264_PROP_MIN_QP,
    H264_PROP_SLICE_NUM,
    H264_PROP_B_FRAME_NUM,
};

static void
gst_vaapi_encode_h264_init(
    GstVaapiEncodeH264 *h264_encode,
    GstVaapiEncodeH264Class *klass
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(h264_encode);
  encode->encoder = GST_VAAPI_ENCODER(gst_vaapi_encoder_h264_new());
  ENCODER_ASSERT(encode->encoder);
}

static void
gst_vaapi_encode_h264_finalize(GObject *object)
{
  //GstVaapiEncodeH264 * const h264_encode = GST_VAAPI_ENCODE_H264(object);
  G_OBJECT_CLASS(parent_class)->finalize(object);
}


static inline gboolean
h264_check_valid_profile(guint profile)
{
   static const guint limit_profiles[] = {
         H264_PROFILE_BASELINE,
         H264_PROFILE_MAIN,
         H264_PROFILE_HIGH
        };
   guint n_profiles = sizeof(limit_profiles)/sizeof(limit_profiles[0]);
   guint i;
   for (i = 0; i < n_profiles; ++i) {
     if (limit_profiles[i] == profile)
       return TRUE;
   }
   return FALSE;
}

static inline gboolean
h264_check_valid_level(guint level)
{
  static const guint limit_levels[] = {
        H264_LEVEL_10,
        H264_LEVEL_11,
        H264_LEVEL_12,
        H264_LEVEL_13,
        H264_LEVEL_20,
        H264_LEVEL_21,
        H264_LEVEL_22,
        H264_LEVEL_30,
        H264_LEVEL_31,
        H264_LEVEL_32,
        H264_LEVEL_40,
        H264_LEVEL_41,
        H264_LEVEL_42,
        H264_LEVEL_50,
        H264_LEVEL_51
       };
  guint n_levels = sizeof(limit_levels)/sizeof(limit_levels[0]);
  guint i;
  for (i = 0; i < n_levels; ++i) {
    if (limit_levels[i] == level)
      return TRUE;
  }
  return FALSE;

}


static void
gst_vaapi_encode_h264_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderH264 *h264encoder = GST_VAAPI_ENCODER_H264(encode->encoder);

  ENCODER_ASSERT(h264encoder);

  switch (prop_id) {
    case H264_PROP_PROFILE: {
      guint profile = g_value_get_uint(value);
      if (h264_check_valid_profile(profile)) {
        h264encoder->profile = profile;
      } else {
        ENCODER_LOG_ERROR("h264encode set property <profile> failed.");
      }
    }
      break;

    case H264_PROP_LEVEL: {
      guint level = g_value_get_uint(value);
      if (h264_check_valid_level(level)) {
        h264encoder->level= level;
      } else {
        ENCODER_LOG_ERROR("h264encode set property <level> failed.");
      }
    }
      break;

    case H264_PROP_RATE_CONTROL: {
      ENCODER_RATE_CONTROL(h264encoder) = g_value_get_enum(value);
    }
      break;

    case H264_PROP_BITRATE: {
      h264encoder->bitrate = g_value_get_uint(value);
    }
      break;

    case H264_PROP_INTRA_PERIOD: {
      h264encoder->intra_period = g_value_get_uint(value);
    }
      break;

    case H264_PROP_INIT_QP: {
      h264encoder->init_qp = g_value_get_uint(value);
    }
      break;

    case H264_PROP_MIN_QP: {
      h264encoder->min_qp = g_value_get_uint(value);
    }
      break;

    case H264_PROP_SLICE_NUM: {
      h264encoder->slice_num= g_value_get_uint(value);
    }
      break;

    case H264_PROP_B_FRAME_NUM: {
      h264encoder->b_frame_num= g_value_get_uint(value);
    }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapi_encode_h264_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  GstVaapiEncoderH264 *h264encoder = GST_VAAPI_ENCODER_H264(encode->encoder);
  ENCODER_ASSERT(h264encoder);

  switch (prop_id) {
    case H264_PROP_PROFILE:
      g_value_set_uint (value, h264encoder->profile);
      break;

    case H264_PROP_LEVEL:
      g_value_set_uint (value, h264encoder->level);
      break;

    case H264_PROP_RATE_CONTROL:
      g_value_set_enum(value, ENCODER_RATE_CONTROL(h264encoder));
      break;

    case H264_PROP_BITRATE:
      g_value_set_uint (value, h264encoder->bitrate);
      break;

    case H264_PROP_INTRA_PERIOD:
      g_value_set_uint (value, h264encoder->intra_period);
      break;

    case H264_PROP_INIT_QP:
      g_value_set_uint (value, h264encoder->init_qp);
      break;

    case H264_PROP_MIN_QP:
      g_value_set_uint (value, h264encoder->min_qp);
      break;

    case H264_PROP_SLICE_NUM:
      g_value_set_uint (value, h264encoder->slice_num);
      break;

    case H264_PROP_B_FRAME_NUM:
      g_value_set_uint (value, h264encoder->b_frame_num);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vaapi_encode_h264_set_src_caps(
    GstVaapiEncode* encode,
    GstCaps *caps
)
{
  GstVaapiEncoderH264 *h264encoder = GST_VAAPI_ENCODER_H264(encode->encoder);
  GstCaps *peer_caps, *allowed_caps;
  GstStructure *s;
  const gchar *stream_format;

  g_return_val_if_fail(caps,FALSE);
  peer_caps = gst_pad_peer_get_caps_reffed(encode->srcpad);
  if (peer_caps) {
    allowed_caps = gst_caps_intersect(peer_caps, caps);
    if (allowed_caps) {
      allowed_caps = gst_caps_make_writable(allowed_caps);
      gst_pad_fixate_caps(encode->srcpad, caps);
      s = gst_caps_get_structure (allowed_caps, 0);
      stream_format = gst_structure_get_string (s, "stream-format");
      if (stream_format) {
        if (!strcmp (stream_format, "avc")) {
            gst_vaapi_encoder_h264_set_avc_flag(h264encoder, TRUE);
        } else if (!strcmp (stream_format, "byte-stream")) {
            gst_vaapi_encoder_h264_set_avc_flag(h264encoder, FALSE);
        }
      }
      gst_caps_unref(allowed_caps);
    }
    gst_caps_unref(peer_caps);
  }
  gst_caps_set_simple(caps, "stream-format",
                            G_TYPE_STRING,
                            (gst_vaapi_encoder_h264_get_avc_flag(h264encoder) ? "avc" : "byte-stream"),
                            "alignment", G_TYPE_STRING, "au",
                            NULL);
  return TRUE;
}

static void
gst_vaapi_encode_h264_base_init(gpointer klass)
{
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details(element_class, &gst_vaapi_encode_h264_details);

  /* sink pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_h264_sink_factory)
  );

  /* src pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_h264_src_factory)
  );
}

static void
gst_vaapi_encode_h264_class_init(GstVaapiEncodeH264Class *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPI_ENCODE_CLASS(klass);
  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h264_encode_debug,
                           "vaapih264encode",
                           0,
                           "vaapih264encode element");

  object_class->finalize      = gst_vaapi_encode_h264_finalize;
  object_class->set_property  = gst_vaapi_encode_h264_set_property;
  object_class->get_property  = gst_vaapi_encode_h264_get_property;

  encode_class->set_encoder_src_caps = gst_vaapi_encode_h264_set_src_caps;

  g_object_class_install_property (
      object_class,
      H264_PROP_PROFILE,
      g_param_spec_uint (
          "profile",
          "H264 Profile",
          "Profile supports: 66(Baseline), 77(Main), 100(High)",
          H264_PROFILE_BASELINE,
          H264_PROFILE_HIGH10,
          H264_DEFAULT_PROFILE,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_LEVEL,
      g_param_spec_uint (
          "level",
          "H264 level idc",
          "Level idc supports: 10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41",
          H264_LEVEL_10,
          H264_LEVEL_41,
          H264_DEFAULT_LEVEL,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "rate-control",
          "Rate control mode",
          GST_VAAPI_TYPE_RATE_CONTROL,
          GST_VAAPI_RATECONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (
      object_class,
      H264_PROP_BITRATE,
      g_param_spec_uint (
          "bitrate",
          "H264 encoding bitrate(kbps)",
          "H264 encoding bitrate(kbps), (0, auto-calculate)",
          0,
          100*1024,
          0,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_INTRA_PERIOD,
      g_param_spec_uint (
          "intra-period",
          "H264 encoding intra-period",
          "H264 encoding intra-period",
          1,
          300,
          H264_DEFAULT_INTRA_PERIOD,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_INIT_QP,
      g_param_spec_uint (
          "init-qp",
          "H264 init-qp",
          "H264 init-qp",
          1,
          51,
          H264_DEFAULT_INIT_QP,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_MIN_QP,
      g_param_spec_uint (
          "min-qp",
          "H264 min-qp",
          "H264 min-qp",
          1,
          51,
          H264_DEFAULT_MIN_QP,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_SLICE_NUM,
      g_param_spec_uint (
          "slice-num",
          "H264 slice num",
          "H264 slice num",
          1,
          200,
          1,
          G_PARAM_READWRITE));

  g_object_class_install_property (
      object_class,
      H264_PROP_B_FRAME_NUM,
      g_param_spec_uint (
          "b-frame-num",
          "B frams num",
          "B frams num",
          0,
          10,
          0,
          G_PARAM_READWRITE));
}
