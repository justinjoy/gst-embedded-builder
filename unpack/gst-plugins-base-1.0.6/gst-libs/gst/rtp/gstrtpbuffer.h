/* GStreamer
 * Copyright (C) <2005> Philippe Khalaf <burger@speedy.org>
 *               <2005> Wim Taymans <wim@fluendo.com>
 *
 * gstrtpbuffer.h: various helper functions to manipulate buffers
 *     with RTP payload.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_RTPBUFFER_H__
#define __GST_RTPBUFFER_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtppayloads.h>

G_BEGIN_DECLS

/**
 * GST_RTP_VERSION:
 *
 * The supported RTP version 2.
 */
#define GST_RTP_VERSION 2


typedef struct _GstRTPBuffer GstRTPBuffer;

/**
 * GstRTPBuffer:
 * @buffer: pointer to RTP buffer
 * @state: internal state
 * @data: array of data
 * @size: array of size
 * @map: array of #GstMapInfo
 *
 * Data structure that points to an RTP packet.
 * The size of the structure is made public to allow stack allocations.
 */
struct _GstRTPBuffer
{
  GstBuffer   *buffer;
  guint        state;
  gpointer     data[4];
  gsize        size[4];
  GstMapInfo   map[4];
};

#define GST_RTP_BUFFER_INIT { NULL, 0, { NULL, NULL, NULL, NULL}, { 0, 0, 0, 0 }, \
  { GST_MAP_INFO_INIT, GST_MAP_INFO_INIT, GST_MAP_INFO_INIT, GST_MAP_INFO_INIT} }

/* creating buffers */
void            gst_rtp_buffer_allocate_data         (GstBuffer *buffer, guint payload_len,
                                                      guint8 pad_len, guint8 csrc_count);

GstBuffer*      gst_rtp_buffer_new_take_data         (gpointer data, gsize len);
GstBuffer*      gst_rtp_buffer_new_copy_data         (gpointer data, gsize len);
GstBuffer*      gst_rtp_buffer_new_allocate          (guint payload_len, guint8 pad_len, guint8 csrc_count);
GstBuffer*      gst_rtp_buffer_new_allocate_len      (guint packet_len, guint8 pad_len, guint8 csrc_count);

guint           gst_rtp_buffer_calc_header_len       (guint8 csrc_count);
guint           gst_rtp_buffer_calc_packet_len       (guint payload_len, guint8 pad_len, guint8 csrc_count);
guint           gst_rtp_buffer_calc_payload_len      (guint packet_len, guint8 pad_len, guint8 csrc_count);

gboolean        gst_rtp_buffer_map                   (GstBuffer *buffer, GstMapFlags flags, GstRTPBuffer *rtp);
void            gst_rtp_buffer_unmap                 (GstRTPBuffer *rtp);

void            gst_rtp_buffer_set_packet_len        (GstRTPBuffer *rtp, guint len);
guint           gst_rtp_buffer_get_packet_len        (GstRTPBuffer *rtp);

guint           gst_rtp_buffer_get_header_len        (GstRTPBuffer *rtp);

guint8          gst_rtp_buffer_get_version           (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_version           (GstRTPBuffer *rtp, guint8 version);

gboolean        gst_rtp_buffer_get_padding           (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_padding           (GstRTPBuffer *rtp, gboolean padding);
void            gst_rtp_buffer_pad_to                (GstRTPBuffer *rtp, guint len);

gboolean        gst_rtp_buffer_get_extension         (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_extension         (GstRTPBuffer *rtp, gboolean extension);
gboolean        gst_rtp_buffer_get_extension_data    (GstRTPBuffer *rtp, guint16 *bits,
                                                      gpointer *data, guint *wordlen);
gboolean        gst_rtp_buffer_set_extension_data    (GstRTPBuffer *rtp, guint16 bits, guint16 length);

guint32         gst_rtp_buffer_get_ssrc              (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_ssrc              (GstRTPBuffer *rtp, guint32 ssrc);

guint8          gst_rtp_buffer_get_csrc_count        (GstRTPBuffer *rtp);
guint32         gst_rtp_buffer_get_csrc              (GstRTPBuffer *rtp, guint8 idx);
void            gst_rtp_buffer_set_csrc              (GstRTPBuffer *rtp, guint8 idx, guint32 csrc);

gboolean        gst_rtp_buffer_get_marker            (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_marker            (GstRTPBuffer *rtp, gboolean marker);

guint8          gst_rtp_buffer_get_payload_type      (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_payload_type      (GstRTPBuffer *rtp, guint8 payload_type);

guint16         gst_rtp_buffer_get_seq               (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_seq               (GstRTPBuffer *rtp, guint16 seq);

guint32         gst_rtp_buffer_get_timestamp         (GstRTPBuffer *rtp);
void            gst_rtp_buffer_set_timestamp         (GstRTPBuffer *rtp, guint32 timestamp);

GstBuffer*      gst_rtp_buffer_get_payload_buffer    (GstRTPBuffer *rtp);
GstBuffer*      gst_rtp_buffer_get_payload_subbuffer (GstRTPBuffer *rtp, guint offset, guint len);

guint           gst_rtp_buffer_get_payload_len       (GstRTPBuffer *rtp);
gpointer        gst_rtp_buffer_get_payload           (GstRTPBuffer *rtp);

/* some helpers */
guint32         gst_rtp_buffer_default_clock_rate    (guint8 payload_type);
gint            gst_rtp_buffer_compare_seqnum        (guint16 seqnum1, guint16 seqnum2);
guint64         gst_rtp_buffer_ext_timestamp         (guint64 *exttimestamp, guint32 timestamp);

gboolean        gst_rtp_buffer_get_extension_onebyte_header  (GstRTPBuffer *rtp,
                                                              guint8 id,
                                                              guint nth,
                                                              gpointer * data,
                                                              guint * size);
gboolean        gst_rtp_buffer_get_extension_twobytes_header (GstRTPBuffer *rtp,
                                                              guint8 * appbits,
                                                              guint8 id,
                                                              guint nth,
                                                              gpointer * data,
                                                              guint * size);

gboolean       gst_rtp_buffer_add_extension_onebyte_header  (GstRTPBuffer *rtp,
                                                             guint8 id,
                                                             gpointer data,
                                                             guint size);
gboolean       gst_rtp_buffer_add_extension_twobytes_header (GstRTPBuffer *rtp,
                                                             guint8 appbits,
                                                             guint8 id,
                                                             gpointer data,
                                                             guint size);


G_END_DECLS

#endif /* __GST_RTPBUFFER_H__ */

