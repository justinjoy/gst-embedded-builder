/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Edgard Lima <edgard.lima@indt.org.br>
 * Copyright (C) <2005> Zeeshan Ali <zeenix@gmail.com>
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
#include "gstrtppcmudepay.h"

/* RtpPcmuDepay signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_rtp_pcmu_depay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_PCMU_STRING ", "
        "encoding-name = (string) \"PCMU\", clock-rate = (int) 8000; "
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "encoding-name = (string) \"PCMU\", clock-rate = (int) [1, MAX ]")
    );

static GstStaticPadTemplate gst_rtp_pcmu_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mulaw, "
        "channels = (int) 1, rate = (int) [1, MAX ]")
    );

static GstBuffer *gst_rtp_pcmu_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);
static gboolean gst_rtp_pcmu_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);

#define gst_rtp_pcmu_depay_parent_class parent_class
G_DEFINE_TYPE (GstRtpPcmuDepay, gst_rtp_pcmu_depay,
    GST_TYPE_RTP_BASE_DEPAYLOAD);

static void
gst_rtp_pcmu_depay_class_init (GstRtpPcmuDepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_depay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_pcmu_depay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP PCMU depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts PCMU audio from RTP packets",
      "Edgard Lima <edgard.lima@indt.org.br>, Zeeshan Ali <zeenix@gmail.com>");

  gstrtpbasedepayload_class->process = gst_rtp_pcmu_depay_process;
  gstrtpbasedepayload_class->set_caps = gst_rtp_pcmu_depay_setcaps;
}

static void
gst_rtp_pcmu_depay_init (GstRtpPcmuDepay * rtppcmudepay)
{
  GstRTPBaseDepayload *depayload;

  depayload = GST_RTP_BASE_DEPAYLOAD (rtppcmudepay);

  gst_pad_use_fixed_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload));
}

static gboolean
gst_rtp_pcmu_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstCaps *srccaps;
  GstStructure *structure;
  gboolean ret;
  gint clock_rate;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 8000;          /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_simple ("audio/x-mulaw",
      "channels", G_TYPE_INT, 1, "rate", G_TYPE_INT, clock_rate, NULL);
  ret = gst_pad_set_caps (GST_RTP_BASE_DEPAYLOAD_SRCPAD (depayload), srccaps);
  gst_caps_unref (srccaps);

  return ret;
}

static GstBuffer *
gst_rtp_pcmu_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstBuffer *outbuf = NULL;
  guint len;
  gboolean marker;
  GstRTPBuffer rtp = { NULL };

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);

  marker = gst_rtp_buffer_get_marker (&rtp);

  GST_DEBUG ("process : got %" G_GSIZE_FORMAT " bytes, mark %d ts %u seqn %d",
      gst_buffer_get_size (buf), marker,
      gst_rtp_buffer_get_timestamp (&rtp), gst_rtp_buffer_get_seq (&rtp));

  len = gst_rtp_buffer_get_payload_len (&rtp);
  outbuf = gst_rtp_buffer_get_payload_buffer (&rtp);
  gst_rtp_buffer_unmap (&rtp);

  if (outbuf) {
    GST_BUFFER_DURATION (outbuf) =
        gst_util_uint64_scale_int (len, GST_SECOND, depayload->clock_rate);

    if (marker) {
      /* mark start of talkspurt with DISCONT */
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    }
  }

  return outbuf;
}

gboolean
gst_rtp_pcmu_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtppcmudepay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_PCMU_DEPAY);
}
