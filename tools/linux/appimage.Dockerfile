# Build container for the Coreplay AppImage. AlmaLinux 9 = glibc 2.34, so the AppImage
# runs on Rocky/Alma/RHEL 9, Ubuntu 22.04+, Debian 12+, Fedora 35+ and newer.
#   docker build -t coreplay-appimage-build -f tools/linux/appimage.Dockerfile tools/linux
FROM almalinux:9
RUN dnf -y install epel-release dnf-plugins-core \
 && dnf config-manager --set-enabled crb \
 && dnf -y install --nogpgcheck https://mirrors.rpmfusion.org/free/el/rpmfusion-free-release-9.noarch.rpm \
 && dnf -y install gcc-toolset-14 cmake ninja-build git pkgconf-pkg-config file patchelf wget \
      qt6-qtbase-devel qt6-qtwayland qt6-qtsvg mesa-libGL-devel mesa-libEGL-devel \
      mpv-devel libtiff-devel libpng-devel libjpeg-turbo-devel libwebp-devel openjpeg2-devel \
      giflib-devel zlib-devel \
      xorg-x11-server-Xvfb xorg-x11-xauth mesa-dri-drivers python3 desktop-file-utils \
 && dnf -y swap ffmpeg-free ffmpeg --allowerasing \
 && dnf -y install ffmpeg \
 && dnf clean all
ENV PATH=/opt/rh/gcc-toolset-14/root/usr/bin:$PATH

# OpenColorIO 2.5 from upstream: Coreplay opens the built-in "ocio://studio-config-latest"
# (OCIO 2.3+); EL9 has 2.1.
RUN git clone --depth 1 -b v2.5.1 https://github.com/AcademySoftwareFoundation/OpenColorIO /tmp/ocio \
 && cmake -S /tmp/ocio -B /tmp/ocio/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/ocio \
      -DOCIO_INSTALL_EXT_PACKAGES=ALL -DOCIO_BUILD_APPS=OFF -DOCIO_BUILD_PYTHON=OFF \
      -DOCIO_BUILD_TESTS=OFF -DOCIO_BUILD_GPU_TESTS=OFF -DOCIO_BUILD_DOCS=OFF -DOCIO_BUILD_OPENFX=OFF \
 && ninja -C /tmp/ocio/b install && rm -rf /tmp/ocio

# Imath, OpenEXR and OpenImageIO from upstream, the versions the Windows build gets from
# vcpkg. EL9's OpenEXR 3.1 / OpenImageIO 2.4 decode ZIP EXR far slower and pull in GDAL,
# OpenCV, DCMTK, OpenVDB and Tesseract. Only the formats Coreplay reads are built.
RUN git clone --depth 1 -b v3.2.3 https://github.com/AcademySoftwareFoundation/Imath /tmp/imath \
 && cmake -S /tmp/imath -B /tmp/imath/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/oiio \
      -DBUILD_TESTING=OFF -DPYTHON=OFF \
 && ninja -C /tmp/imath/b install && rm -rf /tmp/imath \
 && git clone --depth 1 -b v3.5.4 https://github.com/AcademySoftwareFoundation/openexr /tmp/exr \
 && cmake -S /tmp/exr -B /tmp/exr/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/oiio \
      -DCMAKE_PREFIX_PATH=/opt/oiio -DBUILD_TESTING=OFF -DOPENEXR_BUILD_TOOLS=OFF -DOPENEXR_INSTALL_TOOLS=OFF \
      -DOPENEXR_BUILD_EXAMPLES=OFF -DOPENEXR_INSTALL_DOCS=OFF \
 && ninja -C /tmp/exr/b install && rm -rf /tmp/exr \
 && git clone --depth 1 -b v3.2.1.2 https://github.com/AcademySoftwareFoundation/OpenImageIO /tmp/oiio \
 && cmake -S /tmp/oiio -B /tmp/oiio/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/oiio \
      -DCMAKE_PREFIX_PATH="/opt/oiio;/opt/ocio" -DOpenImageIO_BUILD_MISSING_DEPS=all \
      -DOIIO_BUILD_TESTS=OFF -DOIIO_BUILD_TOOLS=OFF -DOIIO_BUILD_DOCS=OFF -DUSE_PYTHON=OFF -DBUILD_TESTING=OFF \
      -DUSE_QT=OFF -DUSE_OPENCV=OFF -DUSE_FFMPEG=OFF -DUSE_DCMTK=OFF -DUSE_OPENVDB=OFF -DUSE_PTEX=OFF \
      -DUSE_LIBRAW=OFF -DUSE_FREETYPE=OFF -DUSE_NUKE=OFF -DUSE_TBB=OFF -DUSE_LIBHEIF=OFF -DUSE_JXL=OFF \
      -DUSE_OPENCOLORIO=ON \
 && ninja -C /tmp/oiio/b install && rm -rf /tmp/oiio

# linuxdeploy (+ its Qt plugin) packs the AppImage; extracted so it runs without FUSE.
RUN mkdir -p /opt/tools && cd /opt/tools \
 && wget -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage \
 && wget -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage \
 && chmod +x *.AppImage \
 && for t in linuxdeploy linuxdeploy-plugin-qt; do \
      ./$t-x86_64.AppImage --appimage-extract >/dev/null && mv squashfs-root $t && rm $t-x86_64.AppImage; done \
 && ln -s /opt/tools/linuxdeploy/AppRun /usr/local/bin/linuxdeploy \
 && ln -s /opt/tools/linuxdeploy-plugin-qt/AppRun /usr/local/bin/linuxdeploy-plugin-qt
