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
/*
 * SECTION:element-panography
 *
 * This element stitches two images together.


 * OpenCV Feature detection, matching and result drawing can be found in [1]
 *
 * [1] http://docs.opencv.org/doc/user_guide/ug_features2d.html

 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0       videotestsrc ! video/x-raw,width=320,height=240 ! disp0.sink_right      videotestsrc ! video/x-raw,width=320,height=240 ! disp0.sink_left      panography name=disp0 ! videoconvert ! ximagesink
 * ]|
 * Another example, with two png files representing a classical stereo matching,
 * downloadable from http://vision.middlebury.edu/stereo/submit/tsukuba/im4.png and
 * im3.png. Note here they are downloaded in ~ (home).
 * |[
gst-launch-1.0    multifilesrc  location=~/im3.png ! pngdec ! videoconvert  ! disp0.sink_right     multifilesrc  location=~/im4.png ! pngdec ! videoconvert ! disp0.sink_left panography   name=disp0 method=sbm     disp0.src ! videoconvert ! ximagesink
 * ]|
 * Yet another example with two cameras, which should be the same model, aligned etc.
 * |[
 gst-launch-1.0    v4l2src device=/dev/video1 ! video/x-raw,width=320,height=240 ! videoconvert  ! disp0.sink_right     v4l2src device=/dev/video0 ! video/x-raw,width=320,height=240 ! videoconvert ! disp0.sink_left panography   name=disp0 method=sgbm     disp0.src ! videoconvert ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <opencv2/contrib/contrib.hpp>
#include "gstpanography.h"

GST_DEBUG_CATEGORY_STATIC (gst_panography_debug);
#define GST_CAT_DEFAULT gst_panography_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_METHOD,
};

typedef enum
{
  METHOD_SURF
} GstPanographyMethod;

#define DEFAULT_METHOD METHOD_SURF

#define GST_TYPE_PANOGRAPHY_METHOD (gst_panography_method_get_type ())
static GType
gst_panography_method_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {METHOD_SURF, "SURF", "surf"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstPanographyMethod", values);
  }
  return etype;
}

/* the capabilities of the inputs and outputs.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

G_DEFINE_TYPE (GstPanography, gst_panography, GST_TYPE_ELEMENT);

static void gst_panography_finalize (GObject * object);
static void gst_panography_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_panography_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_panography_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_panography_handle_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_panography_handle_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_panography_chain_right (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_panography_chain_left (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static void gst_panography_release_all_pointers (GstPanography * filter);

static void initialise_panography (GstPanography * fs, int width, int height,
    int nchannels);

/* initialize the panography's class */
static void
gst_panography_class_init (GstPanographyClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;

  gobject_class->finalize = gst_panography_finalize;
  gobject_class->set_property = gst_panography_set_property;
  gobject_class->get_property = gst_panography_get_property;


  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Keypoint/Feature extractor to use",
          "Keypoint/Feature extractor to use",
          GST_TYPE_PANOGRAPHY_METHOD, DEFAULT_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->change_state = gst_panography_change_state;

  gst_element_class_set_static_metadata (element_class,
      "Two image panography (stitching) calculation",
      "Filter/Effect/Video",
      "Stitches two image sequences together.",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad callback functions
 * initialize instance structure
 */
static void
gst_panography_init (GstPanography * filter)
{
  filter->sinkpad_left =
      gst_pad_new_from_static_template (&sink_factory, "sink_left");
  gst_pad_set_event_function (filter->sinkpad_left,
      GST_DEBUG_FUNCPTR (gst_panography_handle_sink_event));
  gst_pad_set_query_function (filter->sinkpad_left,
      GST_DEBUG_FUNCPTR (gst_panography_handle_query));
  gst_pad_set_chain_function (filter->sinkpad_left,
      GST_DEBUG_FUNCPTR (gst_panography_chain_left));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad_left);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad_left);

  filter->sinkpad_right =
      gst_pad_new_from_static_template (&sink_factory, "sink_right");
  gst_pad_set_event_function (filter->sinkpad_right,
      GST_DEBUG_FUNCPTR (gst_panography_handle_sink_event));
  gst_pad_set_query_function (filter->sinkpad_right,
      GST_DEBUG_FUNCPTR (gst_panography_handle_query));
  gst_pad_set_chain_function (filter->sinkpad_right,
      GST_DEBUG_FUNCPTR (gst_panography_chain_right));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad_right);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad_right);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  g_mutex_init (&filter->lock);
  g_cond_init (&filter->cond);

  filter->method = DEFAULT_METHOD;
}

static void
gst_panography_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPanography *filter = GST_PANOGRAPHY (object);
  switch (prop_id) {
    case PROP_METHOD:
      filter->method = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_panography_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPanography *filter = GST_PANOGRAPHY (object);

  switch (prop_id) {
    case PROP_METHOD:
      g_value_set_enum (value, filter->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
static GstStateChangeReturn
gst_panography_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstPanography *fs = GST_PANOGRAPHY (element);
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&fs->lock);
      fs->flushing = true;
      g_cond_signal (&fs->cond);
      g_mutex_unlock (&fs->lock);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (&fs->lock);
      fs->flushing = false;
      g_mutex_unlock (&fs->lock);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_panography_parent_class)->change_state (element,
      transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_mutex_lock (&fs->lock);
      fs->flushing = true;
      g_cond_signal (&fs->cond);
      g_mutex_unlock (&fs->lock);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_mutex_lock (&fs->lock);
      fs->flushing = false;
      g_mutex_unlock (&fs->lock);
      break;
    default:
      break;
  }
  return ret;
}

static gboolean
gst_panography_handle_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event)
{
  gboolean ret = TRUE;
  GstPanography *fs = GST_PANOGRAPHY (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstVideoInfo info;
      gst_event_parse_caps (event, &caps);

      /* Critical section since both pads handle event sinking simultaneously */
      g_mutex_lock (&fs->lock);
      gst_video_info_from_caps (&info, caps);

      GST_INFO_OBJECT (pad, " Negotiating caps via event %" GST_PTR_FORMAT,
          caps);
      if (!gst_pad_has_current_caps (fs->srcpad)) {
        /* Init image info (widht, height, etc) and all OpenCV matrices */
        initialise_panography (fs, info.width, info.height,
            info.finfo->n_components);

        /* Initialise and keep the caps. Force them on src pad */
        fs->caps = gst_video_info_to_caps (&info);
        gst_pad_set_caps (fs->srcpad, fs->caps);

      } else if (!gst_caps_is_equal (fs->caps, caps)) {
        ret = FALSE;
      }
      g_mutex_unlock (&fs->lock);

      GST_INFO_OBJECT (pad,
          " Negotiated caps (result %d) via event: %" GST_PTR_FORMAT, ret,
          caps);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

static gboolean
gst_panography_handle_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstPanography *fs = GST_PANOGRAPHY (parent);
  gboolean ret = TRUE;
  GstCaps *template_caps;
  GstCaps *current_caps;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
      g_mutex_lock (&fs->lock);
      if (!gst_pad_has_current_caps (fs->srcpad)) {
        template_caps = gst_pad_get_pad_template_caps (pad);
        gst_query_set_caps_result (query, template_caps);
        gst_caps_unref (template_caps);
      } else {
        current_caps = gst_pad_get_current_caps (fs->srcpad);
        gst_query_set_caps_result (query, current_caps);
        gst_caps_unref (current_caps);
      }
      g_mutex_unlock (&fs->lock);
      ret = TRUE;
      break;
    case GST_QUERY_ALLOCATION:
      if (pad == fs->sinkpad_right)
        ret = gst_pad_peer_query (fs->srcpad, query);
      else
        ret = FALSE;
      break;
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}

static void
gst_panography_finalize (GObject * object)
{
  GstPanography *filter;

  filter = GST_PANOGRAPHY (object);
  gst_panography_release_all_pointers (filter);

  gst_caps_replace (&filter->caps, NULL);

  g_cond_clear (&filter->cond);
  g_mutex_clear (&filter->lock);
  G_OBJECT_CLASS (gst_panography_parent_class)->finalize (object);
}



static GstFlowReturn
gst_panography_chain_left (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPanography *fs;
  GstMapInfo info;

  fs = GST_PANOGRAPHY (parent);
  GST_DEBUG_OBJECT (pad, "processing frame from left");
  g_mutex_lock (&fs->lock);
  if (fs->flushing) {
    g_mutex_unlock (&fs->lock);
    return GST_FLOW_FLUSHING;
  }
  if (fs->buffer_left) {
    GST_DEBUG_OBJECT (pad, " right is busy, wait and hold");
    g_cond_wait (&fs->cond, &fs->lock);
    GST_DEBUG_OBJECT (pad, " right is free, continuing");
    if (fs->flushing) {
      g_mutex_unlock (&fs->lock);
      return GST_FLOW_FLUSHING;
    }
  }
  fs->buffer_left = buffer;

  if (!gst_buffer_map (buffer, &info, (GstMapFlags) GST_MAP_READWRITE)) {
    return GST_FLOW_ERROR;
  }
  if (fs->cvRGB_l)
    fs->cvRGB_l->imageData = (char *) info.data;

  GST_DEBUG_OBJECT (pad, "signalled right");
  g_cond_signal (&fs->cond);
  g_mutex_unlock (&fs->lock);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_panography_chain_right (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstPanography *fs;
  GstMapInfo info;
  GstFlowReturn ret;

  fs = GST_PANOGRAPHY (parent);
  GST_DEBUG_OBJECT (pad, "processing frame from right");
  g_mutex_lock (&fs->lock);
  if (fs->flushing) {
    g_mutex_unlock (&fs->lock);
    return GST_FLOW_FLUSHING;
  }
  if (fs->buffer_left == NULL) {
    GST_DEBUG_OBJECT (pad, " left has not provided another frame yet, waiting");
    g_cond_wait (&fs->cond, &fs->lock);
    GST_DEBUG_OBJECT (pad, " left has just provided a frame, continuing");
    if (fs->flushing) {
      g_mutex_unlock (&fs->lock);
      return GST_FLOW_FLUSHING;
    }
  }
  if (!gst_buffer_map (buffer, &info, (GstMapFlags) GST_MAP_READWRITE)) {
    g_mutex_unlock (&fs->lock);
    return GST_FLOW_ERROR;
  }
  if (fs->cvRGB_r)
    fs->cvRGB_r->imageData = (char *) info.data;

  /* Here do the business */
  GST_INFO_OBJECT (pad,
      "stitching frames, %dB (%dx%d) %d ch.", (int) info.size,
      fs->width, fs->height, fs->actualChannels);

  if (METHOD_SURF == fs->method) {
    cv::Mat mat_l(fs->cvRGB_l, true);
    cv::cvtColor(mat_l, *fs->cvGray_left, CV_RGB2GRAY);
    cv::Mat mat_r(fs->cvRGB_r, true);
    cv::cvtColor(mat_r, *fs->cvGray_right, CV_RGB2GRAY);

    /* Detect keypoints */
    cv::SurfFeatureDetector surf(400);
    fs->keypoints1.clear();
    surf.detect(*fs->cvGray_left, fs->keypoints1);
    fs->keypoints2.clear();
    surf.detect(*fs->cvGray_right, fs->keypoints2);

    // Now let's compute the descriptors.
    cv::SurfDescriptorExtractor extractor;
    cv::Mat descriptors1, descriptors2;
    extractor.compute(*fs->cvGray_left, fs->keypoints1, descriptors1);
    extractor.compute(*fs->cvGray_right, fs->keypoints2, descriptors2);

    // Let's match the descriptors.
    cv::FlannBasedMatcher matcher;
    cv::vector<cv::DMatch> matches;
    matcher.match(descriptors1, descriptors2, matches);

    // Quick calculation of max and min distances between keypoints
    double max_dist = 0; double min_dist = 100;
    for( int i = 0; i < descriptors1.rows; i++ ) {
      double dist = matches[i].distance;
      if( dist < min_dist ) min_dist = dist;
      if( dist > max_dist ) max_dist = dist;
    }
    GST_INFO_OBJECT(pad, "Max dist : %f, Min dist :%f", max_dist, min_dist );

    // Use only "good" matches (i.e. whose distance is less than 3*min_dist )
    std::vector<cv::DMatch> good_matches;
    for( int i = 0; i < descriptors1.rows; i++ ) {
      if( matches[i].distance < 3*min_dist ) {
        good_matches.push_back( matches[i]);
      }
    }


    bool draw_matches = false;
    if (draw_matches) {
      // Limit to 10 good matches.
      good_matches.erase(good_matches.begin()+10, good_matches.end());

      cv::Mat img_matches;
      drawMatches(*fs->cvGray_left, fs->keypoints1,
                  *fs->cvGray_right, fs->keypoints2,
                  good_matches, img_matches,
                  cv::Scalar::all(-1),
                  cv::Scalar::all(-1),
                  std::vector<char>(),
                  cv::DrawMatchesFlags::DEFAULT);
      GST_INFO_OBJECT(pad, "(%dx%d)", img_matches.cols, img_matches.rows );
      cv::resize(img_matches, mat_r, mat_r.size(), 0, 0);

      memcpy(fs->cvRGB_r->imageData, mat_r.data, fs->width*fs->height*3);

    } else {
      std::vector<cv::Point2f> obj;
      std::vector<cv::Point2f> scene;
      for( uint i = 0; i < good_matches.size(); i++ ) {
        obj.push_back(fs->keypoints1[good_matches[i].queryIdx].pt);
        scene.push_back(fs->keypoints2[good_matches[i].trainIdx].pt);
      }

         // Find the Homography Matrix
      cv::Mat H = findHomography(obj, scene, CV_RANSAC);
      // Use the Homography Matrix to warp the images
      cv::Mat result;
      warpPerspective(*fs->cvGray_left,
                      result,
                      H,
                      cv::Size(2*fs->width, 2*fs->height));
      cv::Mat half(result,cv::Rect(0, 0, fs->width, fs->height));
      fs->cvGray_right->copyTo(half);

      cv::resize(result, *fs->cvGray_right, fs->cvGray_right->size());
      cv::cvtColor(*fs->cvGray_right, mat_r, CV_GRAY2RGB);

      memcpy(fs->cvRGB_r->imageData, mat_r.data, fs->width*fs->height*3);
    }

  }

  GST_DEBUG_OBJECT (pad, " right has finished");
  gst_buffer_unmap (fs->buffer_left, &info);
  gst_buffer_unref (fs->buffer_left);
  fs->buffer_left = NULL;
  g_cond_signal (&fs->cond);
  g_mutex_unlock (&fs->lock);

  ret = gst_pad_push (fs->srcpad, buffer);
  return ret;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_panography_plugin_init (GstPlugin * panography)
{
  GST_DEBUG_CATEGORY_INIT (gst_panography_debug, "panography", 0,
      "Two image stitching - panography");
  return gst_element_register (panography, "panography", GST_RANK_NONE,
      GST_TYPE_PANOGRAPHY);
}

static void
initialise_panography (GstPanography * fs, int width, int height, int nchannels)
{
  fs->width = width;
  fs->height = height;
  fs->actualChannels = nchannels;

  fs->imgSize = cvSize (fs->width, fs->height);
  if (fs->cvRGB_r)
    gst_panography_release_all_pointers (fs);

  fs->cvRGB_r = cvCreateImageHeader (fs->imgSize, IPL_DEPTH_8U, fs->actualChannels);
  fs->cvRGB_l = cvCreateImageHeader (fs->imgSize, IPL_DEPTH_8U, fs->actualChannels);
  fs->cvGray_right = new cv::Mat(fs->height, fs->width, CV_8UC1);
  fs->cvGray_left  = new cv::Mat(fs->height, fs->width, CV_8UC1);

  /* SURF method. */
  if ((NULL != fs->cvRGB_r) && (NULL != fs->cvRGB_l))
    fs->surf = new cv::SurfFeatureDetector(400);
}

static void
gst_panography_release_all_pointers (GstPanography * filter)
{
  cvReleaseImage (&filter->cvRGB_r);
  cvReleaseImage (&filter->cvRGB_l);
  delete filter->cvGray_left;

  delete filter->surf;
}
