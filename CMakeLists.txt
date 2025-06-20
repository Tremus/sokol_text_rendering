cmake_minimum_required(VERSION 3.16)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

project(text
    VERSION 0.0.1
    DESCRIPTION ""
    LANGUAGES C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(COMPANY_NAME "exacoustics")

set(PRODUCT_NAME "${PROJECT_NAME}")
set(PRODUCT_COPYRIGHT ${COMPANY_NAME})
set(PLUGIN_BUNDLE_ID "com.${COMPANY_NAME}.${PROJECT_NAME}.plugin")
set(APP_BUNDLE_ID "com.${COMPANY_NAME}.${PROJECT_NAME}.app")
# Full list of AU types & tags here:
# https://developer.apple.com/library/archive/documentation/General/Conceptual/ExtensibilityPG/AudioUnit.html
# The properties:
set(MACOSX_BUNDLE_TAGS "<string>Filter</string>") # Predefined tags are: Bass, Delay, Distortion, Drums, Dynamics, Dynamics Processor, Effects, Equalizer, Filter, Format Converter, Guitar, Imaging, MIDI, Mixer, Offline Effect, Output, Panner, Pitch, Reverb, Sampler, Synthesizer, Time Effect, Vocal. But you can use others.
set(MACOSX_BUNDLE_TYPE "aufx") # "aufx" for Effect, "augn" for Generator, "aumu" for Instrument, "aufm" for Music Effect.
set(MACOSX_BUNDLE_SUBTYPE "Txtt") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
set(MACOSX_BUNDLE_MANUFACTURER "ExAc") # For AU and GarageBand 10.3 compatibility, the first letter must be upper-case, the others lower-case.
# Convert version string (1.0.0) > integer (65536)
math(EXPR MACOSX_BUNDLE_VERSION_INT "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT DECIMAL) # cool trick bro


set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})

if (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_C_STANDARD 11)
else()
    set(CMAKE_C_STANDARD 99)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_STANDARD 20)
else()
    set(CMAKE_CXX_STANDARD 11)
endif()

set(PLUGIN_OPTIONS "")

# https://clang.llvm.org/docs/ClangCommandLineReference.html
if (CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    list(APPEND PLUGIN_OPTIONS
        -Werror=absolute-value
        -Werror=return-type
        -Werror=shadow
        -Werror=incompatible-pointer-types
        -Werror=parentheses
        -Werror=excess-initializers
        # -Wunused-function
        # -Wunused-variable
        
        -Wno-deprecated-declarations
        -Wno-deprecated
        -Wno-multichar
        -Wno-nullability-completeness
        -Wno-writable-strings
        -Wno-c2x-extensions
        -Wno-c++14-extensions
        -Wno-c++17-extensions
        -Wno-c++20-extensions
        -Wno-microsoft-enum-forward-reference
        )

    if (WIN32)
        list(APPEND PLUGIN_OPTIONS "-I${PROJECT_SOURCE_DIR}/src/common.h")
    else()
        list(APPEND PLUGIN_OPTIONS "-include${PROJECT_SOURCE_DIR}/src/common.h")
    endif()

endif()
# list(APPEND CMAKE_C_FLAGS -fsanitize=address)
# list(APPEND CMAKE_CXX_FLAGS -fsanitize=address)

if (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    list(APPEND PLUGIN_OPTIONS /GR- /EHasc- "/FI${PROJECT_SOURCE_DIR}/src/common.h")
    list(APPEND PLUGIN_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/Zi>")
    add_link_options("$<$<CONFIG:RELEASE>:/DEBUG>")
    if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        list(APPEND PLUGIN_OPTIONS /Od)
    endif()
else()
    list(APPEND PLUGIN_OPTIONS -fno-rtti -fno-exceptions)
    if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
        list(APPEND PLUGIN_OPTIONS -fno-inline-functions)
    endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_C_VISIBILITY_PRESET hidden)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_OBJC_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
    # Currently getting release mode bugs on MacOS & Windows...
    # list(APPEND PLUGIN_OPTIONS -flto)
endif()

set(PLUGIN_DEFINITIONS
    CPLUG_COMPANY_NAME="${COMPANY_NAME}"
    CPLUG_COMPANY_EMAIL="contact@exacoustics.com"
    CPLUG_PLUGIN_NAME="${PROJECT_NAME}"
    CPLUG_PLUGIN_URI="${PROJECT_HOMEPAGE_URL}"
    CPLUG_PLUGIN_VERSION="${PROJECT_VERSION}"
    _HAS_EXCEPTIONS=0
    STBI_NO_STDIO
    STBI_NO_LINEAR
    NVG_NO_STB
    )

set(PLUGIN_SOURCES
    src/plugin.c
    src/gui.c
    )

set(PLUGIN_INCLUDE
    modules/xhl/include
    modules/CPLUG/src/
    modules/sokol_gfx_multiinstance/
    modules/harfbuzz/src
    modules/freetype/include

    src/libs
    src/shaders/
    )

set(PLUGIN_LIBRARIES "")
set(SHADER_LANG "")

if (WIN32)
    list(APPEND PLUGIN_DEFINITIONS UNICODE _UNICODE PW_DX11)
    set(SHADER_LANG "hlsl5")

    if (CMAKE_BUILD_TYPE MATCHES Release)
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            list(APPEND PLUGIN_OPTIONS /arch:AVX)
        else()
            list(APPEND PLUGIN_OPTIONS -march=corei7-avx)
        endif()
    else()
        if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
            list(APPEND PLUGIN_OPTIONS /arch:AVX2)
        else()
            list(APPEND PLUGIN_OPTIONS -march=haswell)
        endif()
    endif()
    list(APPEND PLUGIN_SOURCES src/platform_win.c)
    list(APPEND PLUGIN_LIBRARIES dxguid)
elseif(APPLE)
    enable_language(OBJC)
    set(SHADER_LANG "metal_macos")
    if (CMAKE_BUILD_TYPE MATCHES Release)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
        list(APPEND PLUGIN_OPTIONS -g)
    endif()
    
    list(APPEND PLUGIN_SOURCES src/platform_osx.m)
    list(APPEND PLUGIN_LIBRARIES
        "-framework Quartz"
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit")
    list(APPEND PLUGIN_DEFINITIONS PW_METAL)
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
endif()

add_library(freetype2 STATIC ${FREETYPE_SRC})

target_include_directories(freetype2 PRIVATE
    modules/freetype/include
    )
target_compile_definitions(freetype2 PRIVATE ${FREETYPE_DEFINES})

# ██╗  ██╗ █████╗ ██████╗ ███████╗██████╗ ██╗   ██╗███████╗███████╗
# ██║  ██║██╔══██╗██╔══██╗██╔════╝██╔══██╗██║   ██║╚══███╔╝╚══███╔╝
# ███████║███████║██████╔╝█████╗  ██████╔╝██║   ██║  ███╔╝   ███╔╝ 
# ██╔══██║██╔══██║██╔══██╗██╔══╝  ██╔══██╗██║   ██║ ███╔╝   ███╔╝  
# ██║  ██║██║  ██║██║  ██║██║     ██████╔╝╚██████╔╝███████╗███████╗
# ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═════╝  ╚═════╝ ╚══════╝╚══════╝

add_library(harfbuzz STATIC modules/harfbuzz/src/harfbuzz.cc) # unity build
target_compile_definitions(harfbuzz PRIVATE HAVE_FREETYPE HB_NO_PRAGMA_GCC_DIAGNOSTIC_WARNING _CRT_SECURE_NO_WARNINGS)
target_compile_options(harfbuzz PRIVATE -Wno-deprecated-declarations)
target_link_libraries(harfbuzz PRIVATE freetype2)
target_include_directories(harfbuzz PRIVATE modules/freetype/include)

list(APPEND PLUGIN_LIBRARIES harfbuzz)


# ██████╗ ██╗     ██╗   ██╗ ██████╗ ██╗███╗   ██╗
# ██╔══██╗██║     ██║   ██║██╔════╝ ██║████╗  ██║
# ██████╔╝██║     ██║   ██║██║  ███╗██║██╔██╗ ██║
# ██╔═══╝ ██║     ██║   ██║██║   ██║██║██║╚██╗██║
# ██║     ███████╗╚██████╔╝╚██████╔╝██║██║ ╚████║
# ╚═╝     ╚══════╝ ╚═════╝  ╚═════╝ ╚═╝╚═╝  ╚═══╝

# .plists are generated using a template
# The params @bundle_identifier@ and @bundle_type@ are used by the .plist
function(configure_info_plist target_name bundle_identifier bundle_type bundle_extension)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/CPLUG/example/Info.plist.in
        ${CMAKE_BINARY_DIR}/${bundle_type}.plist
        @ONLY
    )

    set_target_properties(${target_name} PROPERTIES
        BUNDLE True
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME ${PROJECT_NAME}
        BUNDLE_EXTENSION ${bundle_extension}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/${bundle_type}.plist
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.${bundle_extension}
    )
endfunction()

# All Plugin formats in one
# if (APPLE)
#     add_library(${PROJECT_NAME}_plugin MODULE
#         ${PLUGIN_SOURCES}
#         modules/CPLUG/src/cplug_clap.c
#         modules/CPLUG/src/cplug_vst3.c
#         modules/CPLUG/src/cplug_auv2.c)
#     target_link_libraries(${PROJECT_NAME}_plugin PRIVATE "-framework AudioToolbox -framework Cocoa") # -framework AudioToolbox not actually required...

#     configure_info_plist(${PROJECT_NAME}_plugin ${PLUGIN_BUNDLE_ID} "BNDL" "clap")

#     file(TOUCH_NOCREATE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo")
#     file(WRITE "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap/Contents/PkgInfo" "BNDL????")

#     target_compile_definitions(${PROJECT_NAME}_plugin PRIVATE
#         CPLUG_AUV2_VERSION_INT=${MACOSX_BUNDLE_VERSION_INT}
#         CPLUG_AUV2_BUNDLE_ID="${PLUGIN_BUNDLE_ID}"
#     )

#     add_custom_command(TARGET ${PROJECT_NAME}_plugin POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3"
#         COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component"

#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3"
#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component"

#         COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap to ~/Library/Audio/Plug-Ins/CLAP/"
#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.clap" "~/Library/Audio/Plug-Ins/CLAP/${PROJECT_NAME}.clap"

#         COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3 to ~/Library/Audio/Plug-Ins/VST3/"
#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3" "~/Library/Audio/Plug-Ins/VST3/${PROJECT_NAME}.vst3"

#         COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component to ~/Library/Audio/Plug-Ins/Components/"
#         COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.component" "~/Library/Audio/Plug-Ins/Components/${PROJECT_NAME}.component"
#     )
# elseif(WIN32)
#     add_library(${PROJECT_NAME}_plugin MODULE
#         ${PLUGIN_SOURCES}
#         modules/CPLUG/src/cplug_clap.c
#         modules/CPLUG/src/cplug_vst3.c)
#     set_target_properties(${PROJECT_NAME}_plugin PROPERTIES
#         OUTPUT_NAME ${PROJECT_NAME}_plugin
#         PDB_NAME ${PROJECT_NAME}_plugin
#         )

#     set(VST3_BUILD_PATH ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}.vst3)
#     add_custom_command(TARGET ${PROJECT_NAME}_plugin POST_BUILD
#         # https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/Locations+Format/Plugin+Format.html
#         COMMAND ${CMAKE_COMMAND} -E echo "Building VST3"
#         COMMAND ${CMAKE_COMMAND} -E make_directory ${VST3_BUILD_PATH}
#         COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}/Contents"
#         COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUILD_PATH}/Contents/x86_64-win"
#         COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll" "${VST3_BUILD_PATH}/Contents/x86_64-win/${PROJECT_NAME}.vst3"
#         COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/PlugIn.ico"                      "${VST3_BUILD_PATH}/PlugIn.ico"
#         COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/assets/desktop.ini.in"                  "${VST3_BUILD_PATH}/desktop.ini"
#         COMMAND attrib +s +h ${VST3_BUILD_PATH}/desktop.ini
#         COMMAND attrib +s +h ${VST3_BUILD_PATH}/PlugIn.ico
#         COMMAND attrib +s ${VST3_BUILD_PATH}

#         COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.vst3 to %LOCALAPPDATA%\\Programs\\Common\\VST3"
#         COMMAND ${CMAKE_COMMAND} -E copy_directory  "${VST3_BUILD_PATH}" "%LOCALAPPDATA%\\Programs\\Common\\VST3\\${PROJECT_NAME}.vst3"

#         COMMAND ${CMAKE_COMMAND} -E echo "Installing ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll to %LOCALAPPDATA%\\Programs\\Common\\CLAP"
#         COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${PROJECT_NAME}_plugin.dll" "%LOCALAPPDATA%\\Programs\\Common\\CLAP\\${PROJECT_NAME}.clap"
#         )
# endif()

# target_link_libraries(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_LIBRARIES})
# target_include_directories(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_INCLUDE})
# target_compile_definitions(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_DEFINITIONS} CPLUG_BUILD_VST3 CPLUG_BUILD_CLAP CPLUG_BUILD_AUV2)
# target_compile_options(${PROJECT_NAME}_plugin PRIVATE ${PLUGIN_OPTIONS})

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

    configure_info_plist(${PROJECT_NAME}_standalone ${APP_BUNDLE_ID} "APPL" "app")

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