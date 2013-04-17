/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2006> Julien Moutte <julien@moutte.net>
 * Copyright (C) <2006> Zeeshan Ali <zeeshan.ali@nokia.com>
 * Copyright (C) <2006-2008> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2009> Young-Ho Cha <ganadist@gmail.com>
 * Copyright (C) <2011> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:element-textoverlay
 * @see_also: #GstTextRender, #GstTextOverlay, #GstTimeOverlay, #GstSubParse
 *
 * This plugin renders text on top of a video stream. This can be either
 * static text or text from buffers received on the text sink pad, e.g.
 * as produced by the subparse element. If the text sink pad is not linked,
 * the text set via the "text" property will be rendered. If the text sink
 * pad is linked, text will be rendered as it is received on that pad,
 * honouring and matching the buffer timestamps of both input streams.
 *
 * The text can contain newline characters and text wrapping is enabled by
 * default.
 *
 * <refsect2>
 * <title>Example launch lines</title>
 * |[
 * gst-launch -v videotestsrc ! textoverlay text="Room A" valign=top halign=left ! xvimagesink
 * ]| Here is a simple pipeline that displays a static text in the top left
 * corner of the video picture
 * |[
 * gst-launch -v filesrc location=subtitles.srt ! subparse ! txt.   videotestsrc ! timeoverlay ! textoverlay name=txt shaded-background=yes ! xvimagesink
 * ]| Here is another pipeline that displays subtitles from an .srt subtitle
 * file, centered at the bottom of the picture and with a rectangular shading
 * around the text in the background:
 * <para>
 * If you do not have such a subtitle file, create one looking like this
 * in a text editor:
 * |[
 * 1
 * 00:00:03,000 --> 00:00:05,000
 * Hello? (3-5s)
 *
 * 2
 * 00:00:08,000 --> 00:00:13,000
 * Yes, this is a subtitle. Don&apos;t
 * you like it? (8-13s)
 *
 * 3
 * 00:00:18,826 --> 00:01:02,886
 * Uh? What are you talking about?
 * I don&apos;t understand  (18-62s)
 * ]|
 * One can also feed arbitrary live text into the element:
 * |[
 * gst-launch fdsrc fd=0 ! text/x-raw,format=utf8 ! txt. videotestsrc ! \
 * textoverlay  name=txt shaded-background=yes font-desc="Serif 40" wait-text=false ! \
 * xvimagesink
 * ]| This shows new text as entered on the terminal (stdin). This is not suited
 * for subtitles as the test overlay is not timed. Subtitles should use
 * timestamped formats. For the above use case one can also read the text from
 * the application as set the #GstTextOverlay:text property.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gsttextoverlay.h>

static GstStaticPadTemplate text_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("text_sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("text/x-raw, format = { pango-markup, utf8 }")
    );

G_DEFINE_TYPE (GstTextOverlay, gst_text_overlay, GST_TYPE_BASE_TEXT_OVERLAY);

static void
gst_text_overlay_class_init (GstTextOverlayClass * klass)
{
  GstElementClass *element_class = (GstElementClass *) klass;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&text_sink_template_factory));

  gst_element_class_set_static_metadata (element_class, "Text overlay",
      "Filter/Editor/Video",
      "Adds text strings on top of a video buffer",
      "David Schleef <ds@schleef.org>, " "Zeeshan Ali <zeeshan.ali@nokia.com>");
}

static void
gst_text_overlay_init (GstTextOverlay * overlay)
{
}
