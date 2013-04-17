/*
 * GStreamer Funnel element
 *
 * Copyright 2007 Collabora Ltd.
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk>
 * Copyright 2007 Nokia Corp.
 *
 * gstfunnel.c: Simple Funnel element
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:element-funnel
 *
 * Takes packets from various input sinks into one output source.
 *
 * funnel always outputs a single, open ended segment from
 * 0 with in %GST_FORMAT_TIME and outputs the buffers of the
 * different sinkpads with timestamps that are set to the
 * running time for that stream. funnel does not synchronize
 * the different input streams but simply forwards all buffers
 * immediately when they arrive.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfunnel.h"

GST_DEBUG_CATEGORY_STATIC (gst_funnel_debug);
#define GST_CAT_DEFAULT gst_funnel_debug

GType gst_funnel_pad_get_type (void);
#define GST_TYPE_FUNNEL_PAD \
  (gst_funnel_pad_get_type())
#define GST_FUNNEL_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FUNNEL_PAD, GstFunnelPad))
#define GST_FUNNEL_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FUNNEL_PAD, GstFunnelPadClass))
#define GST_IS_FUNNEL_PAD(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FUNNEL_PAD))
#define GST_IS_FUNNEL_PAD_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FUNNEL_PAD))
#define GST_FUNNEL_PAD_CAST(obj) \
  ((GstFunnelPad *)(obj))

typedef struct _GstFunnelPad GstFunnelPad;
typedef struct _GstFunnelPadClass GstFunnelPadClass;

struct _GstFunnelPad
{
  GstPad parent;

  GstSegment segment;
  gboolean got_eos;
};

struct _GstFunnelPadClass
{
  GstPadClass parent;
};

G_DEFINE_TYPE (GstFunnelPad, gst_funnel_pad, GST_TYPE_PAD);

static void
gst_funnel_pad_class_init (GstFunnelPadClass * klass)
{
}

static void
gst_funnel_pad_reset (GstFunnelPad * pad)
{
  gst_segment_init (&pad->segment, GST_FORMAT_UNDEFINED);
  pad->got_eos = FALSE;
}

static void
gst_funnel_pad_init (GstFunnelPad * pad)
{
  gst_funnel_pad_reset (pad);
}

static GstStaticPadTemplate funnel_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate funnel_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define _do_init \
  GST_DEBUG_CATEGORY_INIT (gst_funnel_debug, "funnel", 0, "funnel element");
#define gst_funnel_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstFunnel, gst_funnel, GST_TYPE_ELEMENT, _do_init);

static GstStateChangeReturn gst_funnel_change_state (GstElement * element,
    GstStateChange transition);
static GstPad *gst_funnel_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_funnel_release_pad (GstElement * element, GstPad * pad);

static GstFlowReturn gst_funnel_sink_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_funnel_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_funnel_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static gboolean gst_funnel_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static void
gst_funnel_dispose (GObject * object)
{
  GList *item;

restart:
  for (item = GST_ELEMENT_PADS (object); item; item = g_list_next (item)) {
    GstPad *pad = GST_PAD (item->data);

    if (GST_PAD_IS_SINK (pad)) {
      gst_element_release_request_pad (GST_ELEMENT (object), pad);
      goto restart;
    }
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_funnel_class_init (GstFunnelClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_funnel_dispose);

  gst_element_class_set_static_metadata (gstelement_class,
      "Funnel pipe fitting", "Generic", "N-to-1 pipe fitting",
      "Olivier Crete <olivier.crete@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&funnel_src_template));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_funnel_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_funnel_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_funnel_change_state);
}

static void
gst_funnel_init (GstFunnel * funnel)
{
  funnel->srcpad = gst_pad_new_from_static_template (&funnel_src_template,
      "src");
  gst_pad_set_event_function (funnel->srcpad, gst_funnel_src_event);
  gst_pad_use_fixed_caps (funnel->srcpad);
  gst_element_add_pad (GST_ELEMENT (funnel), funnel->srcpad);
}

static GstPad *
gst_funnel_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *sinkpad;

  GST_DEBUG_OBJECT (element, "requesting pad");

  sinkpad = GST_PAD_CAST (g_object_new (GST_TYPE_FUNNEL_PAD,
          "name", name, "direction", templ->direction, "template", templ,
          NULL));

  gst_pad_set_chain_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_event));
  gst_pad_set_query_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_funnel_sink_query));

  gst_pad_set_active (sinkpad, TRUE);

  gst_element_add_pad (element, sinkpad);

  return sinkpad;
}

static gboolean
gst_funnel_all_sinkpads_eos_unlocked (GstFunnel * funnel)
{
  GstElement *element = GST_ELEMENT_CAST (funnel);
  GList *item;

  if (element->numsinkpads == 0)
    return FALSE;

  for (item = element->sinkpads; item != NULL; item = g_list_next (item)) {
    GstFunnelPad *sinkpad = item->data;

    if (!sinkpad->got_eos)
      return FALSE;
  }

  return TRUE;
}

static void
gst_funnel_release_pad (GstElement * element, GstPad * pad)
{
  GstFunnel *funnel = GST_FUNNEL (element);
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);
  gboolean got_eos;
  gboolean send_eos = FALSE;

  GST_DEBUG_OBJECT (funnel, "releasing pad");

  gst_pad_set_active (pad, FALSE);

  got_eos = fpad->got_eos;

  gst_element_remove_pad (GST_ELEMENT_CAST (funnel), pad);

  GST_OBJECT_LOCK (funnel);
  if (!got_eos && gst_funnel_all_sinkpads_eos_unlocked (funnel)) {
    GST_DEBUG_OBJECT (funnel, "Pad removed. All others are EOS. Sending EOS");
    send_eos = TRUE;
  }
  GST_OBJECT_UNLOCK (funnel);

  if (send_eos)
    if (!gst_pad_push_event (funnel->srcpad, gst_event_new_eos ()))
      GST_WARNING_OBJECT (funnel, "Failure pushing EOS");
}

static GstFlowReturn
gst_funnel_sink_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstFlowReturn res;
  GstFunnel *funnel = GST_FUNNEL (parent);
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);
  GstEvent *event = NULL;
  GstClockTime newts;
#if 0
  GstCaps *padcaps;
#endif

  GST_DEBUG_OBJECT (funnel, "received buffer %p", buffer);

  GST_OBJECT_LOCK (funnel);

  if (fpad->got_eos) {
    GST_OBJECT_UNLOCK (funnel);
    GST_WARNING_OBJECT (funnel, "Got buffer on pad that received EOS");
    res = GST_FLOW_EOS;
    gst_buffer_unref (buffer);
    goto out;
  }

  if (fpad->segment.format == GST_FORMAT_UNDEFINED) {
    GST_WARNING_OBJECT (funnel, "Got buffer without segment,"
        " setting segment [0,inf[");
    gst_segment_init (&fpad->segment, GST_FORMAT_TIME);
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (buffer)))
    fpad->segment.position = GST_BUFFER_TIMESTAMP (buffer);

  newts = gst_segment_to_running_time (&fpad->segment,
      fpad->segment.format, GST_BUFFER_TIMESTAMP (buffer));
  if (newts != GST_BUFFER_TIMESTAMP (buffer)) {
    buffer = gst_buffer_make_writable (buffer);
    GST_BUFFER_TIMESTAMP (buffer) = newts;
  }

  if (!funnel->has_segment) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_TIME);
    event = gst_event_new_segment (&segment);
    funnel->has_segment = TRUE;
  }
  GST_OBJECT_UNLOCK (funnel);

  if (event) {
    if (!gst_pad_push_event (funnel->srcpad, event))
      GST_WARNING_OBJECT (funnel, "Could not push out newsegment event");
  }
#if 0
  GST_OBJECT_LOCK (pad);
  padcaps = GST_PAD_CAPS (funnel->srcpad);
  GST_OBJECT_UNLOCK (pad);

  if (GST_BUFFER_CAPS (buffer) && GST_BUFFER_CAPS (buffer) != padcaps) {
    if (!gst_pad_set_caps (funnel->srcpad, GST_BUFFER_CAPS (buffer))) {
      res = GST_FLOW_NOT_NEGOTIATED;
      gst_buffer_unref (buffer);
      goto out;
    }
  }
#endif

  res = gst_pad_push (funnel->srcpad, buffer);

  GST_LOG_OBJECT (funnel, "handled buffer %s", gst_flow_get_name (res));

out:

  return res;
}

static gboolean
gst_funnel_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFunnel *funnel = GST_FUNNEL (parent);
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);
  gboolean forward = TRUE;
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEGMENT:
    {
      GST_OBJECT_LOCK (funnel);
      gst_event_copy_segment (event, &fpad->segment);
      GST_OBJECT_UNLOCK (funnel);

      forward = FALSE;
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_OBJECT_LOCK (funnel);
      gst_segment_init (&fpad->segment, GST_FORMAT_UNDEFINED);
      funnel->has_segment = FALSE;
      fpad->got_eos = FALSE;
      GST_OBJECT_UNLOCK (funnel);
    }
      break;
    case GST_EVENT_EOS:
    {
      GST_OBJECT_LOCK (funnel);
      fpad->got_eos = TRUE;

      if (!gst_funnel_all_sinkpads_eos_unlocked (funnel)) {
        GST_DEBUG_OBJECT (funnel,
            "Got EOS, but not from all sinkpads. Skipping");
        forward = FALSE;
      } else {
        GST_DEBUG_OBJECT (funnel, "Got EOS from all sinkpads. Forwarding");
      }
      GST_OBJECT_UNLOCK (funnel);
      break;
    }
    default:
      break;
  }

  if (forward)
    res = gst_pad_push_event (funnel->srcpad, event);
  else
    gst_event_unref (event);

  return res;
}

static gboolean
gst_funnel_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFunnel *funnel = GST_FUNNEL (parent);
  gboolean forward = TRUE;
  gboolean res = TRUE;

  if (forward)
    res = gst_pad_peer_query (funnel->srcpad, query);

  return res;
}

static gboolean
gst_funnel_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstElement *funnel;
  GstIterator *iter;
  GstPad *sinkpad;
  gboolean result = FALSE;
  gboolean done = FALSE;
  GValue value = { 0, };

  funnel = GST_ELEMENT_CAST (parent);

  iter = gst_element_iterate_sink_pads (funnel);

  while (!done) {
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        sinkpad = g_value_get_object (&value);
        gst_event_ref (event);
        result |= gst_pad_push_event (sinkpad, event);
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        result = FALSE;
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (funnel, "Error iterating sinkpads");
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&value);
  gst_iterator_free (iter);
  gst_event_unref (event);

  return result;
}

static void
reset_pad (const GValue * data, gpointer user_data)
{
  GstPad *pad = g_value_get_object (data);
  GstFunnelPad *fpad = GST_FUNNEL_PAD_CAST (pad);

  GST_OBJECT_LOCK (pad);
  gst_funnel_pad_reset (fpad);
  GST_OBJECT_UNLOCK (pad);
}

static GstStateChangeReturn
gst_funnel_change_state (GstElement * element, GstStateChange transition)
{
  GstFunnel *funnel = GST_FUNNEL (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      GstIterator *iter = gst_element_iterate_sink_pads (element);
      GstIteratorResult res;

      do {
        res = gst_iterator_foreach (iter, reset_pad, NULL);
      } while (res == GST_ITERATOR_RESYNC);

      gst_iterator_free (iter);

      if (res == GST_ITERATOR_ERROR)
        return GST_STATE_CHANGE_FAILURE;

      GST_OBJECT_LOCK (funnel);
      funnel->has_segment = FALSE;
      GST_OBJECT_UNLOCK (funnel);
    }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}
