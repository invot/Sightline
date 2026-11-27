cmake_minimum_required(VERSION 3.22)

project(SightLine VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    # JUCE's VST3 client relies on tentative definitions for the VST3 interface
    # IIDs. GCC 10+ defaults to -fno-common, which turns those into "multiple
    # definition" link errors, so keep common symbols on MinGW.
    add_compile_options (-fcommon)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(SL_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/JUCE-8.0.6" CACHE PATH "JUCE source tree")

set(SL_CORE_SOURCES
    Source/core/Scales.cpp
    Source/core/ZoneMap.cpp
    Source/core/HandTracker.cpp
    Source/core/GestureEngine.cpp
    Source/core/InternalSynth.cpp)

# ---------------------------------------------------------------------------
# JUCE-free tests for the mapping / gating / detection core.
# ---------------------------------------------------------------------------
add_executable(SightLineTests Tests/main.cpp ${SL_CORE_SOURCES})
target_include_directories(SightLineTests PRIVATE Source)
set_target_properties(SightLineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if (NOT MSVC)
    target_compile_options(SightLineTests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# The plugin: a MIDI generator driven by a webcam.
# ---------------------------------------------------------------------------
if (EXISTS "${SL_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SL_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

    juce_add_plugin(SightLine
        COMPANY_NAME              "Codebuff"
        BUNDLE_ID                 "com.codebuff.sightline"
        PLUGIN_MANUFACTURER_CODE  Cdbx
        PLUGIN_CODE               Sght
        PRODUCT_NAME              "SightLine"
        DESCRIPTION               "Play notes and drums by moving your hands in front of a webcam"
        IS_SYNTH                  FALSE
        NEEDS_MIDI_INPUT          TRUE
        NEEDS_MIDI_OUTPUT         TRUE
        IS_MIDI_EFFECT            FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD   FALSE
        FORMATS                   VST3 Standalone
        VST3_CATEGORIES           "Fx"
        AAX_CATEGORY              AAX_ePlugInCategory_None)

    target_sources(SightLine PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StandaloneBridge.cpp
        Source/ui/StagePanel.cpp
        Source/ui/KeyPanel.cpp
        Source/ui/SetupPanel.cpp
        ${SL_CORE_SOURCES})

    target_include_directories(SightLine PRIVATE Source)

    # MinGW has no #pragma linker manifest, so embed the manifest (common
    # controls v6 + per-monitor DPI) as a resource on every Windows target.
    # The VST3 wrapper already carries JUCE's own manifest; adding a second one
    # makes ld emit "multiple non-default manifests" and a broken DLL, so the
    # resource goes on the standalone executable only.
    if (WIN32)
        target_sources(SightLine_Standalone PRIVATE Source/platform/win_manifest.rc)
        set_source_files_properties (Source/platform/win_manifest.rc PROPERTIES
            COMPILE_FLAGS "--include-dir=${CMAKE_CURRENT_SOURCE_DIR}/Source/platform")

        # A loaded DLL only honours a manifest stored in resource id 2.
        target_sources(SightLine_VST3 PRIVATE Source/platform/dll_manifest.rc)
        set_source_files_properties (Source/platform/dll_manifest.rc PROPERTIES
            COMPILE_FLAGS "--include-dir=${CMAKE_CURRENT_SOURCE_DIR}/Source/platform")
    endif()

    target_compile_definitions(SightLine PUBLIC
        JUCE_USE_CAMERA=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)

    # JUCE's Windows system libraries come from #pragma comment(lib, ...),
    # which only MSVC understands, so a MinGW build has to name them.
    set(SL_WIN_LIBS
        wininet winmm ws2_32 ole32 oleaut32 uuid version shlwapi user32 gdi32
        shell32 comdlg32 imm32 rstrtmgr dwmapi d2d1 d3d11 dxgi dcomp
        windowscodecs setupapi crypt32 secur32 winhttp uxtheme oleacc propsys
        hid shcore dbghelp comctl32 evr mfuuid avrt dsound strmiids quartz ksuser)

    target_link_libraries(SightLine PRIVATE
        juce::juce_audio_utils
        juce::juce_video
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        ${SL_WIN_LIBS})

    if (NOT MSVC)
        # A plugin gets loaded by a host that knows nothing about our toolchain,
        # so the runtimes have to be inside the binary.
        foreach (slTarget SightLine SightLine_VST3 SightLine_Standalone)
            if (TARGET ${slTarget})
                target_link_options(${slTarget} PRIVATE -static -static-libgcc -static-libstdc++)
            endif()
        endforeach()
    endif()

    # Console harness: builds the processor + editor without a host, drives the
    # tracker with synthetic frames, and renders the pages to PNG.
    add_executable(SightLineSmoke
        Tests/smoke.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/StandaloneBridge.cpp
        Source/ui/StagePanel.cpp
        Source/ui/KeyPanel.cpp
        Source/ui/SetupPanel.cpp
        ${SL_CORE_SOURCES})
    target_include_directories(SightLineSmoke PRIVATE Source)
    if (WIN32)
        target_sources(SightLineSmoke PRIVATE Source/platform/win_manifest.rc)
        set_source_files_properties (Source/platform/win_manifest.rc PROPERTIES
            COMPILE_FLAGS "--include-dir=${CMAKE_CURRENT_SOURCE_DIR}/Source/platform")
    endif()
    target_compile_definitions(SightLineSmoke PUBLIC
        JUCE_USE_CAMERA=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(SightLineSmoke PRIVATE
        juce::juce_audio_utils
        juce::juce_video
        juce::juce_recommended_config_flags
        ${SL_WIN_LIBS})
    if (NOT MSVC)
        target_link_options(SightLineSmoke PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(SightLineSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
else()
    message(WARNING "JUCE not found at ${SL_JUCE_DIR}; only the test target will be built.")
endif()
