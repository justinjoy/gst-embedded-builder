/* GStreamer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 *               2009 Wim Taymans <wim.taymans@gmail.com>
 *
 * gstsegment.c: Unit test for segments
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

#include <gst/check/gstcheck.h>

static void
check_times (GstSegment * segment, guint64 position, guint64 stream_time,
    guint64 running_time)
{
  guint64 st, rt;

  st = gst_segment_to_stream_time (segment, segment->format, position);
  rt = gst_segment_to_running_time (segment, segment->format, position);

  fail_unless_equals_int64 (st, stream_time);
  fail_unless_equals_int64 (rt, running_time);
}

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_nosize)
{
  GstSegment segment;
  gboolean res;
  guint64 cstart, cstop;
  gboolean update;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  /* configure segment to start 100 */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 100, GST_SEEK_TYPE_NONE, -1, &update);
  fail_unless (segment.start == 100);
  fail_unless (segment.position == 100);
  fail_unless (segment.stop == -1);
  fail_unless (update == TRUE);
  /* appended after current position 0 */
  check_times (&segment, 100, 100, 0);

  /* do some clipping on the open range */
  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound, still outside of the segment */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* special case, 0 duration and outside segment */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 90, 90, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* special case, 0 duration and touching lower bound, i.e. inside segment */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 100, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 100);

  /* special case, 0 duration and inside the segment */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      120, 120, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 120);
  fail_unless (cstop == 120);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 200, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 200);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside, we don't know the stop */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == -1);

  /* move to 150, this is a running_time of 50 */
  segment.position = 150;
  check_times (&segment, 150, 150, 50);

  /* add 100 to start, set stop to 300 */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 100 + 100, GST_SEEK_TYPE_SET, 300, &update);
  fail_unless (segment.start == 200);
  fail_unless (segment.position == 200);
  fail_unless (segment.stop == 300);
  fail_unless (segment.base == 50);
  fail_unless (update == TRUE);
  check_times (&segment, 200, 200, 50);
  check_times (&segment, 250, 250, 100);

  update = FALSE;
  /* add 100 to start (to 300), set stop to 200, this is not allowed.
   * nothing should be updated in the segment. A g_warning is
   * emited. */
  ASSERT_CRITICAL (gst_segment_do_seek (&segment, 1.0,
          GST_FORMAT_BYTES,
          GST_SEEK_FLAG_NONE,
          GST_SEEK_TYPE_SET, 200 + 100, GST_SEEK_TYPE_SET, 200, &update));
  fail_unless (segment.start == 200);
  fail_unless (segment.position == 200);
  fail_unless (segment.stop == 300);
  fail_unless (segment.base == 50);
  /* update didn't change */
  fail_unless (update == FALSE);
  check_times (&segment, 200, 200, 50);
  check_times (&segment, 250, 250, 100);

  update = TRUE;
  /* seek relative to end, should not do anything since size is
   * unknown. */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_END, -300, GST_SEEK_TYPE_END, -100, &update);
  fail_unless (segment.start == 200);
  fail_unless (segment.position == 200);
  fail_unless (segment.stop == 300);
  fail_unless (segment.base == 50);
  fail_unless (update == FALSE);
  check_times (&segment, 250, 250, 100);

  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 200, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 250);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      200, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 250);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      250, 290, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 290);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      250, 350, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 300);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 300);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 200, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 200);
  fail_unless (cstop == 300);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 250, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 250);
  fail_unless (cstop == 300);

  /* start outside on boundary */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 300, -1, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 350, -1, &cstart, &cstop);
  fail_unless (res == FALSE);
}

GST_END_TEST;

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_size)
{
  GstSegment segment;
  gboolean res;
  guint64 cstart, cstop;
  gboolean update;

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.duration = 200;

  /* configure segment to start 100 */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 100, GST_SEEK_TYPE_NONE, -1, &update);
  fail_unless (segment.start == 100);
  fail_unless (segment.position == 100);
  fail_unless (segment.stop == -1);
  fail_unless (update == TRUE);
  check_times (&segment, 100, 100, 0);

  /* do some clipping on the open range */
  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 200, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 200);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == -1);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == -1);

  /* add 100 to start, set stop to 300, stop clips to 200 */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 100 + 100, GST_SEEK_TYPE_SET, 300, &update);
  fail_unless (segment.start == 200);
  fail_unless (segment.position == 200);
  fail_unless (segment.stop == 200);
  check_times (&segment, 200, 200, 0);

  /* add 100 to start (to 300), set stop to 200, this clips start
   * to duration */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 200 + 100, GST_SEEK_TYPE_SET, 200, &update);
  fail_unless (segment.start == 200);
  fail_unless (segment.position == 200);
  fail_unless (segment.stop == 200);
  fail_unless (update == FALSE);
  check_times (&segment, 200, 200, 0);

  /* seek relative to end */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_END, -100, GST_SEEK_TYPE_END, -20, &update);
  fail_unless (segment.start == 100);
  fail_unless (segment.position == 100);
  fail_unless (segment.stop == 180);
  fail_unless (update == TRUE);
  check_times (&segment, 150, 150, 50);

  /* completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 0, 50, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* inside, touching lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      100, 150, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 150);

  /* completely inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 170, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 170);

  /* partially inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES,
      150, 250, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 180);

  /* invalid start */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, -1, 100, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 50, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 180);

  /* start on lower bound */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 100, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 100);
  fail_unless (cstop == 180);

  /* start inside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 150, -1, &cstart, &cstop);
  fail_unless (res == TRUE);
  fail_unless (cstart == 150);
  fail_unless (cstop == 180);

  /* start outside on boundary */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 180, -1, &cstart, &cstop);
  fail_unless (res == FALSE);

  /* start completely outside */
  res = gst_segment_clip (&segment, GST_FORMAT_BYTES, 250, -1, &cstart, &cstop);
  fail_unless (res == FALSE);
}

GST_END_TEST;

GST_START_TEST (segment_seek_reverse)
{
  GstSegment segment;
  gboolean update;

  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.duration = 200;

  /* configure segment to stop 100 */
  gst_segment_do_seek (&segment, -1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 100, &update);
  fail_unless (segment.start == 0);
  fail_unless (segment.stop == 100);
  fail_unless (segment.time == 0);
  fail_unless (segment.position == 100);
  fail_unless (update == TRUE);
  check_times (&segment, 100, 100, 0);
  check_times (&segment, 50, 50, 50);
  check_times (&segment, 0, 0, 100);

  /* update */
  gst_segment_do_seek (&segment, -1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 10, GST_SEEK_TYPE_SET, 100 - 20, &update);
  fail_unless (segment.start == 10);
  fail_unless (segment.stop == 80);
  fail_unless (segment.time == 10);
  fail_unless (segment.position == 80);
  fail_unless (update == TRUE);
  check_times (&segment, 80, 80, 0);
  check_times (&segment, 40, 40, 40);
  check_times (&segment, 10, 10, 70);

  gst_segment_do_seek (&segment, -1.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_SET, 20, GST_SEEK_TYPE_NONE, 0, &update);
  fail_unless (segment.start == 20);
  fail_unless (segment.stop == 80);
  fail_unless (segment.time == 20);
  fail_unless (segment.position == 80);
  fail_unless (update == FALSE);
  check_times (&segment, 80, 80, 0);
  check_times (&segment, 20, 20, 60);
}

GST_END_TEST;

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_rate)
{
  GstSegment segment;
  gboolean update;

  gst_segment_init (&segment, GST_FORMAT_BYTES);

  /* configure segment to rate 2.0 */
  gst_segment_do_seek (&segment, 2.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_NONE, -1, &update);
  fail_unless (segment.format == GST_FORMAT_BYTES);
  fail_unless (segment.start == 0);
  fail_unless (segment.position == 0);
  fail_unless (segment.stop == -1);
  fail_unless (segment.rate == 2.0);
  fail_unless (update == FALSE);
  check_times (&segment, 50, 50, 25);

  /* set a real stop position, this must happen in bytes */
  gst_segment_do_seek (&segment, 3.0,
      GST_FORMAT_BYTES,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_SET, 100, &update);
  fail_unless (segment.format == GST_FORMAT_BYTES);
  fail_unless (segment.start == 0);
  fail_unless (segment.stop == 100);
  fail_unless (segment.rate == 3.0);
  /* no seek should happen, we just updated the stop position in forward
   * playback mode.*/
  fail_unless (update == FALSE);
  check_times (&segment, 60, 60, 20);

  /* set some duration, stop -1 END seeks will now work with the
   * duration, if the formats match */
  segment.duration = 200;
  fail_unless (segment.duration == 200);

  /* seek to end with 0 should set the stop to the duration */
  gst_segment_do_seek (&segment, 2.0,
      GST_FORMAT_BYTES, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_END, 0, &update);
  fail_unless (segment.stop == 200);
  fail_unless (segment.duration == 200);

  /* subtract 100 from the end */
  gst_segment_do_seek (&segment, 2.0,
      GST_FORMAT_BYTES, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_END, -100, &update);
  fail_unless (segment.stop == 100);
  fail_unless (segment.duration == 200);

  /* add 100 to the duration, this should be clamped to the duration */
  gst_segment_do_seek (&segment, 2.0,
      GST_FORMAT_BYTES, GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, -1, GST_SEEK_TYPE_END, 100, &update);
  fail_unless (segment.stop == 200);
  fail_unless (segment.duration == 200);
}

GST_END_TEST;

GST_START_TEST (segment_copy)
{
  GstSegment *copy;
  GstSegment segment = { 0.0, };

  /* this is a boxed type copy function, we support copying NULL */
  fail_unless (gst_segment_copy (NULL) == NULL);

  gst_segment_init (&segment, GST_FORMAT_TIME);

  segment.rate = -1.0;
  segment.applied_rate = 1.0;
  segment.start = 0;
  segment.stop = 200;
  segment.time = 0;

  copy = gst_segment_copy (&segment);
  fail_unless (copy != NULL);
  /* we inited the struct on the stack to zeroes, so direct comparison should
   * be ok here despite the padding field and regardless of implementation */
  fail_unless (memcmp (copy, &segment, sizeof (GstSegment)) == 0);
  gst_segment_free (copy);
}

GST_END_TEST;

/* mess with the segment structure in the bytes format */
GST_START_TEST (segment_seek_noupdate)
{
  GstSegment segment;
  gboolean update;

  gst_segment_init (&segment, GST_FORMAT_TIME);

  segment.start = 0;
  segment.position = 50;
  segment.stop = 200;
  segment.time = 0;

  /* doesn't change anything */
  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0, &update);
  fail_unless (update == FALSE);
  fail_unless (segment.format == GST_FORMAT_TIME);
  fail_unless (segment.start == 0);
  fail_unless (segment.stop == 200);
  fail_unless (segment.time == 0);
  fail_unless (segment.position == 50);
  fail_unless (segment.base == 50);
  fail_unless (segment.offset == 50);

  gst_segment_do_seek (&segment, 2.0,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_NONE,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0, &update);
  fail_unless (update == FALSE);
  fail_unless (segment.format == GST_FORMAT_TIME);
  fail_unless (segment.start == 0);
  fail_unless (segment.stop == 200);
  fail_unless (segment.time == 0);
  fail_unless (segment.position == 50);
  fail_unless (segment.base == 50);
  fail_unless_equals_int (segment.offset, 50);

  gst_segment_do_seek (&segment, 1.0,
      GST_FORMAT_TIME,
      GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_NONE, 0, GST_SEEK_TYPE_NONE, 0, &update);
  fail_unless (update == FALSE);
  fail_unless (segment.format == GST_FORMAT_TIME);
  fail_unless (segment.start == 0);
  fail_unless (segment.stop == 200);
  fail_unless (segment.time == 0);
  fail_unless (segment.position == 50);
  fail_unless (segment.base == 0);
  fail_unless (segment.offset == 50);
}

GST_END_TEST;


static Suite *
gst_segment_suite (void)
{
  Suite *s = suite_create ("GstSegment");
  TCase *tc_chain = tcase_create ("segments");

  tcase_set_timeout (tc_chain, 20);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, segment_seek_nosize);
  tcase_add_test (tc_chain, segment_seek_size);
  tcase_add_test (tc_chain, segment_seek_reverse);
  tcase_add_test (tc_chain, segment_seek_rate);
  tcase_add_test (tc_chain, segment_copy);
  tcase_add_test (tc_chain, segment_seek_noupdate);

  return s;
}

GST_CHECK_MAIN (gst_segment);
