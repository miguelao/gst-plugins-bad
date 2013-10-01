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
 * SECTION:element-opticalflow
 *
 * This element calculates the optical flow, i.e. the apparent motion of objects
 * as based in the luminance constancy assumption, whereby a pixel shifting its
 * position in time will move in consonance with its neighbours and this
 * displacement can be tracked via its luminance or other attributes'
 * movement. Several algorithms exist, some of them parametric or sparse,
 * notably the Lucas-Kanade and others dense, mainly the Horn-Schunck. These
 * algorithms are used for camera image stabilization, for instance, or to find
 * displacements hints for compression blocks in video encoders.
 *
 * The first implemented algorightm is the pyramidal Lucas-Kanade implementation
 * as described in [1] and implemented in OpenCV. This implementation only works
 * on single channel images, so a Gray version of the input is used. Searching
 * for interesting optical flows in the whole image would be prohibitive so this
 * search is carried only for "interesting points" or features as called by
 * Harris, which are points standing out from its neighbourhood. Roughly
 * speaking these points are points where the determinant of the luminance
 * Laplacian is minimum. OpenCV provides a conveninence function for this called
 * cvGoodFeaturesToTrack based on [2].
 *
 * Dense OF used to be follow in OpenCV based on Horn-Schunk algorithm, as
 * implemented in cvCalcOpticalFlowHS, but is deprecated in favour of the
 * Farneback algorithm [3]

 * [1] Jean-Yves Bouguet. Pyramidal Implementation of the Lucas Kanade Feature
 Tracker
 * [2] J. Shi and C. Tomasi. Good Features to Track. Proceedings of the IEEE
 Conference on Computer Vision and Pattern Recognition, pp. 593-600, June 1994.
 * [3] Gunnar Farneback, Two-frame motion estimation based on polynomial
 expansion, Lecture Notes in Computer Science, 2003, (2749), , 363-370.


 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 --gst-debug=opticalflow=4  v4l2src device=/dev/video0 ! videoconvert ! opticalflow test-mode=true ! videoconvert ! video/x-raw,width=320,height=240 ! ximagesink
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
  PROP_METHOD,
  PROP_TEST_MODE,
};

typedef enum
{
  METHOD_PYRLK,
  METHOD_FARNE
} GstOpticalflowMethod;

#define DEFAULT_TEST_MODE FALSE
#define DEFAULT_NUM_POINTS 200

#define DEFAULT_METHOD METHOD_PYRLK

#define GST_TYPE_OPTICALFLOW_METHOD (gst_opticalflow_method_get_type ())
static GType
gst_opticalflow_method_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {METHOD_PYRLK, "Pyramidal Lucas-Kanade algorithm", "pyrlk"},
      {METHOD_FARNE, "Dense Farneback algorithm",        "farne"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstOpticalflowMethod", values);
  }
  return etype;
}

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

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method",
          "Optical flow algorithm to use",
          "Optical flow algorithm to use",
          GST_TYPE_OPTICALFLOW_METHOD, DEFAULT_METHOD,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_TEST_MODE,
      g_param_spec_boolean ("test-mode", "test-mode",
          "If true, the output RGB is overwritten with the optical flow " \
          "recognised flows.",
          DEFAULT_TEST_MODE, (GParamFlags)
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "opticalflow",
      "Filter/Effect/Video",
      "Detects optical flows in the incoming RGB image outstanding points, and "
      "writes in the A channel the recognised vectors' end points in white.",
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
  filter->MAX_CORNERS = DEFAULT_NUM_POINTS;
  filter->method = DEFAULT_METHOD;
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}


static void
gst_opticalflow_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpticalflow *filter = GST_OPTICALFLOW (object);

  switch (prop_id) {
    case PROP_METHOD:
      filter->method = g_value_get_enum (value);
      break;
    case PROP_TEST_MODE:
      filter->test_mode = g_value_get_boolean (value);
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
    case PROP_METHOD:
      g_value_set_enum (value, filter->method);
      break;
    case PROP_TEST_MODE:
      g_value_set_boolean (value, filter->test_mode);
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

  of->cvA = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvB = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvC = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvD = cvCreateImage (size, IPL_DEPTH_8U, 1);

  of->cvGray0 = cvCreateImage (size, IPL_DEPTH_8U, 1);
  of->cvGray1 = cvCreateImage (size, IPL_DEPTH_8U, 1);

  of->features_found = new char[ of->MAX_CORNERS ];
  of->feature_errors = new float[ of->MAX_CORNERS ];
  of->pyr_sz = cvSize( in_info->width+8, in_info->height/3 );
  of->pyrA = cvCreateImage (of->pyr_sz, IPL_DEPTH_32F, 1);
  of->pyrB = cvCreateImage (of->pyr_sz, IPL_DEPTH_32F, 1);
  of->cornersA = new CvPoint2D32f[ of->MAX_CORNERS ];
  of->cornersB = new CvPoint2D32f[ of->MAX_CORNERS ];

  of->cvFlow = cvCreateMat (in_info->height, in_info->width, CV_32FC2);

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

  cvReleaseImage (&filter->cvA);
  cvReleaseImage (&filter->cvB);
  cvReleaseImage (&filter->cvC);
  cvReleaseImage (&filter->cvD);

  cvReleaseImage (&filter->cvGray0);
  cvReleaseImage (&filter->cvGray1);

  delete filter->features_found;
  delete filter->feature_errors;
  cvReleaseImage (&filter->pyrA);
  cvReleaseImage (&filter->pyrB);
  delete filter->cornersA;
  delete filter->cornersB;

  cvReleaseMat (&filter->cvFlow);
}

static GstFlowReturn
gst_opticalflow_transform_ip (GstVideoFilter * btrans, GstVideoFrame * frame)
{
  GstOpticalflow *of = GST_OPTICALFLOW (btrans);

  of->cvRGBAin->imageData = (char *) GST_VIDEO_FRAME_COMP_DATA (frame, 0);
  cvSplit (of->cvRGBAin, of->cvA, of->cvB, of->cvC, of->cvD);
  cvCvtColor (of->cvRGBAin, of->cvRGBin, CV_BGRA2BGR);
  cvCvtColor (of->cvRGBin, of->cvGray1, CV_BGR2GRAY);
  cvSet(of->cvD, cvScalar(0,0,0));

  if (of->method == METHOD_PYRLK){
    of->corner_count = of->MAX_CORNERS;
    cvGoodFeaturesToTrack( of->cvGray1, NULL, NULL,
        of->cornersA, &of->corner_count,
        0.05,  /* qualityLevel */
        5.0,  /* minDistance */
        0,  /* mask */
        3,  /* blockSize */
        0,  /* useHarrisDetector */
        0.04 );  /* k, free parameter of Harris detector. */

    /* Normally here we'd run a cvFindCornerSubPix but here we are not
    interested in that precision, that comes at a high CPU cost. */

    cvCalcOpticalFlowPyrLK( of->cvGray0, of->cvGray1,
        of->pyrA, of->pyrB, of->cornersA, of->cornersB, of->corner_count,
        cvSize( 25, 25 ), 5, of->features_found, of->feature_errors,
        cvTermCriteria( CV_TERMCRIT_ITER | CV_TERMCRIT_EPS, 20, 0.3 ), 0 );

    /* Regardless of test output, write a white dot per end-corner in cvD. */
    for (int i=0; i<of->corner_count; ++i){
      if (of->features_found[i]){
        /* cvSet2D is Matrix oriented so x and y are swapped with respect to
        what IplImage understands: x==cols, y==rows. */
        cvSet2D(of->cvD, (int)(of->cornersB[i].y), (int)(of->cornersB[i].x),
            cvScalar(255));
      }
    }

    cvMerge (of->cvA, of->cvB, of->cvC, of->cvD, of->cvRGBAin);
    if (of->test_mode) {
      CvPoint p0, p1;
      /* In test mode we draw the OF vectors on top of output RGB. */
      for (int i=0; i<of->corner_count; ++i){
        if (of->features_found[i]){
          p0 = cvPoint((int)(of->cornersA[i].x), (int)(of->cornersA[i].y));
          p1 = cvPoint((int)(of->cornersB[i].x), (int)(of->cornersB[i].y));
          cvLine(of->cvRGBAin, p0, p1, CV_RGB(0,255,0), 1);
        }
      }
    }

  } else if (of->method == METHOD_FARNE){
    cvCalcOpticalFlowFarneback(of->cvGray0, of->cvGray1, of->cvFlow,
        0.5,  /* pyr_scale */
        3,  /* levels*/
        15,  /* winsize */
        3,  /* iterations*/
        5,  /* poly_n */
        1.2,  /* poly_sigma */
        0);  /* flags */

    int x, y;
    cvSet(of->cvA, cvScalar(0,0,0));
    CvScalar color = CV_RGB(0,255,0);
    for( y = 0; y < of->width; y += 2){
      for( x = 0; x < of->height; x += 2){
        CvScalar fxy = cvGet2D(of->cvFlow, y, x); //CV_MAT_ELEM(*of->cvFlow, CvPoint2D32f, y, x);
        cvLine(of->cvA, cvPoint(x,y), cvPoint(cvRound(x+fxy.val[0]), cvRound(y+fxy.val[1])),
            color, 1, 8, 0);
        cvCircle(of->cvA, cvPoint(x,y), 2, color, -1, 8, 0);
      }
    }

    cvMerge (of->cvA, of->cvA, of->cvA, of->cvD, of->cvRGBAin);

  }

  /* Keep current frame for next iteration */
  cvCopy(of->cvGray1, of->cvGray0, NULL);

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
