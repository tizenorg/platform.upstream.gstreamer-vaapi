/*
 *  gstvaapiencode_h264.c - VA-API H.264 encoder
 *
 *  Copyright (C) 2012-2013 Intel Corporation
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

#include "gst/vaapi/sysdeps.h"
#include "gst/vaapi/gstvaapicompat.h"

#include "gstvaapiencode_h264.h"
#include "gstvaapivideomemory.h"
#include "gst/vaapi/gstvaapiencoder_h264.h"
#include "gst/vaapi/gstvaapiencoder_h264_priv.h"
#include "gst/vaapi/gstvaapidisplay.h"
#include "gst/vaapi/gstvaapivalue.h"
#include "gst/vaapi/gstvaapisurface.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h264_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_h264_encode_debug

#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapiencode_h264_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
            GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, GST_VIDEO_FORMATS_ALL)
        ", interlace-mode=progressive";
#else
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL) ",interlace-mode=progressive; "
    GST_VAAPI_SURFACE_CAPS ",interlace-mode=progressive";
#endif

static const char gst_vaapiencode_h264_src_caps_str[] =
    GST_CAPS_CODEC("video/x-h264");

static GstStaticPadTemplate gst_vaapiencode_h264_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiencode_h264_sink_caps_str));

static GstStaticPadTemplate gst_vaapiencode_h264_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiencode_h264_src_caps_str));

/* h264 encode */
G_DEFINE_TYPE(
    GstVaapiEncodeH264,
    gst_vaapiencode_h264,
    GST_TYPE_VAAPIENCODE)

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
gst_vaapiencode_h264_init(
    GstVaapiEncodeH264 *h264_encode
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE(h264_encode);
    encode->encoder = gst_vaapi_encoder_h264_new(NULL);
    g_assert(encode->encoder);
}

static void
gst_vaapiencode_h264_finalize(GObject *object)
{
    //GstVaapiEncodeH264 * const h264_encode = GST_VAAPIENCODE_H264(object);
    G_OBJECT_CLASS(gst_vaapiencode_h264_parent_class)->finalize(object);
}

static void
gst_vaapiencode_h264_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE(object);
    GstVaapiEncoderH264 *h264encoder = GST_VAAPI_ENCODER_H264(encode->encoder);

    g_assert(h264encoder);

    switch (prop_id) {
    case H264_PROP_PROFILE: {
        guint profile = g_value_get_uint(value);
        h264encoder->profile = profile;
        break;
    }

    case H264_PROP_LEVEL: {
        guint level = g_value_get_uint(value);
        h264encoder->level = level;
        break;
    }

    case H264_PROP_RATE_CONTROL: {
      GST_VAAPI_ENCODER_RATE_CONTROL(h264encoder) = g_value_get_enum(value);
      break;
    }

    case H264_PROP_BITRATE: {
        h264encoder->bitrate = g_value_get_uint(value);
        break;
    }

    case H264_PROP_INTRA_PERIOD:
        h264encoder->intra_period = g_value_get_uint(value);
        break;

    case H264_PROP_INIT_QP:
        h264encoder->init_qp = g_value_get_uint(value);
        break;

    case H264_PROP_MIN_QP:
        h264encoder->min_qp = g_value_get_uint(value);
        break;

    case H264_PROP_SLICE_NUM:
        h264encoder->slice_num= g_value_get_uint(value);
        break;

    case H264_PROP_B_FRAME_NUM:
        h264encoder->b_frame_num= g_value_get_uint(value);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiencode_h264_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE(object);
    GstVaapiEncoderH264 *h264encoder = GST_VAAPI_ENCODER_H264(encode->encoder);
    g_assert(h264encoder);

    switch (prop_id) {
    case H264_PROP_PROFILE:
        g_value_set_uint (value, h264encoder->profile);
        break;
    case H264_PROP_LEVEL:
        g_value_set_uint (value, h264encoder->level);
        break;
    case H264_PROP_RATE_CONTROL:
        g_value_set_enum(value, GST_VAAPI_ENCODER_RATE_CONTROL(h264encoder));
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

static GstVaapiEncoder *
gst_vaapiencode_h264_create_encoder(GstVaapiDisplay *display)
{
    return gst_vaapi_encoder_h264_new(display);
}

static void
gst_vaapiencode_h264_class_init(GstVaapiEncodeH264Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass *const element_class = GST_ELEMENT_CLASS(klass);
    GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS(klass);
    GST_DEBUG_CATEGORY_INIT (gst_vaapi_h264_encode_debug,
                           "vaapih264encode",
                           0,
                           "vaapih264encode element");

    object_class->finalize      = gst_vaapiencode_h264_finalize;
    object_class->set_property  = gst_vaapiencode_h264_set_property;
    object_class->get_property  = gst_vaapiencode_h264_get_property;

    encode_class->create_encoder = gst_vaapiencode_h264_create_encoder;

    gst_element_class_set_static_metadata(element_class,
        "VA-API h264 encoder",
        "Codec/Encoder/Video",
        "A VA-API based video encoder",
        "Wind Yuan <feng.yuan@intel.com>");

    /* sink pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiencode_h264_sink_factory)
    );

    /* src pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiencode_h264_src_factory)
    );

    g_object_class_install_property (
        object_class,
        H264_PROP_PROFILE,
        g_param_spec_uint (
            "profile",
            "H264 Profile",
            "Profile supports: Baseline, Main, High",
            GST_VAAPI_PROFILE_H264_BASELINE,
            GST_VAAPI_PROFILE_H264_HIGH,
            GST_VAAPI_ENCODER_H264_DEFAULT_PROFILE,
            G_PARAM_READWRITE));

    g_object_class_install_property (
      object_class,
      H264_PROP_LEVEL,
      g_param_spec_uint (
          "level",
          "H264 level idc",
          "Level idc supports: 10, 11, 12, 13, 20, 21, 22, 30, 31, 32, 40, 41",
          GST_VAAPI_ENCODER_H264_LEVEL_10,
          GST_VAAPI_ENCODER_H264_LEVEL_51,
          GST_VAAPI_ENCODER_H264_DEFAULT_LEVEL,
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
          GST_VAAPI_ENCODER_H264_DEFAULT_INTRA_PERIOD,
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
          GST_VAAPI_ENCODER_H264_DEFAULT_INIT_QP,
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
          GST_VAAPI_ENCODER_H264_DEFAULT_MIN_QP,
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
