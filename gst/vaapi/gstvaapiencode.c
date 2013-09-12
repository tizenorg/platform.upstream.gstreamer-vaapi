/*
 *  gstvaapiencode.c - VA-API video encoder
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

#include "gst/vaapi/sysdeps.h"
#include "gst/vaapi/gstvaapicompat.h"

#include "gstvaapiencode.h"
#include "gstvaapivideometa.h"
#include "gst/vaapi/gstvaapiencoder_priv.h"
#include "gst/vaapi/gstvaapiencoder_objects.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/video/videocontext.h>
#include "gstvaapipluginutil.h"
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapiencode"
#define GST_PLUGIN_DESC "A VA-API based video encoder"

#define GST_VAAPI_ENCODE_FLOW_TIMEOUT           GST_FLOW_CUSTOM_SUCCESS
#define GST_VAAPI_ENCODE_FLOW_MEM_ERROR         GST_FLOW_CUSTOM_ERROR
#define GST_VAAPI_ENCODE_FLOW_CONVERT_ERROR     GST_FLOW_CUSTOM_ERROR_1
#define GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR  GST_FLOW_CUSTOM_ERROR_2

typedef struct _GstVaapiEncodeFrameUserData {
    GstVaapiEncObjUserDataHead head;
    GstBuffer *vaapi_buf;
} GstVaapiEncodeFrameUserData;

GST_DEBUG_CATEGORY_STATIC (gst_vaapiencode_debug);
#define GST_CAT_DEFAULT gst_vaapiencode_debug

#define GST_VAAPIENCODE_GET_PRIVATE(obj)                 \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj),                  \
                                  GST_TYPE_VAAPIENCODE,  \
                                  GstVaapiEncodePrivate))

typedef struct _GstVaapiEncodePrivate GstVaapiEncodePrivate;

#define GstVideoContextClass GstVideoContextInterface

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapiencode_implements_interface_supported(
    GstImplementsInterface *iface,
    GType                   type
)
{
    return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapiencode_implements_iface_init(GstImplementsInterfaceClass *iface)
{
    iface->supported = gst_vaapiencode_implements_interface_supported;
}
#endif

/* context(display) interface */
static void
gst_vaapiencode_set_video_context(
    GstVideoContext *context,
    const gchar *type,
    const GValue *value
)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE (context);

    gst_vaapi_set_display (type, value, &encode->display);
}

static void
gst_video_context_interface_init(GstVideoContextInterface *iface)
{
    iface->set_context = gst_vaapiencode_set_video_context;
}

G_DEFINE_TYPE_WITH_CODE(
    GstVaapiEncode,
    gst_vaapiencode,
    GST_TYPE_VIDEO_ENCODER,
#if !GST_CHECK_VERSION(1,0,0)
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMPLEMENTS_INTERFACE,
                          gst_vaapiencode_implements_iface_init);
#endif
    G_IMPLEMENT_INTERFACE(GST_TYPE_VIDEO_CONTEXT,
                          gst_video_context_interface_init))

enum {
    PROP_0,
};

static char*
_encode_dump_caps(GstCaps *caps)
{
  guint i = 0, j = 0;
  GstStructure const *structure;
  GValue const *value;
  static char caps_string[4096*5];
  char *tmp;

  char *cur = caps_string;
  memset(caps_string, 0, sizeof(caps_string));
  for (i = 0; i < gst_caps_get_size(caps); i++) {
    structure = gst_caps_get_structure(caps, i);
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

static gboolean
gst_vaapiencode_query (GstPad *pad, GstObject *parent, GstQuery *query)
{
    GstVaapiEncode *encode = GST_VAAPIENCODE (parent);
    gboolean res;

    g_assert(encode);

    GST_DEBUG("vaapiencode query %s", GST_QUERY_TYPE_NAME(query));

    if (encode->encoder &&
        gst_vaapi_reply_to_query(query, encode->display))
      res = TRUE;
    else if (GST_PAD_IS_SINK(pad))
        res = encode->sinkpad_query(pad, parent, query);
    else
        res = encode->srcpad_query(pad, parent, query);;

    return res;
}

static GstFlowReturn
gst_vaapiencode_push_frame(GstVaapiEncode *encode, gint64 ms_timeout)
{
    GstVideoEncoder * const base = GST_VIDEO_ENCODER_CAST(encode);
    GstVideoCodecFrame *out_frame = NULL;
    GstVaapiCodedBufferProxy *coded_buf_proxy = NULL;
    GstVaapiCodedBuffer *coded_buf;
    GstVaapiEncoderStatus encode_status;
    GstBuffer *output_buf;
    gint32 buf_size;
    GstFlowReturn ret;

    encode_status = gst_vaapi_encoder_get_frame(
                        encode->encoder,
                        &out_frame,
                        &coded_buf_proxy,
                        ms_timeout);
    if (encode_status == GST_VAAPI_ENCODER_STATUS_TIMEOUT)
        return GST_VAAPI_ENCODE_FLOW_TIMEOUT;

    if (encode_status != GST_VAAPI_ENCODER_STATUS_SUCCESS) {
        GST_ERROR("get encoded buffer failed, status:%d", encode_status);
        ret = GST_FLOW_ERROR;
        goto error;
    }

    g_assert(out_frame);
    gst_video_codec_frame_set_user_data(out_frame, NULL, NULL);

    coded_buf = coded_buf_proxy->buffer;
    g_assert(coded_buf);
    buf_size = gst_vaapi_coded_buffer_get_size(coded_buf);
    if (buf_size <= 0) {
        GST_ERROR("get encoded buffer size:%d", buf_size);
        ret = GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
        goto error;
    }
    output_buf = gst_video_encoder_allocate_output_buffer(
                    GST_VIDEO_ENCODER_CAST(encode), buf_size);

    if (!gst_vaapi_coded_buffer_get_buffer(coded_buf, output_buf)){
        GST_ERROR("get encoded buffer failed");
        ret = GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
        goto error;
    }
    out_frame->output_buffer = output_buf;

    gst_vaapi_coded_buffer_proxy_replace(&coded_buf_proxy, NULL);

    encode_status = gst_vaapi_encoder_convert_frame(
                         encode->encoder, out_frame);
    if (encode_status != GST_VAAPI_ENCODER_STATUS_SUCCESS) {
        GST_ERROR("get encoded convert frame failed, status:%d", encode_status);
        ret = GST_VAAPI_ENCODE_FLOW_CONVERT_ERROR;
        goto error;
    }

    /* check out_caps, need lock first */
    GST_VIDEO_ENCODER_STREAM_LOCK(encode);
    if (!encode->out_caps_done) {
        GstVaapiEncoderStatus encoder_status;
        GstVideoCodecState *old_state, *new_state;
        GstBuffer *codec_data;

        encoder_status = gst_vaapi_encoder_get_codec_data(
                             encode->encoder, &codec_data);
        if (encoder_status != GST_VAAPI_ENCODER_STATUS_SUCCESS) {
            ret = GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR;
            goto error_unlock;
        }
        if (codec_data) {
            encode->srcpad_caps = gst_caps_make_writable(encode->srcpad_caps);
            gst_caps_set_simple(encode->srcpad_caps,
                                "codec_data", GST_TYPE_BUFFER, codec_data,
                                NULL);
            gst_buffer_replace(&codec_data, NULL);
            old_state = gst_video_encoder_get_output_state(
                            GST_VIDEO_ENCODER_CAST(encode));
            new_state = gst_video_encoder_set_output_state(
                            GST_VIDEO_ENCODER_CAST(encode),
                            gst_caps_ref(encode->srcpad_caps),
                            old_state);
            gst_video_codec_state_unref(old_state);
            gst_video_codec_state_unref(new_state);
            if (!gst_video_encoder_negotiate(GST_VIDEO_ENCODER_CAST(encode))) {
                GST_ERROR("negotiate failed on caps:%s",
                          _encode_dump_caps(encode->srcpad_caps));
                ret = GST_FLOW_NOT_NEGOTIATED;
                goto error_unlock;
            }
            GST_DEBUG("update encoder src caps:%s",
                      _encode_dump_caps(encode->srcpad_caps));
        }
        encode->out_caps_done = TRUE;
    }
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encode);

    GST_DEBUG(
        "output:%" GST_TIME_FORMAT ", size:%d",
        GST_TIME_ARGS(out_frame->pts),
        gst_buffer_get_size(output_buf));

    ret = gst_video_encoder_finish_frame(base, out_frame);
    out_frame = NULL;
    if (ret != GST_FLOW_OK)
        goto error;

    return GST_FLOW_OK;

error_unlock:
    GST_VIDEO_ENCODER_STREAM_UNLOCK(encode);
error:
    gst_vaapi_coded_buffer_proxy_replace(&coded_buf_proxy, NULL);
    if (out_frame)
        gst_video_codec_frame_unref(out_frame);

    return ret;
}

static void
gst_vaapiencode_buffer_loop(GstVaapiEncode *encode)
{
    GstFlowReturn ret;
    const gint64 timeout = 50000; /* microseconds */

    ret = gst_vaapiencode_push_frame(encode, timeout);
    if (ret == GST_FLOW_OK || ret == GST_VAAPI_ENCODE_FLOW_TIMEOUT)
        return;

    gst_pad_pause_task(encode->srcpad);
}

static GstCaps *
gst_vaapiencode_get_caps(GstPad *sink_pad)
{
    GstCaps *caps = NULL;
    GstVaapiEncode * const encode =
        GST_VAAPIENCODE(GST_OBJECT_PARENT(sink_pad));

    if (encode->sinkpad_caps) {
        gst_caps_ref(encode->sinkpad_caps);
        GST_DEBUG("get caps,\n%s",
                  _encode_dump_caps(encode->sinkpad_caps));
        caps = encode->sinkpad_caps;
    } else
        caps = gst_caps_copy(gst_pad_get_pad_template_caps(sink_pad));

    return caps;
}

static gboolean
gst_vaapiencode_destroy(GstVaapiEncode * encode)
{
    gst_vaapi_encoder_replace(&encode->encoder, NULL);
    g_clear_object(&encode->video_buffer_pool);

    if (encode->sinkpad_caps) {
        gst_caps_unref(encode->sinkpad_caps);
        encode->sinkpad_caps = NULL;
    }

    if (encode->srcpad_caps) {
        gst_caps_unref(encode->srcpad_caps);
        encode->srcpad_caps = NULL;
    }

    gst_vaapi_display_replace(&encode->display, NULL);
    return TRUE;
}

static inline gboolean
ensure_display(GstVaapiEncode *encode)
{
    return gst_vaapi_ensure_display(
               encode,
               GST_VAAPI_DISPLAY_TYPE_ANY,
               &encode->display);
}

static gboolean
ensure_encoder(GstVaapiEncode *encode)
{
    GstVaapiEncodeClass *klass = GST_VAAPIENCODE_GET_CLASS(encode);

    g_assert(encode->display);

    if (encode->encoder)
        return gst_vaapi_encoder_reset_display(
                   encode->encoder, encode->display);

    if (!klass->create_encoder)
        return FALSE;

    encode->encoder = klass->create_encoder(encode->display);
    return (encode->encoder ? TRUE : FALSE);
}

static gboolean
gst_vaapiencode_open(GstVideoEncoder *base)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);
    GstVaapiDisplay * const old_display = encode->display;
    gboolean success;

    encode->display = NULL;
    success = ensure_display(encode);
    if (old_display)
        gst_vaapi_display_unref(old_display);

    GST_DEBUG("ensure display %s, display:%p",
              (success ? "okay" : "failed"),
              encode->display);
    return success;
}

static gboolean
gst_vaapiencode_close(GstVideoEncoder *base)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);

    GST_DEBUG("vaapiencode starting close");

    return gst_vaapiencode_destroy(encode);
}

static inline gboolean
gst_vaapiencode_update_sink_caps(
    GstVaapiEncode *encode,
    GstVideoCodecState *state)
{
    gst_caps_replace(&encode->sinkpad_caps, state->caps);
    encode->sink_video_info = state->info;
    return TRUE;
}

static gboolean
gst_vaapiencode_update_src_caps(
    GstVaapiEncode *encode,
    GstVideoCodecState *in_state)
{
    GstVideoCodecState *out_state;
    GstStructure *structure;
    GstCaps *outcaps, *allowed_caps, *template_caps, *intersect;
    GstVaapiEncoderStatus encoder_status;
    GstBuffer *codec_data = NULL;

    g_return_val_if_fail(encode->encoder, FALSE);

    encode->out_caps_done = FALSE;

    /* get peer caps for stream-format avc/bytestream, codec_data */
    template_caps = gst_pad_get_pad_template_caps (encode->srcpad);
    allowed_caps = gst_pad_get_allowed_caps(encode->srcpad);
    intersect = gst_caps_intersect(template_caps, allowed_caps);
    gst_caps_unref(template_caps);
    gst_caps_unref(allowed_caps);

    /* codec data was not set */
    outcaps = gst_vaapi_encoder_set_format(
        encode->encoder,
        in_state,
        intersect);
    gst_caps_unref(intersect);
    g_return_val_if_fail(outcaps, FALSE);

    if (!gst_caps_is_fixed(outcaps)) {
        GST_ERROR("encoder output caps was not fixed");
        gst_caps_unref(outcaps);
        return FALSE;
    }
    structure = gst_caps_get_structure(outcaps, 0);
    if (!gst_structure_has_field(structure, "codec_data")) {
        encoder_status = gst_vaapi_encoder_get_codec_data(
                             encode->encoder, &codec_data);
        if (encoder_status == GST_VAAPI_ENCODER_STATUS_SUCCESS) {
            if (codec_data) {
                outcaps = gst_caps_make_writable(outcaps);
                gst_caps_set_simple(outcaps,
                                    "codec_data",
                                    GST_TYPE_BUFFER, codec_data,
                                    NULL);
                gst_buffer_replace(&codec_data, NULL);
            }
            encode->out_caps_done = TRUE;
        }
    } else
        encode->out_caps_done = TRUE;



    out_state = gst_video_encoder_set_output_state(
                    GST_VIDEO_ENCODER_CAST(encode), outcaps, in_state);

    gst_caps_replace(&encode->srcpad_caps, out_state->caps);
    gst_video_codec_state_unref(out_state);

    GST_DEBUG("encoder set src_caps:%s",
              _encode_dump_caps(encode->srcpad_caps));

    return TRUE;
}

static gboolean
gst_vaapiencode_ensure_video_buffer_pool(GstVaapiEncode *encode, GstCaps *caps)
{
    GstBufferPool *pool;
    GstCaps *pool_caps;
    GstStructure *config;
    GstVideoInfo vi;
    gboolean need_pool;

    if (!ensure_display(encode))
        return FALSE;

    if (encode->video_buffer_pool) {
        config = gst_buffer_pool_get_config(encode->video_buffer_pool);
        gst_buffer_pool_config_get_params(config, &pool_caps, NULL, NULL, NULL);
        need_pool = !gst_caps_is_equal(caps, pool_caps);
        gst_structure_free(config);
        if (!need_pool)
            return TRUE;
        g_clear_object(&encode->video_buffer_pool);
        encode->video_buffer_size = 0;
    }

    pool = gst_vaapi_video_buffer_pool_new(encode->display);
    if (!pool)
        goto error_create_pool;

    gst_video_info_init(&vi);
    gst_video_info_from_caps(&vi, caps);
    if (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_ENCODED) {
        GST_DEBUG("assume video buffer pool format is NV12");
        gst_video_info_set_format(&vi, GST_VIDEO_FORMAT_NV12,
            GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));
    }
    encode->video_buffer_size = vi.size;

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, caps, encode->video_buffer_size,
        0, 0);
    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
    gst_buffer_pool_config_add_option(config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config(pool, config))
        goto error_pool_config;
    encode->video_buffer_pool = pool;
    return TRUE;

    /* ERRORS */
error_create_pool:
    {
        GST_ERROR("failed to create buffer pool");
        return FALSE;
    }
error_pool_config:
    {
        GST_ERROR("failed to reset buffer pool config");
        gst_object_unref(pool);
        return FALSE;
    }
}

static gboolean
gst_vaapiencode_set_format(
    GstVideoEncoder *base,
	GstVideoCodecState *state
)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);

    g_return_val_if_fail(state->caps, FALSE);
    GST_DEBUG("vaapiencode starting set_format, caps:%s",
              _encode_dump_caps(state->caps));

    if (!gst_vaapiencode_ensure_video_buffer_pool(encode, state->caps)) {
        GST_DEBUG("ensure video buffer pool failed, caps:%s",
                  _encode_dump_caps(state->caps));
        return FALSE;
    }

    if (!ensure_encoder(encode)) {
        GST_ERROR("ensure encoder failed.");
        return FALSE;
    }

    if(!gst_vaapiencode_update_sink_caps(encode, state)) {
        GST_DEBUG("vaapiencode update sink caps failed");
        return FALSE;
    }

    /* update output state*/
    if (!gst_vaapiencode_update_src_caps(encode, state)) {
        GST_DEBUG("vaapiencode update src caps failed");
        return FALSE;
    }

    if (encode->out_caps_done && !gst_video_encoder_negotiate(base)) {
        GST_DEBUG("vaapiencode negotiate failed, caps:%s",
                  _encode_dump_caps(encode->srcpad_caps));
        return FALSE;
    }

    return gst_pad_start_task(encode->srcpad,
        (GstTaskFunction)gst_vaapiencode_buffer_loop, encode, NULL);
    return TRUE;
}

static gboolean
gst_vaapiencode_reset(
    GstVideoEncoder *base,
    gboolean hard
)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);

    GST_DEBUG("vaapiencode starting reset");

    /* FIXME: compare sink_caps with encoder */
    encode->is_running = FALSE;
    encode->out_caps_done = FALSE;
    return TRUE;
}

static GstFlowReturn
gst_vaapiencode_get_vaapi_buffer(
    GstVaapiEncode *encode,
    GstBuffer *src_buffer,
    GstBuffer **out_buffer_ptr
)
{
    GstVaapiVideoMeta *meta;
    GstBuffer *out_buffer;
    GstVideoFrame src_frame, out_frame;
    GstFlowReturn ret;

    *out_buffer_ptr = NULL;
    meta = gst_buffer_get_vaapi_video_meta(src_buffer);
    if (meta) {
        *out_buffer_ptr = gst_buffer_ref(src_buffer);
        return GST_FLOW_OK;
    }

    if (!GST_VIDEO_INFO_IS_YUV(&encode->sink_video_info)) {
        GST_ERROR("unsupported video buffer");
        return GST_FLOW_EOS;
    }

    GST_DEBUG("buffer %p not from our pool, copying", src_buffer);

    if (!encode->video_buffer_pool)
        goto error_no_pool;

    if (!gst_buffer_pool_set_active(encode->video_buffer_pool, TRUE))
        goto error_activate_pool;

    ret = gst_buffer_pool_acquire_buffer(encode->video_buffer_pool,
          &out_buffer, NULL);
    if (ret != GST_FLOW_OK)
        goto error_create_buffer;

    if (!gst_video_frame_map(&src_frame, &encode->sink_video_info, src_buffer,
            GST_MAP_READ))
        goto error_map_src_buffer;

    if (!gst_video_frame_map(&out_frame, &encode->sink_video_info, out_buffer,
            GST_MAP_WRITE))
        goto error_map_dst_buffer;

    gst_video_frame_copy(&out_frame, &src_frame);
    gst_video_frame_unmap(&out_frame);
    gst_video_frame_unmap(&src_frame);

    *out_buffer_ptr = out_buffer;
    return GST_FLOW_OK;

    /* ERRORS */
error_no_pool:
    GST_ERROR("no buffer pool was negotiated");
    return GST_FLOW_ERROR;
error_activate_pool:
    GST_ERROR("failed to activate buffer pool");
    return GST_FLOW_ERROR;
error_create_buffer:
    GST_WARNING("failed to create image. Skipping this frame");
    return GST_FLOW_OK;
error_map_dst_buffer:
    gst_video_frame_unmap(&src_frame);
    // fall-through
error_map_src_buffer:
    GST_WARNING("failed to map buffer. Skipping this frame");
    gst_buffer_unref(out_buffer);
    return GST_FLOW_OK;
}

static inline gpointer
_create_user_data(GstBuffer *buf)
{
    GstVaapiVideoMeta *meta;
    GstVaapiSurface *surface;
    GstVaapiEncodeFrameUserData *user_data;

    meta = gst_buffer_get_vaapi_video_meta(buf);
    if (!meta) {
        GST_DEBUG("convert to vaapi buffer failed");
        return NULL;
    }
    surface = gst_vaapi_video_meta_get_surface(meta);
    if (!surface) {
        GST_DEBUG("vaapi_meta of codec frame doesn't have vaapisurfaceproxy");
        return NULL;
    }

    user_data = g_slice_new0(GstVaapiEncodeFrameUserData);
    user_data->head.surface = surface;
    user_data->vaapi_buf = gst_buffer_ref(buf);
    return user_data;
}

static void
_destroy_user_data(gpointer data)
{
    GstVaapiEncodeFrameUserData *user_data = (GstVaapiEncodeFrameUserData*)data;

    g_assert(data);
    if (!user_data)
        return;
    gst_buffer_replace(&user_data->vaapi_buf, NULL);
    g_slice_free(GstVaapiEncodeFrameUserData, user_data);
}

static GstFlowReturn
gst_vaapiencode_handle_frame (
    GstVideoEncoder *base,
    GstVideoCodecFrame *frame
)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);
    GstFlowReturn ret = GST_FLOW_OK;
    GstVaapiEncoderStatus encoder_ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
    GstBuffer *vaapi_buf = NULL;
    gpointer user_data;

    g_assert(encode && encode->encoder);
    g_assert(frame && frame->input_buffer);

    ret = gst_vaapiencode_get_vaapi_buffer(encode, frame->input_buffer, &vaapi_buf);
    GST_VAAPI_ENCODER_CHECK_STATUS(ret == GST_FLOW_OK,
        ret,
        "convert to vaapi buffer failed");

    user_data = _create_user_data(vaapi_buf);
    GST_VAAPI_ENCODER_CHECK_STATUS(user_data,
        ret,
        "create frame user data failed");

    gst_video_codec_frame_set_user_data(frame, user_data, _destroy_user_data);

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
    /*encoding frames*/
    encoder_ret = gst_vaapi_encoder_encode(encode->encoder, frame);
    GST_VIDEO_ENCODER_STREAM_LOCK (encode);

    GST_VAAPI_ENCODER_CHECK_STATUS(
        GST_VAAPI_ENCODER_STATUS_SUCCESS <= encoder_ret,
        GST_FLOW_ERROR,
        "gst_vaapiencoder_encode failed.");

end:
    gst_video_codec_frame_unref(frame);
    gst_buffer_replace(&vaapi_buf, NULL);
    return ret;
}

static GstFlowReturn
gst_vaapiencode_finish(GstVideoEncoder *base)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);
    GstVaapiEncoderStatus encoder_status;
    GstFlowReturn ret = GST_FLOW_OK;

    GST_DEBUG("vaapiencode starting finish");

    encoder_status = gst_vaapi_encoder_flush(encode->encoder);

    GST_VIDEO_ENCODER_STREAM_UNLOCK(encode);
    gst_pad_stop_task(encode->srcpad);
    GST_VIDEO_ENCODER_STREAM_LOCK(encode);

    while (encoder_status == GST_VAAPI_ENCODER_STATUS_SUCCESS &&
           ret == GST_FLOW_OK) {
        ret = gst_vaapiencode_push_frame(encode, 0);
    }

    if (ret == GST_VAAPI_ENCODE_FLOW_TIMEOUT);
        ret = GST_FLOW_OK;
    return ret;
}

static gboolean
gst_vaapiencode_propose_allocation(GstVideoEncoder *base, GstQuery *query)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(base);
    GstCaps *caps = NULL;
    gboolean need_pool;

    GST_DEBUG("vaapiencode starting propose_allocation");

    gst_query_parse_allocation(query, &caps, &need_pool);

    if (need_pool) {
        if (!caps)
            goto error_no_caps;
        if (!gst_vaapiencode_ensure_video_buffer_pool(encode, caps)) {
            GST_WARNING("vaapiencode ensure video_buffer_pool failed, caps:%s",
                        _encode_dump_caps(caps));
            return FALSE;
        }
        gst_query_add_allocation_pool(query, encode->video_buffer_pool,
            encode->video_buffer_size, 0, 0);
    }

    gst_query_add_allocation_meta(query,
        GST_VAAPI_VIDEO_META_API_TYPE, NULL);
    gst_query_add_allocation_meta(query,
        GST_VIDEO_META_API_TYPE, NULL);
    return TRUE;

    /* ERRORS */
error_no_caps:
    {
        GST_ERROR("no caps specified");
        return FALSE;
    }
}

static void
gst_vaapiencode_set_property(
    GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(object);
    g_assert(encode->encoder);

    switch (prop_id) {
    }
}

static void
gst_vaapiencode_get_property (
    GObject * object,
    guint prop_id,
    GValue * value,
    GParamSpec * pspec
)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(object);
    g_assert(encode->encoder);

    switch (prop_id) {
    }
}

static void
gst_vaapiencode_finalize(GObject *object)
{
    GstVaapiEncode * const encode = GST_VAAPIENCODE(object);

    gst_vaapiencode_destroy(encode);

    encode->sinkpad = NULL;
    encode->srcpad = NULL;

    G_OBJECT_CLASS(gst_vaapiencode_parent_class)->finalize(object);
}

static void
gst_vaapiencode_init(GstVaapiEncode *encode)
{
  encode->sinkpad_caps       = NULL;
  encode->srcpad_caps        = NULL;
  encode->is_running         = FALSE;
  encode->out_caps_done      = FALSE;

  encode->encoder = NULL;
  encode->display = NULL;

  encode->video_buffer_pool = NULL;
  encode->video_buffer_size = 0;

  /*sink pad */
  encode->sinkpad = GST_VIDEO_ENCODER_SINK_PAD(encode);
  encode->sinkpad_query = GST_PAD_QUERYFUNC(encode->sinkpad);
  gst_pad_set_query_function(encode->sinkpad, gst_vaapiencode_query);
  gst_video_info_init(&encode->sink_video_info);

  /* src pad */
  encode->srcpad = GST_VIDEO_ENCODER_SRC_PAD(encode);
  encode->srcpad_query = GST_PAD_QUERYFUNC(encode->srcpad);
  gst_pad_set_query_function(encode->srcpad, gst_vaapiencode_query);

  gst_pad_use_fixed_caps(encode->srcpad);
}


static void
gst_vaapiencode_class_init(GstVaapiEncodeClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVideoEncoderClass * const venc_class = GST_VIDEO_ENCODER_CLASS(klass);

    object_class->finalize      = gst_vaapiencode_finalize;
    object_class->set_property  = gst_vaapiencode_set_property;
    object_class->get_property  = gst_vaapiencode_get_property;

    GST_DEBUG_CATEGORY_INIT (gst_vaapiencode_debug,
                             GST_PLUGIN_NAME,
                             0,
                             GST_PLUGIN_DESC);

    venc_class->open = GST_DEBUG_FUNCPTR(gst_vaapiencode_open);
    venc_class->close = GST_DEBUG_FUNCPTR(gst_vaapiencode_close);
    venc_class->set_format = GST_DEBUG_FUNCPTR(gst_vaapiencode_set_format);
    venc_class->handle_frame = GST_DEBUG_FUNCPTR(gst_vaapiencode_handle_frame);
    venc_class->reset = GST_DEBUG_FUNCPTR(gst_vaapiencode_reset);
    venc_class->finish = GST_DEBUG_FUNCPTR(gst_vaapiencode_finish);
    //venc_class->sink_event = GST_DEBUG_FUNCPTR(gst_vaapiencode_sink_event);
    //venc_class->src_event = GST_DEBUG_FUNCPTR(gst_vaapiencode_src_event);
    //venc_class->decide_allocation = GST_DEBUG_FUNCPTR(gst_vaapiencode_decide_allocation);
    venc_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_vaapiencode_propose_allocation);
    //venc_class->negotiate = GST_DEBUG_FUNCPTR(gst_vaapiencode_negotiate);

    /* Registering debug symbols for function pointers */
    GST_DEBUG_REGISTER_FUNCPTR (gst_vaapiencode_get_caps);
    GST_DEBUG_REGISTER_FUNCPTR (gst_vaapiencode_query);
}
