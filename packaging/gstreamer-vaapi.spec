Name:       gstreamer-vaapi
Version:    0.5.4
Release:    0
Summary:    VA-API based plugins for GStreamer and helper libraries
Group:      Multimedia/Multimedia Framework
License:    LGPL-2.1+ and GPL-2.0+
URL:        http://gitorious.org/vaapi/gstreamer-vaapi
Source0:    %{name}-%{version}.tar.bz2
Source1001: gstreamer-vaapi.manifest
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(libva)
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-basevideo-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  which
ExclusiveArch:  %{ix86} x86_64

%description
Gstreamer-vaapi is a collection of VA-API based plugins for GStreamer
and helper libraries. vaapidecode is used to decode MPEG-2, MPEG-4,
H.264, VC-1, WMV3 videos to video/x-vaapi-surface surfaces, depending
on the underlying HW capabilities. vaapiconvert is used to convert from
video/x-raw-yuv pixels to video/x-vaapi-surface surfaces. vaapisink is
used to display video/x-vaapi-surface surfaces to the screen.

%package devel
Summary:    Development files for gstreamer-vaapi
Group:      Development/Libraries
Requires:   %{name} = %{version}
Requires:   pkgconfig

%description devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q

%build
cp %{SOURCE1001} .

%autogen --with-gstreamer-api=1.0
make %{?_smp_mflags}

%install
%make_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest gstreamer-vaapi.manifest
%license COPYING.LIB
%{_libdir}/*.so.*
%{_libdir}/gstreamer-1.0/*.so

%files devel
%manifest gstreamer-vaapi.manifest
%license COPYING.LIB
%dir %{_includedir}/gstreamer-1.0/gst/vaapi
%{_includedir}/gstreamer-1.0/gst/vaapi/*.h
%{_libdir}/*.so
%{_libdir}/pkgconfig/%{name}*.pc
