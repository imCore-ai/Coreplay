# Coreplay for Mac: every library runs on macOS 12 and later (Homebrew's need the macOS they
# were built on: 15 for the 0.3.x downloads). Dynamic, so libmpv and Coreplay share one FFmpeg.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_OSX_ARCHITECTURES arm64)
set(VCPKG_OSX_DEPLOYMENT_TARGET 12.0)
# Never Homebrew's libraries: they need the macOS they were built on. libass linked Homebrew's
# fontconfig through pkg-config's default folders. build_deps.sh sets PKG_CONFIG_LIBDIR to an
# empty folder, so only vcpkg's own .pc files are seen; CMake skips Homebrew's prefixes.
set(VCPKG_ENV_PASSTHROUGH PKG_CONFIG_LIBDIR)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local")
