/*
 *  gstvaapiencode.h - VA-API video encoder
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

#ifndef GST_VAAPI_ENCODE_H
#define GST_VAAPI_ENCODE_H

#include <gst/gst.h>
#include "gst/vaapi/gstvaapiencoder.h"

G_BEGIN_DECLS

/* Default templates */
#define GST_CAPS_CODEC(CODEC)          \
    CODEC ", "                         \
    "width  = (int) [ 1, MAX ], "      \
    "height = (int) [ 1, MAX ]; "

#define GST_TYPE_VAAPI_ENCODE \
    (gst_vaapi_encode_get_type())

#define GST_IS_VAAPI_ENCODE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODE))

#define GST_IS_VAAPI_ENCODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODE))

#define GST_VAAPI_ENCODE_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                  \
                                GST_TYPE_VAAPI_ENCODE,  \
                                GstVaapiEncodeClass))

#define GST_VAAPI_ENCODE(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                 \
                                 GST_TYPE_VAAPI_ENCODE, \
                                 GstVaapiEncode))

#define GST_VAAPI_ENCODE_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                  \
                               GST_TYPE_VAAPI_ENCODE,   \
                               GstVaapiEncodeClass))

typedef struct _GstVaapiEncode        GstVaapiEncode;
typedef struct _GstVaapiEncodeClass   GstVaapiEncodeClass;

struct _GstVaapiEncode {
    GstElement          parent_instance;

    GstPad             *sinkpad;
    GstCaps            *sinkpad_caps;

    GstPad             *srcpad;
    GstCaps            *srcpad_caps;

    GstVaapiEncoder    *encoder;
    gboolean            first_src_frame;

    GstVaapiVideoPool  *video_pool;

    /*encode chain lock*/
    GMutex             *mutex;
    GCond              *idle_buf_added;
    GCond              *busy_buf_added;
    gboolean            is_running;
    gboolean            is_buffer_sharing;
};

struct _GstVaapiEncodeClass {
    GstElementClass     parent_class;
    gboolean          (*set_encoder_src_caps)(GstVaapiEncode* encode, GstCaps *caps);
};

GType gst_vaapi_encode_get_type(void);

G_END_DECLS

#endif /* GST_VAAPI_ENCODE_H */

