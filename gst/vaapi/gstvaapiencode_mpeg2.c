/*
 *  gstvaapiencode_mpeg2.c - VA-API MPEG2 encoder
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

#include "gstvaapiencode_mpeg2.h"
#include "gst/vaapi/gstvaapiencoder_mpeg2.h"
#include "gst/vaapi/gstvaapiencoder_mpeg2_priv.h"
#include "gst/vaapi/gstvaapidisplay.h"
#include "gst/vaapi/gstvaapivalue.h"
#include "gst/vaapi/gstvaapisurface.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_mpeg2_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_mpeg2_encode_debug

#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapiencode_mpeg2_sink_caps_str[] =
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL) ",interlace-mode=progressive; "
    GST_VAAPI_SURFACE_CAPS ",interlace-mode=progressive";

static const char gst_vaapiencode_mpeg2_src_caps_str[] =
    GST_CAPS_CODEC("video/mpeg,"
       "mpegversion = (int) 2, "
       "systemstream = (boolean) false");

static GstStaticPadTemplate gst_vaapiencode_mpeg2_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiencode_mpeg2_sink_caps_str));

static GstStaticPadTemplate gst_vaapiencode_mpeg2_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapiencode_mpeg2_src_caps_str));

/* mpeg2 encode */
G_DEFINE_TYPE(
    GstVaapiEncodeMpeg2,
    gst_vaapiencode_mpeg2,
    GST_TYPE_VAAPIENCODE)

enum {
    MPEG2_PROP_0,
    MPEG2_PROP_PROFILE,
    MPEG2_PROP_LEVEL,
    MPEG2_PROP_RATE_CONTROL,
    MPEG2_PROP_BITRATE,
    MPEG2_PROP_CQP,
    MPEG2_PROP_GOP_SIZE,
    MPEG2_PROP_MAX_BFRAMES
};

static void
gst_vaapiencode_mpeg2_init(
    GstVaapiEncodeMpeg2 *mpeg2_encode
)
{
    GstVaapiEncoderMpeg2 *mpeg2encoder = NULL;
    GstVaapiEncode *encode = GST_VAAPIENCODE(mpeg2_encode);
    encode->encoder = gst_vaapi_encoder_mpeg2_new(NULL);
    g_assert(encode->encoder);

    mpeg2encoder = (GstVaapiEncoderMpeg2 *)encode->encoder;
    mpeg2encoder->profile       = GST_VAAPI_ENCODER_MPEG2_DEFAULT_PROFILE;
    mpeg2encoder->level         = GST_VAAPI_ENCODER_MPEG2_DEFAULT_LEVEL;
    GST_VAAPI_ENCODER_RATE_CONTROL(mpeg2encoder)  = GST_VAAPI_ENCODER_MPEG2_DEFAULT_RATE_CONTROL;
    mpeg2encoder->bitrate       = 0;
    mpeg2encoder->cqp           = GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP;
    mpeg2encoder->intra_period  = GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE;
    mpeg2encoder->ip_period     = GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES;
}

static void
gst_vaapiencode_mpeg2_finalize(GObject *object)
{
    //GstVaapiEncodeMpeg2 * const mpeg2_encode = GST_VAAPIENCODE_MPEG2(object);
    G_OBJECT_CLASS(gst_vaapiencode_mpeg2_parent_class)->finalize(object);
}

static void
gst_vaapiencode_mpeg2_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE(object);
    GstVaapiEncoderMpeg2 *mpeg2encoder = GST_VAAPI_ENCODER_MPEG2(encode->encoder);

    g_assert(mpeg2encoder);

    switch (prop_id) {
    case MPEG2_PROP_PROFILE: {
        guint profile = g_value_get_uint(value);
        mpeg2encoder->profile = profile;
        break;
    }

    case MPEG2_PROP_LEVEL: {
        guint level = g_value_get_uint(value);
        mpeg2encoder->level = level;
        break;
    }

    case MPEG2_PROP_RATE_CONTROL: {
      GstVaapiRateControl rate_control = g_value_get_enum(value);
      if (rate_control == GST_VAAPI_RATECONTROL_CBR ||
        rate_control == GST_VAAPI_RATECONTROL_CQP) {
        GST_VAAPI_ENCODER_RATE_CONTROL(mpeg2encoder) = g_value_get_enum(value);
      } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
    }

    case MPEG2_PROP_BITRATE: {
        mpeg2encoder->bitrate = g_value_get_uint(value);
        break;
    }

    case MPEG2_PROP_CQP: {
        mpeg2encoder->cqp = g_value_get_uint(value);
        break;
    }

    case MPEG2_PROP_GOP_SIZE: {
        mpeg2encoder->intra_period = g_value_get_uint(value);
        break;
    }

    case MPEG2_PROP_MAX_BFRAMES: {
        mpeg2encoder->ip_period = g_value_get_uint(value);
        break;
    }

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapiencode_mpeg2_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE(object);
    GstVaapiEncoderMpeg2 *mpeg2encoder = GST_VAAPI_ENCODER_MPEG2(encode->encoder);
    g_assert(mpeg2encoder);

    switch (prop_id) {
    case MPEG2_PROP_PROFILE:
        g_value_set_uint (value, mpeg2encoder->profile);
        break;
    case MPEG2_PROP_LEVEL:
        g_value_set_uint (value, mpeg2encoder->level);
        break;
    case MPEG2_PROP_RATE_CONTROL:
        g_value_set_enum(value, GST_VAAPI_ENCODER_RATE_CONTROL(mpeg2encoder));
        break;
    case MPEG2_PROP_BITRATE:
        g_value_set_uint (value, mpeg2encoder->bitrate);
        break;
    case MPEG2_PROP_CQP:
        g_value_set_uint (value, mpeg2encoder->cqp);
        break;
    case MPEG2_PROP_GOP_SIZE:
        g_value_set_uint (value, mpeg2encoder->intra_period);
        break;
    case MPEG2_PROP_MAX_BFRAMES:
        g_value_set_uint (value, mpeg2encoder->ip_period);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static GstVaapiEncoder *
gst_vaapiencode_mpeg2_create_encoder(GstVaapiDisplay *display)
{
    return gst_vaapi_encoder_mpeg2_new(display);
}

static void
gst_vaapiencode_mpeg2_class_init(GstVaapiEncodeMpeg2Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstElementClass *const element_class = GST_ELEMENT_CLASS(klass);
    GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS(klass);
    GST_DEBUG_CATEGORY_INIT (gst_vaapi_mpeg2_encode_debug,
                           "vaapimpeg2encode",
                           0,
                           "vaapimpeg2encode element");

    object_class->finalize      = gst_vaapiencode_mpeg2_finalize;
    object_class->set_property  = gst_vaapiencode_mpeg2_set_property;
    object_class->get_property  = gst_vaapiencode_mpeg2_get_property;

    encode_class->create_encoder = gst_vaapiencode_mpeg2_create_encoder;

    gst_element_class_set_static_metadata(element_class,
        "VA-API mpeg2 encoder",
        "Codec/Encoder/Video",
        "A VA-API based video encoder",
        "Wind Yuan <feng.yuan@intel.com>");

    /* sink pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiencode_mpeg2_sink_factory)
    );

    /* src pad */
    gst_element_class_add_pad_template(
        element_class,
        gst_static_pad_template_get(&gst_vaapiencode_mpeg2_src_factory)
    );


    g_object_class_install_property (
        object_class,
        MPEG2_PROP_PROFILE,
        g_param_spec_uint (
            "profile",
            "Mpeg2 Profile",
            "Profile supports: Simple, Main",
            GST_ENCODER_MPEG2_PROFILE_SIMPLE,
            GST_ENCODER_MPEG2_PROFILE_MAIN,
            GST_VAAPI_ENCODER_MPEG2_DEFAULT_PROFILE,
            G_PARAM_READWRITE));

    g_object_class_install_property (
      object_class,
      MPEG2_PROP_LEVEL,
      g_param_spec_uint (
          "level",
          "Mpeg2 level idc",
          "Level idc supports: Low, Main, High",
          GST_VAAPI_ENCODER_MPEG2_LEVEL_LOW,
          GST_VAAPI_ENCODER_MPEG2_LEVEL_HIGH,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_LEVEL,
          G_PARAM_READWRITE));


    g_object_class_install_property (
      object_class,
      MPEG2_PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "rate-control",
          "Rate control mode, only  cbr, cqp work for mpeg2",
          GST_VAAPI_TYPE_RATE_CONTROL,
          GST_VAAPI_RATECONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


    g_object_class_install_property (
      object_class,
      MPEG2_PROP_BITRATE,
      g_param_spec_uint (
          "bitrate",
          "Mpeg2 encoding bitrate(kbps)",
          "Mpeg2 encoding bitrate(kbps), (0, auto-calculate)",
          0,
          GST_VAAPI_ENCODER_MPEG2_MAX_BITRATE,
          0,
          G_PARAM_READWRITE));

    g_object_class_install_property (
      object_class,
      MPEG2_PROP_CQP,
      g_param_spec_uint (
          "const qp",
          "Mpeg2 encoding const qp",
          "only work when you set rate control to CQP",
          GST_VAAPI_ENCODER_MPEG2_MIN_CQP,
          GST_VAAPI_ENCODER_MPEG2_MAX_CQP,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP,
          G_PARAM_READWRITE));



    g_object_class_install_property (
      object_class,
      MPEG2_PROP_GOP_SIZE,
      g_param_spec_uint (
          "gop size",
          "Mpeg2 encoding intra-period",
          "gop size",
          1,
          GST_VAAPI_ENCODER_MPEG2_MAX_GOP_SIZE,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE,
          G_PARAM_READWRITE));

    g_object_class_install_property (
      object_class,
      MPEG2_PROP_MAX_BFRAMES,
      g_param_spec_uint (
          "max bframes",
          "Mpeg2 encoding ip peroid",
          "max bframes between two reference frames",
          0,
          GST_VAAPI_ENCODER_MPEG2_MAX_MAX_BFRAMES,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES,
          G_PARAM_READWRITE));



}
