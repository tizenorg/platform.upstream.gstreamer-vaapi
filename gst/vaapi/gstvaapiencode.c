/*
 *  gstvaapiencode.c - VA-API video encoder
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

#include "config.h"
#include "gstvaapiencode.h"

#include <string.h>
#include <X11/Xlib.h>
#include <gst/video/videocontext.h>
#include "gst/vaapi/gstvaapibaseencoder.h"

#include "gstvaapiencode_h264.h"
#include "gstvaapiencode_h263.h"
#include "gstvaapiencode_mpeg4.h"
#include "gstvaapipluginutil.h"
#include "gstvaapipluginbuffer.h"

/* gst_debug
     GST_DEBUG_CATEGORY_STATIC (gst_vaapi_encode_debug)
     #define GST_CAT_DEFAULT gst_vaapi_encode_debug
         //class_init
         GST_DEBUG_CATEGORY_INIT (gst_vaapi_encode_debug, "vaapiencode", 0,
          "vaapiencode element");
*/
GST_DEBUG_CATEGORY_STATIC (gst_vaapi_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_encode_debug

#define GST_VAAPI_ENCODE_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj),                  \
                                  GST_TYPE_VAAPI_ENCODE,  \
                                  GstVaapiEncodePrivate))

typedef struct _GstVaapiEncodePrivate GstVaapiEncodePrivate;

#define GstVideoContextClass GstVideoContextInterface

#define GST_VAAPI_ENCODE_MUTEX_LOCK(encode)  \
    G_STMT_START{ g_mutex_lock((encode)->mutex); }G_STMT_END

#define GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode) \
    G_STMT_START{ g_mutex_unlock((encode)->mutex); }G_STMT_END

#define GST_VAAPI_ENCODE_IDLE_BUF_WAIT(encode)                       \
        G_STMT_START {                                               \
            g_cond_wait((encode)->idle_buf_added, (encode)->mutex);  \
        } G_STMT_END

#define GST_VAAPI_ENCODE_IDLE_BUF_SIGNAL(encode)                     \
        G_STMT_START {                                               \
            g_cond_signal((encode)->idle_buf_added);                 \
        } G_STMT_END

#define GST_VAAPI_ENCODE_BUSY_BUF_WAIT(encode)                       \
        G_STMT_START {                                               \
            g_cond_wait((encode)->busy_buf_added, (encode)->mutex);  \
        } G_STMT_END

#define GST_VAAPI_ENCODE_BUSY_BUF_SIGNAL(encode)                     \
        G_STMT_START {                                               \
            g_cond_signal((encode)->busy_buf_added);                 \
        } G_STMT_END

GST_BOILERPLATE_WITH_INTERFACE(
    GstVaapiEncode,
    gst_vaapi_encode,
    GstElement,
    GST_TYPE_ELEMENT,
    GstVideoContext,
    GST_TYPE_VIDEO_CONTEXT,
    gst_video_context)


enum {
    PROP_0,
};

static char*
_encode_dump_caps(GstCaps *cpas)
{
  guint i = 0, j = 0;
  GstStructure const *structure;
  GValue const *value;
  static char caps_string[4096*5];
  char *tmp;

  char *cur = caps_string;
  memset(caps_string, 0, sizeof(caps_string));
  for (i = 0; i < gst_caps_get_size(cpas); i++) {
    structure = gst_caps_get_structure(cpas, i);
    const char* caps_name = gst_structure_get_name (structure);
    sprintf(cur, "cap_%02d:%s\n", i, caps_name);
    cur += strlen(cur);

    for (j = 0; j < gst_structure_n_fields(structure); j++) {
      const char* name = gst_structure_nth_field_name(structure, j);
      value = gst_structure_get_value(structure, name);
      tmp = gst_value_serialize(value);
      sprintf(cur, "\t%s:%s(%s)\n", name, tmp, G_VALUE_TYPE_NAME(value));
      cur += strlen(cur);
      g_free(tmp);
    }
  }

  return caps_string;
}

/* context(display) interface */
static void
gst_vaapi_encode_set_video_context(
    GstVideoContext *context,
    const gchar *type,
    const GValue *value
)
{
    GstVaapiEncode *encode = GST_VAAPI_ENCODE (context);
    GstVaapiDisplay *display = NULL;

    gst_vaapi_set_display (type, value, &display);
    gst_vaapi_encoder_set_display(encode->encoder, display);
}

static gboolean
gst_video_context_supported (GstVaapiEncode *decode, GType iface_type)
{
  return (iface_type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapi_encode_set_video_context;
}

static gboolean
gst_vaapi_encode_query (GstPad *pad, GstQuery *query)
{
    GstVaapiEncode *encode = GST_VAAPI_ENCODE (gst_pad_get_parent_element (pad));
    gboolean res;

    if (encode->encoder &&
        gst_vaapi_reply_to_query(query, ENCODER_DISPLAY(encode->encoder)))
      res = TRUE;
    else
      res = gst_pad_query_default (pad, query);

    g_object_unref (encode);
    return res;
}

static inline gboolean
gst_vaapi_encode_ensure_display(GstVaapiEncode *encode)
{
    return gst_vaapi_ensure_display(encode,
                                    GST_VAAPI_DISPLAY_TYPE_ANY,
                                    &ENCODER_DISPLAY(encode->encoder));
}

static void
gst_vaapi_encode_buffer_loop(GstVaapiEncode *encode)
{
  GstBuffer *buf = NULL;
  EncoderStatus encoder_ret = ENCODER_NO_ERROR;
  gboolean is_running;

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);
  is_running = encode->is_running;
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
  if (!is_running)
    return;

  encoder_ret =
      gst_vaapi_encoder_get_encoded_buffer(
          encode->encoder,
          &buf);
  if (encoder_ret < ENCODER_NO_ERROR) {
    ENCODER_LOG_ERROR("get encoded buffer failed");
    return;
  }
  GST_VAAPI_ENCODE_IDLE_BUF_SIGNAL(encode);

  if (!buf)
    return;

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);
  if (encode->first_src_frame) { /* Set src pad caps and codec data */
    GstBuffer *codec_data = NULL;
    if ((ENCODER_NO_ERROR ==
         gst_vaapi_encoder_get_codec_data(encode->encoder, &codec_data)) &&
        codec_data) {
      gst_caps_set_simple(encode->srcpad_caps,
                          "codec_data",GST_TYPE_BUFFER, codec_data,
                          NULL);
    }
    gst_pad_set_caps (encode->srcpad, encode->srcpad_caps);
    GST_BUFFER_CAPS(buf) = gst_caps_ref(encode->srcpad_caps);
    ENCODER_LOG_INFO("gst_vaapi_encode_chain 1st push-buffer caps,\n%s",
                     _encode_dump_caps(encode->srcpad_caps));
    encode->first_src_frame = FALSE;
  }
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
  ENCODER_LOG_DEBUG(
      "output:%" GST_TIME_FORMAT ", 0x%s",
      GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buf)),
      vaapi_encoder_dump_bytes(GST_BUFFER_DATA(buf),
                               (GST_BUFFER_SIZE(buf) > 16 ?
                                16: GST_BUFFER_SIZE(buf)))
                   );
  gst_pad_push(encode->srcpad, buf);
}

static gboolean
_encoder_status_callback(
    GstVaapiBaseEncoder* encoder,
    EncoderStatus status,
    void* user_data
)
{
  GstVaapiEncode *encode = (GstVaapiEncode *)user_data;
  gboolean ret = FALSE;

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);

  if (!encode->is_running) {
    ret = FALSE;
    goto end;
  }

  switch (status) {
  case ENCODER_NO_IDLE_BUF:
    GST_VAAPI_ENCODE_IDLE_BUF_WAIT(encode);
    ret = TRUE;
    break;

  case ENCODER_NO_BUSY_BUF:
    GST_VAAPI_ENCODE_BUSY_BUF_WAIT(encode);
    ret = TRUE;
    break;

  default:
    ret = FALSE;
    goto end;
  }

  if (!encode->is_running)
    ret = FALSE;

end:
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);

  return ret;
}

static gboolean
gst_vaapi_encode_set_caps(GstPad *sink_pad, GstCaps *caps)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(GST_OBJECT_PARENT(sink_pad));
  GstVaapiEncodeClass *encode_class = GST_VAAPI_ENCODE_GET_CLASS(encode);
  GstStructure *structure = NULL, *src_struct = NULL;
  gint width = 0, height = 0;
  gint fps_n = 0, fps_d = 0;
  const GValue *fps_value = NULL, *format_value;
  guint32 format = 0;
  gboolean ret = TRUE;
  EncoderStatus encoder_ret = ENCODER_NO_ERROR;

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);

  encode->sinkpad_caps = caps;
  gst_caps_ref(caps);
  ENCODER_LOG_INFO("gst_vaapi_encode_set_caps,\n%s",
                   _encode_dump_caps(caps));

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name(structure, GST_VAAPI_BUFFER_SHARING_CAPS_NAME))
    encode->is_buffer_sharing = TRUE;
  else
    encode->is_buffer_sharing = FALSE;

  if (gst_structure_get_int (structure, "width", &width)) {
    encode->encoder->width = width;
  }
  if (gst_structure_get_int (structure, "height", &height)) {
    encode->encoder->height = height;
  }
  fps_value = gst_structure_get_value (structure, "framerate");
  if (fps_value) {
    fps_n = gst_value_get_fraction_numerator (fps_value);
    fps_d = gst_value_get_fraction_denominator (fps_value);
    encode->encoder->frame_rate = fps_n/fps_d;
  }
  format_value = gst_structure_get_value (structure, "format");
  if (format_value && GST_IS_VAAPI_ENCODE_H264(encode)) {
    ENCODER_CHECK_STATUS((format_value &&
                          GST_TYPE_FOURCC == G_VALUE_TYPE(format_value)),
                         FALSE,
                         "1st buffer caps' format type is not fourcc.");
    format = gst_value_get_fourcc (format_value);
    if (format) {
      gst_vaapi_base_encoder_set_input_format(
          GST_VAAPI_BASE_ENCODER(encode->encoder),
          format);
    }
  }

  /*set src pad caps*/
  if (encode->srcpad_caps) {
    gst_caps_unref(encode->srcpad_caps);
  }
  encode->srcpad_caps =
      gst_caps_copy(gst_pad_get_pad_template_caps(encode->srcpad));
  src_struct = gst_caps_get_structure(encode->srcpad_caps, 0);
  gst_structure_set(src_struct,
                    "width", G_TYPE_INT, width,
                    "height", G_TYPE_INT, height,
                    "framerate", GST_TYPE_FRACTION, fps_n, fps_d,
                    NULL);
  if (encode_class->set_encoder_src_caps) {
    encode_class->set_encoder_src_caps(encode, encode->srcpad_caps);
  }

  /*set display and initialize encoder*/
  ENCODER_CHECK_STATUS(gst_vaapi_encode_ensure_display(encode),
                       FALSE,
                       "encoder ensure display failed on setting caps.");

  gst_vaapi_base_encoder_set_notify_status(
      GST_VAAPI_BASE_ENCODER(encode->encoder),
      _encoder_status_callback,
      encode);
  gst_vaapi_base_encoder_set_buffer_sharing(
      GST_VAAPI_BASE_ENCODER(encode->encoder),
      encode->is_buffer_sharing);

  encoder_ret = gst_vaapi_encoder_initialize(encode->encoder);
  ENCODER_CHECK_STATUS (ENCODER_NO_ERROR == encoder_ret,
                        FALSE,
                        "gst_vaapi_encoder_initialize failed.");

  encoder_ret = gst_vaapi_encoder_open(encode->encoder);
  ENCODER_CHECK_STATUS (ENCODER_NO_ERROR == encoder_ret,
                        FALSE,
                        "gst_vaapi_encoder_open failed.");

  encode->is_running = TRUE;

end:
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
  if (ret) {
    ret = gst_pad_start_task(encode->srcpad,
                             (GstTaskFunction)gst_vaapi_encode_buffer_loop,
                             encode);
	if (!ret)
	   ENCODER_LOG_INFO("gstvaapiencode start task failed.");
  }
  return ret;
}

static GstCaps *
gst_vaapi_encode_get_caps(GstPad *sink_pad)
{
  GstCaps *caps = NULL;
  GstVaapiEncode * const encode =
      GST_VAAPI_ENCODE(GST_OBJECT_PARENT(sink_pad));

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);

  if (encode->sinkpad_caps) {
    gst_caps_ref(encode->sinkpad_caps);
    ENCODER_LOG_INFO("get caps,\n%s",
                     _encode_dump_caps(encode->sinkpad_caps));
    caps = encode->sinkpad_caps;
  } else
    caps = gst_caps_copy(gst_pad_get_pad_template_caps(sink_pad));

  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
  return caps;
}

static GstStateChangeReturn
gst_vaapi_encode_change_state(
    GstElement *element,
    GstStateChange transition
)
{
  GstVaapiEncode * const encode = GST_VAAPI_ENCODE(element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
  case GST_STATE_CHANGE_READY_TO_PAUSED:
    break;
  case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    break;
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY: {
    GST_VAAPI_ENCODE_MUTEX_LOCK (encode);
    encode->is_running = FALSE;
    encode->is_buffer_sharing = FALSE;
    GST_VAAPI_ENCODE_BUSY_BUF_SIGNAL(encode);
    GST_VAAPI_ENCODE_IDLE_BUF_SIGNAL(encode);
    GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
    gst_pad_stop_task(encode->srcpad);
    break;
  }
  default:
    break;
  }

  ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
  case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_CHANGE_PAUSED_TO_READY:
    gst_vaapi_encoder_close(encode->encoder);
    GST_VAAPI_ENCODE_MUTEX_LOCK(encode);
    if (encode->video_pool) {
      g_object_unref(encode->video_pool);
      encode->video_pool = NULL;
    }
    GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
    break;
  default:
    break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static GstFlowReturn
gst_vaapi_encode_chain(GstPad *sink_pad, GstBuffer *buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(GST_OBJECT_PARENT(sink_pad));
  EncoderStatus encoder_ret = ENCODER_NO_ERROR;
  gboolean is_buffer_sharing;

  ENCODER_ASSERT(encode && encode->encoder);

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);
  is_buffer_sharing = encode->is_buffer_sharing;
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);
  
  if (!is_buffer_sharing && !GST_VAAPI_IS_VIDEO_BUFFER(buf)) {
    ENCODER_LOG_ERROR("gst_vaapi_encode_chain parameter doesn't have video buffer.");
    ret = ENCODER_NO_ERROR;
    goto end;
  }

  /*encoding frames*/
  encoder_ret = gst_vaapi_encoder_encode(encode->encoder, buf);
  GST_VAAPI_ENCODE_BUSY_BUF_SIGNAL(encode);
  ENCODER_CHECK_STATUS (ENCODER_NO_ERROR <= encoder_ret,
                        GST_FLOW_ERROR,
                        "gst_vaapi_encoder_encode failed.");

end:
  gst_buffer_unref(buf);
  return ret;
}

static gboolean
gst_vaapi_encode_ensure_video_pool(
    GstVaapiEncode *encode,
    GstCaps *caps)
{
  GstStructure *structure;
  gint width = 0, height = 0;

  g_return_val_if_fail(caps, FALSE);

  structure = gst_caps_get_structure(caps, 0);
  g_return_val_if_fail(structure, FALSE);

  if (encode->video_pool)
    return TRUE;

  if (!gst_structure_get_int(structure, "width",  &width) ||
      !gst_structure_get_int(structure, "height", &height))
    return FALSE;

  encode->video_pool =
      gst_vaapi_surface_pool_new(ENCODER_DISPLAY(encode->encoder), caps);
  if (!encode->video_pool)
    return FALSE;

  return TRUE;
}

static GstFlowReturn
gst_vaapi_encode_buffer_alloc(
    GstPad * pad,
    guint64 offset,
    guint size,
    GstCaps * caps,
    GstBuffer ** buf
)
{
  GstVaapiEncode * const encode = GST_VAAPI_ENCODE(GST_OBJECT_PARENT(pad));
  GstStructure *structure = NULL;
  GstBuffer *video_buffer = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;

  if (!caps)
    return GST_FLOW_ERROR;

  structure = gst_caps_get_structure(caps, 0);
  if (!structure ||
      (!gst_structure_has_name(structure, GST_VAAPI_SURFACE_CAPS_NAME) &&
       !gst_structure_has_name(structure, GST_VAAPI_BUFFER_SHARING_CAPS_NAME)))
    return GST_FLOW_ERROR;

  ENCODER_CHECK_STATUS(
      gst_vaapi_encode_ensure_display(encode),
      GST_FLOW_ERROR,
      "gst_vaapi_encode_buffer_alloc can't ensure display");

  GST_VAAPI_ENCODE_MUTEX_LOCK(encode);
  if (!gst_vaapi_encode_ensure_video_pool(encode, caps))
    goto unlock;

  video_buffer = gst_vaapi_video_buffer_new_from_pool(encode->video_pool);

unlock:
  GST_VAAPI_ENCODE_MUTEX_UNLOCK(encode);

  ENCODER_CHECK_STATUS(video_buffer,
                       GST_FLOW_ERROR,
                       "gst_vaapi_encode_buffer_alloc failed.");

  if (gst_structure_has_name(structure, GST_VAAPI_BUFFER_SHARING_CAPS_NAME)) {
    ENCODER_CHECK_STATUS(
        gst_vaapi_video_buffer_ensure_pointer(GST_VAAPI_VIDEO_BUFFER(video_buffer)),
        GST_FLOW_ERROR,
        "gst_vaapi_video_buffer_ensure_pointer failed.");
  }

  if (caps) {
    gst_buffer_set_caps(video_buffer, caps);
  }
  *buf = video_buffer;
  ret = GST_FLOW_OK;

end:
  return ret;
}

static void
gst_vaapi_encode_finalize(GObject *object)
{
  GstVaapiEncode * const encode = GST_VAAPI_ENCODE(object);

  if (encode->sinkpad_caps) {
    gst_caps_unref(encode->sinkpad_caps);
    encode->sinkpad_caps = NULL;
  }
  encode->sinkpad = NULL;

  if (encode->srcpad_caps) {
    gst_caps_unref(encode->srcpad_caps);
    encode->srcpad_caps = NULL;
  }
  encode->srcpad = NULL;

  if (encode->video_pool)
    g_object_unref(encode->video_pool);

  if (encode->encoder) {
      gst_vaapi_encoder_close(encode->encoder);
      gst_vaapi_encoder_uninitialize(encode->encoder);
      gst_vaapi_encoder_unref(encode->encoder);
      encode->encoder = NULL;
  }

  g_mutex_free(encode->mutex);
  g_cond_free(encode->idle_buf_added);
  g_cond_free(encode->busy_buf_added);

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
gst_vaapi_encode_init(
    GstVaapiEncode *encode,
    GstVaapiEncodeClass *klass
)
{
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  encode->sinkpad_caps       = NULL;
  encode->srcpad_caps        = NULL;
  encode->first_src_frame    = TRUE;
  encode->is_running         = FALSE;
  encode->is_buffer_sharing  = FALSE;

  encode->encoder = NULL;
  encode->video_pool = NULL;

  encode->mutex = g_mutex_new();
  encode->idle_buf_added = g_cond_new();
  encode->busy_buf_added = g_cond_new();

  /*sink pad */
  encode->sinkpad = gst_pad_new_from_template(
      gst_element_class_get_pad_template(element_class, "sink"),
      "sink"
  );
  gst_pad_set_getcaps_function(encode->sinkpad, gst_vaapi_encode_get_caps);
  gst_pad_set_setcaps_function(encode->sinkpad, gst_vaapi_encode_set_caps);
  gst_pad_set_chain_function(encode->sinkpad, gst_vaapi_encode_chain);
  gst_pad_set_bufferalloc_function(encode->sinkpad,
                                   gst_vaapi_encode_buffer_alloc);
  /*gst_pad_set_event_function(encode->sinkpad, gst_vaapi_encode_sink_event); */
  /*gst_pad_use_fixed_caps(encode->sinkpad);*/
  gst_pad_set_query_function(encode->sinkpad, gst_vaapi_encode_query);
  gst_element_add_pad(GST_ELEMENT(encode), encode->sinkpad);

  /* src pad */
  encode->srcpad = gst_pad_new_from_template(
      gst_element_class_get_pad_template(element_class, "src"),
      "src"
  );
  encode->srcpad_caps = NULL;

  gst_pad_use_fixed_caps(encode->srcpad);
  /*gst_pad_set_event_function(encode->srcpad, gst_vaapi_encode_src_event);*/
  gst_pad_set_query_function(encode->srcpad, gst_vaapi_encode_query);
  gst_element_add_pad(GST_ELEMENT(encode), encode->srcpad);
}

static void
gst_vaapi_encode_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  ENCODER_ASSERT(encode->encoder);

  switch (prop_id) {
  }
}

static void
gst_vaapi_encode_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
  GstVaapiEncode *encode = GST_VAAPI_ENCODE(object);
  ENCODER_ASSERT(encode->encoder);

  switch (prop_id) {
  }
}

static void
gst_vaapi_encode_base_init(gpointer klass)
{
  #if 0
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  gst_element_class_set_details(element_class, &gst_vaapi_encode_details);

  /* sink pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_sink_factory)
  );

  /* src pad */
  gst_element_class_add_pad_template(
      element_class,
      gst_static_pad_template_get(&gst_vaapi_encode_src_factory)
  );
  #endif
}

static void
gst_vaapi_encode_class_init(GstVaapiEncodeClass *klass)
{
  GObjectClass * const object_class = G_OBJECT_CLASS(klass);
  GstElementClass * const element_class = GST_ELEMENT_CLASS(klass);

  object_class->finalize      = gst_vaapi_encode_finalize;
  object_class->set_property  = gst_vaapi_encode_set_property;
  object_class->get_property  = gst_vaapi_encode_get_property;

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_encode_debug,
                           "vaapiencode",
                           0,
                           "vaapiencode element");

  element_class->change_state = gst_vaapi_encode_change_state;

  klass->set_encoder_src_caps = NULL;

  /* Registering debug symbols for function pointers */
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapi_encode_change_state);
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapi_encode_get_caps);
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapi_encode_set_caps);
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapi_encode_chain);
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapi_encode_buffer_alloc);
}
