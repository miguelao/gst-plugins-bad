/*
 * GStreamer
 * Copyright (C) 2013 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>
 *
 * This library is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Library
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library
 * General Public License for more details. You should have received a copy
 * of the GNU Library General Public License along with this library; if
 * not, write to the Free Software Foundation, Inc., 51 Franklin St,
 * Fifth Floor, Boston, MA 02110-1301, USA. 
 */

#ifndef __GST_OPENNI2_SRC_H__
#define __GST_OPENNI2_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <stdio.h>

#include <OpenNI.h>

G_BEGIN_DECLS
#define GST_TYPE_OPENNI2_SRC \
  (gst_openni2_src_get_type())
#define GST_OPENNI2_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OPENNI2_SRC,GstOpenni2Src))
#define GST_OPENNI2_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OPENNI2_SRC,GstOpenni2SrcClass))
#define GST_IS_OPENNI2_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OPENNI2_SRC))
#define GST_IS_OPENNI2_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OPENNI2_SRC))
typedef struct _GstOpenni2Src GstOpenni2Src;
typedef struct _GstOpenni2SrcClass GstOpenni2SrcClass;

typedef enum
{
  GST_OPENNI2_SRC_FILE_TRANSFER,
  GST_OPENNI2_SRC_NEXT_PROGRAM_CHAIN,
  GST_OPENNI2_SRC_INVALID_DATA
} GstOpenni2State;

struct _GstOpenni2Src
{
  GstPushSrc element;

  GstOpenni2State state;
  gchar *uri_name;
  gchar *user_agent;
  gchar *live_chain_id;
  gint openni2_version;
  gint64 content_size;
  gint64 prev_content_size;
  gint64 content_size_last;
  guint64 bytes_read;
  gint64 read_offset;
  gboolean eos;
  gboolean do_start;
  gboolean unique_setup;
  gboolean live_tv;
  gboolean enable_timing_position;
  gint live_tv_id;
  gchar *channel_name;
  guint mode;

  /*
   * Openni2 capabilities 
   */
  GstCaps *openni2_caps;
  gboolean update_prog_chain;

  /*
   * stablish a maximum iteration value to the IS_RECORDING message 
   */
  guint wait_to_transfer;

  /* OpenNI2 needed variables */
  openni::Device device;
  openni::VideoStream depth, color;
};

struct _GstOpenni2SrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_openni2_src_get_type (void);
gboolean gst_openni2src_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_OPENNI2_SRC_H__ */
