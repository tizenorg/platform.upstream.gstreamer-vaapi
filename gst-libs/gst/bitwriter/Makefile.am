noinst_LTLIBRARIES =				\
	libgstvaapi-bitwriter.la		\
	$(NULL)

libgstvaapi_bitwriter_cflags =		\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(NULL)

libgstvaapi_bitwriter_libs =			\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(NULL)

nodist_libgstvaapi_bitwriter_la_SOURCES =	\
	gstbitwriter.c				\
	$(NULL)

libgstvaapi_bitwriter_la_CFLAGS =		\
	$(libgstvaapi_bitwriter_cflags)	\
	$(NULL)

libgstvaapi_bitwriter_la_LIBADD =		\
	$(libgstvaapi_bitwriter_libs)	\
	$(NULL)

libgstvaapi_bitwriter_la_LDFLAGS =		\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)
