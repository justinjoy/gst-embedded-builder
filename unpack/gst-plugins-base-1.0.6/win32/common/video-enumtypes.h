


#ifndef __GST_VIDEO_ENUM_TYPES_H__
#define __GST_VIDEO_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* enumerations from "video-format.h" */
GType gst_video_format_get_type (void);
#define GST_TYPE_VIDEO_FORMAT (gst_video_format_get_type())
GType gst_video_chroma_site_get_type (void);
#define GST_TYPE_VIDEO_CHROMA_SITE (gst_video_chroma_site_get_type())
GType gst_video_format_flags_get_type (void);
#define GST_TYPE_VIDEO_FORMAT_FLAGS (gst_video_format_flags_get_type())
GType gst_video_pack_flags_get_type (void);
#define GST_TYPE_VIDEO_PACK_FLAGS (gst_video_pack_flags_get_type())

/* enumerations from "video-color.h" */
GType gst_video_color_range_get_type (void);
#define GST_TYPE_VIDEO_COLOR_RANGE (gst_video_color_range_get_type())
GType gst_video_color_matrix_get_type (void);
#define GST_TYPE_VIDEO_COLOR_MATRIX (gst_video_color_matrix_get_type())
GType gst_video_transfer_function_get_type (void);
#define GST_TYPE_VIDEO_TRANSFER_FUNCTION (gst_video_transfer_function_get_type())
GType gst_video_color_primaries_get_type (void);
#define GST_TYPE_VIDEO_COLOR_PRIMARIES (gst_video_color_primaries_get_type())

/* enumerations from "video-info.h" */
GType gst_video_interlace_mode_get_type (void);
#define GST_TYPE_VIDEO_INTERLACE_MODE (gst_video_interlace_mode_get_type())
GType gst_video_flags_get_type (void);
#define GST_TYPE_VIDEO_FLAGS (gst_video_flags_get_type())

/* enumerations from "colorbalance.h" */
GType gst_color_balance_type_get_type (void);
#define GST_TYPE_COLOR_BALANCE_TYPE (gst_color_balance_type_get_type())

/* enumerations from "navigation.h" */
GType gst_navigation_command_get_type (void);
#define GST_TYPE_NAVIGATION_COMMAND (gst_navigation_command_get_type())
GType gst_navigation_query_type_get_type (void);
#define GST_TYPE_NAVIGATION_QUERY_TYPE (gst_navigation_query_type_get_type())
GType gst_navigation_message_type_get_type (void);
#define GST_TYPE_NAVIGATION_MESSAGE_TYPE (gst_navigation_message_type_get_type())
GType gst_navigation_event_type_get_type (void);
#define GST_TYPE_NAVIGATION_EVENT_TYPE (gst_navigation_event_type_get_type())
G_END_DECLS

#endif /* __GST_VIDEO_ENUM_TYPES_H__ */



