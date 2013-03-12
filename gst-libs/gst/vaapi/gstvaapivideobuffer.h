/*
 *  gstvaapivideobuffer.h - Gstreamer/VA video buffer
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_VIDEO_BUFFER_H
#define GST_VAAPI_VIDEO_BUFFER_H

#include <gst/video/gstsurfacebuffer.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfaceproxy.h>
#include <gst/vaapi/gstvaapivideopool.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_BUFFER \
    (gst_vaapi_video_buffer_get_type())

#define GST_VAAPI_VIDEO_BUFFER(obj)                             \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_VIDEO_BUFFER,    \
                                GstVaapiVideoBuffer))

#define GST_VAAPI_VIDEO_BUFFER_CLASS(klass)                     \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_VIDEO_BUFFER,       \
                             GstVaapiVideoBufferClass))

#define GST_VAAPI_IS_VIDEO_BUFFER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_BUFFER))

#define GST_VAAPI_IS_VIDEO_BUFFER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_BUFFER))

#define GST_VAAPI_VIDEO_BUFFER_GET_CLASS(obj)                   \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_VIDEO_BUFFER,     \
                               GstVaapiVideoBufferClass))

typedef struct _GstVaapiVideoBuffer             GstVaapiVideoBuffer;
typedef struct _GstVaapiVideoBufferPrivate      GstVaapiVideoBufferPrivate;
typedef struct _GstVaapiVideoBufferClass        GstVaapiVideoBufferClass;

/**
 * GstVaapiVideoBuffer:
 *
 * A #GstBuffer holding video objects (#GstVaapiSurface and #GstVaapiImage).
 */
struct _GstVaapiVideoBuffer {
    /*< private >*/
    GstSurfaceBuffer parent_instance;

    GstVaapiVideoBufferPrivate *priv;
};

/**
 * GstVaapiVideoBufferClass:
 *
 * A #GstBuffer holding video objects
 */
struct _GstVaapiVideoBufferClass {
    /*< private >*/
    GstSurfaceBufferClass parent_class;
};

GType
gst_vaapi_video_buffer_get_type(void) G_GNUC_CONST;

GstVaapiDisplay *
gst_vaapi_video_buffer_get_display(GstVaapiVideoBuffer *buffer);

GstVaapiImage *
gst_vaapi_video_buffer_get_image(GstVaapiVideoBuffer *buffer);

void
gst_vaapi_video_buffer_set_image(
    GstVaapiVideoBuffer *buffer,
    GstVaapiImage       *image
);

gboolean
gst_vaapi_video_buffer_set_image_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
);

GstVaapiSurface *
gst_vaapi_video_buffer_get_surface(GstVaapiVideoBuffer *buffer);

void
gst_vaapi_video_buffer_set_surface(
    GstVaapiVideoBuffer *buffer,
    GstVaapiSurface     *surface
);

gboolean
gst_vaapi_video_buffer_set_surface_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
);

GstVaapiSurfaceProxy *
gst_vaapi_video_buffer_get_surface_proxy(GstVaapiVideoBuffer *buffer);

void
gst_vaapi_video_buffer_set_surface_proxy(
    GstVaapiVideoBuffer  *buffer,
    GstVaapiSurfaceProxy *proxy
);

guint
gst_vaapi_video_buffer_get_render_flags(GstVaapiVideoBuffer *buffer);

void
gst_vaapi_video_buffer_set_render_flags(GstVaapiVideoBuffer *buffer, guint flags);

gboolean
gst_vaapi_video_buffer_ensure_pointer(GstVaapiVideoBuffer *buffer);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_BUFFER_H */
