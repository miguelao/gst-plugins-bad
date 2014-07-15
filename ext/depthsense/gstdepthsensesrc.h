/*
 * GStreamer
 * Copyright (C) 2014 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>
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

#ifndef __GST_DEPTHSeNSE_SRC_H__
#define __GST_DEPTHSeNSE_SRC_H__

#include <gst/gst.h>
#include <stdio.h>

#include <DepthSense.hxx>

using namespace DepthSense;
using namespace std;

#include <gst/base/gstbasesrc.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <pthread.h>

G_BEGIN_DECLS
#define GST_TYPE_DEPTHSENSE_SRC \
  (gst_depthsense_src_get_type())
#define GST_DEPTHSENSE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DEPTHSeNSE_SRC,GstDepthSenseSrc))
#define GST_DEPTHSENSE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DEPTHSeNSE_SRC,GstDepthsenseSrcClass))
#define GST_IS_DEPTHSENSE_SRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DEPTHSeNSE_SRC))
#define GST_IS_DEPTHSENSE_SRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DEPTHSeNSE_SRC))
typedef struct _GstDepthsenseSrc GstDepthSenseSrc;
typedef struct _GstDepthsenseSrcClass GstDepthSenseSrcClass;

typedef enum
{
  GST_DEPTHSENSE_SRC_FILE_TRANSFER,
  GST_DEPTHSENSE_SRC_NEXT_PROGRAM_CHAIN,
  GST_DEPTHSENSE_SRC_INVALID_DATA
} GstDepthsenseState;

struct _GstDepthsenseSrc
{
  GstPushSrc element;

  GstDepthsenseState state;
  gchar *uri_name;
  gint sourcetype;
  GstVideoInfo info;
  GstCaps *gst_caps;

  int width, height, fps;
  bool capturing;
  Context context_;
  DepthNode dnode_;
  pthread_t capture_thread_;
};

struct _GstDepthsenseSrcClass
{
  GstPushSrcClass parent_class;
};

GType gst_depthsense_src_get_type (void);
gboolean gst_depthsensesrc_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_DEPTHSENSE_SRC_H__ */
