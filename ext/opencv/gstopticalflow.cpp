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

/**
 * SECTION:element-Opticalflow
 *
 *
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 --gst-debug=opticalflow=4  v4l2src device=/dev/video0 ! videoconvert ! opticalflow ! videoconvert ! video/x-raw,width=320,height=240 ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/gst.h>
#include "gstopticalflow.h"
extern "C"
{
#include <gst/video/gstvideometa.h>
}
GST_DEBUG_CATEGORY_STATIC (gst_opticalflow_debug);
#define GST_CAT_DEFAULT gst_opticalflow_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_TEST_MODE,
  PROP_SCALE
};

#define DEFAULT_TEST_MODE FALSE
#define DEFAULT_SCALE 1.6

G_DEFINE_TYPE (GstOpticalflow, gst_opticalflow, GST_TYPE_VIDEO_FILTER);
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGBA")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGBA")));


static void gst_opticalflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_opticalflow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_opticalflow_transform_ip (GstVideoFilter * btrans,
    GstVideoFrame * frame);
static gboolean gst_opticalflow_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);

static void gst_opticalflow_release_all_pointers (GstOpticalflow * filter);

static gboolean gst_opticalflow_stop (GstBaseTransform * basesrc);

/* initialize the Opticalflow's class */
static void
gst_opticalflow_class_init (GstOpticalflowClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *video_class = (GstVideoFilterClass *) klass;

  gobject_class->set_property = gst_opticalflow_set_property;
  gobject_class->get_property = gst_opticalflow_get_property;

  btrans_class->stop = gst_opticalflow_stop;
  btrans_class->passthrough_on_same_caps = TRUE;

  video_class->transform_frame_ip = gst_opticalflow_transform_ip;
  video_class->set_info = gst_opticalflow_set_info;

  g_object_class_install_property (gobject_class, PROP_TEST_MODE,
      g_param_spec_boolean ("test-mode", "test-mode",
          "If true, the output RGB is overwritten with the optical flow " \
          "recognised flows.",
          DEFAULT_TEST_MODE, (GParamFlags)
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SCALE,
      g_param_spec_float ("scale", "scale",
          "Grow factor for the face bounding box, if present", 1.0,
          4.0, DEFAULT_SCALE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "opticalflow",
      "Filter/Effect/Video",
      "Runs Opticalflow algorithm on input RGB.",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_opticalflow_init (GstOpticalflow * filter)
{
  filter->test_mode = DEFAULT_TEST_MODE;
  filter->scale = DEFAULT_SCALE;
  filter->MAX_CORNERS = 100;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}


static void
gst_opticalflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpticalflow *Opticalflow = GST_OPTICALFLOW (object);

  switch (prop_id) {
    case PROP_TEST_MODE:
      Opticalflow->test_mode = g_value_get_boolean (value);
      break;
    case PROP_SCALE:
      Opticalflow->scale = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opticalflow_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpticalflow *filter = GST_OPTICALFLOW (object);

  switch (prop_id) {
    case PROP_TEST_MODE:
      g_value_set_boolean (value, filter->test_mode);
      break;
    case PROP_SCALE:
      g_value_set_float (value, filter->scale);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */
/* this function handles the link with other elements */
static gboolean
gst_opticalflow_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstOpticalflow *of = GST_OPTICALFLOW (filter);
  CvSize size;

  size = cvSize (in_info->width, in_info->height);
  /* If cvRGBA is already allocated, it means there's a cap modification,
     so release first all the images.                                      */
  if (NULL != of->cvRGBAin)
    gst_opticalflow_release_all_pointers (of);

  of->cvRGBAin = cvCreateImageHeader (size, IPL_DEPTH_8U, 4);
  of->cvRGBin = cvCreateImage (size, IPL_DEPTH_8U, 3);

  of->eig_image = cvCreateImage( size, IPL_DEPTH_32F, 1 );
  of->tmp_image = cvCreateImage( size, IPL_DEPTH_32F, 1 );

  of->pyr_sz = cvSize( in_info->width+8, in_info->height/3 );
  of->pyrA = cvCreateImage( of->pyr_sz, IPL_DEPTH_32F, 1 );
  of->pyrB = cvCreateImage( of->pyr_sz, IPL_DEPTH_32F, 1 );

  of->cvA = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvB = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvC = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvD = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvA_prev = cvCreateImage (size, IPL_DEPTH_8U, 1);

  of->features_found = new char[ of->MAX_CORNERS ];
  of->feature_errors = new float[ of->MAX_CORNERS ];

  of->corner_count = of->MAX_CORNERS;
  of->cornersA = new CvPoint2D32f[ of->MAX_CORNERS ];
  of->cornersB = new CvPoint2D32f[ of->MAX_CORNERS ];

  return TRUE;
}

/* Clean up */
static gboolean
gst_opticalflow_stop (GstBaseTransform * basesrc)
{
  GstOpticalflow *filter = GST_OPTICALFLOW (basesrc);

  if (filter->cvRGBAin != NULL)
    gst_opticalflow_release_all_pointers (filter);

  return TRUE;
}

static void
gst_opticalflow_release_all_pointers (GstOpticalflow * filter)
{
  cvReleaseImage (&filter->cvRGBAin);
  cvReleaseImage (&filter->cvRGBin);

  cvReleaseImage (&filter->eig_image);
  cvReleaseImage (&filter->tmp_image);
  cvReleaseImage (&filter->pyrA);
  cvReleaseImage (&filter->pyrB);

  cvReleaseImage (&filter->cvA);
  cvReleaseImage (&filter->cvB);
  cvReleaseImage (&filter->cvC);
  cvReleaseImage (&filter->cvD);
  cvReleaseImage (&filter->cvA_prev);

  delete filter->features_found;
  delete filter->feature_errors;
  delete filter->cornersA;
  delete filter->cornersB;
}

static GstFlowReturn
gst_opticalflow_transform_ip (GstVideoFilter * btrans, GstVideoFrame * frame)
{
  GstOpticalflow *gc = GST_OPTICALFLOW (btrans);

  gc->cvRGBAin->imageData = (char *) GST_VIDEO_FRAME_COMP_DATA (frame, 0);

  /*  normally input should be RGBA */
  cvSplit (gc->cvRGBAin, gc->cvA, gc->cvB, gc->cvC, gc->cvD);
  cvCvtColor (gc->cvRGBAin, gc->cvRGBin, CV_BGRA2BGR);

  /* Run optical flow now */
  cvGoodFeaturesToTrack( gc->cvA, gc->eig_image, gc->tmp_image,
      gc->cornersA, &gc->corner_count,
      0.05,  // qualityLevel
      5.0,  // minDistance
      0,  // mask
      3,  // blockSize
      0,  // useHarrisDetector
      0.04 );  // k, free parameter of Harris detector.

  cvFindCornerSubPix( gc->cvA, gc->cornersA, gc->corner_count,
      cvSize( 15, 15 ),
      cvSize( -1, -1 ),
      cvTermCriteria( CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, 0.03 ));

  cvCalcOpticalFlowPyrLK( gc->cvA_prev, gc->cvA,
      gc->pyrA, gc->pyrB, gc->cornersA, gc->cornersB, gc->corner_count,
      cvSize( 15, 15 ), 5, gc->features_found, gc->feature_errors,
      cvTermCriteria( CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, 0.3 ), 0 );

  printf(" Current count is %d\n", gc->corner_count);
  /* Keep current frame for next iteration */
  cvCopy(gc->cvA, gc->cvA_prev, NULL);

  /*  if we want to display, just overwrite the output */
  if (gc->test_mode) {

  }

  cvMerge (gc->cvA, gc->cvB, gc->cvC, gc->cvD, gc->cvRGBAin);

  CvPoint p0, p1;
  if (gc->test_mode) {
    for (int i=0; i<gc->corner_count; ++i){
      p0 = cvPoint((int)(gc->cornersA[i].x), (int)(gc->cornersA[i].y));
      p1 = cvPoint((int)(gc->cornersB[i].x), (int)(gc->cornersB[i].y));
      cvLine( gc->cvRGBAin, p0, p1, CV_RGB(255,0,0), 2 );
    }
  }

  return GST_FLOW_OK;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_opticalflow_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   */
  GST_DEBUG_CATEGORY_INIT (gst_opticalflow_debug, "opticalflow",
      0, "opticalflow");

  return gst_element_register (plugin, "opticalflow", GST_RANK_NONE,
      GST_TYPE_OPTICALFLOW);
}
