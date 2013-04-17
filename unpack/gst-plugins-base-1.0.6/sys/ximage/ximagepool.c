/* GStreamer
 * Copyright (C) <2005> Julien Moutte <julien@moutte.net>
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

/* Object header */
#include "ximagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>

/* Helper functions */
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

GST_DEBUG_CATEGORY_EXTERN (gst_debug_ximagepool);
#define GST_CAT_DEFAULT gst_debug_ximagepool

struct _GstXImageBufferPoolPrivate
{
  GstCaps *caps;
  GstVideoInfo info;
  GstVideoAlignment align;
  guint padded_width;
  guint padded_height;
  gboolean add_metavideo;
  gboolean need_alignment;
};

static void gst_ximage_meta_free (GstXImageMeta * meta, GstBuffer * buffer);

/* ximage metadata */
GType
gst_ximage_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] =
      { "memory", "size", "colorspace", "orientation", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstXImageMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_ximage_meta_get_info (void)
{
  static const GstMetaInfo *ximage_meta_info = NULL;

  if (g_once_init_enter (&ximage_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_XIMAGE_META_API_TYPE, "GstXImageMeta",
        sizeof (GstXImageMeta), (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) gst_ximage_meta_free,
        (GstMetaTransformFunction) NULL);
    g_once_init_leave (&ximage_meta_info, meta);
  }
  return ximage_meta_info;
}

/* X11 stuff */
static gboolean error_caught = FALSE;

static int
gst_ximagesink_handle_xerror (Display * display, XErrorEvent * xevent)
{
  char error_msg[1024];

  XGetErrorText (display, xevent->error_code, error_msg, 1024);
  GST_DEBUG ("ximagesink triggered an XError. error: %s", error_msg);
  error_caught = TRUE;
  return 0;
}

static GstXImageMeta *
gst_buffer_add_ximage_meta (GstBuffer * buffer, GstXImageBufferPool * xpool)
{
  GstXImageSink *ximagesink;
  int (*handler) (Display *, XErrorEvent *);
  gboolean success = FALSE;
  GstXContext *xcontext;
  GstXImageMeta *meta;
  gint width, height, align = 15, offset;
  GstXImageBufferPoolPrivate *priv;

  priv = xpool->priv;
  ximagesink = xpool->sink;
  xcontext = ximagesink->xcontext;

  width = priv->padded_width;
  height = priv->padded_height;

  meta =
      (GstXImageMeta *) gst_buffer_add_meta (buffer, GST_XIMAGE_META_INFO,
      NULL);
#ifdef HAVE_XSHM
  meta->SHMInfo.shmaddr = ((void *) -1);
  meta->SHMInfo.shmid = -1;
#endif
  meta->x = priv->align.padding_left;
  meta->y = priv->align.padding_top;
  meta->width = GST_VIDEO_INFO_WIDTH (&priv->info);
  meta->height = GST_VIDEO_INFO_HEIGHT (&priv->info);
  meta->sink = gst_object_ref (ximagesink);

  GST_DEBUG_OBJECT (ximagesink, "creating image %p (%dx%d)", buffer,
      width, height);

  g_mutex_lock (&ximagesink->x_lock);

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

#ifdef HAVE_XSHM
  if (xcontext->use_xshm) {
    meta->ximage = XShmCreateImage (xcontext->disp,
        xcontext->visual,
        xcontext->depth, ZPixmap, NULL, &meta->SHMInfo, width, height);
    if (!meta->ximage || error_caught) {
      g_mutex_unlock (&ximagesink->x_lock);

      /* Reset error flag */
      error_caught = FALSE;

      /* Push a warning */
      GST_ELEMENT_WARNING (ximagesink, RESOURCE, WRITE,
          ("Failed to create output image buffer of %dx%d pixels",
              width, height),
          ("could not XShmCreateImage a %dx%d image", width, height));

      /* Retry without XShm */
      ximagesink->xcontext->use_xshm = FALSE;

      /* Hold X mutex again to try without XShm */
      g_mutex_lock (&ximagesink->x_lock);

      goto no_xshm;
    }

    /* we have to use the returned bytes_per_line for our shm size */
    meta->size = meta->ximage->bytes_per_line * meta->ximage->height;
    GST_LOG_OBJECT (ximagesink,
        "XShm image size is %" G_GSIZE_FORMAT ", width %d, stride %d",
        meta->size, width, meta->ximage->bytes_per_line);

    /* get shared memory */
    meta->SHMInfo.shmid =
        shmget (IPC_PRIVATE, meta->size + align, IPC_CREAT | 0777);
    if (meta->SHMInfo.shmid == -1)
      goto shmget_failed;

    /* attach */
    meta->SHMInfo.shmaddr = shmat (meta->SHMInfo.shmid, NULL, 0);
    if (meta->SHMInfo.shmaddr == ((void *) -1))
      goto shmat_failed;

    /* now we can set up the image data */
    meta->ximage->data = meta->SHMInfo.shmaddr;
    meta->SHMInfo.readOnly = FALSE;

    if (XShmAttach (xcontext->disp, &meta->SHMInfo) == 0)
      goto xattach_failed;

    XSync (xcontext->disp, FALSE);

    /* Now that everyone has attached, we can delete the shared memory segment.
     * This way, it will be deleted as soon as we detach later, and not
     * leaked if we crash. */
    shmctl (meta->SHMInfo.shmid, IPC_RMID, NULL);

    GST_DEBUG_OBJECT (ximagesink, "XServer ShmAttached to 0x%x, id 0x%lx",
        meta->SHMInfo.shmid, meta->SHMInfo.shmseg);
  } else
  no_xshm:
#endif /* HAVE_XSHM */
  {
    guint allocsize;

    meta->ximage = XCreateImage (xcontext->disp,
        xcontext->visual,
        xcontext->depth, ZPixmap, 0, NULL, width, height, xcontext->bpp, 0);
    if (!meta->ximage || error_caught)
      goto create_failed;

    /* upstream will assume that rowstrides are multiples of 4, but this
     * doesn't always seem to be the case with XCreateImage() */
    if ((meta->ximage->bytes_per_line % 4) != 0) {
      GST_WARNING_OBJECT (ximagesink, "returned stride not a multiple of 4 as "
          "usually assumed");
    }

    /* we have to use the returned bytes_per_line for our image size */
    meta->size = meta->ximage->bytes_per_line * meta->ximage->height;

    /* alloc a bit more for unexpected strides to avoid crashes upstream.
     * FIXME: if we get an unrounded stride, the image will be displayed
     * distorted, since all upstream elements assume a rounded stride */
    allocsize =
        GST_ROUND_UP_4 (meta->ximage->bytes_per_line) * meta->ximage->height;

    meta->ximage->data = g_malloc (allocsize + align);
    GST_LOG_OBJECT (ximagesink,
        "non-XShm image size is %" G_GSIZE_FORMAT " (alloced: %u), width %d, "
        "stride %d", meta->size, allocsize, width,
        meta->ximage->bytes_per_line);

    XSync (xcontext->disp, FALSE);
  }

  if ((offset = ((guintptr) meta->ximage->data & align)))
    offset = (align + 1) - offset;

  GST_DEBUG_OBJECT (ximagesink, "memory %p, align %d, offset %d",
      meta->ximage->data, align, offset);

  /* Reset error handler */
  error_caught = FALSE;
  XSetErrorHandler (handler);

  gst_buffer_append_memory (buffer,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_NO_SHARE, meta->ximage->data,
          meta->size + align, offset, meta->size, NULL, NULL));

  g_mutex_unlock (&ximagesink->x_lock);

  success = TRUE;

beach:
  if (!success)
    meta = NULL;

  return meta;

  /* ERRORS */
create_failed:
  {
    g_mutex_unlock (&ximagesink->x_lock);
    /* Reset error handler */
    error_caught = FALSE;
    XSetErrorHandler (handler);
    /* Push an error */
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height),
        ("could not XShmCreateImage a %dx%d image", width, height));
    goto beach;
  }
#ifdef HAVE_XSHM
shmget_failed:
  {
    g_mutex_unlock (&ximagesink->x_lock);
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height),
        ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
            meta->size));
    goto beach;
  }
shmat_failed:
  {
    g_mutex_unlock (&ximagesink->x_lock);
    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height), ("Failed to shmat: %s", g_strerror (errno)));
    /* Clean up the shared memory segment */
    shmctl (meta->SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }
xattach_failed:
  {
    /* Clean up the shared memory segment */
    shmctl (meta->SHMInfo.shmid, IPC_RMID, NULL);
    g_mutex_unlock (&ximagesink->x_lock);

    GST_ELEMENT_ERROR (ximagesink, RESOURCE, WRITE,
        ("Failed to create output image buffer of %dx%d pixels",
            width, height), ("Failed to XShmAttach"));
    goto beach;
  }
#endif
}

static void
gst_ximage_meta_free (GstXImageMeta * meta, GstBuffer * buffer)
{
  GstXImageSink *ximagesink;

  ximagesink = meta->sink;

  GST_DEBUG_OBJECT (ximagesink, "free meta on buffer %p", buffer);

  /* Hold the object lock to ensure the XContext doesn't disappear */
  GST_OBJECT_LOCK (ximagesink);
  /* We might have some buffers destroyed after changing state to NULL */
  if (ximagesink->xcontext == NULL) {
    GST_DEBUG_OBJECT (ximagesink, "Destroying XImage after XContext");
#ifdef HAVE_XSHM
    /* Need to free the shared memory segment even if the x context
     * was already cleaned up */
    if (meta->SHMInfo.shmaddr != ((void *) -1)) {
      shmdt (meta->SHMInfo.shmaddr);
    }
#endif
    goto beach;
  }

  g_mutex_lock (&ximagesink->x_lock);

#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm) {
    if (meta->SHMInfo.shmaddr != ((void *) -1)) {
      GST_DEBUG_OBJECT (ximagesink, "XServer ShmDetaching from 0x%x id 0x%lx",
          meta->SHMInfo.shmid, meta->SHMInfo.shmseg);
      XShmDetach (ximagesink->xcontext->disp, &meta->SHMInfo);
      XSync (ximagesink->xcontext->disp, FALSE);
      shmdt (meta->SHMInfo.shmaddr);
      meta->SHMInfo.shmaddr = (void *) -1;
    }
    if (meta->ximage)
      XDestroyImage (meta->ximage);
  } else
#endif /* HAVE_XSHM */
  {
    if (meta->ximage) {
      XDestroyImage (meta->ximage);
    }
  }

  XSync (ximagesink->xcontext->disp, FALSE);

  g_mutex_unlock (&ximagesink->x_lock);

beach:
  GST_OBJECT_UNLOCK (ximagesink);

  gst_object_unref (meta->sink);
}

#ifdef HAVE_XSHM
/* This function checks that it is actually really possible to create an image
   using XShm */
gboolean
gst_ximagesink_check_xshm_calls (GstXImageSink * ximagesink,
    GstXContext * xcontext)
{
  XImage *ximage;
  XShmSegmentInfo SHMInfo;
  size_t size;
  int (*handler) (Display *, XErrorEvent *);
  gboolean result = FALSE;
  gboolean did_attach = FALSE;

  g_return_val_if_fail (xcontext != NULL, FALSE);

  /* Sync to ensure any older errors are already processed */
  XSync (xcontext->disp, FALSE);

  /* Set defaults so we don't free these later unnecessarily */
  SHMInfo.shmaddr = ((void *) -1);
  SHMInfo.shmid = -1;

  /* Setting an error handler to catch failure */
  error_caught = FALSE;
  handler = XSetErrorHandler (gst_ximagesink_handle_xerror);

  /* Trying to create a 1x1 ximage */
  GST_DEBUG ("XShmCreateImage of 1x1");

  ximage = XShmCreateImage (xcontext->disp, xcontext->visual,
      xcontext->depth, ZPixmap, NULL, &SHMInfo, 1, 1);

  /* Might cause an error, sync to ensure it is noticed */
  XSync (xcontext->disp, FALSE);
  if (!ximage || error_caught) {
    GST_WARNING ("could not XShmCreateImage a 1x1 image");
    goto beach;
  }
  size = ximage->height * ximage->bytes_per_line;

  SHMInfo.shmid = shmget (IPC_PRIVATE, size, IPC_CREAT | 0777);
  if (SHMInfo.shmid == -1) {
    GST_WARNING ("could not get shared memory of %" G_GSIZE_FORMAT " bytes",
        size);
    goto beach;
  }

  SHMInfo.shmaddr = shmat (SHMInfo.shmid, NULL, 0);
  if (SHMInfo.shmaddr == ((void *) -1)) {
    GST_WARNING ("Failed to shmat: %s", g_strerror (errno));
    /* Clean up the shared memory segment */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  ximage->data = SHMInfo.shmaddr;
  SHMInfo.readOnly = FALSE;

  if (XShmAttach (xcontext->disp, &SHMInfo) == 0) {
    GST_WARNING ("Failed to XShmAttach");
    /* Clean up the shared memory segment */
    shmctl (SHMInfo.shmid, IPC_RMID, NULL);
    goto beach;
  }

  /* Sync to ensure we see any errors we caused */
  XSync (xcontext->disp, FALSE);

  /* Delete the shared memory segment as soon as everyone is attached.
   * This way, it will be deleted as soon as we detach later, and not
   * leaked if we crash. */
  shmctl (SHMInfo.shmid, IPC_RMID, NULL);

  if (!error_caught) {
    GST_DEBUG ("XServer ShmAttached to 0x%x, id 0x%lx", SHMInfo.shmid,
        SHMInfo.shmseg);

    did_attach = TRUE;
    /* store whether we succeeded in result */
    result = TRUE;
  } else {
    GST_WARNING ("MIT-SHM extension check failed at XShmAttach. "
        "Not using shared memory.");
  }

beach:
  /* Sync to ensure we swallow any errors we caused and reset error_caught */
  XSync (xcontext->disp, FALSE);

  error_caught = FALSE;
  XSetErrorHandler (handler);

  if (did_attach) {
    GST_DEBUG ("XServer ShmDetaching from 0x%x id 0x%lx",
        SHMInfo.shmid, SHMInfo.shmseg);
    XShmDetach (xcontext->disp, &SHMInfo);
    XSync (xcontext->disp, FALSE);
  }
  if (SHMInfo.shmaddr != ((void *) -1))
    shmdt (SHMInfo.shmaddr);
  if (ximage)
    XDestroyImage (ximage);
  return result;
}
#endif /* HAVE_XSHM */

/* bufferpool */
static void gst_ximage_buffer_pool_finalize (GObject * object);

#define GST_XIMAGE_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_XIMAGE_BUFFER_POOL, GstXImageBufferPoolPrivate))

#define gst_ximage_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstXImageBufferPool, gst_ximage_buffer_pool,
    GST_TYPE_BUFFER_POOL);

static const gchar **
ximage_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META,
    GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT, NULL
  };

  return options;
}

static gboolean
ximage_buffer_pool_set_config (GstBufferPool * pool, GstStructure * config)
{
  GstXImageBufferPool *xpool = GST_XIMAGE_BUFFER_POOL_CAST (pool);
  GstXImageBufferPoolPrivate *priv = xpool->priv;
  GstVideoInfo info;
  GstCaps *caps;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL))
    goto wrong_config;

  if (caps == NULL)
    goto no_caps;

  /* now parse the caps from the config */
  if (!gst_video_info_from_caps (&info, caps))
    goto wrong_caps;

  GST_LOG_OBJECT (pool, "%dx%d, caps %" GST_PTR_FORMAT, info.width, info.height,
      caps);

  /* keep track of the width and height and caps */
  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_ref (caps);

  /* check for the configured metadata */
  priv->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  /* parse extra alignment info */
  priv->need_alignment = gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  if (priv->need_alignment) {
    gst_buffer_pool_config_get_video_alignment (config, &priv->align);

    GST_LOG_OBJECT (pool, "padding %u-%ux%u-%u", priv->align.padding_top,
        priv->align.padding_left, priv->align.padding_left,
        priv->align.padding_bottom);

    /* do padding and alignment */
    gst_video_info_align (&info, &priv->align);

    /* we need the video metadata too now */
    priv->add_metavideo = TRUE;
  } else {
    gst_video_alignment_reset (&priv->align);
  }

  /* add the padding */
  priv->padded_width =
      GST_VIDEO_INFO_WIDTH (&info) + priv->align.padding_left +
      priv->align.padding_right;
  priv->padded_height =
      GST_VIDEO_INFO_HEIGHT (&info) + priv->align.padding_top +
      priv->align.padding_bottom;

  priv->info = info;

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (pool, config);

  /* ERRORS */
wrong_config:
  {
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_caps:
  {
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

/* This function handles GstXImageBuffer creation depending on XShm availability */
static GstFlowReturn
ximage_buffer_pool_alloc (GstBufferPool * pool, GstBuffer ** buffer,
    GstBufferPoolAcquireParams * params)
{
  GstXImageBufferPool *xpool = GST_XIMAGE_BUFFER_POOL_CAST (pool);
  GstXImageBufferPoolPrivate *priv = xpool->priv;
  GstVideoInfo *info;
  GstBuffer *ximage;
  GstXImageMeta *meta;

  info = &priv->info;

  ximage = gst_buffer_new ();
  meta = gst_buffer_add_ximage_meta (ximage, xpool);
  if (meta == NULL) {
    gst_buffer_unref (ximage);
    goto no_buffer;
  }
  if (priv->add_metavideo) {
    GST_DEBUG_OBJECT (pool, "adding GstVideoMeta");
    /* these are just the defaults for now */
    gst_buffer_add_video_meta_full (ximage, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);
  }
  *buffer = ximage;

  return GST_FLOW_OK;

  /* ERROR */
no_buffer:
  {
    GST_WARNING_OBJECT (pool, "can't create image");
    return GST_FLOW_ERROR;
  }
}

GstBufferPool *
gst_ximage_buffer_pool_new (GstXImageSink * ximagesink)
{
  GstXImageBufferPool *pool;

  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);

  pool = g_object_new (GST_TYPE_XIMAGE_BUFFER_POOL, NULL);
  pool->sink = gst_object_ref (ximagesink);

  GST_LOG_OBJECT (pool, "new XImage buffer pool %p", pool);

  return GST_BUFFER_POOL_CAST (pool);
}

static void
gst_ximage_buffer_pool_class_init (GstXImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  g_type_class_add_private (klass, sizeof (GstXImageBufferPoolPrivate));

  gobject_class->finalize = gst_ximage_buffer_pool_finalize;

  gstbufferpool_class->get_options = ximage_buffer_pool_get_options;
  gstbufferpool_class->set_config = ximage_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = ximage_buffer_pool_alloc;
}

static void
gst_ximage_buffer_pool_init (GstXImageBufferPool * pool)
{
  pool->priv = GST_XIMAGE_BUFFER_POOL_GET_PRIVATE (pool);
}

static void
gst_ximage_buffer_pool_finalize (GObject * object)
{
  GstXImageBufferPool *pool = GST_XIMAGE_BUFFER_POOL_CAST (object);
  GstXImageBufferPoolPrivate *priv = pool->priv;

  GST_LOG_OBJECT (pool, "finalize XImage buffer pool %p", pool);

  if (priv->caps)
    gst_caps_unref (priv->caps);
  gst_object_unref (pool->sink);

  G_OBJECT_CLASS (gst_ximage_buffer_pool_parent_class)->finalize (object);
}
