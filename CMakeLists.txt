cmake_minimum_required(VERSION 3.16)
project(MilkDAWp VERSION 0.3.0 LANGUAGES C CXX)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
endif()

# Ensure vcpkg stages runtime DLLs next to our binary (applocal.ps1)
set(VCPKG_APPLOCAL_DEPS ON CACHE BOOL "Copy vcpkg DLLs beside built targets" FORCE)

if (MSVC)
  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit build required. Configure with -A x64.")
  endif()
  # Normalize MSVC Debug info flags to avoid D9025 (/Zi vs /Z7) warning
  foreach(lang C CXX)
    string(REPLACE "/Zi" "/Z7" CMAKE_${lang}_FLAGS_DEBUG "${CMAKE_${lang}_FLAGS_DEBUG}")
  endforeach()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX "")

add_compile_definitions(JUCE_USE_CURL=0)

include(FetchContent)

FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.9
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(MilkDAWp
        COMPANY_NAME "Otitis Media"
        BUNDLE_ID com.otitismedia.milkdawp
        IS_SYNTH FALSE
        IS_MIDI_EFFECT FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES "Fx" "Analyzer"
        FORMATS VST3
        PRODUCT_NAME "MilkDAWp"
        PLUGIN_MANUFACTURER_CODE OMed
        PLUGIN_CODE MkDw
)

juce_generate_juce_header(MilkDAWp)

target_sources(MilkDAWp PRIVATE
    # Processor
    src/processor/PluginProcessor.cpp
    src/processor/PluginProcessor.h

    # State
    src/state/Parameters.cpp
    src/state/Parameters.h

    # UI
    src/ui/PluginEditor.cpp
    src/ui/PluginEditor.h
    src/ui/VisualizationWindow.cpp
    src/ui/VisualizationWindow.h

    # Renderers
    src/renderers/ProjectMRenderer.cpp
    src/renderers/ProjectMRenderer.h

    # Utils
    src/utils/Logging.cpp
    src/utils/Logging.h
    src/utils/LockFreeAudioFifo.h
)

target_compile_definitions(MilkDAWp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MilkDAWp PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_opengl
    juce::juce_gui_extra
    juce::juce_core
)

# Prefer vcpkg's projectM v4 package
find_package(projectM4 QUIET COMPONENTS Playlist)

if (projectM4_FOUND)
    message(STATUS "Found projectM4 via find_package(projectM4)")
    target_link_libraries(MilkDAWp PRIVATE libprojectM::projectM)

    # Link optional playlist component if available (component may not be provided by all builds)
    if (TARGET libprojectM::playlist)
        target_link_libraries(MilkDAWp PRIVATE libprojectM::playlist)
        target_compile_definitions(MilkDAWp PRIVATE HAVE_PROJECTM_PLAYLIST=1)
        message(STATUS "Linking projectM playlist component")
    else()
        message(STATUS "projectM playlist component not available; building without playlist API")
    endif()

    # Linking the imported targets propagates include dirs and transitive dependencies,
    # so no need to add include directories or probe for headers manually anymore.
    target_compile_definitions(MilkDAWp PRIVATE HAVE_PROJECTM)
else()
    message(STATUS "projectM4 not found; building WITHOUT projectM (fallback renderer only)")
endif()

juce_add_binary_data(MilkDAWpData SOURCES resources/presets/README.md)
target_link_libraries(MilkDAWp PRIVATE MilkDAWpData)

target_compile_definitions(MilkDAWp PRIVATE GLEW_STATIC)

# Log JUCE jassert messages in Debug so we can see the exact condition text
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(MilkDAWp PRIVATE JUCE_LOG_ASSERTIONS=1)
endif()

# Compute vcpkg bin dir when available
if (WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  set(_VCPKG_BIN_DIR "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
else()
  set(_VCPKG_BIN_DIR "")
endif()

# Path to the UCRT redistributable DLLs (adjust if your SDK is elsewhere)
# Define this early so it can be used in PRE_LINK steps below.
set(_UCRT_REDIST_DIR "C:/Program Files (x86)/Windows Kits/10/Redist/ucrt/DLLs/x64")

# Dev-only: stage ALL DLLs from vcpkg bin into helper/plugin dirs (optional)
option(MILKDAWP_STAGE_ALL_VCPKG_DLLS "Copy all vcpkg DLLs into helper/plugin dirs (dev convenience)" OFF)

# Stage runtimes next to juce_vst3_helper.exe so its LoadLibrary sees them
if (WIN32 AND TARGET juce_vst3_helper)
  set(_SYS32 "$ENV{SystemRoot}/System32")

  # Only stage Microsoft runtimes if you explicitly want app-local MSVC (normally OFF for static runtime)
  option(MILKDAWP_APPLOCAL_MSVC "Copy MSVC/UCRT runtime DLLs beside helper (dev convenience)" OFF)
  if (MILKDAWP_APPLOCAL_MSVC)
    add_custom_command(TARGET juce_vst3_helper POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E echo "Staging runtimes next to juce_vst3_helper.exe"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:juce_vst3_helper>"
      # MSVC runtime
      COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_SYS32}/MSVCP140.dll"       "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/MSVCP140.dll"       -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
      COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_SYS32}/VCRUNTIME140.dll"   "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/VCRUNTIME140.dll"   -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
      COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_SYS32}/VCRUNTIME140_1.dll" "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/VCRUNTIME140_1.dll" -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
      # UCRT redistributable (contains ucrtbase.dll and api-ms-win-crt-*.dll)
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_UCRT_REDIST_DIR}" "$<TARGET_FILE_DIR:juce_vst3_helper>"
      VERBATIM)
  endif()

  # Only stage vcpkg DLLs next to helper when NOT using a static triplet
  if (NOT VCPKG_TARGET_TRIPLET MATCHES "-static$" AND NOT _VCPKG_BIN_DIR STREQUAL "")
    add_custom_command(TARGET juce_vst3_helper POST_BUILD
      COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_VCPKG_BIN_DIR}/glew32.dll"     "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/glew32.dll"     -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
      COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_VCPKG_BIN_DIR}/projectM-4.dll" "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/projectM-4.dll" -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
      VERBATIM)
    if (TARGET libprojectM::playlist)
      add_custom_command(TARGET juce_vst3_helper POST_BUILD
        COMMAND "${CMAKE_COMMAND}" "-Dsrc=${_VCPKG_BIN_DIR}/projectM-4-playlist.dll" "-Ddst=$<TARGET_FILE_DIR:juce_vst3_helper>/projectM-4-playlist.dll" -P "${CMAKE_SOURCE_DIR}/cmake/CopyFileIfExists.cmake"
        VERBATIM)
    endif()

    if (MILKDAWP_STAGE_ALL_VCPKG_DLLS)
      add_custom_command(TARGET juce_vst3_helper POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_VCPKG_BIN_DIR}" "$<TARGET_FILE_DIR:juce_vst3_helper>"
        VERBATIM)
    endif()
  endif()
endif()

if (WIN32 AND TARGET MilkDAWp_VST3)
  # Resolve paths via generator expressions
  set(_PLUG_BIN  "$<TARGET_FILE_DIR:MilkDAWp_VST3>")
  set(_BUNDLE    "$<TARGET_FILE_DIR:MilkDAWp_VST3>/..")
  set(_RES_DIR   "$<TARGET_FILE_DIR:MilkDAWp_VST3>/../Resources")

  # Pick the helper exe (prefer target-specific helper name on JUCE 8)
  set(_HELPER_PATH "$<IF:$<TARGET_EXISTS:MilkDAWp_vst3_helper>,$<TARGET_FILE:MilkDAWp_vst3_helper>,$<IF:$<TARGET_EXISTS:juce_vst3_helper>,$<TARGET_FILE:juce_vst3_helper>,juce_vst3_helper.exe>>")

  file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/post-build-MilkDAWp_VST3.bat"
    CONTENT
"@echo off
setlocal ENABLEEXTENSIONS
REM Ensure Resources folder exists
if not exist \"$<SHELL_PATH:${_RES_DIR}>\" mkdir \"$<SHELL_PATH:${_RES_DIR}>\"
REM Inject plugin bin and UCRT into PATH for LoadLibrary resolution
set \"PATH=$<SHELL_PATH:${_UCRT_REDIST_DIR}>;$<SHELL_PATH:${_PLUG_BIN}>;%PATH%\"
REM Run the helper (if present)
if exist \"$<SHELL_PATH:${_HELPER_PATH}>\" (
  \"$<SHELL_PATH:${_HELPER_PATH}>\" -create -version \"${PROJECT_VERSION}\" -path \"$<SHELL_PATH:${_BUNDLE}>\" -output \"$<SHELL_PATH:${_RES_DIR}>/moduleinfo.json\"
) else (
  echo Helper not found: \"$<SHELL_PATH:${_HELPER_PATH}>\"
)
exit /b 0
")

  add_custom_command(TARGET MilkDAWp_VST3 PRE_LINK
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/CMakeFiles/MilkDAWp_VST3.dir"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_BINARY_DIR}/post-build-MilkDAWp_VST3.bat"
            "${CMAKE_BINARY_DIR}/CMakeFiles/MilkDAWp_VST3.dir/post-build.bat"
    VERBATIM)
endif()

# Deploy target: copy the built bundle to the per-user VST3 folder (no admin required).
if (WIN32 AND TARGET MilkDAWp_VST3)
  set(_SRC_BUNDLE "$<TARGET_FILE_DIR:MilkDAWp_VST3>/..")
  set(_USER_VST3_DIR "$ENV{LOCALAPPDATA}/Programs/Common/VST3")

  add_custom_target(deploy_vst3_user
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_USER_VST3_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E rm -rf       "${_USER_VST3_DIR}/MilkDAWp.vst3"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${_SRC_BUNDLE}" "${_USER_VST3_DIR}/MilkDAWp.vst3"
    COMMENT "Deploying MilkDAWp.vst3 to per-user VST3: ${_USER_VST3_DIR}"
    VERBATIM)
endif()
