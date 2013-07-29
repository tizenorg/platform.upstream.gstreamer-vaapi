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

#include "gstbitwriter.h"
#include <string.h>

static const guint8 bit_pading_mask[9] = {
    0x00, 0x01, 0x03, 0x07,
    0x0F, 0x1F, 0x3F, 0x7F,
    0xFF };

#define GST_BITS_WRITER_ALIGNMENT_SIZE 2048

#define GST_BITS_WRITER_ALIGN(bitsize)                       \
    (((bitsize) + GST_BITS_WRITER_ALIGNMENT_SIZE - 1)&       \
     (~(GST_BITS_WRITER_ALIGNMENT_SIZE - 1)))

#define GST_BITS_WRITER_BYTE_ROUND_UP(bitsize)               \
        (((bitsize)+7)>>3)

#define GST_BITS_WRITER_BYTE_FLOOR(bitsize)                  \
            ((bitsize)>>3)

static gboolean
gst_bit_writer_auto_grow(GstBitWriter *bitwriter, guint32 extra_bit_size)
{
    guint32 new_bit_size = extra_bit_size + bitwriter->bit_size;
    guint32 clear_pos;

    g_assert(bitwriter->bit_size <= bitwriter->max_bit_capability);
    if (new_bit_size <= bitwriter->max_bit_capability)
        return TRUE;

    new_bit_size = GST_BITS_WRITER_ALIGN(new_bit_size);
    g_assert(new_bit_size && (new_bit_size%GST_BITS_WRITER_ALIGNMENT_SIZE == 0));
    clear_pos = GST_BITS_WRITER_BYTE_ROUND_UP(bitwriter->bit_size);
    bitwriter->buffer = g_realloc(bitwriter->buffer, (new_bit_size >> 3));
    memset(bitwriter->buffer + clear_pos, 0, (new_bit_size >> 3) - clear_pos);
    bitwriter->max_bit_capability = new_bit_size;
    return TRUE;
}

void
gst_bit_writer_init(GstBitWriter *bitwriter, guint32 bit_capability)
{
    bitwriter->bit_size = 0;
    bitwriter->buffer = NULL;
    bitwriter->max_bit_capability = 0;
    if (bit_capability)
        gst_bit_writer_auto_grow(bitwriter, bit_capability);
}

void
gst_bit_writer_clear(GstBitWriter *bitwriter, gboolean free_flag)
{
    if (bitwriter->buffer && free_flag)
        g_free (bitwriter->buffer);

    bitwriter->buffer = NULL;
    bitwriter->bit_size = 0;
    bitwriter->max_bit_capability = 0;
}

gboolean
gst_bit_writer_write_uint_value(
    GstBitWriter *bitwriter,
    guint32 value,
    guint32 nbits
)
{
    guint32 byte_pos, bit_offset;
    guint8  *cur_byte;
    guint32 fill_bits;

    if (!nbits)
        return TRUE;

    if (!gst_bit_writer_auto_grow(bitwriter, nbits))
        return FALSE;

    byte_pos = GST_BITS_WRITER_BYTE_FLOOR(bitwriter->bit_size);
    bit_offset = (bitwriter->bit_size & 0x07);
    cur_byte = bitwriter->buffer + byte_pos;
    g_assert( bit_offset < 8 &&
            bitwriter->bit_size <= bitwriter->max_bit_capability);

    while (nbits) {
        fill_bits = ((8 - bit_offset) < nbits ? (8 - bit_offset) : nbits);
        nbits -= fill_bits;
        bitwriter->bit_size += fill_bits;

        *cur_byte |= (((value >> nbits) & bit_pading_mask[fill_bits])
                      << (8 - bit_offset - fill_bits));
        ++cur_byte;
        bit_offset = 0;
    }
    g_assert(cur_byte <=
           (bitwriter->buffer + (bitwriter->max_bit_capability >> 3)));

    return TRUE;
}

gboolean
gst_bit_writer_align_byte(GstBitWriter *bitwriter, guint32 value)
{
    guint32 bit_offset, bit_left;

    bit_offset = (bitwriter->bit_size & 0x07);
    if (!bit_offset)
        return TRUE;

    bit_left = 8 - bit_offset;
    if (value)
        value = bit_pading_mask[bit_left];
    return gst_bit_writer_write_uint_value(bitwriter, value, bit_left);
}


gboolean
gst_bit_writer_write_byte_array(
    GstBitWriter *bitwriter,
    const guint8 *buf,
    guint32 byte_size
)
{
    if (!byte_size)
        return FALSE;

    if (!gst_bit_writer_auto_grow(bitwriter, byte_size << 3))
        return FALSE;

    if ((bitwriter->bit_size & 0x07) == 0) {
        memcpy(&bitwriter->buffer[bitwriter->bit_size >> 3],
               buf, byte_size);
        bitwriter->bit_size += (byte_size << 3);
    } else {
        g_assert(0);
        while(byte_size) {
            gst_bit_writer_write_uint_value(bitwriter, *buf, 8);
            --byte_size;
            ++buf;
        }
    }

    return TRUE;
}

gboolean
gst_bit_writer_write_ue(GstBitWriter *bitwriter, guint32 value)
{
    guint32  size_in_bits = 0;
    guint32  tmp_value = ++value;

    while (tmp_value) {
        ++size_in_bits;
        tmp_value >>= 1;
    }
    if (!gst_bit_writer_write_uint_value(bitwriter, 0, size_in_bits-1))
        return FALSE;
    if (!gst_bit_writer_write_uint_value(bitwriter, value, size_in_bits))
        return FALSE;
    return TRUE;
}

gboolean
gst_bit_writer_write_se(GstBitWriter *bitwriter, gint32 value)
{
    guint32 new_val;

    if (value <= 0)
        new_val = -(value<<1);
    else
        new_val = (value<<1) - 1;

    if (!gst_bit_writer_write_ue(bitwriter, new_val))
        return FALSE;
    return TRUE;
}
