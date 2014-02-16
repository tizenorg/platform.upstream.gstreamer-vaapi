/*
 *  gstvaapisink.c - VA-API video sink
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

/**
 * SECTION:gstvaapisink
 * @short_description: A VA-API based videosink
 *
 * vaapisink renders video frames to a drawable (X #Window) on a local
 * display using the Video Acceleration (VA) API. The element will
 * create its own internal window and render into it.
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/vaapi/gstvaapivalue.h>
#if USE_DRM
# include <gst/vaapi/gstvaapidisplay_drm.h>
#endif
#if USE_X11
# include <gst/vaapi/gstvaapidisplay_x11.h>
# include <gst/vaapi/gstvaapiwindow_x11.h>
#endif
#if USE_GLX
# include <gst/vaapi/gstvaapidisplay_glx.h>
# include <gst/vaapi/gstvaapiwindow_glx.h>
#endif
#if USE_WAYLAND
# include <gst/vaapi/gstvaapidisplay_wayland.h>
# include <gst/vaapi/gstvaapiwindow_wayland.h>
#endif

/* Supported interfaces */
#if GST_CHECK_VERSION(1,0,0)
# include <gst/video/videooverlay.h>
#else
# include <gst/interfaces/xoverlay.h>

# define GST_TYPE_VIDEO_OVERLAY         GST_TYPE_X_OVERLAY
# define GST_VIDEO_OVERLAY              GST_X_OVERLAY
# define GstVideoOverlay                GstXOverlay
# define GstVideoOverlayInterface       GstXOverlayClass

# define gst_video_overlay_prepare_window_handle(sink) \
    gst_x_overlay_prepare_xwindow_id(sink)
# define gst_video_overlay_got_window_handle(sink, window_handle) \
    gst_x_overlay_got_window_handle(sink, window_handle)
#endif

#include "gstvaapisink.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"
#endif

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapisink);
#define GST_CAT_DEFAULT gst_debug_vaapisink

/* Default template */
static const char gst_vaapisink_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES(
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE, "{ ENCODED, NV12, I420, YV12 }") ";"
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL);
#else
#if GST_CHECK_VERSION(1,0,0)
    GST_VIDEO_CAPS_MAKE(GST_VIDEO_FORMATS_ALL) "; "
#else
    "video/x-raw-yuv, "
    "width  = (int) [ 1, MAX ], "
    "height = (int) [ 1, MAX ]; "
#endif
    GST_VAAPI_SURFACE_CAPS;
#endif

static GstStaticPadTemplate gst_vaapisink_sink_factory =
    GST_STATIC_PAD_TEMPLATE(
        "sink",
        GST_PAD_SINK,
        GST_PAD_ALWAYS,
        GST_STATIC_CAPS(gst_vaapisink_sink_caps_str));

static gboolean
gst_vaapisink_has_interface(GstVaapiPluginBase *plugin, GType type)
{
    return type == GST_TYPE_VIDEO_OVERLAY;
}

static void
gst_vaapisink_video_overlay_iface_init(GstVideoOverlayInterface *iface);

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiSink,
    gst_vaapisink,
    GST_TYPE_VIDEO_SINK,
    GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_OVERLAY,
                          gst_vaapisink_video_overlay_iface_init))

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_DISPLAY_TYPE,
    PROP_FULLSCREEN,
    PROP_SYNCHRONOUS,
    PROP_USE_GLX,
    PROP_USE_REFLECTION,
    PROP_ROTATION,
    PROP_FORCE_ASPECT_RATIO,
};

#define DEFAULT_DISPLAY_TYPE            GST_VAAPI_DISPLAY_TYPE_ANY
#define DEFAULT_ROTATION                GST_VAAPI_ROTATION_0

static inline gboolean
gst_vaapisink_ensure_display(GstVaapiSink *sink);

/* GstVideoOverlay interface */

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id);
#endif
#if USE_WAYLAND
static gboolean
gst_vaapisink_ensure_window_wl_surface(GstVaapiSink *sink, gpointer wld_surface);
#endif

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *buffer);

static void
gst_vaapisink_video_overlay_set_window_handle(GstVideoOverlay *overlay,
    guintptr window)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
    GstVaapiDisplayType display_type;

    if (!gst_vaapisink_ensure_display(sink))
        return;
    display_type = GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink);

    /* Disable GLX rendering when vaapisink is using a foreign X
       window. It's pretty much useless */
    if (display_type == GST_VAAPI_DISPLAY_TYPE_GLX) {
        display_type = GST_VAAPI_DISPLAY_TYPE_X11;
        gst_vaapi_plugin_base_set_display_type(GST_VAAPI_PLUGIN_BASE(sink),
            display_type);
    }

    sink->foreign_window = TRUE;

    switch (display_type) {
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
        gst_vaapisink_ensure_window_xid(sink, window);
        break;
#endif
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        gst_vaapisink_ensure_window_wl_surface(sink, (gpointer)window);
        break;
#endif
    default:
        break;
    }
}

static void
gst_vaapisink_video_overlay_set_render_rectangle(
    GstVideoOverlay *overlay,
    gint         x,
    gint         y,
    gint         width,
    gint         height
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
    GstVaapiRectangle * const display_rect = &sink->display_rect;

    display_rect->x      = x;
    display_rect->y      = y;
    display_rect->width  = width;
    display_rect->height = height;
    
    GST_DEBUG("render rect (%d,%d):%ux%u",
              display_rect->x, display_rect->y,
              display_rect->width, display_rect->height);
}

static void
gst_vaapisink_video_overlay_expose(GstVideoOverlay *overlay)
{
    GstVaapiSink * const sink = GST_VAAPISINK(overlay);
    GstBaseSink * const base_sink = GST_BASE_SINK(overlay);
    GstBuffer *buffer;

    if (sink->use_overlay)
        buffer = sink->video_buffer[0] ? gst_buffer_ref(sink->video_buffer[0]) : NULL;
    else {
#if GST_CHECK_VERSION(1,0,0)
        GstSample * const sample = gst_base_sink_get_last_sample(base_sink);
        if (!sample)
            return;
        buffer = gst_buffer_ref(gst_sample_get_buffer(sample));
        gst_sample_unref(sample);
#else
        buffer = gst_base_sink_get_last_buffer(base_sink);
#endif
    }
    if (buffer) {
        gst_vaapisink_show_frame(base_sink, buffer);
        gst_buffer_unref(buffer);
    }
}

static void
gst_vaapisink_video_overlay_iface_init(GstVideoOverlayInterface *iface)
{
    iface->set_window_handle    = gst_vaapisink_video_overlay_set_window_handle;
    iface->set_render_rectangle = gst_vaapisink_video_overlay_set_render_rectangle;
    iface->expose               = gst_vaapisink_video_overlay_expose;
}

static void
gst_vaapisink_destroy(GstVaapiSink *sink)
{
    int i;

    for (i=0; i<RETAIN_BUFFER_COUNT; i++) {
        gst_buffer_replace(&sink->video_buffer[i], NULL);
        sink->video_buffer[i] = NULL;
    }
#if USE_GLX
    gst_vaapi_texture_replace(&sink->texture, NULL);
#endif
    gst_caps_replace(&sink->caps, NULL);
}

#if USE_X11
/* Checks whether a ConfigureNotify event is in the queue */
typedef struct _ConfigureNotifyEventPendingArgs ConfigureNotifyEventPendingArgs;
struct _ConfigureNotifyEventPendingArgs {
    Window      window;
    guint       width;
    guint       height;
    gboolean    match;
};

static Bool
configure_notify_event_pending_cb(Display *dpy, XEvent *xev, XPointer arg)
{
    ConfigureNotifyEventPendingArgs * const args =
        (ConfigureNotifyEventPendingArgs *)arg;

    if (xev->type == ConfigureNotify &&
        xev->xconfigure.window == args->window &&
        xev->xconfigure.width  == args->width  &&
        xev->xconfigure.height == args->height)
        args->match = TRUE;

    /* XXX: this is a hack to traverse the whole queue because we
       can't use XPeekIfEvent() since it could block */
    return False;
}

static gboolean
configure_notify_event_pending(
    GstVaapiSink *sink,
    Window        window,
    guint         width,
    guint         height
)
{
    GstVaapiDisplayX11 * const display =
        GST_VAAPI_DISPLAY_X11(GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
    ConfigureNotifyEventPendingArgs args;
    XEvent xev;

    args.window = window;
    args.width  = width;
    args.height = height;
    args.match  = FALSE;

    /* XXX: don't use XPeekIfEvent() because it might block */
    XCheckIfEvent(
        gst_vaapi_display_x11_get_display(display),
        &xev,
        configure_notify_event_pending_cb, (XPointer)&args
    );
    return args.match;
}
#endif

static const gchar *
get_display_type_name(GstVaapiDisplayType display_type)
{
    gpointer const klass = g_type_class_peek(GST_VAAPI_TYPE_DISPLAY_TYPE);
    GEnumValue * const e = g_enum_get_value(klass, display_type);

    if (e)
        return e->value_name;
    return "<unknown-type>";
}

static inline gboolean
gst_vaapisink_ensure_display(GstVaapiSink *sink)
{
    return gst_vaapi_plugin_base_ensure_display(GST_VAAPI_PLUGIN_BASE(sink));
}

static void
gst_vaapisink_display_changed(GstVaapiPluginBase *plugin)
{
    GstVaapiSink * const sink = GST_VAAPISINK(plugin);
    GstVaapiRenderMode render_mode;

    GST_INFO("created %s %p", get_display_type_name(plugin->display_type),
             plugin->display);

    sink->use_overlay =
        gst_vaapi_display_get_render_mode(plugin->display, &render_mode) &&
        render_mode == GST_VAAPI_RENDER_MODE_OVERLAY;
    GST_DEBUG("use %s rendering mode",
              sink->use_overlay ? "overlay" : "texture");

    sink->use_rotation = gst_vaapi_display_has_property(plugin->display,
        GST_VAAPI_DISPLAY_PROP_ROTATION);
}

static gboolean
gst_vaapisink_ensure_uploader(GstVaapiSink *sink)
{
    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    if (!gst_vaapi_plugin_base_ensure_uploader(GST_VAAPI_PLUGIN_BASE(sink)))
        return FALSE;
    return TRUE;
}

static gboolean
gst_vaapisink_ensure_render_rect(GstVaapiSink *sink, guint width, guint height)
{
    GstVaapiRectangle * const display_rect = &sink->display_rect;
    guint num, den, display_par_n, display_par_d;
    gboolean success;

    /* Return success if caps are not set yet */
    if (!sink->caps)
        return TRUE;

    if (!sink->keep_aspect) {
        display_rect->width = width;
        display_rect->height = height;
        display_rect->x = 0;
        display_rect->y = 0;

        GST_DEBUG("force-aspect-ratio is false; distorting while scaling video");
        GST_DEBUG("render rect (%d,%d):%ux%u",
                  display_rect->x, display_rect->y,
                  display_rect->width, display_rect->height);
        return TRUE;
    }

    GST_DEBUG("ensure render rect within %ux%u bounds", width, height);

    gst_vaapi_display_get_pixel_aspect_ratio(
        GST_VAAPI_PLUGIN_BASE_DISPLAY(sink),
        &display_par_n, &display_par_d
    );
    GST_DEBUG("display pixel-aspect-ratio %d/%d",
              display_par_n, display_par_d);

    success = gst_video_calculate_display_ratio(
        &num, &den,
        sink->video_width, sink->video_height,
        sink->video_par_n, sink->video_par_d,
        display_par_n, display_par_d
    );
    if (!success)
        return FALSE;
    GST_DEBUG("video size %dx%d, calculated ratio %d/%d",
              sink->video_width, sink->video_height, num, den);

    display_rect->width = gst_util_uint64_scale_int(height, num, den);
    if (display_rect->width <= width) {
        GST_DEBUG("keeping window height");
        display_rect->height = height;
    }
    else {
        GST_DEBUG("keeping window width");
        display_rect->width  = width;
        display_rect->height =
            gst_util_uint64_scale_int(width, den, num);
    }
    GST_DEBUG("scaling video to %ux%u", display_rect->width, display_rect->height);

    g_assert(display_rect->width  <= width);
    g_assert(display_rect->height <= height);

    display_rect->x = (width  - display_rect->width)  / 2;
    display_rect->y = (height - display_rect->height) / 2;

    GST_DEBUG("render rect (%d,%d):%ux%u",
              display_rect->x, display_rect->y,
              display_rect->width, display_rect->height);
    return TRUE;
}

static void
gst_vaapisink_ensure_window_size(GstVaapiSink *sink, guint *pwidth, guint *pheight)
{
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    GstVideoRectangle src_rect, dst_rect, out_rect;
    guint num, den, display_width, display_height, display_par_n, display_par_d;
    gboolean success, scale;

    if (sink->foreign_window) {
        *pwidth  = sink->window_width;
        *pheight = sink->window_height;
        return;
    }

    gst_vaapi_display_get_size(display, &display_width, &display_height);
    if (sink->fullscreen) {
        *pwidth  = display_width;
        *pheight = display_height;
        return;
    }

    gst_vaapi_display_get_pixel_aspect_ratio(
        display,
        &display_par_n, &display_par_d
    );

    success = gst_video_calculate_display_ratio(
        &num, &den,
        sink->video_width, sink->video_height,
        sink->video_par_n, sink->video_par_d,
        display_par_n, display_par_d
    );
    if (!success) {
        num = sink->video_par_n;
        den = sink->video_par_d;
    }

    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = gst_util_uint64_scale_int(sink->video_height, num, den);
    src_rect.h = sink->video_height;
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.w = display_width;
    dst_rect.h = display_height;
    scale      = (src_rect.w > dst_rect.w || src_rect.h > dst_rect.h);
    gst_video_sink_center_rect(src_rect, dst_rect, &out_rect, scale);
    *pwidth    = out_rect.w;
    *pheight   = out_rect.h;
}

static inline gboolean
gst_vaapisink_ensure_window(GstVaapiSink *sink, guint width, guint height)
{
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

    if (!sink->window) {
        const GstVaapiDisplayType display_type =
            GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink);
        switch (display_type) {
#if USE_GLX
        case GST_VAAPI_DISPLAY_TYPE_GLX:
            sink->window = gst_vaapi_window_glx_new(display, width, height);
            goto notify_video_overlay_interface;
#endif
#if USE_X11
        case GST_VAAPI_DISPLAY_TYPE_X11:
            sink->window = gst_vaapi_window_x11_new(display, width, height);
        notify_video_overlay_interface:
            if (!sink->window)
                break;
            gst_video_overlay_got_window_handle(
                GST_VIDEO_OVERLAY(sink),
                gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window))
            );
            break;
#endif
#if USE_WAYLAND
        case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
            sink->window = gst_vaapi_window_wayland_new(display, width, height);
            break;
#endif
        default:
            GST_ERROR("unsupported display type %d", display_type);
            return FALSE;
        }
    }
    return sink->window != NULL;
}

#if USE_X11
static gboolean
gst_vaapisink_ensure_window_xid(GstVaapiSink *sink, guintptr window_id)
{
    GstVaapiDisplay *display;
    Window rootwin;
    unsigned int width, height, border_width, depth;
    int x, y;
    XID xid = window_id;

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

    gst_vaapi_display_lock(display);
    XGetGeometry(
        gst_vaapi_display_x11_get_display(GST_VAAPI_DISPLAY_X11(display)),
        xid,
        &rootwin,
        &x, &y, &width, &height, &border_width, &depth
    );
    gst_vaapi_display_unlock(display);

    if ((width != sink->window_width || height != sink->window_height) &&
        !configure_notify_event_pending(sink, xid, width, height)) {
        if (!gst_vaapisink_ensure_render_rect(sink, width, height))
            return FALSE;
        sink->window_width  = width;
        sink->window_height = height;
    }

    if (sink->window &&
        gst_vaapi_window_x11_get_xid(GST_VAAPI_WINDOW_X11(sink->window)) == xid)
        return TRUE;

    gst_vaapi_window_replace(&sink->window, NULL);

    switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        sink->window = gst_vaapi_window_glx_new_with_xid(display, xid);
        break;
#endif
    case GST_VAAPI_DISPLAY_TYPE_X11:
        sink->window = gst_vaapi_window_x11_new_with_xid(display, xid);
        break;
    default:
        GST_ERROR("unsupported display type %d",
                  GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink));
        return FALSE;
    }
    return sink->window != NULL;
}
#endif

#if USE_WAYLAND
static gboolean
gst_vaapisink_ensure_window_wl_surface(GstVaapiSink *sink, gpointer wld_surface)
{
    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;

    switch (sink->display_type) {
	case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        sink->window = gst_vaapi_window_wayland_new_with_surface(sink->display, wld_surface);
        break;
    default:
        GST_ERROR("unsupported display type %d", sink->display_type);
        return FALSE;
    }
    return sink->window != NULL;
}
#endif

static gboolean
gst_vaapisink_ensure_rotation(GstVaapiSink *sink, gboolean recalc_display_rect)
{
    GstVaapiDisplay * const display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    gboolean success = FALSE;

    g_return_val_if_fail(display, FALSE);

    if (sink->rotation == sink->rotation_req)
        return TRUE;

    if (!sink->use_rotation) {
        GST_WARNING("VA display does not support rotation");
        goto end;
    }

    gst_vaapi_display_lock(display);
    success = gst_vaapi_display_set_rotation(display, sink->rotation_req);
    gst_vaapi_display_unlock(display);
    if (!success) {
        GST_ERROR("failed to change VA display rotation mode");
        goto end;
    }

    if (((sink->rotation + sink->rotation_req) % 180) == 90) {
        /* Orientation changed */
        G_PRIMITIVE_SWAP(guint, sink->video_width, sink->video_height);
        G_PRIMITIVE_SWAP(gint, sink->video_par_n, sink->video_par_d);
    }

    if (recalc_display_rect && !sink->foreign_window)
        gst_vaapisink_ensure_render_rect(sink, sink->window_width,
            sink->window_height);
    success = TRUE;

end:
    sink->rotation = sink->rotation_req;
    return success;
}

static gboolean
gst_vaapisink_start(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    if (!gst_vaapi_plugin_base_open(GST_VAAPI_PLUGIN_BASE(sink)))
        return FALSE;
    return gst_vaapisink_ensure_uploader(sink);
}

static gboolean
gst_vaapisink_stop(GstBaseSink *base_sink)
{
    int i = 0;
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    for (i=0; i<RETAIN_BUFFER_COUNT; i++) {
        gst_buffer_replace(&sink->video_buffer[i], NULL);
        sink->video_buffer[i] = NULL;
    }
#if GST_CHECK_VERSION(1,0,0)
    g_clear_object(&sink->video_buffer_pool);
#endif
    gst_vaapi_window_replace(&sink->window, NULL);

    gst_vaapi_plugin_base_close(GST_VAAPI_PLUGIN_BASE(sink));
    return TRUE;
}

static GstCaps *
gst_vaapisink_get_caps_impl(GstBaseSink *base_sink)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstCaps *out_caps, *yuv_caps;

#if GST_CHECK_VERSION(1,1,0)
    out_caps = gst_static_pad_template_get_caps(&gst_vaapisink_sink_factory);
#else
    out_caps = gst_caps_from_string(GST_VAAPI_SURFACE_CAPS);
#endif
    if (!out_caps)
        return NULL;

    if (gst_vaapisink_ensure_uploader(sink)) {
        yuv_caps = GST_VAAPI_PLUGIN_BASE_UPLOADER_CAPS(sink);
        if (yuv_caps) {
            out_caps = gst_caps_make_writable(out_caps);
            gst_caps_append(out_caps, gst_caps_copy(yuv_caps));
	}
    }
    return out_caps;
}

#if GST_CHECK_VERSION(1,0,0)
static inline GstCaps *
gst_vaapisink_get_caps(GstBaseSink *base_sink, GstCaps *filter)
{
    GstCaps *caps, *out_caps;

    caps = gst_vaapisink_get_caps_impl(base_sink);
    if (caps && filter) {
        out_caps = gst_caps_intersect_full(caps, filter,
            GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(caps);
    }
    else
        out_caps = caps;
    return out_caps;
}
#else
#define gst_vaapisink_get_caps gst_vaapisink_get_caps_impl
#endif

static void
update_colorimetry(GstVaapiSink *sink, GstVideoColorimetry *cinfo)
{
#if GST_CHECK_VERSION(1,0,0)
    if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_BT601))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_601;
    else if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_BT709))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_709;
    else if (gst_video_colorimetry_matches(cinfo,
            GST_VIDEO_COLORIMETRY_SMPTE240M))
        sink->color_standard = GST_VAAPI_COLOR_STANDARD_SMPTE_240M;
    else
        sink->color_standard = 0;

    GST_DEBUG("colorimetry %s", gst_video_colorimetry_to_string(cinfo));
#endif
}

static gboolean
gst_vaapisink_set_caps(GstBaseSink *base_sink, GstCaps *caps)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(base_sink);
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVideoInfo * const vip = GST_VAAPI_PLUGIN_BASE_SINK_PAD_INFO(sink);
    GstVaapiDisplay *display;
    guint win_width, win_height;

    if (!gst_vaapisink_ensure_display(sink))
        return FALSE;
    display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);

#if USE_DRM
    if (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink) == GST_VAAPI_DISPLAY_TYPE_DRM)
        return TRUE;
#endif

    if (!gst_vaapi_plugin_base_set_caps(plugin, caps, NULL))
        return FALSE;

    sink->video_width   = GST_VIDEO_INFO_WIDTH(vip);
    sink->video_height  = GST_VIDEO_INFO_HEIGHT(vip);
    sink->video_par_n   = GST_VIDEO_INFO_PAR_N(vip);
    sink->video_par_d   = GST_VIDEO_INFO_PAR_D(vip);
    GST_DEBUG("video pixel-aspect-ratio %d/%d",
              sink->video_par_n, sink->video_par_d);

    update_colorimetry(sink, &vip->colorimetry);
    gst_caps_replace(&sink->caps, caps);

    gst_vaapisink_ensure_rotation(sink, FALSE);

    gst_vaapisink_ensure_window_size(sink, &win_width, &win_height);
    if (sink->window) {
        if (!sink->foreign_window || sink->fullscreen)
            gst_vaapi_window_set_size(sink->window, win_width, win_height);
    }
    else {
        gst_vaapi_display_lock(display);
        gst_video_overlay_prepare_window_handle(GST_VIDEO_OVERLAY(sink));
        gst_vaapi_display_unlock(display);
        if (sink->window)
            return TRUE;
        if (!gst_vaapisink_ensure_window(sink, win_width, win_height))
            return FALSE;
        gst_vaapi_window_set_fullscreen(sink->window, sink->fullscreen);
        gst_vaapi_window_show(sink->window);
        gst_vaapi_window_get_size(sink->window, &win_width, &win_height);
    }
    sink->window_width  = win_width;
    sink->window_height = win_height;
    GST_DEBUG("window size %ux%u", win_width, win_height);

    return gst_vaapisink_ensure_render_rect(sink, win_width, win_height);
}

#if USE_GLX
static void
render_background(GstVaapiSink *sink)
{
    /* Original code from Mirco Muller (MacSlow):
       <http://cgit.freedesktop.org/~macslow/gl-gst-player/> */
    GLfloat fStartX = 0.0f;
    GLfloat fStartY = 0.0f;
    GLfloat fWidth  = (GLfloat)sink->window_width;
    GLfloat fHeight = (GLfloat)sink->window_height;

    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    {
        /* top third, darker grey to white */
        glColor3f(0.85f, 0.85f, 0.85f);
        glVertex3f(fStartX, fStartY, 0.0f);
        glColor3f(0.85f, 0.85f, 0.85f);
        glVertex3f(fStartX + fWidth, fStartY, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight / 3.0f, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + fHeight / 3.0f, 0.0f);

        /* middle third, just plain white */
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX + fWidth, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glVertex3f(fStartX, fStartY + 2.0f * fHeight / 3.0f, 0.0f);

        /* bottom third, white to lighter grey */
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glVertex3f(fStartX + fWidth, fStartY + 2.0f * fHeight / 3.0f, 0.0f);
        glColor3f(0.62f, 0.66f, 0.69f);
        glVertex3f(fStartX + fWidth, fStartY + fHeight, 0.0f);
        glColor3f(0.62f, 0.66f, 0.69f);
        glVertex3f(fStartX, fStartY + fHeight, 0.0f);
    }
    glEnd();
}

static void
render_frame(GstVaapiSink *sink, GstVaapiSurface *surface,
    const GstVaapiRectangle *surface_rect)
{
    const guint x1 = sink->display_rect.x;
    const guint x2 = sink->display_rect.x + sink->display_rect.width;
    const guint y1 = sink->display_rect.y;
    const guint y2 = sink->display_rect.y + sink->display_rect.height;
    gfloat tx1, tx2, ty1, ty2;
    guint width, height;

    if (surface_rect) {
        gst_vaapi_surface_get_size(surface, &width, &height);
        tx1 = (gfloat)surface_rect->x / width;
        ty1 = (gfloat)surface_rect->y / height;
        tx2 = (gfloat)(surface_rect->x + surface_rect->width) / width;
        ty2 = (gfloat)(surface_rect->y + surface_rect->height) / height;
    }
    else {
        tx1 = 0.0f;
        ty1 = 0.0f;
        tx2 = 1.0f;
        ty2 = 1.0f;
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glBegin(GL_QUADS);
    {
        glTexCoord2f(tx1, ty1); glVertex2i(x1, y1);
        glTexCoord2f(tx1, ty2); glVertex2i(x1, y2);
        glTexCoord2f(tx2, ty2); glVertex2i(x2, y2);
        glTexCoord2f(tx2, ty1); glVertex2i(x2, y1);
    }
    glEnd();
}

static void
render_reflection(GstVaapiSink *sink)
{
    const guint x1 = sink->display_rect.x;
    const guint x2 = sink->display_rect.x + sink->display_rect.width;
    const guint y1 = sink->display_rect.y;
    const guint rh = sink->display_rect.height / 5;
    GLfloat     ry = 1.0f - (GLfloat)rh / (GLfloat)sink->display_rect.height;

    glBegin(GL_QUADS);
    {
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(x1, y1);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(x2, y1);

        glColor4f(1.0f, 1.0f, 1.0f, 0.0f);
        glTexCoord2f(1.0f, ry); glVertex2i(x2, y1 + rh);
        glTexCoord2f(0.0f, ry); glVertex2i(x1, y1 + rh);
    }
    glEnd();
}

static gboolean
gst_vaapisink_ensure_texture(GstVaapiSink *sink, GstVaapiSurface *surface)
{
    GstVaapiDisplay * display = GST_VAAPI_PLUGIN_BASE_DISPLAY(sink);
    GstVideoRectangle tex_rect, dis_rect, out_rect;
    guint width, height;

    if (sink->texture)
        return TRUE;

    gst_vaapi_surface_get_size(surface, &width, &height);
    tex_rect.x = 0;
    tex_rect.y = 0;
    tex_rect.w = width;
    tex_rect.h = height;

    gst_vaapi_display_get_size(display, &width, &height);
    dis_rect.x = 0;
    dis_rect.y = 0;
    dis_rect.w = width;
    dis_rect.h = height;

    gst_video_sink_center_rect(tex_rect, dis_rect, &out_rect, TRUE);

    /* XXX: use surface size for now since some VA drivers have issues
       with downscaling to the provided texture size. i.e. we should be
       using the resulting out_rect size, which preserves the aspect
       ratio of the surface */
    width = tex_rect.w;
    height = tex_rect.h;
    GST_INFO("texture size %ux%u", width, height);

    sink->texture = gst_vaapi_texture_new(display,
        GL_TEXTURE_2D, GL_BGRA, width, height);
    return sink->texture != NULL;
}

static gboolean
gst_vaapisink_show_frame_glx(
    GstVaapiSink               *sink,
    GstVaapiSurface            *surface,
    const GstVaapiRectangle    *surface_rect,
    guint                       flags
)
{
    GstVaapiWindowGLX * const window = GST_VAAPI_WINDOW_GLX(sink->window);
    GLenum target;
    GLuint texture;

    gst_vaapi_window_glx_make_current(window);
    if (!gst_vaapisink_ensure_texture(sink, surface))
        goto error_create_texture;
    if (!gst_vaapi_texture_put_surface(sink->texture, surface, flags))
        goto error_transfer_surface;

    target  = gst_vaapi_texture_get_target(sink->texture);
    texture = gst_vaapi_texture_get_id(sink->texture);
    if (target != GL_TEXTURE_2D || !texture)
        return FALSE;

    if (sink->use_reflection)
        render_background(sink);

    glEnable(target);
    glBindTexture(target, texture);
    {
        if (sink->use_reflection) {
            glPushMatrix();
            glRotatef(20.0f, 0.0f, 1.0f, 0.0f);
            glTranslatef(50.0f, 0.0f, 0.0f);
        }
        render_frame(sink, surface, surface_rect);
        if (sink->use_reflection) {
            glPushMatrix();
            glTranslatef(0.0, (GLfloat)sink->display_rect.height + 5.0f, 0.0f);
            render_reflection(sink);
            glPopMatrix();
            glPopMatrix();
        }
    }
    glBindTexture(target, 0);
    glDisable(target);
    gst_vaapi_window_glx_swap_buffers(window);
    return TRUE;

    /* ERRORS */
error_create_texture:
    {
        GST_DEBUG("could not create VA/GLX texture");
        return FALSE;
    }
error_transfer_surface:
    {
        GST_DEBUG("could not transfer VA surface to texture");
        return FALSE;
    }
}
#endif

static inline gboolean
gst_vaapisink_put_surface(
    GstVaapiSink               *sink,
    GstVaapiSurface            *surface,
    const GstVaapiRectangle    *surface_rect,
    guint                       flags
)
{
    if (!gst_vaapi_window_put_surface(sink->window, surface,
                surface_rect, &sink->display_rect, flags)) {
        GST_DEBUG("could not render VA surface");
        return FALSE;
    }
    return TRUE;
}

static GstFlowReturn
gst_vaapisink_show_frame(GstBaseSink *base_sink, GstBuffer *src_buffer)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;
    GstBuffer *buffer;
    guint flags;
    gboolean success;
    GstVaapiRectangle *surface_rect = NULL;
    int i;
#if GST_CHECK_VERSION(1,0,0)
    GstVaapiRectangle tmp_rect;
#endif
    GstFlowReturn ret;

#if GST_CHECK_VERSION(1,0,0)
    GstVideoCropMeta * const crop_meta =
        gst_buffer_get_video_crop_meta(src_buffer);
    if (crop_meta) {
        surface_rect = &tmp_rect;
        surface_rect->x = crop_meta->x;
        surface_rect->y = crop_meta->y;
        surface_rect->width = crop_meta->width;
        surface_rect->height = crop_meta->height;
    }
#endif

    ret = gst_vaapi_plugin_base_get_input_buffer(GST_VAAPI_PLUGIN_BASE(sink),
        src_buffer, &buffer);
    if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_SUPPORTED)
        return ret;

    meta = gst_buffer_get_vaapi_video_meta(buffer);
    GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE(sink,
        gst_vaapi_video_meta_get_display(meta));

    if (!sink->window)
        goto error;

    gst_vaapisink_ensure_rotation(sink, TRUE);

    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface)
        goto error;

    GST_DEBUG("render surface %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(gst_vaapi_surface_get_id(surface)));

    if (!surface_rect)
        surface_rect = (GstVaapiRectangle *)
            gst_vaapi_video_meta_get_render_rect(meta);

    if (surface_rect)
        GST_DEBUG("render rect (%d,%d), size %ux%u",
                  surface_rect->x, surface_rect->y,
                  surface_rect->width, surface_rect->height);

    flags = gst_vaapi_video_meta_get_render_flags(meta);

    /* Append default color standard obtained from caps if none was
       available on a per-buffer basis */
    if (!(flags & GST_VAAPI_COLOR_STANDARD_MASK))
        flags |= sink->color_standard;

    if (!gst_vaapi_apply_composition(surface, src_buffer))
        GST_WARNING("could not update subtitles");

    switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink)) {
#if USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
        success = TRUE;
        break;
#endif
#if USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
        if (!sink->use_glx)
            goto put_surface_x11;
        success = gst_vaapisink_show_frame_glx(sink, surface, surface_rect,
            flags);
        break;
#endif
#if USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
    put_surface_x11:
        success = gst_vaapisink_put_surface(sink, surface, surface_rect, flags);
        break;
#endif
#if USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
        success = gst_vaapisink_put_surface(sink, surface, surface_rect, flags);
        break;
#endif
    default:
        GST_ERROR("unsupported display type %d",
                  GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink));
        success = FALSE;
        break;
    }
    if (!success)
        goto error;

    /* Retain VA surface until the next one is displayed */
#if !USE_WAYLAND
    if (sink->use_overlay)
#endif
    {
        gst_buffer_unref(sink->video_buffer[RETAIN_BUFFER_COUNT-1]);
        for (i=RETAIN_BUFFER_COUNT-1; i>0; i--) {
            sink->video_buffer[i] = sink->video_buffer[i-1];
        }
        sink->video_buffer[0] = gst_buffer_ref(buffer);
    }

    gst_buffer_unref(buffer);

#if USE_WAYLAND
    if (sink->display_type == GST_VAAPI_DISPLAY_TYPE_WAYLAND) {
        GstStructure *s;
        GstMessage *msg;
        static int frame_num = 0;
        GST_DEBUG ("post \"frame-rendered\" element message with frame-num(%d)", frame_num);
        s = gst_structure_new ("frame-rendered",
            "frame-num", G_TYPE_INT, frame_num++,
            NULL);
        msg = gst_message_new_element (GST_OBJECT (sink), s);
        gst_element_post_message (GST_ELEMENT (sink), msg);
    }
#endif

    return GST_FLOW_OK;

error:
    gst_buffer_unref(buffer);
    return GST_FLOW_EOS;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapisink_propose_allocation(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(base_sink);

    if (!gst_vaapi_plugin_base_propose_allocation(plugin, query))
        return FALSE;

    gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
    return TRUE;
}
#else
static GstFlowReturn
gst_vaapisink_buffer_alloc(GstBaseSink *base_sink, guint64 offset, guint size,
    GstCaps *caps, GstBuffer **outbuf_ptr)
{
    return gst_vaapi_plugin_base_allocate_input_buffer(
        GST_VAAPI_PLUGIN_BASE(base_sink), caps, outbuf_ptr);
}
#endif

static gboolean
gst_vaapisink_query(GstBaseSink *base_sink, GstQuery *query)
{
    GstVaapiSink * const sink = GST_VAAPISINK(base_sink);

    GST_INFO_OBJECT(sink, "query type %s", GST_QUERY_TYPE_NAME(query));

    if (gst_vaapi_reply_to_query(query, GST_VAAPI_PLUGIN_BASE_DISPLAY(sink))) {
        GST_DEBUG("sharing display %p", GST_VAAPI_PLUGIN_BASE_DISPLAY(sink));
        return TRUE;
    }

    return GST_BASE_SINK_CLASS(gst_vaapisink_parent_class)->query(base_sink,
        query);
}

static void
gst_vaapisink_finalize(GObject *object)
{
    gst_vaapisink_destroy(GST_VAAPISINK(object));

    gst_vaapi_plugin_base_finalize(GST_VAAPI_PLUGIN_BASE(object));
    G_OBJECT_CLASS(gst_vaapisink_parent_class)->finalize(object);
}

static void
gst_vaapisink_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);
#if USE_WAYLAND
    struct wl_display *wld_display = NULL;
#endif

    switch (prop_id) {
#if USE_WAYLAND
    case PROP_DISPLAY:
        wld_display = g_value_get_pointer(value);
        if(wld_display==NULL)
            return;
        sink->display = gst_vaapi_display_wayland_new_with_display(wld_display);
        sink->display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
        break;
#endif
    case PROP_DISPLAY_TYPE:
        gst_vaapi_plugin_base_set_display_type(GST_VAAPI_PLUGIN_BASE(sink),
            g_value_get_enum(value));
        break;
    case PROP_FULLSCREEN:
        sink->fullscreen = g_value_get_boolean(value);
        break;
    case PROP_SYNCHRONOUS:
        sink->synchronous = g_value_get_boolean(value);
        break;
    case PROP_USE_GLX:
        sink->use_glx = g_value_get_boolean(value);
        break;
    case PROP_USE_REFLECTION:
        sink->use_reflection = g_value_get_boolean(value);
        break;
    case PROP_ROTATION:
        sink->rotation_req = g_value_get_enum(value);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        sink->keep_aspect = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSink * const sink = GST_VAAPISINK(object);

    switch (prop_id) {
#if USE_WAYLAND
    case PROP_DISPLAY:
        g_value_set_pointer(value,sink->display);
        break;
#endif
    case PROP_DISPLAY_TYPE:
        g_value_set_enum(value, GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE(sink));
        break;
    case PROP_FULLSCREEN:
        g_value_set_boolean(value, sink->fullscreen);
        break;
    case PROP_SYNCHRONOUS:
        g_value_set_boolean(value, sink->synchronous);
        break;
    case PROP_USE_GLX:
        g_value_set_boolean(value, sink->use_glx);
        break;
    case PROP_USE_REFLECTION:
        g_value_set_boolean(value, sink->use_reflection);
        break;
    case PROP_ROTATION:
        g_value_set_enum(value, sink->rotation);
        break;
    case PROP_FORCE_ASPECT_RATIO:
        g_value_set_boolean(value, sink->keep_aspect);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapisink_class_init(GstVaapiSinkClass *klass)
{
    GObjectClass * const     object_class   = G_OBJECT_CLASS(klass);
    GstElementClass * const  element_class  = GST_ELEMENT_CLASS(klass);
    GstBaseSinkClass * const basesink_class = GST_BASE_SINK_CLASS(klass);
    GstVaapiPluginBaseClass * const base_plugin_class =
        GST_VAAPI_PLUGIN_BASE_CLASS(klass);
    GstPadTemplate *pad_template;

    GST_DEBUG_CATEGORY_INIT(gst_debug_vaapisink,
                            GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

    gst_vaapi_plugin_base_class_init(base_plugin_class);
    base_plugin_class->has_interface    = gst_vaapisink_has_interface;
    base_plugin_class->display_changed  = gst_vaapisink_display_changed;

    object_class->finalize       = gst_vaapisink_finalize;
    object_class->set_property   = gst_vaapisink_set_property;
    object_class->get_property   = gst_vaapisink_get_property;

    basesink_class->start        = gst_vaapisink_start;
    basesink_class->stop         = gst_vaapisink_stop;
    basesink_class->get_caps     = gst_vaapisink_get_caps;
    basesink_class->set_caps     = gst_vaapisink_set_caps;
    basesink_class->preroll      = gst_vaapisink_show_frame;
    basesink_class->render       = gst_vaapisink_show_frame;
    basesink_class->query        = gst_vaapisink_query;
#if GST_CHECK_VERSION(1,0,0)
    basesink_class->propose_allocation = gst_vaapisink_propose_allocation;
#else
    basesink_class->buffer_alloc = gst_vaapisink_buffer_alloc;
#endif

    gst_element_class_set_static_metadata(element_class,
        "VA-API sink",
        "Sink/Video",
        GST_PLUGIN_DESC,
        "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

    pad_template = gst_static_pad_template_get(&gst_vaapisink_sink_factory);
    gst_element_class_add_pad_template(element_class, pad_template);

#if USE_WAYLAND
    /**
     * GstVaapiSink:wl-display:
     * Pointer of wayland display connection
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_pointer("wl-display",
                             "wl-display",
                             "the pointer of wayland's display",
                             G_PARAM_READWRITE));
#endif

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_TYPE,
         g_param_spec_enum("display",
                           "display type",
                           "display type to use",
                           GST_VAAPI_TYPE_DISPLAY_TYPE,
                           GST_VAAPI_DISPLAY_TYPE_ANY,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if USE_GLX
    g_object_class_install_property
        (object_class,
         PROP_USE_GLX,
         g_param_spec_boolean("use-glx",
                              "OpenGL rendering",
                              "Enables OpenGL rendering",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (object_class,
         PROP_USE_REFLECTION,
         g_param_spec_boolean("use-reflection",
                              "Reflection effect",
                              "Enables OpenGL reflection effect",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

    g_object_class_install_property
        (object_class,
         PROP_FULLSCREEN,
         g_param_spec_boolean("fullscreen",
                              "Fullscreen",
                              "Requests window in fullscreen state",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiSink:synchronous:
     *
     * When enabled, runs the X display in synchronous mode. Note that
     * this is used only for debugging.
     */
    g_object_class_install_property
        (object_class,
         PROP_SYNCHRONOUS,
         g_param_spec_boolean("synchronous",
                              "Synchronous mode",
                              "Toggles X display synchronous mode",
                              FALSE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiSink:rotation:
     *
     * The VA display rotation mode, expressed as a #GstVaapiRotation.
     */
    g_object_class_install_property
        (object_class,
         PROP_ROTATION,
         g_param_spec_enum(GST_VAAPI_DISPLAY_PROP_ROTATION,
                           "rotation",
                           "The display rotation mode",
                           GST_VAAPI_TYPE_ROTATION,
                           DEFAULT_ROTATION,
                           G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /**
     * GstVaapiSink:force-aspect-ratio:
     *
     * When enabled, scaling respects video aspect ratio; when disabled, the
     * video is distorted to fit the window.
     */
    g_object_class_install_property
        (object_class,
         PROP_FORCE_ASPECT_RATIO,
         g_param_spec_boolean("force-aspect-ratio",
                              "Force aspect ratio",
                              "When enabled, scaling will respect original aspect ratio",
                              TRUE,
                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_vaapisink_init(GstVaapiSink *sink)
{
    int i = 0;
    GstVaapiPluginBase * const plugin = GST_VAAPI_PLUGIN_BASE(sink);

    gst_vaapi_plugin_base_init(plugin, GST_CAT_DEFAULT);
    gst_vaapi_plugin_base_set_display_type(plugin, DEFAULT_DISPLAY_TYPE);

    sink->caps           = NULL;
    sink->window         = NULL;
    sink->window_width   = 0;
    sink->window_height  = 0;
    sink->texture        = NULL;
    sink->video_width    = 0;
    sink->video_height   = 0;
    sink->video_par_n    = 1;
    sink->video_par_d    = 1;
    sink->foreign_window = FALSE;
    sink->fullscreen     = FALSE;
    sink->synchronous    = FALSE;
    sink->rotation       = DEFAULT_ROTATION;
    sink->rotation_req   = DEFAULT_ROTATION;
    sink->use_reflection = FALSE;
    sink->use_overlay    = FALSE;
    sink->use_rotation   = FALSE;
    sink->keep_aspect    = TRUE;
    gst_video_info_init(&sink->video_info);

    for (i=0; i<RETAIN_BUFFER_COUNT; i++) {
        sink->video_buffer[i]   = NULL;
    }

}
