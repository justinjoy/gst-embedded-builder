/* GStreamer
 *
 * jifmux: JPEG interchange format muxer
 *
 * Copyright (C) 2010 Stefan Kost <stefan.kost@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-jifmux
 * @short_description: JPEG interchange format writer
 *
 * Writes a JPEG image as JPEG/EXIF or JPEG/JFIF including various metadata. The
 * jpeg image received on the sink pad should be minimal (e.g. should not
 * contain metadata already).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc num-buffers=1 ! jpegenc ! jifmux ! filesink location=...
 * ]|
 * The above pipeline renders a frame, encodes to jpeg, adds metadata and writes
 * it to disk.
 * </refsect2>
 */
/*
jpeg interchange format:
file header : SOI, APPn{JFIF,EXIF,...}
frame header: DQT, SOF
scan header : {DAC,DHT},DRI,SOS
<scan data>
file trailer: EOI

tests:
gst-launch videotestsrc num-buffers=1 ! jpegenc ! jifmux ! filesink location=test1.jpeg
gst-launch videotestsrc num-buffers=1 ! jpegenc ! taginject tags="comment=\"test image\"" ! jifmux ! filesink location=test2.jpeg
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include <gst/tag/tag.h>
#include <gst/tag/xmpwriter.h>

#include "gstjifmux.h"

static GstStaticPadTemplate gst_jif_mux_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg")
    );

static GstStaticPadTemplate gst_jif_mux_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg")
    );

GST_DEBUG_CATEGORY_STATIC (jif_mux_debug);
#define GST_CAT_DEFAULT jif_mux_debug

#define COLORSPACE_UNKNOWN         (0 << 0)
#define COLORSPACE_GRAYSCALE       (1 << 0)
#define COLORSPACE_YUV             (1 << 1)
#define COLORSPACE_RGB             (1 << 2)
#define COLORSPACE_CMYK            (1 << 3)
#define COLORSPACE_YCCK            (1 << 4)

typedef struct _GstJifMuxMarker
{
  guint8 marker;
  guint16 size;

  const guint8 *data;
  gboolean owned;
} GstJifMuxMarker;

struct _GstJifMuxPrivate
{
  GstPad *srcpad;

  /* list of GstJifMuxMarker */
  GList *markers;
  guint scan_size;
  const guint8 *scan_data;
};

static void gst_jif_mux_finalize (GObject * object);

static void gst_jif_mux_reset (GstJifMux * self);
static gboolean gst_jif_mux_sink_setcaps (GstJifMux * self, GstCaps * caps);
static gboolean gst_jif_mux_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static GstFlowReturn gst_jif_mux_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static GstStateChangeReturn gst_jif_mux_change_state (GstElement * element,
    GstStateChange transition);

#define gst_jif_mux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstJifMux, gst_jif_mux, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_SETTER, NULL);
    G_IMPLEMENT_INTERFACE (GST_TYPE_TAG_XMP_WRITER, NULL));

static void
gst_jif_mux_class_init (GstJifMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (gobject_class, sizeof (GstJifMuxPrivate));

  gobject_class->finalize = gst_jif_mux_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_jif_mux_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_jif_mux_src_pad_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_jif_mux_sink_pad_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "JPEG stream muxer",
      "Video/Formatter",
      "Remuxes JPEG images with markers and tags",
      "Arnout Vandecappelle (Essensium/Mind) <arnout@mind.be>");

  GST_DEBUG_CATEGORY_INIT (jif_mux_debug, "jifmux", 0,
      "JPEG interchange format muxer");
}

static void
gst_jif_mux_init (GstJifMux * self)
{
  GstPad *sinkpad;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_JIF_MUX,
      GstJifMuxPrivate);

  /* create the sink and src pads */
  sinkpad = gst_pad_new_from_static_template (&gst_jif_mux_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (sinkpad, GST_DEBUG_FUNCPTR (gst_jif_mux_chain));
  gst_pad_set_event_function (sinkpad,
      GST_DEBUG_FUNCPTR (gst_jif_mux_sink_event));
  gst_element_add_pad (GST_ELEMENT (self), sinkpad);

  self->priv->srcpad =
      gst_pad_new_from_static_template (&gst_jif_mux_src_pad_template, "src");
  gst_element_add_pad (GST_ELEMENT (self), self->priv->srcpad);
}

static void
gst_jif_mux_finalize (GObject * object)
{
  GstJifMux *self = GST_JIF_MUX (object);

  gst_jif_mux_reset (self);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_jif_mux_sink_setcaps (GstJifMux * self, GstCaps * caps)
{
  GstStructure *s = gst_caps_get_structure (caps, 0);
  const gchar *variant;

  /* should be {combined (default), EXIF, JFIF} */
  if ((variant = gst_structure_get_string (s, "variant")) != NULL) {
    GST_INFO_OBJECT (self, "muxing to '%s'", variant);
    /* FIXME: do we want to switch it like this or use a gobject property ? */
  }

  return gst_pad_set_caps (self->priv->srcpad, caps);
}

static gboolean
gst_jif_mux_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstJifMux *self = GST_JIF_MUX (parent);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = gst_jif_mux_sink_setcaps (self, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_TAG:{
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (self);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      gst_event_parse_tag (event, &list);

      gst_tag_setter_merge_tags (setter, list, mode);

      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static void
gst_jif_mux_marker_free (GstJifMuxMarker * m)
{
  if (m->owned)
    g_free ((gpointer) m->data);

  g_slice_free (GstJifMuxMarker, m);
}

static void
gst_jif_mux_reset (GstJifMux * self)
{
  GList *node;
  GstJifMuxMarker *m;

  for (node = self->priv->markers; node; node = g_list_next (node)) {
    m = (GstJifMuxMarker *) node->data;
    gst_jif_mux_marker_free (m);
  }
  g_list_free (self->priv->markers);
  self->priv->markers = NULL;
}

static GstJifMuxMarker *
gst_jif_mux_new_marker (guint8 marker, guint16 size, const guint8 * data,
    gboolean owned)
{
  GstJifMuxMarker *m = g_slice_new (GstJifMuxMarker);

  m->marker = marker;
  m->size = size;
  m->data = data;
  m->owned = owned;

  return m;
}

static gboolean
gst_jif_mux_parse_image (GstJifMux * self, GstBuffer * buf)
{
  GstByteReader reader;
  GstJifMuxMarker *m;
  guint8 marker = 0;
  guint16 size = 0;
  const guint8 *data = NULL;
  GstMapInfo map;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  gst_byte_reader_init (&reader, map.data, map.size);

  GST_LOG_OBJECT (self, "Received buffer of size: %" G_GSIZE_FORMAT, map.size);

  if (!gst_byte_reader_peek_uint8 (&reader, &marker))
    goto error;

  while (marker == 0xff) {
    if (!gst_byte_reader_skip (&reader, 1))
      goto error;

    if (!gst_byte_reader_get_uint8 (&reader, &marker))
      goto error;

    switch (marker) {
      case RST0:
      case RST1:
      case RST2:
      case RST3:
      case RST4:
      case RST5:
      case RST6:
      case RST7:
      case SOI:
        GST_DEBUG_OBJECT (self, "marker = %x", marker);
        m = gst_jif_mux_new_marker (marker, 0, NULL, FALSE);
        self->priv->markers = g_list_prepend (self->priv->markers, m);
        break;
      case EOI:
        GST_DEBUG_OBJECT (self, "marker = %x", marker);
        m = gst_jif_mux_new_marker (marker, 0, NULL, FALSE);
        self->priv->markers = g_list_prepend (self->priv->markers, m);
        goto done;
        break;
      default:
        if (!gst_byte_reader_get_uint16_be (&reader, &size))
          goto error;
        if (!gst_byte_reader_get_data (&reader, size - 2, &data))
          goto error;

        m = gst_jif_mux_new_marker (marker, size - 2, data, FALSE);
        self->priv->markers = g_list_prepend (self->priv->markers, m);

        GST_DEBUG_OBJECT (self, "marker = %2x, size = %u", marker, size);
        break;
    }

    if (marker == SOS) {
      gint eoi_pos = -1;
      gint i;

      /* search the last 5 bytes for the EOI marker */
      g_assert (map.size >= 5);
      for (i = 5; i >= 2; i--) {
        if (map.data[map.size - i] == 0xFF && map.data[map.size - i + 1] == EOI) {
          eoi_pos = map.size - i;
          break;
        }
      }
      if (eoi_pos == -1) {
        GST_WARNING_OBJECT (self, "Couldn't find an EOI marker");
        eoi_pos = map.size;
      }

      /* remaining size except EOI is scan data */
      self->priv->scan_size = eoi_pos - gst_byte_reader_get_pos (&reader);
      if (!gst_byte_reader_get_data (&reader, self->priv->scan_size,
              &self->priv->scan_data))
        goto error;

      GST_DEBUG_OBJECT (self, "scan data, size = %u", self->priv->scan_size);
    }

    if (!gst_byte_reader_peek_uint8 (&reader, &marker))
      goto error;
  }
  GST_INFO_OBJECT (self, "done parsing at 0x%x / 0x%x",
      gst_byte_reader_get_pos (&reader), (guint) map.size);

done:
  self->priv->markers = g_list_reverse (self->priv->markers);
  gst_buffer_unmap (buf, &map);

  return TRUE;

  /* ERRORS */
error:
  {
    GST_WARNING_OBJECT (self,
        "Error parsing image header (need more that %u bytes available)",
        gst_byte_reader_get_remaining (&reader));
    gst_buffer_unmap (buf, &map);
    return FALSE;
  }
}

static gboolean
gst_jif_mux_mangle_markers (GstJifMux * self)
{
  gboolean modified = FALSE;
  GstTagList *tags = NULL;
  gboolean cleanup_tags;
  GstJifMuxMarker *m;
  GList *node, *file_hdr = NULL, *frame_hdr = NULL, *scan_hdr = NULL;
  GList *app0_jfif = NULL, *app1_exif = NULL, *app1_xmp = NULL, *com = NULL;
  GstBuffer *xmp_data;
  gchar *str = NULL;
  gint colorspace = COLORSPACE_UNKNOWN;

  /* update the APP markers
   * - put any JFIF APP0 first
   * - the Exif APP1 next,
   * - the XMP APP1 next,
   * - the PSIR APP13 next,
   * - followed by all other marker segments
   */

  /* find some reference points where we insert before/after */
  file_hdr = self->priv->markers;
  for (node = self->priv->markers; node; node = g_list_next (node)) {
    m = (GstJifMuxMarker *) node->data;

    switch (m->marker) {
      case APP0:
        if (m->size > 5 && !memcmp (m->data, "JFIF\0", 5)) {
          GST_DEBUG_OBJECT (self, "found APP0 JFIF");
          colorspace |= COLORSPACE_GRAYSCALE | COLORSPACE_YUV;
          if (!app0_jfif)
            app0_jfif = node;
        }
        break;
      case APP1:
        if (m->size > 6 && (!memcmp (m->data, "EXIF\0\0", 6) ||
                !memcmp (m->data, "Exif\0\0", 6))) {
          GST_DEBUG_OBJECT (self, "found APP1 EXIF");
          if (!app1_exif)
            app1_exif = node;
        } else if (m->size > 29
            && !memcmp (m->data, "http://ns.adobe.com/xap/1.0/\0", 29)) {
          GST_INFO_OBJECT (self, "found APP1 XMP, will be replaced");
          if (!app1_xmp)
            app1_xmp = node;
        }
        break;
      case APP14:
        /* check if this contains RGB */
        /*
         * This marker should have:
         * - 'Adobe\0'
         * - 2 bytes DCTEncodeVersion
         * - 2 bytes flags0
         * - 2 bytes flags1
         * - 1 byte  ColorTransform
         *             - 0 means unknown (RGB or CMYK)
         *             - 1 YCbCr
         *             - 2 YCCK
         */

        if ((m->size >= 14)
            && (strncmp ((gchar *) m->data, "Adobe\0", 6) == 0)) {
          switch (m->data[11]) {
            case 0:
              colorspace |= COLORSPACE_RGB | COLORSPACE_CMYK;
              break;
            case 1:
              colorspace |= COLORSPACE_YUV;
              break;
            case 2:
              colorspace |= COLORSPACE_YCCK;
              break;
            default:
              break;
          }
        }

        break;
      case COM:
        GST_INFO_OBJECT (self, "found COM, will be replaced");
        if (!com)
          com = node;
        break;
      case DQT:
      case SOF0:
      case SOF1:
      case SOF2:
      case SOF3:
      case SOF5:
      case SOF6:
      case SOF7:
      case SOF9:
      case SOF10:
      case SOF11:
      case SOF13:
      case SOF14:
      case SOF15:
        if (!frame_hdr)
          frame_hdr = node;
        break;
      case DAC:
      case DHT:
      case DRI:
      case SOS:
        if (!scan_hdr)
          scan_hdr = node;
        break;
    }
  }

  /* if we want combined or JFIF */
  /* check if we don't have JFIF APP0 */
  if (!app0_jfif && (colorspace & (COLORSPACE_GRAYSCALE | COLORSPACE_YUV))) {
    /* build jfif header */
    static const struct
    {
      gchar id[5];
      guint8 ver[2];
      guint8 du;
      guint8 xd[2], yd[2];
      guint8 tw, th;
    } jfif_data = {
      "JFIF", {
      1, 2}, 0, {
      0, 1},                    /* FIXME: check pixel-aspect from caps */
      {
    0, 1}, 0, 0};
    m = gst_jif_mux_new_marker (APP0, sizeof (jfif_data),
        (const guint8 *) &jfif_data, FALSE);
    /* insert into self->markers list */
    self->priv->markers = g_list_insert (self->priv->markers, m, 1);
    app0_jfif = g_list_nth (self->priv->markers, 1);
  }
  /* else */
  /* remove JFIF if exists */

  /* Existing exif tags will be removed and our own will be added */
  if (!tags) {
    tags = (GstTagList *) gst_tag_setter_get_tag_list (GST_TAG_SETTER (self));
    cleanup_tags = FALSE;
  }
  if (!tags) {
    tags = gst_tag_list_new_empty ();
    cleanup_tags = TRUE;
  }

  GST_DEBUG_OBJECT (self, "Tags to be serialized %" GST_PTR_FORMAT, tags);

  /* FIXME: not happy with those
   * - else where we would use VIDEO_CODEC = "Jpeg"
   gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE,
   GST_TAG_VIDEO_CODEC, "image/jpeg", NULL);
   */

  /* Add EXIF */
  {
    GstBuffer *exif_data;
    gsize exif_size;
    guint8 *data;
    GstJifMuxMarker *m;
    GList *pos;

    /* insert into self->markers list */
    exif_data = gst_tag_list_to_exif_buffer_with_tiff_header (tags);
    exif_size = exif_data ? gst_buffer_get_size (exif_data) : 0;

    if (exif_data && exif_size + 8 >= G_GUINT64_CONSTANT (65536)) {
      GST_WARNING_OBJECT (self, "Exif tags data size exceed maximum size");
      gst_buffer_unref (exif_data);
      exif_data = NULL;
    }
    if (exif_data) {
      data = g_malloc0 (exif_size + 6);
      memcpy (data, "Exif", 4);
      gst_buffer_extract (exif_data, 0, data + 6, exif_size);
      m = gst_jif_mux_new_marker (APP1, exif_size + 6, data, TRUE);
      gst_buffer_unref (exif_data);

      if (app1_exif) {
        gst_jif_mux_marker_free ((GstJifMuxMarker *) app1_exif->data);
        app1_exif->data = m;
      } else {
        pos = file_hdr;
        if (app0_jfif)
          pos = app0_jfif;
        pos = g_list_next (pos);

        self->priv->markers =
            g_list_insert_before (self->priv->markers, pos, m);
        if (pos) {
          app1_exif = g_list_previous (pos);
        } else {
          app1_exif = g_list_last (self->priv->markers);
        }
      }
      modified = TRUE;
    }
  }

  /* add xmp */
  xmp_data =
      gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER (self),
      tags, FALSE);
  if (xmp_data) {
    guint8 *data;
    gsize size;
    GList *pos;

    size = gst_buffer_get_size (xmp_data);
    data = g_malloc (size + 29);
    memcpy (data, "http://ns.adobe.com/xap/1.0/\0", 29);
    gst_buffer_extract (xmp_data, 0, &data[29], size);
    m = gst_jif_mux_new_marker (APP1, size + 29, data, TRUE);

    /*
     * Replace the old xmp marker and not add a new one.
     * There shouldn't be a xmp packet in the input, but it is better
     * to be safe than add another one and end up with 2 packets.
     */
    if (app1_xmp) {
      gst_jif_mux_marker_free ((GstJifMuxMarker *) app1_xmp->data);
      app1_xmp->data = m;
    } else {

      pos = file_hdr;
      if (app1_exif)
        pos = app1_exif;
      else if (app0_jfif)
        pos = app0_jfif;
      pos = g_list_next (pos);

      self->priv->markers = g_list_insert_before (self->priv->markers, pos, m);

    }
    gst_buffer_unref (xmp_data);
    modified = TRUE;
  }

  /* add jpeg comment from any of those */
  (void) (gst_tag_list_get_string (tags, GST_TAG_COMMENT, &str) ||
      gst_tag_list_get_string (tags, GST_TAG_DESCRIPTION, &str) ||
      gst_tag_list_get_string (tags, GST_TAG_TITLE, &str));

  if (str) {
    GST_DEBUG_OBJECT (self, "set COM marker to '%s'", str);
    /* insert new marker into self->markers list */
    m = gst_jif_mux_new_marker (COM, strlen (str) + 1, (const guint8 *) str,
        TRUE);
    /* FIXME: if we have one already, replace */
    /* this should go before SOS, maybe at the end of file-header */
    self->priv->markers = g_list_insert_before (self->priv->markers,
        frame_hdr, m);

    modified = TRUE;
  }

  if (tags && cleanup_tags)
    gst_tag_list_unref (tags);
  return modified;
}

static GstFlowReturn
gst_jif_mux_recombine_image (GstJifMux * self, GstBuffer ** new_buf,
    GstBuffer * old_buf)
{
  GstBuffer *buf;
  GstByteWriter *writer;
  GstJifMuxMarker *m;
  GList *node;
  guint size = self->priv->scan_size;
  gboolean writer_status = TRUE;
  GstMapInfo map;

  /* iterate list and collect size */
  for (node = self->priv->markers; node; node = g_list_next (node)) {
    m = (GstJifMuxMarker *) node->data;

    /* some markers like e.g. SOI are empty */
    if (m->size) {
      size += 2 + m->size;
    }
    /* 0xff <marker> */
    size += 2;
  }
  GST_INFO_OBJECT (self, "old size: %" G_GSIZE_FORMAT ", new size: %u",
      gst_buffer_get_size (old_buf), size);

  /* allocate new buffer */
  buf = gst_buffer_new_allocate (NULL, size, NULL);

  /* copy buffer metadata */
  gst_buffer_copy_into (buf, old_buf,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  /* memcopy markers */
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  writer = gst_byte_writer_new_with_data (map.data, map.size, TRUE);

  for (node = self->priv->markers; node && writer_status;
      node = g_list_next (node)) {
    m = (GstJifMuxMarker *) node->data;

    writer_status &= gst_byte_writer_put_uint8 (writer, 0xff);
    writer_status &= gst_byte_writer_put_uint8 (writer, m->marker);

    GST_DEBUG_OBJECT (self, "marker = %2x, size = %u", m->marker, m->size + 2);

    if (m->size) {
      writer_status &= gst_byte_writer_put_uint16_be (writer, m->size + 2);
      writer_status &= gst_byte_writer_put_data (writer, m->data, m->size);
    }

    if (m->marker == SOS) {
      GST_DEBUG_OBJECT (self, "scan data, size = %u", self->priv->scan_size);
      writer_status &=
          gst_byte_writer_put_data (writer, self->priv->scan_data,
          self->priv->scan_size);
    }
  }
  gst_buffer_unmap (buf, &map);
  gst_byte_writer_free (writer);

  if (!writer_status) {
    GST_WARNING_OBJECT (self, "Failed to write to buffer, calculated size "
        "was probably too short");
    g_assert_not_reached ();
  }

  *new_buf = buf;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_jif_mux_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstJifMux *self = GST_JIF_MUX (parent);
  GstFlowReturn fret = GST_FLOW_OK;

#if 0
  GST_MEMDUMP ("jpeg beg", GST_BUFFER_DATA (buf), 64);
  GST_MEMDUMP ("jpeg end", GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf) - 64,
      64);
#endif

  /* we should have received a whole picture from SOI to EOI
   * build a list of markers */
  if (gst_jif_mux_parse_image (self, buf)) {
    /* modify marker list */
    if (gst_jif_mux_mangle_markers (self)) {
      /* the list was changed, remux */
      GstBuffer *old = buf;
      fret = gst_jif_mux_recombine_image (self, &buf, old);
      gst_buffer_unref (old);
    }
  }

  /* free the marker list */
  gst_jif_mux_reset (self);

  if (fret == GST_FLOW_OK) {
    fret = gst_pad_push (self->priv->srcpad, buf);
  }
  return fret;
}

static GstStateChangeReturn
gst_jif_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstJifMux *self = GST_JIF_MUX_CAST (element);

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
      gst_tag_setter_reset_tags (GST_TAG_SETTER (self));
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
