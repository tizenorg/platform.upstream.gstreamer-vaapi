/*
 *  gstvaapivideobuffer.c - Gst VA video buffer
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

/**
 * SECTION:gstvaapivideobuffer
 * @short_description: VA video buffer for GStreamer
 */

#include "sysdeps.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapivideobuffer_priv.h"
#include "gstvaapiimagepool.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapiobject_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoBuffer, gst_vaapi_video_buffer, GST_TYPE_SURFACE_BUFFER);

#define GST_VAAPI_VIDEO_BUFFER_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_VIDEO_BUFFER,	\
                                 GstVaapiVideoBufferPrivate))

struct _GstVaapiVideoBufferPrivate {
    GstVaapiDisplay            *display;
    GstVaapiVideoPool          *image_pool;
    GstVaapiImage              *image;
    GstVaapiVideoPool          *surface_pool;
    GstVaapiSurface            *surface;
    GstVaapiSurfaceProxy       *proxy;
    GstBuffer                  *buffer;
    guint                       render_flags;
};

static void
set_display(GstVaapiVideoBuffer *buffer, GstVaapiDisplay *display)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;

    g_clear_object(&priv->display);

    if (display)
        priv->display = g_object_ref(display);
}

static inline void
set_image(GstVaapiVideoBuffer *buffer, GstVaapiImage *image)
{
    buffer->priv->image = g_object_ref(image);
    set_display(buffer, GST_VAAPI_OBJECT_DISPLAY(image));
}

static inline void
set_surface(GstVaapiVideoBuffer *buffer, GstVaapiSurface *surface)
{
    buffer->priv->surface = g_object_ref(surface);
    set_display(buffer, GST_VAAPI_OBJECT_DISPLAY(surface));
}

static void
gst_vaapi_video_buffer_destroy_image(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;

    if (priv->image) {
        if (priv->image_pool)
            gst_vaapi_video_pool_put_object(priv->image_pool, priv->image);
        g_object_unref(priv->image);
        priv->image = NULL;
    }

    g_clear_object(&priv->image_pool);
}

static void
gst_vaapi_video_buffer_destroy_surface(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;

    g_clear_object(&priv->proxy);

    if (priv->surface) {
        if (priv->surface_pool)
            gst_vaapi_video_pool_put_object(priv->surface_pool, priv->surface);
        g_object_unref(priv->surface);
        priv->surface = NULL;
    }

    g_clear_object(&priv->surface_pool);

    if (priv->buffer) {
        gst_buffer_unref(priv->buffer);
        priv->buffer = NULL;
    }
}

static void
gst_vaapi_video_buffer_finalize(GstMiniObject *object)
{
    GstVaapiVideoBuffer * const buffer = GST_VAAPI_VIDEO_BUFFER(object);
    GstMiniObjectClass *parent_class;

    gst_vaapi_video_buffer_destroy_image(buffer);
    gst_vaapi_video_buffer_destroy_surface(buffer);

    set_display(buffer, NULL);

    parent_class = GST_MINI_OBJECT_CLASS(gst_vaapi_video_buffer_parent_class);
    if (parent_class->finalize)
        parent_class->finalize(object);
}

static void
gst_vaapi_video_buffer_class_init(GstVaapiVideoBufferClass *klass)
{
    GstMiniObjectClass * const object_class = GST_MINI_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiVideoBufferPrivate));

    object_class->finalize = gst_vaapi_video_buffer_finalize;
}

static void
gst_vaapi_video_buffer_init(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate *priv;

    priv                = GST_VAAPI_VIDEO_BUFFER_GET_PRIVATE(buffer);
    buffer->priv        = priv;
    priv->display       = NULL;
    priv->image_pool    = NULL;
    priv->image         = NULL;
    priv->surface_pool  = NULL;
    priv->surface       = NULL;
    priv->proxy         = NULL;
    priv->buffer        = NULL;
    priv->render_flags  = 0;
}

static inline gboolean
gst_vaapi_video_buffer_is_a(GstBuffer *buffer, GType type)
{
    return G_TYPE_CHECK_INSTANCE_TYPE(buffer, type);
}

static inline gpointer
_gst_vaapi_video_buffer_typed_new(GType type)
{
    g_return_val_if_fail(g_type_is_a(type, GST_VAAPI_TYPE_VIDEO_BUFFER), NULL);

    return gst_mini_object_new(type);
}

/**
 * gst_vaapi_video_buffer_typed_new:
 * @display: a #GstVaapiDisplay
 *
 * Creates an empty #GstBuffer. The caller is responsible for completing
 * the initialization of the buffer with the gst_vaapi_video_buffer_set_*()
 * functions.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL or error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new(GType type, GstVaapiDisplay *display)
{
    GstBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    buffer = _gst_vaapi_video_buffer_typed_new(type);
    if (!buffer)
        return NULL;

    set_display(GST_VAAPI_VIDEO_BUFFER(buffer), display);
    return buffer;
}

/**
 * gst_vaapi_video_buffer_typed_new_from_pool:
 * @pool: a #GstVaapiVideoPool
 *
 * Creates a #GstBuffer with a video object allocated from a @pool.
 * Only #GstVaapiSurfacePool and #GstVaapiImagePool pools are supported.
 *
 * The buffer is destroyed through the last call to gst_buffer_unref()
 * and the video objects are pushed back to their respective pools.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new_from_pool(GType type, GstVaapiVideoPool *pool)
{
    GstVaapiVideoBuffer *buffer;
    gboolean is_image_pool, is_surface_pool;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_POOL(pool), NULL);

    is_image_pool   = GST_VAAPI_IS_IMAGE_POOL(pool);
    is_surface_pool = GST_VAAPI_IS_SURFACE_POOL(pool);

    if (!is_image_pool && !is_surface_pool)
        return NULL;

    buffer = _gst_vaapi_video_buffer_typed_new(type);
    if (buffer &&
        ((is_image_pool &&
          gst_vaapi_video_buffer_set_image_from_pool(buffer, pool)) ||
         (is_surface_pool &&
          gst_vaapi_video_buffer_set_surface_from_pool(buffer, pool)))) {
        set_display(buffer, gst_vaapi_video_pool_get_display(pool));
        return GST_BUFFER(buffer);
    }

    gst_mini_object_unref(GST_MINI_OBJECT(buffer));
    return NULL;
}

/**
 * gst_vaapi_video_buffer_typed_new_from_buffer:
 * @buffer: a #GstBuffer
 *
 * Creates a #GstBuffer with video objects bound to @buffer video
 * objects, if any.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new_from_buffer(GType type, GstBuffer *buffer)
{
    GstVaapiVideoBuffer *inbuf, *outbuf;

    if (!gst_vaapi_video_buffer_is_a(buffer, type)) {
        if (!buffer->parent ||
            !gst_vaapi_video_buffer_is_a(buffer->parent, type))
            return NULL;
        buffer = buffer->parent;
    }
    inbuf = GST_VAAPI_VIDEO_BUFFER(buffer);

    outbuf = _gst_vaapi_video_buffer_typed_new(type);
    if (!outbuf)
        return NULL;

    if (inbuf->priv->image)
        gst_vaapi_video_buffer_set_image(outbuf, inbuf->priv->image);
    if (inbuf->priv->surface)
        gst_vaapi_video_buffer_set_surface(outbuf, inbuf->priv->surface);
    if (inbuf->priv->proxy)
        gst_vaapi_video_buffer_set_surface_proxy(outbuf, inbuf->priv->proxy);

    outbuf->priv->buffer = gst_buffer_ref(buffer);
    return GST_BUFFER(outbuf);
}

/**
 * gst_vaapi_video_buffer_typed_new_with_image:
 * @image: a #GstVaapiImage
 *
 * Creates a #GstBuffer with the specified @image. The resulting
 * buffer holds an additional reference to the @image.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new_with_image(GType type, GstVaapiImage *image)
{
    GstVaapiVideoBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    buffer = _gst_vaapi_video_buffer_typed_new(type);
    if (buffer)
        gst_vaapi_video_buffer_set_image(buffer, image);
    return GST_BUFFER(buffer);
}

/**
 * gst_vaapi_video_buffer_typed_new_with_surface:
 * @surface: a #GstVaapiSurface
 *
 * Creates a #GstBuffer with the specified @surface. The resulting
 * buffer holds an additional reference to the @surface.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new_with_surface(
    GType            type,
    GstVaapiSurface *surface
)
{
    GstVaapiVideoBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    buffer = _gst_vaapi_video_buffer_typed_new(type);
    if (buffer)
        gst_vaapi_video_buffer_set_surface(buffer, surface);
    return GST_BUFFER(buffer);
}

/**
 * gst_vaapi_video_buffer_typed_new_with_surface_proxy:
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Creates a #GstBuffer with the specified surface @proxy. The
 * resulting buffer holds an additional reference to the @proxy.
 *
 * This function shall only be called from within gstreamer-vaapi
 * plugin elements.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_typed_new_with_surface_proxy(
    GType                 type,
    GstVaapiSurfaceProxy *proxy
)
{
    GstVaapiVideoBuffer *buffer;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy), NULL);

    buffer = _gst_vaapi_video_buffer_typed_new(type);
    if (buffer)
        gst_vaapi_video_buffer_set_surface_proxy(buffer, proxy);
    return GST_BUFFER(buffer);
}

/**
 * gst_vaapi_video_buffer_get_display:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Retrieves the #GstVaapiDisplay the @buffer is bound to. The @buffer
 * owns the returned #GstVaapiDisplay object so the caller is
 * responsible for calling g_object_ref() when needed.
 *
 * Return value: the #GstVaapiDisplay the @buffer is bound to
 */
GstVaapiDisplay *
gst_vaapi_video_buffer_get_display(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->display;
}

/**
 * gst_vaapi_video_buffer_get_image:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Retrieves the #GstVaapiImage bound to the @buffer. The @buffer owns
 * the #GstVaapiImage so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiImage bound to the @buffer, or %NULL if
 *   there is none
 */
GstVaapiImage *
gst_vaapi_video_buffer_get_image(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->image;
}

/**
 * gst_vaapi_video_buffer_set_image:
 * @buffer: a #GstVaapiVideoBuffer
 * @image: a #GstVaapiImage
 *
 * Binds @image to the @buffer. If the @buffer contains another image
 * previously allocated from a pool, it's pushed back to its parent
 * pool and the pool is also released.
 */
void
gst_vaapi_video_buffer_set_image(
    GstVaapiVideoBuffer *buffer,
    GstVaapiImage       *image
)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_IMAGE(image));

    gst_vaapi_video_buffer_destroy_image(buffer);

    if (image)
        set_image(buffer, image);
}

/**
 * gst_vaapi_video_buffer_set_image_from_pool
 * @buffer: a #GstVaapiVideoBuffer
 * @pool: a #GstVaapiVideoPool
 *
 * Binds a newly allocated video object from the @pool. The @pool
 * shall be of type #GstVaapiImagePool. Previously allocated objects
 * are released and returned to their parent pools, if any.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_buffer_set_image_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
)
{
    GstVaapiImage *image;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE_POOL(pool), FALSE);

    gst_vaapi_video_buffer_destroy_image(buffer);

    if (pool) {
        image = gst_vaapi_video_pool_get_object(pool);
        if (!image)
            return FALSE;
        set_image(buffer, image);
        buffer->priv->image_pool = g_object_ref(pool);
    }
    return TRUE;
}

/**
 * gst_vaapi_video_buffer_get_surface:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Retrieves the #GstVaapiSurface bound to the @buffer. The @buffer
 * owns the #GstVaapiSurface so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiSurface bound to the @buffer, or %NULL if
 *   there is none
 */
GstVaapiSurface *
gst_vaapi_video_buffer_get_surface(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->surface;
}

/**
 * gst_vaapi_video_buffer_set_surface:
 * @buffer: a #GstVaapiVideoBuffer
 * @surface: a #GstVaapiSurface
 *
 * Binds @surface to the @buffer. If the @buffer contains another
 * surface previously allocated from a pool, it's pushed back to its
 * parent pool and the pool is also released.
 */
void
gst_vaapi_video_buffer_set_surface(
    GstVaapiVideoBuffer *buffer,
    GstVaapiSurface     *surface
)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    gst_vaapi_video_buffer_destroy_surface(buffer);

    if (surface)
        set_surface(buffer, surface);
}

/**
 * gst_vaapi_video_buffer_set_surface_from_pool
 * @buffer: a #GstVaapiVideoBuffer
 * @pool: a #GstVaapiVideoPool
 *
 * Binds a newly allocated video object from the @pool. The @pool
 * shall be of type #GstVaapiSurfacePool. Previously allocated objects
 * are released and returned to their parent pools, if any.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_buffer_set_surface_from_pool(
    GstVaapiVideoBuffer *buffer,
    GstVaapiVideoPool   *pool
)
{
    GstVaapiSurface *surface;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_POOL(pool), FALSE);

    gst_vaapi_video_buffer_destroy_surface(buffer);

    if (pool) {
        surface = gst_vaapi_video_pool_get_object(pool);
        if (!surface)
            return FALSE;
        set_surface(buffer, surface);
        buffer->priv->surface_pool = g_object_ref(pool);
    }
    return TRUE;
}

/**
 * gst_vaapi_video_buffer_get_surface_proxy:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Retrieves the #GstVaapiSurfaceProxy bound to the @buffer. The @buffer
 * owns the #GstVaapiSurfaceProxy so the caller is responsible for calling
 * g_object_ref() when needed.
 *
 * Return value: the #GstVaapiSurfaceProxy bound to the @buffer, or
 *   %NULL if there is none
 */
GstVaapiSurfaceProxy *
gst_vaapi_video_buffer_get_surface_proxy(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), NULL);

    return buffer->priv->proxy;
}

/**
 * gst_vaapi_video_buffer_set_surface_proxy:
 * @buffer: a #GstVaapiVideoBuffer
 * @proxy: a #GstVaapiSurfaceProxy
 *
 * Binds surface @proxy to the @buffer. If the @buffer contains another
 * surface previously allocated from a pool, it's pushed back to its
 * parent pool and the pool is also released.
 */
void
gst_vaapi_video_buffer_set_surface_proxy(
    GstVaapiVideoBuffer  *buffer,
    GstVaapiSurfaceProxy *proxy
)
{
    GstVaapiSurface *surface;

    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_SURFACE_PROXY(proxy));

    gst_vaapi_video_buffer_destroy_surface(buffer);

    if (proxy) {
        surface = GST_VAAPI_SURFACE_PROXY_SURFACE(proxy);
        if (!surface)
            return;
        set_surface(buffer, surface);
        buffer->priv->proxy = g_object_ref(proxy);
    }
}

/**
 * gst_vaapi_video_buffer_get_render_flags:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Retrieves the surface render flags bound to the @buffer.
 *
 * Return value: a combination for #GstVaapiSurfaceRenderFlags
 */
guint
gst_vaapi_video_buffer_get_render_flags(GstVaapiVideoBuffer *buffer)
{
    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), 0);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(buffer->priv->surface), 0);

    return buffer->priv->render_flags;
}

/**
 * gst_vaapi_video_buffer_set_render_flags:
 * @buffer: a #GstVaapiVideoBuffer
 * @flags: a set of surface render flags
 *
 * Sets #GstVaapiSurfaceRenderFlags to the @buffer.
 */
void
gst_vaapi_video_buffer_set_render_flags(GstVaapiVideoBuffer *buffer, guint flags)
{
    g_return_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer));
    g_return_if_fail(GST_VAAPI_IS_SURFACE(buffer->priv->surface));

    buffer->priv->render_flags = flags;
}

/**
 * gst_vaapi_video_buffer_ensure_pointer:
 * @buffer: a #GstVaapiVideoBuffer
 *
 * Maps image data buffer to #GstBuffer of @buffer.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_video_buffer_ensure_pointer(GstVaapiVideoBuffer *buffer)
{
    GstVaapiVideoBufferPrivate * const priv = buffer->priv;
    GstVaapiImage *image = NULL;
    gboolean is_image_derived = FALSE;
    gboolean ret = FALSE;

    g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), FALSE);
    g_return_val_if_fail (priv->image || priv->surface, FALSE);

    if (!priv->image) {
        image = gst_vaapi_surface_derive_image(priv->surface);
        g_return_val_if_fail(image, FALSE);
        is_image_derived = TRUE;
    } else {
        image = priv->image;
        is_image_derived = FALSE;
    }

    if (!gst_vaapi_image_map(image))
        goto end;

    if(!gst_vaapi_image_ensure_mapped_buffer(image))
        goto end;

    GST_BUFFER_DATA(buffer) = gst_vaapi_image_get_plane(image, 0);
    GST_BUFFER_SIZE(buffer) = gst_vaapi_image_get_data_size(image);

    if (!gst_vaapi_image_unmap(image))
        goto end;

    ret = TRUE;

end:
    if (image && is_image_derived)
        g_object_unref(image);
    return ret;
}
