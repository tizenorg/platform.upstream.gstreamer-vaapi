/*
 *  gstvaapidownload.c - VA-API video downloader
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

/**
 * SECTION:gstvaapidownload
 * @short_description: A VA to video flow filter
 *
 * vaapidownload converts from VA surfaces to raw YUV pixels.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videocontext.h>

#include "gstvaapidownload.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideobuffer.h"

#define GST_PLUGIN_NAME "vaapidownload"
#define GST_PLUGIN_DESC "A VA to video flow filter"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapidownload);
#define GST_CAT_DEFAULT gst_debug_vaapidownload

/* Default templates */
static const char gst_vaapidownload_yuv_caps_str[] =
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; ";

static const char gst_vaapidownload_vaapi_caps_str[] =
    GST_VAAPI_SURFACE_CAPS;

static GstStaticPadTemplate gst_vaapidownload_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidownload_vaapi_caps_str));

static GstStaticPadTemplate gst_vaapidownload_src_factory =
    GST_STATIC_PAD_TEMPLATE(
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapidownload_yuv_caps_str));

typedef struct _TransformSizeCache TransformSizeCache;
struct _TransformSizeCache {
    GstCaps            *caps;
    guint               size;
};

struct _GstVaapiDownload {
    /*< private >*/
    GstBaseTransform    parent_instance;

    GstVaapiDisplay    *display;
    GstCaps            *allowed_caps;
    TransformSizeCache  transform_size_cache[2];
    GstVaapiVideoPool  *images;
    GstVideoFormat      image_format;
    guint               image_width;
    guint               image_height;
    unsigned int        images_reset    : 1;
    unsigned int        image_hard_copy : 1;
};

struct _GstVaapiDownloadClass {
    /*< private >*/
    GstBaseTransformClass parent_class;
};

enum {
    PROP_0,
    PROP_HARDCOPY
};

/* GstImplementsInterface interface */
static gboolean
gst_vaapidownload_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapidownload_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapidownload_implements_interface_supported;
}

/* GstVideoContext interface */
static void
gst_vaapidownload_set_video_context(GstVideoContext *context, const gchar *type,
    const GValue *value)
{
  GstVaapiDownload *download = GST_VAAPIDOWNLOAD (context);
  gst_vaapi_set_display (type, value, &download->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapidownload_set_video_context;
}

#define GstVideoContextClass GstVideoContextInterface
G_DEFINE_TYPE_WITH_CODE(
    GstVaapiDownload,
    gst_vaapidownload,
    GST_TYPE_BASE_TRANSFORM,
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapidownload_implements_iface_init);
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

static gboolean
gst_vaapidownload_start(GstBaseTransform *trans);

static gboolean
gst_vaapidownload_stop(GstBaseTransform *trans);

static void
gst_vaapidownload_before_transform(GstBaseTransform *trans, GstBuffer *buffer);

static GstFlowReturn
gst_vaapidownload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
);

static GstCaps *
gst_vaapidownload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
);

static gboolean
gst_vaapidownload_transform_size(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    guint             size,
    GstCaps          *othercaps,
    guint            *othersize
);

static gboolean
gst_vaapidownload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
);

static gboolean
gst_vaapidownload_query(
    GstPad   *pad,
    GstQuery *query
);

static void
gst_vaapidownload_destroy(GstVaapiDownload *download)
{
    guint i;

    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (tsc->caps) {
            gst_caps_unref(tsc->caps);
            tsc->caps = NULL;
            tsc->size = 0;
        }
    }

    if (download->allowed_caps) {
        gst_caps_unref(download->allowed_caps);
        download->allowed_caps = NULL;
    }

    gst_vaapi_video_pool_replace(&download->images, NULL);
    gst_vaapi_display_replace(&download->display, NULL);
}

static void
gst_vaapidownload_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDownload *download = GST_VAAPIDOWNLOAD (object);

    switch (prop_id) {
    case PROP_HARDCOPY:
        download->image_hard_copy = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapidownload_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDownload *download = GST_VAAPIDOWNLOAD (object);

    switch (prop_id) {
    case PROP_HARDCOPY:
        g_value_set_boolean(value, download->image_hard_copy);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapidownload_finalize(GObject *object)
{
    gst_vaapidownload_destroy(GST_VAAPIDOWNLOAD(object));

    G_OBJECT_CLASS(gst_vaapidownload_parent_class)->finalize(object);
}

static void
gst_vaapidownload_class_init(GstVaapiDownloadClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstBaseTransformClass * const trans_class = GST_BASE_TRANSFORM_CLASS(klass);
    GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapidownload,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    object_class->finalize        = gst_vaapidownload_finalize;
    object_class->set_property   = gst_vaapidownload_set_property;
    object_class->get_property   = gst_vaapidownload_get_property;

    trans_class->start            = gst_vaapidownload_start;
    trans_class->stop             = gst_vaapidownload_stop;
    trans_class->before_transform = gst_vaapidownload_before_transform;
    trans_class->transform        = gst_vaapidownload_transform;
    trans_class->transform_caps   = gst_vaapidownload_transform_caps;
    trans_class->transform_size   = gst_vaapidownload_transform_size;
    trans_class->set_caps         = gst_vaapidownload_set_caps;

    gst_element_class_set_static_metadata(element_class,
        "VA-API colorspace converter",
        "Filter/Converter/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    /* sink pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidownload_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    /* src pad */
    pad_template = gst_static_pad_template_get(&gst_vaapidownload_src_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

    g_object_class_install_property
        (object_class,
         PROP_HARDCOPY,
         g_param_spec_boolean("hardcopy",
                              "Hardcopy",
                              "Hard copy image directly from surface with same format",
                              FALSE,
                              G_PARAM_READWRITE));
}

static void
gst_vaapidownload_init(GstVaapiDownload *download)
{
    GstPad *sinkpad, *srcpad;

    download->display           = NULL;
    download->allowed_caps      = NULL;
    download->images            = NULL;
    download->images_reset      = FALSE;
    download->image_format      = GST_VIDEO_FORMAT_UNKNOWN;
    download->image_width       = 0;
    download->image_height      = 0;
    download->image_hard_copy   = FALSE;

    /* Override buffer allocator on sink pad */
    sinkpad = gst_element_get_static_pad(GST_ELEMENT(download), "sink");
    gst_pad_set_query_function(sinkpad, gst_vaapidownload_query);
    gst_object_unref(sinkpad);

    /* Override query on src pad */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
    gst_pad_set_query_function(srcpad, gst_vaapidownload_query);
    gst_object_unref(srcpad);
}

static inline gboolean
gst_vaapidownload_ensure_display(GstVaapiDownload *download)
{
    return gst_vaapi_ensure_display(download, GST_VAAPI_DISPLAY_TYPE_ANY,
        &download->display);
}

static gboolean
gst_vaapidownload_start(GstBaseTransform *trans)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    if (!gst_vaapidownload_ensure_display(download))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_stop(GstBaseTransform *trans)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    gst_vaapi_display_replace(&download->display, NULL);
    return TRUE;
}

static GstVideoFormat
get_surface_format(GstVaapiSurface *surface)
{
    GstVaapiImage *image;
    GstVideoFormat format = GST_VIDEO_FORMAT_NV12;

    /* XXX: NV12 is assumed by default */
    image = gst_vaapi_surface_derive_image(surface);
    if (image) {
        format = gst_vaapi_image_get_format(image);
        gst_vaapi_object_unref(image);
    }
    return format;
}

static void
fix_frame_rate(GstCaps *caps)
{
    GstStructure *structure;

    if (gst_caps_is_any(caps) ||
        !(structure = gst_caps_get_structure(caps, 0)))
        return;
    if (!gst_structure_has_field(structure, "framerate"))
        return;
    gst_structure_fixate_field_nearest_fraction(
        structure, "framerate",
        30, 1);
}

static gboolean
gst_vaapidownload_update_src_caps(GstVaapiDownload *download, GstBuffer *buffer)
{
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;
    GstVideoFormat format;
    GstPad *srcpad;
    GstCaps *in_caps, *out_caps;

    meta = gst_buffer_get_vaapi_video_meta(buffer);
    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface) {
        GST_WARNING("failed to retrieve VA surface from buffer");
        return FALSE;
    }

    format = get_surface_format(surface);
    if (format == download->image_format)
        return TRUE;

    in_caps = GST_BUFFER_CAPS(buffer);
    if (!in_caps) {
        GST_WARNING("failed to retrieve caps from buffer");
        return FALSE;
    }

    out_caps = gst_vaapi_video_format_to_caps(download->image_format);
    if (!out_caps) {
        GST_WARNING("failed to create caps from format %s",
                    gst_video_format_to_string(download->image_format));
        return FALSE;
    }

    if (!gst_vaapi_append_surface_caps(out_caps, in_caps)) {
        gst_caps_unref(out_caps);
        return FALSE;
    }
    fix_frame_rate(out_caps);

    /* Try to renegotiate downstream caps */
    srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
    gst_pad_set_caps(srcpad, out_caps);
    gst_object_unref(srcpad);

    gst_vaapidownload_set_caps(GST_BASE_TRANSFORM(download), in_caps, out_caps);
    gst_caps_replace(&download->allowed_caps, out_caps);
    gst_caps_unref(out_caps);
    return TRUE;
}

static void
gst_vaapidownload_before_transform(GstBaseTransform *trans, GstBuffer *buffer)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    gst_vaapidownload_update_src_caps(download, buffer);
}

static GstFlowReturn
gst_vaapidownload_transform(
    GstBaseTransform *trans,
    GstBuffer        *inbuf,
    GstBuffer        *outbuf
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;
    GstVaapiImage *image = NULL;
    gboolean success;

    meta = gst_buffer_get_vaapi_video_meta(inbuf);
    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface)
        return GST_FLOW_UNEXPECTED;

    if (download->image_hard_copy) {
        image = gst_vaapi_surface_derive_image(surface);
        if (!image)
          return GST_FLOW_UNEXPECTED;

        if (GST_VAAPI_IMAGE_FORMAT(image) != download->image_format)
          goto error_format;

        success = gst_vaapi_image_get_buffer(image, outbuf, NULL);
        g_object_unref(image);
        if (!success) {
          GST_WARNING("failed to hard copy surface(%"
                      GST_FOURCC_FORMAT
                      ") to buffer",
                      GST_FOURCC_ARGS(download->image_format));
          return GST_FLOW_UNEXPECTED;
        }
        return GST_FLOW_OK;
    }

    image = gst_vaapi_video_pool_get_object(download->images);
    if (!image)
        return GST_FLOW_UNEXPECTED;
    if (!gst_vaapi_surface_get_image(surface, image))
        goto error_get_image;

    success = gst_vaapi_image_get_buffer(image, outbuf, NULL);
    gst_vaapi_video_pool_put_object(download->images, image);
    if (!success)
        goto error_get_buffer;
    return GST_FLOW_OK;

error_get_image:
    {
        const GstVideoFormat format = gst_vaapi_image_get_format(image);
        GST_WARNING("failed to download %s image from surface 0x%08x",
                    gst_video_format_to_string(format),
                    gst_vaapi_surface_get_id(surface));
        gst_vaapi_video_pool_put_object(download->images, image);
        return GST_FLOW_UNEXPECTED;
    }

error_get_buffer:
    {
        GST_WARNING("failed to transfer image to output video buffer");
        return GST_FLOW_UNEXPECTED;
    }

error_format:
    {
        GST_WARNING("wrong format in hard_copy, surface format:%"
                    GST_FOURCC_FORMAT
                    " image format:%"
                    GST_FOURCC_FORMAT "",
                    GST_FOURCC_ARGS(gst_vaapi_image_get_format(image)),
                    GST_FOURCC_ARGS(download->image_format));
        g_object_unref(image);
        return GST_FLOW_UNEXPECTED;
    }
}

static GstCaps *
gst_vaapidownload_transform_caps(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstPad *srcpad;
    GstCaps *allowed_caps, *inter_caps, *out_caps = NULL;
    GstStructure *structure;

    g_return_val_if_fail(GST_IS_CAPS(caps), NULL);

    structure = gst_caps_get_structure(caps, 0);

    if (direction == GST_PAD_SINK) {
        if (!gst_structure_has_name(structure, GST_VAAPI_SURFACE_CAPS_NAME))
            return NULL;
        if (!gst_vaapidownload_ensure_display(download))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapidownload_yuv_caps_str);

        /* Build up allowed caps */
        /* XXX: we don't know the decoded surface format yet so we
           expose whatever VA images we support */
        if (download->allowed_caps)
            allowed_caps = gst_caps_ref(download->allowed_caps);
        else {
            allowed_caps = gst_vaapi_display_get_image_caps(download->display);
            if (!allowed_caps)
                return NULL;
        }
        inter_caps = gst_caps_intersect(out_caps, allowed_caps);
        gst_caps_unref(allowed_caps);
        gst_caps_unref(out_caps);
        out_caps = inter_caps;

        /* Intersect with allowed caps from the peer, if any */
        srcpad = gst_element_get_static_pad(GST_ELEMENT(download), "src");
        allowed_caps = gst_pad_peer_get_caps(srcpad);
        if (allowed_caps) {
            inter_caps = gst_caps_intersect(out_caps, allowed_caps);
            gst_caps_unref(allowed_caps);
            gst_caps_unref(out_caps);
            out_caps = inter_caps;
        }
    }
    else {
        if (!gst_structure_has_name(structure, "video/x-raw-yuv"))
            return NULL;
        out_caps = gst_caps_from_string(gst_vaapidownload_vaapi_caps_str);

        structure = gst_caps_get_structure(out_caps, 0);
        gst_structure_set(
            structure,
            "type", G_TYPE_STRING, "vaapi",
            "opengl", G_TYPE_BOOLEAN, USE_GLX,
            NULL
        );
    }

    if (!gst_vaapi_append_surface_caps(out_caps, caps)) {
        gst_caps_unref(out_caps);
        return NULL;
    }
    return out_caps;
}

static gboolean
gst_vaapidownload_ensure_image_pool(GstVaapiDownload *download, GstCaps *caps)
{
    GstVideoInfo vi;
    GstVideoFormat format;
    guint width, height;

    if (!gst_video_info_from_caps(&vi, caps))
        return FALSE;

    format = GST_VIDEO_INFO_FORMAT(&vi);
    width  = GST_VIDEO_INFO_WIDTH(&vi);
    height = GST_VIDEO_INFO_HEIGHT(&vi);

    if (format != download->image_format ||
        width  != download->image_width  ||
        height != download->image_height) {
        download->image_format = format;
        download->image_width  = width;
        download->image_height = height;
        gst_vaapi_video_pool_replace(&download->images, NULL);
        download->images = gst_vaapi_image_pool_new(download->display, &vi);
        if (!download->images)
            return FALSE;
        download->images_reset = TRUE;
    }
    return TRUE;
}

static inline gboolean
gst_vaapidownload_negotiate_buffers(
    GstVaapiDownload  *download,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    if (!gst_vaapidownload_ensure_image_pool(download, outcaps))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_set_caps(
    GstBaseTransform *trans,
    GstCaps          *incaps,
    GstCaps          *outcaps
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);

    if (!gst_vaapidownload_negotiate_buffers(download, incaps, outcaps))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapidownload_transform_size(
    GstBaseTransform *trans,
    GstPadDirection   direction,
    GstCaps          *caps,
    guint             size,
    GstCaps          *othercaps,
    guint            *othersize
)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(trans);
    GstStructure * const structure = gst_caps_get_structure(othercaps, 0);
    GstVideoFormat format;
    gint width, height;
    guint i;

    /* Lookup in cache */
    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (tsc->caps && tsc->caps == othercaps) {
            *othersize = tsc->size;
            return TRUE;
        }
    }

    /* Compute requested buffer size */
    if (gst_structure_has_name(structure, GST_VAAPI_SURFACE_CAPS_NAME))
        *othersize = 0;
    else {
        if (!gst_video_format_parse_caps(othercaps, &format, &width, &height))
            return FALSE;
        *othersize = gst_video_format_get_size(format, width, height);
    }

    /* Update cache */
    for (i = 0; i < G_N_ELEMENTS(download->transform_size_cache); i++) {
        TransformSizeCache * const tsc = &download->transform_size_cache[i];
        if (!tsc->caps) {
            gst_caps_replace(&tsc->caps, othercaps);
            tsc->size = *othersize;
        }
    }
    return TRUE;
}

static gboolean
gst_vaapidownload_query(GstPad *pad, GstQuery *query)
{
    GstVaapiDownload * const download = GST_VAAPIDOWNLOAD(gst_pad_get_parent_element(pad));
    gboolean res;

    GST_DEBUG("sharing display %p", download->display);

    if (gst_vaapi_reply_to_query(query, download->display))
        res = TRUE;
    else
        res = gst_pad_query_default(pad, query);

    gst_object_unref(download);
    return res;

}
