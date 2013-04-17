/* GStreamer
 *
 * Copyright (C) 2001-2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *               2009 Texas Instruments, Inc - http://www.ti.com/
 *
 * gstv4l2bufferpool.c V4L2 buffer pool class
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
#  include <config.h>
#endif

#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

#include "gst/video/video.h"
#include "gst/video/gstvideometa.h"
#include "gst/video/gstvideopool.h"

#include <gstv4l2bufferpool.h>

#include "gstv4l2src.h"
#include "gstv4l2sink.h"
#include "v4l2_calls.h"
#include "gst/gst-i18n-plugin.h"
#include <gst/glib-compat-private.h>

/* videodev2.h is not versioned and we can't easily check for the presence
 * of enum values at compile time, but the V4L2_CAP_VIDEO_OUTPUT_OVERLAY define
 * was added in the same commit as V4L2_FIELD_INTERLACED_{TB,BT} (b2787845) */
#ifndef V4L2_CAP_VIDEO_OUTPUT_OVERLAY
#define V4L2_FIELD_INTERLACED_TB 8
#define V4L2_FIELD_INTERLACED_BT 9
#endif


GST_DEBUG_CATEGORY_EXTERN (v4l2_debug);
#define GST_CAT_DEFAULT v4l2_debug

/*
 * GstV4l2Buffer:
 */
GType
gst_v4l2_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstV4l2MetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_v4l2_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_v4l2_meta_api_get_type (), "GstV4l2Meta",
        sizeof (GstV4l2Meta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

/*
 * GstV4l2BufferPool:
 */
#define gst_v4l2_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstV4l2BufferPool, gst_v4l2_buffer_pool, GST_TYPE_BUFFER_POOL);

static void gst_v4l2_buffer_pool_release_buffer (GstBufferPool * bpool,
    GstBuffer * buffer);

static void
gst_v4l2_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj;

  obj = pool->obj;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      break;
    case GST_V4L2_IO_MMAP:
    {
      GstV4l2Meta *meta;
      gint index;

      meta = GST_V4L2_META_GET (buffer);
      g_assert (meta != NULL);

      index = meta->vbuffer.index;
      GST_LOG_OBJECT (pool,
          "unmap buffer %p idx %d (data %p, len %u)", buffer,
          index, meta->mem, meta->vbuffer.length);

      v4l2_munmap (meta->mem, meta->vbuffer.length);
      pool->buffers[index] = NULL;
      break;
    }
    case GST_V4L2_IO_USERPTR:
    default:
      g_assert_not_reached ();
      break;
  }
  gst_buffer_unref (buffer);
}

static GstFlowReturn
gst_v4l2_buffer_pool_alloc_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstBuffer *newbuf;
  GstV4l2Meta *meta;
  GstV4l2Object *obj;
  GstVideoInfo *info;
  guint index;

  obj = pool->obj;
  info = &obj->info;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
    {
      newbuf =
          gst_buffer_new_allocate (pool->allocator, pool->size, &pool->params);
      break;
    }
    case GST_V4L2_IO_MMAP:
    {
      newbuf = gst_buffer_new ();
      meta = GST_V4L2_META_ADD (newbuf);

      index = pool->num_allocated;

      GST_LOG_OBJECT (pool, "creating buffer %u, %p", index, newbuf);

      meta->vbuffer.index = index;
      meta->vbuffer.type = obj->type;
      meta->vbuffer.memory = V4L2_MEMORY_MMAP;

      if (v4l2_ioctl (pool->video_fd, VIDIOC_QUERYBUF, &meta->vbuffer) < 0)
        goto querybuf_failed;

      GST_LOG_OBJECT (pool, "  index:     %u", meta->vbuffer.index);
      GST_LOG_OBJECT (pool, "  type:      %d", meta->vbuffer.type);
      GST_LOG_OBJECT (pool, "  bytesused: %u", meta->vbuffer.bytesused);
      GST_LOG_OBJECT (pool, "  flags:     %08x", meta->vbuffer.flags);
      GST_LOG_OBJECT (pool, "  field:     %d", meta->vbuffer.field);
      GST_LOG_OBJECT (pool, "  memory:    %d", meta->vbuffer.memory);
      if (meta->vbuffer.memory == V4L2_MEMORY_MMAP)
        GST_LOG_OBJECT (pool, "  MMAP offset:  %u", meta->vbuffer.m.offset);
      GST_LOG_OBJECT (pool, "  length:    %u", meta->vbuffer.length);

      meta->mem = v4l2_mmap (0, meta->vbuffer.length,
          PROT_READ | PROT_WRITE, MAP_SHARED, pool->video_fd,
          meta->vbuffer.m.offset);
      if (meta->mem == MAP_FAILED)
        goto mmap_failed;

      gst_buffer_append_memory (newbuf,
          gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE,
              meta->mem, meta->vbuffer.length, 0, meta->vbuffer.length, NULL,
              NULL));

      /* add metadata to raw video buffers */
      if (pool->add_videometa && info->finfo) {
        const GstVideoFormatInfo *finfo = info->finfo;
        gsize offset[GST_VIDEO_MAX_PLANES];
        gint width, height, n_planes, offs, i, stride[GST_VIDEO_MAX_PLANES];

        width = GST_VIDEO_INFO_WIDTH (info);
        height = GST_VIDEO_INFO_HEIGHT (info);
        n_planes = GST_VIDEO_INFO_N_PLANES (info);

        GST_DEBUG_OBJECT (pool, "adding video meta, bytesperline %d",
            obj->bytesperline);

        offs = 0;
        for (i = 0; i < n_planes; i++) {
          offset[i] = offs;
          stride[i] =
              GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (finfo, i, obj->bytesperline);

          offs +=
              stride[i] * GST_VIDEO_FORMAT_INFO_SCALE_HEIGHT (finfo, i, height);
        }
        gst_buffer_add_video_meta_full (newbuf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (info), width, height, n_planes,
            offset, stride);
      }
      break;
    }
    case GST_V4L2_IO_USERPTR:
    default:
      newbuf = NULL;
      g_assert_not_reached ();
  }

  pool->num_allocated++;

  *buffer = newbuf;

  return GST_FLOW_OK;

  /* ERRORS */
querybuf_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed QUERYBUF: %s", g_strerror (errnosave));
    gst_buffer_unref (newbuf);
    errno = errnosave;
    return GST_FLOW_ERROR;
  }
mmap_failed:
  {
    gint errnosave = errno;

    GST_WARNING ("Failed to mmap: %s", g_strerror (errnosave));
    gst_buffer_unref (newbuf);
    errno = errnosave;
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_v4l2_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  GstCaps *caps;
  guint size, min_buffers, max_buffers, num_buffers, copy_threshold;
  GstAllocator *allocator;
  GstAllocationParams params;
  struct v4l2_requestbuffers breq;

  GST_DEBUG_OBJECT (pool, "set config");

  pool->add_videometa =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  if (!pool->add_videometa &&
      GST_VIDEO_INFO_FORMAT (&obj->info) != GST_VIDEO_FORMAT_ENCODED) {
    gint stride;

    /* we don't have video metadata, and we are not dealing with raw video,
     * see if the strides are compatible */
    stride = GST_VIDEO_INFO_PLANE_STRIDE (&obj->info, 0);

    GST_DEBUG_OBJECT (pool, "no videometadata, checking strides %d and %u",
        stride, obj->bytesperline);

    if (stride != obj->bytesperline)
      goto missing_video_api;
  }

  /* parse the config and keep around */
  if (!gst_buffer_pool_config_get_params (config, &caps, &size, &min_buffers,
          &max_buffers))
    goto wrong_config;

  if (!gst_buffer_pool_config_get_allocator (config, &allocator, &params))
    goto wrong_config;

  GST_DEBUG_OBJECT (pool, "config %" GST_PTR_FORMAT, config);

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      /* we preallocate 1 buffer, this value also instructs the latency
       * calculation to have 1 frame latency max */
      num_buffers = 1;
      copy_threshold = 0;
      break;
    case GST_V4L2_IO_MMAP:
    {
      /* request a reasonable number of buffers when no max specified. We will
       * copy when we run out of buffers */
      if (max_buffers == 0)
        num_buffers = 4;
      else
        num_buffers = max_buffers;

      /* first, lets request buffers, and see how many we can get: */
      GST_DEBUG_OBJECT (pool, "starting, requesting %d MMAP buffers",
          num_buffers);

      memset (&breq, 0, sizeof (struct v4l2_requestbuffers));
      breq.type = obj->type;
      breq.count = num_buffers;
      breq.memory = V4L2_MEMORY_MMAP;

      if (v4l2_ioctl (pool->video_fd, VIDIOC_REQBUFS, &breq) < 0)
        goto reqbufs_failed;

      GST_LOG_OBJECT (pool, " count:  %u", breq.count);
      GST_LOG_OBJECT (pool, " type:   %d", breq.type);
      GST_LOG_OBJECT (pool, " memory: %d", breq.memory);

      if (breq.count < GST_V4L2_MIN_BUFFERS)
        goto no_buffers;

      if (num_buffers != breq.count) {
        GST_WARNING_OBJECT (pool, "using %u buffers instead", breq.count);
        num_buffers = breq.count;
      }
      /* update min buffers with the amount of buffers we just reserved. We need
       * to configure this value in the bufferpool so that the default start
       * implementation calls our allocate function */
      min_buffers = breq.count;

      if (max_buffers == 0 || num_buffers < max_buffers) {
        /* if we are asked to provide more buffers than we have allocated, start
         * copying buffers when we only have 2 buffers left in the pool */
        copy_threshold = 2;
      } else {
        /* we are certain that we have enough buffers so we don't need to
         * copy */
        copy_threshold = 0;
      }
      break;
    }
    case GST_V4L2_IO_USERPTR:
    default:
      num_buffers = 0;
      copy_threshold = 0;
      g_assert_not_reached ();
      break;
  }

  pool->size = size;
  pool->num_buffers = num_buffers;
  pool->copy_threshold = copy_threshold;
  if (pool->allocator)
    gst_object_unref (pool->allocator);
  if ((pool->allocator = allocator))
    gst_object_ref (allocator);
  pool->params = params;

  gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
      max_buffers);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);

  /* ERRORS */
missing_video_api:
  {
    GST_ERROR_OBJECT (pool, "missing GstMetaVideo API in config, "
        "default stride: %d, wanted stride %u",
        GST_VIDEO_INFO_PLANE_STRIDE (&obj->info, 0), obj->bytesperline);
    return FALSE;
  }
wrong_config:
  {
    GST_ERROR_OBJECT (pool, "invalid config %" GST_PTR_FORMAT, config);
    return FALSE;
  }
reqbufs_failed:
  {
    GST_ERROR_OBJECT (pool,
        "error requesting %d buffers: %s", num_buffers, g_strerror (errno));
    return FALSE;
  }
no_buffers:
  {
    GST_ERROR_OBJECT (pool,
        "we received %d from device '%s', we want at least %d",
        breq.count, obj->videodev, GST_V4L2_MIN_BUFFERS);
    return FALSE;
  }
}

static gboolean
start_streaming (GstV4l2BufferPool * pool)
{
  GstV4l2Object *obj = pool->obj;

  switch (obj->mode) {
    case GST_V4L2_IO_RW:
      break;
    case GST_V4L2_IO_MMAP:
    case GST_V4L2_IO_USERPTR:
      GST_DEBUG_OBJECT (pool, "STREAMON");
      if (v4l2_ioctl (pool->video_fd, VIDIOC_STREAMON, &obj->type) < 0)
        goto start_failed;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  pool->streaming = TRUE;

  return TRUE;

  /* ERRORS */
start_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMON %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static gboolean
gst_v4l2_buffer_pool_start (GstBufferPool * bpool)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  pool->obj = obj;
  pool->buffers = g_new0 (GstBuffer *, pool->num_buffers);
  pool->num_allocated = 0;

  /* now, allocate the buffers: */
  if (!GST_BUFFER_POOL_CLASS (parent_class)->start (bpool))
    goto start_failed;

  /* we can start capturing now, we wait for the playback case until we queued
   * the first buffer */
  if (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
    if (!start_streaming (pool))
      goto start_failed;

  gst_poll_set_flushing (obj->poll, FALSE);

  return TRUE;

  /* ERRORS */
start_failed:
  {
    GST_ERROR_OBJECT (pool, "failed to start streaming");
    return FALSE;
  }
}

static gboolean
gst_v4l2_buffer_pool_stop (GstBufferPool * bpool)
{
  gboolean ret;
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;
  guint n;

  GST_DEBUG_OBJECT (pool, "stopping pool");

  gst_poll_set_flushing (obj->poll, TRUE);

  if (pool->streaming) {
    switch (obj->mode) {
      case GST_V4L2_IO_RW:
        break;
      case GST_V4L2_IO_MMAP:
      case GST_V4L2_IO_USERPTR:
        /* we actually need to sync on all queued buffers but not
         * on the non-queued ones */
        GST_DEBUG_OBJECT (pool, "STREAMOFF");
        if (v4l2_ioctl (pool->video_fd, VIDIOC_STREAMOFF, &obj->type) < 0)
          goto stop_failed;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
    pool->streaming = FALSE;
  }

  /* first free the buffers in the queue */
  ret = GST_BUFFER_POOL_CLASS (parent_class)->stop (bpool);

  /* then free the remaining buffers */
  for (n = 0; n < pool->num_queued; n++) {
    gst_v4l2_buffer_pool_free_buffer (bpool, pool->buffers[n]);
  }
  pool->num_queued = 0;
  g_free (pool->buffers);
  pool->buffers = NULL;

  return ret;

  /* ERRORS */
stop_failed:
  {
    GST_ERROR_OBJECT (pool, "error with STREAMOFF %d (%s)", errno,
        g_strerror (errno));
    return FALSE;
  }
}

static GstFlowReturn
gst_v4l2_object_poll (GstV4l2Object * v4l2object)
{
  gint ret;

  if (v4l2object->can_poll_device) {
    GST_LOG_OBJECT (v4l2object->element, "polling device");
    ret = gst_poll_wait (v4l2object->poll, GST_CLOCK_TIME_NONE);
    if (G_UNLIKELY (ret < 0)) {
      if (errno == EBUSY)
        goto stopped;
      if (errno == ENXIO) {
        GST_WARNING_OBJECT (v4l2object->element,
            "v4l2 device doesn't support polling. Disabling");
        v4l2object->can_poll_device = FALSE;
      } else {
        if (errno != EAGAIN && errno != EINTR)
          goto select_error;
      }
    }
  }
  return GST_FLOW_OK;

  /* ERRORS */
stopped:
  {
    GST_DEBUG ("stop called");
    return GST_FLOW_FLUSHING;
  }
select_error:
  {
    GST_ELEMENT_ERROR (v4l2object->element, RESOURCE, READ, (NULL),
        ("poll error %d: %s (%d)", ret, g_strerror (errno), errno));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_qbuf (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstV4l2Meta *meta;
  gint index;

  meta = GST_V4L2_META_GET (buf);
  if (meta == NULL) {
    GST_LOG_OBJECT (pool, "unref copied buffer %p", buf);
    /* no meta, it was a copied buffer that we can unref */
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  index = meta->vbuffer.index;

  GST_LOG_OBJECT (pool, "enqueue buffer %p, index:%d, queued:%d, flags:%08x",
      buf, index, pool->num_queued, meta->vbuffer.flags);

  if (pool->buffers[index] != NULL)
    goto already_queued;

  GST_LOG_OBJECT (pool, "doing QBUF");
  if (v4l2_ioctl (pool->video_fd, VIDIOC_QBUF, &meta->vbuffer) < 0)
    goto queue_failed;

  pool->buffers[index] = buf;
  pool->num_queued++;

  return GST_FLOW_OK;

  /* ERRORS */
already_queued:
  {
    GST_WARNING_OBJECT (pool, "the buffer was already queued");
    return GST_FLOW_ERROR;
  }
queue_failed:
  {
    GST_WARNING_OBJECT (pool, "could not queue a buffer %d (%s)", errno,
        g_strerror (errno));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_dqbuf (GstV4l2BufferPool * pool, GstBuffer ** buffer)
{
  GstFlowReturn res;
  GstBuffer *outbuf;
  struct v4l2_buffer vbuffer;
  GstV4l2Object *obj = pool->obj;
  GstClockTime timestamp;

  if (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
    /* select works for input devices when data is available. According to the
     * specs we can also poll to find out when a frame has been displayed but
     * that just seems to lock up here */
    if ((res = gst_v4l2_object_poll (obj)) != GST_FLOW_OK)
      goto poll_error;
  }

  memset (&vbuffer, 0x00, sizeof (vbuffer));
  vbuffer.type = obj->type;
  vbuffer.memory = V4L2_MEMORY_MMAP;

  GST_LOG_OBJECT (pool, "doing DQBUF");
  if (v4l2_ioctl (pool->video_fd, VIDIOC_DQBUF, &vbuffer) < 0)
    goto error;

  /* get our GstBuffer with that index from the pool, if the buffer was
   * outstanding we have a serious problem.
   */
  outbuf = pool->buffers[vbuffer.index];
  if (outbuf == NULL)
    goto no_buffer;

  /* mark the buffer outstanding */
  pool->buffers[vbuffer.index] = NULL;
  pool->num_queued--;

  timestamp = GST_TIMEVAL_TO_TIME (vbuffer.timestamp);

  GST_LOG_OBJECT (pool,
      "dequeued buffer %p seq:%d (ix=%d), used %d, flags %08x, ts %"
      GST_TIME_FORMAT ", pool-queued=%d, buffer=%p", outbuf, vbuffer.sequence,
      vbuffer.index, vbuffer.bytesused, vbuffer.flags,
      GST_TIME_ARGS (timestamp), pool->num_queued, outbuf);

  /* set top/bottom field first if v4l2_buffer has the information */
  if (vbuffer.field == V4L2_FIELD_INTERLACED_TB) {
    GST_BUFFER_FLAG_SET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  }
  if (vbuffer.field == V4L2_FIELD_INTERLACED_BT) {
    GST_BUFFER_FLAG_UNSET (outbuf, GST_VIDEO_BUFFER_FLAG_TFF);
  }

  /* this can change at every frame, esp. with jpeg */
  if (obj->type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
    gst_buffer_resize (outbuf, 0, vbuffer.bytesused);
  else
    gst_buffer_resize (outbuf, 0, vbuffer.length);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;

  *buffer = outbuf;

  return GST_FLOW_OK;

  /* ERRORS */
poll_error:
  {
    GST_DEBUG_OBJECT (pool, "poll error %s", gst_flow_get_name (res));
    return res;
  }
error:
  {
    GST_WARNING_OBJECT (pool,
        "problem dequeuing frame %d (ix=%d), pool-ct=%d, buf.flags=%d",
        vbuffer.sequence, vbuffer.index,
        GST_MINI_OBJECT_REFCOUNT (pool), vbuffer.flags);

    switch (errno) {
      case EAGAIN:
        GST_WARNING_OBJECT (pool,
            "Non-blocking I/O has been selected using O_NONBLOCK and"
            " no buffer was in the outgoing queue. device %s", obj->videodev);
        break;
      case EINVAL:
        GST_ERROR_OBJECT (pool,
            "The buffer type is not supported, or the index is out of bounds, "
            "or no buffers have been allocated yet, or the userptr "
            "or length are invalid. device %s", obj->videodev);
        break;
      case ENOMEM:
        GST_ERROR_OBJECT (pool,
            "insufficient memory to enqueue a user pointer buffer");
        break;
      case EIO:
        GST_INFO_OBJECT (pool,
            "VIDIOC_DQBUF failed due to an internal error."
            " Can also indicate temporary problems like signal loss."
            " Note the driver might dequeue an (empty) buffer despite"
            " returning an error, or even stop capturing."
            " device %s", obj->videodev);
        /* have we de-queued a buffer ? */
        if (!(vbuffer.flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))) {
          GST_DEBUG_OBJECT (pool, "reenqueing buffer");
          /* FIXME ... should we do something here? */
        }
        break;
      case EINTR:
        GST_WARNING_OBJECT (pool,
            "could not sync on a buffer on device %s", obj->videodev);
        break;
      default:
        GST_WARNING_OBJECT (pool,
            "Grabbing frame got interrupted on %s unexpectedly. %d: %s.",
            obj->videodev, errno, g_strerror (errno));
        break;
    }
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    GST_ERROR_OBJECT (pool, "No free buffer found in the pool at index %d.",
        vbuffer.index);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_v4l2_buffer_pool_acquire_buffer (GstBufferPool * bpool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "acquire");

  if (GST_BUFFER_POOL_IS_FLUSHING (bpool))
    goto flushing;

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      /* capture, This function should return a buffer with new captured data */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* take empty buffer from the pool */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;

        case GST_V4L2_IO_MMAP:
          /* just dequeue a buffer, we basically use the queue of v4l2 as the
           * storage for our buffers. This function does poll first so we can
           * interrupt it fine. */
          ret = gst_v4l2_buffer_pool_dqbuf (pool, buffer);
          if (G_UNLIKELY (ret != GST_FLOW_OK))
            goto done;

          /* start copying buffers when we are running low on buffers */
          if (pool->num_queued < pool->copy_threshold) {
            GstBuffer *copy;

            /* copy the memory */
            copy = gst_buffer_copy (*buffer);
            GST_LOG_OBJECT (pool, "copy buffer %p->%p", *buffer, copy);

            /* and requeue so that we can continue capturing */
            ret = gst_v4l2_buffer_pool_qbuf (pool, *buffer);
            *buffer = copy;
          }
          break;

        case GST_V4L2_IO_USERPTR:
        default:
          ret = GST_FLOW_ERROR;
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      /* playback, This function should return an empty buffer */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* get an empty buffer */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;

        case GST_V4L2_IO_MMAP:
          /* get a free unqueued buffer */
          ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
              buffer, params);
          break;

        case GST_V4L2_IO_USERPTR:
        default:
          ret = GST_FLOW_ERROR;
          g_assert_not_reached ();
          break;
      }
      break;

    default:
      ret = GST_FLOW_ERROR;
      g_assert_not_reached ();
      break;
  }
done:
  return ret;

  /* ERRORS */
flushing:
  {
    GST_DEBUG_OBJECT (pool, "We are flushing");
    return GST_FLOW_FLUSHING;
  }
}

static void
gst_v4l2_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (bpool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "release buffer %p", buffer);

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      /* capture, put the buffer back in the queue so that we can refill it
       * later. */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* release back in the pool */
          GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
          break;

        case GST_V4L2_IO_MMAP:
          /* queue back in the device */
          gst_v4l2_buffer_pool_qbuf (pool, buffer);
          break;

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* release back in the pool */
          GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
          break;

        case GST_V4L2_IO_MMAP:
        {
          GstV4l2Meta *meta;

          meta = GST_V4L2_META_GET (buffer);
          g_assert (meta != NULL);

          if (pool->buffers[meta->vbuffer.index] == NULL) {
            GST_LOG_OBJECT (pool, "buffer not queued, putting on free list");
            /* playback, put the buffer back in the queue to refill later. */
            GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool,
                buffer);
          } else {
            /* the buffer is queued in the device but maybe not played yet. We just
             * leave it there and not make it available for future calls to acquire
             * for now. The buffer will be dequeued and reused later. */
            GST_LOG_OBJECT (pool, "buffer is queued");
          }
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_v4l2_buffer_pool_finalize (GObject * object)
{
  GstV4l2BufferPool *pool = GST_V4L2_BUFFER_POOL (object);

  if (pool->video_fd >= 0)
    v4l2_close (pool->video_fd);
  if (pool->allocator)
    gst_object_unref (pool->allocator);
  g_free (pool->buffers);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_v4l2_buffer_pool_init (GstV4l2BufferPool * pool)
{
}

static void
gst_v4l2_buffer_pool_class_init (GstV4l2BufferPoolClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  object_class->finalize = gst_v4l2_buffer_pool_finalize;

  bufferpool_class->start = gst_v4l2_buffer_pool_start;
  bufferpool_class->stop = gst_v4l2_buffer_pool_stop;
  bufferpool_class->set_config = gst_v4l2_buffer_pool_set_config;
  bufferpool_class->alloc_buffer = gst_v4l2_buffer_pool_alloc_buffer;
  bufferpool_class->acquire_buffer = gst_v4l2_buffer_pool_acquire_buffer;
  bufferpool_class->release_buffer = gst_v4l2_buffer_pool_release_buffer;
  bufferpool_class->free_buffer = gst_v4l2_buffer_pool_free_buffer;
}

/**
 * gst_v4l2_buffer_pool_new:
 * @obj:  the v4l2 object owning the pool
 *
 * Construct a new buffer pool.
 *
 * Returns: the new pool, use gst_object_unref() to free resources
 */
GstBufferPool *
gst_v4l2_buffer_pool_new (GstV4l2Object * obj, GstCaps * caps)
{
  GstV4l2BufferPool *pool;
  GstStructure *s;
  gint fd;

  fd = v4l2_dup (obj->video_fd);
  if (fd < 0)
    goto dup_failed;

  pool = (GstV4l2BufferPool *) g_object_new (GST_TYPE_V4L2_BUFFER_POOL, NULL);
  pool->video_fd = fd;
  pool->obj = obj;

  s = gst_buffer_pool_get_config (GST_BUFFER_POOL_CAST (pool));
  gst_buffer_pool_config_set_params (s, caps, obj->sizeimage, 2, 0);
  gst_buffer_pool_set_config (GST_BUFFER_POOL_CAST (pool), s);

  return GST_BUFFER_POOL (pool);

  /* ERRORS */
dup_failed:
  {
    GST_DEBUG ("failed to dup fd %d (%s)", errno, g_strerror (errno));
    return NULL;
  }
}

static GstFlowReturn
gst_v4l2_do_read (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstFlowReturn res;
  GstV4l2Object *obj = pool->obj;
  gint amount;
  GstMapInfo map;
  gint toread;

  toread = obj->sizeimage;

  GST_LOG_OBJECT (pool, "reading %d bytes into buffer %p", toread, buf);

  gst_buffer_map (buf, &map, GST_MAP_WRITE);

  do {
    if ((res = gst_v4l2_object_poll (obj)) != GST_FLOW_OK)
      goto poll_error;

    amount = v4l2_read (obj->video_fd, map.data, toread);

    if (amount == toread) {
      break;
    } else if (amount == -1) {
      if (errno == EAGAIN || errno == EINTR) {
        continue;
      } else
        goto read_error;
    } else {
      /* short reads can happen if a signal interrupts the read */
      continue;
    }
  } while (TRUE);

  GST_LOG_OBJECT (pool, "read %d bytes", amount);
  gst_buffer_unmap (buf, &map);
  gst_buffer_resize (buf, 0, amount);

  return GST_FLOW_OK;

  /* ERRORS */
poll_error:
  {
    GST_DEBUG ("poll error %s", gst_flow_get_name (res));
    goto cleanup;
  }
read_error:
  {
    GST_ELEMENT_ERROR (obj->element, RESOURCE, READ,
        (_("Error reading %d bytes from device '%s'."),
            toread, obj->videodev), GST_ERROR_SYSTEM);
    res = GST_FLOW_ERROR;
    goto cleanup;
  }
cleanup:
  {
    gst_buffer_unmap (buf, &map);
    gst_buffer_resize (buf, 0, 0);
    return res;
  }
}

/**
 * gst_v4l2_buffer_pool_process:
 * @bpool: a #GstBufferPool
 * @buf: a #GstBuffer
 *
 * Process @buf in @bpool. For capture devices, this functions fills @buf with
 * data from the device. For output devices, this functions send the contents of
 * @buf to the device for playback.
 *
 * Returns: %GST_FLOW_OK on success.
 */
GstFlowReturn
gst_v4l2_buffer_pool_process (GstV4l2BufferPool * pool, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBufferPool *bpool = GST_BUFFER_POOL_CAST (pool);
  GstV4l2Object *obj = pool->obj;

  GST_DEBUG_OBJECT (pool, "process buffer %p", buf);

  switch (obj->type) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      /* capture */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* capture into the buffer */
          ret = gst_v4l2_do_read (pool, buf);
          break;

        case GST_V4L2_IO_MMAP:
        {
          GstBuffer *tmp;

          if (buf->pool == bpool)
            /* nothing, data was inside the buffer when we did _acquire() */
            goto done;

          /* buffer not from our pool, grab a frame and copy it into the target */
          if ((ret = gst_v4l2_buffer_pool_dqbuf (pool, &tmp)) != GST_FLOW_OK)
            goto done;

          if (!gst_v4l2_object_copy (obj, buf, tmp))
            goto copy_failed;

          /* an queue the buffer again after the copy */
          if ((ret = gst_v4l2_buffer_pool_qbuf (pool, tmp)) != GST_FLOW_OK)
            goto done;
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;

    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      /* playback */
      switch (obj->mode) {
        case GST_V4L2_IO_RW:
          /* FIXME, do write() */
          GST_WARNING_OBJECT (pool, "implement write()");
          break;

        case GST_V4L2_IO_MMAP:
        {
          GstBuffer *to_queue;

          if (buf->pool == bpool) {
            /* nothing, we can queue directly */
            to_queue = buf;
            GST_LOG_OBJECT (pool, "processing buffer from our pool");
          } else {
            GST_LOG_OBJECT (pool, "alloc buffer from our pool");
            if (!gst_buffer_pool_is_active (bpool)) {
              GstStructure *config;

              /* this pool was not activated, configure and activate */
              GST_DEBUG_OBJECT (pool, "activating pool");

              config = gst_buffer_pool_get_config (bpool);
              gst_buffer_pool_config_add_option (config,
                  GST_BUFFER_POOL_OPTION_VIDEO_META);
              gst_buffer_pool_set_config (bpool, config);

              if (!gst_buffer_pool_set_active (bpool, TRUE))
                goto activate_failed;
            }

            /* this can block if all buffers are outstanding which would be
             * strange because we would expect the upstream element to have
             * allocated them and returned to us.. */
            ret = GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer (bpool,
                &to_queue, NULL);
            if (ret != GST_FLOW_OK)
              goto acquire_failed;

            /* copy into it and queue */
            if (!gst_v4l2_object_copy (obj, to_queue, buf))
              goto copy_failed;
          }

          if ((ret = gst_v4l2_buffer_pool_qbuf (pool, to_queue)) != GST_FLOW_OK)
            goto done;

          /* if we are not streaming yet (this is the first buffer, start
           * streaming now */
          if (!pool->streaming)
            if (!start_streaming (pool))
              goto start_failed;

          if (pool->num_queued == pool->num_allocated) {
            /* all buffers are queued, try to dequeue one and release it back
             * into the pool so that _acquire can get to it again. */
            ret = gst_v4l2_buffer_pool_dqbuf (pool, &to_queue);
            if (ret != GST_FLOW_OK)
              goto done;

            /* release the rendered buffer back into the pool. This wakes up any
             * thread waiting for a buffer in _acquire() */
            gst_v4l2_buffer_pool_release_buffer (bpool, to_queue);
          }
          break;
        }

        case GST_V4L2_IO_USERPTR:
        default:
          g_assert_not_reached ();
          break;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }
done:
  return ret;

  /* ERRORS */
activate_failed:
  {
    GST_ERROR_OBJECT (obj->element, "failed to activate pool");
    return GST_FLOW_ERROR;
  }
acquire_failed:
  {
    GST_WARNING_OBJECT (obj->element, "failed to acquire a buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
copy_failed:
  {
    GST_ERROR_OBJECT (obj->element, "failed to copy data");
    return GST_FLOW_ERROR;
  }
start_failed:
  {
    GST_ERROR_OBJECT (obj->element, "failed to start streaming");
    return GST_FLOW_ERROR;
  }
}
