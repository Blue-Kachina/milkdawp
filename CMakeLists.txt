cmake_minimum_required(VERSION 3.16)
project(MilkDAWp VERSION 0.3.0 LANGUAGES C CXX)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 NEW)
endif()

if (MSVC)
  if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "64-bit build required. Configure with -A x64.")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_DEBUG_POSTFIX "")

add_compile_definitions(JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_CURL=0)

include(FetchContent)

FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.8
)
FetchContent_MakeAvailable(juce)

option(USE_PREBUILT_PROJECTM "Use prebuilt dynamic projectM instead of building it" ON)
set(PROJECTM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/projectm" CACHE PATH "Root of prebuilt projectM (headers+libs)")

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
    JUCE_DISPLAY_SPLASH_SCREEN=0
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
find_package(projectM4 QUIET)

if (projectM4_FOUND)
    message(STATUS "Found projectM4 via find_package(projectM4)")
    target_link_libraries(MilkDAWp PRIVATE libprojectM::projectM)
    target_compile_definitions(MilkDAWp PRIVATE HAVE_PROJECTM)
else()
    message(STATUS "projectM4 not found; building WITHOUT projectM (fallback renderer only)")
endif()

juce_add_binary_data(MilkDAWpData SOURCES resources/presets/README.md)
target_link_libraries(MilkDAWp PRIVATE MilkDAWpData)

if (WIN32 AND TARGET MilkDAWp_VST3)
  set(VST3_MODULE_DIR "$<TARGET_FILE_DIR:MilkDAWp_VST3>")
  set(VST3_BUNDLE_DIR "${VST3_MODULE_DIR}/../..")
  add_custom_command(TARGET MilkDAWp_VST3 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE_DIR}/Contents/Resources/presets"
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CMAKE_CURRENT_SOURCE_DIR}/resources/presets"
              "${VST3_BUNDLE_DIR}/Contents/Resources/presets"
      COMMENT "Copying presets into VST3 bundle resources"
      VERBATIM)
endif()

option(MILKDAWP_DEV_DEPLOY "Copy VST3 bundle to %COMMONPROGRAMFILES%/VST3 after build" OFF)
if (WIN32 AND TARGET MilkDAWp_VST3)
  set(SYSTEM_VST3_DIR "$ENV{COMMONPROGRAMFILES}/VST3")
  add_custom_target(deploy_vst3
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SYSTEM_VST3_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${SYSTEM_VST3_DIR}/MilkDAWp.vst3"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VST3_BUNDLE_DIR}" "${SYSTEM_VST3_DIR}/MilkDAWp.vst3"
    COMMENT "Deploying MilkDAWp.vst3 to ${SYSTEM_VST3_DIR} (requires Administrator)"
    VERBATIM)
  if (MILKDAWP_DEV_DEPLOY)
    add_dependencies(MilkDAWp_VST3 deploy_vst3)
  endif()
endif()
