/* GStreamer
 * Copyright (C) 2011 Axis Communications <dev-gstreamer@axis.com>
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

#ifndef __GST_CURL_FILE_SINK__
#define __GST_CURL_FILE_SINK__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <curl/curl.h>
#include "gstcurlbasesink.h"

G_BEGIN_DECLS
#define GST_TYPE_CURL_FILE_SINK \
  (gst_curl_file_sink_get_type())
#define GST_CURL_FILE_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_CURL_FILE_SINK, GstCurlFileSink))
#define GST_CURL_FILE_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_CURL_FILE_SINK, GstCurlFileSinkClass))
#define GST_IS_CURL_FILE_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_CURL_FILE_SINK))
#define GST_IS_CURL_FILE_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_CURL_FILE_SINK))
typedef struct _GstCurlFileSink GstCurlFileSink;
typedef struct _GstCurlFileSinkClass GstCurlFileSinkClass;

struct _GstCurlFileSink
{
  GstCurlBaseSink parent;

  /*< private > */
  gboolean create_dirs;
};

struct _GstCurlFileSinkClass
{
  GstCurlBaseSinkClass parent_class;
};

GType gst_curl_file_sink_get_type (void);

G_END_DECLS
#endif
