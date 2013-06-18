Name:       gstreamer-vaapi0.10
Version:    0.4.0.pre1
Release:    0
Summary:    VA-API based plugins for GStreamer and helper libraries
Group:      Multimedia/Gstreamer
License:    LGPLv2+ and GPLv2+
URL:        http://gitorious.org/vaapi/gstreamer-vaapi
Source0:    %{name}-%{version}.tar.bz2
Source1001: packaging/gstreamer-vaapi.manifest
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(libva)
BuildRequires:  pkgconfig(gstreamer-0.10)
BuildRequires:  pkgconfig(gstreamer-basevideo-0.10)
BuildRequires:  pkgconfig(gstreamer-plugins-base-0.10)
BuildRequires:  pkgconfig(gles20)
BuildRequires:  pkgconfig(libdrm)
BuildRequires:  which
ExclusiveArch:  %{ix86}

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
Requires:   %{name} = %{version}-%{release}
Requires:   pkgconfig

%description devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.

%prep
%setup -q -n %{name}-%{version}

%build
cp %{SOURCE1001} .

./autogen.sh --prefix=/usr --enable-encoders
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
find %{buildroot} -regex ".*\.la$" | xargs rm -f --
find %{buildroot} -regex ".*\.a$" | xargs rm -f --

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%manifest gstreamer-vaapi.manifest
%defattr(-,root,root,-)
%doc AUTHORS COPYING.LIB NEWS README
%{_libdir}/*.so.*
%{_libdir}/gstreamer-0.10/*.so

%files devel
%manifest gstreamer-vaapi.manifest
%defattr(-,root,root,-)
%doc README COPYING.LIB
%{_includedir}/gstreamer-0.10/gst/vaapi
%{_libdir}/*.so
%{_libdir}/pkgconfig/*.pc
