/*
 *  gstvaapitypes.h - Basic types
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
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

#ifndef GST_VAAPI_TYPES_H
#define GST_VAAPI_TYPES_H

#include <glib.h>

G_BEGIN_DECLS

/**
 * GstVaapiID:
 *
 * An integer large enough to hold a generic VA id or a pointer
 * wherever necessary.
 */
#if defined(GLIB_SIZEOF_VOID_P)
# define GST_VAAPI_TYPE_ID_SIZE GLIB_SIZEOF_VOID_P
#elif G_MAXULONG == 0xffffffff
# define GST_VAAPI_TYPE_ID_SIZE 4
#elif G_MAXULONG == 0xffffffffffffffffull
# define GST_VAAPI_TYPE_ID_SIZE 8
#else
# error "could not determine size of GstVaapiID"
#endif
#if GST_VAAPI_TYPE_ID_SIZE == 4
typedef guint32 GstVaapiID;
#elif GST_VAAPI_TYPE_ID_SIZE == 8
typedef guint64 GstVaapiID;
#else
# error "unsupported value for GST_VAAPI_TYPE_ID_SIZE"
#endif

/**
 * GST_VAAPI_ID:
 * @id: an arbitrary integer value
 *
 * Macro that creates a #GstVaapiID from @id.
 */
#define GST_VAAPI_ID(id) ((GstVaapiID)(id))

/**
 * GST_VAAPI_ID_NONE:
 *
 * Macro that evaluates to the default #GstVaapiID value.
 */
#define GST_VAAPI_ID_NONE GST_VAAPI_ID(0)

/**
 * GST_VAAPI_ID_FORMAT:
 *
 * Can be used together with #GST_VAAPI_ID_ARGS to properly output an
 * integer value in a printf()-style text message.
 * <informalexample>
 * <programlisting>
 * printf("id: %" GST_VAAPI_ID_FORMAT "\n", GST_VAAPI_ID_ARGS(id));
 * </programlisting>
 * </informalexample>
 */
#define GST_VAAPI_ID_FORMAT "p"

/**
 * GST_VAAPI_ID_ARGS:
 * @id: a #GstVaapiID
 *
 * Can be used together with #GST_VAAPI_ID_FORMAT to properly output
 * an integer value in a printf()-style text message.
 */
#define GST_VAAPI_ID_ARGS(id) GUINT_TO_POINTER(id)

/**
 * GstVaapiPoint:
 * @x: X coordinate
 * @y: Y coordinate
 *
 * A location within a surface.
 */
typedef struct _GstVaapiPoint GstVaapiPoint;
struct _GstVaapiPoint {
    guint32 x;
    guint32 y;
};

/**
 * GstVaapiRectangle:
 * @x: X coordinate
 * @y: Y coordinate
 * @width: region width
 * @height: region height
 *
 * A rectangle region within a surface.
 */
typedef struct _GstVaapiRectangle GstVaapiRectangle;
struct _GstVaapiRectangle {
    guint32 x;
    guint32 y;
    guint32 width;
    guint32 height;
};

/**
 * GstVaapiRenderMode:
 * @GST_VAAPI_RENDER_MODE_OVERLAY: in this mode, the VA display
 *   backend renders surfaces with an overlay engine. This means that
 *   the surface that is currently displayed shall not be re-used
 *   right away for decoding. i.e. it needs to be retained further,
 *   until the next surface is to be displayed.
 * @GST_VAAPI_RENDER_MODE_TEXTURE: in this modem the VA display
 *   backend renders surfaces with a textured blit (GPU/3D engine).
 *   This means that the surface is copied to some intermediate
 *   backing store, or back buffer of a frame buffer, and is free to
 *   be re-used right away for decoding.
 */
typedef enum _GstVaapiRenderMode GstVaapiRenderMode;
enum _GstVaapiRenderMode {
    GST_VAAPI_RENDER_MODE_OVERLAY = 1,
    GST_VAAPI_RENDER_MODE_TEXTURE
};

/**
 * GstVaapiRotation:
 * @GST_VAAPI_ROTATION_0: the VA display is not rotated.
 * @GST_VAAPI_ROTATION_90: the VA display is rotated by 90°, clockwise.
 * @GST_VAAPI_ROTATION_180: the VA display is rotated by 180°, clockwise.
 * @GST_VAAPI_ROTATION_270: the VA display is rotated by 270°, clockwise.
 */
typedef enum _GstVaapiRotation GstVaapiRotation;
enum _GstVaapiRotation {
    GST_VAAPI_ROTATION_0   = 0,
    GST_VAAPI_ROTATION_90,
    GST_VAAPI_ROTATION_180,
    GST_VAAPI_ROTATION_270,
};

/**
 * GstVaapiRateControl:
 * @GST_VAAPI_RATECONTROL_NONE: None rate control
 * @GST_VAAPI_RATECONTROL_CBR: Constant rate control
 * @GST_VAAPI_RATECONTROL_VBR: Variable bitrate control
 * @GST_VAAPI_RATECONTROL_VCM: Video conference mode
 * @GST_VAAPI_RATECONTROL_CQP: Variable bitrate control
 * @GST_VAAPI_RATECONTROL_VBR_CONSTRAINED: Variable bitrate control with peak rate higher than average bitrate
 *
 * The set of all rate-controls for #GstVaapiRateControl, only worked on encoders
 */
typedef enum {
    GST_VAAPI_RATECONTROL_NONE = 0,
    GST_VAAPI_RATECONTROL_CBR,
    GST_VAAPI_RATECONTROL_VBR,
    GST_VAAPI_RATECONTROL_VCM,
    GST_VAAPI_RATECONTROL_CQP,
    GST_VAAPI_RATECONTROL_VBR_CONSTRAINED,
} GstVaapiRateControl;

G_END_DECLS

#endif /* GST_VAAPI_TYPES_H */
