/* GStreamer
 * Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
 *
 * gstdataqueue.c:
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
 * SECTION:gstdataqueue
 * @short_description: Threadsafe queueing object
 *
 * #GstDataQueue is an object that handles threadsafe queueing of objects. It
 * also provides size-related functionality. This object should be used for
 * any #GstElement that wishes to provide some sort of queueing functionality.
 */

#include <gst/gst.h>
#include "string.h"
#include "gstdataqueue.h"
#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (data_queue_debug);
#define GST_CAT_DEFAULT (data_queue_debug)
GST_DEBUG_CATEGORY_STATIC (data_queue_dataflow);


/* Queue signals and args */
enum
{
  SIGNAL_EMPTY,
  SIGNAL_FULL,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_CUR_LEVEL_VISIBLE,
  ARG_CUR_LEVEL_BYTES,
  ARG_CUR_LEVEL_TIME
      /* FILL ME */
};

#define GST_DATA_QUEUE_MUTEX_LOCK(q) G_STMT_START {                     \
    GST_CAT_LOG (data_queue_dataflow,                                   \
      "locking qlock from thread %p",                                   \
      g_thread_self ());                                                \
  g_mutex_lock (&q->qlock);                                              \
  GST_CAT_LOG (data_queue_dataflow,                                     \
      "locked qlock from thread %p",                                    \
      g_thread_self ());                                                \
} G_STMT_END

#define GST_DATA_QUEUE_MUTEX_LOCK_CHECK(q, label) G_STMT_START {        \
    GST_DATA_QUEUE_MUTEX_LOCK (q);                                      \
    if (q->flushing)                                                    \
      goto label;                                                       \
  } G_STMT_END

#define GST_DATA_QUEUE_MUTEX_UNLOCK(q) G_STMT_START {                   \
    GST_CAT_LOG (data_queue_dataflow,                                   \
      "unlocking qlock from thread %p",                                 \
      g_thread_self ());                                                \
  g_mutex_unlock (&q->qlock);                                            \
} G_STMT_END

#define STATUS(q, msg)                                                  \
  GST_CAT_LOG (data_queue_dataflow,                                     \
               "queue:%p " msg ": %u visible items, %u "                \
               "bytes, %"G_GUINT64_FORMAT                               \
               " ns, %u elements",                                      \
               queue,                                                   \
               q->cur_level.visible,                                    \
               q->cur_level.bytes,                                      \
               q->cur_level.time,                                       \
               q->queue.length)

static void gst_data_queue_finalize (GObject * object);

static void gst_data_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_data_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GObjectClass *parent_class = NULL;
static guint gst_data_queue_signals[LAST_SIGNAL] = { 0 };

#define _do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (data_queue_debug, "dataqueue", 0, \
      "data queue object"); \
  GST_DEBUG_CATEGORY_INIT (data_queue_dataflow, "data_queue_dataflow", 0, \
      "dataflow inside the data queue object"); \
}


G_DEFINE_TYPE_WITH_CODE (GstDataQueue, gst_data_queue, G_TYPE_OBJECT, _do_init);

static void
gst_data_queue_class_init (GstDataQueueClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_data_queue_set_property;
  gobject_class->get_property = gst_data_queue_get_property;

  /* signals */
  /**
   * GstDataQueue::empty:
   * @queue: the queue instance
   *
   * Reports that the queue became empty (empty).
   * A queue is empty if the total amount of visible items inside it (num-visible, time,
   * size) is lower than the boundary values which can be set through the GObject
   * properties.
   */
  gst_data_queue_signals[SIGNAL_EMPTY] =
      g_signal_new ("empty", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstDataQueueClass, empty), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /**
   * GstDataQueue::full:
   * @queue: the queue instance
   *
   * Reports that the queue became full (full).
   * A queue is full if the total amount of data inside it (num-visible, time,
   * size) is higher than the boundary values which can be set through the GObject
   * properties.
   */
  gst_data_queue_signals[SIGNAL_FULL] =
      g_signal_new ("full", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST,
      G_STRUCT_OFFSET (GstDataQueueClass, full), NULL, NULL,
      g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

  /* properties */
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_BYTES,
      g_param_spec_uint ("current-level-bytes", "Current level (kB)",
          "Current amount of data in the queue (bytes)",
          0, G_MAXUINT, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_VISIBLE,
      g_param_spec_uint ("current-level-visible",
          "Current level (visible items)",
          "Current number of visible items in the queue", 0, G_MAXUINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, ARG_CUR_LEVEL_TIME,
      g_param_spec_uint64 ("current-level-time", "Current level (ns)",
          "Current amount of data in the queue (in ns)", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  gobject_class->finalize = gst_data_queue_finalize;
}

static void
gst_data_queue_init (GstDataQueue * queue)
{
  queue->cur_level.visible = 0; /* no content */
  queue->cur_level.bytes = 0;   /* no content */
  queue->cur_level.time = 0;    /* no content */

  queue->checkfull = NULL;

  g_mutex_init (&queue->qlock);
  g_cond_init (&queue->item_add);
  g_cond_init (&queue->item_del);
  gst_queue_array_init (&queue->queue, 50);

  GST_DEBUG ("initialized queue's not_empty & not_full conditions");
}

/**
 * gst_data_queue_new_full:
 * @checkfull: the callback used to tell if the element considers the queue full
 * or not.
 * @fullcallback: the callback which will be called when the queue is considered full.
 * @emptycallback: the callback which will be called when the queue is considered empty.
 * @checkdata: a #gpointer that will be given in the @checkfull callback.
 *
 * Creates a new #GstDataQueue. The difference with @gst_data_queue_new is that it will
 * not emit the 'full' and 'empty' signals, but instead calling directly @fullcallback
 * or @emptycallback.
 *
 * Returns: a new #GstDataQueue.
 */

GstDataQueue *
gst_data_queue_new_full (GstDataQueueCheckFullFunction checkfull,
    GstDataQueueFullCallback fullcallback,
    GstDataQueueEmptyCallback emptycallback, gpointer checkdata)
{
  GstDataQueue *ret;

  g_return_val_if_fail (checkfull != NULL, NULL);

  ret = g_object_newv (GST_TYPE_DATA_QUEUE, 0, NULL);
  ret->checkfull = checkfull;
  ret->checkdata = checkdata;
  ret->fullcallback = fullcallback;
  ret->emptycallback = emptycallback;

  return ret;
}

/**
 * gst_data_queue_new:
 * @checkfull: the callback used to tell if the element considers the queue full
 * or not.
 * @checkdata: a #gpointer that will be given in the @checkfull callback.
 *
 * Returns: a new #GstDataQueue.
 */

GstDataQueue *
gst_data_queue_new (GstDataQueueCheckFullFunction checkfull, gpointer checkdata)
{
  return gst_data_queue_new_full (checkfull, NULL, NULL, checkdata);
}

static void
gst_data_queue_cleanup (GstDataQueue * queue)
{
  while (!gst_queue_array_is_empty (&queue->queue)) {
    GstDataQueueItem *item = gst_queue_array_pop_head (&queue->queue);

    /* Just call the destroy notify on the item */
    item->destroy (item);
  }
  queue->cur_level.visible = 0;
  queue->cur_level.bytes = 0;
  queue->cur_level.time = 0;
}

/* called only once, as opposed to dispose */
static void
gst_data_queue_finalize (GObject * object)
{
  GstDataQueue *queue = GST_DATA_QUEUE (object);

  GST_DEBUG ("finalizing queue");

  gst_data_queue_cleanup (queue);
  gst_queue_array_clear (&queue->queue);

  GST_DEBUG ("free mutex");
  g_mutex_clear (&queue->qlock);
  GST_DEBUG ("done free mutex");

  g_cond_clear (&queue->item_add);
  g_cond_clear (&queue->item_del);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static inline void
gst_data_queue_locked_flush (GstDataQueue * queue)
{
  STATUS (queue, "before flushing");
  gst_data_queue_cleanup (queue);
  STATUS (queue, "after flushing");
  /* we deleted something... */
  if (queue->waiting_del)
    g_cond_signal (&queue->item_del);
}

static inline gboolean
gst_data_queue_locked_is_empty (GstDataQueue * queue)
{
  return (queue->queue.length == 0);
}

static inline gboolean
gst_data_queue_locked_is_full (GstDataQueue * queue)
{
  return queue->checkfull (queue, queue->cur_level.visible,
      queue->cur_level.bytes, queue->cur_level.time, queue->checkdata);
}

/**
 * gst_data_queue_flush:
 * @queue: a #GstDataQueue.
 *
 * Flushes all the contents of the @queue. Any call to #gst_data_queue_push and
 * #gst_data_queue_pop will be released.
 * MT safe.
 */
void
gst_data_queue_flush (GstDataQueue * queue)
{
  GST_DEBUG ("queue:%p", queue);
  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  gst_data_queue_locked_flush (queue);
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * gst_data_queue_is_empty:
 * @queue: a #GstDataQueue.
 *
 * Queries if there are any items in the @queue.
 * MT safe.
 *
 * Returns: #TRUE if @queue is empty.
 */
gboolean
gst_data_queue_is_empty (GstDataQueue * queue)
{
  gboolean res;

  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  res = gst_data_queue_locked_is_empty (queue);
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return res;
}

/**
 * gst_data_queue_is_full:
 * @queue: a #GstDataQueue.
 *
 * Queries if @queue is full. This check will be done using the
 * #GstDataQueueCheckFullFunction registered with @queue.
 * MT safe.
 *
 * Returns: #TRUE if @queue is full.
 */
gboolean
gst_data_queue_is_full (GstDataQueue * queue)
{
  gboolean res;

  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  res = gst_data_queue_locked_is_full (queue);
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return res;
}

/**
 * gst_data_queue_set_flushing:
 * @queue: a #GstDataQueue.
 * @flushing: a #gboolean stating if the queue will be flushing or not.
 *
 * Sets the queue to flushing state if @flushing is #TRUE. If set to flushing
 * state, any incoming data on the @queue will be discarded. Any call currently
 * blocking on #gst_data_queue_push or #gst_data_queue_pop will return straight
 * away with a return value of #FALSE. While the @queue is in flushing state, 
 * all calls to those two functions will return #FALSE.
 *
 * MT Safe.
 */
void
gst_data_queue_set_flushing (GstDataQueue * queue, gboolean flushing)
{
  GST_DEBUG ("queue:%p , flushing:%d", queue, flushing);

  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  queue->flushing = flushing;
  if (flushing) {
    /* release push/pop functions */
    if (queue->waiting_add)
      g_cond_signal (&queue->item_add);
    if (queue->waiting_del)
      g_cond_signal (&queue->item_del);
  }
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * gst_data_queue_push:
 * @queue: a #GstDataQueue.
 * @item: a #GstDataQueueItem.
 *
 * Pushes a #GstDataQueueItem (or a structure that begins with the same fields)
 * on the @queue. If the @queue is full, the call will block until space is
 * available, OR the @queue is set to flushing state.
 * MT safe.
 *
 * Note that this function has slightly different semantics than gst_pad_push()
 * and gst_pad_push_event(): this function only takes ownership of @item and
 * the #GstMiniObject contained in @item if the push was successful. If FALSE
 * is returned, the caller is responsible for freeing @item and its contents.
 *
 * Returns: #TRUE if the @item was successfully pushed on the @queue.
 */
gboolean
gst_data_queue_push (GstDataQueue * queue, GstDataQueueItem * item)
{
  g_return_val_if_fail (GST_IS_DATA_QUEUE (queue), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

  STATUS (queue, "before pushing");

  /* We ALWAYS need to check for queue fillness */
  if (gst_data_queue_locked_is_full (queue)) {
    GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    if (G_LIKELY (queue->fullcallback))
      queue->fullcallback (queue, queue->checkdata);
    else
      g_signal_emit (queue, gst_data_queue_signals[SIGNAL_FULL], 0);
    GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

    /* signal might have removed some items */
    while (gst_data_queue_locked_is_full (queue)) {
      queue->waiting_del = TRUE;
      g_cond_wait (&queue->item_del, &queue->qlock);
      queue->waiting_del = FALSE;
      if (queue->flushing)
        goto flushing;
    }
  }

  gst_queue_array_push_tail (&queue->queue, item);

  if (item->visible)
    queue->cur_level.visible++;
  queue->cur_level.bytes += item->size;
  queue->cur_level.time += item->duration;

  STATUS (queue, "after pushing");
  if (queue->waiting_add)
    g_cond_signal (&queue->item_add);

  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return TRUE;

  /* ERRORS */
flushing:
  {
    GST_DEBUG ("queue:%p, we are flushing", queue);
    GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    return FALSE;
  }
}

/**
 * gst_data_queue_pop:
 * @queue: a #GstDataQueue.
 * @item: pointer to store the returned #GstDataQueueItem.
 *
 * Retrieves the first @item available on the @queue. If the queue is currently
 * empty, the call will block until at least one item is available, OR the
 * @queue is set to the flushing state.
 * MT safe.
 *
 * Returns: #TRUE if an @item was successfully retrieved from the @queue.
 */
gboolean
gst_data_queue_pop (GstDataQueue * queue, GstDataQueueItem ** item)
{
  g_return_val_if_fail (GST_IS_DATA_QUEUE (queue), FALSE);
  g_return_val_if_fail (item != NULL, FALSE);

  GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

  STATUS (queue, "before popping");

  if (gst_data_queue_locked_is_empty (queue)) {
    GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    if (G_LIKELY (queue->emptycallback))
      queue->emptycallback (queue, queue->checkdata);
    else
      g_signal_emit (queue, gst_data_queue_signals[SIGNAL_EMPTY], 0);
    GST_DATA_QUEUE_MUTEX_LOCK_CHECK (queue, flushing);

    while (gst_data_queue_locked_is_empty (queue)) {
      queue->waiting_add = TRUE;
      g_cond_wait (&queue->item_add, &queue->qlock);
      queue->waiting_add = FALSE;
      if (queue->flushing)
        goto flushing;
    }
  }

  /* Get the item from the GQueue */
  *item = gst_queue_array_pop_head (&queue->queue);

  /* update current level counter */
  if ((*item)->visible)
    queue->cur_level.visible--;
  queue->cur_level.bytes -= (*item)->size;
  queue->cur_level.time -= (*item)->duration;

  STATUS (queue, "after popping");
  if (queue->waiting_del)
    g_cond_signal (&queue->item_del);

  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  return TRUE;

  /* ERRORS */
flushing:
  {
    GST_DEBUG ("queue:%p, we are flushing", queue);
    GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
    return FALSE;
  }
}

static gint
is_of_type (gconstpointer a, gconstpointer b)
{
  return !G_TYPE_CHECK_INSTANCE_TYPE (a, GPOINTER_TO_SIZE (b));
}

/**
 * gst_data_queue_drop_head:
 * @queue: The #GstDataQueue to drop an item from.
 * @type: The #GType of the item to drop.
 *
 * Pop and unref the head-most #GstMiniObject with the given #GType.
 *
 * Returns: TRUE if an element was removed.
 */
gboolean
gst_data_queue_drop_head (GstDataQueue * queue, GType type)
{
  gboolean res = FALSE;
  GstDataQueueItem *leak = NULL;
  guint idx;

  g_return_val_if_fail (GST_IS_DATA_QUEUE (queue), FALSE);

  GST_DEBUG ("queue:%p", queue);

  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  idx =
      gst_queue_array_find (&queue->queue, is_of_type, GSIZE_TO_POINTER (type));

  if (idx == -1)
    goto done;

  leak = queue->queue.array[idx];
  gst_queue_array_drop_element (&queue->queue, idx);

  if (leak->visible)
    queue->cur_level.visible--;
  queue->cur_level.bytes -= leak->size;
  queue->cur_level.time -= leak->duration;

  leak->destroy (leak);

  res = TRUE;

done:
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);

  GST_DEBUG ("queue:%p , res:%d", queue, res);

  return res;
}

/**
 * gst_data_queue_limits_changed:
 * @queue: The #GstDataQueue 
 *
 * Inform the queue that the limits for the fullness check have changed and that
 * any blocking gst_data_queue_push() should be unblocked to recheck the limts.
 */
void
gst_data_queue_limits_changed (GstDataQueue * queue)
{
  g_return_if_fail (GST_IS_DATA_QUEUE (queue));

  GST_DATA_QUEUE_MUTEX_LOCK (queue);
  if (queue->waiting_del) {
    GST_DEBUG ("signal del");
    g_cond_signal (&queue->item_del);
  }
  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}

/**
 * gst_data_queue_get_level:
 * @queue: The #GstDataQueue
 * @level: the location to store the result
 *
 * Get the current level of the queue.
 */
void
gst_data_queue_get_level (GstDataQueue * queue, GstDataQueueSize * level)
{
  memcpy (level, (&queue->cur_level), sizeof (GstDataQueueSize));
}

static void
gst_data_queue_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_data_queue_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstDataQueue *queue = GST_DATA_QUEUE (object);

  GST_DATA_QUEUE_MUTEX_LOCK (queue);

  switch (prop_id) {
    case ARG_CUR_LEVEL_BYTES:
      g_value_set_uint (value, queue->cur_level.bytes);
      break;
    case ARG_CUR_LEVEL_VISIBLE:
      g_value_set_uint (value, queue->cur_level.visible);
      break;
    case ARG_CUR_LEVEL_TIME:
      g_value_set_uint64 (value, queue->cur_level.time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_DATA_QUEUE_MUTEX_UNLOCK (queue);
}
