/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2010> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Youness Alaoui <youness.alaoui@collabora.co.uk>
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

/*
 * This file was (probably) generated from gstvideoflip.c,
 * gstvideoflip.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */
/**
 * SECTION:element-videoflip
 *
 * Flips and rotates video.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! videoflip method=clockwise ! videoconvert ! ximagesink
 * ]| This pipeline flips the test image 90 degrees clockwise.
 * </refsect2>
 *
 * Last reviewed on 2010-04-18 (0.10.22)
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoflip.h"

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>

/* GstVideoFlip properties */
enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

#define PROP_METHOD_DEFAULT GST_VIDEO_FLIP_METHOD_IDENTITY

GST_DEBUG_CATEGORY_STATIC (video_flip_debug);
#define GST_CAT_DEFAULT video_flip_debug

static GstStaticPadTemplate gst_video_flip_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ AYUV, "
            "ARGB, BGRA, ABGR, RGBA, Y444, xRGB, RGBx,xBGR, BGRx, "
            "RGB, BGR, I420, YV12, IYUV, YUY2, UYVY, YVYU }"))
    );

static GstStaticPadTemplate gst_video_flip_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ AYUV, "
            "ARGB, BGRA, ABGR, RGBA, Y444, xRGB, RGBx,xBGR, BGRx, "
            "RGB, BGR, I420, YV12, IYUV, YUY2, UYVY, YVYU }"))
    );

#define GST_TYPE_VIDEO_FLIP_METHOD (gst_video_flip_method_get_type())

static const GEnumValue video_flip_methods[] = {
  {GST_VIDEO_FLIP_METHOD_IDENTITY, "Identity (no rotation)", "none"},
  {GST_VIDEO_FLIP_METHOD_90R, "Rotate clockwise 90 degrees", "clockwise"},
  {GST_VIDEO_FLIP_METHOD_180, "Rotate 180 degrees", "rotate-180"},
  {GST_VIDEO_FLIP_METHOD_90L, "Rotate counter-clockwise 90 degrees",
      "counterclockwise"},
  {GST_VIDEO_FLIP_METHOD_HORIZ, "Flip horizontally", "horizontal-flip"},
  {GST_VIDEO_FLIP_METHOD_VERT, "Flip vertically", "vertical-flip"},
  {GST_VIDEO_FLIP_METHOD_TRANS,
      "Flip across upper left/lower right diagonal", "upper-left-diagonal"},
  {GST_VIDEO_FLIP_METHOD_OTHER,
      "Flip across upper right/lower left diagonal", "upper-right-diagonal"},
  {0, NULL, NULL},
};

static GType
gst_video_flip_method_get_type (void)
{
  static GType video_flip_method_type = 0;

  if (!video_flip_method_type) {
    video_flip_method_type = g_enum_register_static ("GstVideoFlipMethod",
        video_flip_methods);
  }
  return video_flip_method_type;
}

#define gst_video_flip_parent_class parent_class
G_DEFINE_TYPE (GstVideoFlip, gst_video_flip, GST_TYPE_VIDEO_FILTER);

static GstCaps *
gst_video_flip_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  GstCaps *ret;
  gint width, height, i;

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);
    gint par_n, par_d;

    if (gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "height", &height)) {

      switch (videoflip->method) {
        case GST_VIDEO_FLIP_METHOD_90R:
        case GST_VIDEO_FLIP_METHOD_90L:
        case GST_VIDEO_FLIP_METHOD_TRANS:
        case GST_VIDEO_FLIP_METHOD_OTHER:
          gst_structure_set (structure, "width", G_TYPE_INT, height,
              "height", G_TYPE_INT, width, NULL);
          if (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
                  &par_n, &par_d)) {
            if (par_n != 1 || par_d != 1) {
              GValue val = { 0, };

              g_value_init (&val, GST_TYPE_FRACTION);
              gst_value_set_fraction (&val, par_d, par_n);
              gst_structure_set_value (structure, "pixel-aspect-ratio", &val);
              g_value_unset (&val);
            }
          }
          break;
        case GST_VIDEO_FLIP_METHOD_IDENTITY:
        case GST_VIDEO_FLIP_METHOD_180:
        case GST_VIDEO_FLIP_METHOD_HORIZ:
        case GST_VIDEO_FLIP_METHOD_VERT:
          gst_structure_set (structure, "width", G_TYPE_INT, width,
              "height", G_TYPE_INT, height, NULL);
          break;
        default:
          g_assert_not_reached ();
          break;
      }
    }
  }

  GST_DEBUG_OBJECT (videoflip, "transformed %" GST_PTR_FORMAT " to %"
      GST_PTR_FORMAT, caps, ret);

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT (videoflip, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
    GST_DEBUG_OBJECT (videoflip, "Intersection %" GST_PTR_FORMAT, ret);
  }

  return ret;
}

static void
gst_video_flip_planar_yuv (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  gint src_y_stride, src_u_stride, src_v_stride;
  gint src_y_height, src_u_height, src_v_height;
  gint src_y_width, src_u_width, src_v_width;
  gint dest_y_stride, dest_u_stride, dest_v_stride;
  gint dest_y_height, dest_u_height, dest_v_height;
  gint dest_y_width, dest_u_width, dest_v_width;

  src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  src_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 1);
  src_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 2);

  dest_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);
  dest_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 1);
  dest_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 2);

  src_y_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 0);
  src_u_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 1);
  src_v_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 2);

  dest_y_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 0);
  dest_u_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 1);
  dest_v_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 2);

  src_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 0);
  src_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 1);
  src_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 2);

  dest_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 0);
  dest_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 1);
  dest_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 2);

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_90L:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[x * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[x * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[x * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_180:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_HORIZ:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[y * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[y * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[y * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_VERT:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + x];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + x];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + x];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_TRANS:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] = s[x * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] = s[x * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_v_stride + x] = s[x * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_OTHER:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_packed_simple (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y, z;
  guint8 const *s;
  guint8 *d;
  gint sw = GST_VIDEO_FRAME_WIDTH (src);
  gint sh = GST_VIDEO_FRAME_HEIGHT (src);
  gint dw = GST_VIDEO_FRAME_WIDTH (dest);
  gint dh = GST_VIDEO_FRAME_HEIGHT (dest);
  gint src_stride, dest_stride;
  gint bpp;

  s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);

  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  dest_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);
  /* This is only true for non-subsampled formats! */
  bpp = GST_VIDEO_FRAME_COMP_PSTRIDE (src, 0);

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[x * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[y * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + x * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_TRANS:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] = s[x * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_OTHER:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


static void
gst_video_flip_y422 (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  gint sw = GST_VIDEO_FRAME_WIDTH (src);
  gint sh = GST_VIDEO_FRAME_HEIGHT (src);
  gint dw = GST_VIDEO_FRAME_WIDTH (dest);
  gint dh = GST_VIDEO_FRAME_HEIGHT (dest);
  gint src_stride, dest_stride;
  gint bpp;
  gint y_offset;
  gint u_offset;
  gint v_offset;
  gint y_stride;

  s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);

  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  dest_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);

  y_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 0);
  u_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 1);
  v_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 2);
  y_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (src, 0);
  bpp = y_stride;

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = (y & ~1);

          u = s[(sh - 1 - x) * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + u_offset]
                + u) >> 1;
          v = s[(sh - 1 - x) * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + v_offset]
                + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - x) * src_stride + y * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - (x + 1)) * src_stride + y * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = ((sw - 1 - y) & ~1);

          u = s[x * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(x + 1) * src_stride + even_y * bpp + u_offset] + u) >> 1;
          v = s[x * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(x + 1) * src_stride + even_y * bpp + v_offset] + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[x * src_stride + (sw - 1 - y) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(x + 1) * src_stride + (sw - 1 - y) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = ((sw - 1 - x) & ~1);

          u = (s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - y) * src_stride + (sw - 1 - x) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - y) * src_stride + (sw - 1 - (x + 1)) * bpp +
                y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = ((sw - 1 - x) & ~1);

          u = (s[y * src_stride + even_x * bpp + u_offset] +
              s[y * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[y * src_stride + even_x * bpp + v_offset] +
              s[y * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[y * src_stride + (sw - 1 - x) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[y * src_stride + (sw - 1 - (x + 1)) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = (x & ~1);

          u = (s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - y) * src_stride + x * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - y) * src_stride + (x + 1) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_TRANS:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = (y & ~1);

          u = s[x * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(x + 1) * src_stride + even_y * bpp + u_offset] + u) >> 1;
          v = s[x * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(x + 1) * src_stride + even_y * bpp + v_offset] + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[x * src_stride + y * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(x + 1) * src_stride + y * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_OTHER:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = ((sw - 1 - y) & ~1);

          u = s[(sh - 1 - x) * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + u_offset]
                + u) >> 1;
          v = s[(sh - 1 - x) * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + v_offset]
                + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - x) * src_stride + (sw - 1 - y) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - (x + 1)) * src_stride + (sw - 1 - y) * bpp +
                y_offset];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


static gboolean
gst_video_flip_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (vfilter);
  gboolean ret = FALSE;

  vf->process = NULL;

  if (GST_VIDEO_INFO_FORMAT (in_info) != GST_VIDEO_INFO_FORMAT (out_info))
    goto invalid_caps;

  /* Check that they are correct */
  switch (vf->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
    case GST_VIDEO_FLIP_METHOD_90L:
    case GST_VIDEO_FLIP_METHOD_TRANS:
    case GST_VIDEO_FLIP_METHOD_OTHER:
      if ((in_info->width != out_info->height) ||
          (in_info->height != out_info->width)) {
        GST_ERROR_OBJECT (vf, "we are inverting width and height but caps "
            "are not correct : %dx%d to %dx%d", in_info->width,
            in_info->height, out_info->width, out_info->height);
        goto beach;
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:

      break;
    case GST_VIDEO_FLIP_METHOD_180:
    case GST_VIDEO_FLIP_METHOD_HORIZ:
    case GST_VIDEO_FLIP_METHOD_VERT:
      if ((in_info->width != out_info->width) ||
          (in_info->height != out_info->height)) {
        GST_ERROR_OBJECT (vf, "we are keeping width and height but caps "
            "are not correct : %dx%d to %dx%d", in_info->width,
            in_info->height, out_info->width, out_info->height);
        goto beach;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  ret = TRUE;

  switch (GST_VIDEO_INFO_FORMAT (in_info)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
      vf->process = gst_video_flip_planar_yuv;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YVYU:
      vf->process = gst_video_flip_y422;
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      vf->process = gst_video_flip_packed_simple;
      break;
    default:
      break;
  }

beach:
  return ret && (vf->process != NULL);

invalid_caps:
  GST_ERROR_OBJECT (vf, "Invalid caps: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
      incaps, outcaps);
  return FALSE;
}

static void
gst_video_flip_before_transform (GstBaseTransform * trans, GstBuffer * in)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (videoflip, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (videoflip), stream_time);
}

static GstFlowReturn
gst_video_flip_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (vfilter);

  if (G_UNLIKELY (videoflip->process == NULL))
    goto not_negotiated;

  GST_LOG_OBJECT (videoflip, "videoflip: flipping (%s)",
      video_flip_methods[videoflip->method].value_nick);

  GST_OBJECT_LOCK (videoflip);
  videoflip->process (videoflip, out_frame, in_frame);
  GST_OBJECT_UNLOCK (videoflip);

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_ERROR_OBJECT (videoflip, "Not negotiated yet");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_video_flip_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (trans);
  gdouble new_x, new_y, x, y;
  GstStructure *structure;
  gboolean ret;
  GstVideoInfo *out_info = &GST_VIDEO_FILTER (trans)->out_info;

  GST_DEBUG_OBJECT (vf, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &x) &&
          gst_structure_get_double (structure, "pointer_y", &y)) {
        GST_DEBUG_OBJECT (vf, "converting %fx%f", x, y);
        switch (vf->method) {
          case GST_VIDEO_FLIP_METHOD_90R:
            new_x = y;
            new_y = out_info->width - x;
            break;
          case GST_VIDEO_FLIP_METHOD_90L:
            new_x = out_info->height - y;
            new_y = x;
            break;
          case GST_VIDEO_FLIP_METHOD_OTHER:
            new_x = out_info->height - y;
            new_y = out_info->width - x;
            break;
          case GST_VIDEO_FLIP_METHOD_TRANS:
            new_x = y;
            new_y = x;
            break;
          case GST_VIDEO_FLIP_METHOD_180:
            new_x = out_info->width - x;
            new_y = out_info->height - y;
            break;
          case GST_VIDEO_FLIP_METHOD_HORIZ:
            new_x = out_info->width - x;
            new_y = y;
            break;
          case GST_VIDEO_FLIP_METHOD_VERT:
            new_x = x;
            new_y = out_info->height - y;
            break;
          default:
            new_x = x;
            new_y = y;
            break;
        }
        GST_DEBUG_OBJECT (vf, "to %fx%f", new_x, new_y);
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, new_x,
            "pointer_y", G_TYPE_DOUBLE, new_y, NULL);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static void
gst_video_flip_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  switch (prop_id) {
    case PROP_METHOD:
    {
      GstVideoFlipMethod method;

      method = g_value_get_enum (value);
      GST_OBJECT_LOCK (videoflip);
      if (method != videoflip->method) {
        GstBaseTransform *btrans = GST_BASE_TRANSFORM (videoflip);

        GST_DEBUG_OBJECT (videoflip, "Changing method from %s to %s",
            video_flip_methods[videoflip->method].value_nick,
            video_flip_methods[method].value_nick);

        videoflip->method = method;
        GST_OBJECT_UNLOCK (videoflip);

        gst_base_transform_set_passthrough (btrans,
            method == GST_VIDEO_FLIP_METHOD_IDENTITY);
        gst_base_transform_reconfigure_src (btrans);
      } else {
        GST_OBJECT_UNLOCK (videoflip);
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_flip_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, videoflip->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_flip_class_init (GstVideoFlipClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *vfilter_class = (GstVideoFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (video_flip_debug, "videoflip", 0, "videoflip");

  gobject_class->set_property = gst_video_flip_set_property;
  gobject_class->get_property = gst_video_flip_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEO_FLIP_METHOD, PROP_METHOD_DEFAULT,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class, "Video flipper",
      "Filter/Effect/Video",
      "Flips and rotates video", "David Schleef <ds@schleef.org>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_flip_sink_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_video_flip_src_template));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_flip_transform_caps);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_flip_before_transform);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_flip_src_event);

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_flip_set_info);
  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_flip_transform_frame);
}

static void
gst_video_flip_init (GstVideoFlip * videoflip)
{
  videoflip->method = PROP_METHOD_DEFAULT;
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (videoflip), TRUE);
}
