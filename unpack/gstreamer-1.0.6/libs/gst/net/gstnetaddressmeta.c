/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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
 * SECTION:gstnetaddressmeta
 * @short_description: Network address metadata
 *
 * #GstNetAddress can be used to store a network address. #GstNetAddressMeta can
 * be used to store a network address in a #GstBuffer so that it network
 * elements can track the to and from address of the buffer.
 *
 * Last reviewed on 2011-11-03 (0.11.2)
 */

#include <string.h>

#include "gstnetaddressmeta.h"

static gboolean
net_address_meta_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstNetAddressMeta *nmeta = (GstNetAddressMeta *) meta;

  nmeta->addr = NULL;

  return TRUE;
}

static gboolean
net_address_meta_transform (GstBuffer * transbuf, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstNetAddressMeta *nmeta = (GstNetAddressMeta *) meta;

  /* we always copy no matter what transform */
  gst_buffer_add_net_address_meta (transbuf, nmeta->addr);

  return TRUE;
}

static void
net_address_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstNetAddressMeta *nmeta = (GstNetAddressMeta *) meta;

  if (nmeta->addr)
    g_object_unref (nmeta->addr);
  nmeta->addr = NULL;
}

GType
gst_net_address_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "origin", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstNetAddressMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_net_address_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *mi = gst_meta_register (GST_NET_ADDRESS_META_API_TYPE,
        "GstNetAddressMeta",
        sizeof (GstNetAddressMeta),
        net_address_meta_init,
        net_address_meta_free, net_address_meta_transform);
    g_once_init_leave (&meta_info, mi);
  }
  return meta_info;
}

GstNetAddressMeta *
gst_buffer_add_net_address_meta (GstBuffer * buffer, GSocketAddress * addr)
{
  GstNetAddressMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (addr), NULL);

  meta =
      (GstNetAddressMeta *) gst_buffer_add_meta (buffer,
      GST_NET_ADDRESS_META_INFO, NULL);

  meta->addr = g_object_ref (addr);

  return meta;
}
