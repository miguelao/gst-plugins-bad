/*
 * GStreamer
 * Copyright (C) 2013 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_PANOGRAPHY_H__
#define __GST_PANOGRAPHY_H__

#include <gst/gst.h>
#include <cv.h>
#include <opencv2/features2d/features2d.hpp>

G_BEGIN_DECLS
/* #defines don't like whitespacey bits */
#define GST_TYPE_PANOGRAPHY \
  (gst_panography_get_type())
#define GST_PANOGRAPHY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PANOGRAPHY,GstPanography))
#define GST_PANOGRAPHY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PANOGRAPHY,GstPanographyClass))
#define GST_IS_PANOGRAPHY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PANOGRAPHY))
#define GST_IS_PANOGRAPHY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PANOGRAPHY))
typedef struct _GstPanography GstPanography;
typedef struct _GstPanographyClass GstPanographyClass;

struct _GstPanography
{
  GstElement element;

  GstPad *sinkpad_left, *sinkpad_right, *srcpad;
  GstCaps *caps;

  gint method;
  gboolean display;

  int width;
  int height;
  int actualChannels;

  GstBuffer *buffer_left;
  GMutex lock;
  GCond cond;
  gboolean flushing;

  long num_frame;

  CvSize imgSize;
  IplImage* cvRGB_r;
  IplImage* cvRGB_l;
  cv::Mat* cvGray_right;
  cv::Mat* cvGray_left;

  cv::Ptr<cv::FeatureDetector> detector;
  cv::Ptr<cv::DescriptorExtractor> extractor;
  cv::Ptr<cv::DescriptorMatcher> matcher;

  cv::vector<cv::KeyPoint> keypoints1, keypoints2;
  cv::Ptr<cv::Mat> descriptors1, descriptors2;
  cv::vector<cv::DMatch> matches;
  std::vector<cv::DMatch> good_matches;
};

struct _GstPanographyClass
{
  GstElementClass parent_class;
};

GType gst_panography_get_type (void);

gboolean gst_panography_plugin_init (GstPlugin * panography);

G_END_DECLS
#endif /* __GST_PANOGRAPHY_H__ */
