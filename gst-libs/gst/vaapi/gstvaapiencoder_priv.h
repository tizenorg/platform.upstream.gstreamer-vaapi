/*
 *  gstvaapiencoder_priv.h - VA-API encoder private interface
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

#ifndef GST_VAAPI_ENCODER_PRIV_H
#define GST_VAAPI_ENCODER_PRIV_H

#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiencoder_objects.h>
#include <gst/vaapi/gstvaapicontext.h>
#include <gst/video/gstvideoutils.h>


G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_CLASS(klass) \
    ((GstVaapiEncoderClass *)(klass))

#define GST_IS_VAAPI_ENCODER_CLASS(klass) \
    ((klass) != NULL)

#define GST_VAAPI_ENCODER_GET_CLASS(obj) \
    GST_VAAPI_ENCODER_CLASS(GST_VAAPI_MINI_OBJECT_GET_CLASS(obj))

/* Get GstVaapiDisplay* */
#define GST_VAAPI_ENCODER_DISPLAY(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->display)

/* Get VADisplay */
#define GST_VAAPI_ENCODER_VA_DISPLAY(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->va_display)

/* Get GstVaapiContext* */
#define GST_VAAPI_ENCODER_CONTEXT(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->context)

/* Get VAContext */
#define GST_VAAPI_ENCODER_VA_CONTEXT(encoder) \
    (GST_VAAPI_ENCODER_CAST(encoder)->va_context)

#define GST_VAAPI_ENCODER_VIDEO_INFO(encoder) (GST_VAAPI_ENCODER_CAST(encoder)->video_info)
#define GST_VAAPI_ENCODER_CAPS(encoder)       (GST_VAAPI_ENCODER_CAST(encoder)->caps)
#define GST_VAAPI_ENCODER_WIDTH(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.width)
#define GST_VAAPI_ENCODER_HEIGHT(encoder)     (GST_VAAPI_ENCODER_CAST(encoder)->video_info.height)
#define GST_VAAPI_ENCODER_FPS_N(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.fps_n)
#define GST_VAAPI_ENCODER_FPS_D(encoder)      (GST_VAAPI_ENCODER_CAST(encoder)->video_info.fps_d)
#define GST_VAAPI_ENCODER_RATE_CONTROL(encoder)   \
    (GST_VAAPI_ENCODER_CAST(encoder)->rate_control)


#define GST_VAAPI_ENCODER_LOG_ERROR(...)   GST_ERROR( __VA_ARGS__)
#define GST_VAAPI_ENCODER_LOG_WARNING(...) GST_WARNING( __VA_ARGS__)
#define GST_VAAPI_ENCODER_LOG_DEBUG(...)   GST_DEBUG( __VA_ARGS__)
#define GST_VAAPI_ENCODER_LOG_INFO(...)    GST_INFO( __VA_ARGS__)

#define GST_VAAPI_ENCODER_CHECK_STATUS(exp, err_num, err_reason, ...) \
  if (!(exp)) {                                   \
    ret = err_num;                                 \
    GST_VAAPI_ENCODER_LOG_ERROR(err_reason, ## __VA_ARGS__); \
    goto end;                                      \
  }

typedef struct _GstVaapiCodedBufferProxyClass  GstVaapiCodedBufferProxyClass;
typedef struct _GstVaapiEncoderClass           GstVaapiEncoderClass;

struct _GstVaapiEncoder {
    /*< private >*/
    GstVaapiMiniObject  parent_instance;

    GstVaapiDisplay     *display;
    GstVaapiContext     *context;
    GstCaps             *caps;

    VADisplay            va_display;
    VAContextID          va_context;
    GstVideoInfo         video_info;
    GstVaapiRateControl  rate_control;

    guint                buf_count;
    guint                max_buf_num;
    guint                buf_size;
    GMutex               lock;
    GCond                codedbuf_free;
    GCond                surface_free;
    GQueue               coded_buffers;

    /* queue for sync */
    GQueue               sync_pictures;
    GCond                sync_ready;
};

struct _GstVaapiEncoderClass {
    GObjectClass parent_class;

    gboolean              (*init)            (GstVaapiEncoder* encoder);
    void                  (*destroy)         (GstVaapiEncoder* encoder);

    GstCaps *             (*set_format)      (GstVaapiEncoder* encoder,
                                              GstVideoCodecState *in_state,
                                              GstCaps *ref_caps);

    gboolean              (*get_context_info) (GstVaapiEncoder* encoder,
                                               GstVaapiContextInfo *info);

    GstVaapiEncoderStatus (*reordering)      (GstVaapiEncoder* encoder,
                                              GstVideoCodecFrame *in,
                                              gboolean flush,
                                              GstVaapiEncPicture **out);
    GstVaapiEncoderStatus (*encode)          (GstVaapiEncoder* encoder,
                                              GstVaapiEncPicture *picture,
                                              GstVaapiCodedBufferProxy *codedbuf);

    GstVaapiEncoderStatus (*convert_buf)     (GstVaapiEncoder* encoder,
                                              GstVideoCodecFrame *frame);
    GstVaapiEncoderStatus (*flush)           (GstVaapiEncoder* encoder);

    /* get_codec_data can be NULL */
    GstVaapiEncoderStatus (*get_codec_data)  (GstVaapiEncoder* encoder,
                                              GstBuffer **codec_data);

};

struct _GstVaapiCodedBufferProxy {
    /* private */
    GstVaapiMiniObject   parent_instance;
    GstVaapiEncoder     *encoder;

    /* public */
    GstVaapiCodedBuffer *buffer;
};

struct _GstVaapiCodedBufferProxyClass {
    GstVaapiMiniObjectClass parent_class;
};

void
gst_vaapi_encoder_class_init(GstVaapiEncoderClass *klass);

GstVaapiEncoder *
gst_vaapi_encoder_new(
    const GstVaapiEncoderClass *klass,
    GstVaapiDisplay *display
);

void
gst_vaapi_encoder_finalize(GstVaapiEncoder *encoder);

GstVaapiSurfaceProxy*
gst_vaapi_encoder_create_surface(GstVaapiEncoder* encoder);

void
gst_vaapi_encoder_release_surface(
    GstVaapiEncoder *encoder,
    GstVaapiSurfaceProxy* surface
);

/* ------------------  GstVaapiCodedBufferProxy ---------------------------- */

GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_new(GstVaapiEncoder *encoder);

GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_ref(GstVaapiCodedBufferProxy *proxy);

void
gst_vaapi_coded_buffer_proxy_unref(GstVaapiCodedBufferProxy *proxy);

void
gst_vaapi_coded_buffer_proxy_replace(
    GstVaapiCodedBufferProxy **old_proxy_ptr,
    GstVaapiCodedBufferProxy *new_proxy
);

G_END_DECLS

#endif
