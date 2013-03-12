/*
 *  gstvaapiencode_h263.h - VA-API H.263 encoder
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

#ifndef GST_VAAPI_ENCODE_H263_H
#define GST_VAAPI_ENCODE_H263_H

#include <gst/gst.h>
#include "gstvaapiencode.h"

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_ENCODE_H263 \
    (gst_vaapi_encode_h263_get_type())

#define GST_IS_VAAPI_ENCODE_H263(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_ENCODE_H263))

#define GST_IS_VAAPI_ENCODE_H263_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VAAPI_ENCODE_H263))

#define GST_VAAPI_ENCODE_H263_GET_CLASS(obj)                   \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),                         \
                                GST_TYPE_VAAPI_ENCODE_H263,    \
                                GstVaapiEncodeH263Class))

#define GST_VAAPI_ENCODE_H263(obj)                             \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj),                        \
                                 GST_TYPE_VAAPI_ENCODE_H263,   \
                                 GstVaapiEncodeH263))

#define GST_VAAPI_ENCODE_H263_CLASS(klass)                     \
    (G_TYPE_CHECK_CLASS_CAST ((klass),                         \
                              GST_TYPE_VAAPI_ENCODE_H263,      \
                              GstVaapiEncodeH263Class))

typedef struct _GstVaapiEncodeH263        GstVaapiEncodeH263;
typedef struct _GstVaapiEncodeH263Class   GstVaapiEncodeH263Class;

struct _GstVaapiEncodeH263 {
  GstVaapiEncode parent;
};

struct _GstVaapiEncodeH263Class {
    GstVaapiEncodeClass     parent_class;
};

GType gst_vaapi_encode_h263_get_type(void);

G_END_DECLS

#endif /* GST_VAAPI_ENCODE_H263_H */
