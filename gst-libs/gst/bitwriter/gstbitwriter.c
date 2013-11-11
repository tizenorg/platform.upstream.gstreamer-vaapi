/*
 *  gstbitwriter.c - bitstream writer
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

#define GST_BIT_WRITER_DISABLE_INLINES

#include "gstbitwriter.h"

void
gst_bit_writer_init(GstBitWriter *bitwriter, guint32 reserved_bits)
{
    bitwriter->bit_size = 0;
    bitwriter->data = NULL;
    bitwriter->max_bit_capacity = 0;
    bitwriter->auto_grow = TRUE;
    if (reserved_bits)
        _gst_bit_writer_check_space(bitwriter, reserved_bits);
}

void
gst_bit_writer_init_fill(GstBitWriter *bitwriter, guint8 *data, guint bits)
{
    bitwriter->bit_size = 0;
    bitwriter->data = data;
    bitwriter->max_bit_capacity = bits;
    bitwriter->auto_grow = FALSE;
}

void
gst_bit_writer_clear(GstBitWriter *bitwriter, gboolean free_data)
{
    if (bitwriter->auto_grow && bitwriter->data && free_data)
        g_free (bitwriter->data);

    bitwriter->data = NULL;
    bitwriter->bit_size = 0;
    bitwriter->max_bit_capacity = 0;
}

GstBitWriter *
gst_bit_writer_new(guint32 reserved_bits)
{
    GstBitWriter *ret = g_slice_new0 (GstBitWriter);

    gst_bit_writer_init(ret, reserved_bits);

    return ret;
}

GstBitWriter *
gst_bit_writer_new_fill(guint8 *data, guint bits)
{
    GstBitWriter *ret = g_slice_new0 (GstBitWriter);

    gst_bit_writer_init_fill(ret, data, bits);

    return ret;
}

void
gst_bit_writer_free(GstBitWriter *writer, gboolean free_data)
{
    g_return_if_fail (writer != NULL);

    gst_bit_writer_clear(writer, free_data);

    g_slice_free (GstBitWriter, writer);
}

guint
gst_bit_writer_get_size(GstBitWriter *bitwriter)
{
    return _gst_bit_writer_get_size_inline(bitwriter);
}

guint8 *
gst_bit_writer_get_data(GstBitWriter *bitwriter)
{
    return _gst_bit_writer_get_data_inline(bitwriter);
}

gboolean
gst_bit_writer_set_pos(GstBitWriter *bitwriter, guint pos)
{
    return _gst_bit_writer_set_pos_inline(bitwriter, pos);
}

#define GST_BIT_WRITER_WRITE_BITS(bits) \
gboolean \
gst_bit_writer_put_bits_uint##bits (GstBitWriter *bitwriter, guint##bits value, guint nbits) \
{ \
  return _gst_bit_writer_put_bits_uint##bits##_inline (bitwriter, value, nbits); \
}

GST_BIT_WRITER_WRITE_BITS(8)
GST_BIT_WRITER_WRITE_BITS(16)
GST_BIT_WRITER_WRITE_BITS(32)
GST_BIT_WRITER_WRITE_BITS(64)

gboolean
gst_bit_writer_put_bytes(
    GstBitWriter *bitwriter,
    const guint8 *data,
    guint nbytes
)
{
    return _gst_bit_writer_put_bytes_inline(bitwriter, data, nbytes);
}

gboolean
gst_bit_writer_align_bytes(GstBitWriter *bitwriter, guint8 trailing_bit)
{
    return _gst_bit_writer_align_bytes_inline(bitwriter, trailing_bit);
}
