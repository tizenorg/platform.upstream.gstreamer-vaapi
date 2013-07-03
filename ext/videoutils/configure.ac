# videoutils package version number
m4_define([gst_videoutils_major_version], [0])
m4_define([gst_videoutils_minor_version], [10])
m4_define([gst_videoutils_micro_version], [36])
m4_define([gst_videoutils_pre_version],   [1])
m4_define([gst_videoutils_version],
          [gst_videoutils_major_version.gst_videoutils_minor_version.gst_videoutils_micro_version])
m4_if(gst_videoutils_pre_version, [0], [], [
m4_append([gst_videoutils_version], gst_videoutils_pre_version, [.pre])
])

# glib version number
m4_define([glib_major_version], [2])
m4_define([glib_minor_version], [28])
m4_define([glib_micro_version], [0])
m4_define([glib_major_minor_version],
          [glib_major_version.glib_minor_version])
m4_define([glib_version],
          [glib_major_version.glib_minor_version.glib_micro_version])

# gst version number
m4_define([gst_major_version], [0])
m4_define([gst_minor_version], [10])
m4_define([gst_micro_version], [36])
m4_define([gst_major_minor_version],
          [gst_major_version.gst_minor_version])
m4_define([gst_version],
          [gst_major_version.gst_minor_version.gst_micro_version])

# gst plugins-base version number
m4_define([gst_plugins_base_major_version], [0])
m4_define([gst_plugins_base_minor_version], [10])
m4_define([gst_plugins_base_micro_version], [31])
m4_define([gst_plugins_base_version],
          [gst_plugins_base_major_version.gst_plugins_base_minor_version.gst_plugins_base_micro_version])

AC_PREREQ([2.58])
AC_INIT([gst_videoutils], [gst_videoutils_version],
    [gwenole.beauchesne@intel.com],
    [gstreamer-videoutils])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.9 tar-ustar no-dist-gzip dist-bzip2 -Wno-portability])

dnl Versions for GStreamer and plugins-base
GST_MAJORMINOR=gst_major_minor_version
GST_API_VERSION=$GST_MAJORMINOR
GST_VERSION_REQUIRED=gst_version
GST_PLUGINS_BASE_VERSION_REQUIRED=gst_plugins_base_version
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(GST_API_VERSION)
AC_SUBST(GST_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BASE_VERSION_REQUIRED)

dnl Use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])], [
  AM_DEFAULT_VERBOSITY=1
  AC_SUBST(AM_DEFAULT_VERBOSITY)
])

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O

ORC_CHECK([0.4.11])

dnl Initialize libtool
LT_PREREQ([2.2])
LT_INIT

dnl Check for GLib
GLIB_VERSION_REQUIRED=glib_version
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= $GLIB_VERSION_REQUIRED])
AC_SUBST(GLIB_VERSION_REQUIRED)

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([0.9.12])

dnl GStreamer Core
PKG_CHECK_MODULES([GST],       [gstreamer-$GST_MAJORMINOR >= gst_version])
PKG_CHECK_MODULES([GST_BASE],  [gstreamer-base-$GST_MAJORMINOR >= gst_version])
PKG_CHECK_MODULES([GST_CHECK], [gstreamer-check-$GST_MAJORMINOR])

dnl GST_ALL_LDFLAGS:
dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

AC_CONFIG_FILES([
    Makefile
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/video/Makefile
    tests/Makefile
    tests/check/Makefile
    tests/check/libs/Makefile
])
AC_OUTPUT
