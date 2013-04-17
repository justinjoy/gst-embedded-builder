/* GStreamer
 * Copyright (C) 2012 Fluendo S.A. <support@fluendo.com>
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
#include <SLES/OpenSLES.h>

#include "opensles.h"
#include "openslessink.h"
#include "openslessrc.h"

static GMutex engine_mutex;
static SLObjectItf engine_object = NULL;
static gint engine_object_refcount = 0;

SLObjectItf
gst_opensles_get_engine (void)
{
  g_mutex_lock (&engine_mutex);
  if (!engine_object) {
    SLresult result;
    result = slCreateEngine (&engine_object, 0, NULL, 0, NULL, NULL);
    if (result != SL_RESULT_SUCCESS) {
      GST_ERROR ("slCreateEngine failed(0x%08x)", (guint32) result);
      engine_object = NULL;
    }

    result = (*engine_object)->Realize (engine_object, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
      GST_ERROR ("engine.Realize failed(0x%08x)", (guint32) result);
      (*engine_object)->Destroy (engine_object);
      engine_object = NULL;
    }
  }

  if (engine_object) {
    engine_object_refcount++;
  }
  g_mutex_unlock (&engine_mutex);

  return engine_object;
}

void
gst_opensles_release_engine (SLObjectItf engine_object_parameter)
{
  g_mutex_lock (&engine_mutex);
  g_assert (engine_object == engine_object_parameter);

  if (engine_object) {
    engine_object_refcount--;

    if (engine_object_refcount == 0) {
      (*engine_object)->Destroy (engine_object);
      engine_object = NULL;
    }
  }
  g_mutex_unlock (&engine_mutex);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  g_mutex_init (&engine_mutex);

  if (!gst_element_register (plugin, "openslessink", GST_RANK_PRIMARY,
          GST_TYPE_OPENSLES_SINK)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "openslessrc", GST_RANK_PRIMARY,
          GST_TYPE_OPENSLES_SRC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opensles,
    "OpenSL ES support for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
