cmake_minimum_required(VERSION 3.22)

# Must come before project() to take effect.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS version to support")

project(Patternspace VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PATTERNSPACE_BUILD_PLUGIN "Build the plug-ins and the standalone app" ON)
option(PATTERNSPACE_BUILD_CLAP "Build the CLAP plug-in (needs PATTERNSPACE_BUILD_PLUGIN)" ON)
option(PATTERNSPACE_BUILD_TESTS "Build the tests" ON)
option(PATTERNSPACE_COPY_PLUGINS "Copy plug-ins into the system plug-in folders after building" OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---- dependencies --------------------------------------------------------------------------
# Fetched at configure time. To build offline or against a local checkout, pass
# -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE (and likewise _CLAP-JUCE-EXTENSIONS).
include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

if(PATTERNSPACE_BUILD_PLUGIN AND PATTERNSPACE_BUILD_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 55525c9858d4b25687be7759a5e0f70eccef218e
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap-juce-extensions)
endif()

# ---- the patch engine (plain C++, no JUCE) -------------------------------------------------
add_library(patternspace_core STATIC
    source/core/Engine.cpp
    source/core/Engine.h
    source/core/Modules.cpp
    source/core/Modules.h
    source/core/Patch.cpp
    source/core/Patch.h
    source/core/Patterns.cpp
    source/core/Patterns.h
    source/core/Sampler.cpp
    source/core/Sampler.h)
target_include_directories(patternspace_core PUBLIC source)
set_target_properties(patternspace_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(patternspace_core PRIVATE /W4)
else()
    target_compile_options(patternspace_core PRIVATE -Wall -Wextra)
endif()

# ---- factory presets, embedded in the plug-in ------------------------------------------------
file(GLOB PATTERNSPACE_PRESETS CONFIGURE_DEPENDS presets/factory/*.json)
file(GLOB PATTERNSPACE_SUBSYSTEMS CONFIGURE_DEPENDS presets/subsystems/*.json)

# ---- the plug-ins and standalone app -------------------------------------------------------
if(PATTERNSPACE_BUILD_PLUGIN)
    if(APPLE)
        set(PATTERNSPACE_FORMATS VST3 AU Standalone)
    elseif(WIN32)
        set(PATTERNSPACE_FORMATS VST3 Standalone)
    else()
        set(PATTERNSPACE_FORMATS VST3 LV2 Standalone)
    endif()

    juce_add_plugin(Patternspace
        PRODUCT_NAME "Patternspace"
        COMPANY_NAME "Patternspace"
        BUNDLE_ID "com.patternspace.patternspace"
        PLUGIN_MANUFACTURER_CODE Pspc
        PLUGIN_CODE Psq1
        FORMATS ${PATTERNSPACE_FORMATS}
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Instrument Drum Sampler
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        LV2URI "https://github.com/thomas-nelson23/patternspace"
        NEEDS_CURL TRUE
        COPY_PLUGIN_AFTER_BUILD ${PATTERNSPACE_COPY_PLUGINS})

    # Logic only takes MIDI from AU MIDI-effect units, so macOS also builds "Patternspace MIDI":
    # the same plug-in as a MIDI processor with no audio outputs.
    if(APPLE)
        juce_add_plugin(PatternspaceMidiFX
            PRODUCT_NAME "Patternspace MIDI"
            COMPANY_NAME "Patternspace"
            BUNDLE_ID "com.patternspace.patternspace-midi"
            PLUGIN_MANUFACTURER_CODE Pspc
            PLUGIN_CODE Psq2
            FORMATS AU
            IS_SYNTH FALSE
            NEEDS_MIDI_INPUT TRUE
            NEEDS_MIDI_OUTPUT TRUE
            IS_MIDI_EFFECT TRUE
            EDITOR_WANTS_KEYBOARD_FOCUS TRUE
            AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
            NEEDS_CURL TRUE
            COPY_PLUGIN_AFTER_BUILD ${PATTERNSPACE_COPY_PLUGINS})
        set(PATTERNSPACE_PLUGIN_TARGETS Patternspace PatternspaceMidiFX)
    else()
        set(PATTERNSPACE_PLUGIN_TARGETS Patternspace)
    endif()

    juce_add_binary_data(PatternspacePresets
        HEADER_NAME PatternspacePresets.h
        NAMESPACE PatternspacePresets
        SOURCES ${PATTERNSPACE_PRESETS} ${PATTERNSPACE_SUBSYSTEMS})

    # Sora, JetBrains Mono and Michroma (SIL Open Font License, resources/fonts/licenses), the
    # Home Dashboard's typefaces. Sora and JetBrains Mono are static cuts of the variable fonts.
    juce_add_binary_data(PatternspaceFonts
        HEADER_NAME PatternspaceFonts.h
        NAMESPACE PatternspaceFonts
        SOURCES
            resources/fonts/Sora-Regular.ttf
            resources/fonts/Sora-SemiBold.ttf
            resources/fonts/Sora-Bold.ttf
            resources/fonts/JetBrainsMono-Regular.ttf
            resources/fonts/JetBrainsMono-Medium.ttf
            resources/fonts/Michroma-Regular.ttf)

    # The standalone app's update check reads GitHub releases over HTTPS, which needs curl on Linux.
    set(PATTERNSPACE_UPDATE_REPO "thomas-nelson23/Patternspace" CACHE STRING "GitHub owner/repo the standalone app checks for releases")

    set(PATTERNSPACE_PLUGIN_SOURCES
            source/plugin/PatchCanvas.cpp
            source/plugin/PatchCanvas.h
            source/plugin/PatchCanvasDetail.h
            source/plugin/PatchCanvasItems.cpp
            source/plugin/PatchCanvasMenus.cpp
            source/plugin/PatchCanvasPaint.cpp
            source/plugin/PluginEditor.cpp
            source/plugin/PluginEditor.h
            source/plugin/PluginProcessor.cpp
            source/plugin/PluginProcessor.h
            source/plugin/Presets.cpp
            source/plugin/Presets.h
            source/plugin/Theme.cpp
            source/plugin/Theme.h
            source/plugin/Updater.cpp
            source/plugin/Updater.h
            source/plugin/UserLibrary.cpp
            source/plugin/UserLibrary.h
            source/state/PatchJson.cpp
            source/state/PatchJson.h
            source/update/UpdateCheck.cpp
            source/update/UpdateCheck.h)

    foreach(target IN LISTS PATTERNSPACE_PLUGIN_TARGETS)
        target_sources(${target} PRIVATE ${PATTERNSPACE_PLUGIN_SOURCES})

        target_compile_definitions(${target} PUBLIC
            PATTERNSPACE_UPDATE_REPO="${PATTERNSPACE_UPDATE_REPO}"
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=1
            JUCE_VST3_CAN_REPLACE_VST2=0
            JUCE_DISPLAY_SPLASH_SCREEN=0)

        target_link_libraries(${target}
            PRIVATE
                patternspace_core
                PatternspacePresets
                PatternspaceFonts
                juce::juce_audio_utils
                juce::juce_gui_basics
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_lto_flags
                juce::juce_recommended_warning_flags)
    endforeach()
    target_compile_definitions(Patternspace PUBLIC PATTERNSPACE_MIDI_EFFECT=0)
    if(APPLE)
        target_compile_definitions(PatternspaceMidiFX PUBLIC PATTERNSPACE_MIDI_EFFECT=1)
    endif()

    if(PATTERNSPACE_BUILD_CLAP)
        clap_juce_extensions_plugin(TARGET Patternspace
            CLAP_ID "com.patternspace.patternspace"
            CLAP_FEATURES instrument drum-machine sampler note-effect stereo)
    endif()
endif()

# ---- tests ---------------------------------------------------------------------------------
if(PATTERNSPACE_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(PatternspaceTests PRODUCT_NAME "PatternspaceTests")
    target_sources(PatternspaceTests PRIVATE
        tests/CoreTests.cpp
        source/state/PatchJson.cpp
        source/update/UpdateCheck.cpp)
    target_compile_definitions(PatternspaceTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        PATTERNSPACE_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(PatternspaceTests
        PRIVATE
            patternspace_core
            juce::juce_core
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
    add_test(NAME PatternspaceTests COMMAND PatternspaceTests)

    # The window, headless: every factory patch is laid out, painted and edited through the
    # editor, so a change to the canvas that crashes or mislays a module fails here. Needs a
    # display (xvfb-run on Linux); built with the plug-ins, since it compiles their sources.
    if(PATTERNSPACE_BUILD_PLUGIN)
        juce_add_console_app(PatternspaceUiTests PRODUCT_NAME "PatternspaceUiTests")
        target_sources(PatternspaceUiTests PRIVATE tests/UiTests.cpp ${PATTERNSPACE_PLUGIN_SOURCES})
        target_compile_definitions(PatternspaceUiTests PRIVATE
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=0
            PATTERNSPACE_MIDI_EFFECT=0
            PATTERNSPACE_UPDATE_REPO="${PATTERNSPACE_UPDATE_REPO}"
            PATTERNSPACE_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
            JucePlugin_Name="Patternspace"
            JucePlugin_VersionString="${PROJECT_VERSION}")
        target_link_libraries(PatternspaceUiTests
            PRIVATE
                patternspace_core
                PatternspacePresets
                PatternspaceFonts
                juce::juce_audio_utils
                juce::juce_gui_basics
            PUBLIC
                juce::juce_recommended_config_flags
                juce::juce_recommended_warning_flags)
        add_test(NAME PatternspaceUiTests COMMAND PatternspaceUiTests)
    endif()
endif()
