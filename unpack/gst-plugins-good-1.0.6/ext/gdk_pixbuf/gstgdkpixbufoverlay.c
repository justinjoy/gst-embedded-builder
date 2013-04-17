/* GStreamer GdkPixbuf overlay
 * Copyright (C) 2012 Tim-Philipp Müller <tim centricular net>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */

/**
 * SECTION:element-gdkpixbufoverlay
 * @see_also:
 *
 * The gdkpixbufoverlay element overlays an image loaded from file onto
 * a video stream.
 *
 * Changing the positioning or overlay width and height properties at runtime
 * is supported, but it might be prudent to to protect the property setting
 * code with GST_BASE_TRANSFORM_LOCK and GST_BASE_TRANSFORM_UNLOCK, as
 * g_object_set() is not atomic for multiple properties passed in one go.
 *
 * Changing the image at runtime is currently not supported.
 *
 * Negative offsets are also not yet supported.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! gdkpixbufoverlay location=image.png ! autovideosink
 * ]|
 * Overlays the image in image.png onto the test video picture produced by
 * videotestsrc.
 * </refsect2>
 *
 * Since: 0.10.33
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include "gstgdkpixbufoverlay.h"

#include <gst/video/gstvideometa.h>

GST_DEBUG_CATEGORY_STATIC (gdkpixbufoverlay_debug);
#define GST_CAT_DEFAULT gdkpixbufoverlay_debug

static void gst_gdk_pixbuf_overlay_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_gdk_pixbuf_overlay_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_gdk_pixbuf_overlay_finalize (GObject * object);

static gboolean gst_gdk_pixbuf_overlay_start (GstBaseTransform * trans);
static gboolean gst_gdk_pixbuf_overlay_stop (GstBaseTransform * trans);
static GstFlowReturn
gst_gdk_pixbuf_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);
static void gst_gdk_pixbuf_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * outbuf);
static gboolean gst_gdk_pixbuf_overlay_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_OFFSET_X,
  PROP_OFFSET_Y,
  PROP_RELATIVE_X,
  PROP_RELATIVE_Y,
  PROP_OVERLAY_WIDTH,
  PROP_OVERLAY_HEIGHT,
  PROP_ALPHA
};

#define VIDEO_FORMATS "{ RGBx, RGB, BGR, BGRx, xRGB, xBGR, " \
    "RGBA, BGRA, ARGB, ABGR, I420, YV12, AYUV, YUY2, UYVY, " \
    "v308, v210, v216, Y41B, Y42B, Y444, YVYU, NV12, NV21, UYVP, " \
    "RGB16, BGR16, RGB15, BGR15, UYVP, A420, YUV9, YVU9, " \
    "IYU1, ARGB64, AYUV64, r210, I420_10LE, I420_10BE, " \
    "GRAY8, GRAY16_BE, GRAY16_LE }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS))
    );

G_DEFINE_TYPE (GstGdkPixbufOverlay, gst_gdk_pixbuf_overlay,
    GST_TYPE_VIDEO_FILTER);

static void
gst_gdk_pixbuf_overlay_class_init (GstGdkPixbufOverlayClass * klass)
{
  GstVideoFilterClass *videofilter_class = GST_VIDEO_FILTER_CLASS (klass);
  GstBaseTransformClass *basetrans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_gdk_pixbuf_overlay_set_property;
  gobject_class->get_property = gst_gdk_pixbuf_overlay_get_property;
  gobject_class->finalize = gst_gdk_pixbuf_overlay_finalize;

  basetrans_class->start = GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_start);
  basetrans_class->stop = GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_stop);

  basetrans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_before_transform);

  videofilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_set_info);
  videofilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_gdk_pixbuf_overlay_transform_frame_ip);

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "Location of image file to overlay", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_X,
      g_param_spec_int ("offset-x", "X Offset",
          "Horizontal offset of overlay image in pixels from top-left corner "
          "of video image", G_MININT, G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_Y,
      g_param_spec_int ("offset-y", "Y Offset",
          "Vertical offset of overlay image in pixels from top-left corner "
          "of video image", G_MININT, G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RELATIVE_X,
      g_param_spec_double ("relative-x", "Relative X Offset",
          "Horizontal offset of overlay image in fractions of video image "
          "width, from top-left corner of video image", 0.0, 1.0, 0.0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RELATIVE_Y,
      g_param_spec_double ("relative-y", "Relative Y Offset",
          "Vertical offset of overlay image in fractions of video image "
          "height, from top-left corner of video image", 0.0, 1.0, 0.0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_WIDTH,
      g_param_spec_int ("overlay-width", "Overlay Width",
          "Width of overlay image in pixels (0 = same as overlay image)", 0,
          G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OVERLAY_HEIGHT,
      g_param_spec_int ("overlay-height", "Overlay Height",
          "Height of overlay image in pixels (0 = same as overlay image)", 0,
          G_MAXINT, 0,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE
          | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ALPHA,
      g_param_spec_double ("alpha", "Alpha", "Global alpha of overlay image",
          0.0, 1.0, 1.0, GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING
          | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "GdkPixbuf Overlay", "Filter/Effect/Video",
      "Overlay an image onto a video stream",
      "Tim-Philipp Müller <tim centricular net>");
  GST_DEBUG_CATEGORY_INIT (gdkpixbufoverlay_debug, "gdkpixbufoverlay", 0,
      "debug category for gdkpixbufoverlay element");
}

static void
gst_gdk_pixbuf_overlay_init (GstGdkPixbufOverlay * overlay)
{
  overlay->offset_x = 0;
  overlay->offset_y = 0;

  overlay->relative_x = 0.0;
  overlay->relative_y = 0.0;

  overlay->overlay_width = 0;
  overlay->overlay_height = 0;

  overlay->alpha = 1.0;
}

void
gst_gdk_pixbuf_overlay_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);

  switch (property_id) {
    case PROP_LOCATION:
      g_free (overlay->location);
      overlay->location = g_value_dup_string (value);
      break;
    case PROP_OFFSET_X:
      overlay->offset_x = g_value_get_int (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_OFFSET_Y:
      overlay->offset_y = g_value_get_int (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_RELATIVE_X:
      overlay->relative_x = g_value_get_double (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_RELATIVE_Y:
      overlay->relative_y = g_value_get_double (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_OVERLAY_WIDTH:
      overlay->overlay_width = g_value_get_int (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_OVERLAY_HEIGHT:
      overlay->overlay_height = g_value_get_int (value);
      overlay->update_composition = TRUE;
      break;
    case PROP_ALPHA:
      overlay->alpha = g_value_get_double (value);
      overlay->update_composition = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (overlay);
}

void
gst_gdk_pixbuf_overlay_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  GST_OBJECT_LOCK (overlay);

  switch (property_id) {
    case PROP_LOCATION:
      g_value_set_string (value, overlay->location);
      break;
    case PROP_OFFSET_X:
      g_value_set_int (value, overlay->offset_x);
      break;
    case PROP_OFFSET_Y:
      g_value_set_int (value, overlay->offset_y);
      break;
    case PROP_RELATIVE_X:
      g_value_set_double (value, overlay->relative_x);
      break;
    case PROP_RELATIVE_Y:
      g_value_set_double (value, overlay->relative_y);
      break;
    case PROP_OVERLAY_WIDTH:
      g_value_set_int (value, overlay->overlay_width);
      break;
    case PROP_OVERLAY_HEIGHT:
      g_value_set_int (value, overlay->overlay_height);
      break;
    case PROP_ALPHA:
      g_value_set_double (value, overlay->alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (overlay);
}

void
gst_gdk_pixbuf_overlay_finalize (GObject * object)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (object);

  g_free (overlay->location);
  overlay->location = NULL;

  G_OBJECT_CLASS (gst_gdk_pixbuf_overlay_parent_class)->finalize (object);
}

static gboolean
gst_gdk_pixbuf_overlay_load_image (GstGdkPixbufOverlay * overlay, GError ** err)
{
  GstVideoMeta *video_meta;
  GdkPixbuf *pixbuf;
  guint8 *pixels, *p;
  gint width, height, stride, w, h, plane;

  pixbuf = gdk_pixbuf_new_from_file (overlay->location, err);

  if (pixbuf == NULL)
    return FALSE;

  if (!gdk_pixbuf_get_has_alpha (pixbuf)) {
    GdkPixbuf *alpha_pixbuf;

    /* FIXME: we could do this much more efficiently ourselves below, but
     * we're lazy for now */
    /* FIXME: perhaps expose substitute_color via properties */
    alpha_pixbuf = gdk_pixbuf_add_alpha (pixbuf, FALSE, 0, 0, 0);
    g_object_unref (pixbuf);
    pixbuf = alpha_pixbuf;
  }

  width = gdk_pixbuf_get_width (pixbuf);
  height = gdk_pixbuf_get_height (pixbuf);
  stride = gdk_pixbuf_get_rowstride (pixbuf);
  pixels = gdk_pixbuf_get_pixels (pixbuf);

  /* the memory layout in GdkPixbuf is R-G-B-A, we want:
   *  - B-G-R-A on little-endian platforms
   *  - A-R-G-B on big-endian platforms
   */
  for (h = 0; h < height; ++h) {
    p = pixels + (h * stride);
    for (w = 0; w < width; ++w) {
      guint8 tmp;

      /* R-G-B-A ==> B-G-R-A */
      tmp = p[0];
      p[0] = p[2];
      p[2] = tmp;

      if (G_BYTE_ORDER == G_BIG_ENDIAN) {
        /* B-G-R-A ==> A-R-G-B */
        /* we can probably assume sane alignment */
        *((guint32 *) p) = GUINT32_SWAP_LE_BE (*((guint32 *) p));
      }

      p += 4;
    }
  }

  /* assume we have row padding even for the last row */
  /* transfer ownership of pixbuf to the buffer */
  overlay->pixels = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      pixels, height * stride, 0, height * stride, pixbuf,
      (GDestroyNotify) g_object_unref);

  video_meta = gst_buffer_add_video_meta (overlay->pixels,
      GST_VIDEO_FRAME_FLAG_NONE, GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB,
      width, height);

  for (plane = 0; plane < video_meta->n_planes; ++plane)
    video_meta->stride[plane] = stride;

  overlay->update_composition = TRUE;

  GST_INFO_OBJECT (overlay, "Loaded image, %d x %d", width, height);
  return TRUE;
}

static gboolean
gst_gdk_pixbuf_overlay_start (GstBaseTransform * trans)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (trans);
  GError *err = NULL;

  if (overlay->location != NULL) {
    if (!gst_gdk_pixbuf_overlay_load_image (overlay, &err))
      goto error_loading_image;

    gst_base_transform_set_passthrough (trans, FALSE);
  } else {
    GST_WARNING_OBJECT (overlay, "no image location set, doing nothing");
    gst_base_transform_set_passthrough (trans, TRUE);
  }

  return TRUE;

/* ERRORS */
error_loading_image:
  {
    GST_ELEMENT_ERROR (overlay, RESOURCE, OPEN_READ,
        ("Could not load overlay image."), ("%s", err->message));
    g_error_free (err);
    return FALSE;
  }
}

static gboolean
gst_gdk_pixbuf_overlay_stop (GstBaseTransform * trans)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (trans);

  if (overlay->comp) {
    gst_video_overlay_composition_unref (overlay->comp);
    overlay->comp = NULL;
  }

  gst_buffer_replace (&overlay->pixels, NULL);

  return TRUE;
}

static gboolean
gst_gdk_pixbuf_overlay_set_info (GstVideoFilter * filter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GST_INFO_OBJECT (filter, "caps: %" GST_PTR_FORMAT, incaps);
  return TRUE;
}

static void
gst_gdk_pixbuf_overlay_update_composition (GstGdkPixbufOverlay * overlay)
{
  GstVideoOverlayComposition *comp;
  GstVideoOverlayRectangle *rect;
  GstVideoMeta *overlay_meta;
  gint x, y, width, height;

  if (overlay->comp) {
    gst_video_overlay_composition_unref (overlay->comp);
    overlay->comp = NULL;
  }

  if (overlay->alpha == 0.0)
    return;

  overlay_meta = gst_buffer_get_video_meta (overlay->pixels);

  x = overlay->offset_x + (overlay->relative_x * overlay_meta->width);
  y = overlay->offset_y + (overlay->relative_y * overlay_meta->height);

  /* FIXME: this should work, but seems to crash */
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  width = overlay->overlay_width;
  if (width == 0)
    width = overlay_meta->width;

  height = overlay->overlay_height;
  if (height == 0)
    height = overlay_meta->height;

  GST_DEBUG_OBJECT (overlay, "overlay image dimensions: %d x %d, alpha=%.2f",
      overlay_meta->width, overlay_meta->height, overlay->alpha);
  GST_DEBUG_OBJECT (overlay, "properties: x,y: %d,%d (%g%%,%g%%) - WxH: %dx%d",
      overlay->offset_x, overlay->offset_y,
      overlay->relative_x * 100.0, overlay->relative_y * 100.0,
      overlay->overlay_height, overlay->overlay_width);
  GST_DEBUG_OBJECT (overlay, "overlay rendered: %d x %d @ %d,%d (onto %d x %d)",
      width, height, x, y,
      GST_VIDEO_INFO_WIDTH (&GST_VIDEO_FILTER (overlay)->in_info),
      GST_VIDEO_INFO_HEIGHT (&GST_VIDEO_FILTER (overlay)->in_info));

  rect = gst_video_overlay_rectangle_new_raw (overlay->pixels,
      x, y, width, height, GST_VIDEO_OVERLAY_FORMAT_FLAG_NONE);

  if (overlay->alpha != 1.0)
    gst_video_overlay_rectangle_set_global_alpha (rect, overlay->alpha);

  comp = gst_video_overlay_composition_new (rect);
  gst_video_overlay_rectangle_unref (rect);

  overlay->comp = comp;
}

static void
gst_gdk_pixbuf_overlay_before_transform (GstBaseTransform * trans,
    GstBuffer * outbuf)
{
  GstClockTime stream_time;

  stream_time = gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME,
      GST_BUFFER_TIMESTAMP (outbuf));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (trans), stream_time);
}

static GstFlowReturn
gst_gdk_pixbuf_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  GstGdkPixbufOverlay *overlay = GST_GDK_PIXBUF_OVERLAY (filter);

  GST_OBJECT_LOCK (overlay);

  if (G_UNLIKELY (overlay->update_composition)) {
    gst_gdk_pixbuf_overlay_update_composition (overlay);
    overlay->update_composition = FALSE;
  }

  GST_OBJECT_UNLOCK (overlay);

  if (overlay->comp != NULL)
    gst_video_overlay_composition_blend (overlay->comp, frame);

  return GST_FLOW_OK;
}
