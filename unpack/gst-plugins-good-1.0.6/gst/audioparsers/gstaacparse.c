/* GStreamer AAC parser plugin
 * Copyright (C) 2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Stefan Kost <stefan.kost@nokia.com>
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
 * SECTION:element-aacparse
 * @short_description: AAC parser
 * @see_also: #GstAmrParse
 *
 * This is an AAC parser which handles both ADIF and ADTS stream formats.
 *
 * As ADIF format is not framed, it is not seekable and stream duration cannot
 * be determined either. However, ADTS format AAC clips can be seeked, and parser
 * can also estimate playback position and clip duration.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 filesrc location=abc.aac ! aacparse ! faad ! audioresample ! audioconvert ! alsasink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include <gst/base/gstbitreader.h>
#include "gstaacparse.h"


static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "framed = (boolean) true, " "mpegversion = (int) { 2, 4 }, "
        "stream-format = (string) { raw, adts, adif, loas };"));

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, mpegversion = (int) { 2, 4 };"));

GST_DEBUG_CATEGORY_STATIC (aacparse_debug);
#define GST_CAT_DEFAULT aacparse_debug


#define ADIF_MAX_SIZE 40        /* Should be enough */
#define ADTS_MAX_SIZE 10        /* Should be enough */
#define LOAS_MAX_SIZE 3         /* Should be enough */


#define AAC_FRAME_DURATION(parse) (GST_SECOND/parse->frames_per_sec)

static const gint loas_sample_rate_table[32] = {
  96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
  16000, 12000, 11025, 8000, 7350, 0, 0, 0
};

static const gint loas_channels_table[32] = {
  0, 1, 2, 3, 4, 5, 6, 8,
  0, 0, 0, 0, 0, 0, 0, 0
};

static gboolean gst_aac_parse_start (GstBaseParse * parse);
static gboolean gst_aac_parse_stop (GstBaseParse * parse);

static gboolean gst_aac_parse_sink_setcaps (GstBaseParse * parse,
    GstCaps * caps);
static GstCaps *gst_aac_parse_sink_getcaps (GstBaseParse * parse,
    GstCaps * filter);

static GstFlowReturn gst_aac_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize);

G_DEFINE_TYPE (GstAacParse, gst_aac_parse, GST_TYPE_BASE_PARSE);

static inline gint
gst_aac_parse_get_sample_rate_from_index (guint sr_idx)
{
  static const guint aac_sample_rates[] = { 96000, 88200, 64000, 48000, 44100,
    32000, 24000, 22050, 16000, 12000, 11025, 8000
  };

  if (sr_idx < G_N_ELEMENTS (aac_sample_rates))
    return aac_sample_rates[sr_idx];
  GST_WARNING ("Invalid sample rate index %u", sr_idx);
  return 0;
}

/**
 * gst_aac_parse_class_init:
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aac_parse_class_init (GstAacParseClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseParseClass *parse_class = GST_BASE_PARSE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (aacparse_debug, "aacparse", 0,
      "AAC audio stream parser");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "AAC audio stream parser", "Codec/Parser/Audio",
      "Advanced Audio Coding parser", "Stefan Kost <stefan.kost@nokia.com>");

  parse_class->start = GST_DEBUG_FUNCPTR (gst_aac_parse_start);
  parse_class->stop = GST_DEBUG_FUNCPTR (gst_aac_parse_stop);
  parse_class->set_sink_caps = GST_DEBUG_FUNCPTR (gst_aac_parse_sink_setcaps);
  parse_class->get_sink_caps = GST_DEBUG_FUNCPTR (gst_aac_parse_sink_getcaps);
  parse_class->handle_frame = GST_DEBUG_FUNCPTR (gst_aac_parse_handle_frame);
}


/**
 * gst_aac_parse_init:
 * @aacparse: #GstAacParse.
 * @klass: #GstAacParseClass.
 *
 */
static void
gst_aac_parse_init (GstAacParse * aacparse)
{
  GST_DEBUG ("initialized");
}


/**
 * gst_aac_parse_set_src_caps:
 * @aacparse: #GstAacParse.
 * @sink_caps: (proposed) caps of sink pad
 *
 * Set source pad caps according to current knowledge about the
 * audio stream.
 *
 * Returns: TRUE if caps were successfully set.
 */
static gboolean
gst_aac_parse_set_src_caps (GstAacParse * aacparse, GstCaps * sink_caps)
{
  GstStructure *s;
  GstCaps *src_caps = NULL;
  gboolean res = FALSE;
  const gchar *stream_format;

  GST_DEBUG_OBJECT (aacparse, "sink caps: %" GST_PTR_FORMAT, sink_caps);
  if (sink_caps)
    src_caps = gst_caps_copy (sink_caps);
  else
    src_caps = gst_caps_new_empty_simple ("audio/mpeg");

  gst_caps_set_simple (src_caps, "framed", G_TYPE_BOOLEAN, TRUE,
      "mpegversion", G_TYPE_INT, aacparse->mpegversion, NULL);

  switch (aacparse->header_type) {
    case DSPAAC_HEADER_NONE:
      stream_format = "raw";
      break;
    case DSPAAC_HEADER_ADTS:
      stream_format = "adts";
      break;
    case DSPAAC_HEADER_ADIF:
      stream_format = "adif";
      break;
    case DSPAAC_HEADER_LOAS:
      stream_format = "loas";
      break;
    default:
      stream_format = NULL;
  }

  s = gst_caps_get_structure (src_caps, 0);
  if (aacparse->sample_rate > 0)
    gst_structure_set (s, "rate", G_TYPE_INT, aacparse->sample_rate, NULL);
  if (aacparse->channels > 0)
    gst_structure_set (s, "channels", G_TYPE_INT, aacparse->channels, NULL);
  if (stream_format)
    gst_structure_set (s, "stream-format", G_TYPE_STRING, stream_format, NULL);

  GST_DEBUG_OBJECT (aacparse, "setting src caps: %" GST_PTR_FORMAT, src_caps);

  res = gst_pad_set_caps (GST_BASE_PARSE (aacparse)->srcpad, src_caps);
  gst_caps_unref (src_caps);
  return res;
}


/**
 * gst_aac_parse_sink_setcaps:
 * @sinkpad: GstPad
 * @caps: GstCaps
 *
 * Implementation of "set_sink_caps" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_aac_parse_sink_setcaps (GstBaseParse * parse, GstCaps * caps)
{
  GstAacParse *aacparse;
  GstStructure *structure;
  gchar *caps_str;
  const GValue *value;

  aacparse = GST_AAC_PARSE (parse);
  structure = gst_caps_get_structure (caps, 0);
  caps_str = gst_caps_to_string (caps);

  GST_DEBUG_OBJECT (aacparse, "setcaps: %s", caps_str);
  g_free (caps_str);

  /* This is needed at least in case of RTP
   * Parses the codec_data information to get ObjectType,
   * number of channels and samplerate */
  value = gst_structure_get_value (structure, "codec_data");
  if (value) {
    GstBuffer *buf = gst_value_get_buffer (value);

    if (buf) {
      GstMapInfo map;
      guint sr_idx;

      gst_buffer_map (buf, &map, GST_MAP_READ);

      sr_idx = ((map.data[0] & 0x07) << 1) | ((map.data[1] & 0x80) >> 7);
      aacparse->object_type = (map.data[0] & 0xf8) >> 3;
      aacparse->sample_rate = gst_aac_parse_get_sample_rate_from_index (sr_idx);
      aacparse->channels = (map.data[1] & 0x78) >> 3;
      aacparse->header_type = DSPAAC_HEADER_NONE;
      aacparse->mpegversion = 4;
      aacparse->frame_samples = (map.data[1] & 4) ? 960 : 1024;
      gst_buffer_unmap (buf, &map);

      GST_DEBUG ("codec_data: object_type=%d, sample_rate=%d, channels=%d, "
          "samples=%d", aacparse->object_type, aacparse->sample_rate,
          aacparse->channels, aacparse->frame_samples);

      /* arrange for metadata and get out of the way */
      gst_aac_parse_set_src_caps (aacparse, caps);
      gst_base_parse_set_passthrough (parse, TRUE);
    } else
      return FALSE;

    /* caps info overrides */
    gst_structure_get_int (structure, "rate", &aacparse->sample_rate);
    gst_structure_get_int (structure, "channels", &aacparse->channels);
  } else {
    aacparse->sample_rate = 0;
    aacparse->channels = 0;
    aacparse->header_type = DSPAAC_HEADER_NOT_PARSED;
    gst_base_parse_set_passthrough (parse, FALSE);
  }

  return TRUE;
}


/**
 * gst_aac_parse_adts_get_frame_len:
 * @data: block of data containing an ADTS header.
 *
 * This function calculates ADTS frame length from the given header.
 *
 * Returns: size of the ADTS frame.
 */
static inline guint
gst_aac_parse_adts_get_frame_len (const guint8 * data)
{
  return ((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] & 0xe0) >> 5);
}


/**
 * gst_aac_parse_check_adts_frame:
 * @aacparse: #GstAacParse.
 * @data: Data to be checked.
 * @avail: Amount of data passed.
 * @framesize: If valid ADTS frame was found, this will be set to tell the
 *             found frame size in bytes.
 * @needed_data: If frame was not found, this may be set to tell how much
 *               more data is needed in the next round to detect the frame
 *               reliably. This may happen when a frame header candidate
 *               is found but it cannot be guaranteed to be the header without
 *               peeking the following data.
 *
 * Check if the given data contains contains ADTS frame. The algorithm
 * will examine ADTS frame header and calculate the frame size. Also, another
 * consecutive ADTS frame header need to be present after the found frame.
 * Otherwise the data is not considered as a valid ADTS frame. However, this
 * "extra check" is omitted when EOS has been received. In this case it is
 * enough when data[0] contains a valid ADTS header.
 *
 * This function may set the #needed_data to indicate that a possible frame
 * candidate has been found, but more data (#needed_data bytes) is needed to
 * be absolutely sure. When this situation occurs, FALSE will be returned.
 *
 * When a valid frame is detected, this function will use
 * gst_base_parse_set_min_frame_size() function from #GstBaseParse class
 * to set the needed bytes for next frame.This way next data chunk is already
 * of correct size.
 *
 * Returns: TRUE if the given data contains a valid ADTS header.
 */
static gboolean
gst_aac_parse_check_adts_frame (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, guint * needed_data)
{
  *needed_data = 0;

  if (G_UNLIKELY (avail < 2))
    return FALSE;

  if ((data[0] == 0xff) && ((data[1] & 0xf6) == 0xf0)) {
    *framesize = gst_aac_parse_adts_get_frame_len (data);

    /* In EOS mode this is enough. No need to examine the data further.
       We also relax the check when we have sync, on the assumption that
       if we're not looking at random data, we have a much higher chance
       to get the correct sync, and this avoids losing two frames when
       a single bit corruption happens. */
    if (drain || !GST_BASE_PARSE_LOST_SYNC (aacparse)) {
      return TRUE;
    }

    if (*framesize + ADTS_MAX_SIZE > avail) {
      /* We have found a possible frame header candidate, but can't be
         sure since we don't have enough data to check the next frame */
      GST_DEBUG ("NEED MORE DATA: we need %d, available %d",
          *framesize + ADTS_MAX_SIZE, avail);
      *needed_data = *framesize + ADTS_MAX_SIZE;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          *framesize + ADTS_MAX_SIZE);
      return FALSE;
    }

    if ((data[*framesize] == 0xff) && ((data[*framesize + 1] & 0xf6) == 0xf0)) {
      guint nextlen = gst_aac_parse_adts_get_frame_len (data + (*framesize));

      GST_LOG ("ADTS frame found, len: %d bytes", *framesize);
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          nextlen + ADTS_MAX_SIZE);
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
gst_aac_parse_latm_get_value (GstAacParse * aacparse, GstBitReader * br,
    guint32 * value)
{
  guint8 bytes, i, byte;

  *value = 0;
  if (!gst_bit_reader_get_bits_uint8 (br, &bytes, 2))
    return FALSE;
  for (i = 0; i < bytes; ++i) {
    *value <<= 8;
    if (!gst_bit_reader_get_bits_uint8 (br, &byte, 8))
      return FALSE;
    *value += byte;
  }
  return TRUE;
}

static gboolean
gst_aac_parse_get_audio_object_type (GstAacParse * aacparse, GstBitReader * br,
    guint8 * audio_object_type)
{
  if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 5))
    return FALSE;
  if (*audio_object_type == 31) {
    if (!gst_bit_reader_get_bits_uint8 (br, audio_object_type, 6))
      return FALSE;
    *audio_object_type += 32;
  }
  GST_LOG_OBJECT (aacparse, "audio object type %u", *audio_object_type);
  return TRUE;
}

static gboolean
gst_aac_parse_get_audio_sample_rate (GstAacParse * aacparse, GstBitReader * br,
    gint * sample_rate)
{
  guint8 sampling_frequency_index;
  if (!gst_bit_reader_get_bits_uint8 (br, &sampling_frequency_index, 4))
    return FALSE;
  GST_LOG_OBJECT (aacparse, "sampling_frequency_index: %u",
      sampling_frequency_index);
  if (sampling_frequency_index == 0xf) {
    guint32 sampling_rate;
    if (!gst_bit_reader_get_bits_uint32 (br, &sampling_rate, 24))
      return FALSE;
    *sample_rate = sampling_rate;
  } else {
    *sample_rate = loas_sample_rate_table[sampling_frequency_index];
    if (!*sample_rate)
      return FALSE;
  }
  return TRUE;
}

/* See table 1.13 in ISO/IEC 14496-3 */
static gboolean
gst_aac_parse_read_loas_audio_specific_config (GstAacParse * aacparse,
    GstBitReader * br, gint * sample_rate, gint * channels, guint32 * bits)
{
  guint8 audio_object_type, channel_configuration;

  if (!gst_aac_parse_get_audio_object_type (aacparse, br, &audio_object_type))
    return FALSE;

  if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
    return FALSE;

  if (!gst_bit_reader_get_bits_uint8 (br, &channel_configuration, 4))
    return FALSE;
  GST_LOG_OBJECT (aacparse, "channel_configuration: %d", channel_configuration);
  *channels = loas_channels_table[channel_configuration];
  if (!*channels)
    return FALSE;

  if (audio_object_type == 5) {
    GST_LOG_OBJECT (aacparse,
        "Audio object type 5, so rereading sampling rate...");
    if (!gst_aac_parse_get_audio_sample_rate (aacparse, br, sample_rate))
      return FALSE;
  }

  GST_INFO_OBJECT (aacparse, "Found LOAS config: %d Hz, %d channels",
      *sample_rate, *channels);

  /* There's LOTS of stuff next, but we ignore it for now as we have
     what we want (sample rate and number of channels */
  GST_DEBUG_OBJECT (aacparse,
      "Need more code to parse humongous LOAS data, currently ignored");
  if (bits)
    *bits = 0;
  return TRUE;
}


static gboolean
gst_aac_parse_read_loas_config (GstAacParse * aacparse, const guint8 * data,
    guint avail, gint * sample_rate, gint * channels, gint * version)
{
  GstBitReader br;
  guint8 u8, v, vA;

  /* No version in the bitstream, but the spec has LOAS in the MPEG-4 section */
  if (version)
    *version = 4;

  gst_bit_reader_init (&br, data, avail);

  /* skip sync word (11 bits) and size (13 bits) */
  if (!gst_bit_reader_skip (&br, 11 + 13))
    return FALSE;

  /* First bit is "use last config" */
  if (!gst_bit_reader_get_bits_uint8 (&br, &u8, 1))
    return FALSE;
  if (u8) {
    GST_DEBUG_OBJECT (aacparse, "Frame uses previous config");
    if (!aacparse->sample_rate || !aacparse->channels) {
      GST_WARNING_OBJECT (aacparse, "No previous config to use");
    }
    *sample_rate = aacparse->sample_rate;
    *channels = aacparse->channels;
    return TRUE;
  }

  GST_DEBUG_OBJECT (aacparse, "Frame contains new config");

  if (!gst_bit_reader_get_bits_uint8 (&br, &v, 1))
    return FALSE;
  if (v) {
    if (!gst_bit_reader_get_bits_uint8 (&br, &vA, 1))
      return FALSE;
  } else
    vA = 0;

  GST_LOG_OBJECT (aacparse, "v %d, vA %d", v, vA);
  if (vA == 0) {
    guint8 same_time, subframes, num_program, prog;
    if (v == 1) {
      guint32 value;
      if (!gst_aac_parse_latm_get_value (aacparse, &br, &value))
        return FALSE;
    }
    if (!gst_bit_reader_get_bits_uint8 (&br, &same_time, 1))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (&br, &subframes, 6))
      return FALSE;
    if (!gst_bit_reader_get_bits_uint8 (&br, &num_program, 4))
      return FALSE;
    GST_LOG_OBJECT (aacparse, "same_time %d, subframes %d, num_program %d",
        same_time, subframes, num_program);

    for (prog = 0; prog <= num_program; ++prog) {
      guint8 num_layer, layer;
      if (!gst_bit_reader_get_bits_uint8 (&br, &num_layer, 3))
        return FALSE;
      GST_LOG_OBJECT (aacparse, "Program %d: %d layers", prog, num_layer);

      for (layer = 0; layer <= num_layer; ++layer) {
        guint8 use_same_config;
        if (prog == 0 && layer == 0) {
          use_same_config = 0;
        } else {
          if (!gst_bit_reader_get_bits_uint8 (&br, &use_same_config, 1))
            return FALSE;
        }
        if (!use_same_config) {
          if (v == 0) {
            if (!gst_aac_parse_read_loas_audio_specific_config (aacparse, &br,
                    sample_rate, channels, NULL))
              return FALSE;
          } else {
            guint32 bits, asc_len;
            if (!gst_aac_parse_latm_get_value (aacparse, &br, &asc_len))
              return FALSE;
            if (!gst_aac_parse_read_loas_audio_specific_config (aacparse, &br,
                    sample_rate, channels, &bits))
              return FALSE;
            asc_len -= bits;
            if (!gst_bit_reader_skip (&br, asc_len))
              return FALSE;
          }
        }
      }
    }
    GST_WARNING_OBJECT (aacparse, "More data ignored");
  } else {
    GST_WARNING_OBJECT (aacparse, "Spec says \"TBD\"...");
  }
  return TRUE;
}

/**
 * gst_aac_parse_loas_get_frame_len:
 * @data: block of data containing a LOAS header.
 *
 * This function calculates LOAS frame length from the given header.
 *
 * Returns: size of the LOAS frame.
 */
static inline guint
gst_aac_parse_loas_get_frame_len (const guint8 * data)
{
  return (((data[1] & 0x1f) << 8) | data[2]) + 3;
}


/**
 * gst_aac_parse_check_loas_frame:
 * @aacparse: #GstAacParse.
 * @data: Data to be checked.
 * @avail: Amount of data passed.
 * @framesize: If valid LOAS frame was found, this will be set to tell the
 *             found frame size in bytes.
 * @needed_data: If frame was not found, this may be set to tell how much
 *               more data is needed in the next round to detect the frame
 *               reliably. This may happen when a frame header candidate
 *               is found but it cannot be guaranteed to be the header without
 *               peeking the following data.
 *
 * Check if the given data contains contains LOAS frame. The algorithm
 * will examine LOAS frame header and calculate the frame size. Also, another
 * consecutive LOAS frame header need to be present after the found frame.
 * Otherwise the data is not considered as a valid LOAS frame. However, this
 * "extra check" is omitted when EOS has been received. In this case it is
 * enough when data[0] contains a valid LOAS header.
 *
 * This function may set the #needed_data to indicate that a possible frame
 * candidate has been found, but more data (#needed_data bytes) is needed to
 * be absolutely sure. When this situation occurs, FALSE will be returned.
 *
 * When a valid frame is detected, this function will use
 * gst_base_parse_set_min_frame_size() function from #GstBaseParse class
 * to set the needed bytes for next frame.This way next data chunk is already
 * of correct size.
 *
 * LOAS can have three different formats, if I read the spec correctly. Only
 * one of them is supported here, as the two samples I have use this one.
 *
 * Returns: TRUE if the given data contains a valid LOAS header.
 */
static gboolean
gst_aac_parse_check_loas_frame (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, guint * needed_data)
{
  *needed_data = 0;

  /* 3 byte header */
  if (G_UNLIKELY (avail < 3))
    return FALSE;

  if ((data[0] == 0x56) && ((data[1] & 0xe0) == 0xe0)) {
    *framesize = gst_aac_parse_loas_get_frame_len (data);
    GST_DEBUG_OBJECT (aacparse, "Found %u byte LOAS frame", *framesize);

    /* In EOS mode this is enough. No need to examine the data further.
       We also relax the check when we have sync, on the assumption that
       if we're not looking at random data, we have a much higher chance
       to get the correct sync, and this avoids losing two frames when
       a single bit corruption happens. */
    if (drain || !GST_BASE_PARSE_LOST_SYNC (aacparse)) {
      return TRUE;
    }

    if (*framesize + LOAS_MAX_SIZE > avail) {
      /* We have found a possible frame header candidate, but can't be
         sure since we don't have enough data to check the next frame */
      GST_DEBUG ("NEED MORE DATA: we need %d, available %d",
          *framesize + LOAS_MAX_SIZE, avail);
      *needed_data = *framesize + LOAS_MAX_SIZE;
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          *framesize + LOAS_MAX_SIZE);
      return FALSE;
    }

    if ((data[*framesize] == 0x56) && ((data[*framesize + 1] & 0xe0) == 0xe0)) {
      guint nextlen = gst_aac_parse_loas_get_frame_len (data + (*framesize));

      GST_LOG ("LOAS frame found, len: %d bytes", *framesize);
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          nextlen + LOAS_MAX_SIZE);
      return TRUE;
    }
  }
  return FALSE;
}

/* caller ensure sufficient data */
static inline void
gst_aac_parse_parse_adts_header (GstAacParse * aacparse, const guint8 * data,
    gint * rate, gint * channels, gint * object, gint * version)
{

  if (rate) {
    gint sr_idx = (data[2] & 0x3c) >> 2;

    *rate = gst_aac_parse_get_sample_rate_from_index (sr_idx);
  }
  if (channels)
    *channels = ((data[2] & 0x01) << 2) | ((data[3] & 0xc0) >> 6);

  if (version)
    *version = (data[1] & 0x08) ? 2 : 4;
  if (object)
    *object = (data[2] & 0xc0) >> 6;
}

/**
 * gst_aac_parse_detect_stream:
 * @aacparse: #GstAacParse.
 * @data: A block of data that needs to be examined for stream characteristics.
 * @avail: Size of the given datablock.
 * @framesize: If valid stream was found, this will be set to tell the
 *             first frame size in bytes.
 * @skipsize: If valid stream was found, this will be set to tell the first
 *            audio frame position within the given data.
 *
 * Examines the given piece of data and try to detect the format of it. It
 * checks for "ADIF" header (in the beginning of the clip) and ADTS frame
 * header. If the stream is detected, TRUE will be returned and #framesize
 * is set to indicate the found frame size. Additionally, #skipsize might
 * be set to indicate the number of bytes that need to be skipped, a.k.a. the
 * position of the frame inside given data chunk.
 *
 * Returns: TRUE on success.
 */
static gboolean
gst_aac_parse_detect_stream (GstAacParse * aacparse,
    const guint8 * data, const guint avail, gboolean drain,
    guint * framesize, gint * skipsize)
{
  gboolean found = FALSE;
  guint need_data_adts = 0, need_data_loas;
  guint i = 0;

  GST_DEBUG_OBJECT (aacparse, "Parsing header data");

  /* FIXME: No need to check for ADIF if we are not in the beginning of the
     stream */

  /* Can we even parse the header? */
  if (avail < MAX (ADTS_MAX_SIZE, LOAS_MAX_SIZE)) {
    GST_DEBUG_OBJECT (aacparse, "Not enough data to check");
    return FALSE;
  }

  for (i = 0; i < avail - 4; i++) {
    if (((data[i] == 0xff) && ((data[i + 1] & 0xf6) == 0xf0)) ||
        ((data[0] == 0x56) && ((data[1] & 0xe0) == 0xe0)) ||
        strncmp ((char *) data + i, "ADIF", 4) == 0) {
      GST_DEBUG_OBJECT (aacparse, "Found signature at offset %u", i);
      found = TRUE;

      if (i) {
        /* Trick: tell the parent class that we didn't find the frame yet,
           but make it skip 'i' amount of bytes. Next time we arrive
           here we have full frame in the beginning of the data. */
        *skipsize = i;
        return FALSE;
      }
      break;
    }
  }
  if (!found) {
    if (i)
      *skipsize = i;
    return FALSE;
  }

  if (gst_aac_parse_check_adts_frame (aacparse, data, avail, drain,
          framesize, &need_data_adts)) {
    gint rate, channels;

    GST_INFO ("ADTS ID: %d, framesize: %d", (data[1] & 0x08) >> 3, *framesize);

    gst_aac_parse_parse_adts_header (aacparse, data, &rate, &channels,
        &aacparse->object_type, &aacparse->mpegversion);

    if (!channels || !framesize) {
      GST_DEBUG_OBJECT (aacparse, "impossible ADTS configuration");
      return FALSE;
    }

    aacparse->header_type = DSPAAC_HEADER_ADTS;
    gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse), rate,
        aacparse->frame_samples, 2, 2);

    GST_DEBUG ("ADTS: samplerate %d, channels %d, objtype %d, version %d",
        rate, channels, aacparse->object_type, aacparse->mpegversion);

    gst_base_parse_set_syncable (GST_BASE_PARSE (aacparse), TRUE);

    return TRUE;
  }

  if (gst_aac_parse_check_loas_frame (aacparse, data, avail, drain,
          framesize, &need_data_loas)) {
    gint rate, channels;

    GST_INFO ("LOAS, framesize: %d", *framesize);

    aacparse->header_type = DSPAAC_HEADER_LOAS;

    if (!gst_aac_parse_read_loas_config (aacparse, data, avail, &rate,
            &channels, &aacparse->mpegversion)) {
      GST_WARNING_OBJECT (aacparse, "Error reading LOAS config");
      return FALSE;
    }

    if (rate && channels) {
      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse), rate,
          aacparse->frame_samples, 2, 2);

      GST_DEBUG ("LOAS: samplerate %d, channels %d, objtype %d, version %d",
          rate, channels, aacparse->object_type, aacparse->mpegversion);
      aacparse->sample_rate = rate;
      aacparse->channels = channels;
    }

    gst_base_parse_set_syncable (GST_BASE_PARSE (aacparse), TRUE);

    return TRUE;
  }

  if (need_data_adts || need_data_loas) {
    /* This tells the parent class not to skip any data */
    *skipsize = 0;
    return FALSE;
  }

  if (avail < ADIF_MAX_SIZE)
    return FALSE;

  if (memcmp (data + i, "ADIF", 4) == 0) {
    const guint8 *adif;
    int skip_size = 0;
    int bitstream_type;
    int sr_idx;
    GstCaps *sinkcaps;

    aacparse->header_type = DSPAAC_HEADER_ADIF;
    aacparse->mpegversion = 4;

    /* Skip the "ADIF" bytes */
    adif = data + i + 4;

    /* copyright string */
    if (adif[0] & 0x80)
      skip_size += 9;           /* skip 9 bytes */

    bitstream_type = adif[0 + skip_size] & 0x10;
    aacparse->bitrate =
        ((unsigned int) (adif[0 + skip_size] & 0x0f) << 19) |
        ((unsigned int) adif[1 + skip_size] << 11) |
        ((unsigned int) adif[2 + skip_size] << 3) |
        ((unsigned int) adif[3 + skip_size] & 0xe0);

    /* CBR */
    if (bitstream_type == 0) {
#if 0
      /* Buffer fullness parsing. Currently not needed... */
      guint num_elems = 0;
      guint fullness = 0;

      num_elems = (adif[3 + skip_size] & 0x1e);
      GST_INFO ("ADIF num_config_elems: %d", num_elems);

      fullness = ((unsigned int) (adif[3 + skip_size] & 0x01) << 19) |
          ((unsigned int) adif[4 + skip_size] << 11) |
          ((unsigned int) adif[5 + skip_size] << 3) |
          ((unsigned int) (adif[6 + skip_size] & 0xe0) >> 5);

      GST_INFO ("ADIF buffer fullness: %d", fullness);
#endif
      aacparse->object_type = ((adif[6 + skip_size] & 0x01) << 1) |
          ((adif[7 + skip_size] & 0x80) >> 7);
      sr_idx = (adif[7 + skip_size] & 0x78) >> 3;
    }
    /* VBR */
    else {
      aacparse->object_type = (adif[4 + skip_size] & 0x18) >> 3;
      sr_idx = ((adif[4 + skip_size] & 0x07) << 1) |
          ((adif[5 + skip_size] & 0x80) >> 7);
    }

    /* FIXME: This gives totally wrong results. Duration calculation cannot
       be based on this */
    aacparse->sample_rate = gst_aac_parse_get_sample_rate_from_index (sr_idx);

    /* baseparse is not given any fps,
     * so it will give up on timestamps, seeking, etc */

    /* FIXME: Can we assume this? */
    aacparse->channels = 2;

    GST_INFO ("ADIF: br=%d, samplerate=%d, objtype=%d",
        aacparse->bitrate, aacparse->sample_rate, aacparse->object_type);

    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), 512);

    /* arrange for metadata and get out of the way */
    sinkcaps = gst_pad_get_current_caps (GST_BASE_PARSE_SINK_PAD (aacparse));
    gst_aac_parse_set_src_caps (aacparse, sinkcaps);
    if (sinkcaps)
      gst_caps_unref (sinkcaps);

    /* not syncable, not easily seekable (unless we push data from start */
    gst_base_parse_set_syncable (GST_BASE_PARSE_CAST (aacparse), FALSE);
    gst_base_parse_set_passthrough (GST_BASE_PARSE_CAST (aacparse), TRUE);
    gst_base_parse_set_average_bitrate (GST_BASE_PARSE_CAST (aacparse), 0);

    *framesize = avail;
    return TRUE;
  }

  /* This should never happen */
  return FALSE;
}


/**
 * gst_aac_parse_check_valid_frame:
 * @parse: #GstBaseParse.
 * @frame: #GstBaseParseFrame.
 * @skipsize: How much data parent class should skip in order to find the
 *            frame header.
 *
 * Implementation of "handle_frame" vmethod in #GstBaseParse class.
 *
 * Also determines frame overhead.
 * ADTS streams have a 7 byte header in each frame. MP4 and ADIF streams don't have
 * a per-frame header. LOAS has 3 bytes.
 *
 * We're making a couple of simplifying assumptions:
 *
 * 1. We count Program Configuration Elements rather than searching for them
 *    in the streams to discount them - the overhead is negligible.
 *
 * 2. We ignore CRC. This has a worst-case impact of (num_raw_blocks + 1)*16
 *    bits, which should still not be significant enough to warrant the
 *    additional parsing through the headers
 *
 * Returns: a #GstFlowReturn.
 */
static GstFlowReturn
gst_aac_parse_handle_frame (GstBaseParse * parse,
    GstBaseParseFrame * frame, gint * skipsize)
{
  GstMapInfo map;
  GstAacParse *aacparse;
  gboolean ret = FALSE;
  gboolean lost_sync;
  GstBuffer *buffer;
  guint framesize;
  gint rate, channels;

  aacparse = GST_AAC_PARSE (parse);
  buffer = frame->buffer;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  *skipsize = -1;
  lost_sync = GST_BASE_PARSE_LOST_SYNC (parse);

  if (aacparse->header_type == DSPAAC_HEADER_ADIF ||
      aacparse->header_type == DSPAAC_HEADER_NONE) {
    /* There is nothing to parse */
    framesize = map.size;
    ret = TRUE;

  } else if (aacparse->header_type == DSPAAC_HEADER_NOT_PARSED || lost_sync) {

    ret = gst_aac_parse_detect_stream (aacparse, map.data, map.size,
        GST_BASE_PARSE_DRAINING (parse), &framesize, skipsize);

  } else if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
    guint needed_data = 1024;

    ret = gst_aac_parse_check_adts_frame (aacparse, map.data, map.size,
        GST_BASE_PARSE_DRAINING (parse), &framesize, &needed_data);

    if (!ret) {
      GST_DEBUG ("buffer didn't contain valid frame");
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          needed_data);
    }

  } else if (aacparse->header_type == DSPAAC_HEADER_LOAS) {
    guint needed_data = 1024;

    ret = gst_aac_parse_check_loas_frame (aacparse, map.data,
        map.size, GST_BASE_PARSE_DRAINING (parse), &framesize, &needed_data);

    if (!ret) {
      GST_DEBUG ("buffer didn't contain valid frame");
      gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
          needed_data);
    }

  } else {
    GST_DEBUG ("buffer didn't contain valid frame");
    gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse),
        ADTS_MAX_SIZE);
  }

  if (G_UNLIKELY (!ret))
    goto exit;

  if (aacparse->header_type == DSPAAC_HEADER_ADTS) {
    /* see above */
    frame->overhead = 7;

    gst_aac_parse_parse_adts_header (aacparse, map.data,
        &rate, &channels, NULL, NULL);

    GST_LOG_OBJECT (aacparse, "rate: %d, chans: %d", rate, channels);

    if (G_UNLIKELY (rate != aacparse->sample_rate
            || channels != aacparse->channels)) {
      aacparse->sample_rate = rate;
      aacparse->channels = channels;

      if (!gst_aac_parse_set_src_caps (aacparse, NULL)) {
        /* If linking fails, we need to return appropriate error */
        ret = GST_FLOW_NOT_LINKED;
      }

      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse),
          aacparse->sample_rate, aacparse->frame_samples, 2, 2);
    }
  } else if (aacparse->header_type == DSPAAC_HEADER_LOAS) {
    gboolean setcaps = FALSE;

    /* see above */
    frame->overhead = 3;

    if (!gst_aac_parse_read_loas_config (aacparse, map.data, map.size, &rate,
            &channels, NULL)) {
      GST_WARNING_OBJECT (aacparse, "Error reading LOAS config");
    } else if (G_UNLIKELY (rate != aacparse->sample_rate
            || channels != aacparse->channels)) {
      aacparse->sample_rate = rate;
      aacparse->channels = channels;
      setcaps = TRUE;
      GST_INFO_OBJECT (aacparse, "New LOAS config: %d Hz, %d channels", rate,
          channels);
    }

    /* We want to set caps both at start, and when rate/channels change.
       Since only some LOAS frames have that info, we may receive frames
       before knowing about rate/channels. */
    if (setcaps
        || !gst_pad_has_current_caps (GST_BASE_PARSE_SRC_PAD (aacparse))) {
      if (!gst_aac_parse_set_src_caps (aacparse, NULL)) {
        /* If linking fails, we need to return appropriate error */
        ret = GST_FLOW_NOT_LINKED;
      }

      gst_base_parse_set_frame_rate (GST_BASE_PARSE (aacparse),
          aacparse->sample_rate, aacparse->frame_samples, 2, 2);
    }
  }

exit:
  gst_buffer_unmap (buffer, &map);

  if (ret) {
    /* found, skip if needed */
    if (*skipsize > 0)
      return GST_FLOW_OK;
    *skipsize = 0;
  } else {
    if (*skipsize < 0)
      *skipsize = 1;
  }

  if (ret && framesize <= map.size) {
    return gst_base_parse_finish_frame (parse, frame, framesize);
  }

  return GST_FLOW_OK;
}


/**
 * gst_aac_parse_start:
 * @parse: #GstBaseParse.
 *
 * Implementation of "start" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE if startup succeeded.
 */
static gboolean
gst_aac_parse_start (GstBaseParse * parse)
{
  GstAacParse *aacparse;

  aacparse = GST_AAC_PARSE (parse);
  GST_DEBUG ("start");
  aacparse->frame_samples = 1024;
  gst_base_parse_set_min_frame_size (GST_BASE_PARSE (aacparse), ADTS_MAX_SIZE);
  return TRUE;
}


/**
 * gst_aac_parse_stop:
 * @parse: #GstBaseParse.
 *
 * Implementation of "stop" vmethod in #GstBaseParse class.
 *
 * Returns: TRUE is stopping succeeded.
 */
static gboolean
gst_aac_parse_stop (GstBaseParse * parse)
{
  GST_DEBUG ("stop");
  return TRUE;
}

static GstCaps *
gst_aac_parse_sink_getcaps (GstBaseParse * parse, GstCaps * filter)
{
  GstCaps *peercaps, *templ;
  GstCaps *res;

  templ = gst_pad_get_pad_template_caps (GST_BASE_PARSE_SINK_PAD (parse));
  peercaps = gst_pad_peer_query_caps (GST_BASE_PARSE_SRC_PAD (parse), filter);
  if (peercaps) {
    guint i, n;

    /* Remove the framed field */
    peercaps = gst_caps_make_writable (peercaps);
    n = gst_caps_get_size (peercaps);
    for (i = 0; i < n; i++) {
      GstStructure *s = gst_caps_get_structure (peercaps, i);

      gst_structure_remove_field (s, "framed");
    }

    res = gst_caps_intersect_full (peercaps, templ, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (peercaps);
    res = gst_caps_make_writable (res);

    /* Append the template caps because we still want to accept
     * caps without any fields in the case upstream does not
     * know anything.
     */
    gst_caps_append (res, templ);
  } else {
    res = templ;
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, res, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (res);
    res = intersection;
  }

  return res;
}
