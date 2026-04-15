cmake_minimum_required(VERSION 3.22)
if(APPLE)
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET STREQUAL "")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS deployment target (Monterey)" FORCE)
    endif()
endif()

project(oXygen VERSION 1.0.4)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(OXYGEN_ENABLE_NATIVE_OPTIMIZATIONS "Enable CPU-specific optimizations for local builds" OFF)

set(OXYGEN_PLATFORM "Unknown")

if(APPLE)
    set(OXYGEN_PLATFORM "macOS")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(OXYGEN_PLATFORM "Windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(OXYGEN_PLATFORM "Linux")
endif()

if(NOT OXYGEN_PLATFORM STREQUAL "Unknown")
    message(STATUS "Detected System: ${OXYGEN_PLATFORM}")
    message(STATUS "Detected Compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

if(APPLE)
    add_compile_options(-O3 -funroll-loops -flto)
    if(OXYGEN_ENABLE_NATIVE_OPTIMIZATIONS)
        add_compile_options(-march=native)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(MSVC)
        add_compile_options(/MP /W4 /permissive- /O2 /Oi /Ot /GF /Gy)
        if(OXYGEN_ENABLE_NATIVE_OPTIMIZATIONS)
            add_compile_options(/arch:AVX2)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-O3)
        if(OXYGEN_ENABLE_NATIVE_OPTIMIZATIONS)
            add_compile_options(-march=native)
        endif()
    else()
        message(STATUS "Using default compiler flags for Windows toolchain '${CMAKE_CXX_COMPILER_ID}'")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_options(-O3)
    if(OXYGEN_ENABLE_NATIVE_OPTIMIZATIONS)
        add_compile_options(-march=native)
    endif()
else()
    message(WARNING "Detected unsupported system: ${CMAKE_SYSTEM_NAME}")
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        master
    )
    FetchContent_MakeAvailable(juce)
endif()

# Create the plugin target
juce_add_plugin(oXygen
    COMPANY_NAME "Wamphyre"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE "Wamp"
    PLUGIN_CODE "Oxyg"
    FORMATS VST3
    PRODUCT_NAME "oXygen"

)

juce_add_binary_data(oXygenAssets
    SOURCES
        assets/oxygen_icon.svg
        assets/reference_icon.svg
)

# Add source files
target_sources(oXygen
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/GUI/MainComponent.cpp
        Source/GUI/MainComponent.h
        Source/GUI/ModuleRack.cpp
        Source/GUI/ModuleRack.h
        Source/GUI/EqualizerEditor.cpp
        Source/GUI/EqualizerEditor.h
        Source/Modules/GainModule.h
        Source/Modules/EqualizerModule.cpp
        Source/Modules/EqualizerModule.h
        Source/Modules/DynamicEqModule.cpp
        Source/Modules/DynamicEqModule.h
        Source/Modules/MultibandCompressorModule.cpp
        Source/Modules/MultibandCompressorModule.h
        Source/GUI/DynamicEqEditor.cpp
        Source/GUI/DynamicEqEditor.h
        Source/GUI/MultibandCompressorEditor.cpp
        Source/GUI/MultibandCompressorEditor.h
        Source/Modules/MaximizerModule.cpp
        Source/Modules/MaximizerModule.h
        Source/GUI/MaximizerEditor.cpp
        Source/GUI/MaximizerEditor.h
        Source/Modules/StereoImagerModule.cpp
        Source/Modules/StereoImagerModule.h
        Source/GUI/StereoImagerEditor.cpp
        Source/GUI/StereoImagerEditor.h
        Source/GUI/SpectrumAnalyzer.cpp
        Source/GUI/SpectrumAnalyzer.h
        Source/GUI/AudioBufferQueue.h
        Source/GUI/ModuleWrapper.cpp
        Source/GUI/ModuleWrapper.h
        Source/MasteringModule.h
        # AI
        Source/AI/InferenceEngine.h
        Source/AI/InferenceEngine.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-x11-3.0)
    pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)
endif()

# Link against JUCE modules
target_link_libraries(oXygen
    PRIVATE
        oXygenAssets
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(oXygen PRIVATE PkgConfig::GTK3 PkgConfig::WEBKIT2GTK)
endif()

# Fix for VST2/VST3 compatibility error on new plugins
target_compile_definitions(oXygen
    PUBLIC
        JUCE_VST3_CAN_REPLACE_VST2=0
)

# Generate JuceHeader.h for classic JUCE project compatibility
juce_generate_juce_header(oXygen)

# Custom command to copy artifacts to releases folder after build
add_custom_command(TARGET oXygen_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/releases"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:oXygen_VST3>/../../" "${CMAKE_SOURCE_DIR}/releases/oXygen.vst3"
    COMMENT "Copying VST3 artifact to releases/oXygen.vst3"
)


