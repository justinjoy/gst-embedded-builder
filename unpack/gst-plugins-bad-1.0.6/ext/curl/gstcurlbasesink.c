/* GStreamer
 * Copyright (C) 2011 Axis Communications <dev-gstreamer@axis.com>
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
 * SECTION:element-curlsink
 * @short_description: sink that uploads data to a server using libcurl
 * @see_also:
 *
 * This is a network sink that uses libcurl as a client to upload data to
 * a server (e.g. a HTTP/FTP server).
 *
 * <refsect2>
 * <title>Example launch line (upload a JPEG file to an HTTP server)</title>
 * |[
 * gst-launch filesrc location=image.jpg ! jpegparse ! curlsink  \
 *     file-name=image.jpg  \
 *     location=http://192.168.0.1:8080/cgi-bin/patupload.cgi/  \
 *     user=test passwd=test  \
 *     content-type=image/jpeg  \
 *     use-content-length=false
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <curl/curl.h>
#include <string.h>
#include <stdio.h>

#if HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#include <sys/types.h>
#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#include <unistd.h>
#if HAVE_NETINET_IP_H
#include <netinet/ip.h>
#endif
#if HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

#include "gstcurlbasesink.h"

/* Default values */
#define GST_CAT_DEFAULT                gst_curl_base_sink_debug
#define DEFAULT_URL                    "localhost:5555"
#define DEFAULT_TIMEOUT                30
#define DEFAULT_QOS_DSCP               0

#define DSCP_MIN                       0
#define DSCP_MAX                       63


/* Plugin specific settings */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (gst_curl_base_sink_debug);

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_USER_NAME,
  PROP_USER_PASSWD,
  PROP_FILE_NAME,
  PROP_TIMEOUT,
  PROP_QOS_DSCP
};

/* Object class function declarations */
static void gst_curl_base_sink_finalize (GObject * gobject);
static void gst_curl_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_curl_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* BaseSink class function declarations */
static GstFlowReturn gst_curl_base_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static gboolean gst_curl_base_sink_event (GstBaseSink * bsink,
    GstEvent * event);
static gboolean gst_curl_base_sink_start (GstBaseSink * bsink);
static gboolean gst_curl_base_sink_stop (GstBaseSink * bsink);
static gboolean gst_curl_base_sink_unlock (GstBaseSink * bsink);
static gboolean gst_curl_base_sink_unlock_stop (GstBaseSink * bsink);

/* private functions */

static gboolean gst_curl_base_sink_transfer_setup_unlocked
    (GstCurlBaseSink * sink);
static gboolean gst_curl_base_sink_transfer_start_unlocked
    (GstCurlBaseSink * sink);
static void gst_curl_base_sink_transfer_cleanup (GstCurlBaseSink * sink);
static size_t gst_curl_base_sink_transfer_read_cb (void *ptr, size_t size,
    size_t nmemb, void *stream);
static size_t gst_curl_base_sink_transfer_write_cb (void *ptr, size_t size,
    size_t nmemb, void *stream);
static size_t gst_curl_base_sink_transfer_data_buffer (GstCurlBaseSink * sink,
    void *curl_ptr, size_t block_size, guint * last_chunk);
static int gst_curl_base_sink_transfer_socket_cb (void *clientp,
    curl_socket_t curlfd, curlsocktype purpose);
static gpointer gst_curl_base_sink_transfer_thread_func (gpointer data);
static gint gst_curl_base_sink_setup_dscp_unlocked (GstCurlBaseSink * sink);
static CURLcode gst_curl_base_sink_transfer_check (GstCurlBaseSink * sink);

static gboolean gst_curl_base_sink_wait_for_data_unlocked
    (GstCurlBaseSink * sink);
static void gst_curl_base_sink_new_file_notify_unlocked
    (GstCurlBaseSink * sink);
static void gst_curl_base_sink_wait_for_transfer_thread_to_send_unlocked
    (GstCurlBaseSink * sink);
static void gst_curl_base_sink_data_sent_notify (GstCurlBaseSink * sink);
static void gst_curl_base_sink_wait_for_response (GstCurlBaseSink * sink);
static void gst_curl_base_sink_got_response_notify (GstCurlBaseSink * sink);

static void handle_transfer (GstCurlBaseSink * sink);
static size_t transfer_data_buffer (void *curl_ptr, TransferBuffer * buf,
    size_t max_bytes_to_send, guint * last_chunk);

#define parent_class gst_curl_base_sink_parent_class
G_DEFINE_TYPE (GstCurlBaseSink, gst_curl_base_sink, GST_TYPE_BASE_SINK);

static void
gst_curl_base_sink_class_init (GstCurlBaseSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = (GstBaseSinkClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_curl_base_sink_debug, "curlbasesink", 0,
      "curl base sink element");
  GST_DEBUG_OBJECT (klass, "class_init");

  gst_element_class_set_static_metadata (element_class,
      "Curl base sink",
      "Sink/Network",
      "Upload data over the network to a server using libcurl",
      "Patricia Muscalu <patricia@axis.com>");

  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_curl_base_sink_event);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_curl_base_sink_render);
  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_curl_base_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_curl_base_sink_stop);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_curl_base_sink_unlock);
  gstbasesink_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_curl_base_sink_unlock_stop);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_curl_base_sink_finalize);

  gobject_class->set_property = gst_curl_base_sink_set_property;
  gobject_class->get_property = gst_curl_base_sink_get_property;

  klass->handle_transfer = handle_transfer;
  klass->transfer_read_cb = gst_curl_base_sink_transfer_read_cb;
  klass->transfer_data_buffer = gst_curl_base_sink_transfer_data_buffer;

  /* FIXME: check against souphttpsrc and use same names for same properties */
  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "URI location to write to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_NAME,
      g_param_spec_string ("user", "User name",
          "User name to use for server authentication", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_USER_PASSWD,
      g_param_spec_string ("passwd", "User password",
          "User password to use for server authentication", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FILE_NAME,
      g_param_spec_string ("file-name", "Base file name",
          "The base file name for the uploaded images", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMEOUT,
      g_param_spec_int ("timeout", "Timeout",
          "Number of seconds waiting to write before timeout",
          0, G_MAXINT, DEFAULT_TIMEOUT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QOS_DSCP,
      g_param_spec_int ("qos-dscp",
          "QoS diff srv code point",
          "Quality of Service, differentiated services code point (0 default)",
          DSCP_MIN, DSCP_MAX, DEFAULT_QOS_DSCP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));
}

static void
gst_curl_base_sink_init (GstCurlBaseSink * sink)
{
  sink->transfer_buf = g_malloc (sizeof (TransferBuffer));
  sink->transfer_cond = g_malloc (sizeof (TransferCondition));
  g_cond_init (&sink->transfer_cond->cond);
  sink->transfer_cond->data_sent = FALSE;
  sink->transfer_cond->data_available = FALSE;
  sink->transfer_cond->wait_for_response = FALSE;
  sink->timeout = DEFAULT_TIMEOUT;
  sink->qos_dscp = DEFAULT_QOS_DSCP;
  sink->url = g_strdup (DEFAULT_URL);
  sink->transfer_thread_close = FALSE;
  sink->new_file = TRUE;
  sink->flow_ret = GST_FLOW_OK;
  sink->is_live = FALSE;
}

static void
gst_curl_base_sink_finalize (GObject * gobject)
{
  GstCurlBaseSink *this = GST_CURL_BASE_SINK (gobject);

  GST_DEBUG ("finalizing curlsink");
  if (this->transfer_thread != NULL) {
    g_thread_join (this->transfer_thread);
  }

  gst_curl_base_sink_transfer_cleanup (this);
  g_cond_clear (&this->transfer_cond->cond);
  g_free (this->transfer_cond);
  g_free (this->transfer_buf);

  g_free (this->url);
  g_free (this->user);
  g_free (this->passwd);
  g_free (this->file_name);
  if (this->fdset != NULL) {
    gst_poll_free (this->fdset);
    this->fdset = NULL;
  }
  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

void
gst_curl_base_sink_transfer_thread_notify_unlocked (GstCurlBaseSink * sink)
{
  GST_LOG ("more data to send");

  sink->transfer_cond->data_available = TRUE;
  sink->transfer_cond->data_sent = FALSE;
  sink->transfer_cond->wait_for_response = TRUE;
  g_cond_signal (&sink->transfer_cond->cond);
}

void
gst_curl_base_sink_transfer_thread_close (GstCurlBaseSink * sink)
{
  GST_OBJECT_LOCK (sink);
  GST_LOG_OBJECT (sink, "setting transfer thread close flag");
  sink->transfer_thread_close = TRUE;
  g_cond_signal (&sink->transfer_cond->cond);
  GST_OBJECT_UNLOCK (sink);

  if (sink->transfer_thread != NULL) {
    GST_LOG_OBJECT (sink, "waiting for transfer thread to finish");
    g_thread_join (sink->transfer_thread);
    sink->transfer_thread = NULL;
  }
}

void
gst_curl_base_sink_set_live (GstCurlBaseSink * sink, gboolean live)
{
  g_return_if_fail (GST_IS_CURL_BASE_SINK (sink));

  GST_OBJECT_LOCK (sink);
  sink->is_live = live;
  GST_OBJECT_UNLOCK (sink);
}

gboolean
gst_curl_base_sink_is_live (GstCurlBaseSink * sink)
{
  gboolean result;

  g_return_val_if_fail (GST_IS_CURL_BASE_SINK (sink), FALSE);

  GST_OBJECT_LOCK (sink);
  result = sink->is_live;
  GST_OBJECT_UNLOCK (sink);

  return result;
}

static GstFlowReturn
gst_curl_base_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstCurlBaseSink *sink = GST_CURL_BASE_SINK (bsink);
  GstMapInfo map;
  guint8 *data;
  size_t size;
  GstFlowReturn ret;

  GST_LOG ("enter render");

  sink = GST_CURL_BASE_SINK (bsink);
  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  GST_OBJECT_LOCK (sink);

  /* check if the transfer thread has encountered problems while the
   * pipeline thread was working elsewhere */
  if (sink->flow_ret != GST_FLOW_OK) {
    goto done;
  }

  g_assert (sink->transfer_cond->data_available == FALSE);

  /* if there is no transfer thread created, lets create one */
  if (sink->transfer_thread == NULL) {
    if (!gst_curl_base_sink_transfer_start_unlocked (sink)) {
      sink->flow_ret = GST_FLOW_ERROR;
      goto done;
    }
  }

  /* make data available for the transfer thread and notify */
  sink->transfer_buf->ptr = data;
  sink->transfer_buf->len = size;
  sink->transfer_buf->offset = 0;
  gst_curl_base_sink_transfer_thread_notify_unlocked (sink);

  /* wait for the transfer thread to send the data. This will be notified
   * either when transfer is completed by the curl read callback or by
   * the thread function if an error has occured. */
  gst_curl_base_sink_wait_for_transfer_thread_to_send_unlocked (sink);

done:
  ret = sink->flow_ret;
  GST_OBJECT_UNLOCK (sink);
  gst_buffer_unmap (buf, &map);

  GST_LOG ("exit render");

  return ret;
}

static gboolean
gst_curl_base_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstCurlBaseSink *sink = GST_CURL_BASE_SINK (bsink);
  GstCurlBaseSinkClass *klass = GST_CURL_BASE_SINK_GET_CLASS (sink);

  switch (event->type) {
    case GST_EVENT_EOS:
      GST_DEBUG_OBJECT (sink, "received EOS");
      gst_curl_base_sink_transfer_thread_close (sink);
      gst_curl_base_sink_wait_for_response (sink);
      break;
    case GST_EVENT_CAPS:
      if (klass->set_mime_type) {
        GstCaps *caps;
        gst_event_parse_caps (event, &caps);
        klass->set_mime_type (sink, caps);
      }
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}

static gboolean
gst_curl_base_sink_start (GstBaseSink * bsink)
{
  GstCurlBaseSink *sink;

  sink = GST_CURL_BASE_SINK (bsink);

  /* reset flags */
  sink->transfer_cond->data_sent = FALSE;
  sink->transfer_cond->data_available = FALSE;
  sink->transfer_cond->wait_for_response = FALSE;
  sink->transfer_thread_close = FALSE;
  sink->new_file = TRUE;
  sink->flow_ret = GST_FLOW_OK;

  if ((sink->fdset = gst_poll_new (TRUE)) == NULL) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_READ_WRITE,
        ("gst_poll_new failed: %s", g_strerror (errno)), (NULL));
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_curl_base_sink_stop (GstBaseSink * bsink)
{
  GstCurlBaseSink *sink = GST_CURL_BASE_SINK (bsink);

  gst_curl_base_sink_transfer_thread_close (sink);
  if (sink->fdset != NULL) {
    gst_poll_free (sink->fdset);
    sink->fdset = NULL;
  }

  return TRUE;
}

static gboolean
gst_curl_base_sink_unlock (GstBaseSink * bsink)
{
  GstCurlBaseSink *sink;

  sink = GST_CURL_BASE_SINK (bsink);

  GST_LOG_OBJECT (sink, "Flushing");
  gst_poll_set_flushing (sink->fdset, TRUE);

  return TRUE;
}

static gboolean
gst_curl_base_sink_unlock_stop (GstBaseSink * bsink)
{
  GstCurlBaseSink *sink;

  sink = GST_CURL_BASE_SINK (bsink);

  GST_LOG_OBJECT (sink, "No longer flushing");
  gst_poll_set_flushing (sink->fdset, FALSE);

  return TRUE;
}

static void
gst_curl_base_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCurlBaseSink *sink;
  GstState cur_state;

  g_return_if_fail (GST_IS_CURL_BASE_SINK (object));
  sink = GST_CURL_BASE_SINK (object);

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);
  if (cur_state != GST_STATE_PLAYING && cur_state != GST_STATE_PAUSED) {
    GST_OBJECT_LOCK (sink);

    switch (prop_id) {
      case PROP_LOCATION:
        g_free (sink->url);
        sink->url = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "url set to %s", sink->url);
        break;
      case PROP_USER_NAME:
        g_free (sink->user);
        sink->user = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "user set to %s", sink->user);
        break;
      case PROP_USER_PASSWD:
        g_free (sink->passwd);
        sink->passwd = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "passwd set to %s", sink->passwd);
        break;
      case PROP_FILE_NAME:
        g_free (sink->file_name);
        sink->file_name = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->file_name);
        break;
      case PROP_TIMEOUT:
        sink->timeout = g_value_get_int (value);
        GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->timeout);
        break;
      case PROP_QOS_DSCP:
        sink->qos_dscp = g_value_get_int (value);
        gst_curl_base_sink_setup_dscp_unlocked (sink);
        GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->qos_dscp);
        break;
      default:
        GST_DEBUG_OBJECT (sink, "invalid property id %d", prop_id);
        break;
    }

    GST_OBJECT_UNLOCK (sink);

    return;
  }

  /* in PLAYING or PAUSED state */
  GST_OBJECT_LOCK (sink);

  switch (prop_id) {
    case PROP_FILE_NAME:
      g_free (sink->file_name);
      sink->file_name = g_value_dup_string (value);
      GST_DEBUG_OBJECT (sink, "file_name set to %s", sink->file_name);
      gst_curl_base_sink_new_file_notify_unlocked (sink);
      break;
    case PROP_TIMEOUT:
      sink->timeout = g_value_get_int (value);
      GST_DEBUG_OBJECT (sink, "timeout set to %d", sink->timeout);
      break;
    case PROP_QOS_DSCP:
      sink->qos_dscp = g_value_get_int (value);
      gst_curl_base_sink_setup_dscp_unlocked (sink);
      GST_DEBUG_OBJECT (sink, "dscp set to %d", sink->qos_dscp);
      break;
    default:
      GST_WARNING_OBJECT (sink, "cannot set property when PLAYING");
      break;
  }

  GST_OBJECT_UNLOCK (sink);
}

static void
gst_curl_base_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCurlBaseSink *sink;

  g_return_if_fail (GST_IS_CURL_BASE_SINK (object));
  sink = GST_CURL_BASE_SINK (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, sink->url);
      break;
    case PROP_USER_NAME:
      g_value_set_string (value, sink->user);
      break;
    case PROP_USER_PASSWD:
      g_value_set_string (value, sink->passwd);
      break;
    case PROP_FILE_NAME:
      g_value_set_string (value, sink->file_name);
      break;
    case PROP_TIMEOUT:
      g_value_set_int (value, sink->timeout);
      break;
    case PROP_QOS_DSCP:
      g_value_set_int (value, sink->qos_dscp);
      break;
    default:
      GST_DEBUG_OBJECT (sink, "invalid property id");
      break;
  }
}

static gboolean
gst_curl_base_sink_transfer_set_common_options_unlocked (GstCurlBaseSink * sink)
{
  GstCurlBaseSinkClass *klass = GST_CURL_BASE_SINK_GET_CLASS (sink);

#ifdef DEBUG
  curl_easy_setopt (sink->curl, CURLOPT_VERBOSE, 1);
#endif

  curl_easy_setopt (sink->curl, CURLOPT_URL, sink->url);

  curl_easy_setopt (sink->curl, CURLOPT_CONNECTTIMEOUT, sink->timeout);

  /* using signals in a multithreaded application is dangeous */
  curl_easy_setopt (sink->curl, CURLOPT_NOSIGNAL, 1);

  /* socket settings */
  curl_easy_setopt (sink->curl, CURLOPT_SOCKOPTDATA, sink);
  curl_easy_setopt (sink->curl, CURLOPT_SOCKOPTFUNCTION,
      gst_curl_base_sink_transfer_socket_cb);

  curl_easy_setopt (sink->curl, CURLOPT_READFUNCTION, klass->transfer_read_cb);
  curl_easy_setopt (sink->curl, CURLOPT_READDATA, sink);
  curl_easy_setopt (sink->curl, CURLOPT_WRITEFUNCTION,
      gst_curl_base_sink_transfer_write_cb);
  curl_easy_setopt (sink->curl, CURLOPT_WRITEDATA, sink);

  return TRUE;
}

static gboolean
gst_curl_base_sink_transfer_set_options_unlocked (GstCurlBaseSink * sink)
{
  gboolean res = FALSE;
  GstCurlBaseSinkClass *klass = GST_CURL_BASE_SINK_GET_CLASS (sink);

  gst_curl_base_sink_transfer_set_common_options_unlocked (sink);

  /* authentication settings */
  if (sink->user != NULL && strlen (sink->user)) {
    curl_easy_setopt (sink->curl, CURLOPT_USERNAME, sink->user);
    curl_easy_setopt (sink->curl, CURLOPT_PASSWORD, sink->passwd);
  }

  if (klass->set_options_unlocked) {
    res = klass->set_options_unlocked (sink);
  }

  return res;
}

static size_t
transfer_data_buffer (void *curl_ptr, TransferBuffer * buf,
    size_t max_bytes_to_send, guint * last_chunk)
{
  guint buf_len = buf->len;
  size_t bytes_to_send = MIN (max_bytes_to_send, buf->len);

  memcpy ((guint8 *) curl_ptr, buf->ptr + buf->offset, bytes_to_send);
  buf->offset = buf->offset + bytes_to_send;
  buf->len = buf->len - bytes_to_send;

  /* the last data chunk */
  if (bytes_to_send == buf_len) {
    buf->offset = 0;
    buf->len = 0;
    *last_chunk = 1;
  }

  GST_LOG ("sent : %" G_GSIZE_FORMAT, bytes_to_send);

  return bytes_to_send;
}

static size_t
gst_curl_base_sink_transfer_data_buffer (GstCurlBaseSink * sink,
    void *curl_ptr, size_t block_size, guint * last_chunk)
{
  TransferBuffer *buffer;
  size_t bytes_to_send;

  buffer = sink->transfer_buf;
  GST_LOG ("write buf len=%" G_GSIZE_FORMAT ", offset=%" G_GSIZE_FORMAT,
      buffer->len, buffer->offset);

  if (buffer->len <= 0) {
    GST_WARNING ("got zero- or negative-length buffer");

    return 0;
  }

  /* more data in buffer(s) */
  bytes_to_send = transfer_data_buffer (curl_ptr, sink->transfer_buf,
      block_size, last_chunk);

  return bytes_to_send;
}

static size_t
gst_curl_base_sink_transfer_read_cb (void *curl_ptr, size_t size, size_t nmemb,
    void *stream)
{
  GstCurlBaseSink *sink;
  GstCurlBaseSinkClass *klass;
  size_t max_bytes_to_send;
  size_t bytes_to_send;
  guint last_chunk = 0;

  sink = (GstCurlBaseSink *) stream;
  klass = GST_CURL_BASE_SINK_GET_CLASS (sink);

  max_bytes_to_send = size * nmemb;

  /* wait for data to come available, if new file or thread close is set
   * then zero will be returned to indicate end of current transfer */
  GST_OBJECT_LOCK (sink);
  if (gst_curl_base_sink_wait_for_data_unlocked (sink) == FALSE) {
    if (klass->flush_data_unlocked) {
      bytes_to_send = klass->flush_data_unlocked (sink, curl_ptr,
          max_bytes_to_send, sink->new_file);

      GST_OBJECT_UNLOCK (sink);

      return bytes_to_send;
    }

    GST_OBJECT_UNLOCK (sink);
    GST_LOG ("returning 0, no more data to send in this file");

    return 0;
  }

  GST_OBJECT_UNLOCK (sink);

  bytes_to_send = klass->transfer_data_buffer (sink, curl_ptr,
      max_bytes_to_send, &last_chunk);

  /* the last data chunk */
  if (last_chunk) {
    gst_curl_base_sink_data_sent_notify (sink);
  }

  return bytes_to_send;
}

static size_t
gst_curl_base_sink_transfer_write_cb (void G_GNUC_UNUSED * ptr, size_t size,
    size_t nmemb, void G_GNUC_UNUSED * stream)
{
  GstCurlBaseSink *sink;
  GstCurlBaseSinkClass *klass;
  size_t realsize = size * nmemb;

  sink = (GstCurlBaseSink *) stream;
  klass = GST_CURL_BASE_SINK_GET_CLASS (sink);

  if (klass->transfer_verify_response_code) {
    if (!klass->transfer_verify_response_code (sink)) {
      GST_DEBUG_OBJECT (sink, "response error");
      GST_OBJECT_LOCK (sink);
      sink->flow_ret = GST_FLOW_ERROR;
      GST_OBJECT_UNLOCK (sink);
    }
  }

  GST_DEBUG ("response %s", (gchar *) ptr);

  return realsize;
}

CURLcode
gst_curl_base_sink_transfer_check (GstCurlBaseSink * sink)
{
  CURLcode code = CURLE_OK;
  CURL *easy;
  CURLMsg *msg;
  gint msgs_left;
  gchar *eff_url = NULL;

  do {
    easy = NULL;
    while ((msg = curl_multi_info_read (sink->multi_handle, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        easy = msg->easy_handle;
        code = msg->data.result;
        break;
      }
    }
    if (easy) {
      curl_easy_getinfo (easy, CURLINFO_EFFECTIVE_URL, &eff_url);
      GST_DEBUG ("transfer done %s (%s-%d)\n", eff_url,
          curl_easy_strerror (code), code);
    }
  } while (easy);

  return code;
}

static void
handle_transfer (GstCurlBaseSink * sink)
{
  GstCurlBaseSinkClass *klass = GST_CURL_BASE_SINK_GET_CLASS (sink);
  gint retval;
  gint activated_fds;
  gint running_handles;
  gint timeout;
  CURLMcode m_code;
  CURLcode e_code;

  GST_OBJECT_LOCK (sink);
  timeout = sink->timeout;
  GST_OBJECT_UNLOCK (sink);

  /* Receiving CURLM_CALL_MULTI_PERFORM means that libcurl may have more data
     available to send or receive - call simply curl_multi_perform before
     poll() on more actions */
  do {
    m_code = curl_multi_perform (sink->multi_handle, &running_handles);
  } while (m_code == CURLM_CALL_MULTI_PERFORM);

  while (running_handles && (m_code == CURLM_OK)) {
    if (klass->transfer_prepare_poll_wait) {
      klass->transfer_prepare_poll_wait (sink);
    }

    activated_fds = gst_poll_wait (sink->fdset, timeout * GST_SECOND);
    if (G_UNLIKELY (activated_fds == -1)) {
      if (errno == EAGAIN || errno == EINTR) {
        GST_DEBUG_OBJECT (sink, "interrupted by signal");
      } else if (errno == EBUSY) {
        GST_DEBUG_OBJECT (sink, "poll stopped");
        retval = GST_FLOW_EOS;
        goto fail;
      } else {
        GST_DEBUG_OBJECT (sink, "poll failed: %s", g_strerror (errno));
        GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("poll failed"), (NULL));
        retval = GST_FLOW_ERROR;
        goto fail;
      }
    } else if (G_UNLIKELY (activated_fds == 0)) {
      GST_DEBUG_OBJECT (sink, "poll timed out");
      GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("poll timed out"), (NULL));
      retval = GST_FLOW_ERROR;
      goto fail;
    }

    /* readable/writable sockets */
    do {
      m_code = curl_multi_perform (sink->multi_handle, &running_handles);
    } while (m_code == CURLM_CALL_MULTI_PERFORM);
  }

  if (m_code != CURLM_OK) {
    GST_DEBUG_OBJECT (sink, "curl multi error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("%s",
            curl_multi_strerror (m_code)), (NULL));
    retval = GST_FLOW_ERROR;
    goto fail;
  }

  /* problems still might have occurred on individual transfers even when
   * curl_multi_perform returns CURLM_OK */
  if ((e_code = gst_curl_base_sink_transfer_check (sink)) != CURLE_OK) {
    GST_DEBUG_OBJECT (sink, "curl easy error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("%s",
            curl_easy_strerror (e_code)), (NULL));
    retval = GST_FLOW_ERROR;
    goto fail;
  }

  gst_curl_base_sink_got_response_notify (sink);

  return;

fail:
  GST_OBJECT_LOCK (sink);
  if (sink->flow_ret == GST_FLOW_OK) {
    sink->flow_ret = retval;
  }
  GST_OBJECT_UNLOCK (sink);
  return;
}

/* This function gets called by libcurl after the socket() call but before
 * the connect() call. */
static int
gst_curl_base_sink_transfer_socket_cb (void *clientp, curl_socket_t curlfd,
    curlsocktype G_GNUC_UNUSED purpose)
{
  GstCurlBaseSink *sink;
  gboolean ret = TRUE;

  sink = (GstCurlBaseSink *) clientp;

  g_assert (sink);

  if (curlfd < 0) {
    /* signal an unrecoverable error to the library which will close the socket
       and return CURLE_COULDNT_CONNECT
     */
    return 1;
  }

  gst_poll_fd_init (&sink->fd);
  sink->fd.fd = curlfd;

  ret = ret && gst_poll_add_fd (sink->fdset, &sink->fd);
  ret = ret && gst_poll_fd_ctl_write (sink->fdset, &sink->fd, TRUE);
  ret = ret && gst_poll_fd_ctl_read (sink->fdset, &sink->fd, TRUE);
  GST_DEBUG ("fd: %d", sink->fd.fd);
  GST_OBJECT_LOCK (sink);
  gst_curl_base_sink_setup_dscp_unlocked (sink);
  GST_OBJECT_UNLOCK (sink);

  /* success */
  if (ret) {
    return 0;
  } else {
    return 1;
  }
}

static gboolean
gst_curl_base_sink_transfer_start_unlocked (GstCurlBaseSink * sink)
{
  GError *error = NULL;
  gboolean ret = TRUE;

  GST_LOG ("creating transfer thread");
  sink->transfer_thread_close = FALSE;
  sink->new_file = TRUE;
  sink->transfer_thread =
      g_thread_try_new ("Curl Transfer Thread", (GThreadFunc)
      gst_curl_base_sink_transfer_thread_func, sink, &error);

  if (sink->transfer_thread == NULL || error != NULL) {
    ret = FALSE;
    if (error) {
      GST_ERROR_OBJECT (sink, "could not create thread %s", error->message);
      g_error_free (error);
    } else {
      GST_ERROR_OBJECT (sink, "could not create thread for unknown reason");
    }
  }

  return ret;
}

static gpointer
gst_curl_base_sink_transfer_thread_func (gpointer data)
{
  GstCurlBaseSink *sink = (GstCurlBaseSink *) data;
  GstCurlBaseSinkClass *klass = GST_CURL_BASE_SINK_GET_CLASS (sink);
  GstFlowReturn ret;
  gboolean data_available;

  GST_LOG ("transfer thread started");
  GST_OBJECT_LOCK (sink);
  if (!gst_curl_base_sink_transfer_setup_unlocked (sink)) {
    GST_DEBUG_OBJECT (sink, "curl setup error");
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, ("curl setup error"), (NULL));
    sink->flow_ret = GST_FLOW_ERROR;
    goto done;
  }

  while (!sink->transfer_thread_close && sink->flow_ret == GST_FLOW_OK) {
    /* we are working on a new file, clearing flag and setting a new file
     * name */
    sink->new_file = FALSE;

    /* wait for data to arrive for this new file, if we get a new file name
     * again before getting data we will simply skip transfering anything
     * for this file and go directly to the new file */
    data_available = gst_curl_base_sink_wait_for_data_unlocked (sink);
    if (data_available) {
      if (G_UNLIKELY (!klass->set_protocol_dynamic_options_unlocked (sink))) {
        sink->flow_ret = GST_FLOW_ERROR;
        GST_OBJECT_UNLOCK (sink);
        GST_ELEMENT_ERROR (sink, RESOURCE, FAILED, ("Unexpected state."),
            (NULL));
        GST_OBJECT_LOCK (sink);
        goto done;
      }
    }

    /* stay unlocked while handling the actual transfer */
    GST_OBJECT_UNLOCK (sink);

    if (data_available) {
      if (!gst_curl_base_sink_is_live (sink)) {
        /* prepare transfer if needed */
        if (klass->prepare_transfer) {
          GST_OBJECT_LOCK (sink);
          if (!klass->prepare_transfer (sink)) {
            sink->flow_ret = GST_FLOW_ERROR;
            goto done;
          }
          GST_OBJECT_UNLOCK (sink);
        }
        curl_multi_add_handle (sink->multi_handle, sink->curl);
      }

      /* Start driving the transfer. */
      klass->handle_transfer (sink);

      /* easy handle will be possibly re-used for next transfer, thus it needs to
       * be removed from the multi stack and re-added again */
      if (!gst_curl_base_sink_is_live (sink)) {
        curl_multi_remove_handle (sink->multi_handle, sink->curl);
      }
    }

    /* lock again before looping to check the thread closed flag */
    GST_OBJECT_LOCK (sink);
  }

  if (sink->is_live) {
    curl_multi_remove_handle (sink->multi_handle, sink->curl);
  }

done:
  /* extract the error code so the lock does not have to be
   * taken when calling the functions below that take the lock
   * on their own */
  ret = sink->flow_ret;
  GST_OBJECT_UNLOCK (sink);

  /* if there is a flow error, always notify the render function so it
   * can return the flow error up along the pipeline. as an error has
   * occurred there is no response to receive, so notify the event function
   * so it doesn't block indefinitely waiting for a response. */
  if (ret != GST_FLOW_OK) {
    gst_curl_base_sink_data_sent_notify (sink);
    gst_curl_base_sink_got_response_notify (sink);
  }

  GST_DEBUG ("exit thread func - transfer thread close flag: %d",
      sink->transfer_thread_close);

  return NULL;
}

static gboolean
gst_curl_base_sink_transfer_setup_unlocked (GstCurlBaseSink * sink)
{
  g_assert (sink);

  if (sink->curl == NULL) {
    /* curl_easy_init automatically calls curl_global_init(3) */
    if ((sink->curl = curl_easy_init ()) == NULL) {
      g_warning ("Failed to init easy handle");
      return FALSE;
    }
  }

  if (!gst_curl_base_sink_transfer_set_options_unlocked (sink)) {
    g_warning ("Failed to setup easy handle");
    GST_OBJECT_UNLOCK (sink);
    return FALSE;
  }

  /* init a multi stack (non-blocking interface to liburl) */
  if (sink->multi_handle == NULL) {
    if ((sink->multi_handle = curl_multi_init ()) == NULL) {
      return FALSE;
    }
  }

  return TRUE;
}

static void
gst_curl_base_sink_transfer_cleanup (GstCurlBaseSink * sink)
{
  if (sink->curl != NULL) {
    if (sink->multi_handle != NULL) {
      curl_multi_remove_handle (sink->multi_handle, sink->curl);
    }
    curl_easy_cleanup (sink->curl);
    sink->curl = NULL;
  }

  if (sink->multi_handle != NULL) {
    curl_multi_cleanup (sink->multi_handle);
    sink->multi_handle = NULL;
  }
}

static gboolean
gst_curl_base_sink_wait_for_data_unlocked (GstCurlBaseSink * sink)
{
  gboolean data_available = FALSE;

  GST_LOG ("waiting for data");
  while (!sink->transfer_cond->data_available &&
      !sink->transfer_thread_close && !sink->new_file) {
    g_cond_wait (&sink->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }

  if (sink->transfer_thread_close) {
    GST_LOG ("wait for data aborted due to thread close");
  } else if (sink->new_file) {
    GST_LOG ("wait for data aborted due to new file name");
  } else {
    GST_LOG ("wait for data completed");
    data_available = TRUE;
  }

  return data_available;
}

static void
gst_curl_base_sink_new_file_notify_unlocked (GstCurlBaseSink * sink)
{
  GST_LOG ("new file name");
  sink->new_file = TRUE;
  g_cond_signal (&sink->transfer_cond->cond);
}

static void
    gst_curl_base_sink_wait_for_transfer_thread_to_send_unlocked
    (GstCurlBaseSink * sink)
{
  GST_LOG ("waiting for buffer send to complete");

  /* this function should not check if the transfer thread is set to be closed
   * since that flag only can be set by the EoS event (by the pipeline thread).
   * This can therefore never happen while this function is running since this
   * function also is called by the pipeline thread (in the render function) */
  while (!sink->transfer_cond->data_sent) {
    g_cond_wait (&sink->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }
  GST_LOG ("buffer send completed");
}

static void
gst_curl_base_sink_data_sent_notify (GstCurlBaseSink * sink)
{
  GST_LOG ("transfer completed");
  GST_OBJECT_LOCK (sink);
  sink->transfer_cond->data_available = FALSE;
  sink->transfer_cond->data_sent = TRUE;
  g_cond_signal (&sink->transfer_cond->cond);
  GST_OBJECT_UNLOCK (sink);
}

static void
gst_curl_base_sink_wait_for_response (GstCurlBaseSink * sink)
{
  GST_LOG ("waiting for remote to send response code");

  GST_OBJECT_LOCK (sink);
  while (sink->transfer_cond->wait_for_response) {
    g_cond_wait (&sink->transfer_cond->cond, GST_OBJECT_GET_LOCK (sink));
  }
  GST_OBJECT_UNLOCK (sink);

  GST_LOG ("response code received");
}

static void
gst_curl_base_sink_got_response_notify (GstCurlBaseSink * sink)
{
  GST_LOG ("got response code");

  GST_OBJECT_LOCK (sink);
  sink->transfer_cond->wait_for_response = FALSE;
  g_cond_signal (&sink->transfer_cond->cond);
  GST_OBJECT_UNLOCK (sink);
}

static gint
gst_curl_base_sink_setup_dscp_unlocked (GstCurlBaseSink * sink)
{
  gint tos;
  gint af;
  gint ret = -1;
  union
  {
    struct sockaddr sa;
    struct sockaddr_in6 sa_in6;
    struct sockaddr_storage sa_stor;
  } sa;
  socklen_t slen = sizeof (sa);

  if (getsockname (sink->fd.fd, &sa.sa, &slen) < 0) {
    GST_DEBUG_OBJECT (sink, "could not get sockname: %s", g_strerror (errno));
    return ret;
  }
  af = sa.sa.sa_family;

  /* if this is an IPv4-mapped address then do IPv4 QoS */
  if (af == AF_INET6) {
    GST_DEBUG_OBJECT (sink, "check IP6 socket");
    if (IN6_IS_ADDR_V4MAPPED (&(sa.sa_in6.sin6_addr))) {
      GST_DEBUG_OBJECT (sink, "mapped to IPV4");
      af = AF_INET;
    }
  }
  /* extract and shift 6 bits of the DSCP */
  tos = (sink->qos_dscp & 0x3f) << 2;

  switch (af) {
    case AF_INET:
      ret = setsockopt (sink->fd.fd, IPPROTO_IP, IP_TOS, (void *) &tos,
          sizeof (tos));
      break;
    case AF_INET6:
#ifdef IPV6_TCLASS
      ret = setsockopt (sink->fd.fd, IPPROTO_IPV6, IPV6_TCLASS, (void *) &tos,
          sizeof (tos));
      break;
#endif
    default:
      GST_ERROR_OBJECT (sink, "unsupported AF");
      break;
  }
  if (ret) {
    GST_DEBUG_OBJECT (sink, "could not set DSCP: %s", g_strerror (errno));
  }

  return ret;
}
