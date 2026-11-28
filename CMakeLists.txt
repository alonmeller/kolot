cmake_minimum_required(VERSION 3.22)
project(Kolot VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
endif()

option(KOLOT_BUILD_CLI "Build the command-line renderer (for testing)" OFF)

# JUCE: use a local checkout if given (-DJUCE_DIR=...), otherwise download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                              GIT_TAG 8.0.9 GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(KOLOT_FORMATS AU VST3 Standalone)
else()
    set(KOLOT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Kolot
    COMPANY_NAME "Alon Meller"
    BUNDLE_ID "com.alonmeller.kolot"
    PLUGIN_MANUFACTURER_CODE Alnm
    PLUGIN_CODE Klot
    PRODUCT_NAME "Kolot"
    DESCRIPTION "Sings the track it is on with a whole choir"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Pitch Shift
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${KOLOT_FORMATS})

set(KOLOT_DSP_SOURCES
    Source/RealtimeChoir.cpp
    Source/LiveFinisher.cpp)

juce_add_binary_data(KolotAssets SOURCES Resources/background.jpg)

target_sources(Kolot PRIVATE
    ${KOLOT_DSP_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Kolot PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Kolot
    PRIVATE KolotAssets juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

if(KOLOT_BUILD_CLI)
    if(NOT DEFINED KOLOT_CLI_MAIN)
        set(KOLOT_CLI_MAIN tools/KolotRenderCLI.cpp)
    endif()
    juce_add_console_app(KolotCLI PRODUCT_NAME "kolot-render")
    target_sources(KolotCLI PRIVATE ${KOLOT_CLI_MAIN} ${KOLOT_DSP_SOURCES})
    target_include_directories(KolotCLI PRIVATE Source)
    target_compile_definitions(KolotCLI PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(KolotCLI PRIVATE juce::juce_audio_formats juce::juce_audio_processors
                                   PUBLIC juce::juce_recommended_config_flags)
endif()
