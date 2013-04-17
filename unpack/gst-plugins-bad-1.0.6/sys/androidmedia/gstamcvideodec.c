/*
 * Initially based on gst-omx/omx/gstomxvideodec.c
 *
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <string.h>

#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#include "gstamcvideodec.h"
#include "gstamc-constants.h"

GST_DEBUG_CATEGORY_STATIC (gst_amc_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_amc_video_dec_debug_category

typedef struct _BufferIdentification BufferIdentification;
struct _BufferIdentification
{
  guint64 timestamp;
};

static BufferIdentification *
buffer_identification_new (GstClockTime timestamp)
{
  BufferIdentification *id = g_slice_new (BufferIdentification);

  id->timestamp = timestamp;

  return id;
}

static void
buffer_identification_free (BufferIdentification * id)
{
  g_slice_free (BufferIdentification, id);
}

/* prototypes */
static void gst_amc_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_amc_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_amc_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_start (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_amc_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_amc_video_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_amc_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_amc_video_dec_finish (GstVideoDecoder * decoder);

static GstFlowReturn gst_amc_video_dec_drain (GstAmcVideoDec * self);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_amc_video_dec_debug_category, "amcvideodec", 0, \
      "Android MediaCodec video decoder");

GST_BOILERPLATE_FULL (GstAmcVideoDec, gst_amc_video_dec, GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER, DEBUG_INIT);

static GstCaps *
create_sink_caps (const GstAmcCodecInfo * codec_info)
{
  GstCaps *ret;
  gint i;

  ret = gst_caps_new_empty ();

  for (i = 0; i < codec_info->n_supported_types; i++) {
    const GstAmcCodecType *type = &codec_info->supported_types[i];

    if (strcmp (type->mime, "video/mp4v-es") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/mpeg",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "mpegversion", G_TYPE_INT, 4,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          const gchar *profile, *level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_STRING);

          profile =
              gst_amc_mpeg4_profile_to_string (type->profile_levels[j].profile);
          if (!profile) {
            GST_ERROR ("Unable to map MPEG4 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_mpeg4_level_to_string (k);
            if (!level)
              continue;

            g_value_set_string (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }

          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "video/3gpp") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/x-h263",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "parsed", G_TYPE_BOOLEAN, TRUE,
          "variant", G_TYPE_STRING, "itu", NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          gint profile, level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_UINT);

          profile =
              gst_amc_h263_profile_to_gst_id (type->profile_levels[j].profile);

          if (profile == -1) {
            GST_ERROR ("Unable to map h263 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_h263_level_to_gst_id (k);
            if (level == -1)
              continue;

            g_value_set_uint (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }
          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_UINT, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "video/avc") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile_level = FALSE;

      tmp = gst_structure_new ("video/x-h264",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "parsed", G_TYPE_BOOLEAN, TRUE,
          "stream-format", G_TYPE_STRING, "byte-stream",
          "alignment", G_TYPE_STRING, "au", NULL);

      if (type->n_profile_levels) {
        for (j = type->n_profile_levels - 1; j >= 0; j--) {
          const gchar *profile, *alternative = NULL, *level;
          gint k;
          GValue va = { 0, };
          GValue v = { 0, };

          g_value_init (&va, GST_TYPE_LIST);
          g_value_init (&v, G_TYPE_STRING);

          profile =
              gst_amc_avc_profile_to_string (type->profile_levels[j].profile,
              &alternative);

          if (!profile) {
            GST_ERROR ("Unable to map H264 profile 0x%08x",
                type->profile_levels[j].profile);
            continue;
          }

          for (k = 1; k <= type->profile_levels[j].level && k != 0; k <<= 1) {
            level = gst_amc_avc_level_to_string (k);
            if (!level)
              continue;

            g_value_set_string (&v, level);
            gst_value_list_append_value (&va, &v);
            g_value_reset (&v);
          }
          tmp2 = gst_structure_copy (tmp);
          gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
          gst_structure_set_value (tmp2, "level", &va);
          if (!alternative)
            g_value_unset (&va);
          g_value_unset (&v);
          gst_caps_merge_structure (ret, tmp2);

          if (alternative) {
            tmp2 = gst_structure_copy (tmp);
            gst_structure_set (tmp2, "profile", G_TYPE_STRING, alternative,
                NULL);
            gst_structure_set_value (tmp2, "level", &va);
            g_value_unset (&va);
            gst_caps_merge_structure (ret, tmp2);
          }
          have_profile_level = TRUE;
        }
      }

      if (!have_profile_level) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "video/x-vnd.on2.vp8") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("video/x-vp8",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);

      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "video/mpeg2") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("video/mpeg",
          "width", GST_TYPE_INT_RANGE, 16, 4096,
          "height", GST_TYPE_INT_RANGE, 16, 4096,
          "framerate", GST_TYPE_FRACTION_RANGE,
          0, 1, G_MAXINT, 1,
          "mpegversion", GST_TYPE_INT_RANGE, 1, 2,
          "systemstream", G_TYPE_BOOLEAN, FALSE,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);

      gst_caps_merge_structure (ret, tmp);
    } else {
      GST_WARNING ("Unsupported mimetype '%s'", type->mime);
    }
  }

  return ret;
}

static const gchar *
caps_to_mime (GstCaps * caps)
{
  GstStructure *s;
  const gchar *name;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    return NULL;

  name = gst_structure_get_name (s);

  if (strcmp (name, "video/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion))
      return NULL;

    if (mpegversion == 4)
      return "video/mp4v-es";
    else if (mpegversion == 1 || mpegversion == 2)
      return "video/mpeg2";
  } else if (strcmp (name, "video/x-h263") == 0) {
    return "video/3gpp";
  } else if (strcmp (name, "video/x-h264") == 0) {
    return "video/avc";
  } else if (strcmp (name, "video/x-vp8") == 0) {
    return "video/x-vnd.on2.vp8";
  }

  return NULL;
}

static GstCaps *
create_src_caps (const GstAmcCodecInfo * codec_info)
{
  GstCaps *ret;
  gint i;

  ret = gst_caps_new_empty ();

  for (i = 0; i < codec_info->n_supported_types; i++) {
    const GstAmcCodecType *type = &codec_info->supported_types[i];
    gint j;

    for (j = 0; j < type->n_color_formats; j++) {
      GstVideoFormat format;
      GstCaps *tmp;

      format = gst_amc_color_format_to_video_format (type->color_formats[j]);
      if (format == GST_VIDEO_FORMAT_UNKNOWN) {
        GST_WARNING ("Unknown color format 0x%08x", type->color_formats[j]);
        continue;
      }
      tmp = gst_video_format_new_template_caps (format);
      gst_caps_merge (ret, tmp);
    }
  }

  return ret;
}

static void
gst_amc_video_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstAmcVideoDecClass *videodec_class = GST_AMC_VIDEO_DEC_CLASS (g_class);
  const GstAmcCodecInfo *codec_info;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar *longname;

  codec_info =
      g_type_get_qdata (G_TYPE_FROM_CLASS (g_class), gst_amc_codec_info_quark);
  /* This happens for the base class and abstract subclasses */
  if (!codec_info)
    return;

  videodec_class->codec_info = codec_info;

  /* Add pad templates */
  caps = create_sink_caps (codec_info);
  templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  caps = create_src_caps (codec_info);
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  longname = g_strdup_printf ("Android MediaCodec %s", codec_info->name);
  gst_element_class_set_details_simple (element_class,
      codec_info->name,
      "Codec/Decoder/Video",
      longname, "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
  g_free (longname);
}

static void
gst_amc_video_dec_class_init (GstAmcVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *videodec_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_amc_video_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_change_state);

  videodec_class->start = GST_DEBUG_FUNCPTR (gst_amc_video_dec_start);
  videodec_class->stop = GST_DEBUG_FUNCPTR (gst_amc_video_dec_stop);
  videodec_class->open = GST_DEBUG_FUNCPTR (gst_amc_video_dec_open);
  videodec_class->close = GST_DEBUG_FUNCPTR (gst_amc_video_dec_close);
  videodec_class->reset = GST_DEBUG_FUNCPTR (gst_amc_video_dec_reset);
  videodec_class->set_format = GST_DEBUG_FUNCPTR (gst_amc_video_dec_set_format);
  videodec_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_amc_video_dec_handle_frame);
  videodec_class->finish = GST_DEBUG_FUNCPTR (gst_amc_video_dec_finish);
}

static void
gst_amc_video_dec_init (GstAmcVideoDec * self, GstAmcVideoDecClass * klass)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_amc_video_dec_open (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);
  GstAmcVideoDecClass *klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->codec = gst_amc_codec_new (klass->codec_info->name);
  if (!self->codec)
    return FALSE;
  self->started = FALSE;
  self->flushing = TRUE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_amc_video_dec_close (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (self->codec)
    gst_amc_codec_free (self->codec);
  self->codec = NULL;

  self->started = FALSE;
  self->flushing = TRUE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_amc_video_dec_finalize (GObject * object)
{
  GstAmcVideoDec *self = GST_AMC_VIDEO_DEC (object);

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_amc_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstAmcVideoDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_AMC_VIDEO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_AMC_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->flushing = TRUE;
      gst_amc_codec_flush (self->codec);
      g_mutex_lock (self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (self->drain_cond);
      g_mutex_unlock (self->drain_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

#define MAX_FRAME_DIST_TIME  (5 * GST_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoCodecFrame *
_find_nearest_frame (GstAmcVideoDec * self, GstClockTime reference_timestamp)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoCodecFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;
  GList *frames;

  frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
    guint64 timestamp, diff;

    /* This happens for frames that were just added but
     * which were not passed to the component yet. Ignore
     * them here!
     */
    if (!id)
      continue;

    timestamp = id->timestamp;

    if (timestamp > reference_timestamp)
      diff = timestamp - reference_timestamp;
    else
      diff = reference_timestamp - timestamp;

    if (best == NULL || diff < best_diff) {
      best = tmp;
      best_timestamp = timestamp;
      best_diff = diff;
      best_l = l;
      best_id = id;

      /* For frames without timestamp we simply take the first frame */
      if ((reference_timestamp == 0 && timestamp == 0) || diff == 0)
        break;
    }
  }

  if (best_id) {
    for (l = frames; l && l != best_l; l = l->next) {
      GstVideoCodecFrame *tmp = l->data;
      BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
      guint64 diff_time, diff_frames;

      if (id->timestamp > best_timestamp)
        break;

      if (id->timestamp == 0 || best_timestamp == 0)
        diff_time = 0;
      else
        diff_time = best_timestamp - id->timestamp;
      diff_frames = best->system_frame_number - tmp->system_frame_number;

      if (diff_time > MAX_FRAME_DIST_TIME
          || diff_frames > MAX_FRAME_DIST_FRAMES) {
        finish_frames = g_list_prepend (finish_frames, tmp);
      }
    }
  }

  if (finish_frames) {
    g_warning ("%s: Too old frames, bug in decoder -- please file a bug",
        GST_ELEMENT_NAME (self));
    for (l = finish_frames; l; l = l->next) {
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), l->data);
    }
  }

  if (best)
    gst_video_codec_frame_ref (best);

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}

static gboolean
gst_amc_video_dec_set_src_caps (GstAmcVideoDec * self, GstAmcFormat * format)
{
  GstVideoCodecState *output_state;
  gint color_format, width, height;
  gint stride, slice_height;
  gint crop_left, crop_right;
  gint crop_top, crop_bottom;
  GstVideoFormat gst_format;

  if (!gst_amc_format_get_int (format, "color-format", &color_format) ||
      !gst_amc_format_get_int (format, "width", &width) ||
      !gst_amc_format_get_int (format, "height", &height)) {
    GST_ERROR_OBJECT (self, "Failed to get output format metadata");
    return FALSE;
  }

  if (!gst_amc_format_get_int (format, "stride", &stride) ||
      !gst_amc_format_get_int (format, "slice-height", &slice_height)) {
    GST_ERROR_OBJECT (self, "Failed to get stride and slice-height");
    return FALSE;
  }

  if (!gst_amc_format_get_int (format, "crop-left", &crop_left) ||
      !gst_amc_format_get_int (format, "crop-right", &crop_right) ||
      !gst_amc_format_get_int (format, "crop-top", &crop_top) ||
      !gst_amc_format_get_int (format, "crop-bottom", &crop_bottom)) {
    GST_ERROR_OBJECT (self, "Failed to get crop rectangle");
    return FALSE;
  }

  if (width == 0 || height == 0) {
    GST_ERROR_OBJECT (self, "Height or width not set");
    return FALSE;
  }

  if (crop_bottom)
    height = height - (height - crop_bottom - 1);
  if (crop_top)
    height = height - crop_top;

  if (crop_right)
    width = width - (width - crop_right - 1);
  if (crop_left)
    width = width - crop_left;

  gst_format = gst_amc_color_format_to_video_format (color_format);
  if (gst_format == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Unknown color format 0x%08x", color_format);
    return FALSE;
  }

  output_state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      gst_format, width, height, self->input_state);

  self->format = gst_format;
  self->color_format = color_format;
  self->height = height;
  self->width = width;
  self->stride = stride;
  self->slice_height = slice_height;
  self->crop_left = crop_left;
  self->crop_right = crop_right;
  self->crop_top = crop_top;
  self->crop_bottom = crop_bottom;

  gst_video_codec_state_unref (output_state);
  self->input_state_changed = FALSE;

  return TRUE;
}

/* The weird handling of cropping, alignment and everything is taken from
 * platform/frameworks/media/libstagefright/colorconversion/ColorConversion.cpp
 */
static gboolean
gst_amc_video_dec_fill_buffer (GstAmcVideoDec * self, gint idx,
    const GstAmcBufferInfo * buffer_info, GstBuffer * outbuf)
{
  GstAmcVideoDecClass *klass = GST_AMC_VIDEO_DEC_GET_CLASS (self);
  GstAmcBuffer *buf = &self->output_buffers[idx];
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
  GstVideoInfo *info = &state->info;
  gboolean ret = FALSE;

  if (idx >= self->n_output_buffers) {
    GST_ERROR_OBJECT (self, "Invalid output buffer index %d of %d",
        idx, self->n_output_buffers);
    goto done;
  }

  /* Same video format */
  if (buffer_info->size == GST_BUFFER_SIZE (outbuf)) {
    GST_DEBUG_OBJECT (self, "Buffer sizes equal, doing fast copy");
    orc_memcpy (GST_BUFFER_DATA (outbuf), buf->data + buffer_info->offset,
        buffer_info->size);
    ret = TRUE;
    goto done;
  }

  GST_DEBUG_OBJECT (self,
      "Sizes not equal (%d vs %d), doing slow line-by-line copying",
      buffer_info->size, GST_BUFFER_SIZE (outbuf));

  /* Different video format, try to convert */
  switch (self->color_format) {
    case COLOR_FormatYUV420Planar:{
      gint i, j, height;
      guint8 *src, *dest;
      gint stride, slice_height;
      gint src_stride, dest_stride;
      gint row_length;

      stride = self->stride;
      if (stride == 0) {
        GST_ERROR_OBJECT (self, "Stride not set");
        goto done;
      }

      slice_height = self->slice_height;
      if (slice_height == 0) {
        /* NVidia Tegra 3 on Nexus 7 does not set this */
        if (g_str_has_prefix (klass->codec_info->name, "OMX.Nvidia.")) {
          slice_height = GST_ROUND_UP_32 (self->height);
        } else {
          GST_ERROR_OBJECT (self, "Slice height not set");
          goto done;
        }
      }

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        } else {
          src_stride = (stride + 1) / 2;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        }

        src = buf->data + buffer_info->offset;

        if (i == 0) {
          src += self->crop_top * stride;
          src += self->crop_left;
          row_length = self->width;
        } else if (i > 0) {
          src += slice_height * stride;
          src += self->crop_top * src_stride;
          src += self->crop_left / 2;
          row_length = (self->width + 1) / 2;
        }
        if (i == 2)
          src += ((slice_height + 1) / 2) * ((stride + 1) / 2);

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case COLOR_TI_FormatYUV420PackedSemiPlanar:
    case COLOR_TI_FormatYUV420PackedSemiPlanarInterlaced:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;
      gint row_length;

      /* This should always be set */
      if (self->stride == 0 || self->slice_height == 0) {
        GST_ERROR_OBJECT (self, "Stride or slice height not set");
        goto done;
      }

      /* FIXME: This does not work for odd widths or heights
       * but might as well be a bug in the codec */
      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = self->stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        } else {
          src_stride = GST_ROUND_UP_2 (self->stride);
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        }

        src = buf->data + buffer_info->offset;
        if (i == 0) {
          row_length = self->width;
        } else if (i == 1) {
          src += (self->slice_height - self->crop_top / 2) * self->stride;
          row_length = GST_ROUND_UP_2 (self->width);
        }

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case COLOR_QCOM_FormatYUV420SemiPlanar:
    case COLOR_FormatYUV420SemiPlanar:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;
      gint row_length;

      /* This should always be set */
      if (self->stride == 0 || self->slice_height == 0) {
        GST_ERROR_OBJECT (self, "Stride or slice height not set");
        goto done;
      }

      /* FIXME: This is untested! */

      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = self->stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        } else {
          src_stride = self->stride;
          dest_stride = GST_VIDEO_INFO_COMP_STRIDE (info, i);
        }

        src = buf->data + buffer_info->offset;
        if (i == 0) {
          src += self->crop_top * self->stride;
          src += self->crop_left;
          row_length = self->width;
        } else if (i == 1) {
          src += self->slice_height * self->stride;
          src += self->crop_top * self->stride;
          src += self->crop_left;
          row_length = self->width;
        }

        dest = GST_BUFFER_DATA (outbuf) + GST_VIDEO_INFO_COMP_OFFSET (info, i);
        height = GST_VIDEO_INFO_COMP_HEIGHT (info, i);

        for (j = 0; j < height; j++) {
          orc_memcpy (dest, src, row_length);
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported color format %d",
          self->color_format);
      goto done;
      break;
  }

done:
  gst_video_codec_state_unref (state);
  return ret;
}

static void
gst_amc_video_dec_loop (GstAmcVideoDec * self)
{
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstClockTimeDiff deadline;
  gboolean is_eos;
  GstAmcBufferInfo buffer_info;
  gint idx;

  GST_VIDEO_DECODER_STREAM_LOCK (self);

retry:
  /*if (self->input_state_changed) {
     idx = INFO_OUTPUT_FORMAT_CHANGED;
     } else { */
  GST_DEBUG_OBJECT (self, "Waiting for available output buffer");
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  /* Wait at most 100ms here, some codecs don't fail dequeueing if
   * the codec is flushing, causing deadlocks during shutdown */
  idx = gst_amc_codec_dequeue_output_buffer (self->codec, &buffer_info, 100000);
  GST_VIDEO_DECODER_STREAM_LOCK (self);
  /*} */

  if (idx < 0) {
    if (self->flushing || self->downstream_flow_ret == GST_FLOW_WRONG_STATE)
      goto flushing;

    switch (idx) {
      case INFO_OUTPUT_BUFFERS_CHANGED:{
        GST_DEBUG_OBJECT (self, "Output buffers have changed");
        if (self->output_buffers)
          gst_amc_codec_free_buffers (self->output_buffers,
              self->n_output_buffers);
        self->output_buffers =
            gst_amc_codec_get_output_buffers (self->codec,
            &self->n_output_buffers);
        if (!self->output_buffers)
          goto get_output_buffers_error;
        break;
      }
      case INFO_OUTPUT_FORMAT_CHANGED:{
        GstAmcFormat *format;
        gchar *format_string;

        GST_DEBUG_OBJECT (self, "Output format has changed");

        format = gst_amc_codec_get_output_format (self->codec);
        if (!format)
          goto format_error;

        format_string = gst_amc_format_to_string (format);
        GST_DEBUG_OBJECT (self, "Got new output format: %s", format_string);
        g_free (format_string);

        if (!gst_amc_video_dec_set_src_caps (self, format)) {
          gst_amc_format_free (format);
          goto format_error;
        }
        gst_amc_format_free (format);

        if (self->output_buffers)
          gst_amc_codec_free_buffers (self->output_buffers,
              self->n_output_buffers);
        self->output_buffers =
            gst_amc_codec_get_output_buffers (self->codec,
            &self->n_output_buffers);
        if (!self->output_buffers)
          goto get_output_buffers_error;

        goto retry;
        break;
      }
      case INFO_TRY_AGAIN_LATER:
        GST_DEBUG_OBJECT (self, "Dequeueing output buffer timed out");
        goto retry;
        break;
      case G_MININT:
        GST_ERROR_OBJECT (self, "Failure dequeueing input buffer");
        goto dequeue_error;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    goto retry;
  }

  GST_DEBUG_OBJECT (self,
      "Got output buffer at index %d: size %d time %" G_GINT64_FORMAT
      " flags 0x%08x", idx, buffer_info.size, buffer_info.presentation_time_us,
      buffer_info.flags);

  frame =
      _find_nearest_frame (self,
      gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND, 1));

  is_eos = ! !(buffer_info.flags & BUFFER_FLAG_END_OF_STREAM);

  if (frame
      && (deadline =
          gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self),
              frame)) < 0) {
    GST_WARNING_OBJECT (self,
        "Frame is too late, dropping (deadline %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (-deadline));
    flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
  } else if (!frame && buffer_info.size > 0) {
    GstBuffer *outbuf;

    /* This sometimes happens at EOS or if the input is not properly framed,
     * let's handle it gracefully by allocating a new buffer for the current
     * caps and filling it
     */
    GST_ERROR_OBJECT (self, "No corresponding frame found");

    outbuf = gst_video_decoder_alloc_output_buffer (GST_VIDEO_DECODER (self));

    if (!gst_amc_video_dec_fill_buffer (self, idx, &buffer_info, outbuf)) {
      gst_buffer_unref (outbuf);
      if (!gst_amc_codec_release_output_buffer (self->codec, idx))
        GST_ERROR_OBJECT (self, "Failed to release output buffer index %d",
            idx);
      goto invalid_buffer;
    }

    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (buffer_info.presentation_time_us, GST_USECOND,
        1);
    flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
  } else if (buffer_info.size > 0) {
    if ((flow_ret = gst_video_decoder_alloc_output_frame (GST_VIDEO_DECODER
                (self), frame)) != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Failed to allocate buffer");
      goto flow_error;
    }

    if (!gst_amc_video_dec_fill_buffer (self, idx, &buffer_info,
            frame->output_buffer)) {
      gst_buffer_replace (&frame->output_buffer, NULL);
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
      if (!gst_amc_codec_release_output_buffer (self->codec, idx))
        GST_ERROR_OBJECT (self, "Failed to release output buffer index %d",
            idx);
      goto invalid_buffer;
    }

    flow_ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
  } else if (frame != NULL) {
    flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
  }

  if (!gst_amc_codec_release_output_buffer (self->codec, idx))
    goto failed_release;

  if (is_eos || flow_ret == GST_FLOW_UNEXPECTED) {
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    g_mutex_lock (self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (self->drain_cond);
    } else if (flow_ret == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_UNEXPECTED;
    }
    g_mutex_unlock (self->drain_lock);
    GST_VIDEO_DECODER_STREAM_LOCK (self);
  } else {
    GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));
  }

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  return;

dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to dequeue output buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

get_output_buffers_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to get output buffers"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

format_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to handle format"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
failed_release:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to release output buffer index %d", idx));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "EOS");
      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else
        if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));
      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    }
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_amc_video_dec_start (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self;

  self = GST_AMC_VIDEO_DEC (decoder);
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  self->flushing = TRUE;

  return TRUE;
}

static gboolean
gst_amc_video_dec_stop (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self;

  self = GST_AMC_VIDEO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Stopping decoder");
  self->flushing = TRUE;
  if (self->started) {
    gst_amc_codec_flush (self->codec);
    gst_amc_codec_stop (self->codec);
    self->started = FALSE;
    if (self->input_buffers)
      gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
    self->input_buffers = NULL;
    if (self->output_buffers)
      gst_amc_codec_free_buffers (self->output_buffers, self->n_output_buffers);
    self->output_buffers = NULL;
  }
  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));

  self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
  self->eos = FALSE;
  g_mutex_lock (self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (self->drain_cond);
  g_mutex_unlock (self->drain_lock);
  gst_buffer_replace (&self->codec_data, NULL);
  GST_DEBUG_OBJECT (self, "Stopped decoder");
  return TRUE;
}

static gboolean
gst_amc_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstAmcVideoDec *self;
  GstAmcFormat *format;
  const gchar *mime;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  gchar *format_string;

  self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_format_change |= self->width != state->info.width;
  is_format_change |= self->height != state->info.height;
  is_format_change |= (self->codec_data != state->codec_data);

  needs_disable = self->started;

  /* If the component is not started and a real format change happens
   * we have to restart the component. If no real format change
   * happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    /* Framerate or something minor changed */
    self->input_state_changed = TRUE;
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    gst_amc_video_dec_drain (self);
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    gst_amc_video_dec_stop (GST_VIDEO_DECODER (self));
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    gst_amc_video_dec_close (GST_VIDEO_DECODER (self));
    if (!gst_amc_video_dec_open (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to open codec again");
      return FALSE;
    }

    if (!gst_amc_video_dec_start (GST_VIDEO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to start codec again");
    }
  }
  /* srcpad task is not running at this point */

  gst_buffer_replace (&self->codec_data, state->codec_data);

  mime = caps_to_mime (state->caps);
  if (!mime) {
    GST_ERROR_OBJECT (self, "Failed to convert caps to mime");
    return FALSE;
  }

  format =
      gst_amc_format_new_video (mime, state->info.width, state->info.height);
  if (!format) {
    GST_ERROR_OBJECT (self, "Failed to create video format");
    return FALSE;
  }

  /* FIXME: This buffer needs to be valid until the codec is stopped again */
  if (self->codec_data)
    gst_amc_format_set_buffer (format, "csd-0", self->codec_data);

  format_string = gst_amc_format_to_string (format);
  GST_DEBUG_OBJECT (self, "Configuring codec with format: %s", format_string);
  g_free (format_string);

  if (!gst_amc_codec_configure (self->codec, format, 0)) {
    GST_ERROR_OBJECT (self, "Failed to configure codec");
    return FALSE;
  }

  gst_amc_format_free (format);

  if (!gst_amc_codec_start (self->codec)) {
    GST_ERROR_OBJECT (self, "Failed to start codec");
    return FALSE;
  }

  if (self->input_buffers)
    gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
  self->input_buffers =
      gst_amc_codec_get_input_buffers (self->codec, &self->n_input_buffers);
  if (!self->input_buffers) {
    GST_ERROR_OBJECT (self, "Failed to get input buffers");
    return FALSE;
  }

  self->started = TRUE;
  self->input_state_changed = TRUE;

  /* Start the srcpad loop again */
  self->flushing = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_amc_video_dec_loop, decoder);

  return TRUE;
}

static gboolean
gst_amc_video_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstAmcVideoDec *self;

  self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Codec not started yet");
    return TRUE;
  }

  self->flushing = TRUE;
  gst_amc_codec_flush (self->codec);

  /* Wait until the srcpad loop is finished,
   * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_VIDEO_DECODER_STREAM_LOCK (self);
  self->flushing = FALSE;

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_amc_video_dec_loop, decoder);

  GST_DEBUG_OBJECT (self, "Reset decoder");

  return TRUE;
}

static GstFlowReturn
gst_amc_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstAmcVideoDec *self;
  gint idx;
  GstAmcBuffer *buf;
  GstAmcBufferInfo buffer_info;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_AMC_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (!self->started) {
    GST_ERROR_OBJECT (self, "Codec not started yet");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_UNEXPECTED;
  }

  if (self->flushing)
    goto flushing;

  if (self->downstream_flow_ret != GST_FLOW_OK)
    goto downstream_error;

  timestamp = frame->pts;
  duration = frame->duration;

  while (offset < GST_BUFFER_SIZE (frame->input_buffer)) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    /* Wait at most 100ms here, some codecs don't fail dequeueing if
     * the codec is flushing, causing deadlocks during shutdown */
    idx = gst_amc_codec_dequeue_input_buffer (self->codec, 100000);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

    if (idx < 0) {
      if (self->flushing)
        goto flushing;
      switch (idx) {
        case INFO_TRY_AGAIN_LATER:
          GST_DEBUG_OBJECT (self, "Dequeueing input buffer timed out");
          continue;             /* next try */
          break;
        case G_MININT:
          GST_ERROR_OBJECT (self, "Failed to dequeue input buffer");
          goto dequeue_error;
        default:
          g_assert_not_reached ();
          break;
      }

      continue;
    }

    if (idx >= self->n_input_buffers)
      goto invalid_buffer_index;

    if (self->flushing)
      goto flushing;

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      memset (&buffer_info, 0, sizeof (buffer_info));
      gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info);
      goto downstream_error;
    }

    /* Now handle the frame */

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf = &self->input_buffers[idx];

    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.offset = 0;
    buffer_info.size =
        MIN (GST_BUFFER_SIZE (frame->input_buffer) - offset, buf->size);

    orc_memcpy (buf->data, GST_BUFFER_DATA (frame->input_buffer) + offset,
        buffer_info.size);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration,
          GST_BUFFER_SIZE (frame->input_buffer));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buffer_info.presentation_time_us =
          gst_util_uint64_scale (timestamp + timestamp_offset, 1, GST_USECOND);
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE)
      self->last_upstream_ts += duration;

    if (offset == 0) {
      BufferIdentification *id =
          buffer_identification_new (timestamp + timestamp_offset);
      if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
        buffer_info.flags |= BUFFER_FLAG_SYNC_FRAME;
      gst_video_codec_frame_set_user_data (frame, id,
          (GDestroyNotify) buffer_identification_free);
    }

    offset += buffer_info.size;
    GST_DEBUG_OBJECT (self,
        "Queueing buffer %d: size %d time %" G_GINT64_FORMAT " flags 0x%08x",
        idx, buffer_info.size, buffer_info.presentation_time_us,
        buffer_info.flags);
    if (!gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info))
      goto queue_error;
  }

  gst_video_codec_frame_unref (frame);

  return self->downstream_flow_ret;

downstream_error:
  {
    GST_ERROR_OBJECT (self, "Downstream returned %s",
        gst_flow_get_name (self->downstream_flow_ret));
    gst_video_codec_frame_unref (frame);
    return self->downstream_flow_ret;
  }
invalid_buffer_index:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Invalid input buffer index %d of %d", idx, self->n_input_buffers));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to dequeue input buffer"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
queue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to queue input buffer"));
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- returning WRONG_STATE");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_WRONG_STATE;
  }
}

static GstFlowReturn
gst_amc_video_dec_finish (GstVideoDecoder * decoder)
{
  GstAmcVideoDec *self;
  gint idx;

  self = GST_AMC_VIDEO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Component is already EOS");
    return GST_VIDEO_DECODER_FLOW_DROPPED;
  }
  self->eos = TRUE;

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port.
   * Wait at most 0.5s here. */
  idx = gst_amc_codec_dequeue_input_buffer (self->codec, 500000);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  if (idx >= 0 && idx < self->n_input_buffers) {
    GstAmcBufferInfo buffer_info;

    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.size = 0;
    buffer_info.presentation_time_us =
        gst_util_uint64_scale (self->last_upstream_ts, 1, GST_USECOND);
    buffer_info.flags |= BUFFER_FLAG_END_OF_STREAM;

    if (gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info))
      GST_DEBUG_OBJECT (self, "Sent EOS to the codec");
    else
      GST_ERROR_OBJECT (self, "Failed to send EOS to the codec");
  } else if (idx >= self->n_input_buffers) {
    GST_ERROR_OBJECT (self, "Invalid input buffer index %d of %d",
        idx, self->n_input_buffers);
  } else {
    GST_ERROR_OBJECT (self, "Failed to dequeue input buffer for EOS: %d", idx);
  }

  return GST_VIDEO_DECODER_FLOW_DROPPED;
}

static GstFlowReturn
gst_amc_video_dec_drain (GstAmcVideoDec * self)
{
  GstFlowReturn ret;
  gint idx;

  GST_DEBUG_OBJECT (self, "Draining codec");
  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Codec not started yet");
    return GST_FLOW_OK;
  }

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Codec is EOS already");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port.
   * Wait at most 0.5s here. */
  idx = gst_amc_codec_dequeue_input_buffer (self->codec, 500000);
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  if (idx >= 0 && idx < self->n_input_buffers) {
    GstAmcBufferInfo buffer_info;

    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    g_mutex_lock (self->drain_lock);
    self->draining = TRUE;

    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.size = 0;
    buffer_info.presentation_time_us =
        gst_util_uint64_scale (self->last_upstream_ts, 1, GST_USECOND);
    buffer_info.flags |= BUFFER_FLAG_END_OF_STREAM;

    if (gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info)) {
      GST_DEBUG_OBJECT (self, "Waiting until codec is drained");
      g_cond_wait (self->drain_cond, self->drain_lock);
      GST_DEBUG_OBJECT (self, "Drained codec");
      ret = GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (self, "Failed to queue input buffer");
      ret = GST_FLOW_ERROR;
    }

    g_mutex_unlock (self->drain_lock);
    GST_VIDEO_DECODER_STREAM_LOCK (self);
  } else if (idx >= self->n_input_buffers) {
    GST_ERROR_OBJECT (self, "Invalid input buffer index %d of %d",
        idx, self->n_input_buffers);
    ret = GST_FLOW_ERROR;
  } else {
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for EOS: %d", idx);
    ret = GST_FLOW_ERROR;
  }

  return ret;
}
