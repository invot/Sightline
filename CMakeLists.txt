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
    Source/core/HandLandmarker.cpp
    Source/core/OrtModel.cpp
    Source/core/GestureEngine.cpp
    Source/core/InternalSynth.cpp)

# ---------------------------------------------------------------------------
# Hand landmark model: ONNX Runtime (MIT, Microsoft's NuGet package) and the
# MediaPipe palm detector + hand landmark models (Apache-2.0, OpenCV model
# zoo). Downloaded once into SL_DEPS_DIR, pinned by SHA-256. Without them the
# build still works and the app uses the basic skin/motion tracker.
# ---------------------------------------------------------------------------
set(SL_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools" CACHE PATH "Where downloaded dependencies are kept")
option(SL_HAND_MODEL "Track hands with the MediaPipe landmark model (ONNX Runtime)" ON)
option(SL_REQUIRE_HAND_MODEL "Fail configure when the hand landmark model cannot be prepared (release builds)" OFF)

set(SL_ORT_VERSION 1.31.1)
set(SL_ORT_URL "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${SL_ORT_VERSION}/microsoft.ml.onnxruntime.${SL_ORT_VERSION}.nupkg")
set(SL_ORT_SHA256 6871ccbc97f3d5bdb4095a1dcec33aa78d56f6ed8de5980ef5fa1d9f2e7c679a)
set(SL_ORT_DIR "${SL_DEPS_DIR}/onnxruntime-${SL_ORT_VERSION}")
set(SL_ORT_DLL "${SL_ORT_DIR}/runtimes/win-x64/native/onnxruntime.dll")

set(SL_MODEL_DIR "${SL_DEPS_DIR}/mediapipe-hands-2023feb")
set(SL_HF "https://huggingface.co/opencv")
set(SL_MODEL_FILES
    "palm_detection.onnx|${SL_HF}/palm_detection_mediapipe/resolve/233e619dcea1759bf6de707b9b904fe30881ea55/palm_detection_mediapipe_2023feb.onnx|78ff51c38496b7fc8b8ebdb6cc8c1abb02fa6c38427c6848254cdaba57fcce7c"
    "hand_landmark.onnx|${SL_HF}/handpose_estimation_mediapipe/resolve/4b2a0b446e5cf2f11fb6b2c7251091c035d2c1f7/handpose_estimation_mediapipe_2023feb.onnx|db0898ae717b76b075d9bf563af315b29562e11f8df5027a1ef07b02bef6d81c"
    "LICENSE|${SL_HF}/handpose_estimation_mediapipe/resolve/4b2a0b446e5cf2f11fb6b2c7251091c035d2c1f7/LICENSE|58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd")

# CMake's own file(DOWNLOAD) cannot verify TLS against every host: on the
# GitHub Windows runners every https fetch fails with curl error 60 ("SSL peer
# certificate or SSH remote key was not OK"), and EXPECTED_HASH made that a
# fatal configure error even though the hand model is optional. Download
# through curl instead - it ships with Windows 10+ and with Git for Windows -
# fall back to file(DOWNLOAD) when curl is missing or fails, and verify the
# pinned hash by hand so a blocked network only costs the model, not the
# configure.
find_program(SL_CURL_EXECUTABLE curl)

# sl_download_file(<url> <dest> <sha256> <out-ok>): leaves <dest> in place and
# sets <out-ok> to ON only when its contents match the pinned hash.
function(sl_download_file url dest expectedHash outOk)
    set(ok OFF)

    if (EXISTS "${dest}")
        file(SHA256 "${dest}" existingHash)
        if (existingHash STREQUAL expectedHash)
            set(ok ON)
        else()
            message(WARNING "${dest} does not match its pinned SHA-256; fetching it again")
            file(REMOVE "${dest}")
        endif()
    endif()

    if (NOT ok)
        get_filename_component(dir "${dest}" DIRECTORY)
        file(MAKE_DIRECTORY "${dir}")
        set(part "${dest}.part")
        file(REMOVE "${part}")
        message(STATUS "Downloading ${url}")

        set(downloaded OFF)

        if (SL_CURL_EXECUTABLE)
            execute_process(
                COMMAND "${SL_CURL_EXECUTABLE}" -fsSL --retry 4 --retry-delay 3 --retry-all-errors
                        -o "${part}" "${url}"
                RESULT_VARIABLE curlCode)
            if (curlCode STREQUAL "0")
                set(downloaded ON)
            else()
                message(WARNING "curl could not download ${url} (exit ${curlCode})")
            endif()
        endif()

        if (NOT downloaded)
            file(DOWNLOAD "${url}" "${part}" STATUS dlStatus)
            list(GET dlStatus 0 dlCode)
            if (dlCode EQUAL 0)
                set(downloaded ON)
            else()
                message(WARNING "Download of ${url} failed (${dlStatus})")
            endif()
        endif()

        if (downloaded)
            file(SHA256 "${part}" actualHash)
            if (actualHash STREQUAL expectedHash)
                file(RENAME "${part}" "${dest}")
                set(ok ON)
            else()
                message(WARNING "${url} does not match its pinned SHA-256 (got ${actualHash})")
            endif()
        endif()

        if (NOT ok)
            file(REMOVE "${part}")
        endif()
    endif()

    set(${outOk} ${ok} PARENT_SCOPE)
endfunction()

set(SL_HAVE_ORT OFF)

if (SL_HAND_MODEL AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ortOk OFF)

    if (EXISTS "${SL_ORT_DLL}" AND EXISTS "${SL_ORT_DIR}/build/native/include/onnxruntime_c_api.h")
        set(ortOk ON)
    else()
        set(nupkg "${SL_DEPS_DIR}/dl/onnxruntime-${SL_ORT_VERSION}.nupkg")
        message(STATUS "Preparing ONNX Runtime ${SL_ORT_VERSION} (about 160 MB, once)")
        sl_download_file("${SL_ORT_URL}" "${nupkg}" "${SL_ORT_SHA256}" nupkgOk)

        if (nupkgOk)
            file(ARCHIVE_EXTRACT INPUT "${nupkg}" DESTINATION "${SL_ORT_DIR}"
                 PATTERNS "build/native/include/*" "runtimes/win-x64/native/onnxruntime.dll"
                          "LICENSE" "ThirdPartyNotices.txt")
            # Only the extracted pieces are kept; the archive itself is 160 MB.
            file(REMOVE "${nupkg}")
            set(ortOk ON)
        else()
            message(WARNING "ONNX Runtime ${SL_ORT_VERSION} could not be prepared; hand tracking will be limited")
        endif()
    endif()

    set(modelsOk ON)

    foreach (entry IN LISTS SL_MODEL_FILES)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 url)
        list(GET parts 2 hash)

        sl_download_file("${url}" "${SL_MODEL_DIR}/${name}" "${hash}" modelOk)
        if (NOT modelOk)
            message(WARNING "${name} could not be prepared; hand tracking will be limited")
            set(modelsOk OFF)
        endif()
    endforeach()

    if (ortOk AND modelsOk)
        set(SL_HAVE_ORT ON)
    endif()
endif()

if (SL_HAVE_ORT)
    message(STATUS "Hand landmark model: on (ONNX Runtime ${SL_ORT_VERSION})")
elseif (SL_REQUIRE_HAND_MODEL)
    message(FATAL_ERROR "Hand landmark model: off, but SL_REQUIRE_HAND_MODEL is set. "
                        "Fix the download problems listed above, or build without it using -DSL_HAND_MODEL=OFF.")
else()
    message(WARNING "Hand landmark model: off, the basic tracker will be used")
endif()

# Puts onnxruntime.dll and the licences of what it ships next to a target's binary.
function(sl_copy_runtime target)
    if (SL_HAVE_ORT AND TARGET ${target})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SL_ORT_DLL}" "$<TARGET_FILE_DIR:${target}>/onnxruntime.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SL_ORT_DIR}/LICENSE" "$<TARGET_FILE_DIR:${target}>/onnxruntime-LICENSE.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SL_ORT_DIR}/ThirdPartyNotices.txt" "$<TARGET_FILE_DIR:${target}>/onnxruntime-ThirdPartyNotices.txt"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SL_MODEL_DIR}/LICENSE" "$<TARGET_FILE_DIR:${target}>/mediapipe-models-LICENSE.txt"
            VERBATIM)
    endif()
endfunction()

function(sl_use_ort target)
    if (SL_HAVE_ORT)
        target_compile_definitions(${target} PRIVATE SL_HAVE_ORT=1)
        target_include_directories(${target} SYSTEM PRIVATE "${SL_ORT_DIR}/build/native/include")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# JUCE-free tests for the mapping / gating / detection core.
# ---------------------------------------------------------------------------
add_executable(SightLineTests Tests/main.cpp ${SL_CORE_SOURCES})
target_include_directories(SightLineTests PRIVATE Source)
set_target_properties(SightLineTests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
sl_use_ort(SightLineTests)
sl_copy_runtime(SightLineTests)
if (SL_HAVE_ORT)
    target_compile_definitions(SightLineTests PRIVATE SL_TEST_MODEL_DIR="${SL_MODEL_DIR}")
endif()
if (NOT MSVC)
    target_compile_options(SightLineTests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# The plugin: a MIDI generator driven by a webcam.
# ---------------------------------------------------------------------------
if (EXISTS "${SL_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${SL_JUCE_DIR}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)

    if(APPLE)
        set(IOLINE_ICON_ARGS
            ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon-1024.png"
            ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon-256.png")
    endif()
    juce_add_plugin(SightLine
        ${IOLINE_ICON_ARGS}
        COMPANY_NAME              "Codebuff"
        BUNDLE_ID                 "com.codebuff.sightline"
        PLUGIN_MANUFACTURER_CODE  Cdbx
        PLUGIN_CODE               Sght
        PRODUCT_NAME              "sightline"
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
    sl_use_ort(SightLine)

    # The two models are compiled into the binary (about 8 MB), so they can
    # never go missing or mismatch; only onnxruntime.dll sits beside it.
    if (SL_HAVE_ORT)
        juce_add_binary_data(SightLineModels
            HEADER_NAME SightLineModels.h
            NAMESPACE SightLineModels
            SOURCES "${SL_MODEL_DIR}/palm_detection.onnx" "${SL_MODEL_DIR}/hand_landmark.onnx")
        target_link_libraries(SightLine PRIVATE SightLineModels)
    endif()

    sl_copy_runtime(SightLine_VST3)
    sl_copy_runtime(SightLine_Standalone)

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
    # which only MSVC understands, so a MinGW build has to name them. JUCE
    # links the macOS frameworks itself, so the list stays empty there.
    set(SL_WIN_LIBS)
    if (WIN32)
        set(SL_WIN_LIBS
            wininet winmm ws2_32 ole32 oleaut32 uuid version shlwapi user32 gdi32
            shell32 comdlg32 imm32 rstrtmgr dwmapi d2d1 d3d11 dxgi dcomp
            windowscodecs setupapi crypt32 secur32 winhttp uxtheme oleacc propsys
            hid shcore dbghelp comctl32 evr mfuuid avrt dsound strmiids quartz ksuser)
    endif()

    target_link_libraries(SightLine PRIVATE
        juce::juce_audio_utils
        juce::juce_video
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
        ${SL_WIN_LIBS})

    if (MINGW)
        # A plugin gets loaded by a host that knows nothing about our toolchain,
        # so the runtimes have to be inside the binary. Apple's linker has no
        # -static, so this is MinGW-only.
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
    sl_use_ort(SightLineSmoke)
    if (SL_HAVE_ORT)
        target_link_libraries(SightLineSmoke PRIVATE SightLineModels)
    endif()
    sl_copy_runtime(SightLineSmoke)
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
    if (MINGW)
        target_link_options(SightLineSmoke PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    set_target_properties(SightLineSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
else()
    message(WARNING "JUCE not found at ${SL_JUCE_DIR}; only the test target will be built.")
endif()
