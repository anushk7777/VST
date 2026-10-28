cmake_minimum_required(VERSION 3.22)

project(Linen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (MSVC)
    # Static runtime so the .vst3 runs on machines without the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---- Dependencies -----------------------------------------------------------
# For offline/local builds point these at existing checkouts, e.g.
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
#   -DFETCHCONTENT_SOURCE_DIR_MELATONIN_BLUR=/path/to/melatonin_blur
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# melatonin_blur is a JUCE module: its folder name must match the module name,
# and its own CMakeLists is only for its tests, so point SOURCE_SUBDIR elsewhere.
FetchContent_Declare(melatonin_blur
    GIT_REPOSITORY https://github.com/sudara/melatonin_blur.git
    GIT_TAG        v1.4
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_CURRENT_BINARY_DIR}/melatonin_blur
    SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(melatonin_blur)
juce_add_module("${melatonin_blur_SOURCE_DIR}")

# ---- Shared sources ---------------------------------------------------------
# DSP code has no dependency on the plugin wrapper, so the tests and the render
# tool compile exactly the same engine the plugin runs.
set(LINEN_DSP_SOURCES
    src/dsp/Air.cpp
    src/dsp/Eq1073.cpp
    src/dsp/LinenEngine.cpp
    src/dsp/Preamp.cpp
    src/dsp/PitchTracker.cpp
    src/dsp/ResonanceSuppressor.cpp
    src/dsp/SilkCompressor.cpp
    src/dsp/SilkMacro.cpp)

juce_add_binary_data(LinenAssets
    NAMESPACE LinenBinary
    SOURCES
        assets/fonts/CormorantGaramond-Light.ttf
        assets/fonts/CormorantGaramond-Medium.ttf
        assets/fonts/CormorantGaramond-Italic.ttf
        assets/fonts/Inter-Regular.ttf
        assets/fonts/Inter-Medium.ttf)

set(LINEN_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# ---- Plugin -----------------------------------------------------------------
juce_add_plugin(Linen
    COMPANY_NAME             "Linen Audio"
    COMPANY_WEBSITE          "https://github.com/anushk7777/VST"
    BUNDLE_ID                com.linenaudio.linen
    PLUGIN_MANUFACTURER_CODE Lnna
    PLUGIN_CODE              Lnn1
    PRODUCT_NAME             "Linen"
    DESCRIPTION              "Silky vocal processor"
    FORMATS                  VST3 Standalone
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Fx Dynamics EQ
    COPY_PLUGIN_AFTER_BUILD  FALSE)

set(LINEN_PLUGIN_SOURCES
    ${LINEN_DSP_SOURCES}
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/LinenLookAndFeel.cpp
    src/ui/LinenWeave.cpp
    src/ui/Meters.cpp
    src/ui/SpectrumDisplay.cpp)

target_sources(Linen PRIVATE ${LINEN_PLUGIN_SOURCES})

target_include_directories(Linen PRIVATE src)
target_compile_definitions(Linen PUBLIC ${LINEN_COMMON_DEFINITIONS})

target_link_libraries(Linen
    PRIVATE
        LinenAssets
        melatonin_blur
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_animation
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- Unit tests (headless) --------------------------------------------------
juce_add_console_app(LinenTests PRODUCT_NAME "LinenTests")
target_sources(LinenTests PRIVATE
    ${LINEN_DSP_SOURCES}
    tests/TestMain.cpp
    tests/EngineTests.cpp
    tests/DeharshTests.cpp
    tests/PreampEqTests.cpp
    tests/CompAirTests.cpp
    tests/SilkTests.cpp)
target_include_directories(LinenTests PRIVATE src tests)
target_compile_definitions(LinenTests PRIVATE ${LINEN_COMMON_DEFINITIONS})
target_link_libraries(LinenTests
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---- Offline render tool ----------------------------------------------------
juce_add_console_app(LinenRender PRODUCT_NAME "LinenRender")
target_sources(LinenRender PRIVATE
    ${LINEN_DSP_SOURCES}
    tools/render.cpp)
target_include_directories(LinenRender PRIVATE src)
target_compile_definitions(LinenRender PRIVATE ${LINEN_COMMON_DEFINITIONS})
target_link_libraries(LinenRender
    PRIVATE
        juce::juce_dsp
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---- GUI snapshot tool (dev only: renders the window to PNG with audio playing)
option(LINEN_BUILD_SNAPSHOT "Build the LinenSnapshot GUI screenshot tool" OFF)
if (LINEN_BUILD_SNAPSHOT)
    juce_add_gui_app(LinenSnapshot PRODUCT_NAME "LinenSnapshot")
    target_sources(LinenSnapshot PRIVATE ${LINEN_PLUGIN_SOURCES} tools/snapshot.cpp)
    target_include_directories(LinenSnapshot PRIVATE src)
    target_compile_definitions(LinenSnapshot PRIVATE ${LINEN_COMMON_DEFINITIONS}
        JucePlugin_Name="Linen" JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(LinenSnapshot
        PRIVATE
            LinenAssets
            melatonin_blur
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_animation
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()

enable_testing()
add_test(NAME LinenTests COMMAND LinenTests)
