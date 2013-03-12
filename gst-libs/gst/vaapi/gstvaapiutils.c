/*
 *  gstvaapiutils.c - VA-API utilities
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

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisurface.h"
#include <stdio.h>
#include <stdarg.h>

#define DEBUG 1
#include "gstvaapidebug.h"

#define CONCAT(a, b)    CONCAT_(a, b)
#define CONCAT_(a, b)   a##b
#define STRINGIFY(x)    STRINGIFY_(x)
#define STRINGIFY_(x)   #x
#define STRCASEP(p, x)  STRCASE(CONCAT(p, x))
#define STRCASE(x)      case x: return STRINGIFY(x)

/* Check VA status for success or print out an error */
gboolean
vaapi_check_status(VAStatus status, const char *msg)
{
    if (status != VA_STATUS_SUCCESS) {
        GST_DEBUG("%s: %s", msg, vaErrorStr(status));
        return FALSE;
    }
    return TRUE;
}

/* Maps VA buffer */
void *
vaapi_map_buffer(VADisplay dpy, VABufferID buf_id)
{
    VAStatus status;
    void *data = NULL;

    status = vaMapBuffer(dpy, buf_id, &data);
    if (!vaapi_check_status(status, "vaMapBuffer()"))
        return NULL;
    return data;
}

/* Unmaps VA buffer */
void
vaapi_unmap_buffer(VADisplay dpy, VABufferID buf_id, void **pbuf)
{
    VAStatus status;

    if (pbuf)
        *pbuf = NULL;

    status = vaUnmapBuffer(dpy, buf_id);
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        return;
}

/* Creates and maps VA buffer */
gboolean
vaapi_create_buffer(
    VADisplay     dpy,
    VAContextID   ctx,
    int           type,
    unsigned int  size,
    gconstpointer buf,
    VABufferID   *buf_id_ptr,
    gpointer     *mapped_data
)
{
    VABufferID buf_id;
    VAStatus status;
    gpointer data = (gpointer)buf;

    status = vaCreateBuffer(dpy, ctx, type, size, 1, data, &buf_id);
    if (!vaapi_check_status(status, "vaCreateBuffer()"))
        return FALSE;

    if (mapped_data) {
        data = vaapi_map_buffer(dpy, buf_id);
        if (!data)
            goto error;
        *mapped_data = data;
    }

    *buf_id_ptr = buf_id;
    return TRUE;

error:
    vaapi_destroy_buffer(dpy, &buf_id);
    return FALSE;
}

/* Destroy VA buffer */
void
vaapi_destroy_buffer(VADisplay dpy, VABufferID *buf_id_ptr)
{
    if (!buf_id_ptr || *buf_id_ptr == VA_INVALID_ID)
        return;

    vaDestroyBuffer(dpy, *buf_id_ptr);
    *buf_id_ptr = VA_INVALID_ID;
}

/* Return a string representation of a VAProfile */
const char *string_of_VAProfile(VAProfile profile)
{
    switch (profile) {
#define MAP(profile) \
        STRCASEP(VAProfile, profile)
        MAP(MPEG2Simple);
        MAP(MPEG2Main);
        MAP(MPEG4Simple);
        MAP(MPEG4AdvancedSimple);
        MAP(MPEG4Main);
#if VA_CHECK_VERSION(0,32,0)
        MAP(JPEGBaseline);
        MAP(H263Baseline);
        MAP(H264ConstrainedBaseline);
#endif
        MAP(H264Baseline);
        MAP(H264Main);
        MAP(H264High);
        MAP(VC1Simple);
        MAP(VC1Main);
        MAP(VC1Advanced);
#undef MAP
    default: break;
    }
    return "<unknown>";
}

/* Return a string representation of a VAEntrypoint */
const char *string_of_VAEntrypoint(VAEntrypoint entrypoint)
{
    switch (entrypoint) {
#define MAP(entrypoint) \
        STRCASEP(VAEntrypoint, entrypoint)
        MAP(VLD);
        MAP(IZZ);
        MAP(IDCT);
        MAP(MoComp);
        MAP(Deblocking);
#undef MAP
    default: break;
    }
    return "<unknown>";
}

/* Return a string representation of a VADisplayAttributeType */
const char *
string_of_VADisplayAttributeType(VADisplayAttribType attribute_type)
{
    switch (attribute_type) {
#define MAP(attribute_type) \
        STRCASEP(VADisplayAttrib, attribute_type)
        MAP(Brightness);
        MAP(Contrast);
        MAP(Hue);
        MAP(Saturation);
        MAP(BackgroundColor);
#if !VA_CHECK_VERSION(0,34,0)
        MAP(DirectSurface);
#endif
        MAP(Rotation);
        MAP(OutofLoopDeblock);
#if VA_CHECK_VERSION(0,31,1) && !VA_CHECK_VERSION(0,34,0)
        MAP(BLEBlackMode);
        MAP(BLEWhiteMode);
        MAP(BlueStretch);
        MAP(SkinColorCorrection);
#endif
        MAP(CSCMatrix);
        MAP(BlendColor);
        MAP(OverlayAutoPaintColorKey);
        MAP(OverlayColorKey);
        MAP(RenderMode);
        MAP(RenderDevice);
        MAP(RenderRect);
#undef MAP
    default: break;
    }
    return "<unknown>";
}

/**
 * from_GstVaapiSurfaceRenderFlags:
 * @flags: the #GstVaapiSurfaceRenderFlags
 *
 * Converts #GstVaapiSurfaceRenderFlags to flags suitable for
 * vaPutSurface().
 */
guint
from_GstVaapiSurfaceRenderFlags(guint flags)
{
    guint va_fields = 0, va_csc = 0;

    if (flags & GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        va_fields |= VA_TOP_FIELD;
    if (flags & GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        va_fields |= VA_BOTTOM_FIELD;
    if ((va_fields ^ (VA_TOP_FIELD|VA_BOTTOM_FIELD)) == 0)
        va_fields  = VA_FRAME_PICTURE;

#ifdef VA_SRC_BT601
    if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_601)
        va_csc = VA_SRC_BT601;
#endif
#ifdef VA_SRC_BT709
    if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_709)
        va_csc = VA_SRC_BT709;
#endif

    return va_fields|va_csc;
}

/**
 * to_GstVaapiSurfaceStatus:
 * @flags: the #GstVaapiSurfaceStatus flags to translate
 *
 * Converts vaQuerySurfaceStatus() @flags to #GstVaapiSurfaceStatus
 * flags.
 *
 * Return value: the #GstVaapiSurfaceStatus flags
 */
guint
to_GstVaapiSurfaceStatus(guint va_flags)
{
    guint flags;
    const guint va_flags_mask = (VASurfaceReady|
                                 VASurfaceRendering|
                                 VASurfaceDisplaying);

    /* Check for core status */
    switch (va_flags & va_flags_mask) {
    case VASurfaceReady:
        flags = GST_VAAPI_SURFACE_STATUS_IDLE;
        break;
    case VASurfaceRendering:
        flags = GST_VAAPI_SURFACE_STATUS_RENDERING;
        break;
    case VASurfaceDisplaying:
        flags = GST_VAAPI_SURFACE_STATUS_DISPLAYING;
        break;
    default:
        flags = 0;
        break;
    }

    /* Check for encoder status */
#if VA_CHECK_VERSION(0,30,0)
    if (va_flags & VASurfaceSkipped)
        flags |= GST_VAAPI_SURFACE_STATUS_SKIPPED;
#endif
    return flags;
}

/* Translate GstVaapiRotation value to VA-API rotation value */
guint
from_GstVaapiRotation(guint value)
{
    switch (value) {
    case GST_VAAPI_ROTATION_0:   return VA_ROTATION_NONE;
    case GST_VAAPI_ROTATION_90:  return VA_ROTATION_90;
    case GST_VAAPI_ROTATION_180: return VA_ROTATION_180;
    case GST_VAAPI_ROTATION_270: return VA_ROTATION_270;
    }
    GST_ERROR("unsupported GstVaapiRotation value %d", value);
    return VA_ROTATION_NONE;
}

/* Translate VA-API rotation value to GstVaapiRotation value */
guint
to_GstVaapiRotation(guint value)
{
    switch (value) {
    case VA_ROTATION_NONE: return GST_VAAPI_ROTATION_0;
    case VA_ROTATION_90:   return GST_VAAPI_ROTATION_90;
    case VA_ROTATION_180:  return GST_VAAPI_ROTATION_180;
    case VA_ROTATION_270:  return GST_VAAPI_ROTATION_270;
    }
    GST_ERROR("unsupported VA-API rotation value %d", value);
    return GST_VAAPI_ROTATION_0;
}

guint
from_GstVaapiRateControl(guint value)
{
    switch (value) {
    case GST_VAAPI_RATECONTROL_NONE:   return VA_RC_NONE;
    case GST_VAAPI_RATECONTROL_CBR:  return VA_RC_CBR;
    case GST_VAAPI_RATECONTROL_VBR: return VA_RC_VBR;
    case GST_VAAPI_RATECONTROL_VCM: return VA_RC_VCM;
#if VA_CHECK_VERSION(0,34,0)
    case GST_VAAPI_RATECONTROL_CQP: return VA_RC_CQP;
    case GST_VAAPI_RATECONTROL_VBR_CONSTRAINED: return VA_RC_VBR_CONSTRAINED;
#endif
    }
    GST_ERROR("unsupported GstVaapiRateControl value %d", value);
    return VA_RC_NONE;
}

guint
to_GstVaapiRateControl(guint value)
{
    switch (value) {
    case VA_RC_NONE: return GST_VAAPI_RATECONTROL_NONE;
    case VA_RC_CBR:   return GST_VAAPI_RATECONTROL_CBR;
    case VA_RC_VBR:  return GST_VAAPI_RATECONTROL_VBR;
    case VA_RC_VCM:  return GST_VAAPI_RATECONTROL_VCM;
#if VA_CHECK_VERSION(0,34,0)
    case VA_RC_CQP: return GST_VAAPI_RATECONTROL_CQP;
    case VA_RC_VBR_CONSTRAINED: return GST_VAAPI_RATECONTROL_VBR_CONSTRAINED;
#endif
    }
    GST_ERROR("unsupported VA-API Rate Control value %d", value);
    return GST_VAAPI_RATECONTROL_NONE;
}

const char *
string_of_VARateControl(guint rate_control)
{
    switch (rate_control) {
    case VA_RC_NONE: return "VA_RC_NONE";
    case VA_RC_CBR: return "VA_RC_CBR";
    case VA_RC_VBR: return "VA_RC_VBR";
    case VA_RC_VCM: return "VA_RC_VCM";
#if VA_CHECK_VERSION(0,34,0)
    case VA_RC_CQP: return "VA_RC_CQP";
    case VA_RC_VBR_CONSTRAINED: return "VA_RC_VBR_CONSTRAINED";
#endif
    default: break;
    }
    return "<unknown>";
}
