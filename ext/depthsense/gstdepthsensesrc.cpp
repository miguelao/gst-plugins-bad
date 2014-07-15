/*
 * GStreamer DepthSense device source element
 * Copyright (C) 2014 Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>

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
 * SECTION:element-depthsensesrc
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
  LD_LIBRARY_PATH=/usr/lib/DeptHSENSE/Drivers/ gst-launch-1.0 --gst-debug=depthsensesrc:5   depthsensesrc location='Downloads/mr.oni' sourcetype=depth ! videoconvert ! ximagesink
 * </programlisting>
 * <programlisting>
  LD_LIBRARY_PATH=/usr/lib/DeptHSENSE/Drivers/ gst-launch-1.0 --gst-debug=depthsensesrc:5   depthsensesrc location='Downloads/mr.oni' sourcetype=color ! videoconvert ! ximagesink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdepthsensesrc.h"

#include <string.h>
#include <vector>

GST_DEBUG_CATEGORY_STATIC (depthsensesrc_debug);
#define GST_CAT_DEFAULT depthsensesrc_debug
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("GRAY16_LE"))
    );

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_SOURCETYPE
};
typedef enum
{
  SOURCETYPE_DEPTH,
  SOURCETYPE_COLOR,
  SOURCETYPE_BOTH
} GstDepthSenseSourceType;
#define DEFAULT_SOURCETYPE  SOURCETYPE_DEPTH

#define SAMPLE_READ_WAIT_TIMEOUT 2000   /* 2000ms */

#define GST_TYPE_DEPTHSENSE_SRC_SOURCETYPE (gst_depthsense_src_sourcetype_get_type ())
static GType
gst_depthsense_src_sourcetype_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {SOURCETYPE_DEPTH, "Get depth readings", "depth"},
      {0, NULL, NULL},
    };
    etype = g_enum_register_static ("GstDepthSenseSrcSourcetype", values);
  }
  return etype;
}

/* GObject methods */
static void gst_depthsense_src_dispose (GObject * object);
static void gst_depthsense_src_finalize (GObject * gobject);
static void gst_depthsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_depthsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

/* basesrc methods */
static gboolean gst_depthsense_src_start (GstBaseSrc * bsrc);
static gboolean gst_depthsense_src_stop (GstBaseSrc * bsrc);
static gboolean gst_depthsense_src_set_caps (GstBaseSrc * src, GstCaps * caps);
static GstCaps *gst_depthsense_src_get_caps (GstBaseSrc * src, GstCaps * filter);
static gboolean gst_depthsensesrc_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query);

/* element methods */
static GstStateChangeReturn gst_depthsense_src_change_state (GstElement * element,
    GstStateChange transition);

/* pushsrc method */
static GstFlowReturn gst_depthsensesrc_fill (GstPushSrc * src, GstBuffer * buf);

/* DeptHSENSE interaction methods */
static gboolean depthsense_initialise_devices (GstDepthSenseSrc * src);
static GstFlowReturn depthsense_read_gstbuffer (GstDepthSenseSrc * src,
    GstBuffer * buf);

static void onNewDepthSample(DepthNode node,
                             DepthNode::NewSampleReceivedData data);

////////////////////////////////////////////////////////////////////////////////
static pthread_mutex_t g_capture_mutex =  PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t condc, condp;
enum {kProducerTurn, kConsumerTurn} consumer_or_producer = kProducerTurn;
bool finish_capture_thread;


#define parent_class gst_depthsense_src_parent_class
G_DEFINE_TYPE (GstDepthSenseSrc, gst_depthsense_src, GST_TYPE_PUSH_SRC);

static void
gst_depthsense_src_class_init (GstDepthSenseSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstPushSrcClass *pushsrc_class;
  GstBaseSrcClass *basesrc_class;
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gobject_class = (GObjectClass *) klass;
  basesrc_class = (GstBaseSrcClass *) klass;
  pushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->dispose = gst_depthsense_src_dispose;
  gobject_class->finalize = gst_depthsense_src_finalize;
  gobject_class->set_property = gst_depthsense_src_set_property;
  gobject_class->get_property = gst_depthsense_src_get_property;
  g_object_class_install_property
      (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Source uri, can be a file or a device.", "", (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_SOURCETYPE,
      g_param_spec_enum ("sourcetype",
          "Device source type",
          "Type of readings to get from the source",
          GST_TYPE_DEPTHSENSE_SRC_SOURCETYPE, DEFAULT_SOURCETYPE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));


  basesrc_class->start = GST_DEBUG_FUNCPTR (gst_depthsense_src_start);
  basesrc_class->stop = GST_DEBUG_FUNCPTR (gst_depthsense_src_stop);
  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_depthsense_src_get_caps);
  basesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_depthsense_src_set_caps);
  basesrc_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_depthsensesrc_decide_allocation);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (element_class,
      "DepthSense client source",
      "Source/Video",
      "Extract readings from a DepthSense supported device (DS325, Creative Senz etc). ",
      "Miguel Casas-Sanchez <miguelecasassanchez@gmail.com>");

  element_class->change_state = gst_depthsense_src_change_state;

  pushsrc_class->fill = GST_DEBUG_FUNCPTR (gst_depthsensesrc_fill);

  GST_DEBUG_CATEGORY_INIT (depthsensesrc_debug, "depthsensesrc", 0,
      "Depthsense Device Source");
}

static void
gst_depthsense_src_init (GstDepthSenseSrc * ds_src)
{
  gst_base_src_set_live (GST_BASE_SRC (ds_src), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (ds_src), GST_FORMAT_TIME);

  ds_src->width = 0;
  ds_src->height = 0;
  ds_src->capturing = false;
}

static void
gst_depthsense_src_dispose (GObject * object)
{
  GstDepthSenseSrc *ds_src = GST_DEPTHSENSE_SRC (object);

  if (ds_src->gst_caps)
    gst_caps_unref (ds_src->gst_caps);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_depthsense_src_finalize (GObject * gobject)
{
  G_OBJECT_CLASS (parent_class)->finalize (gobject);
}

static void
gst_depthsense_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDepthSenseSrc *depthsensesrc = GST_DEPTHSENSE_SRC (object);

  GST_OBJECT_LOCK (depthsensesrc);
  switch (prop_id) {
    case PROP_LOCATION:
      if (!g_value_get_string (value)) {
        GST_WARNING ("location property cannot be NULL");
        break;
      }

      if (depthsensesrc->uri_name != NULL) {
        g_free (depthsensesrc->uri_name);
        depthsensesrc->uri_name = NULL;
      }

      depthsensesrc->uri_name = g_value_dup_string (value);
      break;
    case PROP_SOURCETYPE:
      depthsensesrc->sourcetype = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (depthsensesrc);
}

static void
gst_depthsense_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDepthSenseSrc *depthsensesrc = GST_DEPTHSENSE_SRC (object);

  GST_OBJECT_LOCK (depthsensesrc);
  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, depthsensesrc->uri_name);
      break;
    case PROP_SOURCETYPE:
      g_value_set_enum (value, depthsensesrc->sourcetype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (depthsensesrc);
}

static void run_capture_thread(void* context) {
  ((Context*)(context))->run();
}

static gboolean
gst_depthsense_src_start (GstBaseSrc * bsrc)
{
  GstDepthSenseSrc *src = GST_DEPTHSENSE_SRC (bsrc);
  GST_INFO_OBJECT (src, "gst_depthsense_src_start");
  pthread_mutex_init(&g_capture_mutex, NULL);
  pthread_cond_init(&condc, NULL);    /* Initialize consumer condition variable */
  pthread_cond_init(&condp, NULL);    /* Initialize producer condition variable */

  src->context_.startNodes();

  finish_capture_thread = false;
  // context_.run() has to go on a thread of its own, otherwise it will starve
  // the Gstreamer depthsensesrc thread.
  pthread_create(&src->capture_thread_,
                 NULL,
                 (void* (*)(void*))&run_capture_thread,
                 (void*)&src->context_);
  src->capturing = true;
  return TRUE;
}

static gboolean
gst_depthsense_src_stop (GstBaseSrc * bsrc)
{
  GstDepthSenseSrc *src = GST_DEPTHSENSE_SRC (bsrc);
  GST_INFO_OBJECT (src, "gst_depthsense_src_stop");
  if (src->capturing) {
    src->context_.stopNodes();
    while (!src->context_.getRegisteredNodes().empty())
      src->context_.unregisterNode(src->context_.getRegisteredNodes().back());
    src->context_.quit();
    src->capturing = false;
  }
  finish_capture_thread = true;
  pthread_join(src->capture_thread_, NULL);

  return TRUE;
}

static GstCaps *
gst_depthsense_src_get_caps (GstBaseSrc * src, GstCaps * filter)
  {
  GstDepthSenseSrc *ds_src;
  GstCaps *caps;
  GstVideoInfo info;
  GstVideoFormat format;

  ds_src = GST_DEPTHSENSE_SRC (src);
  GST_INFO_OBJECT(src, "gst_depthsense_src_get_caps");

  GST_OBJECT_LOCK (ds_src);
  if (ds_src->gst_caps)
    goto out;

  if (!ds_src->dnode_.isSet())
    goto out;

  format = GST_VIDEO_FORMAT_GRAY16_LE;
  gst_video_info_init (&info);
  gst_video_info_set_format (&info, format, ds_src->width, ds_src->height);
  info.fps_n = ds_src->fps;
  info.fps_d = 1;
  caps = gst_video_info_to_caps (&info);

  GST_INFO_OBJECT (ds_src, "probed caps: %" GST_PTR_FORMAT, caps);
  ds_src->gst_caps = caps;

out:
  GST_OBJECT_UNLOCK (ds_src);

  if (!ds_src->gst_caps)
    return gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (ds_src));

  return (filter)
      ? gst_caps_intersect_full (filter, ds_src->gst_caps,
      GST_CAPS_INTERSECT_FIRST)
      : gst_caps_ref (ds_src->gst_caps);
}

static gboolean
gst_depthsense_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstDepthSenseSrc *ds_src;

  ds_src = GST_DEPTHSENSE_SRC (src);

  return gst_video_info_from_caps (&ds_src->info, caps);
}

static GstStateChangeReturn
gst_depthsense_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_FAILURE;
  GstDepthSenseSrc *src = GST_DEPTHSENSE_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Action! */
      if (!depthsense_initialise_devices (src))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
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
      gst_depthsense_src_stop (GST_BASE_SRC (src));
      if (src->gst_caps) {
        gst_caps_unref (src->gst_caps);
        src->gst_caps = NULL;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  return ret;
}


static GstFlowReturn
gst_depthsensesrc_fill (GstPushSrc * src, GstBuffer * buf)
{
  GstDepthSenseSrc *ds_src = GST_DEPTHSENSE_SRC (src);
  return depthsense_read_gstbuffer (ds_src, buf);
}

static gboolean
gst_depthsensesrc_decide_allocation (GstBaseSrc * bsrc, GstQuery * query)
{
  GstBufferPool *pool;
  guint size, min, max;
  gboolean update;
  GstStructure *config;
  GstCaps *caps;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &caps, NULL);
  gst_video_info_from_caps (&info, caps);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update = TRUE;
  } else {
    pool = NULL;
    min = max = 0;
    size = info.size;
    update = FALSE;
  }

  GST_DEBUG_OBJECT (bsrc, "allocation: size:%u min:%u max:%u pool:%"
      GST_PTR_FORMAT " caps:%" GST_PTR_FORMAT, size, min, max, pool, caps);

  if (!pool)
    pool = gst_video_buffer_pool_new ();

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);

  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    GST_DEBUG_OBJECT (pool, "activate Video Meta");
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }

  gst_buffer_pool_set_config (pool, config);

  if (update)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  return GST_BASE_SRC_CLASS (parent_class)->decide_allocation (bsrc, query);
}

gboolean
gst_depthsensesrc_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "depthsensesrc", GST_RANK_NONE,
      GST_TYPE_DEPTHSENSE_SRC);
}

static gboolean
depthsense_initialise_devices (GstDepthSenseSrc * src)
{
  GST_INFO_OBJECT (src, "depthsense_initialise_devices");
  src->context_ = Context::create("localhost");

  if (src->dnode_.isSet()) {
    GST_ERROR_OBJECT (src, "Depth node already set.");
    return FALSE;
  }

  // Get the list of currently connected devices
  vector<Device> da = src->context_.getDevices();

  // We are only interested in the first device
  if (da.size() < 1) {
    GST_ERROR_OBJECT (src, "Found no DepthSense devices");
    return FALSE;
  }
  GST_INFO_OBJECT (src, "Found %lu devices", da.size());

  vector<Node> na = da[0].getNodes();
  if ((int)na.size() == 0) {
    GST_ERROR_OBJECT (src, "Found no DepthSense nodes");
    return FALSE;
  }
  GST_INFO_OBJECT (src, "Found %lu nodes", na.size());

  size_t i = 0;
  for (; i < na.size(); ++i) {
    if (na[i].is<DepthNode>())
      break;
  }
  if (i == na.size()) {
    GST_ERROR_OBJECT (src, "Found no Depth node.");
    return FALSE;
  }
  Node node = na[i];

  src->dnode_ = node.as<DepthNode>();
  src->context_.registerNode(src->dnode_);

  const int kFrameRateDepth = 30;

  src->dnode_.newSampleReceivedEvent().connect(&onNewDepthSample);
  DepthNode::Configuration configRef(DepthSense::FRAME_FORMAT_QVGA,
                                     kFrameRateDepth,
                                     DepthNode::CAMERA_MODE_CLOSE_MODE,
                                     true);
  DepthNode::Configuration config = src->dnode_.getConfiguration();
  config.framerate = kFrameRateDepth;
  config.mode = DepthNode::CAMERA_MODE_CLOSE_MODE;
  config.saturation = true;
  config.frameFormat = DepthSense::FRAME_FORMAT_QVGA;
  src->width = 320;
  src->height = 240;

  src->dnode_.setEnableDepthMap(true);
  src->dnode_.setEnableConfidenceMap(true);
  try {
    src->context_.requestControl(src->dnode_, 0);
    src->dnode_.setConfiguration(config);
  } catch (ArgumentException& e) {
    GST_ERROR_OBJECT(src, "DEPTH Argument Exception: %s\n",e.what());
  } catch (UnauthorizedAccessException& e) {
    GST_ERROR_OBJECT(src, "DEPTH Unauthorized Access Exception: %s\n",e.what());
  } catch (IOException& e) {
    GST_ERROR_OBJECT(src, "DEPTH IO Exception: %s\n",e.what());
  } catch (InvalidOperationException& e) {
    GST_ERROR_OBJECT(src, "DEPTH Invalid Operation Exception: %s\n",e.what());
  } catch (ConfigurationException& e) {
    GST_ERROR_OBJECT(src, "DEPTH Configuration Exception: %s\n",e.what());
  } catch (StreamingException& e) {
    GST_ERROR_OBJECT(src, "DEPTH Streaming Exception: %s\n",e.what());
  } catch (TimeoutException&) {
    GST_ERROR_OBJECT(src, "DEPTH TimeoutException\n");
  }
  src->context_.releaseControl(src->dnode_);

  return TRUE;
}


const int kNumPixelsQVGA = 320 * 240;
const int16_t kConfidenceThreshold = 10;
const uint16_t kNoDepthDefault = 65535;
uint16_t pixelsDepthAcqQVGA[kNumPixelsQVGA];
uint16_t* const pixelsDepthAcq = pixelsDepthAcqQVGA;

static
void onNewDepthSample(DepthNode node, DepthNode::NewSampleReceivedData data) {

  int32_t w, h;
  FrameFormat_toResolution(data.captureConfiguration.frameFormat, &w, &h);
  //printf(" onNewDepthSample %dx%d\n", w, h);

  // Critical section entry protocol.
  pthread_mutex_lock(&g_capture_mutex);
  while (consumer_or_producer == kConsumerTurn)
    pthread_cond_wait(&condp, &g_capture_mutex);

  // Initialize raw depth and UV maps
  for (int pixel = 0; pixel < kNumPixelsQVGA; pixel++) {
    if (data.confidenceMap[pixel] > kConfidenceThreshold)
      pixelsDepthAcq[pixel] = data.depthMap[pixel];
    else
      pixelsDepthAcq[pixel] = kNoDepthDefault;
  }

  // Critical section exit protocol.
  consumer_or_producer = kConsumerTurn;
  pthread_cond_signal(&condc);  // wake up consumer
  pthread_mutex_unlock(&g_capture_mutex);

  if (finish_capture_thread)
    pthread_exit(NULL);
}

static GstFlowReturn
depthsense_read_gstbuffer (GstDepthSenseSrc * src, GstBuffer * buf)
{
  GST_DEBUG_OBJECT (src, "sending buffer (%dx%d)=%dB",
      src->width,
      src->height,
      kNumPixelsQVGA);
  GstVideoFrame vframe;

  gst_video_frame_map (&vframe, &src->info, buf, GST_MAP_WRITE);

  pthread_mutex_lock(&g_capture_mutex);
  while (consumer_or_producer == kProducerTurn)
    pthread_cond_wait(&condc, &g_capture_mutex);

  guint16 *pData = (guint16 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
  guint16 *pDepth = (guint16 *) pixelsDepthAcq;
  memcpy (pData, pDepth, 2 * src->height * src->width);
  gst_video_frame_unmap (&vframe);

  //Exit protocol
  consumer_or_producer = kProducerTurn;
  pthread_cond_signal(&condp);  // wake up producer
  pthread_mutex_unlock(&g_capture_mutex);

  GST_LOG_OBJECT (src, "Calculated PTS as %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_PTS (buf)));

  return GST_FLOW_OK;
}
