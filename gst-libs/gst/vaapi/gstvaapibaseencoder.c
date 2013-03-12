/*
 *  gstvaapibaseencoder.c - VA-API base encoder
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

#include "gstvaapibaseencoder.h"

#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>

#include <X11/Xlib.h>

#include <va/va.h>
#include "va/va_x11.h"

#include "gst/gstclock.h"
#include "gst/gstvalue.h"

#include "gstvaapiobject.h"
#include "gstvaapiobject_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapisurface.h"
#include "gstvaapisurfacepool.h"
#include "gstvaapivideobuffer.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidebug.h"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_base_encoder_debug);
#define GST_CAT_DEFAULT gst_vaapi_base_encoder_debug

#define VA_INVALID_PROFILE 0xffffffff
#define DEFAULT_VA_CODEDBUF_NUM  4
#define GST_VAAPI_ENCODER_SURFACE_COUNT 3

#define GST_VAAPI_BASE_ENCODER_LOCK(encoder)              \
    G_STMT_START {                                        \
      g_static_rec_mutex_lock(                            \
        &((GstVaapiBaseEncoder*)(encoder))->priv->mutex); \
    } G_STMT_END

#define GST_VAAPI_BASE_ENCODER_UNLOCK(encoder)             \
    G_STMT_START {                                         \
      g_static_rec_mutex_unlock(                           \
        &((GstVaapiBaseEncoder*)(encoder))->priv->mutex);  \
    } G_STMT_END

struct _GstVaapiBaseEncoderPrivate {
  guint32           format;   /*NV12, I420,*/
  VAProfile         profile;
  /*total encoded frames*/
  guint32           frame_count;
  VABufferID       *coded_bufs;
  guint32           coded_buf_num;
  GStaticRecMutex   mutex;
  GQueue           *idle_buf_queue;
  GQueue           *busy_buf_queue;

  GstVaapiSurfacePool            *surfaces_pool;
  GstVaapiBaseEncoderNotifyStatus notify_status;
  gpointer                        user_data;

  guint             buffer_sharing_flag : 1;
  guint             buffer_notify_flag : 1;
  guint             need_flush        : 1;
};

G_DEFINE_TYPE(GstVaapiBaseEncoder, gst_vaapi_base_encoder, GST_TYPE_VAAPI_ENCODER)

typedef struct _GstVaapiEncoderBufferInfo      GstVaapiEncoderBufferInfo;
typedef struct _GstVaapiEncoderBufferInfoClass GstVaapiEncoderBufferInfoClass;

#define GST_TYPE_VAAPI_ENCODER_BUFFER_INFO  \
    (gst_vaapi_encoder_buffer_info_get_type())

#define GST_VAAPI_ENCODER_BUFFER_INFO(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                             \
                                 GST_TYPE_VAAPI_ENCODER_BUFFER_INFO,\
                                 GstVaapiEncoderBufferInfo))

struct _GstVaapiEncoderBufferInfo {
  GObject              base;
  GstVaapiBaseEncoder *encoder;
  VABufferID          *id;
  GstClockTime         timestamp;
  GstClockTime         duration;

  void                *data;

  guint32              is_key    : 1;
  guint32              is_mapped : 1;
};

struct _GstVaapiEncoderBufferInfoClass {
  GObjectClass   base_class;
};

G_DEFINE_TYPE(
    GstVaapiEncoderBufferInfo,
    gst_vaapi_encoder_buffer_info,
    G_TYPE_OBJECT)

static gboolean
gst_vaapi_encoder_buffer_info_map(
    GstVaapiEncoderBufferInfo *info,
    void **buf
)
{
  g_return_val_if_fail(info, FALSE);
  g_return_val_if_fail(info->encoder && info->id && buf, FALSE);

  GstVaapiDisplay *display = ENCODER_DISPLAY(info->encoder);
  VADisplay va_dpy = ENCODER_VA_DISPLAY(info->encoder);
  VAStatus va_status = VA_STATUS_SUCCESS;

  if (info->is_mapped)
    return TRUE;

  GST_VAAPI_DISPLAY_LOCK(display);
  va_status = vaMapBuffer(va_dpy, *info->id, &info->data);
  GST_VAAPI_DISPLAY_UNLOCK(display);

  g_return_val_if_fail(va_status == VA_STATUS_SUCCESS, FALSE);
  info->is_mapped = TRUE;
  *buf = info->data;
  return TRUE;
}

static gboolean
gst_vaapi_encoder_buffer_info_unmap(
    GstVaapiEncoderBufferInfo *info
)
{
  g_return_val_if_fail(info, FALSE);
  g_return_val_if_fail(info->encoder && info->id, FALSE);

  GstVaapiDisplay *display = ENCODER_DISPLAY(info->encoder);
  VADisplay va_dpy = ENCODER_VA_DISPLAY(info->encoder);
  VAStatus va_status = VA_STATUS_SUCCESS;

  if (!info->is_mapped)
    return TRUE;

  GST_VAAPI_DISPLAY_LOCK(display);
  va_status = vaUnmapBuffer(va_dpy, *info->id);
  GST_VAAPI_DISPLAY_UNLOCK(display);
  info->data = NULL;
  info->is_mapped = FALSE;

  g_return_val_if_fail(va_status == VA_STATUS_SUCCESS, FALSE);
  return TRUE;
}

static GstVaapiEncoderBufferInfo*
pop_busy_buffer_info(GstVaapiBaseEncoder *encoder)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  GstVaapiEncoderBufferInfo *info = NULL;

  g_return_val_if_fail(priv->busy_buf_queue, NULL);

  GST_VAAPI_BASE_ENCODER_LOCK(encoder);

  if (g_queue_is_empty(priv->busy_buf_queue))
    goto end;

  info = (GstVaapiEncoderBufferInfo*)g_queue_pop_head(priv->busy_buf_queue);

end:
  GST_VAAPI_BASE_ENCODER_UNLOCK(encoder);
  return info;
}

static void
push_busy_buffer_info(
    GstVaapiBaseEncoder *encoder,
    GstVaapiEncoderBufferInfo *info)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;

  GST_VAAPI_BASE_ENCODER_LOCK(encoder);
  g_queue_push_tail(priv->busy_buf_queue, info);
  GST_VAAPI_BASE_ENCODER_UNLOCK(encoder);
}

static VABufferID *
pop_idle_buffer_id(GstVaapiBaseEncoder *encoder)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  VABufferID *coded_buf = NULL;

  g_return_val_if_fail(priv->idle_buf_queue, NULL);

  GST_VAAPI_BASE_ENCODER_LOCK(encoder);

  if (g_queue_is_empty(priv->idle_buf_queue))
    goto end;

  coded_buf = (VABufferID*)g_queue_pop_head(priv->idle_buf_queue);

end:
  GST_VAAPI_BASE_ENCODER_UNLOCK(encoder);
  return coded_buf;
}

static void
push_idle_buffer_id(
    GstVaapiBaseEncoder *encoder,
    VABufferID *buf
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;

  GST_VAAPI_BASE_ENCODER_LOCK(encoder);
  g_queue_push_tail(priv->idle_buf_queue, buf);
  GST_VAAPI_BASE_ENCODER_UNLOCK(encoder);
}

static void
gst_vaapi_encoder_buffer_info_init(
    GstVaapiEncoderBufferInfo *info
)
{
    info->encoder   = NULL;
    info->id        = NULL;
    info->timestamp = 0;
    info->duration  = 0;
    info->data      = NULL;
    info->is_key    = FALSE;
    info->is_mapped = FALSE;
}

static void
gst_vaapi_encoder_buffer_info_finalize(GObject *object)
{
  GstVaapiEncoderBufferInfo *info = GST_VAAPI_ENCODER_BUFFER_INFO(object);

  if (info->id && *info->id != VA_INVALID_ID && info->encoder) {
    if (info->is_mapped)
      gst_vaapi_encoder_buffer_info_unmap(info);
    push_idle_buffer_id(info->encoder, info->id);
  }

  if (info->encoder) {
    g_object_unref(info->encoder);
    info->encoder = NULL;
  }
  info->id = NULL;
}

static GstVaapiEncoderBufferInfo*
gst_vaapi_encoder_buffer_info_new(
    GstVaapiBaseEncoder *encoder
)
{
    GstVaapiEncoderBufferInfo *ret;

    g_return_val_if_fail(encoder, NULL);

    ret = GST_VAAPI_ENCODER_BUFFER_INFO(
            g_object_new(GST_TYPE_VAAPI_ENCODER_BUFFER_INFO,
                         NULL));
    if (!ret)
        return NULL;
    ret->encoder = g_object_ref(encoder);
    return ret;
}

static void
gst_vaapi_encoder_buffer_info_class_init(
    GstVaapiEncoderBufferInfoClass *klass
)
{
  GObjectClass * const obj_class = G_OBJECT_CLASS(klass);

  obj_class->finalize = gst_vaapi_encoder_buffer_info_finalize;
}

void
gst_vaapi_base_encoder_set_frame_notify(
    GstVaapiBaseEncoder *encoder,
    gboolean flag
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  priv->buffer_notify_flag = flag;
}

gboolean
gst_vaapi_base_encoder_set_va_profile(
    GstVaapiBaseEncoder *encoder,
    guint profile
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  priv->profile = profile;
  return TRUE;
}

void
gst_vaapi_base_encoder_set_input_format(
    GstVaapiBaseEncoder* encoder,
    guint32 format
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  priv->format = format;
}

gboolean
gst_vaapi_base_encoder_set_notify_status(
    GstVaapiBaseEncoder *encoder,
    GstVaapiBaseEncoderNotifyStatus func,
    gpointer             user_data
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;

  priv->notify_status = func;
  priv->user_data = user_data;
  return TRUE;
}

void
gst_vaapi_base_encoder_set_buffer_sharing(
    GstVaapiBaseEncoder *encoder,
    gboolean is_buffer_sharing
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;

  priv->buffer_sharing_flag = is_buffer_sharing;
}

static gboolean
default_validate_encoder_parameters(
    GstVaapiBaseEncoder *encoder
)
{
  if (!ENCODER_WIDTH(encoder) ||
      !ENCODER_HEIGHT(encoder) ||
      !ENCODER_FPS(encoder)) {
    return FALSE;
  }
  return TRUE;
}

static gboolean
base_encoder_alloc_coded_buffers(
    GstVaapiBaseEncoder *base_encoder,
    GstVaapiContext *context
)
{
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;
  VADisplay va_dpy;
  VAContextID context_id;
  VAStatus va_status = VA_STATUS_SUCCESS;
  guint i = 0;
  gboolean ret = TRUE;
  guint32 buffer_size =
      (ENCODER_WIDTH(base_encoder) * ENCODER_HEIGHT(base_encoder) * 400) /
      (16*16);

  ENCODER_ASSERT(context);
  ENCODER_ASSERT(priv->idle_buf_queue);
  ENCODER_ASSERT(!priv->coded_bufs);

  va_dpy = ENCODER_VA_DISPLAY(base_encoder);
  context_id = gst_vaapi_context_get_id(context);

  priv->coded_bufs = (VABufferID*)
      g_malloc0(priv->coded_buf_num * sizeof(priv->coded_bufs[0]));

  for (i = 0; i < priv->coded_buf_num; i++) {
    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncCodedBufferType,
                               buffer_size, 1,
                               NULL,
                               &priv->coded_bufs[i]);
    if (VA_STATUS_SUCCESS != va_status)
      break;
  }
  ENCODER_CHECK_STATUS(VA_STATUS_SUCCESS == va_status,
                       FALSE,
                       "create coded buffer failed.");

  /* init queue idle_buf_queue */
  GST_VAAPI_BASE_ENCODER_LOCK(base_encoder);
  for (i = 0; i < priv->coded_buf_num; i++) {
    g_queue_push_head(priv->idle_buf_queue, &priv->coded_bufs[i]);
  }
  GST_VAAPI_BASE_ENCODER_UNLOCK(base_encoder);

end:
  return ret;

}

static EncoderStatus
release_coded_buffers(GstVaapiBaseEncoder *base_encoder)
{
  VAStatus va_status = VA_STATUS_SUCCESS;
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;
  GstVaapiDisplay *display = ENCODER_DISPLAY(base_encoder);
  guint32 available_buf_count = priv->coded_buf_num;
  GstVaapiEncoderBufferInfo *info;
  guint32 i;

  ENCODER_ASSERT(display);
  VAAPI_UNUSED_ARG(va_status);
  VADisplay va_dpy = gst_vaapi_display_get_display(display);

  /* wait clear all available coded buffers */
  GST_VAAPI_BASE_ENCODER_LOCK(base_encoder);
  if (available_buf_count) {
    while (!g_queue_is_empty(priv->busy_buf_queue)) {
      info = (GstVaapiEncoderBufferInfo*)g_queue_pop_head(priv->busy_buf_queue);
      g_object_unref(info);
    }
    while(!g_queue_is_empty(priv->idle_buf_queue)) {
      g_queue_pop_head(priv->idle_buf_queue);
      available_buf_count--;
    }
  }
  GST_VAAPI_BASE_ENCODER_UNLOCK(base_encoder);
  ENCODER_ASSERT(available_buf_count == 0);

  for (i = 0; i < priv->coded_buf_num; i++) {
    va_status = vaDestroyBuffer(va_dpy, priv->coded_bufs[i]);
  }

  return ENCODER_NO_ERROR;
}

static EncoderStatus
gst_vaapi_base_encoder_close_default(GstVaapiEncoder* encoder)
{
  GstVaapiBaseEncoder* base_encoder = GST_VAAPI_BASE_ENCODER(encoder);
  GstVaapiBaseEncoderClass *base_class =
      GST_VAAPI_BASE_ENCODER_GET_CLASS(encoder);
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;
  EncoderStatus ret = ENCODER_NO_ERROR;

  /* release buffers first */
  priv->need_flush = FALSE;

  if (base_class->release_resource) {
    base_class->release_resource(base_encoder);
  }
  release_coded_buffers(base_encoder);
  priv->frame_count = 0;

  return ret;
}

static EncoderStatus
gst_vaapi_base_encoder_open_default(
    GstVaapiEncoder* encoder,
    GstVaapiContext **context
)
{
  GstVaapiBaseEncoder* base_encoder = GST_VAAPI_BASE_ENCODER(encoder);
  GstVaapiBaseEncoderClass *base_class =
      GST_VAAPI_BASE_ENCODER_GET_CLASS(encoder);
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;
  GstVaapiDisplay *display = ENCODER_DISPLAY(encoder);

  GstVaapiContext *out_context = NULL;

  EncoderStatus ret = ENCODER_NO_ERROR;
  gboolean check_attri_ret = TRUE;
  /*check and set default values*/
  if (base_class->validate_attributes) {
    check_attri_ret = base_class->validate_attributes(base_encoder);
  } else {
    check_attri_ret = default_validate_encoder_parameters(base_encoder);
  }
  ENCODER_CHECK_STATUS(check_attri_ret,
                       ENCODER_PARAMETER_ERR,
                       "vaapi encoder paramerter error.");
  ENCODER_CHECK_STATUS(VA_INVALID_PROFILE != priv->profile,
                       ENCODER_PROFILE_ERR,
                       "vaapi encoder profile not set.");

  ENCODER_ASSERT(ENCODER_DISPLAY(encoder));

  out_context = gst_vaapi_context_new(display,
                        gst_vaapi_profile(priv->profile),
                        gst_vaapi_entrypoint(VAEntrypointEncSlice),
                        ENCODER_RATE_CONTROL(encoder),
                        ENCODER_WIDTH(encoder),
                        ENCODER_HEIGHT(encoder),
                        GST_VAAPI_ENCODER_SURFACE_COUNT);
  ENCODER_CHECK_STATUS(out_context,
                       ENCODER_CONTEXT_ERR,
                       "gst_vaapi_context_new failed.");
  ENCODER_CHECK_STATUS(VA_INVALID_ID != GST_VAAPI_OBJECT_ID(out_context),
                       ENCODER_CONTEXT_ERR,
                       "gst_vaapi_context_new failed.");

  if (base_class->pre_alloc_resource) {
    ENCODER_CHECK_STATUS(
        base_class->pre_alloc_resource(base_encoder, out_context),
        ENCODER_MEM_ERR,
        "encoder <pre_alloc_resource> failed."
    );
  }
  ENCODER_CHECK_STATUS(
    base_encoder_alloc_coded_buffers(base_encoder, out_context),
    ENCODER_MEM_ERR,
    "encoder <base_encoder_alloc_coded_buffers> failed."
  );
  *context = out_context;

  return ENCODER_NO_ERROR;

end:
  // clear resources
  if (ENCODER_NO_ERROR != ret) {
    gst_vaapi_base_encoder_close_default(encoder);
    if (out_context) {
      g_object_unref(out_context);
    }
  }
  return ret;
}

static EncoderStatus
query_encoding_status(
    GstVaapiBaseEncoder *base_encoder,
    GstVaapiSurface *buffer_surface
)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiSurfaceStatus surface_status;

  ENCODER_CHECK_STATUS(gst_vaapi_surface_sync(buffer_surface),
                       ENCODER_SURFACE_ERR,
                       "gst_vaapi_surface_sync failed.");

  ENCODER_CHECK_STATUS(
      gst_vaapi_surface_query_status(buffer_surface, &surface_status),
      ENCODER_SURFACE_ERR,
      "gst_vaapi_surface_query_status failed."
      );
  if (GST_VAAPI_SURFACE_STATUS_SKIPPED & surface_status) {
    ENCODER_LOG_ERROR("frame skipped"); /* not sure continue or not */
  }

  return ENCODER_NO_ERROR;

end:
  return ret;
}

static inline guint
get_buf_list_size(VACodedBufferSegment *buf_list)
{
  guint size = 0;
  while(buf_list) {
    size += buf_list->size;
    buf_list = (VACodedBufferSegment*)buf_list->next;
  }
  return size;
}

static inline guint
move_buf_list_to_buf(
    VACodedBufferSegment *buf_list,
    guint8 *data,
    guint max_len
)
{
    guint left_size = max_len;
    guint cur_size;
    while (buf_list && left_size) {
      if (buf_list->size <= left_size)
        cur_size = buf_list->size;
      else
        cur_size = left_size;
      memcpy(data, buf_list->buf, cur_size);
      data += cur_size;
      buf_list = (VACodedBufferSegment*)buf_list->next;
      left_size -= cur_size;
    }
    return max_len - left_size;
}

static EncoderStatus
gst_vaapi_base_encoder_get_coded_buffer(
    GstVaapiEncoder* base,
    GstBuffer **out_buf
)
{
  GstVaapiBaseEncoder *encoder = GST_VAAPI_BASE_ENCODER(base);
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  GstVaapiBaseEncoderClass   *klass =
      GST_VAAPI_BASE_ENCODER_GET_CLASS(encoder);
  GstVaapiEncoderBufferInfo* info;
  EncoderStatus ret;
  VACodedBufferSegment *buf_list = NULL;
  GstBuffer* buffer = NULL;
  guint buf_size;

  while((info = pop_busy_buffer_info(encoder)) == NULL){
    ret = ENCODER_NO_BUSY_BUF;
    if (!priv->notify_status ||
        !priv->notify_status(encoder, ret, priv->user_data))
      goto end;
  }
  ret = ENCODER_NO_ERROR;

  ENCODER_CHECK_STATUS(
      info->id && *info->id != VA_INVALID_ID,
      ENCODER_DATA_ERR,
      "get invalid buffer info"
      );

  ENCODER_CHECK_STATUS(
      gst_vaapi_encoder_buffer_info_map(info, (void**)&buf_list),
      ENCODER_DATA_ERR,
      "vaMapBuffer failed");

  buf_size = get_buf_list_size(buf_list);
  ENCODER_CHECK_STATUS(
      buf_size,
      ENCODER_DATA_ERR,
      "encoded size:0");
  buffer = gst_buffer_new_and_alloc(buf_size);
  GST_BUFFER_SIZE(buffer) = move_buf_list_to_buf(
                                buf_list,
                                GST_BUFFER_DATA(buffer),
                                buf_size);

  if (priv->buffer_notify_flag && klass->notify_buffer) {
    klass->notify_buffer(
        encoder,
        GST_BUFFER_DATA(buffer),
        GST_BUFFER_SIZE(buffer));
  }

  if (klass->wrap_buffer) {
    GstBuffer *new_buf = klass->wrap_buffer(encoder, buffer);
    gst_buffer_unref(buffer);
    buffer = new_buf;
  }

  if (buffer) {
    GST_BUFFER_TIMESTAMP(buffer) = info->timestamp;
    GST_BUFFER_DURATION(buffer) = info->duration;
    if (!info->is_key)
      GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_DELTA_UNIT);
  }
  FPS_CALCULATION(vaapiencode);

  *out_buf = buffer;

end:
  if (info) {
    gst_vaapi_encoder_buffer_info_unmap(info);
    g_object_unref(info);
  }
  return ret;
}

static GstVaapiVideoBuffer *
_get_video_buffer(
    GstVaapiBaseEncoder* encoder,
    GstBuffer *buf
)
{
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  GstBuffer *video_buffer;

  if (priv->buffer_sharing_flag)
    video_buffer = (GstBuffer *)buf->data;
  else
    video_buffer = buf;

  if (GST_VAAPI_IS_VIDEO_BUFFER(video_buffer))
    return GST_VAAPI_VIDEO_BUFFER(video_buffer);
  return NULL;
}

static EncoderStatus
gst_vaapi_base_encoder_encode_default(
    GstVaapiEncoder* base,
    GstBuffer *pic
)
{
  GstVaapiBaseEncoder* encoder = GST_VAAPI_BASE_ENCODER(base);
  GstVaapiBaseEncoderClass *base_class =
      GST_VAAPI_BASE_ENCODER_GET_CLASS(encoder);
  GstVaapiBaseEncoderPrivate *priv = encoder->priv;
  GstVaapiDisplay *display = ENCODER_DISPLAY(encoder);
  GstVaapiContext *context = ENCODER_CONTEXT(encoder);
  EncoderStatus ret = ENCODER_NO_ERROR;
  gboolean is_key = FALSE;
  VABufferID* coded_buf = NULL;
  GstVaapiSurface *buffer_surface = NULL;
  gboolean is_prepared_buffer = FALSE;

  ENCODER_ASSERT(display && context);

  /* Video Buffer */
  GstVaapiVideoBuffer *video_buffer = NULL;

  ENCODER_CHECK_STATUS(pic || base_class->prepare_next_input_buffer,
                       ENCODER_DATA_ERR,
                       "Need a picture to encode");
  if (!pic)
    priv->need_flush = TRUE;

again:
  if (base_class->prepare_next_input_buffer) {
    GstBuffer* tmp_buf = NULL;
    ret = base_class->prepare_next_input_buffer(encoder,
                                                pic,
                                                priv->need_flush,
                                                &tmp_buf);
    priv->need_flush = FALSE;
    if (ret != ENCODER_NO_ERROR || !tmp_buf)
      goto end;

    is_prepared_buffer = TRUE;
    pic = tmp_buf;
  }

  video_buffer = _get_video_buffer(encoder, pic);
  ENCODER_CHECK_STATUS(
      video_buffer,
      ENCODER_SURFACE_ERR,
      "vaapi encoder doesn't has video buffer");

  buffer_surface = gst_vaapi_video_buffer_get_surface(video_buffer);

  while ((coded_buf = pop_idle_buffer_id(encoder)) == NULL) {
    ret = ENCODER_NO_IDLE_BUF;
    if (!priv->notify_status ||
        !priv->notify_status(encoder, ret, priv->user_data))
      goto end;
  }
  ret = ENCODER_NO_ERROR;

  /* prepare frame*/
  ret = base_class->render_frame(encoder,
                                 buffer_surface,
                                 priv->frame_count,
                                 *coded_buf,
                                 &is_key);
  /* prepare failed, push back */
  if (ENCODER_NO_ERROR != ret) {
    push_idle_buffer_id(encoder, coded_buf);
  }
  ENCODER_CHECK_STATUS(ENCODER_NO_ERROR == ret,
                       ENCODER_PICTURE_ERR,
                       "base_prepare_encoding failed");

  /*query surface result*/
  ret = query_encoding_status(encoder, buffer_surface);
  if (ENCODER_NO_ERROR != ret) {
    goto end;
  }

  /* Push coded buffer to another task */
  GstVaapiEncoderBufferInfo* info = gst_vaapi_encoder_buffer_info_new(encoder);
  info->id = coded_buf;
  info->timestamp = GST_BUFFER_TIMESTAMP(pic);
  info->duration = GST_BUFFER_DURATION(pic);
  info->is_key = is_key;
  push_busy_buffer_info(encoder, info);

  priv->frame_count++;

  if (is_prepared_buffer) {
    if (pic)
      gst_buffer_unref(pic);
    pic = NULL;
    buffer_surface = NULL;
    coded_buf = NULL;
    goto again;
  }

end:
  if (ret < ENCODER_NO_ERROR && base_class->encode_frame_failed) {
    base_class->encode_frame_failed(encoder, video_buffer);
  }
  if (pic && is_prepared_buffer)
    gst_buffer_unref(pic);

  return ret;
}

#if 0
static EncoderStatus
base_put_raw_buffer_to_surface(GstVaapiBaseEncoder *base_encoder,
                               GstVaapiDisplay *display,
                               GstBuffer *raw_pic,
                               GstVaapiSurface *surface)
{
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiImage *image;
  GstVaapiImageFormat image_format;
  guint8 *y_src = NULL, *u_src = NULL, *v_src = NULL;
  guint8 *y_dst = NULL, *u_dst = NULL, *v_dst = NULL;
  int y_size = 0, u_size = 0;
  int row = 0, col = 0;
  guint32 plane_count = 0;
  guint32 image_width = 0, image_height = 0;
  guint32 pitchy = 0, pitchu = 0, pitchv = 0;
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;

  ENCODER_ASSERT(display);
  VAAPI_UNUSED_ARG(pitchv);
  VAAPI_UNUSED_ARG(v_dst);
  /*map image*/
  image = gst_vaapi_surface_derive_image(surface);
  gst_vaapi_image_map(image);

  image_format = gst_vaapi_image_get_format(image);
  image_width = gst_vaapi_image_get_width(image);
  image_height = gst_vaapi_image_get_height(image);

  /* copy buffer to surface */
  ENCODER_ASSERT(GST_BUFFER_SIZE(raw_pic) >= y_size + (y_size>>1));

  y_size = ENCODER_WIDTH(base_encoder) * ENCODER_HEIGHT(base_encoder);
  u_size = ((ENCODER_WIDTH(base_encoder)+1) >> 1) * ((ENCODER_HEIGHT(base_encoder)+1) >> 1);

  y_src = GST_BUFFER_DATA(raw_pic);
  u_src = y_src + y_size;
  v_src = u_src + u_size;

  plane_count = gst_vaapi_image_get_plane_count(image);
  y_dst = gst_vaapi_image_get_plane(image, 0);
  u_dst = gst_vaapi_image_get_plane(image, 1);
  pitchy = gst_vaapi_image_get_pitch(image, 0);
  pitchu = gst_vaapi_image_get_pitch(image, 1);

  if (plane_count > 2) {
    v_dst = gst_vaapi_image_get_plane(image, 2);
    pitchv = gst_vaapi_image_get_pitch(image, 2);
  }

  /* copy from avcenc.c*/
  /* Y plane */
  for (row = 0; row < image_height; row++) {
      memcpy(y_dst, y_src, image_width);
      y_dst += pitchy;
      y_src += ENCODER_WIDTH(base_encoder);
  }

  if (GST_VAAPI_IMAGE_NV12 == image_format) { /* UV plane */
    if (GST_VAAPI_IMAGE_I420 == priv->format) {
      for (row = 0; row < image_height / 2; row++) {
          for (col = 0; col < image_width / 2; col++) {
              u_dst[col * 2] = u_src[col];
              u_dst[col * 2 + 1] = v_src[col];
          }

          u_dst += pitchu;
          u_src += (ENCODER_WIDTH(base_encoder)>>1);
          v_src += (ENCODER_WIDTH(base_encoder)>>1);
      }
    } else if (GST_VAAPI_IMAGE_NV12 == priv->format){
      for (row = 0; row < image_height / 2; row++) {
        memcpy(u_dst, u_src, image_width);
        u_src += ENCODER_WIDTH(base_encoder);
        u_dst += pitchu;
      }
    } else {
      ENCODER_ASSERT(0);
    }
  } else {
      /* FIXME: fix this later */
      ENCODER_ASSERT(0);
  }

  /*unmap image*/
  g_object_unref(image);

  return ret;
}
#endif

static EncoderStatus
gst_vaapi_base_encoder_flush_default(
    GstVaapiEncoder* encoder
)
{
  GstVaapiBaseEncoder* base_encoder = GST_VAAPI_BASE_ENCODER(encoder);
  EncoderStatus ret = ENCODER_NO_ERROR;
  GstVaapiBaseEncoderPrivate *priv = base_encoder->priv;

  priv->frame_count = 0;
  priv->need_flush = TRUE;
  /*do we need destroy priv->seq_parameter? */

  //end:
  return ret;
}


static void
gst_vaapi_base_encoder_finalize(GObject *object)
{
  /*free private buffers*/
  GstVaapiEncoder *encoder = GST_VAAPI_ENCODER(object);
  GstVaapiBaseEncoderPrivate *priv =
      GST_VAAPI_BASE_ENCODER_GET_PRIVATE(object);

  if (gst_vaapi_encoder_get_state(encoder) != VAAPI_ENC_NULL) {
    gst_vaapi_encoder_uninitialize(encoder);
  }

  g_static_rec_mutex_free(&priv->mutex);
  if (priv->idle_buf_queue)
    g_queue_free(priv->idle_buf_queue);

  if (priv->busy_buf_queue)
    g_queue_free(priv->busy_buf_queue);

  G_OBJECT_CLASS(gst_vaapi_base_encoder_parent_class)->finalize(object);
}

static void
gst_vaapi_base_encoder_init(GstVaapiBaseEncoder *encoder)
{
  GstVaapiBaseEncoderPrivate *priv =
      GST_VAAPI_BASE_ENCODER_GET_PRIVATE(encoder);
  ENCODER_ASSERT(priv);
  encoder->priv = priv;

  /* init private values*/
  priv->format = 0;
  priv->profile= VA_INVALID_PROFILE;
  priv->frame_count = 0;
  priv->buffer_sharing_flag = FALSE;
  priv->buffer_notify_flag = FALSE;

  priv->coded_bufs = NULL;
  priv->coded_buf_num = DEFAULT_VA_CODEDBUF_NUM;
  priv->idle_buf_queue = g_queue_new();
  priv->busy_buf_queue = g_queue_new();
  g_static_rec_mutex_init(&priv->mutex);

  priv->notify_status = NULL;
  priv->user_data = NULL;

  priv->need_flush = FALSE;
}

static void
gst_vaapi_base_encoder_class_init(GstVaapiBaseEncoderClass *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstVaapiEncoderClass * const encoder_class = GST_VAAPI_ENCODER_CLASS(klass);
  g_type_class_add_private(klass, sizeof(GstVaapiBaseEncoderPrivate));

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_base_encoder_debug,
                           "gst_vaapi_base_encoder",
                           0,
                           "gst_vaapi_base_encoder element");

  object_class->finalize = gst_vaapi_base_encoder_finalize;

  encoder_class->open = gst_vaapi_base_encoder_open_default;
  encoder_class->close = gst_vaapi_base_encoder_close_default;
  encoder_class->encode = gst_vaapi_base_encoder_encode_default;
  encoder_class->flush = gst_vaapi_base_encoder_flush_default;
  encoder_class->get_buf = gst_vaapi_base_encoder_get_coded_buffer;
  encoder_class->get_codec_data = NULL;

  /* user defined functions*/
  klass->validate_attributes = NULL;
  klass->pre_alloc_resource = NULL;
  klass->release_resource = NULL;
  klass->prepare_next_input_buffer = NULL;
  klass->render_frame = NULL;
  klass->notify_buffer = NULL;
  klass->wrap_buffer = NULL;
  klass->encode_frame_failed = NULL;

  /*
  object_class->set_property = gst_vaapi_base_encoder_set_property;
  object_class->get_property = gst_vaapi_base_encoder_get_property;
  */
}
