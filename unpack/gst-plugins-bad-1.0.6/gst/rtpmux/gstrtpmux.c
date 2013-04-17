/* RTP muxer element for GStreamer
 *
 * gstrtpmux.c:
 *
 * Copyright (C) <2007-2010> Nokia Corporation.
 *   Contact: Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2007-2010> Collabora Ltd
 *   Contact: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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

/**
 * SECTION:element-rtpmux
 * @see_also: rtpdtmfmux
 *
 * The rtp muxer takes multiple RTP streams having the same clock-rate and
 * muxes into a single stream with a single SSRC.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch rtpmux name=mux ! udpsink host=127.0.0.1 port=8888        \
 *              alsasrc ! alawenc ! rtppcmapay !                        \
 *              application/x-rtp, payload=8, rate=8000 ! mux.sink_0    \
 *              audiotestsrc is-live=1 !                                \
 *              mulawenc ! rtppcmupay !                                 \
 *              application/x-rtp, payload=0, rate=8000 ! mux.sink_1
 * ]|
 * In this example, an audio stream is captured from ALSA and another is
 * generated, both are encoded into different payload types and muxed together
 * so they can be sent on the same port.
 * </refsect2>
 *
 * Last reviewed on 2010-09-30 (0.10.21)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <string.h>

#include "gstrtpmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_rtp_mux_debug);
#define GST_CAT_DEFAULT gst_rtp_mux_debug

enum
{
  ARG_0,
  PROP_TIMESTAMP_OFFSET,
  PROP_SEQNUM_OFFSET,
  PROP_SEQNUM,
  PROP_SSRC
};

#define DEFAULT_TIMESTAMP_OFFSET -1
#define DEFAULT_SEQNUM_OFFSET    -1
#define DEFAULT_SSRC             -1

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("application/x-rtp")
    );

static GstPad *gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_rtp_mux_release_pad (GstElement * element, GstPad * pad);
static GstFlowReturn gst_rtp_mux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstFlowReturn gst_rtp_mux_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * bufferlist);
static gboolean gst_rtp_mux_setcaps (GstPad * pad, GstRTPMux * rtp_mux,
    GstCaps * caps);
static gboolean gst_rtp_mux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_rtp_mux_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstStateChangeReturn gst_rtp_mux_change_state (GstElement *
    element, GstStateChange transition);

static void gst_rtp_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_rtp_mux_dispose (GObject * object);

static gboolean gst_rtp_mux_src_event_real (GstRTPMux * rtp_mux,
    GstEvent * event);

G_DEFINE_TYPE (GstRTPMux, gst_rtp_mux, GST_TYPE_ELEMENT);


static void
gst_rtp_mux_class_init (GstRTPMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;


  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (gstelement_class, "RTP muxer",
      "Codec/Muxer",
      "multiplex N rtp streams into one", "Zeeshan Ali <first.last@nokia.com>");

  gobject_class->get_property = gst_rtp_mux_get_property;
  gobject_class->set_property = gst_rtp_mux_set_property;
  gobject_class->dispose = gst_rtp_mux_dispose;

  klass->src_event = gst_rtp_mux_src_event_real;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_TIMESTAMP_OFFSET, g_param_spec_int ("timestamp-offset",
          "Timestamp Offset",
          "Offset to add to all outgoing timestamps (-1 = random)", -1,
          G_MAXINT, DEFAULT_TIMESTAMP_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM_OFFSET,
      g_param_spec_int ("seqnum-offset", "Sequence number Offset",
          "Offset to add to all outgoing seqnum (-1 = random)", -1, G_MAXINT,
          DEFAULT_SEQNUM_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SEQNUM,
      g_param_spec_uint ("seqnum", "Sequence number",
          "The RTP sequence number of the last processed packet",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SSRC,
      g_param_spec_uint ("ssrc", "SSRC",
          "The SSRC of the packets (-1 == random)",
          0, G_MAXUINT, DEFAULT_SSRC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_rtp_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_rtp_mux_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_rtp_mux_change_state);
}

static void
gst_rtp_mux_dispose (GObject * object)
{
  GstRTPMux *rtp_mux = GST_RTP_MUX (object);
  GList *item;

  g_clear_object (&rtp_mux->last_pad);

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);
    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (gst_rtp_mux_parent_class)->dispose (object);
}

static gboolean
gst_rtp_mux_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRTPMux *rtp_mux = GST_RTP_MUX (parent);
  GstRTPMuxClass *klass;
  gboolean ret = FALSE;

  klass = GST_RTP_MUX_GET_CLASS (rtp_mux);

  ret = klass->src_event (rtp_mux, event);

  return ret;
}

static gboolean
gst_rtp_mux_src_event_real (GstRTPMux * rtp_mux, GstEvent * event)
{
  GstIterator *iter;
  gboolean result = FALSE;
  gboolean done = FALSE;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (rtp_mux));

  while (!done) {
    GValue item = { 0, };

    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
        gst_event_ref (event);
        result |= gst_pad_push_event (g_value_get_object (&item), event);
        g_value_reset (&item);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        result = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (rtp_mux, "Error iterating sinkpads");
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);
  gst_event_unref (event);

  return result;
}

static void
gst_rtp_mux_init (GstRTPMux * rtp_mux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (rtp_mux);

  rtp_mux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_event_function (rtp_mux->srcpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_src_event));
  gst_element_add_pad (GST_ELEMENT (rtp_mux), rtp_mux->srcpad);

  rtp_mux->ssrc = DEFAULT_SSRC;
  rtp_mux->ts_offset = DEFAULT_TIMESTAMP_OFFSET;
  rtp_mux->seqnum_offset = DEFAULT_SEQNUM_OFFSET;

  rtp_mux->last_stop = GST_CLOCK_TIME_NONE;
}

static void
gst_rtp_mux_setup_sinkpad (GstRTPMux * rtp_mux, GstPad * sinkpad)
{
  GstRTPMuxPadPrivate *padpriv = g_slice_new0 (GstRTPMuxPadPrivate);

  /* setup some pad functions */
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (gst_rtp_mux_chain));
  gst_pad_set_chain_list_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_chain_list));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_sink_event));
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_rtp_mux_sink_query));


  gst_segment_init (&padpriv->segment, GST_FORMAT_UNDEFINED);

  gst_pad_set_element_private (sinkpad, padpriv);

  gst_pad_set_active (sinkpad, TRUE);
  gst_element_add_pad (GST_ELEMENT (rtp_mux), sinkpad);
}

static GstPad *
gst_rtp_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstRTPMux *rtp_mux;
  GstPad *newpad;

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (GST_IS_RTP_MUX (element), NULL);

  rtp_mux = GST_RTP_MUX (element);

  if (templ->direction != GST_PAD_SINK) {
    GST_WARNING_OBJECT (rtp_mux, "request pad that is not a SINK pad");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, req_name);
  if (newpad)
    gst_rtp_mux_setup_sinkpad (rtp_mux, newpad);
  else
    GST_WARNING_OBJECT (rtp_mux, "failed to create request pad");

  return newpad;
}

static void
gst_rtp_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstRTPMuxPadPrivate *padpriv;

  GST_OBJECT_LOCK (element);
  padpriv = gst_pad_get_element_private (pad);
  gst_pad_set_element_private (pad, NULL);
  GST_OBJECT_UNLOCK (element);

  gst_element_remove_pad (element, pad);

  if (padpriv) {
    g_slice_free (GstRTPMuxPadPrivate, padpriv);
  }
}

/* Put our own clock-base on the buffer */
static void
gst_rtp_mux_readjust_rtp_timestamp_locked (GstRTPMux * rtp_mux,
    GstRTPMuxPadPrivate * padpriv, GstRTPBuffer * rtpbuffer)
{
  guint32 ts;
  guint32 sink_ts_base = 0;

  if (padpriv && padpriv->have_clock_base)
    sink_ts_base = padpriv->clock_base;

  ts = gst_rtp_buffer_get_timestamp (rtpbuffer) - sink_ts_base +
      rtp_mux->ts_base;
  GST_LOG_OBJECT (rtp_mux, "Re-adjusting RTP ts %u to %u",
      gst_rtp_buffer_get_timestamp (rtpbuffer), ts);
  gst_rtp_buffer_set_timestamp (rtpbuffer, ts);
}

static gboolean
process_buffer_locked (GstRTPMux * rtp_mux, GstRTPMuxPadPrivate * padpriv,
    GstRTPBuffer * rtpbuffer)
{
  GstRTPMuxClass *klass = GST_RTP_MUX_GET_CLASS (rtp_mux);

  if (klass->accept_buffer_locked)
    if (!klass->accept_buffer_locked (rtp_mux, padpriv, rtpbuffer))
      return FALSE;

  rtp_mux->seqnum++;
  gst_rtp_buffer_set_seq (rtpbuffer, rtp_mux->seqnum);

  gst_rtp_buffer_set_ssrc (rtpbuffer, rtp_mux->current_ssrc);
  gst_rtp_mux_readjust_rtp_timestamp_locked (rtp_mux, padpriv, rtpbuffer);
  GST_LOG_OBJECT (rtp_mux,
      "Pushing packet size %" G_GSIZE_FORMAT ", seq=%d, ts=%u",
      rtpbuffer->map[0].size, rtp_mux->seqnum,
      gst_rtp_buffer_get_timestamp (rtpbuffer));

  if (padpriv) {
    if (padpriv->segment.format == GST_FORMAT_TIME)
      GST_BUFFER_PTS (rtpbuffer->buffer) =
          gst_segment_to_running_time (&padpriv->segment, GST_FORMAT_TIME,
          GST_BUFFER_PTS (rtpbuffer->buffer));
  }

  return TRUE;
}

struct BufferListData
{
  GstRTPMux *rtp_mux;
  GstRTPMuxPadPrivate *padpriv;
  gboolean drop;
};

static gboolean
process_list_item (GstBuffer ** buffer, guint idx, gpointer user_data)
{
  struct BufferListData *bd = user_data;
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

  *buffer = gst_buffer_make_writable (*buffer);

  gst_rtp_buffer_map (*buffer, GST_MAP_READWRITE, &rtpbuffer);

  bd->drop = !process_buffer_locked (bd->rtp_mux, bd->padpriv, &rtpbuffer);

  gst_rtp_buffer_unmap (&rtpbuffer);

  if (bd->drop)
    return FALSE;

  if (GST_BUFFER_DURATION_IS_VALID (*buffer) &&
      GST_BUFFER_TIMESTAMP_IS_VALID (*buffer))
    bd->rtp_mux->last_stop = GST_BUFFER_TIMESTAMP (*buffer) +
        GST_BUFFER_DURATION (*buffer);
  else
    bd->rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  return TRUE;
}

static GstFlowReturn
gst_rtp_mux_chain_list (GstPad * pad, GstObject * parent,
    GstBufferList * bufferlist)
{
  GstRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstRTPMuxPadPrivate *padpriv;
  gboolean drop = TRUE;
  struct BufferListData bd;

  rtp_mux = GST_RTP_MUX (parent);

  GST_OBJECT_LOCK (rtp_mux);

  padpriv = gst_pad_get_element_private (pad);
  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    ret = GST_FLOW_NOT_LINKED;
    gst_buffer_list_unref (bufferlist);
    goto out;
  }

  bd.rtp_mux = rtp_mux;
  bd.padpriv = padpriv;
  bd.drop = FALSE;

  bufferlist = gst_buffer_list_make_writable (bufferlist);
  gst_buffer_list_foreach (bufferlist, process_list_item, &bd);

  GST_OBJECT_UNLOCK (rtp_mux);

  if (drop) {
    gst_buffer_list_unref (bufferlist);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push_list (rtp_mux->srcpad, bufferlist);
  }

out:

  return ret;
}

static gboolean
resend_events (GstPad * pad, GstEvent ** event, gpointer user_data)
{
  GstRTPMux *rtp_mux = user_data;

  if (GST_EVENT_TYPE (*event) == GST_EVENT_CAPS) {
    GstCaps *caps;

    gst_event_parse_caps (*event, &caps);
    gst_rtp_mux_setcaps (pad, rtp_mux, caps);
  } else {
    gst_pad_push_event (rtp_mux->srcpad, gst_event_ref (*event));
  }

  return TRUE;
}

static GstFlowReturn
gst_rtp_mux_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstRTPMux *rtp_mux;
  GstFlowReturn ret;
  GstRTPMuxPadPrivate *padpriv;
  gboolean drop;
  gboolean changed = FALSE;
  GstRTPBuffer rtpbuffer = GST_RTP_BUFFER_INIT;

  rtp_mux = GST_RTP_MUX (GST_OBJECT_PARENT (pad));

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);

  if (!padpriv) {
    GST_OBJECT_UNLOCK (rtp_mux);
    gst_buffer_unref (buffer);
    return GST_FLOW_NOT_LINKED;
  }

  buffer = gst_buffer_make_writable (buffer);

  if (!gst_rtp_buffer_map (buffer, GST_MAP_READWRITE, &rtpbuffer)) {
    GST_OBJECT_UNLOCK (rtp_mux);
    gst_buffer_unref (buffer);
    GST_ERROR_OBJECT (rtp_mux, "Invalid RTP buffer");
    return GST_FLOW_ERROR;
  }

  drop = !process_buffer_locked (rtp_mux, padpriv, &rtpbuffer);

  gst_rtp_buffer_unmap (&rtpbuffer);

  if (!drop) {
    if (pad != rtp_mux->last_pad) {
      changed = TRUE;
      g_clear_object (&rtp_mux->last_pad);
      rtp_mux->last_pad = g_object_ref (pad);
    }

    if (GST_BUFFER_DURATION_IS_VALID (buffer) &&
        GST_BUFFER_TIMESTAMP_IS_VALID (buffer))
      rtp_mux->last_stop = GST_BUFFER_TIMESTAMP (buffer) +
          GST_BUFFER_DURATION (buffer);
    else
      rtp_mux->last_stop = GST_CLOCK_TIME_NONE;
  }

  GST_OBJECT_UNLOCK (rtp_mux);

  if (changed)
    gst_pad_sticky_events_foreach (pad, resend_events, rtp_mux);

  if (drop) {
    gst_buffer_unref (buffer);
    ret = GST_FLOW_OK;
  } else {
    ret = gst_pad_push (rtp_mux->srcpad, buffer);
  }

  return ret;
}

static gboolean
gst_rtp_mux_setcaps (GstPad * pad, GstRTPMux * rtp_mux, GstCaps * caps)
{
  GstStructure *structure;
  gboolean ret = FALSE;
  GstRTPMuxPadPrivate *padpriv;

  structure = gst_caps_get_structure (caps, 0);

  if (!structure)
    return FALSE;

  GST_OBJECT_LOCK (rtp_mux);
  padpriv = gst_pad_get_element_private (pad);
  if (padpriv &&
      gst_structure_get_uint (structure, "clock-base", &padpriv->clock_base)) {
    padpriv->have_clock_base = TRUE;
  }
  GST_OBJECT_UNLOCK (rtp_mux);

  caps = gst_caps_copy (caps);

  gst_caps_set_simple (caps,
      "clock-base", G_TYPE_UINT, rtp_mux->ts_base,
      "seqnum-base", G_TYPE_UINT, rtp_mux->seqnum_base, NULL);

  GST_DEBUG_OBJECT (rtp_mux,
      "setting caps %" GST_PTR_FORMAT " on src pad..", caps);
  ret = gst_pad_set_caps (rtp_mux->srcpad, caps);

  if (rtp_mux->ssrc == -1) {
    if (gst_structure_has_field_typed (structure, "ssrc", G_TYPE_UINT)) {
      rtp_mux->current_ssrc = g_value_get_uint
          (gst_structure_get_value (structure, "ssrc"));
    }
  }

  gst_caps_unref (caps);

  return ret;
}

static void
clear_caps (GstCaps * caps, gboolean only_clock_rate)
{
  gint i, j;

  /* Lets only match on the clock-rate */
  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);

    for (j = 0; j < gst_structure_n_fields (s); j++) {
      const gchar *name = gst_structure_nth_field_name (s, j);

      if (strcmp (name, "clock-rate") && (only_clock_rate ||
              (strcmp (name, "ssrc")))) {
        gst_structure_remove_field (s, name);
        j--;
      }
    }
  }
}

static gboolean
same_clock_rate_fold (const GValue * item, GValue * ret, gpointer user_data)
{
  GstPad *mypad = user_data;
  GstPad *pad = g_value_get_object (item);
  GstCaps *peercaps;
  GstCaps *accumcaps;
  GstCaps *intersect;

  if (pad == mypad)
    return TRUE;

  accumcaps = g_value_get_boxed (ret);
  peercaps = gst_pad_peer_query_caps (pad, accumcaps);
  if (!peercaps) {
    g_warning ("no peercaps");
    return TRUE;
  }
  peercaps = gst_caps_make_writable (peercaps);
  clear_caps (peercaps, TRUE);

  intersect = gst_caps_intersect (accumcaps, peercaps);

  g_value_take_boxed (ret, intersect);
  gst_caps_unref (peercaps);

  return !gst_caps_is_empty (intersect);
}

static GstCaps *
gst_rtp_mux_getcaps (GstPad * pad, GstRTPMux * mux, GstCaps * filter)
{
  GstCaps *caps = NULL;
  GstIterator *iter = NULL;
  GValue v = { 0 };
  GstIteratorResult res;
  GstCaps *peercaps;
  GstCaps *othercaps = NULL;
  GstCaps *filtered_caps;

  peercaps = gst_pad_peer_query_caps (mux->srcpad, filter);

  if (peercaps) {
    othercaps = gst_caps_intersect (peercaps,
        gst_pad_get_pad_template_caps (pad));
    gst_caps_unref (peercaps);
  } else {
    othercaps = gst_caps_copy (gst_pad_get_pad_template_caps (mux->srcpad));
  }

  if (filter) {
    filtered_caps = gst_caps_intersect (othercaps, filter);
    gst_caps_unref (othercaps);
  } else {
    filtered_caps = othercaps;
  }

  filtered_caps = gst_caps_make_writable (filtered_caps);
  clear_caps (filtered_caps, FALSE);

  g_value_init (&v, GST_TYPE_CAPS);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (mux));
  do {
    gst_value_set_caps (&v, filtered_caps);
    res = gst_iterator_fold (iter, same_clock_rate_fold, &v, pad);
    gst_iterator_resync (iter);
  } while (res == GST_ITERATOR_RESYNC);
  gst_iterator_free (iter);

  caps = (GstCaps *) gst_value_get_caps (&v);

  if (res == GST_ITERATOR_ERROR) {
    gst_caps_unref (caps);
    caps = gst_caps_new_empty ();
  }

  gst_caps_unref (filtered_caps);

  return caps;
}

static gboolean
gst_rtp_mux_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstRTPMux *mux = GST_RTP_MUX (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_rtp_mux_getcaps (pad, mux, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;


}


static void
gst_rtp_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int (value, rtp_mux->ts_offset);
      break;
    case PROP_SEQNUM_OFFSET:
      g_value_set_int (value, rtp_mux->seqnum_offset);
      break;
    case PROP_SEQNUM:
      GST_OBJECT_LOCK (rtp_mux);
      g_value_set_uint (value, rtp_mux->seqnum);
      GST_OBJECT_UNLOCK (rtp_mux);
      break;
    case PROP_SSRC:
      g_value_set_uint (value, rtp_mux->ssrc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstRTPMux *rtp_mux;

  rtp_mux = GST_RTP_MUX (object);

  switch (prop_id) {
    case PROP_TIMESTAMP_OFFSET:
      rtp_mux->ts_offset = g_value_get_int (value);
      break;
    case PROP_SEQNUM_OFFSET:
      rtp_mux->seqnum_offset = g_value_get_int (value);
      break;
    case PROP_SSRC:
      rtp_mux->ssrc = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rtp_mux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstRTPMux *mux = GST_RTP_MUX (parent);
  gboolean is_pad;
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_rtp_mux_setcaps (pad, mux, caps);
      gst_event_unref (event);
      return ret;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_OBJECT_LOCK (mux);
      mux->last_stop = GST_CLOCK_TIME_NONE;
      GST_OBJECT_UNLOCK (mux);
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstRTPMuxPadPrivate *padpriv;

      GST_OBJECT_LOCK (mux);
      padpriv = gst_pad_get_element_private (pad);

      if (padpriv) {
        gst_event_copy_segment (event, &padpriv->segment);
      }
      GST_OBJECT_UNLOCK (mux);
      break;
    }
    default:
      break;
  }

  GST_OBJECT_LOCK (mux);
  is_pad = (pad == mux->last_pad);
  GST_OBJECT_UNLOCK (mux);

  if (is_pad) {
    return gst_pad_push_event (mux->srcpad, event);
  } else {
    gst_event_unref (event);
    return TRUE;
  }
}

static void
gst_rtp_mux_ready_to_paused (GstRTPMux * rtp_mux)
{

  GST_OBJECT_LOCK (rtp_mux);

  g_clear_object (&rtp_mux->last_pad);

  if (rtp_mux->ssrc == -1)
    rtp_mux->current_ssrc = g_random_int ();
  else
    rtp_mux->current_ssrc = rtp_mux->ssrc;

  if (rtp_mux->seqnum_offset == -1)
    rtp_mux->seqnum_base = g_random_int_range (0, G_MAXUINT16);
  else
    rtp_mux->seqnum_base = rtp_mux->seqnum_offset;
  rtp_mux->seqnum = rtp_mux->seqnum_base;

  if (rtp_mux->ts_offset == -1)
    rtp_mux->ts_base = g_random_int ();
  else
    rtp_mux->ts_base = rtp_mux->ts_offset;

  rtp_mux->last_stop = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (rtp_mux, "set clock-base to %u", rtp_mux->ts_base);

  GST_OBJECT_UNLOCK (rtp_mux);
}

static GstStateChangeReturn
gst_rtp_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstRTPMux *rtp_mux;
  GstStateChangeReturn ret;

  rtp_mux = GST_RTP_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_rtp_mux_ready_to_paused (rtp_mux);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (gst_rtp_mux_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_clear_object (&rtp_mux->last_pad);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_rtp_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_rtp_mux_debug, "rtpmux", 0, "rtp muxer");

  return gst_element_register (plugin, "rtpmux", GST_RANK_NONE,
      GST_TYPE_RTP_MUX);
}
