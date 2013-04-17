/* GStreamer
 * Copyright (C) 2010 David Schleef <ds@entropywave.com>
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
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "gstrgb2bayer.h"

#define GST_CAT_DEFAULT gst_rgb2bayer_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void gst_rgb2bayer_finalize (GObject * object);

static GstCaps *gst_rgb2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean
gst_rgb2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size);
static gboolean
gst_rgb2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_rgb2bayer_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static GstStaticPadTemplate gst_rgb2bayer_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("ARGB"))
    );

#if 0
/* do these later */
static GstStaticPadTemplate gst_rgb2bayer_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_BGRx ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_ARGB ";"
        GST_VIDEO_CAPS_BGRA ";" GST_VIDEO_CAPS_ABGR)
    );
#endif

static GstStaticPadTemplate gst_rgb2bayer_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-bayer,"
        "format=(string){bggr,gbrg,grbg,rggb},"
        "width=[1,MAX],height=[1,MAX]," "framerate=(fraction)[0/1,MAX]")
    );

/* class initialization */

#define gst_rgb2bayer_parent_class parent_class
G_DEFINE_TYPE (GstRGB2Bayer, gst_rgb2bayer, GST_TYPE_BASE_TRANSFORM);

static void
gst_rgb2bayer_class_init (GstRGB2BayerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_rgb2bayer_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rgb2bayer_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rgb2bayer_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "RGB to Bayer converter",
      "Filter/Converter/Video",
      "Converts video/x-raw to video/x-bayer",
      "David Schleef <ds@entropywave.com>");

  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_rgb2bayer_transform_caps);
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_rgb2bayer_get_unit_size);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_rgb2bayer_set_caps);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_rgb2bayer_transform);

  GST_DEBUG_CATEGORY_INIT (gst_rgb2bayer_debug, "rgb2bayer", 0,
      "rgb2bayer element");
}

static void
gst_rgb2bayer_init (GstRGB2Bayer * rgb2bayer)
{

}

void
gst_rgb2bayer_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static GstCaps *
gst_rgb2bayer_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstStructure *structure;
  GstStructure *new_structure;
  GstCaps *newcaps;
  const GValue *value;

  GST_DEBUG_OBJECT (trans, "transforming caps (from) %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);

  if (direction == GST_PAD_SRC) {
    newcaps = gst_caps_new_empty_simple ("video/x-raw");
  } else {
    newcaps = gst_caps_new_empty_simple ("video/x-bayer");
  }
  new_structure = gst_caps_get_structure (newcaps, 0);

  value = gst_structure_get_value (structure, "width");
  gst_structure_set_value (new_structure, "width", value);

  value = gst_structure_get_value (structure, "height");
  gst_structure_set_value (new_structure, "height", value);

  value = gst_structure_get_value (structure, "framerate");
  gst_structure_set_value (new_structure, "framerate", value);

  GST_DEBUG_OBJECT (trans, "transforming caps (into) %" GST_PTR_FORMAT,
      newcaps);

  if (filter) {
    GstCaps *tmpcaps = newcaps;
    newcaps = gst_caps_intersect (newcaps, filter);
    gst_caps_unref (tmpcaps);
  }

  return newcaps;
}

static gboolean
gst_rgb2bayer_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
    gsize * size)
{
  GstStructure *structure;
  int width;
  int height;
  const char *name;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    name = gst_structure_get_name (structure);
    /* Our name must be either video/x-bayer video/x-raw */
    if (g_str_equal (name, "video/x-bayer")) {
      *size = width * height;
      return TRUE;
    } else {
      /* For output, calculate according to format */
      *size = width * height * 4;
      return TRUE;
    }

  }

  return FALSE;
}

static gboolean
gst_rgb2bayer_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstRGB2Bayer *rgb2bayer = GST_RGB_2_BAYER (trans);
  GstStructure *structure;
  const char *format;
  GstVideoInfo info;

  GST_DEBUG ("in caps %" GST_PTR_FORMAT " out caps %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_info_from_caps (&info, incaps))
    return FALSE;

  rgb2bayer->info = info;

  structure = gst_caps_get_structure (outcaps, 0);

  gst_structure_get_int (structure, "width", &rgb2bayer->width);
  gst_structure_get_int (structure, "height", &rgb2bayer->height);

  format = gst_structure_get_string (structure, "format");
  if (g_str_equal (format, "bggr")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_BGGR;
  } else if (g_str_equal (format, "gbrg")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_GBRG;
  } else if (g_str_equal (format, "grbg")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_GRBG;
  } else if (g_str_equal (format, "rggb")) {
    rgb2bayer->format = GST_RGB_2_BAYER_FORMAT_RGGB;
  } else {
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_rgb2bayer_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstRGB2Bayer *rgb2bayer = GST_RGB_2_BAYER (trans);
  GstMapInfo map;
  guint8 *dest;
  guint8 *src;
  int i, j;
  int height = rgb2bayer->height;
  int width = rgb2bayer->width;
  GstVideoFrame frame;

  gst_video_frame_map (&frame, &rgb2bayer->info, inbuf, GST_MAP_READ);

  gst_buffer_map (outbuf, &map, GST_MAP_READ);
  dest = map.data;
  src = GST_VIDEO_FRAME_PLANE_DATA (&frame, 0);

  for (j = 0; j < height; j++) {
    guint8 *dest_line = dest + width * j;
    guint8 *src_line = src + width * 4 * j;

    for (i = 0; i < width; i++) {
      int is_blue = ((j & 1) << 1) | (i & 1);
      if (is_blue == rgb2bayer->format) {
        dest_line[i] = src_line[i * 4 + 3];
      } else if ((is_blue ^ 3) == rgb2bayer->format) {
        dest_line[i] = src_line[i * 4 + 1];
      } else {
        dest_line[i] = src_line[i * 4 + 2];
      }
    }
  }
  gst_buffer_unmap (outbuf, &map);
  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;
}
