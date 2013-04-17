/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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
 * SECTION:element-gsty4mdec
 *
 * The gsty4mdec element decodes uncompressed video in YUV4MPEG format.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=file.y4m ! y4mdec ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsty4mdec.h"

#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 32768

GST_DEBUG_CATEGORY (y4mdec_debug);
#define GST_CAT_DEFAULT y4mdec_debug

/* prototypes */


static void gst_y4m_dec_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_y4m_dec_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_y4m_dec_dispose (GObject * object);
static void gst_y4m_dec_finalize (GObject * object);

static GstFlowReturn gst_y4m_dec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_y4m_dec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_y4m_dec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_y4m_dec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static GstStateChangeReturn
gst_y4m_dec_change_state (GstElement * element, GstStateChange transition);

enum
{
  PROP_0
};

/* pad templates */

static GstStaticPadTemplate gst_y4m_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, y4mversion=2")
    );

static GstStaticPadTemplate gst_y4m_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{I420,Y42B,Y444}"))
    );

/* class initialization */
#define gst_y4m_dec_parent_class parent_class
G_DEFINE_TYPE (GstY4mDec, gst_y4m_dec, GST_TYPE_ELEMENT);

static void
gst_y4m_dec_class_init (GstY4mDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_y4m_dec_set_property;
  gobject_class->get_property = gst_y4m_dec_get_property;
  gobject_class->dispose = gst_y4m_dec_dispose;
  gobject_class->finalize = gst_y4m_dec_finalize;

  element_class->change_state = GST_DEBUG_FUNCPTR (gst_y4m_dec_change_state);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_y4m_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_y4m_dec_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "YUV4MPEG demuxer/decoder", "Codec/Demuxer",
      "Demuxes/decodes YUV4MPEG streams", "David Schleef <ds@schleef.org>");
}

static void
gst_y4m_dec_init (GstY4mDec * y4mdec)
{
  y4mdec->adapter = gst_adapter_new ();

  y4mdec->sinkpad =
      gst_pad_new_from_static_template (&gst_y4m_dec_sink_template, "sink");
  gst_pad_set_event_function (y4mdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_sink_event));
  gst_pad_set_chain_function (y4mdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_chain));
  gst_element_add_pad (GST_ELEMENT (y4mdec), y4mdec->sinkpad);

  y4mdec->srcpad = gst_pad_new_from_static_template (&gst_y4m_dec_src_template,
      "src");
  gst_pad_set_event_function (y4mdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_src_event));
  gst_pad_set_query_function (y4mdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_y4m_dec_src_query));
  gst_pad_use_fixed_caps (y4mdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (y4mdec), y4mdec->srcpad);

}

void
gst_y4m_dec_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_y4m_dec_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_y4m_dec_dispose (GObject * object)
{
  GstY4mDec *y4mdec;

  g_return_if_fail (GST_IS_Y4M_DEC (object));
  y4mdec = GST_Y4M_DEC (object);

  /* clean up as possible.  may be called multiple times */
  if (y4mdec->adapter) {
    g_object_unref (y4mdec->adapter);
    y4mdec->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

void
gst_y4m_dec_finalize (GObject * object)
{
  g_return_if_fail (GST_IS_Y4M_DEC (object));

  /* clean up object here */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_y4m_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  g_return_val_if_fail (GST_IS_Y4M_DEC (element), GST_STATE_CHANGE_FAILURE);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static GstClockTime
gst_y4m_dec_frames_to_timestamp (GstY4mDec * y4mdec, int frame_index)
{
  return gst_util_uint64_scale (frame_index, GST_SECOND * y4mdec->info.fps_d,
      y4mdec->info.fps_n);
}

static int
gst_y4m_dec_timestamp_to_frames (GstY4mDec * y4mdec, GstClockTime timestamp)
{
  return gst_util_uint64_scale (timestamp, y4mdec->info.fps_n,
      GST_SECOND * y4mdec->info.fps_d);
}

static int
gst_y4m_dec_bytes_to_frames (GstY4mDec * y4mdec, gint64 bytes)
{
  if (bytes < y4mdec->header_size)
    return 0;
  return (bytes - y4mdec->header_size) / (y4mdec->info.size + 6);
}

static gint64
gst_y4m_dec_frames_to_bytes (GstY4mDec * y4mdec, int frame_index)
{
  return y4mdec->header_size + (y4mdec->info.size + 6) * frame_index;
}

static GstClockTime
gst_y4m_dec_bytes_to_timestamp (GstY4mDec * y4mdec, gint64 bytes)
{
  return gst_y4m_dec_frames_to_timestamp (y4mdec,
      gst_y4m_dec_bytes_to_frames (y4mdec, bytes));
}


static gboolean
gst_y4m_dec_parse_header (GstY4mDec * y4mdec, char *header)
{
  char *end;
  int iformat = 420;
  int interlaced_char = 0;
  gint fps_n = 0, fps_d = 0;
  gint par_n = 0, par_d = 0;
  gint width = 0, height = 0;
  GstVideoFormat format;

  if (memcmp (header, "YUV4MPEG2 ", 10) != 0) {
    return FALSE;
  }

  header += 10;
  while (*header) {
    GST_DEBUG_OBJECT (y4mdec, "parsing at '%s'", header);
    switch (*header) {
      case ' ':
        header++;
        break;
      case 'C':
        header++;
        iformat = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'W':
        header++;
        width = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'H':
        header++;
        height = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'I':
        header++;
        if (header[0] == 0) {
          GST_WARNING_OBJECT (y4mdec, "Expecting interlaced flag");
          return FALSE;
        }
        interlaced_char = header[0];
        header++;
        break;
      case 'F':
        header++;
        fps_n = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        if (header[0] != ':') {
          GST_WARNING_OBJECT (y4mdec, "Expecting :");
          return FALSE;
        }
        header++;
        fps_d = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      case 'A':
        header++;
        par_n = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        if (header[0] != ':') {
          GST_WARNING_OBJECT (y4mdec, "Expecting :");
          return FALSE;
        }
        header++;
        par_d = strtoul (header, &end, 10);
        if (end == header)
          goto error;
        header = end;
        break;
      default:
        GST_WARNING_OBJECT (y4mdec, "Unknown y4m header field '%c', ignoring",
            *header);
        while (*header && *header != ' ')
          header++;
        break;
    }
  }

  switch (iformat) {
    case 420:
      format = GST_VIDEO_FORMAT_I420;
      break;
    case 422:
      format = GST_VIDEO_FORMAT_Y42B;
      break;
    case 444:
      format = GST_VIDEO_FORMAT_Y444;
      break;
    default:
      GST_WARNING_OBJECT (y4mdec, "unknown y4m format %d", iformat);
      return FALSE;
  }

  if (width <= 0 || width > MAX_SIZE || height <= 0 || height > MAX_SIZE) {
    GST_WARNING_OBJECT (y4mdec, "Dimensions %dx%d out of range", width, height);
    return FALSE;
  }

  gst_video_info_init (&y4mdec->info);
  gst_video_info_set_format (&y4mdec->info, format, width, height);

  switch (interlaced_char) {
    case 0:
    case '?':
    case 'p':
      y4mdec->info.interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
      break;
    case 't':
    case 'b':
      y4mdec->info.interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      break;
    default:
      GST_WARNING_OBJECT (y4mdec, "Unknown interlaced char '%c'",
          interlaced_char);
      return FALSE;
      break;
  }

  if (fps_n == 0)
    fps_n = 1;
  if (fps_d == 0)
    fps_d = 1;
  if (par_n == 0)
    par_n = 1;
  if (par_d == 0)
    par_d = 1;

  y4mdec->info.fps_n = fps_n;
  y4mdec->info.fps_d = fps_d;
  y4mdec->info.par_n = par_n;
  y4mdec->info.par_d = par_d;

  return TRUE;
error:
  GST_WARNING_OBJECT (y4mdec, "Expecting number y4m header at '%s'", header);
  return FALSE;
}

static GstFlowReturn
gst_y4m_dec_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstY4mDec *y4mdec;
  int n_avail;
  GstFlowReturn flow_ret = GST_FLOW_OK;
#define MAX_HEADER_LENGTH 80
  char header[MAX_HEADER_LENGTH];
  int i;
  int len;

  y4mdec = GST_Y4M_DEC (parent);

  GST_DEBUG_OBJECT (y4mdec, "chain");

  if (GST_BUFFER_IS_DISCONT (buffer)) {
    GST_DEBUG ("got discont");
    gst_adapter_clear (y4mdec->adapter);
  }

  gst_adapter_push (y4mdec->adapter, buffer);
  n_avail = gst_adapter_available (y4mdec->adapter);

  if (!y4mdec->have_header) {
    gboolean ret;
    GstCaps *caps;

    if (n_avail < MAX_HEADER_LENGTH)
      return GST_FLOW_OK;

    gst_adapter_copy (y4mdec->adapter, (guint8 *) header, 0, MAX_HEADER_LENGTH);

    header[MAX_HEADER_LENGTH - 1] = 0;
    for (i = 0; i < MAX_HEADER_LENGTH; i++) {
      if (header[i] == 0x0a)
        header[i] = 0;
    }

    ret = gst_y4m_dec_parse_header (y4mdec, header);
    if (!ret) {
      GST_ELEMENT_ERROR (y4mdec, STREAM, DECODE,
          ("Failed to parse YUV4MPEG header"), (NULL));
      return GST_FLOW_ERROR;
    }

    y4mdec->header_size = strlen (header) + 1;
    gst_adapter_flush (y4mdec->adapter, y4mdec->header_size);

    caps = gst_video_info_to_caps (&y4mdec->info);
    ret = gst_pad_set_caps (y4mdec->srcpad, caps);
    gst_caps_unref (caps);
    if (!ret) {
      GST_DEBUG_OBJECT (y4mdec, "Couldn't set caps on src pad");
      return GST_FLOW_ERROR;
    }

    y4mdec->have_header = TRUE;
  }

  if (y4mdec->have_new_segment) {
    GstEvent *event;
    GstClockTime start = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment.start);
    GstClockTime stop = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment.stop);
    GstClockTime time = gst_y4m_dec_bytes_to_timestamp (y4mdec,
        y4mdec->segment.time);
    GstSegment seg;

    gst_segment_init (&seg, GST_FORMAT_TIME);
    seg.start = start;
    seg.stop = stop;
    seg.time = time;
    event = gst_event_new_segment (&seg);

    gst_pad_push_event (y4mdec->srcpad, event);
    //gst_event_unref (event);

    y4mdec->have_new_segment = FALSE;
    y4mdec->frame_index = gst_y4m_dec_bytes_to_frames (y4mdec,
        y4mdec->segment.time);
    GST_DEBUG ("new frame_index %d", y4mdec->frame_index);

  }

  while (1) {
    n_avail = gst_adapter_available (y4mdec->adapter);
    if (n_avail < MAX_HEADER_LENGTH)
      break;

    gst_adapter_copy (y4mdec->adapter, (guint8 *) header, 0, MAX_HEADER_LENGTH);
    header[MAX_HEADER_LENGTH - 1] = 0;
    for (i = 0; i < MAX_HEADER_LENGTH; i++) {
      if (header[i] == 0x0a)
        header[i] = 0;
    }
    if (memcmp (header, "FRAME", 5) != 0) {
      GST_ELEMENT_ERROR (y4mdec, STREAM, DECODE,
          ("Failed to parse YUV4MPEG frame"), (NULL));
      flow_ret = GST_FLOW_ERROR;
      break;
    }

    len = strlen (header);
    if (n_avail < y4mdec->info.size + len + 1) {
      /* not enough data */
      GST_DEBUG ("not enough data for frame %d < %" G_GSIZE_FORMAT,
          n_avail, y4mdec->info.size + len + 1);
      break;
    }

    gst_adapter_flush (y4mdec->adapter, len + 1);

    buffer = gst_adapter_take_buffer (y4mdec->adapter, y4mdec->info.size);

    GST_BUFFER_TIMESTAMP (buffer) =
        gst_y4m_dec_frames_to_timestamp (y4mdec, y4mdec->frame_index);
    GST_BUFFER_DURATION (buffer) =
        gst_y4m_dec_frames_to_timestamp (y4mdec, y4mdec->frame_index + 1) -
        GST_BUFFER_TIMESTAMP (buffer);

    y4mdec->frame_index++;

    flow_ret = gst_pad_push (y4mdec->srcpad, buffer);
    if (flow_ret != GST_FLOW_OK)
      break;
  }

  GST_DEBUG ("returning %d", flow_ret);

  return flow_ret;
}

static gboolean
gst_y4m_dec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstY4mDec *y4mdec;

  y4mdec = GST_Y4M_DEC (parent);

  GST_DEBUG_OBJECT (y4mdec, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;

      gst_event_copy_segment (event, &seg);

      GST_DEBUG ("segment: %" GST_SEGMENT_FORMAT, &seg);

      if (seg.format == GST_FORMAT_BYTES) {
        y4mdec->segment = seg;
        y4mdec->have_new_segment = TRUE;
      }

      res = TRUE;
      /* not sure why it's not forwarded, but let's unref it so it
         doesn't leak, remove the unref if it gets forwarded again */
      gst_event_unref (event);
      //res = gst_pad_push_event (y4mdec->srcpad, event);
    }
      break;
    case GST_EVENT_EOS:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (y4mdec->srcpad, event);
      break;
  }

  return res;
}

static gboolean
gst_y4m_dec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res;
  GstY4mDec *y4mdec;

  y4mdec = GST_Y4M_DEC (parent);

  GST_DEBUG_OBJECT (y4mdec, "event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;
      int framenum;
      guint64 byte;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type,
          &start, &stop_type, &stop);

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        break;
      }

      framenum = gst_y4m_dec_timestamp_to_frames (y4mdec, start);
      GST_DEBUG ("seeking to frame %d", framenum);

      byte = gst_y4m_dec_frames_to_bytes (y4mdec, framenum);
      GST_DEBUG ("offset %d", (int) byte);

      gst_event_unref (event);
      event = gst_event_new_seek (rate, GST_FORMAT_BYTES, flags,
          start_type, byte, stop_type, -1);

      res = gst_pad_push_event (y4mdec->sinkpad, event);
    }
      break;
    default:
      res = gst_pad_push_event (y4mdec->sinkpad, event);
      break;
  }

  return res;
}

static gboolean
gst_y4m_dec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstY4mDec *y4mdec = GST_Y4M_DEC (parent);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      GstQuery *peer_query;

      GST_DEBUG ("duration query");

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        res = FALSE;
        GST_DEBUG_OBJECT (y4mdec, "not handling duration query in format %d",
            format);
        break;
      }

      peer_query = gst_query_new_duration (GST_FORMAT_BYTES);

      res = gst_pad_peer_query (y4mdec->sinkpad, peer_query);
      if (res) {
        gint64 duration;
        int n_frames;

        gst_query_parse_duration (peer_query, &format, &duration);

        n_frames = gst_y4m_dec_bytes_to_frames (y4mdec, duration);
        GST_DEBUG ("duration in frames %d", n_frames);

        duration = gst_y4m_dec_frames_to_timestamp (y4mdec, n_frames);
        GST_DEBUG ("duration in time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration));

        gst_query_set_duration (query, GST_FORMAT_TIME, duration);
        res = TRUE;
      }
      gst_query_unref (peer_query);
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}


static gboolean
plugin_init (GstPlugin * plugin)
{

  gst_element_register (plugin, "y4mdec", GST_RANK_SECONDARY,
      gst_y4m_dec_get_type ());

  GST_DEBUG_CATEGORY_INIT (y4mdec_debug, "y4mdec", 0, "y4mdec element");

  return TRUE;
}


GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    y4mdec,
    "Demuxes/decodes YUV4MPEG streams",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
