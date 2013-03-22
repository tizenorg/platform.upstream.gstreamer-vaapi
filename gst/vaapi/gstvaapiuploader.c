/*
 *  gstvaapiuploader.c - VA-API video uploader helper
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
#include <string.h>
#include <gst/video/video.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapiimagepool.h>
#include <gst/vaapi/gstvaapisurfacepool.h>
#include <gst/vaapi/gstvaapivideobuffer.h>

#include "gstvaapiuploader.h"
#include "gstvaapipluginbuffer.h"

#define GST_HELPER_NAME "vaapiuploader"
#define GST_HELPER_DESC "VA-API video internal uploader"

GST_DEBUG_CATEGORY_STATIC(gst_debug_vaapi_uploader);
#define GST_CAT_DEFAULT gst_debug_vaapi_uploader

G_DEFINE_TYPE(GstVaapiUploader, gst_vaapi_uploader, G_TYPE_OBJECT)

#define GST_VAAPI_UPLOADER_CAST(obj) \
    ((GstVaapiUploader *)(obj))

#define GST_VAAPI_UPLOADER_GET_PRIVATE(obj)                     \
  (G_TYPE_INSTANCE_GET_PRIVATE((obj),					\
			       GST_VAAPI_TYPE_UPLOADER,			\
			       GstVaapiUploaderPrivate))

struct _GstVaapiUploaderPrivate {
  GstVaapiDisplay    *display;
  GstCaps            *allowed_caps;
  GstVaapiVideoPool  *images;
  GstCaps            *image_caps;
  guint               image_width;
  guint               image_height;
  GstVaapiVideoPool  *surfaces;
  guint               surface_width;
  guint               surface_height;
  guint               direct_rendering;
};

enum {
  PROP_0,
  PROP_DISPLAY,
};

static void
gst_vaapi_uploader_destroy(GstVaapiUploader *uploader)
{
  GstVaapiUploaderPrivate * const priv = uploader->priv;

  gst_caps_replace(&priv->image_caps, NULL);
  gst_caps_replace(&priv->allowed_caps, NULL);
  g_clear_object(&priv->images);
  g_clear_object(&priv->surfaces);
  g_clear_object(&priv->display);
}

static gboolean
ensure_display(GstVaapiUploader *uploader, GstVaapiDisplay *display)
{
  GstVaapiUploaderPrivate * const priv = uploader->priv;
  if (priv->display == display)
    return TRUE;

  g_clear_object(&priv->display);
  if (display)
    priv->display = g_object_ref(display);
  return TRUE;
}

static gboolean
ensure_image(GstVaapiImage *image)
{
  guint i, num_planes, width, height;

  /* Make the image fully dirty */
  if (!gst_vaapi_image_map(image))
    return FALSE;

  gst_vaapi_image_get_size(image, &width, &height);
  num_planes = gst_vaapi_image_get_plane_count(image);
  for (i = 0; i < num_planes; i++) {
    guchar * const plane = gst_vaapi_image_get_plane(image, i);
    if (plane)
      memset(plane, 0, height * gst_vaapi_image_get_pitch(image, i));
  }

  if (!gst_vaapi_image_unmap(image))
    gst_vaapi_image_unmap(image);
  return TRUE;
}

static GstCaps *
virtual_src_caps_new (const char *mimetype,const char *fieldname, ...)
{
  GstStructure *structure = NULL;
  GstCaps *caps = NULL;
  va_list var_args;

  caps = gst_caps_new_simple (mimetype,
			      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
			      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
			      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

  structure = gst_caps_get_structure (caps, 0);

  if (structure) {
    va_start (var_args, fieldname);
    gst_structure_set_valist (structure, fieldname, var_args);
    va_end (var_args);
  }
  return caps;
}


static gboolean
ensure_allowed_caps(GstVaapiUploader *uploader)
{
  GstVaapiUploaderPrivate * const priv = uploader->priv;
  GstVaapiSurface *surface = NULL;
  GstCaps *out_caps, *image_caps = NULL;
  guint i, n_structures;
  gboolean success = FALSE;
  GstCaps *virtual_caps = NULL;

  enum { WIDTH = 64, HEIGHT = 64 };
  if (priv->allowed_caps)
    return TRUE;

  out_caps = gst_caps_new_empty();
  if (!out_caps)
    return FALSE;

  image_caps = gst_vaapi_display_get_image_caps(priv->display);
  if (!image_caps)
    goto end;

  surface = gst_vaapi_surface_new(priv->display,
				  GST_VAAPI_CHROMA_TYPE_YUV420, WIDTH, HEIGHT);
  if (!surface)
    goto end;

  virtual_caps = virtual_src_caps_new(GST_VAAPI_RAWYUV_CAPS_NAME,"format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'), NULL);

  n_structures = gst_caps_get_size(image_caps);

  for (i = 0; i < n_structures; i++) {
    GstStructure * const structure = gst_caps_get_structure(image_caps, i);
    GstVaapiImage *image;
    GstVaapiImageFormat format;
    guint32 fourcc;

    if (!gst_structure_get_fourcc(structure, "format", &fourcc))
      continue;
    format = gst_vaapi_image_format_from_fourcc(fourcc);
    if (!format)
      continue;
    image = gst_vaapi_image_new(priv->display, format, WIDTH, HEIGHT);
    if (!image)
      continue;

    if (ensure_image(image) && gst_vaapi_surface_put_image(surface, image))
      gst_caps_append_structure(out_caps, gst_structure_copy(structure));
    gst_object_unref(image);
  }

  // Add Virtaul Src Caps
  if(virtual_caps){
    gst_caps_append(out_caps,virtual_caps);
  }
  gst_caps_replace(&priv->allowed_caps, out_caps);
  success = TRUE;

end:
  gst_caps_unref(out_caps);

  if (image_caps)
    gst_caps_unref(image_caps);
  if (surface)
    gst_object_unref(surface);
  return success;
}

static gboolean
ensure_image_pool(GstVaapiUploader *uploader, GstCaps *caps)
{
  GstVaapiUploaderPrivate * const priv = uploader->priv;
  GstStructure * const structure = gst_caps_get_structure(caps, 0);
  gint width, height;

  gst_structure_get_int(structure, "width",  &width);
  gst_structure_get_int(structure, "height", &height);

  if (width != priv->image_width || height != priv->image_height) {
    priv->image_width  = width;
    priv->image_height = height;
    g_clear_object(&priv->images);
    priv->images = gst_vaapi_image_pool_new(priv->display, caps);
    if (!priv->images)
      return FALSE;
    gst_caps_replace(&priv->image_caps, caps);
  }
  return TRUE;
}

static gboolean
ensure_surface_pool(GstVaapiUploader *uploader, GstCaps *caps)
{
  GstVaapiUploaderPrivate * const priv = uploader->priv;
  GstStructure * const structure = gst_caps_get_structure(caps, 0);
  gint width, height;
  gst_structure_get_int(structure, "width",  &width);
  gst_structure_get_int(structure, "height", &height);

  if (width != priv->surface_width || height != priv->surface_height) {
    priv->surface_width  = width;
    priv->surface_height = height;
    g_clear_object(&priv->surfaces);
    priv->surfaces = gst_vaapi_surface_pool_new(priv->display, caps);
    if (!priv->surfaces)
      return FALSE;
  }
  return TRUE;
}

static void
gst_vaapi_uploader_finalize(GObject *object)
{
  gst_vaapi_uploader_destroy(GST_VAAPI_UPLOADER_CAST(object));
  G_OBJECT_CLASS(gst_vaapi_uploader_parent_class)->finalize(object);
}

static void
gst_vaapi_uploader_set_property(GObject *object, guint prop_id,
				const GValue *value, GParamSpec *pspec)
{
  GstVaapiUploader * const uploader = GST_VAAPI_UPLOADER_CAST(object);
  switch (prop_id) {
  case PROP_DISPLAY:
    ensure_display(uploader, g_value_get_object(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gst_vaapi_uploader_get_property(GObject *object, guint prop_id,
				GValue *value, GParamSpec *pspec)
{
  GstVaapiUploader * const uploader = GST_VAAPI_UPLOADER_CAST(object);
  switch (prop_id) {
  case PROP_DISPLAY:
    g_value_set_object(value, uploader->priv->display);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gst_vaapi_uploader_class_init(GstVaapiUploaderClass *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GST_DEBUG_CATEGORY_INIT(gst_debug_vaapi_uploader,
			  GST_HELPER_NAME, 0, GST_HELPER_DESC);
  g_type_class_add_private(klass, sizeof(GstVaapiUploaderPrivate));
  object_class->finalize     = gst_vaapi_uploader_finalize;
  object_class->set_property = gst_vaapi_uploader_set_property;
  object_class->get_property = gst_vaapi_uploader_get_property;

  g_object_class_install_property(
	  object_class,
	  PROP_DISPLAY,
	  g_param_spec_object(
	      "display",
	      "Display",
	      "The GstVaapiDisplay this object is bound to",
	      GST_VAAPI_TYPE_DISPLAY,
	      G_PARAM_READWRITE));
}

static void
gst_vaapi_uploader_init(GstVaapiUploader *uploader)
{
  GstVaapiUploaderPrivate *priv;
  priv                = GST_VAAPI_UPLOADER_GET_PRIVATE(uploader);
  uploader->priv      = priv;
}

GstVaapiUploader *
gst_vaapi_uploader_new(GstVaapiDisplay *display)
{
  return g_object_new(GST_VAAPI_TYPE_UPLOADER, "display", display, NULL);
}

gboolean
gst_vaapi_uploader_ensure_display(
				  GstVaapiUploader *uploader,
				  GstVaapiDisplay  *display
				  )
{
  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), FALSE);
  g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), FALSE);
  return ensure_display(uploader,display);
}

gboolean
gst_vaapi_uploader_ensure_caps(
			       GstVaapiUploader *uploader,
			       GstCaps          *src_caps,
			       GstCaps          *out_caps
			       )
{
  GstVaapiUploaderPrivate *priv;
  GstVaapiImage *image;
  GstVaapiImageFormat vaformat;
  GstVideoFormat vformat;
  GstStructure *structure;
  gint width, height;

  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), FALSE);
  g_return_val_if_fail(src_caps != NULL, FALSE);

  if (!ensure_image_pool(uploader, src_caps))
    return FALSE;
  if (!ensure_surface_pool(uploader, out_caps ? out_caps : src_caps))
    return FALSE;

  priv = uploader->priv;
  priv->direct_rendering = 0;
  structure = gst_caps_get_structure(src_caps, 0);
  if (!structure)
    return FALSE;
  gst_structure_get_int(structure, "width",  &width);
  gst_structure_get_int(structure, "height", &height);

  /* Translate from Gst video format to VA image format */
  if (!gst_video_format_parse_caps(src_caps, &vformat, NULL, NULL))
    return FALSE;
  if (!gst_video_format_is_yuv(vformat))
    return FALSE;
  vaformat = gst_vaapi_image_format_from_video(vformat);
  if (!vaformat)
    return FALSE;

  gst_vaapi_video_pool_set_capacity(priv->images, 4);
  gst_vaapi_video_pool_reserve(priv->images, 3);

  /* Check if we can alias source and output buffers (same data_size) */
  image = gst_vaapi_video_pool_get_object(priv->images);
  if (image) {
    if (gst_vaapi_image_get_format(image) == vaformat &&
	gst_vaapi_image_is_linear(image) &&
	(gst_vaapi_image_get_data_size(image) ==
	 gst_video_format_get_size(vformat, width, height)))
      priv->direct_rendering = 1;
    gst_vaapi_video_pool_put_object(priv->images, image);
  }
  return TRUE;
}

gboolean
gst_vaapi_uploader_process(
			   GstVaapiUploader *uploader,
			   GstBuffer        *src_buffer,
			   GstBuffer        *out_buffer
			   )
{
  GstVaapiVideoBuffer *out_vbuffer;
  GstVaapiSurface *surface;
  GstVaapiImage *image;
  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), FALSE);

  if (GST_VAAPI_IS_VIDEO_BUFFER(out_buffer))
    out_vbuffer = GST_VAAPI_VIDEO_BUFFER(out_buffer);
  else if (GST_VAAPI_IS_VIDEO_BUFFER(out_buffer->parent))
    out_vbuffer = GST_VAAPI_VIDEO_BUFFER(out_buffer->parent);
  else {
    GST_WARNING("expected an output video buffer");
    return FALSE;
  }

  surface = gst_vaapi_video_buffer_get_surface(out_vbuffer);
  g_return_val_if_fail(surface != NULL, FALSE);

  if (GST_VAAPI_IS_VIDEO_BUFFER(src_buffer)) {
    /* GstVaapiVideoBuffer with mapped VA image */
    image = gst_vaapi_video_buffer_get_image(
	     GST_VAAPI_VIDEO_BUFFER(src_buffer));

    if (!image || !gst_vaapi_image_unmap(image))
      return FALSE;
  }
  else if (GST_VAAPI_IS_VIDEO_BUFFER(src_buffer->parent)) {
    /* Sub-buffer from GstVaapiVideoBuffer with mapped VA image */
    image = gst_vaapi_video_buffer_get_image(
	     GST_VAAPI_VIDEO_BUFFER(src_buffer->parent));

    if (!image || !gst_vaapi_image_unmap(image))
    return FALSE;
  }
  else {
    /* Regular GstBuffer that needs to be uploaded and transmit to a VA image */
    image = gst_vaapi_video_buffer_get_image(out_vbuffer);

    if (!image) {
      image = gst_vaapi_video_pool_get_object(uploader->priv->images);
      if (!image)
	return FALSE;
      gst_vaapi_video_buffer_set_image(out_vbuffer, image);
    }

    // convert coming image data type to real acceptable type
    if (!gst_vaapi_convert_buffer_to_image(image, src_buffer))
      return FALSE;
  }
  g_return_val_if_fail(image != NULL, FALSE);

  if (!gst_vaapi_surface_put_image(surface, image)) {
    GST_WARNING("failed to upload YUV buffer to VA surface");
    return FALSE;
  }

  /* Map again for next uploads */
  if (!gst_vaapi_image_map(image))
    return FALSE;
  return TRUE;
}

GstCaps *
gst_vaapi_uploader_get_caps(GstVaapiUploader *uploader)
{
  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), NULL);
  if (!ensure_allowed_caps(uploader))
    return NULL;
  return uploader->priv->allowed_caps;
}

GstBuffer *
gst_vaapi_uploader_get_buffer(GstVaapiUploader *uploader)
{
  GstVaapiUploaderPrivate *priv;
  GstVaapiSurface *surface;
  GstVaapiImage *image;
  GstVaapiVideoBuffer *vbuffer;
  GstBuffer *buffer = NULL;

  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), NULL);
  priv = uploader->priv;

  buffer = gst_vaapi_video_buffer_new_from_pool(priv->images);
  if (!buffer) {
    GST_WARNING("failed to allocate video buffer");
    goto error;
  }

  vbuffer = GST_VAAPI_VIDEO_BUFFER(buffer);
  surface = gst_vaapi_video_pool_get_object(priv->surfaces);
  if (!surface) {
    GST_WARNING("failed to allocate VA surface");
    goto error;
  }

  gst_vaapi_video_buffer_set_surface(vbuffer, surface);
  image = gst_vaapi_video_buffer_get_image(vbuffer);
  if (!gst_vaapi_image_map(image)) {
    GST_WARNING("failed to map VA image");
    goto error;
  }

  GST_BUFFER_DATA(buffer) = gst_vaapi_image_get_plane(image, 0);
  GST_BUFFER_SIZE(buffer) = gst_vaapi_image_get_data_size(image);
  gst_buffer_set_caps(buffer, priv->image_caps);
  return buffer;

error:
  gst_buffer_unref(buffer);
  return buffer;
}

gboolean
gst_vaapi_uploader_free_buffer(GstVaapiUploader *uploader, GstBuffer *buffer)
{
  GstVaapiUploaderPrivate *priv;
  GstVaapiSurface *surface;
  GstVaapiImage *image;
  gboolean success = FALSE;

  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), success);
  g_return_val_if_fail(GST_VAAPI_IS_VIDEO_BUFFER(buffer), success);

  priv = uploader->priv;
  image = gst_vaapi_video_buffer_get_image(GST_VAAPI_VIDEO_BUFFER(buffer));
  if (image){
    gst_vaapi_video_pool_put_object(priv->images,image);
    success = TRUE;
  }

  surface = gst_vaapi_video_buffer_get_surface(GST_VAAPI_VIDEO_BUFFER(buffer));
  if (surface){
    gst_vaapi_video_pool_put_object(priv->surfaces,surface);
  }else{
    success = FALSE;
  }

  return success;
}

gboolean
gst_vaapi_uploader_has_direct_rendering(GstVaapiUploader *uploader)
{
  g_return_val_if_fail(GST_VAAPI_IS_UPLOADER(uploader), FALSE);
  return uploader->priv->direct_rendering;
}
