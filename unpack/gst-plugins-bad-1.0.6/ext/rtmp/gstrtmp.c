/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2002 Kristian Rietveld <kris@gtk.org>
 *                    2002,2003 Colin Walters <walters@gnu.org>
 *                    2001,2010 Bastien Nocera <hadess@hadess.net>
 *                    2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *                    2010 Jan Schmidt <thaytan@noraisin.net>
 *
 * rtmpsrc.c:
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

#include "gstrtmpsrc.h"
#include "gstrtmpsink.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ret;

  ret = gst_element_register (plugin, "rtmpsrc", GST_RANK_PRIMARY,
      GST_TYPE_RTMP_SRC);
  ret &= gst_element_register (plugin, "rtmpsink", GST_RANK_PRIMARY,
      GST_TYPE_RTMP_SINK);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rtmp,
    "RTMP source and sink",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
