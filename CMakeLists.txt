cmake_minimum_required(VERSION 3.22)

project(KickDrum VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(KICKDRUM_BUILD_STANDALONE "Also build a standalone app for testing"     OFF)
option(KICKDRUM_BUILD_TESTS      "Build the unit test console app"            ON)
option(KICKDRUM_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 9.0.4)")

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Sources shared by the plugin and the unit tests
set(KICKDRUM_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/Crossover.cpp
    Source/DSP/PitchDetect.cpp
    Source/DSP/KickBank.cpp
    Source/DSP/KickEngine.cpp
    Source/DSP/FxChain.cpp
    Source/DSP/OutputRenderer.cpp
    Source/Browser/SampleLibrary.cpp
    Source/Browser/FolderScanner.cpp
    Source/UI/BrowserPanel.cpp
    Source/UI/Controls.cpp
    Source/UI/KickDisplays.cpp
    Source/UI/DragAudioHandle.cpp
    Source/UI/Shared/CustomLookAndFeel.cpp
    Source/UI/Shared/VirusComponents.cpp)

set(KICKDRUM_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

set(KICKDRUM_MODULES
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp)

# ------------------------------------------------------------------------------
# Plugin (VST3 instrument; Windows is the target for now)
set(KICKDRUM_FORMATS VST3)
if(KICKDRUM_BUILD_STANDALONE)
    list(APPEND KICKDRUM_FORMATS Standalone)
endif()

# The VST3 manifest helper has to run on the build machine, which isn't possible
# when cross-compiling.
if(CMAKE_CROSSCOMPILING)
    set(KICKDRUM_VST3_MANIFEST FALSE)
else()
    set(KICKDRUM_VST3_MANIFEST TRUE)
endif()

# Same manufacturer as Virus Delay and ArpSampler, so hosts list them under one vendor.
juce_add_plugin(KickDrum
    PRODUCT_NAME                "KickDrum"
    COMPANY_NAME                "VirusDelay Project"
    BUNDLE_ID                   com.virusdelayproject.kickdrum
    PLUGIN_MANUFACTURER_CODE    Vdpj
    PLUGIN_CODE                 Kick
    FORMATS                     ${KICKDRUM_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Drum
    VST3_AUTO_MANIFEST          ${KICKDRUM_VST3_MANIFEST}
    COPY_PLUGIN_AFTER_BUILD     ${KICKDRUM_COPY_AFTER_BUILD})

target_sources(KickDrum PRIVATE ${KICKDRUM_SOURCES})
target_include_directories(KickDrum PRIVATE Source)
target_compile_definitions(KickDrum PUBLIC ${KICKDRUM_DEFINITIONS})
target_link_libraries(KickDrum
    PRIVATE
        ${KICKDRUM_MODULES}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Windows release builds also write debug symbols to a separate .pdb (never shipped
# inside the plugin), so crash dumps from hosts can be turned into readable stacks.
if(MSVC)
    foreach(target KickDrum KickDrum_VST3 KickDrum_Standalone)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE "$<$<CONFIG:Release>:/Zi>")
            target_link_options(${target} PRIVATE "$<$<CONFIG:Release>:/DEBUG;/OPT:REF;/OPT:ICF>")
        endif()
    endforeach()
endif()

# ------------------------------------------------------------------------------
# Unit tests: a console app that compiles the same sources plus Tests/*.cpp
if(KICKDRUM_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(KickDrumTests PRODUCT_NAME "KickDrumTests")

    file(GLOB KICKDRUM_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)
    target_sources(KickDrumTests PRIVATE ${KICKDRUM_SOURCES} ${KICKDRUM_TEST_SOURCES})
    target_include_directories(KickDrumTests PRIVATE Source)
    target_compile_definitions(KickDrumTests PRIVATE ${KICKDRUM_DEFINITIONS} KICKDRUM_TESTS=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KickDrumTests
        PRIVATE
            ${KICKDRUM_MODULES}
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME KickDrumTests COMMAND KickDrumTests)
endif()
