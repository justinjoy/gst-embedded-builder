/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpmp2tdepay.h"

/* RtpMP2TDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SKIP_FIRST_BYTES	0

enum
{
  PROP_0,
  PROP_SKIP_FIRST_BYTES
};

static GstStaticPadTemplate gst_rtp_mp2t_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts,"
        "packetsize=(int)188," "systemstream=(boolean)true")
    );

static GstStaticPadTemplate gst_rtp_mp2t_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX ], " "encoding-name = (string) \"MP2T\";"
        /* All optional parameters
         *
         * "profile-level-id=[1,MAX]"
         * "config=" 
         */
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_MP2T_STRING ", "
        "clock-rate = (int) [1, MAX ]")
    );

G_DEFINE_TYPE (GstRtpMP2TDepay, gst_rtp_mp2t_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static gboolean gst_rtp_mp2t_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_mp2t_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);

static void gst_rtp_mp2t_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mp2t_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void
gst_rtp_mp2t_depay_class_init (GstRtpMP2TDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gstrtpbasedepayload_class->process = gst_rtp_mp2t_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_mp2t_depay_setcaps;

  gobject_class->set_property = gst_rtp_mp2t_depay_set_property;
  gobject_class->get_property = gst_rtp_mp2t_depay_get_property;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_mp2t_depay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_mp2t_depay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP MPEG Transport Stream depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts MPEG2 TS from RTP packets (RFC 2250)",
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Thijs Vermeir <thijs.vermeir@barco.com>");

  g_object_class_install_property (gobject_class, PROP_SKIP_FIRST_BYTES,
      g_param_spec_uint ("skip-first-bytes",
          "Skip first bytes",
          "The amount of bytes that need to be skipped at the beginning of the payload",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_rtp_mp2t_depay_init (GstRtpMP2TDepay * rtpmp2tdepay)
{
  rtpmp2tdepay->skip_first_bytes = DEFAULT_SKIP_FIRST_BYTES;
}

static gboolean
gst_rtp_mp2t_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure;
  gint clock_rate;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("video/mpegts",
      "packetsize", G_TYPE_INT, 188,
      "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);
  res = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return res;
}

static GstBuffer *
gst_rtp_mp2t_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpMP2TDepay *rtpmp2tdepay;
  GstBuffer *outbuf;
  gint payload_len, leftover;
  GstRTPBuffer rtp = { NULL };

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (depayload);

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);
  payload_len = gst_rtp_buffer_get_payload_len (&rtp);

  if (G_UNLIKELY (payload_len <= rtpmp2tdepay->skip_first_bytes))
    goto empty_packet;

  payload_len -= rtpmp2tdepay->skip_first_bytes;

  /* RFC 2250
   *
   * 2. Encapsulation of MPEG System and Transport Streams
   *
   * For MPEG2 Transport Streams the RTP payload will contain an integral
   * number of MPEG transport packets.
   */
  leftover = payload_len % 188;
  if (G_UNLIKELY (leftover)) {
    GST_WARNING ("We don't have an integral number of buffers (leftover: %d)",
        leftover);

    payload_len -= leftover;
  }

  outbuf =
      gst_rtp_buffer_get_payload_subbuffer (&rtp,
      rtpmp2tdepay->skip_first_bytes, payload_len);

  gst_rtp_buffer_unmap (&rtp);
  if (outbuf)
    GST_DEBUG ("gst_rtp_mp2t_depay_chain: pushing buffer of size %"
        G_GSIZE_FORMAT, gst_buffer_get_size (outbuf));

  return outbuf;

  /* ERRORS */
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpmp2tdepay, STREAM, DECODE,
        (NULL), ("Packet was empty"));
    gst_rtp_buffer_unmap (&rtp);
    return NULL;
  }
}

static void
gst_rtp_mp2t_depay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpMP2TDepay *rtpmp2tdepay;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (object);

  switch (prop_id) {
    case PROP_SKIP_FIRST_BYTES:
      rtpmp2tdepay->skip_first_bytes = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mp2t_depay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpMP2TDepay *rtpmp2tdepay;

  rtpmp2tdepay = GST_RTP_MP2T_DEPAY (object);

  switch (prop_id) {
    case PROP_SKIP_FIRST_BYTES:
      g_value_set_uint (value, rtpmp2tdepay->skip_first_bytes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_mp2t_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpmp2tdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_MP2T_DEPAY);
}
