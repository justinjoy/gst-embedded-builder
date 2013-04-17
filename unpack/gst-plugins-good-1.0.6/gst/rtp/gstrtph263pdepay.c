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

#include <string.h>

#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtph263pdepay.h"

static GstStaticPadTemplate gst_rtp_h263p_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h263, " "variant = (string) \"itu\" ")
    );

static GstStaticPadTemplate gst_rtp_h263p_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX], "
        "encoding-name = (string) \"H263-1998\"; "
        /* optional params */
        /* NOTE all optional SDP params must be strings in the caps */
        /*
           "sqcif = (string) [1, 32], "
           "qcif = (string) [1, 32], "
           "cif = (string) [1, 32], "
           "cif4 = (string) [1, 32], "
           "cif16 = (string) [1, 32], "
           "custom = (string) ANY, "
           "f = (string) {0, 1},"
           "i = (string) {0, 1},"
           "j = (string) {0, 1},"
           "t = (string) {0, 1},"
           "k = (string) {1, 2, 3, 4},"
           "n = (string) {1, 2, 3, 4},"
           "p = (string) ANY,"
           "par = (string) ANY, "
           "cpcf = (string) ANY, "
           "bpp = (string) [0, 65536], "
           "hrd = (string) {0, 1}; "
         */
        "application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) [1, MAX], "
        "encoding-name = (string) \"H263-2000\" "
        /* optional params */
        /* NOTE all optional SDP params must be strings in the caps */
        /*
           "profile = (string) [0, 10], "
           "level = (string) {10, 20, 30, 40, 45, 50, 60, 70}, "
           "interlace = (string) {0, 1};"
         */
    )
    );

#define gst_rtp_h263p_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpH263PDepay, gst_rtp_h263p_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static void gst_rtp_h263p_depay_finalize (GObject * object);

static GstStateChangeReturn gst_rtp_h263p_depay_change_state (GstElement *
    element, GstStateChange transition);

static GstBuffer *gst_rtp_h263p_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);
gboolean gst_rtp_h263p_depay_setcaps (GstRTPBaseDepayload * filter,
    GstCaps * caps);

static void
gst_rtp_h263p_depay_class_init (GstRtpH263PDepayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gobject_class->finalize = gst_rtp_h263p_depay_finalize;

  gstelement_class->change_state = gst_rtp_h263p_depay_change_state;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_h263p_depay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_h263p_depay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP H263 depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts H263/+/++ video from RTP packets (RFC 4629)",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstrtpbasedepayload_class->process = gst_rtp_h263p_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_h263p_depay_setcaps;
}

static void
gst_rtp_h263p_depay_init (GstRtpH263PDepay * rtph263pdepay)
{
  rtph263pdepay->adapter = gst_adapter_new ();
}

static void
gst_rtp_h263p_depay_finalize (GObject * object)
{
  GstRtpH263PDepay *rtph263pdepay;

  rtph263pdepay = GST_RTP_H263P_DEPAY (object);

  g_object_unref (rtph263pdepay->adapter);
  rtph263pdepay->adapter = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

gboolean
gst_rtp_h263p_depay_setcaps (GstRTPBaseDepayload * filter, GstCaps * caps)
{
  GstCaps *srccaps = NULL;
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint clock_rate;
  const gchar *encoding_name = NULL;
  gboolean res;

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  filter->clock_rate = clock_rate;

  encoding_name = gst_structure_get_string (structure, "encoding-name");
  if (encoding_name == NULL)
    goto no_encoding_name;

  if (g_ascii_strcasecmp (encoding_name, "H263-2000") == 0) {
    /* always h263++ */
    srccaps = gst_caps_new_simple ("video/x-h263",
        "variant", G_TYPE_STRING, "itu",
        "h263version", G_TYPE_STRING, "h263pp", NULL);
  } else if (g_ascii_strcasecmp (encoding_name, "H263-1998") == 0) {
    /* this can be H263 or H263+ depending on defined appendixes in the optional
     * SDP params */
    const gchar *F, *I, *J, *T, *K, *N, *P;
    gboolean is_h263p = FALSE;

    F = gst_structure_get_string (structure, "f");
    if (F)
      if (g_ascii_strcasecmp (F, "1") == 0)
        is_h263p = TRUE;
    I = gst_structure_get_string (structure, "i");
    if (I)
      if (g_ascii_strcasecmp (I, "1") == 0)
        is_h263p = TRUE;
    J = gst_structure_get_string (structure, "j");
    if (J)
      if (g_ascii_strcasecmp (J, "1") == 0)
        is_h263p = TRUE;
    T = gst_structure_get_string (structure, "t");
    if (T)
      if (g_ascii_strcasecmp (T, "1") == 0)
        is_h263p = TRUE;
    K = gst_structure_get_string (structure, "k");
    if (K)
      is_h263p = TRUE;
    N = gst_structure_get_string (structure, "n");
    if (N)
      is_h263p = TRUE;
    P = gst_structure_get_string (structure, "p");
    if (P)
      is_h263p = TRUE;

    if (is_h263p) {
      srccaps = gst_caps_new_simple ("video/x-h263",
          "variant", G_TYPE_STRING, "itu",
          "h263version", G_TYPE_STRING, "h263p", NULL);
    } else {
      srccaps = gst_caps_new_simple ("video/x-h263",
          "variant", G_TYPE_STRING, "itu",
          "h263version", G_TYPE_STRING, "h263", NULL);
    }
  }
  if (!srccaps)
    goto no_caps;

  res = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (filter), srccaps);
  gst_caps_unref (srccaps);

  return res;

  /* ERRORS */
no_encoding_name:
  {
    GST_ERROR_OBJECT (filter, "no encoding-name");
    return FALSE;
  }
no_caps:
  {
    GST_ERROR_OBJECT (filter, "invalid encoding-name");
    return FALSE;
  }
}

static GstBuffer *
gst_rtp_h263p_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpH263PDepay *rtph263pdepay;
  GstBuffer *outbuf;
  gint payload_len;
  guint8 *payload;
  gboolean P, V, M;
  guint header_len;
  guint8 PLEN, PEBIT;
  GstRTPBuffer rtp = { NULL };

  rtph263pdepay = GST_RTP_H263P_DEPAY (depayload);

  /* flush remaining data on discont */
  if (GST_BUFFER_IS_DISCONT (buf)) {
    GST_LOG_OBJECT (depayload, "DISCONT, flushing adapter");
    gst_adapter_clear (rtph263pdepay->adapter);
    rtph263pdepay->wait_start = TRUE;
  }

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);

  payload_len = gst_rtp_buffer_get_payload_len (&rtp);
  header_len = 2;

  if (payload_len < header_len)
    goto too_small;

  payload = gst_rtp_buffer_get_payload (&rtp);

  M = gst_rtp_buffer_get_marker (&rtp);

  /*  0                   1
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |   RR    |P|V|   PLEN    |PEBIT|
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  P = (payload[0] & 0x04) == 0x04;
  V = (payload[0] & 0x02) == 0x02;
  PLEN = ((payload[0] & 0x1) << 5) | (payload[1] >> 3);
  PEBIT = payload[1] & 0x7;

  GST_LOG_OBJECT (depayload, "P %d, V %d, PLEN %d, PEBIT %d", P, V, PLEN,
      PEBIT);

  if (V) {
    header_len++;
  }
  if (PLEN) {
    header_len += PLEN;
  }

  if ((!P && payload_len < header_len) || (P && payload_len < header_len - 2))
    goto too_small;

  if (P) {
    /* FIXME, have to make the packet writable hear. Better to reset these
     * bytes when we copy the packet below */
    rtph263pdepay->wait_start = FALSE;
    header_len -= 2;
    payload[header_len] = 0;
    payload[header_len + 1] = 0;
  }

  if (rtph263pdepay->wait_start)
    goto waiting_start;

  if (payload_len < header_len)
    goto too_small;

  /* FIXME do not ignore the VRC header (See RFC 2429 section 4.2) */
  /* FIXME actually use the RTP picture header when it is lost in the network */
  /* for now strip off header */
  payload += header_len;
  payload_len -= header_len;

  if (M) {
    /* frame is completed: append to previous, push it out */
    guint len, padlen;
    guint avail;
    GstMapInfo map;

    GST_LOG_OBJECT (depayload, "Frame complete");

    avail = gst_adapter_available (rtph263pdepay->adapter);
    len = avail + payload_len;
    padlen = (len % 4) + 4;

    outbuf = gst_buffer_new_and_alloc (len + padlen);

    gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
    memset (map.data + len, 0, padlen);

    /* prepend previous data */
    if (avail > 0) {
      gst_adapter_copy (rtph263pdepay->adapter, map.data, 0, avail);
      gst_adapter_flush (rtph263pdepay->adapter, avail);
    }
    memcpy (map.data + avail, payload, payload_len);
    gst_buffer_unmap (outbuf, &map);
    gst_rtp_buffer_unmap (&rtp);

    return outbuf;

  } else {
    /* frame not completed: store in adapter */
    outbuf = gst_buffer_new_and_alloc (payload_len);

    GST_LOG_OBJECT (depayload, "Frame incomplete, storing %d", payload_len);
    gst_buffer_fill (outbuf, 0, payload, payload_len);

    gst_adapter_push (rtph263pdepay->adapter, outbuf);
    gst_rtp_buffer_unmap (&rtp);
  }
  return NULL;

too_small:
  {
    GST_ELEMENT_WARNING (rtph263pdepay, STREAM, DECODE,
        ("Packet payload was too small"), (NULL));
    gst_rtp_buffer_unmap (&rtp);
    return NULL;
  }
waiting_start:
  {
    GST_DEBUG_OBJECT (rtph263pdepay, "waiting for picture start");
    gst_rtp_buffer_unmap (&rtp);
    return NULL;
  }
}

static GstStateChangeReturn
gst_rtp_h263p_depay_change_state (GstElement * element,
    GstStateChange transition)
{
  GstRtpH263PDepay *rtph263pdepay;
  GstStateChangeReturn ret;

  rtph263pdepay = GST_RTP_H263P_DEPAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_adapter_clear (rtph263pdepay->adapter);
      rtph263pdepay->wait_start = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }
  return ret;
}

gboolean
gst_rtp_h263p_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph263pdepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_H263P_DEPAY);
}
