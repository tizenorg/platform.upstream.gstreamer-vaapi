/*
 *  gstvaapiwindow_wayland.c - VA/Wayland window abstraction
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

/**
 * SECTION:gstvaapiwindow_wayland
 * @short_description: VA/Wayland window abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_wayland.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiutils.h"

#define DEBUG 1
#include "gstvaapidebug.h"

#define GST_VAAPI_WINDOW_WAYLAND_CAST(obj) \
    ((GstVaapiWindowWayland *)(obj))

#define GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(obj) \
    (&GST_VAAPI_WINDOW_WAYLAND_CAST(obj)->priv)

typedef struct _GstVaapiWindowWaylandPrivate    GstVaapiWindowWaylandPrivate;
typedef struct _GstVaapiWindowWaylandClass      GstVaapiWindowWaylandClass;

struct _GstVaapiWindowWaylandPrivate {
    struct wl_shell_surface    *shell_surface;
    struct wl_surface          *surface;
    struct wl_buffer           *buffer;
    struct wl_region           *opaque_region;
    struct wl_event_queue      *event_queue;
    guint                       redraw_pending          : 1;
    guint                       is_shown                : 1;
    guint                       fullscreen_on_show      : 1;
};

/**
 * GstVaapiWindowWayland:
 *
 * A Wayland window abstraction.
 */
struct _GstVaapiWindowWayland {
    /*< private >*/
    GstVaapiWindow parent_instance;

    GstVaapiWindowWaylandPrivate priv;
};

/**
 * GstVaapiWindowWaylandClass:
 *
 * An Wayland #Window wrapper class.
 */
struct _GstVaapiWindowWaylandClass {
    /*< private >*/
    GstVaapiWindowClass parent_class;
};

static gboolean
gst_vaapi_window_wayland_show(GstVaapiWindow *window)
{
    GST_WARNING("unimplemented GstVaapiWindowWayland::show()");

    return TRUE;
}

static gboolean
gst_vaapi_window_wayland_hide(GstVaapiWindow *window)
{
    GST_WARNING("unimplemented GstVaapiWindowWayland::hide()");

    return TRUE;
}

static gboolean
gst_vaapi_window_wayland_sync(GstVaapiWindow *window)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);

    if (priv->redraw_pending) {
        struct wl_display * const wl_display =
            GST_VAAPI_OBJECT_WL_DISPLAY(window);

        do {
            if (wl_display_dispatch_queue(wl_display, priv->event_queue) < 0)
                return FALSE;
        } while (priv->redraw_pending);
    }
    return TRUE;
}

static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
            uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
                 uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping,
    handle_configure,
    handle_popup_done
};

static gboolean
gst_vaapi_window_wayland_set_fullscreen(GstVaapiWindow *window, gboolean fullscreen)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);

    if (!priv->is_shown) {
        priv->fullscreen_on_show = fullscreen;
        return TRUE;
    }

    if (!fullscreen)
        wl_shell_surface_set_toplevel(priv->shell_surface);
    else {
        wl_shell_surface_set_fullscreen(
            priv->shell_surface,
            WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE,
            0,
            NULL
        );
    }

    return TRUE;
}

static gboolean
gst_vaapi_window_wayland_create(
    GstVaapiWindow *window,
    guint          *width,
    guint          *height
)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);
    GstVaapiDisplayWaylandPrivate * const priv_display =
        GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(GST_VAAPI_OBJECT_DISPLAY(window));

    GST_DEBUG("create window, size %ux%u", *width, *height);

    void *wld_surface  = (void*)GST_VAAPI_OBJECT_ID(window);
    if (!wld_surface) {
        g_return_val_if_fail(priv_display->compositor != NULL, FALSE);
        g_return_val_if_fail(priv_display->shell != NULL, FALSE);
    }
    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    priv->event_queue = wl_display_create_queue(priv_display->wl_display);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    if (!priv->event_queue)
        return FALSE;

    if(wld_surface){
        priv->surface = wld_surface;
        window->use_foreign_window = TRUE;
    } else {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        priv->surface = wl_compositor_create_surface(priv_display->compositor);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        window->use_foreign_window = FALSE;
    }
    if (!priv->surface)
        return FALSE;
    wl_proxy_set_queue((struct wl_proxy *)priv->surface, priv->event_queue);
    if (window->use_foreign_window == FALSE) {

        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        priv->shell_surface =
            wl_shell_get_shell_surface(priv_display->shell, priv->surface);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        if (!priv->shell_surface)
            return FALSE;
        wl_proxy_set_queue((struct wl_proxy *)priv->shell_surface,
            priv->event_queue);

        wl_shell_surface_add_listener(priv->shell_surface,
                                      &shell_surface_listener, priv);
        wl_shell_surface_set_toplevel(priv->shell_surface);

        if (priv->fullscreen_on_show)
            gst_vaapi_window_wayland_set_fullscreen(window, TRUE);
    }
    priv->redraw_pending = FALSE;
    priv->is_shown = TRUE;

    return TRUE;
}

static void
gst_vaapi_window_wayland_destroy(GstVaapiWindow * window)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);

    if (priv->shell_surface) {
        wl_shell_surface_destroy(priv->shell_surface);
        priv->shell_surface = NULL;
    }

    if (priv->surface) {
        if (!window->use_foreign_window) {
            wl_surface_destroy(priv->surface);
        }
        priv->surface = NULL;
    }

    if (priv->buffer) {
        wl_buffer_destroy(priv->buffer);
        priv->buffer = NULL;
    }

    if (priv->event_queue) {
        wl_event_queue_destroy(priv->event_queue);
        priv->event_queue = NULL;
    }
}

static gboolean
gst_vaapi_window_wayland_resize(
    GstVaapiWindow * window,
    guint            width,
    guint            height
)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);
    GstVaapiDisplayWaylandPrivate * const priv_display =
        GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(GST_VAAPI_OBJECT_DISPLAY(window));

    GST_DEBUG("resize window, new size %ux%u", width, height);

    if (window->use_foreign_window == FALSE) {
        if (priv->opaque_region)
            wl_region_destroy(priv->opaque_region);
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        priv->opaque_region = wl_compositor_create_region(priv_display->compositor);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        wl_region_add(priv->opaque_region, 0, 0, width, height);
    }
    return TRUE;
}

static void
frame_redraw_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    GstVaapiWindowWaylandPrivate * const priv = data;
    priv->buffer = NULL;
    wl_callback_destroy(callback);
    priv->redraw_pending = FALSE;
}

static const struct wl_callback_listener frame_callback_listener = {
    frame_redraw_callback
};

static gboolean
gst_vaapi_window_wayland_render(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect,
    guint                    flags
)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(window);
    struct wl_display * const wl_display = GST_VAAPI_OBJECT_WL_DISPLAY(window);
    struct wl_buffer *buffer;
    struct wl_callback *callback;
    guint width, height, va_flags;
    VASurfaceID surface_id;
    VAStatus status;

    /* XXX: use VPP to support unusual source and destination rectangles */
    gst_vaapi_surface_get_size(surface, &width, &height);
    if (src_rect->x      != 0     ||
        src_rect->y      != 0     ||
        src_rect->width  != width ||
        src_rect->height != height) {
        GST_ERROR("unsupported source rectangle for rendering");
        return FALSE;
    }

    if (0 && (dst_rect->width != width || dst_rect->height != height)) {
        GST_ERROR("unsupported target rectangle for rendering");
        return FALSE;
    }

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    if (surface_id == VA_INVALID_ID)
        return FALSE;

    /* Wait for the previous frame to complete redraw */
    if (!gst_vaapi_window_wayland_sync(window))
        return FALSE;

    /* XXX: use VA/VPP for other filters */
    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    va_flags = from_GstVaapiSurfaceRenderFlags(flags);
    status = vaGetSurfaceBufferWl(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface_id,
        va_flags & (VA_TOP_FIELD|VA_BOTTOM_FIELD),
        &buffer
    );
    if (status == VA_STATUS_ERROR_FLAG_NOT_SUPPORTED) {
        /* XXX: de-interlacing flags not supported, try with VPP? */
        status = vaGetSurfaceBufferWl(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            surface_id,
            VA_FRAME_PICTURE,
            &buffer
        );
    }
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    if (!vaapi_check_status(status, "vaGetSurfaceBufferWl()"))
        return FALSE;

    /* XXX: attach to the specified target rectangle */
    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    wl_surface_attach(priv->surface, buffer, 0, 0);
    wl_surface_damage(priv->surface, 0, 0, width, height);

    if (priv->opaque_region) {
        wl_surface_set_opaque_region(priv->surface, priv->opaque_region);
        wl_region_destroy(priv->opaque_region);
        priv->opaque_region = NULL;
    }

    priv->redraw_pending = TRUE;
    priv->buffer = buffer;
    callback = wl_surface_frame(priv->surface);
    wl_callback_add_listener(callback, &frame_callback_listener, priv);

    wl_surface_commit(priv->surface);
    wl_display_flush(wl_display);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    return TRUE;
}

static void
gst_vaapi_window_wayland_class_init(GstVaapiWindowWaylandClass * klass)
{
    GstVaapiObjectClass * const object_class = GST_VAAPI_OBJECT_CLASS(klass);
    GstVaapiWindowClass * const window_class = GST_VAAPI_WINDOW_CLASS(klass);

    object_class->finalize = (GstVaapiObjectFinalizeFunc)
        gst_vaapi_window_wayland_destroy;

    window_class->create         = gst_vaapi_window_wayland_create;
    window_class->show           = gst_vaapi_window_wayland_show;
    window_class->hide           = gst_vaapi_window_wayland_hide;
    window_class->render         = gst_vaapi_window_wayland_render;
    window_class->resize         = gst_vaapi_window_wayland_resize;
    window_class->set_fullscreen = gst_vaapi_window_wayland_set_fullscreen;
}

#define gst_vaapi_window_wayland_finalize \
    gst_vaapi_window_wayland_destroy

GST_VAAPI_OBJECT_DEFINE_CLASS_WITH_CODE(
    GstVaapiWindowWayland,
    gst_vaapi_window_wayland,
    gst_vaapi_window_wayland_class_init(&g_class))

/**
 * gst_vaapi_window_wayland_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_wayland_new(
    GstVaapiDisplay *display,
    guint            width,
    guint            height
)
{
    GST_DEBUG("new window, size %ux%u", width, height);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_WAYLAND(display), NULL);

    return gst_vaapi_window_new(GST_VAAPI_WINDOW_CLASS(
        gst_vaapi_window_wayland_class()), display, width, height);
}

/**
 * gst_vaapi_window_wayland_new_with_id:
 * @display: a #GstVaapiDisplay
 * @id:a wl_surface #Surface id
 *
 * Creates a window with the specified @id. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_wayland_new_with_id(
    GstVaapiDisplay *display,
    guint            id
)
{
    GST_INFO("new window from id 0x%08x,vaapidisplay=%p", id,display);
    struct wl_display *wl_dpy = gst_vaapi_display_wayland_get_display((GstVaapiDisplayWayland*)display);
    g_return_val_if_fail(display, NULL);
    g_return_val_if_fail(id  > 0, NULL);
    return gst_vaapi_window_new_from_native(GST_VAAPI_WINDOW_CLASS(
            gst_vaapi_window_wayland_class()), display, GINT_TO_POINTER(id));

}

