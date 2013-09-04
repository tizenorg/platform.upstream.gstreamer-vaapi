/*
 *  gstvaapiencode.h - VA-API video encoder
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPIENCODE_H
#define GST_VAAPIENCODE_H

#include <gst/gst.h>
#include <gst/video/gstvideoencoder.h>
#include <gst/vaapi/gstvaapiencoder.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIENCODE \
    (gst_vaapiencode_get_type())

#define GST_IS_VAAPIENCODE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPIENCODE))

#define GST_IS_VAAPIENCODE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPIENCODE))

#define GST_VAAPIENCODE_GET_CLASS(obj)                 \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                  \
                                GST_TYPE_VAAPIENCODE,  \
                                GstVaapiEncodeClass))

#define GST_VAAPIENCODE(obj)                           \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                 \
                                 GST_TYPE_VAAPIENCODE, \
                                 GstVaapiEncode))

#define GST_VAAPIENCODE_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                  \
                               GST_TYPE_VAAPIENCODE,   \
                               GstVaapiEncodeClass))

typedef struct _GstVaapiEncode        GstVaapiEncode;
typedef struct _GstVaapiEncodeClass   GstVaapiEncodeClass;

struct _GstVaapiEncode {
    GstVideoEncoder     parent_instance;

    GstPad             *sinkpad;
    GstCaps            *sinkpad_caps;
    GstPadQueryFunction sinkpad_query;

    GstPad             *srcpad;
    GstCaps            *srcpad_caps;
    GstPadQueryFunction srcpad_query;

    GstVaapiDisplay    *display;
    GstVaapiEncoder    *encoder;

    GstBufferPool      *video_buffer_pool;
    guint               video_buffer_size;

    guint32             is_running     : 1;
    guint32             out_caps_done  : 1;
};

struct _GstVaapiEncodeClass {
    GstVideoEncoderClass     parent_class;
    GstVaapiEncoder *   (*create_encoder)(GstVaapiDisplay *display);
};

GType gst_vaapiencode_get_type(void);

G_END_DECLS

#endif /* GST_VAAPIENCODE_H */
