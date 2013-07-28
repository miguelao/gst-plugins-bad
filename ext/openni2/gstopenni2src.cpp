/*
 * GStreamer OpenNI2 device source element
 * Copyright (C) 2013 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>

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

/**
 * SECTION:element-openni2src
 *
 * OpenNI2 is a library to access 3D sensors such as those based on PrimeSense
 * depth sensor. Examples of such sensors are the Kinect used in Microsoft Xbox
 * consoles and Asus WAVI Xtion. Notably recordings of 3D sessions can also be
 * replayed as the original devices. See www.openni.org for more details.
 * 
 * OpenNI2 can be downloaded from source, compiled and installed in Linux, Mac
 * and Windows devices(https://github.com/OpenNI/OpenNI2). However is better to
 * rely on Debian packages as part of the PCL library (or http://goo.gl/0o87EB).
 * 
 * <refsect2>
 * <title>Examples</title>
 * <para>
 * Some recorded .oni files are available at:
 * <programlisting>
 *  http://people.cs.pitt.edu/~chang/1635/proj11/kinectRecord
 * </programlisting>
 *
 * <programlisting>
  LD_LIBRARY_PATH=/usr/lib/OpenNI2/Drivers/ GST_DEBUG=2 gst-launch-1.0 --gst-debug=openni2src:5   openni2src location='~/Downloads/mr.oni' ! fakesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstopenni2src.h"



GST_DEBUG_CATEGORY_STATIC (openni2src_debug);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;

enum
{
  PROP_0,
  PROP_LOCATION,
};

G_DEFINE_TYPE (GstOpenni2Src, gst_openni2_src, GST_TYPE_PUSH_SRC)

static void gst_openni2_src_dispose (GObject * object);
static void gst_openni2_src_finalize (GObject * gobject);

/* basesrc methods */
static gboolean gst_openni2_src_start (GstBaseSrc * bsrc);
static gboolean gst_openni2_src_stop (GstBaseSrc * bsrc);
static gboolean gst_openni2_src_get_size (GstBaseSrc * bsrc, guint64 * size);
static void gst_openni2_src_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_openni2_src_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* element methods */
static GstStateChangeReturn
gst_openni2_src_change_state (GstElement * element, GstStateChange transition);

/* pushsrc method */
static GstFlowReturn gst_openni2src_fill (GstPushSrc * src, GstBuffer * buf);

/* OpenNI2 interaction methods */
static GstFlowReturn gst_openni2src_initialise_devices(GstOpenni2Src *src);
static void gst_openni2src_finalise_devices(GstOpenni2Src *src);

static void gst_openni2_src_class_init (GstOpenni2SrcClass * klass)
{
  GObjectClass *gobject_class;
  GstPushSrcClass *pushsrc_class;
  GstBaseSrcClass *basesrc_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;
  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_openni2_src_dispose;
  gobject_class->finalize = gst_openni2_src_finalize;
  gobject_class->set_property = gst_openni2_src_set_property;
  gobject_class->get_property = gst_openni2_src_get_property;
  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Source uri, can be a file or a device.",
          "", (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_openni2_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_openni2_src_stop);
  basesrc_class->get_size = GST_DEBUG_FUNCPTR (gst_openni2_src_get_size);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (element_class, "Openni2 client source",
      "Source/Device",
      "Extract readings from an OpenNI supported device (Kinect etc). ",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  element_class->change_state = gst_openni2_src_change_state;

  pushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_openni2src_fill);
}

static void
gst_openni2_src_init (GstOpenni2Src * ni2src)
{
  gst_base_src_set_format (GST_BASE_SRC (ni2src), GST_FORMAT_BYTES);

  /* OpenNI2 initialisation inside this function */
  gst_openni2src_initialise_devices(ni2src);
}

static void
gst_openni2_src_dispose (GObject* object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_openni2_src_finalize (GObject * gobject)
{
  GstOpenni2Src *ni2src = GST_OPENNI2_SRC (gobject);

  gst_openni2src_finalise_devices (ni2src);

  if (ni2src->uri_name) {
    g_free (ni2src->uri_name);
    ni2src->uri_name = NULL;
  }


  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}


/*
 * create a socket for connecting to remote server 
 */
static gboolean
gst_openni2_src_start (GstBaseSrc * bsrc)
{
  GstOpenni2Src *src = GST_OPENNI2_SRC (bsrc);

  GString *chain_id_local = NULL;
  GstMessage *msg;

  msg = gst_message_new_duration_changed (GST_OBJECT (src));
  gst_element_post_message (GST_ELEMENT (src), msg);

  gst_element_post_message (GST_ELEMENT (src),
      gst_message_new_duration_changed (GST_OBJECT (src)));
#if 0
  gst_pad_push_event (GST_BASE_SRC_PAD (GST_BASE_SRC (src)),
      gst_event_new_new_segment (TRUE, 1.0,
          GST_FORMAT_BYTES, 0, src->content_size, 0));
#endif

  if (chain_id_local != NULL) {
    g_string_free (chain_id_local, TRUE);
    chain_id_local = NULL;
  }

  return TRUE;
}

static gboolean
gst_openni2_src_get_size (GstBaseSrc * bsrc, guint64 * size)
{
  gboolean ret = TRUE;

  return ret;
}

/*
 * close the socket and associated resources used both to recover from
 * errors and go to NULL state 
 */
static gboolean
gst_openni2_src_stop (GstBaseSrc * bsrc)
{
  GstOpenni2Src *src = GST_OPENNI2_SRC (bsrc);

  gst_openni2src_finalise_devices (src);
  return TRUE;
}

#if 0
static gboolean
gst_openni2_src_handle_event (GstPad * pad, GstEvent * event)
{
  GstOpenni2Src *src = GST_OPENNI2_SRC (GST_PAD_PARENT (pad));
  gint64 cont_size = 0;
  gboolean ret = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (src->live_tv) {
        cont_size = gst_openni2_src_get_position (src);
        if (cont_size > src->content_size) {
          src->content_size = cont_size;
          src->eos = FALSE;
        } else {
          src->eos = TRUE;
          gst_element_set_state (GST_ELEMENT (src), GST_STATE_NULL);
          gst_element_set_locked_state (GST_ELEMENT (src), FALSE);
        }
      }
      break;
    default:
      ret = gst_pad_event_default (pad, event);
  }
  GST_DEBUG_OBJECT (src, "HANDLE EVENT %d", ret);
  return ret;
}
#endif

static GstStateChangeReturn
gst_openni2_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstOpenni2Src *src = GST_OPENNI2_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!src->uri_name) {
        GST_ERROR_OBJECT (src, "Invalid location");
        return ret;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_openni2src_finalise_devices (src);
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_openni2_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpenni2Src *openni2src = GST_OPENNI2_SRC (object);

  GST_OBJECT_LOCK (openni2src);
  switch (prop_id) {
    case PROP_LOCATION:
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        break;
      }

      if (openni2src->uri_name != NULL) {
        g_free (openni2src->uri_name);
        openni2src->uri_name = NULL;
      }
      openni2src->uri_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (openni2src);
}

static void
gst_openni2_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpenni2Src *openni2src = GST_OPENNI2_SRC (object);

  GST_OBJECT_LOCK (openni2src);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, openni2src->uri_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (openni2src);
}

static GstFlowReturn
gst_openni2src_fill (GstPushSrc * src, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  return ret;
}

gboolean
gst_openni2src_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (openni2src_debug, "openni2src", 0,
      "OpenNI2 Device Source");

  return gst_element_register (plugin, "openni2src", GST_RANK_NONE,
      GST_TYPE_OPENNI2_SRC);
}




GstFlowReturn gst_openni2src_initialise_devices(GstOpenni2Src *src)
{
  /****** OpenNI2 specific stuff *********************************/
  openni::Status rc = openni::STATUS_OK;
  const char* deviceURI = openni::ANY_DEVICE;
  if (src->uri_name)
    deviceURI = src->uri_name;

  rc = openni::OpenNI::initialize();
  if (rc != openni::STATUS_OK) {
    GST_ERROR_OBJECT(src, "Initialization failed: %s",
		     openni::OpenNI::getExtendedError());
    openni::OpenNI::shutdown();
    return GST_FLOW_ERROR;
  }

  rc = src->device.open(deviceURI);
  if (rc != openni::STATUS_OK) {
    GST_ERROR_OBJECT(src, "Device (%s) open failed: %s", deviceURI,
		     openni::OpenNI::getExtendedError());
    openni::OpenNI::shutdown();
    return GST_FLOW_ERROR;
  }

  /** depth sensor **/
  rc = src->depth.create(src->device, openni::SENSOR_DEPTH);
  if (rc == openni::STATUS_OK) {
    rc = src->depth.start();
    if (rc != openni::STATUS_OK){
      GST_ERROR_OBJECT(src, "%s", openni::OpenNI::getExtendedError());
      src->depth.destroy();
    }
  }
  else
    GST_WARNING_OBJECT(src, "Couldn't find depth stream: %s", 
		       openni::OpenNI::getExtendedError());

  /** color sensor **/
  rc = src->color.create(src->device, openni::SENSOR_COLOR);
  if (rc == openni::STATUS_OK) {
    rc = src->color.start();
    if (rc != openni::STATUS_OK) {
      GST_ERROR_OBJECT(src, "Couldn't start color stream: %s ", 
	     openni::OpenNI::getExtendedError());
      src->color.destroy();
    }

    /* All this code should go to query_caps */
    //const openni::SensorInfo* info;
    //info = &(src->color.getSensorInfo());
    //const openni::Array<openni::VideoMode>& caps = info->getSupportedVideoModes();
    //for( int i = 0; i < caps.getSize(); ++i){
    //  printf("Supported video mode (%dx%d)@%d fps \n", 
    //	     caps[i].getResolutionX(), caps[i].getResolutionY(), caps[i].getFps());
    //}
  }
  else
    GST_WARNING_OBJECT(src, "Couldn't find color stream: %s", 
		       openni::OpenNI::getExtendedError());
 

  if (!src->depth.isValid() && !src->color.isValid()){
    GST_ERROR_OBJECT(src, "No valid streams. Exiting\n");
    openni::OpenNI::shutdown();
    return GST_FLOW_ERROR;
  }


  if (!src->depth.isValid() && !src->color.isValid()){
    src->depthVideoMode = src->depth.getVideoMode();
    src->colorVideoMode = src->color.getVideoMode();

    int depthWidth = src->depthVideoMode.getResolutionX();
    int depthHeight = src->depthVideoMode.getResolutionY();
    int colorWidth = src->colorVideoMode.getResolutionX();
    int colorHeight = src->colorVideoMode.getResolutionY();
    
    if (depthWidth == colorWidth && depthHeight == colorHeight){
      src->width = depthWidth;
      src->height = depthHeight;
    }
    else {
      GST_ERROR_OBJECT(src, "Error - expect color and depth to be"
        " in same resolution: D: %dx%d vs C: %dx%d",
	       depthWidth, depthHeight,
	       colorWidth, colorHeight);
      return GST_FLOW_ERROR;
    }
  }
  else if (src->depth.isValid()) {
    src->depthVideoMode = src->depth.getVideoMode();
    src->width = src->depthVideoMode.getResolutionX();
    src->height = src->depthVideoMode.getResolutionY();
  }
  else if (src->color.isValid()){
    src->colorVideoMode = src->color.getVideoMode();
    src->width = src->colorVideoMode.getResolutionX();
    src->height = src->colorVideoMode.getResolutionY();
  }
  else{
    GST_ERROR_OBJECT(src, "Expected at least one of the streams to be valid.");
    return GST_FLOW_ERROR;
  }
  GST_WARNING_OBJECT(src, "resolution: %dx%d", src->width, src->height);
  
  return GST_FLOW_OK;
}

static
void gst_openni2src_finalise_devices(GstOpenni2Src *src)
{
}
