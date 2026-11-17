cmake_minimum_required(VERSION 3.22)
project(AutoLevelDJ VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(juce)

# The audio engine, shared with autolevel-box (git submodule).
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/autolevel-core/CMakeLists.txt")
    message(FATAL_ERROR "external/autolevel-core is missing. Run: git submodule update --init")
endif()
add_subdirectory(external/autolevel-core)

if (APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
elseif (WIN32)
    set(PLUGIN_FORMATS VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone)
endif()

# Legacy VST2 (.dll on Windows, e.g. for 32-bit hosts). Steinberg no longer distributes the
# VST2 SDK, so it can't be fetched here: point VST2_SDK_PATH at your own copy (the folder that
# contains pluginterfaces/vst2.x/aeffect.h), e.g. -DVST2_SDK_PATH=C:/SDKs/vstsdk2.4
set(VST2_SDK_PATH "$ENV{VST2_SDK_PATH}" CACHE PATH "Path to the VST2 SDK (enables the VST2 format)")
if (VST2_SDK_PATH)
    if (NOT EXISTS "${VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h")
        message(FATAL_ERROR "VST2_SDK_PATH=${VST2_SDK_PATH} does not contain pluginterfaces/vst2.x/aeffect.h")
    endif()
    juce_set_vst2_sdk_path("${VST2_SDK_PATH}")
    list(APPEND PLUGIN_FORMATS VST)
    message(STATUS "VST2 SDK found at ${VST2_SDK_PATH}: building the VST2 format")
else()
    message(STATUS "VST2_SDK_PATH not set: skipping the VST2 format")
endif()

juce_add_plugin(AutoLevelDJ
    COMPANY_NAME "DistrictNoir"
    PLUGIN_MANUFACTURER_CODE DstN
    PLUGIN_CODE AlDj
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "AutoLevel DJ"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "AutoLevel DJ requires microphone/audio input access to process your live audio signal."
)

target_sources(AutoLevelDJ PRIVATE
    src/PluginProcessor.h
    src/PluginProcessor.cpp
    src/PluginEditor.h
    src/PluginEditor.cpp
)

target_include_directories(AutoLevelDJ PRIVATE src)

target_link_libraries(AutoLevelDJ PRIVATE
    autolevel::core
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

target_compile_definitions(AutoLevelDJ PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# The engine's own tests (autolevel-core), built here too so CI runs them on every platform.
add_executable(dsp_test external/autolevel-core/tests/dsp_test.cpp)
target_link_libraries(dsp_test PRIVATE autolevel::core)
target_compile_options(dsp_test PRIVATE -UNDEBUG)

# Plugin tests: parameter mapping, box-block parity, presets, state. Links the plugin's
# shared code, so it tests the real processor.
add_executable(plugin_test tests/plugin_test.cpp)
target_link_libraries(plugin_test PRIVATE
    AutoLevelDJ
    autolevel::core
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_recommended_config_flags)
target_include_directories(plugin_test PRIVATE src)
target_compile_definitions(plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_compile_options(plugin_test PRIVATE -UNDEBUG)

# Convenience target to install VST3 and AU into user's audio plugin folder
if (APPLE)
    add_custom_target(install_plugins
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/Library/Audio/Plug-Ins/VST3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/Library/Audio/Plug-Ins/Components"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/AutoLevelDJ_artefacts/$<CONFIG>/VST3/AutoLevel DJ.vst3"
            "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/AutoLevel DJ.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/AutoLevelDJ_artefacts/$<CONFIG>/AU/AutoLevel DJ.component"
            "$ENV{HOME}/Library/Audio/Plug-Ins/Components/AutoLevel DJ.component"
        COMMENT "Installing VST3 and AU plugins to ~/Library/Audio/Plug-Ins..."
        VERBATIM
    )
elseif (WIN32)
    add_custom_target(install_plugins
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{COMMONPROGRAMFILES}/VST3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/AutoLevelDJ_artefacts/$<CONFIG>/VST3/AutoLevel DJ.vst3"
            "$ENV{COMMONPROGRAMFILES}/VST3/AutoLevel DJ.vst3"
        COMMENT "Installing VST3 plugin to %COMMONPROGRAMFILES%/VST3..."
        VERBATIM
    )
endif()
