plugin_LTLIBRARIES = libgstdepthsense.la

# sources used to compile this plug-in
libgstdepthsense_la_SOURCES = gstdepthsense.cpp \
                              gstdepthsensesrc.cpp

libgstdepthsense_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
$(GST_CXXFLAGS) $(DEPTHSENSE_CXXFLAGS)

# flags used to compile this module; add other _CFLAGS and _LIBS as needed
#
libgstdepthsense_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
                       $(GST_CFLAGS) $(DEPTHSENSE_CFLAGS)

libgstdepthsense_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(DEPTHSENSE_LIBS) \
                       $(GSTPB_BASE_LIBS) -lgstvideo-$(GST_API_VERSION)

libgstdepthsense_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdepthsense_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstdepthsensesrc.h
