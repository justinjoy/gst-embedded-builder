/* ex: set tabstop=2 shiftwidth=2 expandtab: */
/* GStreamer
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
#  include "config.h"
#endif

#include <string.h>
#include <stdlib.h>

#include <gst/rtp/gstrtpbuffer.h>
#include <gst/pbutils/pbutils.h>

#include "gstrtph264pay.h"


#define IDR_TYPE_ID  5
#define SPS_TYPE_ID  7
#define PPS_TYPE_ID  8

GST_DEBUG_CATEGORY_STATIC (rtph264pay_debug);
#define GST_CAT_DEFAULT (rtph264pay_debug)

/* references:
 *
 * RFC 3984
 */

static GstStaticPadTemplate gst_rtp_h264_pay_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) avc, alignment = (string) au;"
        "video/x-h264, "
        "stream-format = (string) byte-stream, alignment = (string) { nal, au }")
    );

static GstStaticPadTemplate gst_rtp_h264_pay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"video\", "
        "payload = (int) " GST_RTP_PAYLOAD_DYNAMIC_STRING ", "
        "clock-rate = (int) 90000, " "encoding-name = (string) \"H264\"")
    );

#define DEFAULT_SPROP_PARAMETER_SETS    NULL
#define DEFAULT_CONFIG_INTERVAL		      0

enum
{
  PROP_0,
  PROP_SPROP_PARAMETER_SETS,
  PROP_CONFIG_INTERVAL,
  PROP_LAST
};

#define IS_ACCESS_UNIT(x) (((x) > 0x00) && ((x) < 0x06))

static void gst_rtp_h264_pay_finalize (GObject * object);

static void gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_rtp_h264_pay_getcaps (GstRTPBasePayload * payload,
    GstPad * pad, GstCaps * filter);
static gboolean gst_rtp_h264_pay_setcaps (GstRTPBasePayload * basepayload,
    GstCaps * caps);
static GstFlowReturn gst_rtp_h264_pay_handle_buffer (GstRTPBasePayload * pad,
    GstBuffer * buffer);
static gboolean gst_rtp_h264_pay_sink_event (GstRTPBasePayload * payload,
    GstEvent * event);
static GstStateChangeReturn gst_rtp_h264_pay_change_state (GstElement *
    element, GstStateChange transition);

#define gst_rtp_h264_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpH264Pay, gst_rtp_h264_pay, GST_TYPE_RTP_BASE_PAYLOAD);

static void
gst_rtp_h264_pay_class_init (GstRtpH264PayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gobject_class->set_property = gst_rtp_h264_pay_set_property;
  gobject_class->get_property = gst_rtp_h264_pay_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_SPROP_PARAMETER_SETS, g_param_spec_string ("sprop-parameter-sets",
          "sprop-parameter-sets",
          "The base64 sprop-parameter-sets to set in out caps (set to NULL to "
          "extract from stream)",
          DEFAULT_SPROP_PARAMETER_SETS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_CONFIG_INTERVAL,
      g_param_spec_uint ("config-interval",
          "SPS PPS Send Interval",
          "Send SPS and PPS Insertion Interval in seconds (sprop parameter sets "
          "will be multiplexed in the data stream when detected.) (0 = disabled)",
          0, 3600, DEFAULT_CONFIG_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  gobject_class->finalize = gst_rtp_h264_pay_finalize;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_h264_pay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "RTP H264 payloader",
      "Codec/Payloader/Network/RTP",
      "Payload-encode H264 video into RTP packets (RFC 3984)",
      "Laurent Glayal <spglegle@yahoo.fr>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtp_h264_pay_change_state);

  gstrtpbasepayload_class->get_caps = gst_rtp_h264_pay_getcaps;
  gstrtpbasepayload_class->set_caps = gst_rtp_h264_pay_setcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_h264_pay_handle_buffer;
  gstrtpbasepayload_class->sink_event = gst_rtp_h264_pay_sink_event;

  GST_DEBUG_CATEGORY_INIT (rtph264pay_debug, "rtph264pay", 0,
      "H264 RTP Payloader");
}

static void
gst_rtp_h264_pay_init (GstRtpH264Pay * rtph264pay)
{
  rtph264pay->queue = g_array_new (FALSE, FALSE, sizeof (guint));
  rtph264pay->profile = 0;
  rtph264pay->sps = NULL;
  rtph264pay->pps = NULL;
  rtph264pay->last_spspps = -1;
  rtph264pay->spspps_interval = DEFAULT_CONFIG_INTERVAL;

  rtph264pay->adapter = gst_adapter_new ();
}

static void
gst_rtp_h264_pay_clear_sps_pps (GstRtpH264Pay * rtph264pay)
{
  g_list_foreach (rtph264pay->sps, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtph264pay->sps);
  rtph264pay->sps = NULL;
  g_list_foreach (rtph264pay->pps, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (rtph264pay->pps);
  rtph264pay->pps = NULL;
}

static void
gst_rtp_h264_pay_finalize (GObject * object)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  g_array_free (rtph264pay->queue, TRUE);

  gst_rtp_h264_pay_clear_sps_pps (rtph264pay);

  g_free (rtph264pay->sprop_parameter_sets);

  g_object_unref (rtph264pay->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static const gchar all_levels[][4] = {
  "1",
  "1b",
  "1.1",
  "1.2",
  "1.3",
  "2",
  "2.1",
  "2.2",
  "3",
  "3.1",
  "3.2",
  "4",
  "4.1",
  "4.2",
  "5",
  "5.1"
};

static GstCaps *
gst_rtp_h264_pay_getcaps (GstRTPBasePayload * payload, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *template_caps;
  GstCaps *allowed_caps;
  GstCaps *caps, *icaps;
  gboolean append_unrestricted;
  guint i;

  allowed_caps =
      gst_pad_peer_query_caps (GST_RTP_BASE_PAYLOAD_SRCPAD (payload), NULL);

  if (allowed_caps == NULL)
    return NULL;

  template_caps =
      gst_static_pad_template_get_caps (&gst_rtp_h264_pay_sink_template);

  if (gst_caps_is_any (allowed_caps)) {
    caps = gst_caps_ref (template_caps);
    goto done;
  }

  if (gst_caps_is_empty (allowed_caps)) {
    caps = gst_caps_ref (allowed_caps);
    goto done;
  }

  caps = gst_caps_new_empty ();

  append_unrestricted = FALSE;
  for (i = 0; i < gst_caps_get_size (allowed_caps); i++) {
    GstStructure *s = gst_caps_get_structure (allowed_caps, i);
    GstStructure *new_s = gst_structure_new_empty ("video/x-h264");
    const gchar *profile_level_id;

    profile_level_id = gst_structure_get_string (s, "profile-level-id");

    if (profile_level_id && strlen (profile_level_id) == 6) {
      const gchar *profile;
      const gchar *level;
      long int spsint;
      guint8 sps[3];

      spsint = strtol (profile_level_id, NULL, 16);
      sps[0] = spsint >> 16;
      sps[1] = spsint >> 8;
      sps[2] = spsint;

      profile = gst_codec_utils_h264_get_profile (sps, 3);
      level = gst_codec_utils_h264_get_level (sps, 3);

      if (profile && level) {
        GST_LOG_OBJECT (payload, "In caps, have profile %s and level %s",
            profile, level);

        if (!strcmp (profile, "constrained-baseline"))
          gst_structure_set (new_s, "profile", G_TYPE_STRING, profile, NULL);
        else {
          GValue val = { 0, };
          GValue profiles = { 0, };

          g_value_init (&profiles, GST_TYPE_LIST);
          g_value_init (&val, G_TYPE_STRING);

          g_value_set_static_string (&val, profile);
          gst_value_list_append_value (&profiles, &val);

          g_value_set_static_string (&val, "constrained-baseline");
          gst_value_list_append_value (&profiles, &val);

          gst_structure_take_value (new_s, "profile", &profiles);
        }

        if (!strcmp (level, "1"))
          gst_structure_set (new_s, "level", G_TYPE_STRING, level, NULL);
        else {
          GValue levels = { 0, };
          GValue val = { 0, };
          int j;

          g_value_init (&levels, GST_TYPE_LIST);
          g_value_init (&val, G_TYPE_STRING);

          for (j = 0; j < G_N_ELEMENTS (all_levels); j++) {
            g_value_set_static_string (&val, all_levels[j]);
            gst_value_list_prepend_value (&levels, &val);
            if (!strcmp (level, all_levels[j]))
              break;
          }
          gst_structure_take_value (new_s, "level", &levels);
        }
      } else {
        /* Invalid profile-level-id means baseline */

        gst_structure_set (new_s,
            "profile", G_TYPE_STRING, "constrained-baseline", NULL);
      }
    } else {
      /* No profile-level-id means baseline or unrestricted */

      gst_structure_set (new_s,
          "profile", G_TYPE_STRING, "constrained-baseline", NULL);
      append_unrestricted = TRUE;
    }

    caps = gst_caps_merge_structure (caps, new_s);
  }

  if (append_unrestricted) {
    caps =
        gst_caps_merge_structure (caps, gst_structure_new ("video/x-h264", NULL,
            NULL));
  }

  icaps = gst_caps_intersect (caps, template_caps);
  gst_caps_unref (caps);
  caps = icaps;

done:

  gst_caps_unref (template_caps);
  gst_caps_unref (allowed_caps);

  GST_LOG_OBJECT (payload, "returning caps %" GST_PTR_FORMAT, caps);
  return caps;
}

/* take the currently configured SPS and PPS lists and set them on the caps as
 * sprop-parameter-sets */
static gboolean
gst_rtp_h264_pay_set_sps_pps (GstRTPBasePayload * basepayload)
{
  GstRtpH264Pay *payloader = GST_RTP_H264_PAY (basepayload);
  gchar *profile;
  gchar *set;
  GList *walk;
  GString *sprops;
  guint count;
  gboolean res;
  GstMapInfo map;

  sprops = g_string_new ("");
  count = 0;

  /* build the sprop-parameter-sets */
  for (walk = payloader->sps; walk; walk = g_list_next (walk)) {
    GstBuffer *sps_buf = GST_BUFFER_CAST (walk->data);

    gst_buffer_map (sps_buf, &map, GST_MAP_READ);
    set = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (sps_buf, &map);

    g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
    g_free (set);
    count++;
  }
  for (walk = payloader->pps; walk; walk = g_list_next (walk)) {
    GstBuffer *pps_buf = GST_BUFFER_CAST (walk->data);

    gst_buffer_map (pps_buf, &map, GST_MAP_READ);
    set = g_base64_encode (map.data, map.size);
    gst_buffer_unmap (pps_buf, &map);

    g_string_append_printf (sprops, "%s%s", count ? "," : "", set);
    g_free (set);
    count++;
  }

  if (G_LIKELY (count)) {
    /* profile is 24 bit. Force it to respect the limit */
    profile = g_strdup_printf ("%06x", payloader->profile & 0xffffff);
    /* combine into output caps */
    res = gst_rtp_base_payload_set_outcaps (basepayload,
        "sprop-parameter-sets", G_TYPE_STRING, sprops->str, NULL);
    g_free (profile);
  } else {
    res = gst_rtp_base_payload_set_outcaps (basepayload, NULL);
  }
  g_string_free (sprops, TRUE);

  return res;
}

static GList *
add_sps_pps_without_duplicates (GList * list, GstBuffer * buffer)
{
  GList *walk;
  GstMapInfo map;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  for (walk = list; walk; walk = walk->next) {
    if (gst_buffer_get_size (walk->data) == map.size &&
        !gst_buffer_memcmp (walk->data, 0, map.data, map.size)) {
      /* Same data */
      gst_buffer_unmap (buffer, &map);
      gst_buffer_unref (buffer);
      return list;
    }
  }

  gst_buffer_unmap (buffer, &map);
  list = g_list_append (list, buffer);

  return list;
}

static gboolean
gst_rtp_h264_pay_setcaps (GstRTPBasePayload * basepayload, GstCaps * caps)
{
  GstRtpH264Pay *rtph264pay;
  GstStructure *str;
  const GValue *value;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  GstBuffer *buffer;
  const gchar *alignment, *stream_format;

  rtph264pay = GST_RTP_H264_PAY (basepayload);

  str = gst_caps_get_structure (caps, 0);

  /* we can only set the output caps when we found the sprops and profile
   * NALs */
  gst_rtp_base_payload_set_options (basepayload, "video", TRUE, "H264", 90000);

  rtph264pay->alignment = GST_H264_ALIGNMENT_UNKNOWN;
  alignment = gst_structure_get_string (str, "alignment");
  if (alignment) {
    if (g_str_equal (alignment, "au"))
      rtph264pay->alignment = GST_H264_ALIGNMENT_AU;
    if (g_str_equal (alignment, "nal"))
      rtph264pay->alignment = GST_H264_ALIGNMENT_NAL;
  }

  rtph264pay->stream_format = GST_H264_STREAM_FORMAT_UNKNOWN;
  stream_format = gst_structure_get_string (str, "stream-format");
  if (stream_format) {
    if (g_str_equal (stream_format, "avc"))
      rtph264pay->stream_format = GST_H264_STREAM_FORMAT_AVC;
    if (g_str_equal (stream_format, "byte-stream"))
      rtph264pay->stream_format = GST_H264_STREAM_FORMAT_BYTESTREAM;
  }

  /* packetized AVC video has a codec_data */
  if ((value = gst_structure_get_value (str, "codec_data"))) {
    guint num_sps, num_pps;
    gint i, nal_size;

    GST_DEBUG_OBJECT (rtph264pay, "have packetized h264");

    buffer = gst_value_get_buffer (value);

    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    /* parse the avcC data */
    if (size < 7)
      goto avcc_too_small;
    /* parse the version, this must be 1 */
    if (data[0] != 1)
      goto wrong_version;

    /* AVCProfileIndication */
    /* profile_compat */
    /* AVCLevelIndication */
    rtph264pay->profile = (data[1] << 16) | (data[2] << 8) | data[3];
    GST_DEBUG_OBJECT (rtph264pay, "profile %06x", rtph264pay->profile);

    /* 6 bits reserved | 2 bits lengthSizeMinusOne */
    /* this is the number of bytes in front of the NAL units to mark their
     * length */
    rtph264pay->nal_length_size = (data[4] & 0x03) + 1;
    GST_DEBUG_OBJECT (rtph264pay, "nal length %u", rtph264pay->nal_length_size);
    /* 3 bits reserved | 5 bits numOfSequenceParameterSets */
    num_sps = data[5] & 0x1f;
    GST_DEBUG_OBJECT (rtph264pay, "num SPS %u", num_sps);

    data += 6;
    size -= 6;

    /* create the sprop-parameter-sets */
    for (i = 0; i < num_sps; i++) {
      GstBuffer *sps_buf;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      GST_LOG_OBJECT (rtph264pay, "SPS %d size %d", i, nal_size);

      if (size < nal_size)
        goto avcc_error;

      /* make a buffer out of it and add to SPS list */
      sps_buf = gst_buffer_new_and_alloc (nal_size);
      gst_buffer_fill (sps_buf, 0, data, nal_size);
      rtph264pay->sps = add_sps_pps_without_duplicates (rtph264pay->sps,
          sps_buf);

      data += nal_size;
      size -= nal_size;
    }
    if (size < 1)
      goto avcc_error;

    /* 8 bits numOfPictureParameterSets */
    num_pps = data[0];
    data += 1;
    size -= 1;

    GST_DEBUG_OBJECT (rtph264pay, "num PPS %u", num_pps);
    for (i = 0; i < num_pps; i++) {
      GstBuffer *pps_buf;

      if (size < 2)
        goto avcc_error;

      nal_size = (data[0] << 8) | data[1];
      data += 2;
      size -= 2;

      GST_LOG_OBJECT (rtph264pay, "PPS %d size %d", i, nal_size);

      if (size < nal_size)
        goto avcc_error;

      /* make a buffer out of it and add to PPS list */
      pps_buf = gst_buffer_new_and_alloc (nal_size);
      gst_buffer_fill (pps_buf, 0, data, nal_size);
      rtph264pay->pps = add_sps_pps_without_duplicates (rtph264pay->pps,
          pps_buf);

      data += nal_size;
      size -= nal_size;
    }
    gst_buffer_unmap (buffer, &map);
    /* and update the caps with the collected data */
    if (!gst_rtp_h264_pay_set_sps_pps (basepayload))
      goto set_sps_pps_failed;
  } else {
    GST_DEBUG_OBJECT (rtph264pay, "have bytestream h264");
  }

  return TRUE;

avcc_too_small:
  {
    GST_ERROR_OBJECT (rtph264pay, "avcC size %" G_GSIZE_FORMAT " < 7", size);
    goto error;
  }
wrong_version:
  {
    GST_ERROR_OBJECT (rtph264pay, "wrong avcC version");
    goto error;
  }
avcc_error:
  {
    GST_ERROR_OBJECT (rtph264pay, "avcC too small ");
    goto error;
  }
set_sps_pps_failed:
  {
    GST_ERROR_OBJECT (rtph264pay, "failed to set sps/pps");
    goto error;
  }
error:
  {
    gst_buffer_unmap (buffer, &map);
    return FALSE;
  }
}

static void
gst_rtp_h264_pay_parse_sprop_parameter_sets (GstRtpH264Pay * rtph264pay)
{
  const gchar *ps;
  gchar **params;
  guint len, num_sps, num_pps;
  gint i;
  GstBuffer *buf;

  ps = rtph264pay->sprop_parameter_sets;
  if (ps == NULL)
    return;

  gst_rtp_h264_pay_clear_sps_pps (rtph264pay);

  params = g_strsplit (ps, ",", 0);
  len = g_strv_length (params);

  GST_DEBUG_OBJECT (rtph264pay, "we have %d params", len);

  num_sps = num_pps = 0;

  for (i = 0; params[i]; i++) {
    gsize nal_len;
    GstMapInfo map;
    guint8 *nalp;
    guint save = 0;
    gint state = 0;
    guint8 nal_type;

    nal_len = strlen (params[i]);
    buf = gst_buffer_new_and_alloc (nal_len);

    gst_buffer_map (buf, &map, GST_MAP_WRITE);
    nalp = map.data;
    nal_len = g_base64_decode_step (params[i], nal_len, nalp, &state, &save);
    nal_type = nalp[0];
    gst_buffer_unmap (buf, &map);
    gst_buffer_resize (buf, 0, nal_len);

    if (!nal_len) {
      gst_buffer_unref (buf);
      continue;
    }

    /* append to the right list */
    if ((nal_type & 0x1f) == 7) {
      GST_DEBUG_OBJECT (rtph264pay, "adding param %d as SPS %d", i, num_sps);
      rtph264pay->sps = add_sps_pps_without_duplicates (rtph264pay->sps, buf);
      num_sps++;
    } else {
      GST_DEBUG_OBJECT (rtph264pay, "adding param %d as PPS %d", i, num_pps);
      rtph264pay->pps = add_sps_pps_without_duplicates (rtph264pay->pps, buf);
      num_pps++;
    }
  }
  g_strfreev (params);
}

static guint
next_start_code (const guint8 * data, guint size)
{
  /* Boyer-Moore string matching algorithm, in a degenerative
   * sense because our search 'alphabet' is binary - 0 & 1 only.
   * This allow us to simplify the general BM algorithm to a very
   * simple form. */
  /* assume 1 is in the 3th byte */
  guint offset = 2;

  while (offset < size) {
    if (1 == data[offset]) {
      unsigned int shift = offset;

      if (0 == data[--shift]) {
        if (0 == data[--shift]) {
          return shift;
        }
      }
      /* The jump is always 3 because of the 1 previously matched.
       * All the 0's must be after this '1' matched at offset */
      offset += 3;
    } else if (0 == data[offset]) {
      /* maybe next byte is 1? */
      offset++;
    } else {
      /* can jump 3 bytes forward */
      offset += 3;
    }
    /* at each iteration, we rescan in a backward manner until
     * we match 0.0.1 in reverse order. Since our search string
     * has only 2 'alpabets' (i.e. 0 & 1), we know that any
     * mismatch will force us to shift a fixed number of steps */
  }
  GST_DEBUG ("Cannot find next NAL start code. returning %u", size);

  return size;
}

static gboolean
gst_rtp_h264_pay_decode_nal (GstRtpH264Pay * payloader,
    const guint8 * data, guint size, GstClockTime dts, GstClockTime pts)
{
  const guint8 *sps = NULL, *pps = NULL;
  guint sps_len = 0, pps_len = 0;
  guint8 header, type;
  guint len;
  gboolean updated;

  /* default is no update */
  updated = FALSE;

  GST_DEBUG ("NAL payload len=%u", size);

  len = size;
  header = data[0];
  type = header & 0x1f;

  /* keep sps & pps separately so that we can update either one 
   * independently. We also record the timestamp of the last SPS/PPS so 
   * that we can insert them at regular intervals and when needed. */
  if (SPS_TYPE_ID == type) {
    /* encode the entire SPS NAL in base64 */
    GST_DEBUG ("Found SPS %x %x %x Len=%u", (header >> 7),
        (header >> 5) & 3, type, len);

    sps = data;
    sps_len = len;
    /* remember when we last saw SPS */
    if (pts != -1)
      payloader->last_spspps = pts;
  } else if (PPS_TYPE_ID == type) {
    /* encoder the entire PPS NAL in base64 */
    GST_DEBUG ("Found PPS %x %x %x Len = %u",
        (header >> 7), (header >> 5) & 3, type, len);

    pps = data;
    pps_len = len;
    /* remember when we last saw PPS */
    if (pts != -1)
      payloader->last_spspps = pts;
  } else {
    GST_DEBUG ("NAL: %x %x %x Len = %u", (header >> 7),
        (header >> 5) & 3, type, len);
  }

  /* If we encountered an SPS and/or a PPS, check if it's the
   * same as the one we have. If not, update our version and
   * set updated to TRUE
   */
  if (sps_len > 0) {
    GstBuffer *sps_buf;

    if (payloader->sps != NULL) {
      sps_buf = GST_BUFFER_CAST (payloader->sps->data);

      if (gst_buffer_memcmp (sps_buf, 0, sps, sps_len)) {
        /* something changed, update */
        payloader->profile = (sps[1] << 16) + (sps[2] << 8) + sps[3];
        GST_DEBUG ("Profile level IDC = %06x", payloader->profile);
        updated = TRUE;
      }
    } else {
      /* no previous SPS, update */
      updated = TRUE;
    }

    if (updated) {
      sps_buf = gst_buffer_new_and_alloc (sps_len);
      gst_buffer_fill (sps_buf, 0, sps, sps_len);

      if (payloader->sps) {
        /* replace old buffer */
        gst_buffer_unref (payloader->sps->data);
        payloader->sps->data = sps_buf;
      } else {
        /* add new buffer */
        payloader->sps = g_list_prepend (payloader->sps, sps_buf);
      }
    }
  }

  if (pps_len > 0) {
    GstBuffer *pps_buf;

    if (payloader->pps != NULL) {
      pps_buf = GST_BUFFER_CAST (payloader->pps->data);

      if (gst_buffer_memcmp (pps_buf, 0, pps, pps_len)) {
        /* something changed, update */
        updated = TRUE;
      }
    } else {
      /* no previous SPS, update */
      updated = TRUE;
    }

    if (updated) {
      pps_buf = gst_buffer_new_and_alloc (pps_len);
      gst_buffer_fill (pps_buf, 0, pps, pps_len);

      if (payloader->pps) {
        /* replace old buffer */
        gst_buffer_unref (payloader->pps->data);
        payloader->pps->data = pps_buf;
      } else {
        /* add new buffer */
        payloader->pps = g_list_prepend (payloader->pps, pps_buf);
      }
    }
  }
  return updated;
}

static GstFlowReturn
gst_rtp_h264_pay_payload_nal (GstRTPBasePayload * basepayload,
    GstBuffer * paybuf, GstClockTime dts, GstClockTime pts, gboolean end_of_au);

static GstFlowReturn
gst_rtp_h264_pay_send_sps_pps (GstRTPBasePayload * basepayload,
    GstRtpH264Pay * rtph264pay, GstClockTime dts, GstClockTime pts)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GList *walk;

  for (walk = rtph264pay->sps; walk; walk = g_list_next (walk)) {
    GstBuffer *sps_buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (rtph264pay, "inserting SPS in the stream");
    /* resend SPS */
    ret = gst_rtp_h264_pay_payload_nal (basepayload, gst_buffer_ref (sps_buf),
        dts, pts, FALSE);
    /* Not critical here; but throw a warning */
    if (ret != GST_FLOW_OK)
      GST_WARNING ("Problem pushing SPS");
  }
  for (walk = rtph264pay->pps; walk; walk = g_list_next (walk)) {
    GstBuffer *pps_buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (rtph264pay, "inserting PPS in the stream");
    /* resend PPS */
    ret = gst_rtp_h264_pay_payload_nal (basepayload, gst_buffer_ref (pps_buf),
        dts, pts, FALSE);
    /* Not critical here; but throw a warning */
    if (ret != GST_FLOW_OK)
      GST_WARNING ("Problem pushing PPS");
  }

  if (pts != -1)
    rtph264pay->last_spspps = pts;

  return ret;
}

static GstFlowReturn
gst_rtp_h264_pay_payload_nal (GstRTPBasePayload * basepayload,
    GstBuffer * paybuf, GstClockTime dts, GstClockTime pts, gboolean end_of_au)
{
  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  guint8 nalType;
  guint packet_len, payload_len, mtu;
  GstBuffer *outbuf;
  guint8 *payload;
  GstBufferList *list = NULL;
  gboolean send_spspps;
  GstRTPBuffer rtp = { NULL };
  guint size = gst_buffer_get_size (paybuf);

  rtph264pay = GST_RTP_H264_PAY (basepayload);
  mtu = GST_RTP_BASE_PAYLOAD_MTU (rtph264pay);

  gst_buffer_extract (paybuf, 0, &nalType, 1);
  nalType &= 0x1f;

  GST_DEBUG_OBJECT (rtph264pay, "Processing Buffer with NAL TYPE=%d", nalType);

  /* should set src caps before pushing stuff,
   * and if we did not see enough SPS/PPS, that may not be the case */
  if (G_UNLIKELY (!gst_pad_has_current_caps (GST_RTP_BASE_PAYLOAD_SRCPAD
              (basepayload))))
    gst_rtp_h264_pay_set_sps_pps (basepayload);

  send_spspps = FALSE;

  /* check if we need to emit an SPS/PPS now */
  if (nalType == IDR_TYPE_ID && rtph264pay->spspps_interval > 0) {
    if (rtph264pay->last_spspps != -1) {
      guint64 diff;

      GST_LOG_OBJECT (rtph264pay,
          "now %" GST_TIME_FORMAT ", last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (pts), GST_TIME_ARGS (rtph264pay->last_spspps));

      /* calculate diff between last SPS/PPS in milliseconds */
      if (pts > rtph264pay->last_spspps)
        diff = pts - rtph264pay->last_spspps;
      else
        diff = 0;

      GST_DEBUG_OBJECT (rtph264pay,
          "interval since last SPS/PPS %" GST_TIME_FORMAT,
          GST_TIME_ARGS (diff));

      /* bigger than interval, queue SPS/PPS */
      if (GST_TIME_AS_SECONDS (diff) >= rtph264pay->spspps_interval) {
        GST_DEBUG_OBJECT (rtph264pay, "time to send SPS/PPS");
        send_spspps = TRUE;
      }
    } else {
      /* no know previous SPS/PPS time, send now */
      GST_DEBUG_OBJECT (rtph264pay, "no previous SPS/PPS time, send now");
      send_spspps = TRUE;
    }
  }

  if (send_spspps || rtph264pay->send_spspps) {
    /* we need to send SPS/PPS now first. FIXME, don't use the pts for
     * checking when we need to send SPS/PPS but convert to running_time first. */
    rtph264pay->send_spspps = FALSE;
    ret = gst_rtp_h264_pay_send_sps_pps (basepayload, rtph264pay, dts, pts);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  packet_len = gst_rtp_buffer_calc_packet_len (size, 0, 0);

  if (packet_len < mtu) {
    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit fit in one packet datasize=%d mtu=%d", size, mtu);
    /* will fit in one packet */

    /* use buffer lists
     * create buffer without payload containing only the RTP header
     * (memory block at index 0) */
    outbuf = gst_rtp_buffer_new_allocate (0, 0, 0);

    gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

    /* only set the marker bit on packets containing access units */
    if (IS_ACCESS_UNIT (nalType) && end_of_au) {
      gst_rtp_buffer_set_marker (&rtp, 1);
    }

    /* timestamp the outbuffer */
    GST_BUFFER_PTS (outbuf) = pts;
    GST_BUFFER_DTS (outbuf) = dts;

    /* insert payload memory block */
    outbuf = gst_buffer_append (outbuf, paybuf);

    list = gst_buffer_list_new ();

    /* add the buffer to the buffer list */
    gst_buffer_list_add (list, outbuf);

    gst_rtp_buffer_unmap (&rtp);

    /* push the list to the next element in the pipe */
    ret = gst_rtp_base_payload_push_list (basepayload, list);
  } else {
    /* fragmentation Units FU-A */
    guint8 nalHeader;
    guint limitedSize;
    int ii = 0, start = 1, end = 0, pos = 0;

    GST_DEBUG_OBJECT (basepayload,
        "NAL Unit DOES NOT fit in one packet datasize=%d mtu=%d", size, mtu);

    gst_buffer_extract (paybuf, 0, &nalHeader, 1);
    pos++;
    size--;

    ret = GST_FLOW_OK;

    GST_DEBUG_OBJECT (basepayload, "Using FU-A fragmentation for data size=%d",
        size);

    /* We keep 2 bytes for FU indicator and FU Header */
    payload_len = gst_rtp_buffer_calc_payload_len (mtu - 2, 0, 0);

    list = gst_buffer_list_new ();

    while (end == 0) {
      limitedSize = size < payload_len ? size : payload_len;
      GST_DEBUG_OBJECT (basepayload,
          "Inside  FU-A fragmentation limitedSize=%d iteration=%d", limitedSize,
          ii);

      /* use buffer lists
       * create buffer without payload containing only the RTP header
       * (memory block at index 0) */
      outbuf = gst_rtp_buffer_new_allocate (2, 0, 0);

      gst_rtp_buffer_map (outbuf, GST_MAP_WRITE, &rtp);

      GST_BUFFER_DTS (outbuf) = dts;
      GST_BUFFER_PTS (outbuf) = pts;
      payload = gst_rtp_buffer_get_payload (&rtp);

      if (limitedSize == size) {
        GST_DEBUG_OBJECT (basepayload, "end size=%d iteration=%d", size, ii);
        end = 1;
      }
      if (IS_ACCESS_UNIT (nalType)) {
        gst_rtp_buffer_set_marker (&rtp, end && end_of_au);
      }

      /* FU indicator */
      payload[0] = (nalHeader & 0x60) | 28;

      /* FU Header */
      payload[1] = (start << 7) | (end << 6) | (nalHeader & 0x1f);

      gst_rtp_buffer_unmap (&rtp);

      /* insert payload memory block */
      gst_buffer_append (outbuf,
          gst_buffer_copy_region (paybuf, GST_BUFFER_COPY_MEMORY, pos,
              limitedSize));

      /* add the buffer to the buffer list */
      gst_buffer_list_add (list, outbuf);


      size -= limitedSize;
      pos += limitedSize;
      ii++;
      start = 0;
    }

    ret = gst_rtp_base_payload_push_list (basepayload, list);
    gst_buffer_unref (paybuf);
  }
  return ret;
}

static GstFlowReturn
gst_rtp_h264_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpH264Pay *rtph264pay;
  GstFlowReturn ret;
  gsize size;
  guint nal_len, i;
  GstMapInfo map;
  const guint8 *data;
  GstClockTime dts, pts;
  GArray *nal_queue;
  gboolean avc;
  GstBuffer *paybuf = NULL;
  gsize skip;

  rtph264pay = GST_RTP_H264_PAY (basepayload);

  /* the input buffer contains one or more NAL units */

  avc = rtph264pay->stream_format == GST_H264_STREAM_FORMAT_AVC;

  if (avc) {
    /* In AVC mode, there is no adapter, so nothign to flush */
    if (buffer == NULL)
      return GST_FLOW_OK;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;
    pts = GST_BUFFER_PTS (buffer);
    dts = GST_BUFFER_DTS (buffer);
    GST_DEBUG_OBJECT (basepayload, "got %" G_GSIZE_FORMAT " bytes", size);
  } else {
    dts = gst_adapter_prev_dts (rtph264pay->adapter, NULL);
    pts = gst_adapter_prev_pts (rtph264pay->adapter, NULL);
    if (buffer) {
      if (!GST_CLOCK_TIME_IS_VALID (dts))
        dts = GST_BUFFER_DTS (buffer);
      if (!GST_CLOCK_TIME_IS_VALID (pts))
        pts = GST_BUFFER_PTS (buffer);

      gst_adapter_push (rtph264pay->adapter, buffer);
    }
    size = gst_adapter_available (rtph264pay->adapter);
    data = gst_adapter_map (rtph264pay->adapter, size);
    GST_DEBUG_OBJECT (basepayload,
        "got %" G_GSIZE_FORMAT " bytes (%" G_GSIZE_FORMAT ")", size,
        buffer ? gst_buffer_get_size (buffer) : 0);
  }

  ret = GST_FLOW_OK;

  /* now loop over all NAL units and put them in a packet
   * FIXME, we should really try to pack multiple NAL units into one RTP packet
   * if we can, especially for the config packets that wont't cause decoder 
   * latency. */
  if (avc) {
    guint nal_length_size;
    gsize offset = 0;

    nal_length_size = rtph264pay->nal_length_size;

    while (size > nal_length_size) {
      gint i;
      gboolean end_of_au = FALSE;

      nal_len = 0;
      for (i = 0; i < nal_length_size; i++) {
        nal_len = ((nal_len << 8) + data[i]);
      }

      /* skip the length bytes, make sure we don't run past the buffer size */
      data += nal_length_size;
      offset += nal_length_size;
      size -= nal_length_size;

      if (size >= nal_len) {
        GST_DEBUG_OBJECT (basepayload, "got NAL of size %u", nal_len);
      } else {
        nal_len = size;
        GST_DEBUG_OBJECT (basepayload, "got incomplete NAL of size %u",
            nal_len);
      }

      /* If we're at the end of the buffer, then we're at the end of the
       * access unit
       */
      if (rtph264pay->alignment == GST_H264_ALIGNMENT_AU
          && size - nal_len <= nal_length_size) {
        end_of_au = TRUE;
      }

      paybuf = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_MEMORY, offset,
          nal_len);
      ret =
          gst_rtp_h264_pay_payload_nal (basepayload, paybuf, dts, pts,
          end_of_au);
      if (ret != GST_FLOW_OK)
        break;

      data += nal_len;
      offset += nal_len;
      size -= nal_len;
    }
  } else {
    guint next;
    gboolean update = FALSE;

    /* get offset of first start code */
    next = next_start_code (data, size);

    /* skip to start code, if no start code is found, next will be size and we
     * will not collect data. */
    data += next;
    size -= next;
    nal_queue = rtph264pay->queue;
    skip = next;

    /* array must be empty when we get here */
    g_assert (nal_queue->len == 0);

    GST_DEBUG_OBJECT (basepayload,
        "found first start at %u, bytes left %" G_GSIZE_FORMAT, next, size);

    /* first pass to locate NALs and parse SPS/PPS */
    while (size > 4) {
      /* skip start code */
      data += 3;
      size -= 3;

      /* use next_start_code() to scan buffer.
       * next_start_code() returns the offset in data, 
       * starting from zero to the first byte of 0.0.0.1
       * If no start code is found, it returns the value of the 
       * 'size' parameter. 
       * data is unchanged by the call to next_start_code()
       */
      next = next_start_code (data, size);

      if (next == size && buffer != NULL) {
        /* Didn't find the start of next NAL and it's not EOS,
         * handle it next time */
        break;
      }

      /* nal length is distance to next start code */
      nal_len = next;

      GST_DEBUG_OBJECT (basepayload, "found next start at %u of size %u", next,
          nal_len);

      if (rtph264pay->sprop_parameter_sets != NULL) {
        /* explicitly set profile and sprop, use those */
        if (rtph264pay->update_caps) {
          if (!gst_rtp_base_payload_set_outcaps (basepayload,
                  "sprop-parameter-sets", G_TYPE_STRING,
                  rtph264pay->sprop_parameter_sets, NULL))
            goto caps_rejected;

          /* parse SPS and PPS from provided parameter set (for insertion) */
          gst_rtp_h264_pay_parse_sprop_parameter_sets (rtph264pay);

          rtph264pay->update_caps = FALSE;

          GST_DEBUG ("outcaps update: sprop-parameter-sets=%s",
              rtph264pay->sprop_parameter_sets);
        }
      } else {
        /* We know our stream is a valid H264 NAL packet,
         * go parse it for SPS/PPS to enrich the caps */
        /* order: make sure to check nal */
        update =
            gst_rtp_h264_pay_decode_nal (rtph264pay, data, nal_len, dts, pts)
            || update;
      }
      /* move to next NAL packet */
      data += nal_len;
      size -= nal_len;

      g_array_append_val (nal_queue, nal_len);
    }

    /* if has new SPS & PPS, update the output caps */
    if (G_UNLIKELY (update))
      if (!gst_rtp_h264_pay_set_sps_pps (basepayload))
        goto caps_rejected;

    /* second pass to payload and push */

    if (nal_queue->len != 0)
      gst_adapter_flush (rtph264pay->adapter, skip);

    for (i = 0; i < nal_queue->len; i++) {
      guint size;
      gboolean end_of_au = FALSE;

      nal_len = g_array_index (nal_queue, guint, i);
      /* skip start code */
      gst_adapter_flush (rtph264pay->adapter, 3);

      /* Trim the end unless we're the last NAL in the stream.
       * In case we're not at the end of the buffer we know the next block
       * starts with 0x000001 so all the 0x00 bytes at the end of this one are
       * trailing 0x0 that can be discarded */
      size = nal_len;
      data = gst_adapter_map (rtph264pay->adapter, size);
      if (i + 1 != nal_queue->len || buffer != NULL)
        for (; size > 1 && data[size - 1] == 0x0; size--)
          /* skip */ ;


      /* If it's the last nal unit we have in non-bytestream mode, we can
       * assume it's the end of an access-unit
       *
       * FIXME: We need to wait until the next packet or EOS to
       * actually payload the NAL so we can know if the current NAL is
       * the last one of an access unit or not if we are in bytestream mode
       */
      if ((rtph264pay->alignment == GST_H264_ALIGNMENT_AU || buffer == NULL) &&
          i == nal_queue->len - 1)
        end_of_au = TRUE;
      paybuf = gst_adapter_take_buffer (rtph264pay->adapter, size);
      g_assert (paybuf);

      /* put the data in one or more RTP packets */
      ret =
          gst_rtp_h264_pay_payload_nal (basepayload, paybuf, dts, pts,
          end_of_au);
      if (ret != GST_FLOW_OK) {
        break;
      }

      /* move to next NAL packet */
      /* Skips the trailing zeros */
      gst_adapter_flush (rtph264pay->adapter, nal_len - size);
    }
    g_array_set_size (nal_queue, 0);
  }

done:
  if (avc) {
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
  } else {
    gst_adapter_unmap (rtph264pay->adapter);
  }

  return ret;

caps_rejected:
  {
    GST_WARNING_OBJECT (basepayload, "Could not set outcaps");
    g_array_set_size (nal_queue, 0);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static gboolean
gst_rtp_h264_pay_sink_event (GstRTPBasePayload * payload, GstEvent * event)
{
  gboolean res;
  const GstStructure *s;
  GstRtpH264Pay *rtph264pay = GST_RTP_H264_PAY (payload);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_STOP:
      gst_adapter_clear (rtph264pay->adapter);
      break;
    case GST_EVENT_CUSTOM_DOWNSTREAM:
      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        gboolean resend_codec_data;

        if (gst_structure_get_boolean (s, "all-headers",
                &resend_codec_data) && resend_codec_data)
          rtph264pay->send_spspps = TRUE;
      }
      break;
    case GST_EVENT_EOS:
    {
      /* call handle_buffer with NULL to flush last NAL from adapter
       * in byte-stream mode
       */
      gst_rtp_h264_pay_handle_buffer (payload, NULL);
      break;
    }
    default:
      break;
  }

  res = GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->sink_event (payload, event);

  return res;
}

static GstStateChangeReturn
gst_rtp_h264_pay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRtpH264Pay *rtph264pay = GST_RTP_H264_PAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      rtph264pay->send_spspps = FALSE;
      gst_adapter_clear (rtph264pay->adapter);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
}

static void
gst_rtp_h264_pay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    case PROP_SPROP_PARAMETER_SETS:
      g_free (rtph264pay->sprop_parameter_sets);
      rtph264pay->sprop_parameter_sets = g_value_dup_string (value);
      rtph264pay->update_caps = TRUE;
      break;
    case PROP_CONFIG_INTERVAL:
      rtph264pay->spspps_interval = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rtp_h264_pay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRtpH264Pay *rtph264pay;

  rtph264pay = GST_RTP_H264_PAY (object);

  switch (prop_id) {
    case PROP_SPROP_PARAMETER_SETS:
      g_value_set_string (value, rtph264pay->sprop_parameter_sets);
      break;
    case PROP_CONFIG_INTERVAL:
      g_value_set_uint (value, rtph264pay->spspps_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_rtp_h264_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtph264pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_H264_PAY);
}
