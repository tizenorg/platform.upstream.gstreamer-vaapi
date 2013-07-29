/*
 *  gstbitwriter.h - bitstream writer
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

#ifndef GST_BIT_WRITER_H
#define GST_BIT_WRITER_H

#include <glib.h>

G_BEGIN_DECLS

#define GST_BIT_WRITER_BUFFER(writer)    ((writer)->buffer)
#define GST_BIT_WRITER_BIT_SIZE(writer)  ((writer)->bit_size)

#define GST_BIT_WRITER(writer) ((GstBitWriter *) (writer))

typedef struct _GstBitWriter     GstBitWriter;

struct _GstBitWriter {
  guint8   *buffer;
  guint32   bit_size;
  guint32   max_bit_capability;
};

void
gst_bit_writer_init(GstBitWriter *bitwriter, guint32 bit_capability);

void
gst_bit_writer_clear(GstBitWriter *bitwriter, gboolean free_flag);

gboolean
gst_bit_writer_write_uint_value(GstBitWriter *bitwriter, guint32 value, guint32 nbits);

static inline gboolean
gst_bit_writer_write_uint8(GstBitWriter *bitwriter, guint8 value)
{
    return gst_bit_writer_write_uint_value(bitwriter, value, 8);
}

static inline gboolean
gst_bit_writer_write_uint16(GstBitWriter *bitwriter, guint16 value)
{
    return gst_bit_writer_write_uint_value(bitwriter, value, 16);
}

static inline gboolean
gst_bit_writer_write_uint32(GstBitWriter *bitwriter, guint32 value)
{
    return gst_bit_writer_write_uint_value(bitwriter, value, 32);
}

static inline gboolean
gst_bit_writer_write_uint64(GstBitWriter *bitwriter, guint64 value)
{
    if (!gst_bit_writer_write_uint_value(bitwriter, (guint32)(value>>32), 32))
        return FALSE;
    return gst_bit_writer_write_uint_value(bitwriter, (guint32)value, 32);
}


gboolean
gst_bit_writer_write_byte_array(GstBitWriter *bitwriter, const guint8 *buf, guint32 byte_size);

gboolean
gst_bit_writer_align_byte(GstBitWriter *bitwriter, guint32 value);

gboolean
gst_bit_writer_write_ue(GstBitWriter *bitwriter, guint32 value);

gboolean
gst_bit_writer_write_se(GstBitWriter *bitwriter, gint32 value);

G_END_DECLS

#endif /* GST_BIT_WRITER_H */
