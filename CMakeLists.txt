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
    src/text_rendering_layer.c
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
    # modules/freetype/src/base/ftver.rc.res
    src/libs/freetype_scu.c
    )

set(FREETYPE_INCLUDE
    modules/freetype/include
    modules/freetype/src/base
    src/libs
    )

add_library(freetype STATIC ${FREETYPE_SRC})

target_include_directories(freetype PRIVATE ${FREETYPE_INCLUDE})


# set(FT_DISABLE_ZLIB TRUE)
# set(FT_DISABLE_BZIP2 TRUE)
# set(FT_DISABLE_PNG TRUE)
# set(FT_DISABLE_HARFBUZZ TRUE)
# set(FT_DISABLE_BROTLI TRUE)
# add_subdirectory(modules/freetype)

# link_directories(libs)

list(APPEND PLUGIN_LIBRARIES freetype)

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

if (CMAKE_BUILD_TYPE MATCHES Debug)
set(HOTRELOAD_LIB_NAME ${PROJECT_NAME}_hotreload_lib)

add_library(${HOTRELOAD_LIB_NAME} MODULE ${PLUGIN_SOURCES})
target_compile_definitions(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED CPLUG_BUILD_STANDALONE)
target_include_directories(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_INCLUDE})
target_compile_options(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_OPTIONS})

set(HOTRELOAD_WATCH_DIR "")
set(HOTRELOAD_LIB_PATH "")
set(HOTRELOAD_HOST_LIBS "")

if (WIN32)
    add_executable(${PROJECT_NAME}_hotreload WIN32 modules/CPLUG/src/cplug_standalone_win.c)
    set(HOTRELOAD_WATCH_DIR "${SRC_DIR}\\\\src")
    set(HOTRELOAD_LIB_PATH "${BIN_DIR}\\\\${CMAKE_BUILD_TYPE}\\\\${HOTRELOAD_LIB_NAME}.dll")
endif()# WIN32
if (APPLE)
    add_executable(${PROJECT_NAME}_hotreload MACOSX_BUNDLE modules/CPLUG/src/cplug_standalone_osx.m)
    file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_hotreload.app/Contents/PkgInfo")
    file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_hotreload.app/Contents/PkgInfo" "APPL????")

    set(HOTRELOAD_WATCH_DIR "${PROJECT_SOURCE_DIR}/src")
    set(HOTRELOAD_LIB_PATH "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/lib${HOTRELOAD_LIB_NAME}.so")
    set(HOTRELOAD_HOST_LIBS "-framework Cocoa -framework CoreMIDI -framework CoreAudio -framework CoreServices")
endif() # APPLE

target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE
    HOTRELOAD_WATCH_DIR="${HOTRELOAD_WATCH_DIR}"
    HOTRELOAD_LIB_PATH="${HOTRELOAD_LIB_PATH}"
    HOTRELOAD_BUILD_COMMAND="cmake --build ${BIN_DIR} --config Debug --target ${HOTRELOAD_LIB_NAME}"
    CPLUG_SHARED
    CPLUG_BUILD_STANDALONE
    )
target_compile_definitions(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_SHARED)
target_include_directories(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_INCLUDE})
target_compile_options(${PROJECT_NAME}_hotreload PRIVATE ${PLUGIN_OPTIONS})
target_link_libraries(${PROJECT_NAME}_hotreload PRIVATE ${HOTRELOAD_HOST_LIBS})
# Forces plugin to be built before the host
add_dependencies(${PROJECT_NAME}_hotreload ${HOTRELOAD_LIB_NAME})

target_precompile_headers(${HOTRELOAD_LIB_NAME} PRIVATE src/common.h)
target_link_libraries(${HOTRELOAD_LIB_NAME} PRIVATE ${PLUGIN_LIBRARIES})

endif() # CMAKE_BUILD_TYPE MATCHES Debug
