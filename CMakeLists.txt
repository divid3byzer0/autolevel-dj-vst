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

juce_add_plugin(AutoLevelDJ
    COMPANY_NAME "DistrictNoir"
    PLUGIN_MANUFACTURER_CODE DstN
    PLUGIN_CODE AlDj
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "AutoLevel DJ"
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

# Offline DSP unit test executable
add_executable(dsp_test tests/dsp_test.cpp)
target_include_directories(dsp_test PRIVATE src)
