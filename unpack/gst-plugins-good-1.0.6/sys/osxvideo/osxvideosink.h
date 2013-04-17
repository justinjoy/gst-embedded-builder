/* GStreamer
 * Copyright (C) 2004-6 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
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
 *
 *
 * The development of this code was made possible due to the involvement of Pioneers 
 * of the Inevitable, the creators of the Songbird Music player
 * 
 */
 
#ifndef __GST_OSX_VIDEO_SINK_H__
#define __GST_OSX_VIDEO_SINK_H__

#include <gst/video/gstvideosink.h>

#include <string.h>
#include <math.h>
#include <objc/runtime.h>
#include <Cocoa/Cocoa.h>

#include <QuickTime/QuickTime.h>
#import "cocoawindow.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_osx_video_sink);
#define GST_CAT_DEFAULT gst_debug_osx_video_sink

/* The hack doesn't work on leopard, the _CFMainPThread symbol
 * is doesn't exist in the CoreFoundation library */
#if MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_5
#ifdef RUN_NS_APP_THREAD
#undef RUN_NS_APP_THREAD
#endif
#endif

G_BEGIN_DECLS

#define GST_TYPE_OSX_VIDEO_SINK \
  (gst_osx_video_sink_get_type())
#define GST_OSX_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OSX_VIDEO_SINK, GstOSXVideoSink))
#define GST_OSX_VIDEO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OSX_VIDEO_SINK, GstOSXVideoSinkClass))
#define GST_IS_OSX_VIDEO_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OSX_VIDEO_SINK))
#define GST_IS_OSX_VIDEO_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OSX_VIDEO_SINK))

typedef struct _GstOSXWindow GstOSXWindow;

typedef struct _GstOSXVideoSink GstOSXVideoSink;
typedef struct _GstOSXVideoSinkClass GstOSXVideoSinkClass;

#define GST_TYPE_OSXVIDEOBUFFER (gst_osxvideobuffer_get_type())

/* OSXWindow stuff */
struct _GstOSXWindow {
  gint width, height;
  gboolean closed;
  gboolean internal;
  GstGLView* gstview;
  GstOSXVideoSinkWindow* win;
};

struct _GstOSXVideoSink {
  /* Our element stuff */
  GstVideoSink videosink;
  GstOSXWindow *osxwindow;
  void *osxvideosinkobject;
  NSView *superview;
  NSThread *ns_app_thread;
#ifdef RUN_NS_APP_THREAD
  GMutex loop_thread_lock;
  GCond loop_thread_cond;
#else
  guint cocoa_timeout;
#endif
  GMutex mrl_check_lock;
  GCond mrl_check_cond;
  gboolean mrl_check_done;
  gboolean main_run_loop_running;
  gboolean app_started;
  gboolean keep_par;
  gboolean embed;
};

struct _GstOSXVideoSinkClass {
  GstVideoSinkClass parent_class;
};

GType gst_osx_video_sink_get_type(void);

#if MAC_OS_X_VERSION_MAX_ALLOWED >= MAC_OS_X_VERSION_10_4
@interface NSApplication(AppleMenu)
- (void)setAppleMenu:(NSMenu *)menu;
@end
#endif

@interface GstBufferObject : NSObject
{
  @public
  GstBuffer *buf;
}

-(id) initWithBuffer: (GstBuffer *) buf;
@end


#if MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_5
@interface GstWindowDelegate : NSObject
#else
@interface GstWindowDelegate : NSObject <NSWindowDelegate>
#endif
{
  @public
  GstOSXVideoSink *osxvideosink;
}
-(id) initWithSink: (GstOSXVideoSink *) sink;
@end

@interface GstOSXVideoSinkObject : NSObject
{
  BOOL destroyed;

  @public
  GstOSXVideoSink *osxvideosink;
}

-(id) initWithSink: (GstOSXVideoSink *) sink;
-(void) createInternalWindow;
-(void) resize;
-(void) destroy;
-(void) showFrame: (GstBufferObject*) buf;
#ifdef RUN_NS_APP_THREAD
+ (BOOL) isMainThread;
-(void) nsAppThread;
-(void) checkMainRunLoop;
#endif
@end

G_END_DECLS

#endif /* __GST_OSX_VIDEO_SINK_H__ */

