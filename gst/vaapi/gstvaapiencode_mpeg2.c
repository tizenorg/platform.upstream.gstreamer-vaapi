/*
 *  gstvaapiencode_mpeg2.c - VA-API MPEG2 encoder
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Guangxin Xu <guangxin.xu@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_mpeg2.h>
#include "gstvaapiencode_mpeg2.h"
#include "gstvaapipluginutil.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideomemory.h"
#endif

#define GST_PLUGIN_NAME "vaapiencode_mpeg2"
#define GST_PLUGIN_DESC "A VA-API based MPEG-2 video encoder"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_mpeg2_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_mpeg2_encode_debug

#define GST_CODEC_CAPS                          \
  "video/mpeg, mpegversion = (int) 2, "         \
  "systemstream = (boolean) false"

/* *INDENT-OFF* */
static const char gst_vaapiencode_mpeg2_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE,
      "{ ENCODED, NV12, I420, YV12 }") ", "
#else
  GST_VAAPI_SURFACE_CAPS ", "
#endif
  GST_CAPS_INTERLACED_FALSE "; "
#if GST_CHECK_VERSION(1,0,0)
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ", "
#else
  "video/x-raw-yuv, "
  "width  = (int) [ 1, MAX ], "
  "height = (int) [ 1, MAX ], "
#endif
  GST_CAPS_INTERLACED_FALSE;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapiencode_mpeg2_src_caps_str[] =
  GST_CODEC_CAPS;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapiencode_mpeg2_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (gst_vaapiencode_mpeg2_sink_caps_str));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapiencode_mpeg2_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (gst_vaapiencode_mpeg2_src_caps_str));
/* *INDENT-ON* */

/* mpeg2 encode */
G_DEFINE_TYPE (GstVaapiEncodeMpeg2, gst_vaapiencode_mpeg2,
    GST_TYPE_VAAPIENCODE);

static void
gst_vaapiencode_mpeg2_init (GstVaapiEncodeMpeg2 * encode)
{
  gst_vaapiencode_init_properties (GST_VAAPIENCODE_CAST (encode));
}

static void
gst_vaapiencode_mpeg2_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapiencode_mpeg2_parent_class)->finalize (object);
}

static void
gst_vaapiencode_mpeg2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_GET_CLASS (object);
  GstVaapiEncode *const base_encode = GST_VAAPIENCODE_CAST (object);

  switch (prop_id) {
    default:
      if (!encode_class->set_property (base_encode, prop_id, value))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapiencode_mpeg2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_GET_CLASS (object);
  GstVaapiEncode *const base_encode = GST_VAAPIENCODE_CAST (object);

  switch (prop_id) {
    default:
      if (!encode_class->get_property (base_encode, prop_id, value))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_vaapiencode_mpeg2_get_caps (GstVaapiEncode * base_encode)
{
  GstCaps *caps;

  caps = gst_caps_from_string (GST_CODEC_CAPS);

  /* XXX: update profile and level information */
  return caps;
}

static GstVaapiEncoder *
gst_vaapiencode_mpeg2_alloc_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_mpeg2_new (display);
}

static void
gst_vaapiencode_mpeg2_class_init (GstVaapiEncodeMpeg2Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_mpeg2_encode_debug,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  object_class->finalize = gst_vaapiencode_mpeg2_finalize;
  object_class->set_property = gst_vaapiencode_mpeg2_set_property;
  object_class->get_property = gst_vaapiencode_mpeg2_get_property;

  encode_class->get_properties = gst_vaapi_encoder_mpeg2_get_default_properties;
  encode_class->get_caps = gst_vaapiencode_mpeg2_get_caps;
  encode_class->alloc_encoder = gst_vaapiencode_mpeg2_alloc_encoder;

  gst_element_class_set_static_metadata (element_class,
      "VA-API MPEG-2 encoder",
      "Codec/Encoder/Video",
      GST_PLUGIN_DESC, "Guangxin Xu <guangxin.xu@intel.com>");

  /* sink pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_sink_factory));

  /* src pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_src_factory));

  gst_vaapiencode_class_init_properties (encode_class);
}
