/*
 *  gstvaapivideobufferpool.h - Gstreamer/VA video buffer pool
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPI_VIDEO_BUFFER_POOL_H
#define GST_VAAPI_VIDEO_BUFFER_POOL_H

#include <gst/video/gstvideopool.h>
#include <gst/vaapi/gstvaapidisplay.h>

G_BEGIN_DECLS

#define GST_VAAPI_TYPE_VIDEO_BUFFER_POOL \
    (gst_vaapi_video_buffer_pool_get_type())

#define GST_VAAPI_VIDEO_BUFFER_POOL(obj)                \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
        GST_VAAPI_TYPE_VIDEO_BUFFER_POOL,               \
        GstVaapiVideoBufferPool))

#define GST_VAAPI_VIDEO_BUFFER_POOL_CLASS(klass)        \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
        GST_VAAPI_TYPE_VIDEO_BUFFER_POOL,               \
        GstVaapiVideoBufferPoolClass))

#define GST_VAAPI_IS_VIDEO_BUFFER_POOL(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_VIDEO_BUFFER_POOL))

#define GST_VAAPI_IS_VIDEO_BUFFER_POOL_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_VIDEO_BUFFER_POOL))

typedef struct _GstVaapiVideoBufferPool         GstVaapiVideoBufferPool;
typedef struct _GstVaapiVideoBufferPoolClass    GstVaapiVideoBufferPoolClass;
typedef struct _GstVaapiVideoBufferPoolPrivate  GstVaapiVideoBufferPoolPrivate;

/**
 * GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META:
 *
 * An option that can be activated on bufferpool to request vaapi
 * video metadata on buffers from the pool.
 */
#define GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META "GstBufferPoolOptionVaapiVideoMeta"

/**
 *
 * GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META:
 *
 * An option that can be activated on bufferpool to request gl texture
 * upload on buffers from the pool.
 *
 * When this option is enabled on the bufferpool,
 * #GST_BUFFER_POOL_OPTION_VIDEO_META should also be enabled.
 */
#ifndef GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META
#define GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META \
    "GstBufferPoolOptionVideoGLTextureUploadMeta"
#endif

/**
 * GstVaapiVideoBufferPool:
 *
 * A VA video buffer pool object.
 */
struct _GstVaapiVideoBufferPool {
    GstBufferPool parent_instance;

    /*< private >*/
    GstVaapiVideoBufferPoolPrivate *priv;
};

/**
 * GstVaapiVideoBufferPoolClass:
 *
 * A VA video buffer pool class.
 */
struct _GstVaapiVideoBufferPoolClass {
    GstBufferPoolClass parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_video_buffer_pool_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstBufferPool *
gst_vaapi_video_buffer_pool_new(GstVaapiDisplay *display) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_BUFFER_POOL_H */
