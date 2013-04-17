/* GStreamer
 *
 * Copyright (C) 2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * Description: V4L2 sink element
 *  Created on: Jul 2, 2009
 *      Author: Rob Clark <rob@ti.com>
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
 * SECTION:element-v4l2sink
 *
 * v4l2sink can be used to display video to v4l2 devices (screen overlays
 * provided by the graphics hardware, tv-out, etc)
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch-1.0 videotestsrc ! v4l2sink device=/dev/video1
 * ]| This pipeline displays a test pattern on /dev/video1
 * |[
 * gst-launch-1.0 -v videotestsrc ! navigationtest ! v4l2sink
 * ]| A pipeline to test navigation events.
 * While moving the mouse pointer over the test signal you will see a black box
 * following the mouse pointer. If you press the mouse button somewhere on the
 * video and release it somewhere else a green box will appear where you pressed
 * the button and a red one where you released it. (The navigationtest element
 * is part of gst-plugins-good.) You can observe here that even if the images
 * are scaled through hardware the pointer coordinates are converted back to the
 * original video frame geometry so that the box can be drawn to the correct
 * position. This also handles borders correctly, limiting coordinates to the
 * image area
 * </refsect2>
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gst/video/gstvideometa.h"

#include "gstv4l2colorbalance.h"
#include "gstv4l2tuner.h"
#ifdef HAVE_XVIDEO
#include "gstv4l2videooverlay.h"
#endif
#include "gstv4l2vidorient.h"

#include "gstv4l2sink.h"
#include "gst/gst-i18n-plugin.h"

#include <string.h>

GST_DEBUG_CATEGORY (v4l2sink_debug);
#define GST_CAT_DEFAULT v4l2sink_debug

#define DEFAULT_PROP_DEVICE   "/dev/video1"

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
  PROP_OVERLAY_TOP,
  PROP_OVERLAY_LEFT,
  PROP_OVERLAY_WIDTH,
  PROP_OVERLAY_HEIGHT,
  PROP_CROP_TOP,
  PROP_CROP_LEFT,
  PROP_CROP_WIDTH,
  PROP_CROP_HEIGHT,
};


GST_IMPLEMENT_V4L2_COLOR_BALANCE_METHODS (GstV4l2Sink, gst_v4l2sink);
GST_IMPLEMENT_V4L2_TUNER_METHODS (GstV4l2Sink, gst_v4l2sink);
#ifdef HAVE_XVIDEO
GST_IMPLEMENT_V4L2_VIDEO_OVERLAY_METHODS (GstV4l2Sink, gst_v4l2sink);
#endif
GST_IMPLEMENT_V4L2_VIDORIENT_METHODS (GstV4l2Sink, gst_v4l2sink);

#ifdef HAVE_XVIDEO
static void gst_v4l2sink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure);
static void
gst_v4l2sink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_v4l2sink_navigation_send_event;
}
#endif

#define gst_v4l2sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstV4l2Sink, gst_v4l2sink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TUNER, gst_v4l2sink_tuner_interface_init);
#ifdef HAVE_XVIDEO
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_v4l2sink_video_overlay_interface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION, gst_v4l2sink_navigation_init);
#endif
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_v4l2sink_color_balance_interface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_ORIENTATION,
        gst_v4l2sink_video_orientation_interface_init));


static void gst_v4l2sink_dispose (GObject * object);
static void gst_v4l2sink_finalize (GstV4l2Sink * v4l2sink);

/* GObject methods: */
static void gst_v4l2sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_v4l2sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* GstElement methods: */
static GstStateChangeReturn gst_v4l2sink_change_state (GstElement * element,
    GstStateChange transition);

/* GstBaseSink methods: */
static gboolean gst_v4l2sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);
static GstCaps *gst_v4l2sink_get_caps (GstBaseSink * bsink, GstCaps * filter);
static gboolean gst_v4l2sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
#if 0
static GstFlowReturn gst_v4l2sink_buffer_alloc (GstBaseSink * bsink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
#endif
static GstFlowReturn gst_v4l2sink_show_frame (GstBaseSink * bsink,
    GstBuffer * buf);

static void
gst_v4l2sink_class_init (GstV4l2SinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseSinkClass *basesink_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  basesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->dispose = gst_v4l2sink_dispose;
  gobject_class->finalize = (GObjectFinalizeFunc) gst_v4l2sink_finalize;
  gobject_class->set_property = gst_v4l2sink_set_property;
  gobject_class->get_property = gst_v4l2sink_get_property;

  element_class->change_state = gst_v4l2sink_change_state;

  gst_v4l2_object_install_properties_helper (gobject_class,
      DEFAULT_PROP_DEVICE);

  g_object_class_install_property (gobject_class, PROP_OVERLAY_TOP,
      g_param_spec_int ("overlay-top", "Overlay top",
          "The topmost (y) coordinate of the video overlay; top left corner of screen is 0,0",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_LEFT,
      g_param_spec_int ("overlay-left", "Overlay left",
          "The leftmost (x) coordinate of the video overlay; top left corner of screen is 0,0",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_WIDTH,
      g_param_spec_uint ("overlay-width", "Overlay width",
          "The width of the video overlay; default is equal to negotiated image width",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_HEIGHT,
      g_param_spec_uint ("overlay-height", "Overlay height",
          "The height of the video overlay; default is equal to negotiated image height",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CROP_TOP,
      g_param_spec_int ("crop-top", "Crop top",
          "The topmost (y) coordinate of the video crop; top left corner of image is 0,0",
          0x80000000, 0x7fffffff, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CROP_LEFT,
      g_param_spec_int ("crop-left", "Crop left",
          "The leftmost (x) coordinate of the video crop; top left corner of image is 0,0",
          0x80000000, 0x7fffffff, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("crop-width", "Crop width",
          "The width of the video crop; default is equal to negotiated image width",
          0, 0xffffffff, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("crop-height", "Crop height",
          "The height of the video crop; default is equal to negotiated image height",
          0, 0xffffffff, 0, G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (element_class,
      "Video (video4linux2) Sink", "Sink/Video",
      "Displays frames on a video4linux2 device", "Rob Clark <rob@ti.com>,");

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          gst_v4l2_object_get_all_caps ()));

  basesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_v4l2sink_get_caps);
  basesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_v4l2sink_set_caps);
  basesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2sink_propose_allocation);
  basesink_class->render = GST_DEBUG_FUNCPTR (gst_v4l2sink_show_frame);

  klass->v4l2_class_devices = NULL;

  GST_DEBUG_CATEGORY_INIT (v4l2sink_debug, "v4l2sink", 0, "V4L2 sink element");

}

static void
gst_v4l2sink_init (GstV4l2Sink * v4l2sink)
{
  v4l2sink->v4l2object = gst_v4l2_object_new (GST_ELEMENT (v4l2sink),
      V4L2_BUF_TYPE_VIDEO_OUTPUT, DEFAULT_PROP_DEVICE,
      gst_v4l2_get_output, gst_v4l2_set_output, NULL);

  /* same default value for video output device as is used for
   * v4l2src/capture is no good..  so lets set a saner default
   * (which can be overridden by the one creating the v4l2sink
   * after the constructor returns)
   */
  g_object_set (v4l2sink, "device", "/dev/video1", NULL);

  v4l2sink->probed_caps = NULL;

  v4l2sink->overlay_fields_set = 0;
  v4l2sink->crop_fields_set = 0;
}


static void
gst_v4l2sink_dispose (GObject * object)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (object);

  if (v4l2sink->probed_caps) {
    gst_caps_unref (v4l2sink->probed_caps);
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}


static void
gst_v4l2sink_finalize (GstV4l2Sink * v4l2sink)
{
  gst_v4l2_object_destroy (v4l2sink->v4l2object);

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (v4l2sink));
}


/*
 * flags to indicate which overlay/crop properties the user has set (and
 * therefore which ones should override the defaults from the driver)
 */
enum
{
  RECT_TOP_SET = 0x01,
  RECT_LEFT_SET = 0x02,
  RECT_WIDTH_SET = 0x04,
  RECT_HEIGHT_SET = 0x08
};

static void
gst_v4l2sink_sync_overlay_fields (GstV4l2Sink * v4l2sink)
{
  if (!v4l2sink->overlay_fields_set)
    return;

  if (GST_V4L2_IS_OPEN (v4l2sink->v4l2object)) {

    gint fd = v4l2sink->v4l2object->video_fd;
    struct v4l2_format format;

    memset (&format, 0x00, sizeof (struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;

    if (v4l2_ioctl (fd, VIDIOC_G_FMT, &format) < 0) {
      GST_WARNING_OBJECT (v4l2sink, "VIDIOC_G_FMT failed");
      return;
    }

    GST_DEBUG_OBJECT (v4l2sink,
        "setting overlay: overlay_fields_set=0x%02x, top=%d, left=%d, width=%d, height=%d",
        v4l2sink->overlay_fields_set,
        v4l2sink->overlay.top, v4l2sink->overlay.left,
        v4l2sink->overlay.width, v4l2sink->overlay.height);

    if (v4l2sink->overlay_fields_set & RECT_TOP_SET)
      format.fmt.win.w.top = v4l2sink->overlay.top;
    if (v4l2sink->overlay_fields_set & RECT_LEFT_SET)
      format.fmt.win.w.left = v4l2sink->overlay.left;
    if (v4l2sink->overlay_fields_set & RECT_WIDTH_SET)
      format.fmt.win.w.width = v4l2sink->overlay.width;
    if (v4l2sink->overlay_fields_set & RECT_HEIGHT_SET)
      format.fmt.win.w.height = v4l2sink->overlay.height;

    if (v4l2_ioctl (fd, VIDIOC_S_FMT, &format) < 0) {
      GST_WARNING_OBJECT (v4l2sink, "VIDIOC_S_FMT failed");
      return;
    }

    v4l2sink->overlay_fields_set = 0;
    v4l2sink->overlay = format.fmt.win.w;
  }
}

static void
gst_v4l2sink_sync_crop_fields (GstV4l2Sink * v4l2sink)
{
  if (!v4l2sink->crop_fields_set)
    return;

  if (GST_V4L2_IS_OPEN (v4l2sink->v4l2object)) {

    gint fd = v4l2sink->v4l2object->video_fd;
    struct v4l2_crop crop;

    memset (&crop, 0x00, sizeof (struct v4l2_crop));
    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (v4l2_ioctl (fd, VIDIOC_G_CROP, &crop) < 0) {
      GST_WARNING_OBJECT (v4l2sink, "VIDIOC_G_CROP failed");
      return;
    }

    GST_DEBUG_OBJECT (v4l2sink,
        "setting crop: crop_fields_set=0x%02x, top=%d, left=%d, width=%d, height=%d",
        v4l2sink->crop_fields_set,
        v4l2sink->crop.top, v4l2sink->crop.left,
        v4l2sink->crop.width, v4l2sink->crop.height);

    if (v4l2sink->crop_fields_set & RECT_TOP_SET)
      crop.c.top = v4l2sink->crop.top;
    if (v4l2sink->crop_fields_set & RECT_LEFT_SET)
      crop.c.left = v4l2sink->crop.left;
    if (v4l2sink->crop_fields_set & RECT_WIDTH_SET)
      crop.c.width = v4l2sink->crop.width;
    if (v4l2sink->crop_fields_set & RECT_HEIGHT_SET)
      crop.c.height = v4l2sink->crop.height;

    if (v4l2_ioctl (fd, VIDIOC_S_CROP, &crop) < 0) {
      GST_WARNING_OBJECT (v4l2sink, "VIDIOC_S_CROP failed");
      return;
    }

    v4l2sink->crop_fields_set = 0;
    v4l2sink->crop = crop.c;
  }
}


static void
gst_v4l2sink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (object);

  if (!gst_v4l2_object_set_property_helper (v4l2sink->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_OVERLAY_TOP:
        v4l2sink->overlay.top = g_value_get_int (value);
        v4l2sink->overlay_fields_set |= RECT_TOP_SET;
        gst_v4l2sink_sync_overlay_fields (v4l2sink);
        break;
      case PROP_OVERLAY_LEFT:
        v4l2sink->overlay.left = g_value_get_int (value);
        v4l2sink->overlay_fields_set |= RECT_LEFT_SET;
        gst_v4l2sink_sync_overlay_fields (v4l2sink);
        break;
      case PROP_OVERLAY_WIDTH:
        v4l2sink->overlay.width = g_value_get_uint (value);
        v4l2sink->overlay_fields_set |= RECT_WIDTH_SET;
        gst_v4l2sink_sync_overlay_fields (v4l2sink);
        break;
      case PROP_OVERLAY_HEIGHT:
        v4l2sink->overlay.height = g_value_get_uint (value);
        v4l2sink->overlay_fields_set |= RECT_HEIGHT_SET;
        gst_v4l2sink_sync_overlay_fields (v4l2sink);
        break;
      case PROP_CROP_TOP:
        v4l2sink->crop.top = g_value_get_int (value);
        v4l2sink->crop_fields_set |= RECT_TOP_SET;
        gst_v4l2sink_sync_crop_fields (v4l2sink);
        break;
      case PROP_CROP_LEFT:
        v4l2sink->crop.left = g_value_get_int (value);
        v4l2sink->crop_fields_set |= RECT_LEFT_SET;
        gst_v4l2sink_sync_crop_fields (v4l2sink);
        break;
      case PROP_CROP_WIDTH:
        v4l2sink->crop.width = g_value_get_uint (value);
        v4l2sink->crop_fields_set |= RECT_WIDTH_SET;
        gst_v4l2sink_sync_crop_fields (v4l2sink);
        break;
      case PROP_CROP_HEIGHT:
        v4l2sink->crop.height = g_value_get_uint (value);
        v4l2sink->crop_fields_set |= RECT_HEIGHT_SET;
        gst_v4l2sink_sync_crop_fields (v4l2sink);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}


static void
gst_v4l2sink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (object);

  if (!gst_v4l2_object_get_property_helper (v4l2sink->v4l2object,
          prop_id, value, pspec)) {
    switch (prop_id) {
      case PROP_OVERLAY_TOP:
        g_value_set_int (value, v4l2sink->overlay.top);
        break;
      case PROP_OVERLAY_LEFT:
        g_value_set_int (value, v4l2sink->overlay.left);
        break;
      case PROP_OVERLAY_WIDTH:
        g_value_set_uint (value, v4l2sink->overlay.width);
        break;
      case PROP_OVERLAY_HEIGHT:
        g_value_set_uint (value, v4l2sink->overlay.height);
        break;
      case PROP_CROP_TOP:
        g_value_set_int (value, v4l2sink->crop.top);
        break;
      case PROP_CROP_LEFT:
        g_value_set_int (value, v4l2sink->crop.left);
        break;
      case PROP_CROP_WIDTH:
        g_value_set_uint (value, v4l2sink->crop.width);
        break;
      case PROP_CROP_HEIGHT:
        g_value_set_uint (value, v4l2sink->crop.height);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
  }
}

static GstStateChangeReturn
gst_v4l2sink_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (element);

  GST_DEBUG_OBJECT (v4l2sink, "%d -> %d",
      GST_STATE_TRANSITION_CURRENT (transition),
      GST_STATE_TRANSITION_NEXT (transition));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* open the device */
      if (!gst_v4l2_object_open (v4l2sink->v4l2object))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_v4l2_object_stop (v4l2sink->v4l2object))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      /* we need to call stop here too */
      if (!gst_v4l2_object_stop (v4l2sink->v4l2object))
        return GST_STATE_CHANGE_FAILURE;
      /* close the device */
      if (!gst_v4l2_object_close (v4l2sink->v4l2object))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}


static GstCaps *
gst_v4l2sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (bsink);
  GstCaps *ret;
  GSList *walk;
  GSList *formats;

  if (!GST_V4L2_IS_OPEN (v4l2sink->v4l2object)) {
    /* FIXME: copy? */
    GST_DEBUG_OBJECT (v4l2sink, "device is not open");
    return gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (v4l2sink));
  }

  if (v4l2sink->probed_caps == NULL) {
    formats = gst_v4l2_object_get_format_list (v4l2sink->v4l2object);

    ret = gst_caps_new_empty ();

    for (walk = formats; walk; walk = walk->next) {
      struct v4l2_fmtdesc *format;

      GstStructure *template;

      format = (struct v4l2_fmtdesc *) walk->data;

      template = gst_v4l2_object_v4l2fourcc_to_structure (format->pixelformat);

      if (template) {
        GstCaps *tmp;

        tmp =
            gst_v4l2_object_probe_caps_for_format (v4l2sink->v4l2object,
            format->pixelformat, template);
        if (tmp)
          gst_caps_append (ret, tmp);

        gst_structure_free (template);
      } else {
        GST_DEBUG_OBJECT (v4l2sink, "unknown format %u", format->pixelformat);
      }
    }
    v4l2sink->probed_caps = ret;
  }

  if (filter) {
    ret =
        gst_caps_intersect_full (filter, v4l2sink->probed_caps,
        GST_CAPS_INTERSECT_FIRST);
  } else {
    ret = gst_caps_ref (v4l2sink->probed_caps);
  }

  GST_INFO_OBJECT (v4l2sink, "probed caps: %p", ret);
  LOG_CAPS (v4l2sink, ret);

  return ret;
}

static gboolean
gst_v4l2sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (bsink);
  GstV4l2Object *obj = v4l2sink->v4l2object;

  LOG_CAPS (v4l2sink, caps);

  if (!GST_V4L2_IS_OPEN (v4l2sink->v4l2object)) {
    GST_DEBUG_OBJECT (v4l2sink, "device is not open");
    return FALSE;
  }

  if (!gst_v4l2_object_stop (obj))
    goto stop_failed;

  if (!gst_v4l2_object_set_format (v4l2sink->v4l2object, caps))
    goto invalid_format;

  gst_v4l2sink_sync_overlay_fields (v4l2sink);
  gst_v4l2sink_sync_crop_fields (v4l2sink);

#ifdef HAVE_XVIDEO
  gst_v4l2_video_overlay_prepare_window_handle (v4l2sink->v4l2object, TRUE);
#endif

  GST_INFO_OBJECT (v4l2sink, "outputting buffers via mmap()");

  v4l2sink->video_width = GST_V4L2_WIDTH (v4l2sink->v4l2object);
  v4l2sink->video_height = GST_V4L2_HEIGHT (v4l2sink->v4l2object);

  /* TODO: videosink width/height should be scaled according to
   * pixel-aspect-ratio
   */
  GST_VIDEO_SINK_WIDTH (v4l2sink) = v4l2sink->video_width;
  GST_VIDEO_SINK_HEIGHT (v4l2sink) = v4l2sink->video_height;

  return TRUE;

  /* ERRORS */
stop_failed:
  {
    GST_DEBUG_OBJECT (v4l2sink, "failed to stop streaming");
    return FALSE;
  }
invalid_format:
  {
    /* error already posted */
    GST_DEBUG_OBJECT (v4l2sink, "can't set format");
    return FALSE;
  }
}

static gboolean
gst_v4l2sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (bsink);
  GstV4l2Object *obj = v4l2sink->v4l2object;
  GstBufferPool *pool;
  guint size = 0;
  GstCaps *caps;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = obj->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;
    GstStructure *config;

    /* we had a pool, check caps */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    GST_DEBUG_OBJECT (v4l2sink,
        "we had a pool with caps %" GST_PTR_FORMAT, pcaps);
    if (!gst_caps_is_equal (caps, pcaps)) {
      gst_structure_free (config);
      gst_object_unref (pool);
      goto different_caps;
    }
    gst_structure_free (config);
  }
  /* we need at least 2 buffers to operate */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);

  if (pool)
    gst_object_unref (pool);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (v4l2sink, "no caps specified");
    return FALSE;
  }
different_caps:
  {
    /* different caps, we can't use this pool */
    GST_DEBUG_OBJECT (v4l2sink, "pool has different caps");
    return FALSE;
  }
}

/* called after A/V sync to render frame */
static GstFlowReturn
gst_v4l2sink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstFlowReturn ret;
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (bsink);
  GstV4l2Object *obj = v4l2sink->v4l2object;

  GST_DEBUG_OBJECT (v4l2sink, "render buffer: %p", buf);

  if (G_UNLIKELY (obj->pool == NULL))
    goto not_negotiated;

  ret =
      gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL_CAST (obj->pool), buf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ERROR_OBJECT (bsink, "not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

#ifdef HAVE_XVIDEO
static void
gst_v4l2sink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstV4l2Sink *v4l2sink = GST_V4L2SINK (navigation);
  GstV4l2Xv *xv = v4l2sink->v4l2object->xv;
  GstPad *peer;

  if (!xv)
    return;

  if ((peer = gst_pad_get_peer (GST_VIDEO_SINK_PAD (v4l2sink)))) {
    GstVideoRectangle rect;
    gdouble x, y, xscale = 1.0, yscale = 1.0;

    gst_v4l2_video_overlay_get_render_rect (v4l2sink->v4l2object, &rect);

    /* We calculate scaling using the original video frames geometry to
     * include pixel aspect ratio scaling.
     */
    xscale = (gdouble) v4l2sink->video_width / rect.w;
    yscale = (gdouble) v4l2sink->video_height / rect.h;

    /* Converting pointer coordinates to the non scaled geometry */
    if (gst_structure_get_double (structure, "pointer_x", &x)) {
      x = MIN (x, rect.x + rect.w);
      x = MAX (x - rect.x, 0);
      gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
          (gdouble) x * xscale, NULL);
    }
    if (gst_structure_get_double (structure, "pointer_y", &y)) {
      y = MIN (y, rect.y + rect.h);
      y = MAX (y - rect.y, 0);
      gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
          (gdouble) y * yscale, NULL);
    }

    gst_pad_send_event (peer, gst_event_new_navigation (structure));
    gst_object_unref (peer);
  }
}
#endif
