cmake_minimum_required(VERSION 3.24)
# Version = major.minor from here + the commit count as the third number (set by build.ps1
# as COREPLAY_VERSION_FULL), so every pushed commit is a newer version for the update feed.
project(Coreplay VERSION 0.3.0 LANGUAGES CXX)
set(COREPLAY_VERSION_FULL "${PROJECT_VERSION}" CACHE STRING "Full version (build.ps1 sets it)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Network LinguistTools)
find_package(OpenImageIO CONFIG REQUIRED)
find_package(OpenColorIO CONFIG REQUIRED)
if(NOT WIN32)
    # Ubuntu's packages name include folders that are not there: OpenImageIO names
    # /usr/include/opencv4 (its optional OpenCV part, not used here), and OpenColorIO,
    # whose config sits in /usr/share/cmake, names /include. Keep the folders that exist.
    foreach(_t OpenImageIO::OpenImageIO OpenColorIO::OpenColorIO)
        get_target_property(_inc ${_t} INTERFACE_INCLUDE_DIRECTORIES)
        set(_keep "")
        foreach(_d IN LISTS _inc)
            if(EXISTS "${_d}" OR _d MATCHES "^\\$<")
                list(APPEND _keep "${_d}")
            endif()
        endforeach()
        set_target_properties(${_t} PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_keep}")
    endforeach()
endif()

if(WIN32)
    # libmpv: MPV_DIR points at a folder with include/mpv/*.h and an import library.
    set(MPV_DIR "${CMAKE_SOURCE_DIR}/third_party/mpv" CACHE PATH "libmpv SDK folder")
    find_path(MPV_INCLUDE_DIR mpv/client.h PATHS "${MPV_DIR}/include" REQUIRED NO_DEFAULT_PATH)
    find_library(MPV_LIBRARY NAMES mpv libmpv PATHS "${MPV_DIR}" "${MPV_DIR}/lib" REQUIRED NO_DEFAULT_PATH)
else()
    # Linux: the system's libmpv (libmpv-dev).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    set(MPV_INCLUDE_DIR "")
    set(MPV_LIBRARY PkgConfig::MPV)
endif()

add_executable(coreplay WIN32
    src/main.cpp
    src/Engine.h
    src/MainWindow.h        src/MainWindow.cpp
    src/Hotkeys.h           src/Hotkeys.cpp
    src/Lut.h               src/Lut.cpp
    src/Export.h            src/Export.cpp
    src/MovFix.h            src/MovFix.cpp
    src/Updater.h           src/Updater.cpp src/UpdateKey.h
    src/Subtitles.h         src/Subtitles.cpp
    src/SettingsDialog.h    src/SettingsDialog.cpp
    src/Compare.h
    src/CompareBar.h        src/CompareBar.cpp
    src/Playlist.h          src/Playlist.cpp
    src/Link.h              src/Link.cpp
    src/SeekBar.h           src/SeekBar.cpp
    src/Osd.h               src/Osd.cpp
    src/HotkeyOverlay.h     src/HotkeyOverlay.cpp
    src/I18n.h              src/I18n.cpp
    src/Grade.h             src/Grade.cpp
    src/GradeWindow.h       src/GradeWindow.cpp
    src/MpvEngine.h         src/MpvEngine.cpp
    src/seq/Sequence.h      src/seq/Sequence.cpp
    src/seq/ImageDecoder.h  src/seq/ImageDecoder.cpp
    src/seq/FrameCache.h    src/seq/FrameCache.cpp
    src/seq/SeqView.h       src/seq/SeqView.cpp
    src/seq/SequenceEngine.h src/seq/SequenceEngine.cpp
    res/coreplay.qrc
)
if(WIN32)
    target_sources(coreplay PRIVATE res/coreplay.rc)  # the .exe icon (Explorer, taskbar)
    # The resource compiler does not track the .ico it includes: rebuild when it changes.
    set_source_files_properties(res/coreplay.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_SOURCE_DIR}/res/coreplay.ico)
endif()
if(APPLE)
    # Coreplay.app; the icon (coreplay.icns, made from res/coreplay.png) is used when present.
    set_target_properties(coreplay PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME Coreplay
        MACOSX_BUNDLE_BUNDLE_NAME Coreplay
        MACOSX_BUNDLE_GUI_IDENTIFIER ai.imcore.coreplay
        MACOSX_BUNDLE_BUNDLE_VERSION "${COREPLAY_VERSION_FULL}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${COREPLAY_VERSION_FULL}")
    if(EXISTS "${CMAKE_SOURCE_DIR}/res/coreplay.icns")
        target_sources(coreplay PRIVATE res/coreplay.icns)
        set_source_files_properties(res/coreplay.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set_target_properties(coreplay PROPERTIES MACOSX_BUNDLE_ICON_FILE coreplay.icns)
    endif()
endif()
target_include_directories(coreplay PRIVATE src ${MPV_INCLUDE_DIR})
target_link_libraries(coreplay PRIVATE
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Network
    OpenImageIO::OpenImageIO OpenColorIO::OpenColorIO
    ${MPV_LIBRARY})
target_compile_definitions(coreplay PRIVATE COREPLAY_VERSION="${COREPLAY_VERSION_FULL}")
if(MSVC)
    # Fast start-up: the big media libraries load on first use, not at launch
    # (libmpv alone is ~120 MB).
    target_link_options(coreplay PRIVATE
        /DELAYLOAD:libmpv-2.dll /DELAYLOAD:OpenImageIO.dll /DELAYLOAD:OpenImageIO_Util.dll
        /DELAYLOAD:OpenColorIO_2_6.dll)
    target_link_libraries(coreplay PRIVATE delayimp bcrypt dwmapi)  # bcrypt: update signature check; dwmapi: title bar
elseif(APPLE)
    target_link_libraries(coreplay PRIVATE "-framework Security" "-framework CoreFoundation")  # update signature check
elseif(UNIX)
    target_link_libraries(coreplay PRIVATE ${CMAKE_DL_LIBS})  # the update check loads the system's libcrypto
endif()

# Interface languages (Settings > General > Language). English is the source text; each
# i18n/coreplay_<code>.ts is compiled to a .qm and built into the program as :/i18n/...
# Refresh the .ts files after changing strings: cmake --build build --target update_translations
set(COREPLAY_LANGUAGES zh_TW zh_CN ja ko es fr de pt ru th ar hi id vi ms fil)
set(COREPLAY_TS "")
foreach(_l IN LISTS COREPLAY_LANGUAGES)
    list(APPEND COREPLAY_TS "${CMAKE_SOURCE_DIR}/i18n/coreplay_${_l}.ts")
endforeach()
qt_add_translations(coreplay TS_FILES ${COREPLAY_TS} RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none)

option(COREPLAY_TESTS "Build the end-to-end test driver (--test-script)" OFF)
if(COREPLAY_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_sources(coreplay PRIVATE src/TestDriver.h src/TestDriver.cpp)
    target_link_libraries(coreplay PRIVATE Qt6::Test $<$<PLATFORM_ID:Windows>:dbghelp>)
    target_compile_definitions(coreplay PRIVATE COREPLAY_TESTS)
    if(MSVC)  # symbols in optimized test builds, for the crash handler's stack
        target_compile_options(coreplay PRIVATE /Zi)
        target_link_options(coreplay PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    endif()
    # Writes a test EXR that carries framesPerSecond in its header.
    add_executable(mkexr tools/mkexr.cpp)
    target_link_libraries(mkexr PRIVATE OpenImageIO::OpenImageIO)
endif()
if(MSVC)
    target_compile_options(coreplay PRIVATE /W3 /utf-8 /permissive-)
endif()
