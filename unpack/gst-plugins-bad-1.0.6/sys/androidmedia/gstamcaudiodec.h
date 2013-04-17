/*
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

#ifndef __GST_AMC_AUDIO_DEC_H__
#define __GST_AMC_AUDIO_DEC_H__

#include <gst/gst.h>
#include <gst/audio/multichannel.h>
#include <gst/audio/gstaudiodecoder.h>

#include "gstamc.h"

G_BEGIN_DECLS

#define GST_TYPE_AMC_AUDIO_DEC \
  (gst_amc_audio_dec_get_type())
#define GST_AMC_AUDIO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AMC_AUDIO_DEC,GstAmcAudioDec))
#define GST_AMC_AUDIO_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AMC_AUDIO_DEC,GstAmcAudioDecClass))
#define GST_AMC_AUDIO_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_AMC_AUDIO_DEC,GstAmcAudioDecClass))
#define GST_IS_AMC_AUDIO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AMC_AUDIO_DEC))
#define GST_IS_AMC_AUDIO_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AMC_AUDIO_DEC))

typedef struct _GstAmcAudioDec GstAmcAudioDec;
typedef struct _GstAmcAudioDecClass GstAmcAudioDecClass;

struct _GstAmcAudioDec
{
  GstAudioDecoder parent;

  /* < private > */
  GstAmcCodec *codec;
  GstAmcBuffer *input_buffers, *output_buffers;
  gsize n_input_buffers, n_output_buffers;

  GstCaps *input_caps;
  GList *codec_datas;
  gboolean input_caps_changed;

  /* Output format of the codec */
  gint channels, rate;
  GstAudioChannelPosition *positions;

  GstBuffer *codec_data;
  /* TRUE if the component is configured and saw
   * the first buffer */
  gboolean started;
  gboolean flushing;

  GstClockTime last_upstream_ts;

  /* Draining state */
  GMutex *drain_lock;
  GCond *drain_cond;
  /* TRUE if EOS buffers shouldn't be forwarded */
  gboolean draining;

  /* TRUE if upstream is EOS */
  gboolean eos;

  GstFlowReturn downstream_flow_ret;

  /* Output buffers counter */
  gint n_buffers;
};

struct _GstAmcAudioDecClass
{
  GstAudioDecoderClass parent_class;

  const GstAmcCodecInfo *codec_info;
};

GType gst_amc_audio_dec_get_type (void);

G_END_DECLS

#endif /* __GST_AMC_AUDIO_DEC_H__ */
