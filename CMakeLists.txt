cmake_minimum_required(VERSION 3.22)
project(AutoLevelDJ VERSION 1.0.0 LANGUAGES C CXX)

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

if (APPLE)
    set(PLUGIN_FORMATS VST3 AU Standalone)
elseif (WIN32)
    set(PLUGIN_FORMATS VST3 Standalone)
else()
    set(PLUGIN_FORMATS VST3 Standalone LV2)
endif()

juce_add_plugin(AutoLevelDJ
    COMPANY_NAME "DistrictNoir"
    PLUGIN_MANUFACTURER_CODE DstN
    PLUGIN_CODE AlDj
    FORMATS ${PLUGIN_FORMATS}
    PRODUCT_NAME "AutoLevel DJ"
    LV2URI "https://districtnoir.com/plugins/autolevel-dj"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(AutoLevelDJ PRIVATE
    src/PluginProcessor.h
    src/PluginProcessor.cpp
    src/PluginEditor.h
    src/PluginEditor.cpp
    src/dsp/KWeightingFilter.h
    src/dsp/LoudnessMeter.h
    src/dsp/Leveler.h
    src/dsp/Bands.h
    src/dsp/Biquad.h
    src/dsp/MultibandCompressor.h
    src/dsp/SafetyLimiter.h
    src/dsp/AutoLevelEngine.h
)

target_include_directories(AutoLevelDJ PRIVATE src)

target_link_libraries(AutoLevelDJ PRIVATE
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

# Offline DSP unit test executable
add_executable(dsp_test tests/dsp_test.cpp)
target_include_directories(dsp_test PRIVATE src)

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
elseif (UNIX AND NOT APPLE)
    add_custom_target(install_plugins
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.vst3"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.lv2"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/AutoLevelDJ_artefacts/$<CONFIG>/VST3/AutoLevel DJ.vst3"
            "$ENV{HOME}/.vst3/AutoLevel DJ.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/AutoLevelDJ_artefacts/$<CONFIG>/LV2/AutoLevel DJ.lv2"
            "$ENV{HOME}/.lv2/AutoLevel DJ.lv2"
        COMMENT "Installing VST3 and LV2 plugins to ~/.vst3 and ~/.lv2..."
        VERBATIM
    )
endif()
