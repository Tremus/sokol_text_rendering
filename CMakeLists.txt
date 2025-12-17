cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(text
    VERSION 0.0.1
    DESCRIPTION ""
    LANGUAGES C CXX)
set(COMPANY_NAME "exacoustics")
set(COMPANY_EMAIL "example@email.com")

set(PLUGIN_BUNDLE_ID "com.${COMPANY_NAME}.${PROJECT_NAME}.plugin")
set(APP_BUNDLE_ID "com.${COMPANY_NAME}.${PROJECT_NAME}.app")
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_TAGS "<string>Filter</string>") # Predefined tags are: Bass, Delay, Distortion, Drums, Dynamics, Dynamics Processor, Effects, Equalizer, Filter, Format Converter, Guitar, Imaging, MIDI, Mixer, Offline Effect, Output, Panner, Pitch, Reverb, Sampler, Synthesizer, Time Effect, Vocal. But you can use others.
set(MACOSX_BUNDLE_TYPE "aufx") # "aufx" for Effect, "augn" for Generator, "aumu" for Instrument, "aufm" for Music Effect.
set(MACOSX_BUNDLE_SUBTYPE "Txtt") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
set(MACOSX_BUNDLE_MANUFACTURER "ExAc") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.

include(modules/wip_libs/src/cplug_plugin.cmake)

list(APPEND PLUGIN_DEFINITIONS
    STBI_NO_STDIO
    STBI_NO_LINEAR
    NVG_NO_STB
    )

set(PLUGIN_SOURCES
    src/plugin.c
    src/gui.c

    src/libs/kb_text_shape.c
    src/libs/stb_rect_pack.c
    src/libs/stb_image.c
    src/libs/stb_truetype.c
    )

set(PLUGIN_INCLUDE
    modules/xhl/include
    modules/CPLUG/src/
    modules/sokol_gfx_multiinstance/
    modules/freetype/include

    src/libs
    src/shaders/
    )

if (WIN32)
    list(APPEND PLUGIN_SOURCES 
        src/platform_win.c
        modules/sokol_gfx_multiinstance/sokol_gfx_d3d11.c
        modules/CPLUG/src/cplug_extensions/window_win.c
        )
elseif(APPLE)
    list(APPEND PLUGIN_SOURCES
        src/platform_osx.m
        modules/sokol_gfx_multiinstance/sokol_gfx_metal.m
        modules/CPLUG/src/cplug_extensions/window_osx.m
        )
endif()


# ███████╗██████╗ ███████╗███████╗████████╗██╗   ██╗██████╗ ███████╗
# ██╔════╝██╔══██╗██╔════╝██╔════╝╚══██╔══╝╚██╗ ██╔╝██╔══██╗██╔════╝
# █████╗  ██████╔╝█████╗  █████╗     ██║    ╚████╔╝ ██████╔╝█████╗
# ██╔══╝  ██╔══██╗██╔══╝  ██╔══╝     ██║     ╚██╔╝  ██╔═══╝ ██╔══╝
# ██║     ██║  ██║███████╗███████╗   ██║      ██║   ██║     ███████╗
# ╚═╝     ╚═╝  ╚═╝╚══════╝╚══════╝   ╚═╝      ╚═╝   ╚═╝     ╚══════╝

set(FREETYPE_SRC
    # modules/freetype/src/bzip2/ftbzip2.c
    modules/freetype/src/base/ftbdf.c
    modules/freetype/src/base/ftbbox.c
    modules/freetype/src/base/ftpatent.c
    modules/freetype/src/base/ftcid.c
    modules/freetype/src/base/ftwinfnt.c
    modules/freetype/src/base/ftsynth.c
    modules/freetype/src/base/ftmm.c
    modules/freetype/src/base/ftstroke.c
    modules/freetype/src/base/ftpfr.c
    modules/freetype/src/base/ftfstype.c
    modules/freetype/src/base/ftotval.c
    modules/freetype/src/base/fttype1.c
    modules/freetype/src/base/ftinit.c
    modules/freetype/src/base/ftgxval.c
    modules/freetype/src/base/ftgasp.c
    modules/freetype/src/base/ftglyph.c
    modules/freetype/src/base/ftbitmap.c
    modules/freetype/src/bdf/bdf.c
    modules/freetype/src/cache/ftcache.c
    modules/freetype/src/cid/type1cid.c
    modules/freetype/src/lzw/ftlzw.c
    modules/freetype/src/svg/svg.c
    modules/freetype/src/raster/raster.c
    modules/freetype/src/winfonts/winfnt.c
    modules/freetype/src/smooth/smooth.c
    modules/freetype/src/pcf/pcf.c
    modules/freetype/src/gzip/ftgzip.c
    modules/freetype/src/pfr/pfr.c
    # modules/freetype/src/base/ftver.rc.res
    modules/freetype/src/pshinter/pshinter.c
    modules/freetype/src/type42/type42.c
    modules/freetype/src/sdf/sdf.c
    modules/freetype/src/cff/cff.c
    modules/freetype/src/psnames/psnames.c
    modules/freetype/src/autofit/autofit.c
    modules/freetype/src/type1/type1.c
    modules/freetype/src/base/ftbase.c
    modules/freetype/src/psaux/psaux.c
    modules/freetype/src/truetype/truetype.c
    modules/freetype/src/sfnt/sfnt.c
    )

set(FREETYPE_DEFINES
    FT_CONFIG_CONFIG_H=<freetype/config/ftconfig.h>
    FT_CONFIG_OPTIONS_H=<freetype/config/ftoption.h>
    FT_CONFIG_STANDARD_LIBRARY_H=<freetype/config/ftstdlib.h>
    FT_CONFIG_MODULES_H=<freetype/config/ftmodule.h>
    FT_BEGIN_HEADER=;
    FT_END_HEADER=;
    FT2_BUILD_LIBRARY
)

set(FREETYPE_INCLUDE modules/freetype/include)
if (WIN32)
    list(APPEND FREETYPE_SRC
        modules/freetype/builds/windows/ftdebug.c
        modules/freetype/builds/windows/ftsystem.c
        )
    list(APPEND FREETYPE_DEFINES
        _CRT_SECURE_NO_WARNINGS
        )
elseif(APPLE)
    list(APPEND FREETYPE_SRC
        modules/freetype/builds/mac/ftmac.c
        modules/freetype/src/base/ftdebug.c
        modules/freetype/builds/unix/ftsystem.c
        )
    list(APPEND FREETYPE_DEFINES
        HAVE_FCNTL_H
        HAVE_UNISTD_H
        )
    list(APPEND FREETYPE_INCLUDE modules/freetype/src/base)
endif()

add_library(freetype2 STATIC ${FREETYPE_SRC})

target_include_directories(freetype2 PRIVATE ${FREETYPE_INCLUDE})
target_compile_definitions(freetype2 PRIVATE ${FREETYPE_DEFINES})

list(APPEND PLUGIN_LIBRARIES freetype2)

# ███████╗████████╗ █████╗ ███╗   ██╗██████╗  █████╗ ██╗      ██████╗ ███╗   ██╗███████╗
# ██╔════╝╚══██╔══╝██╔══██╗████╗  ██║██╔══██╗██╔══██╗██║     ██╔═══██╗████╗  ██║██╔════╝
# ███████╗   ██║   ███████║██╔██╗ ██║██║  ██║███████║██║     ██║   ██║██╔██╗ ██║█████╗  
# ╚════██║   ██║   ██╔══██║██║╚██╗██║██║  ██║██╔══██║██║     ██║   ██║██║╚██╗██║██╔══╝  
# ███████║   ██║   ██║  ██║██║ ╚████║██████╔╝██║  ██║███████╗╚██████╔╝██║ ╚████║███████╗
# ╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═══╝╚═════╝ ╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚═╝  ╚═══╝╚══════╝

if (WIN32)
    add_executable(${PROJECT_NAME}_standalone WIN32 ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_win.c)
    target_link_libraries(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_LIBRARIES})
elseif(APPLE)
    add_executable(${PROJECT_NAME}_standalone MACOSX_BUNDLE ${PLUGIN_SOURCES} modules/CPLUG/src/cplug_standalone_osx.m)
    target_link_libraries(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_LIBRARIES} "-framework CoreMIDI -framework CoreAudio -framework CoreServices")

    cplug_configure_info_plist(${PROJECT_NAME}_standalone ${APP_BUNDLE_ID} "APPL" "app")

    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.app/Contents/PkgInfo" "APPL????")
endif()
target_include_directories(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_INCLUDE})
target_compile_definitions(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_STANDALONE)
target_compile_options(${PROJECT_NAME}_standalone PRIVATE ${PLUGIN_OPTIONS})

# ██╗  ██╗ ██████╗ ████████╗██████╗ ███████╗██╗      ██████╗  █████╗ ██████╗
# ██║  ██║██╔═══██╗╚══██╔══╝██╔══██╗██╔════╝██║     ██╔═══██╗██╔══██╗██╔══██╗
# ███████║██║   ██║   ██║   ██████╔╝█████╗  ██║     ██║   ██║███████║██║  ██║
# ██╔══██║██║   ██║   ██║   ██╔══██╗██╔══╝  ██║     ██║   ██║██╔══██║██║  ██║
# ██║  ██║╚██████╔╝   ██║   ██║  ██║███████╗███████╗╚██████╔╝██║  ██║██████╔╝
# ╚═╝  ╚═╝ ╚═════╝    ╚═╝   ╚═╝  ╚═╝╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═════╝

set(HOTRELOAD_LIB_NAME ${PROJECT_NAME}_hotreload_lib)
if (WIN32 AND CMAKE_BUILD_TYPE MATCHES Debug)
    add_library(${HOTRELOAD_LIB_NAME} MODULE ${PLUGIN_SOURCES})
    target_compile_definitions(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED CPLUG_BUILD_STANDALONE)
    target_include_directories(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_INCLUDE})
    target_compile_options(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_OPTIONS})
    target_link_libraries(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_LIBRARIES})

    # Windows paths are complicated
    string(REPLACE "/" "\\\\" SOURCE_DIR_WIN ${PROJECT_SOURCE_DIR})
    string(REPLACE "/" "\\\\" BINARY_DIR_WIN ${CMAKE_BINARY_DIR})

    add_executable(${PROJECT_NAME}_hotreload WIN32 modules/CPLUG/src/cplug_standalone_win.c)
    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE
        HOTRELOAD_WATCH_DIR="${SOURCE_DIR_WIN}\\\\src"
        HOTRELOAD_LIB_PATH="${BINARY_DIR_WIN}\\\\${CMAKE_BUILD_TYPE}\\\\${HOTRELOAD_LIB_NAME}.dll"
        HOTRELOAD_BUILD_COMMAND="cmake --build ${BINARY_DIR_WIN} --config Debug --target ${HOTRELOAD_LIB_NAME}"
        CPLUG_SHARED
        )
    target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED)
    target_include_directories(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_INCLUDE})
    target_compile_options(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_OPTIONS})
    # Forces plugin to be built before the host
    add_dependencies(${PROJECT_NAME}_hotreload ${HOTRELOAD_LIB_NAME})
endif()