#!/bin/sh
if [ -n "$1" ]; then
    export VAAPI_PKG_PREFIX=/opt/$1
else
    export VAAPI_PKG_PREFIX=/opt/xxx
fi

export VAAPI_PKG_LIBS="$VAAPI_PKG_PREFIX/lib"

PLATFORM_ARCH_64=`uname -a | grep x86_64`
if [ -n "$PKG_CONFIG_PATH" ]; then
    export PKG_CONFIG_PATH="$VAAPI_PKG_LIBS/pkgconfig/:$PKG_CONFIG_PATH"
elif [ -n "$PLATFORM_ARCH_64" ]; then
    export PKG_CONFIG_PATH="$VAAPI_PKG_LIBS/pkgconfig/:/usr/lib/pkgconfig/:/usr/lib/i386-linux-gnu/pkgconfig/"
else
    export PKG_CONFIG_PATH="$VAAPI_PKG_LIBS/pkgconfig/:/usr/lib/pkgconfig/:/usr/lib/x86_64-linux-gnu/pkgconfig/"
fi

echo "create installation path for gst-vaapi plugins: $VAAPI_PKG_LIBS/gstreamer-1.0"
sudo mkdir -p $VAAPI_PKG_LIBS/gstreamer-1.0

if [ -n "$GST_PLUGIN_PATH" ]; then
    export GST_PLUGIN_PATH="$VAAPI_PKG_LIBS/gstreamer-1.0:$GST_PLUGIN_PATH"
else
    export GST_PLUGIN_PATH="$VAAPI_PKG_LIBS/gstreamer-1.0"
fi

if [ -n "$LD_LIBRARY_PATH" ]; then
    export LD_LIBRARY_PATH="$VAAPI_PKG_LIBS:$LD_LIBRARY_PATH"
else
    export LD_LIBRARY_PATH="$VAAPI_PKG_LIBS"
fi
export PATH=/opt/$1/bin:$PATH

echo "*======================current configuration============================="
echo "* VAAPI_PKG_PREFIX:          $VAAPI_PKG_PREFIX"
echo "* VAAPI_PKG_LIBS:            $VAAPI_PKG_LIBS"
echo "* PKG_CONFIG_PATH:           $PKG_CONFIG_PATH"
echo "* GST_PLUGIN_PATH:           $GST_PLUGIN_PATH"
echo "* LD_LIBRARY_PATH:           $LD_LIBRARY_PATH"
echo "* PATH(GST 1.0 optional):    $PATH"
echo "*========================================================================="

echo "git clean -dxf && ./autogen.sh --prefix=\$VAAPI_PKG_PREFIX --enable-egl=no --enable-glx=no --enable-wayland=no && make && make install"
