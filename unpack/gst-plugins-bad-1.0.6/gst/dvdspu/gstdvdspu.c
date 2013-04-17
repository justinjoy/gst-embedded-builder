/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
 * SECTION:element-dvdspu
 *
 * DVD sub picture overlay element.
 * 
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * FIXME: gst-launch ...
 * ]| FIXME: description for the sample launch pipeline
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

/* FIXME 0.11: suppress warnings for deprecated API such as GStaticRecMutex
 * with newer GLib versions (>= 2.31.0) */
#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include <gst/gst-i18n-plugin.h>
#include <gst/video/video.h>

#include <string.h>

#include <gst/gst.h>

#include "gstdvdspu.h"

GST_DEBUG_CATEGORY (dvdspu_debug);
#define GST_CAT_DEFAULT dvdspu_debug

GstDVDSPUDebugFlags dvdspu_debug_flags;

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

static GstStaticPadTemplate video_sink_factory =
GST_STATIC_PAD_TEMPLATE ("video",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, " "format = (string) { I420, NV12, YV12 }, "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, " "format = (string) { I420, NV12, YV12 }, "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    );

static GstStaticPadTemplate subpic_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("subpicture",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("subpicture/x-dvd; subpicture/x-pgs")
    );

static const guint32 default_clut[16] = {
  0xb48080, 0x248080, 0x628080, 0xd78080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080,
  0x808080, 0x808080, 0x808080, 0x808080
};

#define gst_dvd_spu_parent_class parent_class
G_DEFINE_TYPE (GstDVDSpu, gst_dvd_spu, GST_TYPE_ELEMENT);

static void gst_dvd_spu_dispose (GObject * object);
static void gst_dvd_spu_finalize (GObject * object);
static GstStateChangeReturn gst_dvd_spu_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_dvd_spu_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dvd_spu_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstCaps *gst_dvd_spu_video_proxy_getcaps (GstPad * pad,
    GstCaps * filter);
static gboolean gst_dvd_spu_video_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_dvd_spu_video_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_dvd_spu_video_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dvd_spu_video_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static void gst_dvd_spu_redraw_still (GstDVDSpu * dvdspu, gboolean force);

static void gst_dvd_spu_check_still_updates (GstDVDSpu * dvdspu);
static GstFlowReturn gst_dvd_spu_subpic_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);
static gboolean gst_dvd_spu_subpic_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_dvd_spu_subpic_set_caps (GstPad * pad, GstCaps * caps);

static void gst_dvd_spu_clear (GstDVDSpu * dvdspu);
static void gst_dvd_spu_flush_spu_info (GstDVDSpu * dvdspu,
    gboolean process_events);
static void gst_dvd_spu_advance_spu (GstDVDSpu * dvdspu, GstClockTime new_ts);
static void gstspu_render (GstDVDSpu * dvdspu, GstBuffer * buf);
static GstFlowReturn
dvdspu_handle_vid_buffer (GstDVDSpu * dvdspu, GstBuffer * buf);
static void gst_dvd_spu_handle_dvd_event (GstDVDSpu * dvdspu, GstEvent * event);

static void
gst_dvd_spu_class_init (GstDVDSpuClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = (GObjectFinalizeFunc) gst_dvd_spu_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_dvd_spu_finalize;

  gstelement_class->change_state = gst_dvd_spu_change_state;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&video_sink_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&subpic_sink_factory));

  gst_element_class_set_static_metadata (gstelement_class,
      "Sub-picture Overlay", "Mixer/Video/Overlay/SubPicture/DVD/Bluray",
      "Parses Sub-Picture command streams and renders the SPU overlay "
      "onto the video as it passes through",
      "Jan Schmidt <thaytan@noraisin.net>");
}

static void
gst_dvd_spu_init (GstDVDSpu * dvdspu)
{
  dvdspu->videosinkpad =
      gst_pad_new_from_static_template (&video_sink_factory, "video");
  gst_pad_set_chain_function (dvdspu->videosinkpad, gst_dvd_spu_video_chain);
  gst_pad_set_event_function (dvdspu->videosinkpad, gst_dvd_spu_video_event);
  gst_pad_set_query_function (dvdspu->videosinkpad, gst_dvd_spu_video_query);

  dvdspu->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_event_function (dvdspu->srcpad, gst_dvd_spu_src_event);
  gst_pad_set_query_function (dvdspu->srcpad, gst_dvd_spu_src_query);

  dvdspu->subpic_sinkpad =
      gst_pad_new_from_static_template (&subpic_sink_factory, "subpicture");
  gst_pad_set_chain_function (dvdspu->subpic_sinkpad, gst_dvd_spu_subpic_chain);
  gst_pad_set_event_function (dvdspu->subpic_sinkpad, gst_dvd_spu_subpic_event);

  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->videosinkpad);
  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->subpic_sinkpad);
  gst_element_add_pad (GST_ELEMENT (dvdspu), dvdspu->srcpad);

  g_mutex_init (&dvdspu->spu_lock);
  dvdspu->pending_spus = g_queue_new ();

  gst_dvd_spu_clear (dvdspu);
}

static void
gst_dvd_spu_clear (GstDVDSpu * dvdspu)
{
  gst_dvd_spu_flush_spu_info (dvdspu, FALSE);
  gst_segment_init (&dvdspu->subp_seg, GST_FORMAT_UNDEFINED);

  dvdspu->spu_input_type = SPU_INPUT_TYPE_NONE;

  gst_buffer_replace (&dvdspu->ref_frame, NULL);
  gst_buffer_replace (&dvdspu->pending_frame, NULL);

  dvdspu->spu_state.info.fps_n = 25;
  dvdspu->spu_state.info.fps_d = 1;

  gst_segment_init (&dvdspu->video_seg, GST_FORMAT_UNDEFINED);
}

static void
gst_dvd_spu_dispose (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);

  /* need to hold the SPU lock in case other stuff is still running... */
  DVD_SPU_LOCK (dvdspu);
  gst_dvd_spu_clear (dvdspu);
  DVD_SPU_UNLOCK (dvdspu);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_dvd_spu_finalize (GObject * object)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (object);
  gint i;

  for (i = 0; i < 3; i++) {
    if (dvdspu->spu_state.comp_bufs[i] != NULL) {
      g_free (dvdspu->spu_state.comp_bufs[i]);
      dvdspu->spu_state.comp_bufs[i] = NULL;
    }
  }
  g_queue_free (dvdspu->pending_spus);
  g_mutex_clear (&dvdspu->spu_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* With SPU lock held, clear the queue of SPU packets */
static void
gst_dvd_spu_flush_spu_info (GstDVDSpu * dvdspu, gboolean keep_events)
{
  SpuPacket *packet;
  SpuState *state = &dvdspu->spu_state;
  GQueue tmp_q = G_QUEUE_INIT;

  GST_INFO_OBJECT (dvdspu, "Flushing SPU information");

  if (dvdspu->partial_spu) {
    gst_buffer_unref (dvdspu->partial_spu);
    dvdspu->partial_spu = NULL;
  }

  packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  while (packet != NULL) {
    if (packet->buf) {
      gst_buffer_unref (packet->buf);
      g_assert (packet->event == NULL);
      g_free (packet);
    } else if (packet->event) {
      if (keep_events) {
        g_queue_push_tail (&tmp_q, packet);
      } else {
        gst_event_unref (packet->event);
        g_free (packet);
      }
    }
    packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);
  }
  /* Push anything we decided to keep back onto the pending_spus list */
  for (packet = g_queue_pop_head (&tmp_q); packet != NULL;
      packet = g_queue_pop_head (&tmp_q))
    g_queue_push_tail (dvdspu->pending_spus, packet);

  state->flags &= ~(SPU_STATE_FLAGS_MASK);
  state->next_ts = GST_CLOCK_TIME_NONE;

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      gstspu_vobsub_flush (dvdspu);
      break;
    case SPU_INPUT_TYPE_PGS:
      gstspu_pgs_flush (dvdspu);
      break;
    default:
      break;
  }
}

static gboolean
gst_dvd_spu_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (parent);
  GstPad *peer;
  gboolean res = TRUE;

  peer = gst_pad_get_peer (dvdspu->videosinkpad);
  if (peer) {
    res = gst_pad_send_event (peer, event);
    gst_object_unref (peer);
  }
  return res;
}

static gboolean
gst_dvd_spu_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_dvd_spu_video_proxy_getcaps (pad, filter);
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

static gboolean
gst_dvd_spu_video_set_caps (GstPad * pad, GstCaps * caps)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  gboolean res = FALSE;
  GstVideoInfo info;
  gint i;
  SpuState *state;

  if (!gst_video_info_from_caps (&info, caps))
    goto done;

  DVD_SPU_LOCK (dvdspu);

  state = &dvdspu->spu_state;

  state->info = info;
  for (i = 0; i < 3; i++) {
    state->comp_bufs[i] = g_realloc (state->comp_bufs[i],
        sizeof (guint32) * info.width);
  }
  DVD_SPU_UNLOCK (dvdspu);

  res = TRUE;
done:
  gst_object_unref (dvdspu);
  return res;
}

static GstCaps *
gst_dvd_spu_video_proxy_getcaps (GstPad * pad, GstCaps * filter)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  GstCaps *caps;
  GstPad *otherpad;

  /* Proxy the getcaps between videosink and the srcpad, ignoring the 
   * subpicture sink pad */
  otherpad = (pad == dvdspu->srcpad) ? dvdspu->videosinkpad : dvdspu->srcpad;

  caps = gst_pad_peer_query_caps (otherpad, filter);
  if (caps) {
    GstCaps *temp, *templ;

    templ = gst_pad_get_pad_template_caps (otherpad);
    temp = gst_caps_intersect (caps, templ);
    gst_caps_unref (templ);
    gst_caps_unref (caps);
    caps = temp;
  } else {
    caps = gst_pad_get_pad_template_caps (pad);
  }

  gst_object_unref (dvdspu);
  return caps;
}

/* With SPU lock held */
static void
update_video_to_position (GstDVDSpu * dvdspu, GstClockTime new_pos)
{
  SpuState *state = &dvdspu->spu_state;
#if 0
  g_print ("Segment update for video. Advancing from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (dvdspu->video_seg.position), GST_TIME_ARGS (start));
#endif
  while (dvdspu->video_seg.position < new_pos &&
      !(state->flags & SPU_STATE_STILL_FRAME)) {
    DVD_SPU_UNLOCK (dvdspu);
    if (dvdspu_handle_vid_buffer (dvdspu, NULL) != GST_FLOW_OK) {
      DVD_SPU_LOCK (dvdspu);
      break;
    }
    DVD_SPU_LOCK (dvdspu);
  }
}

static gboolean
gst_dvd_spu_video_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) parent;
  SpuState *state = &dvdspu->spu_state;
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_dvd_spu_video_set_caps (pad, caps);
      if (res)
        res = gst_pad_push_event (dvdspu->srcpad, event);
      else
        gst_event_unref (event);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      gboolean in_still;

      if (gst_video_event_parse_still_frame (event, &in_still)) {
        GstBuffer *to_push = NULL;

        /* Forward the event before handling */
        res = gst_pad_event_default (pad, parent, event);

        GST_DEBUG_OBJECT (dvdspu,
            "Still frame event on video pad: in-still = %d", in_still);

        DVD_SPU_LOCK (dvdspu);
        if (in_still) {
          state->flags |= SPU_STATE_STILL_FRAME;
          /* Entering still. Advance the SPU to make sure the state is 
           * up to date */
          gst_dvd_spu_check_still_updates (dvdspu);
          /* And re-draw the still frame to make sure it appears on
           * screen, otherwise the last frame  might have been discarded 
           * by QoS */
          gst_dvd_spu_redraw_still (dvdspu, TRUE);
          to_push = dvdspu->pending_frame;
          dvdspu->pending_frame = NULL;
        } else {
          state->flags &= ~(SPU_STATE_STILL_FRAME);
        }
        DVD_SPU_UNLOCK (dvdspu);
        if (to_push)
          gst_pad_push (dvdspu->srcpad, to_push);
      } else {
        GST_DEBUG_OBJECT (dvdspu,
            "Custom event %" GST_PTR_FORMAT " on video pad", event);
        res = gst_pad_event_default (pad, parent, event);
      }
      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      if (seg.format != GST_FORMAT_TIME)
        return FALSE;

      /* Only print updates if they have an end time (don't print start_time
       * updates */
      GST_DEBUG_OBJECT (dvdspu, "video pad Segment: %" GST_SEGMENT_FORMAT,
          &seg);

      DVD_SPU_LOCK (dvdspu);

      if (seg.start > dvdspu->video_seg.position) {
        update_video_to_position (dvdspu, seg.start);
      }

      dvdspu->video_seg = seg;
      DVD_SPU_UNLOCK (dvdspu);

      res = gst_pad_event_default (pad, parent, event);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        timestamp += duration;

      DVD_SPU_LOCK (dvdspu);
      GST_LOG_OBJECT (dvdspu, "Received GAP. Advancing to %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
      update_video_to_position (dvdspu, timestamp);
      DVD_SPU_UNLOCK (dvdspu);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      res = gst_pad_event_default (pad, parent, event);
      goto done;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_event_default (pad, parent, event);

      DVD_SPU_LOCK (dvdspu);
      gst_segment_init (&dvdspu->video_seg, GST_FORMAT_UNDEFINED);
      gst_buffer_replace (&dvdspu->ref_frame, NULL);
      gst_buffer_replace (&dvdspu->pending_frame, NULL);

      DVD_SPU_UNLOCK (dvdspu);
      goto done;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

done:
  return res;
#if 0
error:
  gst_event_unref (event);
  return FALSE;
#endif
}

static gboolean
gst_dvd_spu_video_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_dvd_spu_video_proxy_getcaps (pad, filter);
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

static GstFlowReturn
gst_dvd_spu_video_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) parent;
  GstFlowReturn ret;

  g_return_val_if_fail (dvdspu != NULL, GST_FLOW_ERROR);

  GST_LOG_OBJECT (dvdspu, "video buffer %p with TS %" GST_TIME_FORMAT,
      buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  ret = dvdspu_handle_vid_buffer (dvdspu, buf);

  return ret;
}

static GstFlowReturn
dvdspu_handle_vid_buffer (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  GstClockTime new_ts;
  GstFlowReturn ret;
  gboolean using_ref = FALSE;

  DVD_SPU_LOCK (dvdspu);

  if (buf == NULL) {
    GstClockTime next_ts = dvdspu->video_seg.position;

    next_ts += gst_util_uint64_scale_int (GST_SECOND,
        dvdspu->spu_state.info.fps_d, dvdspu->spu_state.info.fps_n);

    /* NULL buffer was passed - use the reference frame and update the timestamp,
     * or else there's nothing to draw, and just return GST_FLOW_OK */
    if (dvdspu->ref_frame == NULL) {
      dvdspu->video_seg.position = next_ts;
      goto no_ref_frame;
    }

    buf = gst_buffer_copy (dvdspu->ref_frame);

#if 0
    g_print ("Duping frame %" GST_TIME_FORMAT " with new TS %" GST_TIME_FORMAT
        "\n", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (next_ts));
#endif

    GST_BUFFER_TIMESTAMP (buf) = next_ts;
    using_ref = TRUE;
  }

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    dvdspu->video_seg.position = GST_BUFFER_TIMESTAMP (buf);
  }

  new_ts = gst_segment_to_running_time (&dvdspu->video_seg, GST_FORMAT_TIME,
      dvdspu->video_seg.position);

#if 0
  g_print ("TS %" GST_TIME_FORMAT " running: %" GST_TIME_FORMAT "\n",
      GST_TIME_ARGS (dvdspu->video_seg.position), GST_TIME_ARGS (new_ts));
#endif

  gst_dvd_spu_advance_spu (dvdspu, new_ts);

  /* If we have an active SPU command set, we store a copy of the frame in case
   * we hit a still and need to draw on it. Otherwise, a reference is
   * sufficient in case we later encounter a still */
  if ((dvdspu->spu_state.flags & SPU_STATE_FORCED_DSP) ||
      ((dvdspu->spu_state.flags & SPU_STATE_FORCED_ONLY) == 0 &&
          (dvdspu->spu_state.flags & SPU_STATE_DISPLAY))) {
    if (using_ref == FALSE) {
      GstBuffer *copy;

      /* Take a copy in case we hit a still frame and need the pristine 
       * frame around */
      copy = gst_buffer_copy (buf);
      gst_buffer_replace (&dvdspu->ref_frame, copy);
      gst_buffer_unref (copy);
    }

    /* Render the SPU overlay onto the buffer */
    buf = gst_buffer_make_writable (buf);

    gstspu_render (dvdspu, buf);
  } else {
    if (using_ref == FALSE) {
      /* Not going to draw anything on this frame, just store a reference
       * in case we hit a still frame and need it */
      gst_buffer_replace (&dvdspu->ref_frame, buf);
    }
  }

  if (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME) {
    GST_DEBUG_OBJECT (dvdspu, "Outputting buffer with TS %" GST_TIME_FORMAT
        "from chain while in still",
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
  }

  DVD_SPU_UNLOCK (dvdspu);

  /* just push out the incoming buffer without touching it */
  ret = gst_pad_push (dvdspu->srcpad, buf);

  return ret;

no_ref_frame:

  DVD_SPU_UNLOCK (dvdspu);

  return GST_FLOW_OK;
}


static void
gstspu_render (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  GstVideoFrame frame;

  gst_video_frame_map (&frame, &dvdspu->spu_state.info, buf, GST_MAP_READWRITE);

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      gstspu_vobsub_render (dvdspu, &frame);
      break;
    case SPU_INPUT_TYPE_PGS:
      gstspu_pgs_render (dvdspu, &frame);
      break;
    default:
      break;
  }
  gst_video_frame_unmap (&frame);
}

/* With SPU LOCK */
static void
gst_dvd_spu_redraw_still (GstDVDSpu * dvdspu, gboolean force)
{
  /* If we have an active SPU command set and a reference frame, copy the
   * frame, redraw the SPU and store it as the pending frame for output */
  if (dvdspu->ref_frame) {
    gboolean redraw = (dvdspu->spu_state.flags & SPU_STATE_FORCED_DSP);
    redraw |= (dvdspu->spu_state.flags & SPU_STATE_FORCED_ONLY) == 0 &&
        (dvdspu->spu_state.flags & SPU_STATE_DISPLAY);

    if (redraw) {
      GstBuffer *buf = gst_buffer_ref (dvdspu->ref_frame);

      buf = gst_buffer_make_writable (buf);

      GST_LOG_OBJECT (dvdspu, "Redraw due to Still Frame with ref %p",
          dvdspu->ref_frame);
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;

      /* Render the SPU overlay onto the buffer */
      gstspu_render (dvdspu, buf);
      gst_buffer_replace (&dvdspu->pending_frame, buf);
      gst_buffer_unref (buf);
    } else if (force) {
      /* Simply output the reference frame */
      GstBuffer *buf = gst_buffer_ref (dvdspu->ref_frame);
      buf = gst_buffer_make_writable (buf);
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      GST_BUFFER_TIMESTAMP (buf) = GST_CLOCK_TIME_NONE;
      GST_BUFFER_DURATION (buf) = GST_CLOCK_TIME_NONE;

      GST_DEBUG_OBJECT (dvdspu, "Pushing reference frame at start of still");

      gst_buffer_replace (&dvdspu->pending_frame, buf);
      gst_buffer_unref (buf);
    } else {
      GST_LOG_OBJECT (dvdspu, "Redraw due to Still Frame skipped");
    }
  } else {
    GST_LOG_OBJECT (dvdspu, "Not redrawing still frame - no ref frame");
  }
}

static void
gst_dvd_spu_handle_dvd_event (GstDVDSpu * dvdspu, GstEvent * event)
{
  gboolean hl_change = FALSE;

  GST_INFO_OBJECT (dvdspu, "DVD event of type %s on subp pad OOB=%d",
      gst_structure_get_string (gst_event_get_structure (event), "event"),
      (GST_EVENT_TYPE (event) == GST_EVENT_CUSTOM_DOWNSTREAM_OOB));

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      hl_change = gstspu_vobsub_handle_dvd_event (dvdspu, event);
      break;
    case SPU_INPUT_TYPE_PGS:
      hl_change = gstspu_pgs_handle_dvd_event (dvdspu, event);
      break;
    default:
      break;
  }

  if (hl_change && (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME)) {
    gst_dvd_spu_redraw_still (dvdspu, FALSE);
  }
}

static gboolean
gstspu_execute_event (GstDVDSpu * dvdspu)
{
  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      return gstspu_vobsub_execute_event (dvdspu);
      break;
    case SPU_INPUT_TYPE_PGS:
      return gstspu_pgs_execute_event (dvdspu);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  return FALSE;
}

/* Advance the SPU packet/command queue to a time. new_ts is in running time */
static void
gst_dvd_spu_advance_spu (GstDVDSpu * dvdspu, GstClockTime new_ts)
{
  SpuState *state = &dvdspu->spu_state;

  if (G_UNLIKELY (dvdspu->spu_input_type == SPU_INPUT_TYPE_NONE))
    return;

  while (state->next_ts == GST_CLOCK_TIME_NONE || state->next_ts <= new_ts) {
    GST_DEBUG_OBJECT (dvdspu,
        "Advancing SPU from TS %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (state->next_ts), GST_TIME_ARGS (new_ts));

    if (!gstspu_execute_event (dvdspu)) {
      GstClockTime vid_run_ts;

      /* No current command buffer, try and get one */
      SpuPacket *packet = (SpuPacket *) g_queue_pop_head (dvdspu->pending_spus);

      if (packet == NULL)
        return;                 /* No SPU packets available */

      vid_run_ts =
          gst_segment_to_running_time (&dvdspu->video_seg, GST_FORMAT_TIME,
          dvdspu->video_seg.position);
      GST_LOG_OBJECT (dvdspu,
          "Popped new SPU packet with TS %" GST_TIME_FORMAT
          ". Video position=%" GST_TIME_FORMAT " (%" GST_TIME_FORMAT
          ") type %s",
          GST_TIME_ARGS (packet->event_ts), GST_TIME_ARGS (vid_run_ts),
          GST_TIME_ARGS (dvdspu->video_seg.position),
          packet->buf ? "buffer" : "event");

      if (packet->buf) {
        switch (dvdspu->spu_input_type) {
          case SPU_INPUT_TYPE_VOBSUB:
            gstspu_vobsub_handle_new_buf (dvdspu, packet->event_ts,
                packet->buf);
            break;
          case SPU_INPUT_TYPE_PGS:
            gstspu_pgs_handle_new_buf (dvdspu, packet->event_ts, packet->buf);
            break;
          default:
            g_assert_not_reached ();
            break;
        }
        g_assert (packet->event == NULL);
      } else if (packet->event)
        gst_dvd_spu_handle_dvd_event (dvdspu, packet->event);

      g_free (packet);
      continue;
    }
  }
}

static void
gst_dvd_spu_check_still_updates (GstDVDSpu * dvdspu)
{
  GstClockTime sub_ts;
  GstClockTime vid_ts;

  if (dvdspu->spu_state.flags & SPU_STATE_STILL_FRAME) {

    vid_ts = gst_segment_to_running_time (&dvdspu->video_seg,
        GST_FORMAT_TIME, dvdspu->video_seg.position);
    sub_ts = gst_segment_to_running_time (&dvdspu->subp_seg,
        GST_FORMAT_TIME, dvdspu->subp_seg.position);

    vid_ts = MAX (vid_ts, sub_ts);

    GST_DEBUG_OBJECT (dvdspu,
        "In still frame - advancing TS to %" GST_TIME_FORMAT
        " to process SPU buffer", GST_TIME_ARGS (vid_ts));
    gst_dvd_spu_advance_spu (dvdspu, vid_ts);
  }
}

static void
submit_new_spu_packet (GstDVDSpu * dvdspu, GstBuffer * buf)
{
  SpuPacket *spu_packet;
  GstClockTime ts;
  GstClockTime run_ts = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (dvdspu,
      "Complete subpicture buffer of %" G_GSIZE_FORMAT " bytes with TS %"
      GST_TIME_FORMAT, gst_buffer_get_size (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

  /* Decide whether to pass this buffer through to the rendering code */
  ts = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (ts)) {
    if (ts < (GstClockTime) dvdspu->subp_seg.start) {
      GstClockTimeDiff diff = dvdspu->subp_seg.start - ts;

      /* Buffer starts before segment, see if we can calculate a running time */
      run_ts =
          gst_segment_to_running_time (&dvdspu->subp_seg, GST_FORMAT_TIME,
          dvdspu->subp_seg.start);
      if (run_ts >= (GstClockTime) diff)
        run_ts -= diff;
      else
        run_ts = GST_CLOCK_TIME_NONE;   /* No running time possible for this subpic */
    } else {
      /* TS within segment, convert to running time */
      run_ts =
          gst_segment_to_running_time (&dvdspu->subp_seg, GST_FORMAT_TIME, ts);
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (run_ts)) {
    /* Complete SPU packet, push it onto the queue for processing when
     * video packets come past */
    spu_packet = g_new0 (SpuPacket, 1);
    spu_packet->buf = buf;

    /* Store the activation time of this buffer in running time */
    spu_packet->event_ts = run_ts;
    GST_INFO_OBJECT (dvdspu,
        "Pushing SPU buf with TS %" GST_TIME_FORMAT " running time %"
        GST_TIME_FORMAT, GST_TIME_ARGS (ts),
        GST_TIME_ARGS (spu_packet->event_ts));

    g_queue_push_tail (dvdspu->pending_spus, spu_packet);

    /* In a still frame condition, advance the SPU to make sure the state is 
     * up to date */
    gst_dvd_spu_check_still_updates (dvdspu);
  } else {
    gst_buffer_unref (buf);
  }
}

static GstFlowReturn
gst_dvd_spu_subpic_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) parent;
  GstFlowReturn ret = GST_FLOW_OK;
  gsize size;

  g_return_val_if_fail (dvdspu != NULL, GST_FLOW_ERROR);

  GST_INFO_OBJECT (dvdspu, "Have subpicture buffer with timestamp %"
      GST_TIME_FORMAT " and size %" G_GSIZE_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), gst_buffer_get_size (buf));

  DVD_SPU_LOCK (dvdspu);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    dvdspu->subp_seg.position = GST_BUFFER_TIMESTAMP (buf);
  }

  if (GST_BUFFER_IS_DISCONT (buf) && dvdspu->partial_spu) {
    gst_buffer_unref (dvdspu->partial_spu);
    dvdspu->partial_spu = NULL;
  }

  if (dvdspu->partial_spu != NULL) {
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      GST_WARNING_OBJECT (dvdspu,
          "Joining subpicture buffer with timestamp to previous");
    dvdspu->partial_spu = gst_buffer_append (dvdspu->partial_spu, buf);
  } else {
    /* If we don't yet have a buffer, wait for one with a timestamp,
     * since that will avoid collecting the 2nd half of a partial buf */
    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      dvdspu->partial_spu = buf;
    else
      gst_buffer_unref (buf);
  }

  if (dvdspu->partial_spu == NULL)
    goto done;

  size = gst_buffer_get_size (dvdspu->partial_spu);

  switch (dvdspu->spu_input_type) {
    case SPU_INPUT_TYPE_VOBSUB:
      if (size > 4) {
        guint8 header[2];
        guint16 packet_size;

        gst_buffer_extract (dvdspu->partial_spu, 0, header, 2);
        packet_size = GST_READ_UINT16_BE (header);
        if (packet_size == size) {
          submit_new_spu_packet (dvdspu, dvdspu->partial_spu);
          dvdspu->partial_spu = NULL;
        } else if (packet_size < size) {
          /* Somehow we collected too much - something is wrong. Drop the
           * packet entirely and wait for a new one */
          GST_DEBUG_OBJECT (dvdspu,
              "Discarding invalid SPU buffer of size %" G_GSIZE_FORMAT, size);

          gst_buffer_unref (dvdspu->partial_spu);
          dvdspu->partial_spu = NULL;
        } else {
          GST_LOG_OBJECT (dvdspu,
              "SPU buffer claims to be of size %u. Collected %" G_GSIZE_FORMAT
              " so far.", packet_size, size);
        }
      }
      break;
    case SPU_INPUT_TYPE_PGS:{
      /* Collect until we have a command buffer that ends exactly at the size
       * we've collected */
      guint8 packet_type;
      guint16 packet_size;
      GstMapInfo map;
      guint8 *ptr, *end;
      gboolean invalid = FALSE;

      gst_buffer_map (dvdspu->partial_spu, &map, GST_MAP_READ);

      ptr = map.data;
      end = ptr + map.size;

      /* FIXME: There's no need to walk the command set each time. We can set a
       * marker and resume where we left off next time */
      /* FIXME: Move the packet parsing and sanity checking into the format-specific modules */
      while (ptr != end) {
        if (ptr + 3 > end)
          break;
        packet_type = *ptr++;
        packet_size = GST_READ_UINT16_BE (ptr);
        ptr += 2;
        if (ptr + packet_size > end)
          break;
        ptr += packet_size;
        /* 0x80 is the END command for PGS packets */
        if (packet_type == 0x80 && ptr != end) {
          /* Extra cruft on the end of the packet -> assume invalid */
          invalid = TRUE;
          break;
        }
      }
      gst_buffer_unmap (dvdspu->partial_spu, &map);

      if (invalid) {
        gst_buffer_unref (dvdspu->partial_spu);
        dvdspu->partial_spu = NULL;
      } else if (ptr == end) {
        GST_DEBUG_OBJECT (dvdspu,
            "Have complete PGS packet of size %" G_GSIZE_FORMAT ". Enqueueing.",
            map.size);
        submit_new_spu_packet (dvdspu, dvdspu->partial_spu);
        dvdspu->partial_spu = NULL;
      }
      break;
    }
    default:
      GST_ERROR_OBJECT (dvdspu, "Input type not configured before SPU passing");
      goto caps_not_set;
  }

done:
  DVD_SPU_UNLOCK (dvdspu);

  return ret;

  /* ERRORS */
caps_not_set:
  {
    GST_ELEMENT_ERROR (dvdspu, RESOURCE, NO_SPACE_LEFT,
        (_("Subpicture format was not configured before data flow")), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
gst_dvd_spu_subpic_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) parent;
  gboolean res = TRUE;

  /* Some events on the subpicture sink pad just get ignored, like 
   * FLUSH_START */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      res = gst_dvd_spu_subpic_set_caps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_OOB:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      gboolean need_push;

      if (!gst_structure_has_name (structure, "application/x-gst-dvd")) {
        res = gst_pad_event_default (pad, parent, event);
        break;
      }

      DVD_SPU_LOCK (dvdspu);
      if (GST_EVENT_IS_SERIALIZED (event)) {
        SpuPacket *spu_packet = g_new0 (SpuPacket, 1);
        GST_DEBUG_OBJECT (dvdspu,
            "Enqueueing DVD event on subpicture pad for later");
        spu_packet->event = event;
        g_queue_push_tail (dvdspu->pending_spus, spu_packet);
      } else {
        gst_dvd_spu_handle_dvd_event (dvdspu, event);
      }

      /* If the handle_dvd_event generated a pending frame, we
       * need to synchronise with the video pad's stream lock and push it.
       * This requires some dancing to preserve locking order and handle
       * flushes correctly */
      need_push = (dvdspu->pending_frame != NULL);
      DVD_SPU_UNLOCK (dvdspu);
      if (need_push) {
        GstBuffer *to_push = NULL;
        gboolean flushing;

        GST_LOG_OBJECT (dvdspu, "Going for stream lock");
        GST_PAD_STREAM_LOCK (dvdspu->videosinkpad);
        GST_LOG_OBJECT (dvdspu, "Got stream lock");
        GST_OBJECT_LOCK (dvdspu->videosinkpad);
        flushing = GST_PAD_IS_FLUSHING (dvdspu->videosinkpad);
        GST_OBJECT_UNLOCK (dvdspu->videosinkpad);

        DVD_SPU_LOCK (dvdspu);
        if (dvdspu->pending_frame == NULL || flushing) {
          /* Got flushed while waiting for the stream lock */
          DVD_SPU_UNLOCK (dvdspu);
        } else {
          to_push = dvdspu->pending_frame;
          dvdspu->pending_frame = NULL;

          DVD_SPU_UNLOCK (dvdspu);
          gst_pad_push (dvdspu->srcpad, to_push);
        }
        GST_LOG_OBJECT (dvdspu, "Dropping stream lock");
        GST_PAD_STREAM_UNLOCK (dvdspu->videosinkpad);
      }

      break;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      /* Only print updates if they have an end time (don't print start_time
       * updates */
      GST_DEBUG_OBJECT (dvdspu, "subpic pad Segment: %" GST_SEGMENT_FORMAT,
          &seg);

      DVD_SPU_LOCK (dvdspu);

      dvdspu->subp_seg = seg;
      GST_LOG_OBJECT (dvdspu, "Subpicture segment now: %" GST_SEGMENT_FORMAT,
          &dvdspu->subp_seg);
      DVD_SPU_UNLOCK (dvdspu);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_GAP:
    {
      GstClockTime timestamp, duration;
      gst_event_parse_gap (event, &timestamp, &duration);
      if (GST_CLOCK_TIME_IS_VALID (duration))
        timestamp += duration;

      DVD_SPU_LOCK (dvdspu);
      dvdspu->subp_seg.position = timestamp;
      GST_LOG_OBJECT (dvdspu, "Received GAP. Segment now: %" GST_SEGMENT_FORMAT,
          &dvdspu->subp_seg);
      DVD_SPU_UNLOCK (dvdspu);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      gst_event_unref (event);
      goto done;
    case GST_EVENT_FLUSH_STOP:
      GST_DEBUG_OBJECT (dvdspu, "Have flush-stop event on SPU pad");
      DVD_SPU_LOCK (dvdspu);
      gst_segment_init (&dvdspu->subp_seg, GST_FORMAT_UNDEFINED);
      gst_dvd_spu_flush_spu_info (dvdspu, TRUE);
      DVD_SPU_UNLOCK (dvdspu);

      /* We don't forward flushes on the spu pad */
      gst_event_unref (event);
      goto done;
    case GST_EVENT_EOS:
      /* drop EOS on the subtitle pad, it means there are no more subtitles,
       * video might still continue, though */
      gst_event_unref (event);
      goto done;
      break;
    default:
      res = gst_pad_event_default (pad, parent, event);
      break;
  }

done:

  return res;
}

static gboolean
gst_dvd_spu_subpic_set_caps (GstPad * pad, GstCaps * caps)
{
  GstDVDSpu *dvdspu = GST_DVD_SPU (gst_pad_get_parent (pad));
  gboolean res = FALSE;
  GstStructure *s;
  SpuInputType input_type;

  s = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (s, "subpicture/x-dvd")) {
    input_type = SPU_INPUT_TYPE_VOBSUB;
  } else if (gst_structure_has_name (s, "subpicture/x-pgs")) {
    input_type = SPU_INPUT_TYPE_PGS;
  } else {
    goto done;
  }

  DVD_SPU_LOCK (dvdspu);
  if (dvdspu->spu_input_type != input_type) {
    GST_INFO_OBJECT (dvdspu, "Incoming SPU packet type changed to %u",
        input_type);
    dvdspu->spu_input_type = input_type;
    gst_dvd_spu_flush_spu_info (dvdspu, TRUE);
  }

  DVD_SPU_UNLOCK (dvdspu);
  res = TRUE;
done:
  gst_object_unref (dvdspu);
  return res;
}

static GstStateChangeReturn
gst_dvd_spu_change_state (GstElement * element, GstStateChange transition)
{
  GstDVDSpu *dvdspu = (GstDVDSpu *) element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      DVD_SPU_LOCK (dvdspu);
      gst_dvd_spu_clear (dvdspu);
      DVD_SPU_UNLOCK (dvdspu);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_dvd_spu_plugin_init (GstPlugin * plugin)
{
  const gchar *env;

  GST_DEBUG_CATEGORY_INIT (dvdspu_debug, "gstspu",
      0, "Sub-picture Overlay decoder/renderer");

  env = g_getenv ("GST_DVD_SPU_DEBUG");

  dvdspu_debug_flags = 0;
  if (env != NULL) {
    if (strstr (env, "render-rectangle") != NULL)
      dvdspu_debug_flags |= GST_DVD_SPU_DEBUG_RENDER_RECTANGLE;
    if (strstr (env, "highlight-rectangle") != NULL)
      dvdspu_debug_flags |= GST_DVD_SPU_DEBUG_HIGHLIGHT_RECTANGLE;
  }
  GST_INFO ("debug flags : 0x%02x", dvdspu_debug_flags);

  return gst_element_register (plugin, "dvdspu",
      GST_RANK_PRIMARY, GST_TYPE_DVD_SPU);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dvdspu,
    "DVD Sub-picture Overlay element",
    gst_dvd_spu_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
