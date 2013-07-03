noinst_PROGRAMS = \
	h264parser	\
	mpegvideoparser	\
	vc1parser	\
	$(NULL)

AM_CFLAGS = \
	-I$(top_srcdir)/gst-libs	\
	$(GST_PLUGINS_BASE_CFLAGS)	\
	-DGST_USE_UNSTABLE_API		\
	$(GST_CHECK_CFLAGS)		\
	$(GST_BASE_CFLAGS)		\
	$(GST_CFLAGS)			\
	$(NULL)

LDADD = \
	$(GST_PLUGINS_BASE_LIBS)	\
	$(GST_CHECK_LIBS)		\
	$(GST_BASE_LIBS)		\
	$(GST_LIBS)			\
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_MAJORMINOR).la

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
