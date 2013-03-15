/*
 *  gstvaapisurface_userptr.c - VA surface abstraction for userptr
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

/**
 * SECTION:gstvaapisurface_userptr
 * @short_description: VA surface abstraction for userptr
 */
#include "sysdeps.h"
#include "gstvaapisurface_userptr.h"
#include "gstvaapiutils.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapidisplay_priv.h"

#include <va/va.h>
#include <va/va_tpi.h>

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSurfaceUserPtr, gst_vaapi_surface_userptr, GST_VAAPI_TYPE_SURFACE);

#define GST_VAAPI_SURFACE_USERPTR_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                            \
                                 GST_VAAPI_TYPE_SURFACE_USERPTR,   \
                                 GstVaapiSurfaceUserPtrPrivate))

struct _GstVaapiSurfaceUserPtrPrivate {
    GstVaapiImageFormat format;
    guint               stride;
};

enum {
    PROP_0,

    PROP_STRIDE,
    PROP_FORMAT
};

typedef struct _GstVaapiSurfaceUserPtrInfo {
    guint fourcc;
    guint datasize;
    guint y_stride;
    guint u_stride;
    guint v_stride;
    guint y_offset;
    guint u_offset;
    guint v_offset;
} GstVaapiSurfaceUserPtrInfo;

static gboolean
_get_surface_userptr_info(
    GstVaapiImageFormat format,
    guint width,
    guint height,
    guint first_stride,
    GstVaapiSurfaceUserPtrInfo *info
)
{
    const VAImageFormat *va_format;
    guint stride1, stride2;
    guint height2;

    g_return_val_if_fail(info, FALSE);

    va_format = gst_vaapi_image_format_get_va_format(format);
    if (!va_format)
        return FALSE;
    info->fourcc = va_format->fourcc;

    stride1 = GST_ROUND_UP_4(width);
    stride2 = GST_ROUND_UP_4((width + 1) / 2);
    if (stride1 < first_stride)
        stride1 = first_stride;
    if (stride2 < first_stride/2)
        stride2 = first_stride/2;
    height2 = (height + 1) / 2;

    info->datasize = (stride1 * height) + (stride2 * height2)*2;
    info->y_stride = stride1;
    info->y_offset = 0;

    switch (format) {
    case GST_VAAPI_IMAGE_NV12:
        info->u_stride = stride1;
        info->v_stride = stride1;
        info->u_offset = stride1 * height;
        info->v_offset = stride1 * height;
        break;

    case GST_VAAPI_IMAGE_YV12:
        info->u_stride = stride2;
        info->v_stride = stride2;
        info->u_offset = stride1 * height + stride2 * height2;
        info->v_offset = stride1 * height;
        break;

    case GST_VAAPI_IMAGE_I420:
        info->u_stride = stride2;
        info->v_stride = stride2;
        info->u_offset = stride1 * height;
        info->v_offset = stride1 * height + stride2 * height2;
        break;

    default:
        return FALSE;
    }
    return TRUE;
}

static gboolean
gst_vaapi_surface_userptr_create(GstVaapiSurface *base)
{
    GstVaapiSurfaceUserPtr * const surface = GST_VAAPI_SURFACE_USERPTR(base);
    GstVaapiSurfaceUserPtrPrivate * const priv = surface->priv;
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(surface);
    GstVaapiSurfaceUserPtrInfo info;
    VASurfaceID surface_id;
    VAStatus status;
    guint surface_format;
    GstVaapiChromaType chroma_type;
    guint width, height;

    chroma_type = gst_vaapi_surface_get_chroma_type(base);
    gst_vaapi_surface_get_size(base, &width, &height);

    if (!_get_surface_userptr_info(
            priv->format, width, height, priv->stride, &info)) {
        GST_ERROR("surface userptr format error.");
        return FALSE;
    }

    switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV420:
        surface_format = VA_RT_FORMAT_YUV420;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
        surface_format = VA_RT_FORMAT_YUV422;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
        surface_format = VA_RT_FORMAT_YUV444;
        break;
    default:
        GST_DEBUG("unsupported chroma-type %u\n", chroma_type);
        return FALSE;
    }

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateSurfacesForUserPtr(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        width,
        height,
        surface_format,
        1, &surface_id,
        info.datasize,
        info.fourcc,
        info.y_stride,
        info.u_stride,
        info.v_stride,
        info.y_offset,
        info.u_offset,
        info.v_offset);
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateSurfaces()"))
        return FALSE;

    GST_DEBUG("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(surface_id));
    GST_VAAPI_OBJECT_ID(surface) = surface_id;
    return TRUE;
}

static void
gst_vaapi_surface_userptr_finalize(GObject *object)
{

    G_OBJECT_CLASS(gst_vaapi_surface_userptr_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_userptr_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSurfaceUserPtr        * const surface = GST_VAAPI_SURFACE_USERPTR(object);
    GstVaapiSurfaceUserPtrPrivate * const priv    = surface->priv;

    switch (prop_id) {
    case PROP_FORMAT:
        priv->format = g_value_get_uint(value);
        break;
    case PROP_STRIDE:
        priv->stride = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_userptr_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSurfaceUserPtr * const surface = GST_VAAPI_SURFACE_USERPTR(object);
    GstVaapiSurfaceUserPtrPrivate *priv = GST_VAAPI_SURFACE_USERPTR_GET_PRIVATE(surface);

    switch (prop_id) {
    case PROP_FORMAT:
        g_value_set_uint(value, gst_vaapi_surface_userptr_get_format(surface));
        break;
    case PROP_STRIDE:
        g_value_set_uint(value, priv->stride);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_userptr_class_init(GstVaapiSurfaceUserPtrClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiSurfaceClass * const surface_class = GST_VAAPI_SURFACE_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfaceUserPtrPrivate));

    object_class->finalize     = gst_vaapi_surface_userptr_finalize;
    object_class->set_property = gst_vaapi_surface_userptr_set_property;
    object_class->get_property = gst_vaapi_surface_userptr_get_property;

    surface_class->create = gst_vaapi_surface_userptr_create;

    g_object_class_install_property
        (object_class,
         PROP_FORMAT,
         g_param_spec_uint("format",
                           "format",
                           "The buffer format of surface userptr",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
    g_object_class_install_property
        (object_class,
         PROP_STRIDE,
         g_param_spec_uint("stride",
                           "stride",
                           "The first row stride for surface",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_surface_userptr_init(GstVaapiSurfaceUserPtr *surface)
{
    GstVaapiSurfaceUserPtrPrivate *priv = GST_VAAPI_SURFACE_USERPTR_GET_PRIVATE(surface);

    surface->priv        = priv;
    priv->format         = 0;
    priv->stride         = 0;
}

/**
 * gst_vaapi_surface_new:
 * @display: a #GstVaapiDisplay
 * @chroma_type: the surface chroma format
 * @format: the fourcc format of the buffer
 * @width: the requested surface width
 * @height: the requested surface height
 * @row_stride: the requested surface row_stride(luma)
 *
 * Creates a new #GstVaapiSurface with the specified chroma format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiSurface object
 */
GstVaapiSurfaceUserPtr *
gst_vaapi_surface_userptr_new(
    GstVaapiDisplay    *display,
    GstVaapiChromaType  chroma_type,
    GstVaapiImageFormat format,
    guint               width,
    guint               height,
    guint               row_stride
)
{
    GST_DEBUG("size %ux%u, chroma type 0x%x", width, height, chroma_type);

    return g_object_new(GST_VAAPI_TYPE_SURFACE_USERPTR,
                        "display",      display,
                        "id",           GST_VAAPI_ID(VA_INVALID_ID),
                        "width",        width,
                        "height",       height,
                        "stride",       row_stride,
                        "chroma-type",  chroma_type,
                        "format",       format,
                        NULL);
}


GstVaapiImageFormat
gst_vaapi_surface_userptr_get_format(GstVaapiSurfaceUserPtr *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE_USERPTR(surface), 0);

    return surface->priv->format;
}
