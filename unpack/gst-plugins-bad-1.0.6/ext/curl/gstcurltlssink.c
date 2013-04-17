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
 * SECTION:element-curltlssink
 * @short_description: sink that uploads data to a server using libcurl
 * @see_also:
 *
 * This is a network sink that uses libcurl.
 *
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
#include "gstcurltlssink.h"

/* Default values */
#define GST_CAT_DEFAULT                gst_curl_tls_sink_debug
#define DEFAULT_INSECURE               TRUE


/* Plugin specific settings */

GST_DEBUG_CATEGORY_STATIC (gst_curl_tls_sink_debug);

enum
{
  PROP_0,
  PROP_CA_CERT,
  PROP_CA_PATH,
  PROP_CRYPTO_ENGINE,
  PROP_INSECURE
};


/* Object class function declarations */

static void gst_curl_tls_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_curl_tls_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_curl_tls_sink_finalize (GObject * gobject);
static gboolean gst_curl_tls_sink_set_options_unlocked
    (GstCurlBaseSink * bcsink);

#define gst_curl_tls_sink_parent_class parent_class
G_DEFINE_TYPE (GstCurlTlsSink, gst_curl_tls_sink, GST_TYPE_CURL_BASE_SINK);

/* private functions */

static void
gst_curl_tls_sink_class_init (GstCurlTlsSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_curl_tls_sink_debug, "curltlssink", 0,
      "curl tls sink element");
  GST_DEBUG_OBJECT (klass, "class_init");

  gst_element_class_set_static_metadata (element_class,
      "Curl tls sink",
      "Sink/Network",
      "Upload data over TLS protocol using libcurl",
      "Patricia Muscalu <patricia@axis.com>");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_curl_tls_sink_finalize);

  gobject_class->set_property = gst_curl_tls_sink_set_property;
  gobject_class->get_property = gst_curl_tls_sink_get_property;

  klass->set_options_unlocked = gst_curl_tls_sink_set_options_unlocked;

  g_object_class_install_property (gobject_class, PROP_CA_CERT,
      g_param_spec_string ("ca-cert",
          "CA certificate",
          "CA certificate to use in order to verify the peer",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CA_PATH,
      g_param_spec_string ("ca-path",
          "CA path",
          "CA directory path to use in order to verify the peer",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CRYPTO_ENGINE,
      g_param_spec_string ("crypto-engine",
          "OpenSSL crypto engine",
          "OpenSSL crytpo engine to use for cipher operations",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_INSECURE,
      g_param_spec_boolean ("insecure",
          "Perform insecure SSL connections",
          "Allow curl to perform insecure SSL connections",
          DEFAULT_INSECURE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_curl_tls_sink_init (GstCurlTlsSink * sink)
{
  sink->ca_cert = NULL;
  sink->ca_path = NULL;
  sink->crypto_engine = NULL;
  sink->insecure = DEFAULT_INSECURE;
}

static void
gst_curl_tls_sink_finalize (GObject * gobject)
{
  GstCurlTlsSink *this = GST_CURL_TLS_SINK (gobject);

  GST_DEBUG ("finalizing curltlssink");

  g_free (this->ca_cert);
  g_free (this->ca_path);
  g_free (this->crypto_engine);

  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_curl_tls_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCurlTlsSink *sink;
  GstState cur_state;

  g_return_if_fail (GST_IS_CURL_TLS_SINK (object));
  sink = GST_CURL_TLS_SINK (object);

  gst_element_get_state (GST_ELEMENT (sink), &cur_state, NULL, 0);
  if (cur_state != GST_STATE_PLAYING && cur_state != GST_STATE_PAUSED) {
    GST_OBJECT_LOCK (sink);

    switch (prop_id) {
      case PROP_CA_CERT:
        g_free (sink->ca_cert);
        sink->ca_cert = g_value_dup_string (value);
        sink->insecure = FALSE;
        GST_DEBUG_OBJECT (sink, "ca_cert set to %s", sink->ca_cert);
        break;
      case PROP_CA_PATH:
        g_free (sink->ca_path);
        sink->ca_path = g_value_dup_string (value);
        sink->insecure = FALSE;
        GST_DEBUG_OBJECT (sink, "ca_path set to %s", sink->ca_path);
        break;
      case PROP_CRYPTO_ENGINE:
        g_free (sink->crypto_engine);
        sink->crypto_engine = g_value_dup_string (value);
        GST_DEBUG_OBJECT (sink, "crypto_engine set to %s", sink->crypto_engine);
        break;
      case PROP_INSECURE:
        sink->insecure = g_value_get_boolean (value);
        GST_DEBUG_OBJECT (sink, "insecure set to %d", sink->insecure);
        break;
    }

    GST_OBJECT_UNLOCK (sink);

    return;
  }

  GST_OBJECT_UNLOCK (sink);
}

static void
gst_curl_tls_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCurlTlsSink *sink;

  g_return_if_fail (GST_IS_CURL_TLS_SINK (object));
  sink = GST_CURL_TLS_SINK (object);

  switch (prop_id) {
    case PROP_CA_CERT:
      g_value_set_string (value, sink->ca_cert);
      break;
    case PROP_CA_PATH:
      g_value_set_string (value, sink->ca_path);
      break;
    case PROP_CRYPTO_ENGINE:
      g_value_set_string (value, sink->crypto_engine);
      break;
    case PROP_INSECURE:
      g_value_set_boolean (value, sink->insecure);
      break;
    default:
      GST_DEBUG_OBJECT (sink, "invalid property id");
      break;
  }
}

static gboolean
gst_curl_tls_sink_set_options_unlocked (GstCurlBaseSink * bcsink)
{
  GstCurlTlsSink *sink = GST_CURL_TLS_SINK (bcsink);

  if (!g_str_has_prefix (bcsink->url, "http"))
    curl_easy_setopt (bcsink->curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);

  /* crypto engine */
  if ((g_strcmp0 (sink->crypto_engine, "auto") == 0) ||
      (sink->crypto_engine == NULL)) {
    if (curl_easy_setopt (bcsink->curl, CURLOPT_SSLENGINE_DEFAULT, 1L)
        != CURLE_OK) {
      GST_WARNING ("Error setting up default SSL engine.");
    }
  } else {
    if (curl_easy_setopt (bcsink->curl, CURLOPT_SSLENGINE,
            sink->crypto_engine) == CURLE_SSL_ENGINE_NOTFOUND) {
      GST_WARNING ("Error setting up SSL engine: %s.", sink->crypto_engine);
    }
  }

  /* note that, using ca-path can allow libcurl to make SSL-connections much more
   * efficiently than using ca-cert if the ca-cert ile
   * contains many CA certificates. */
  if (sink->ca_cert != NULL && strlen (sink->ca_cert)) {
    GST_DEBUG ("setting ca cert");
    curl_easy_setopt (bcsink->curl, CURLOPT_CAINFO, sink->ca_cert);
  }

  if (sink->ca_path != NULL && strlen (sink->ca_path)) {
    GST_DEBUG ("setting ca path");
    curl_easy_setopt (bcsink->curl, CURLOPT_CAPATH, sink->ca_path);
  }

  if (!sink->insecure) {
    /* identify authenticity of the peer's certificate */
    curl_easy_setopt (bcsink->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    /* when CURLOPT_SSL_VERIFYHOST is 2, the commonName or subjectAltName
     * fields are verified */
    curl_easy_setopt (bcsink->curl, CURLOPT_SSL_VERIFYHOST, 2L);

    return TRUE;
  }

  /* allow "insecure" SSL connections and transfers */
  if (sink->insecure) {
    curl_easy_setopt (bcsink->curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt (bcsink->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  return TRUE;
}
