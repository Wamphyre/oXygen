cmake_minimum_required(VERSION 3.22)

project(oXygen VERSION 0.0.1)

# Set C++ standard
# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# System Detection & Optimization Flags
if(APPLE)
    message(STATUS "🍏 Detected System: macOS")
    # macOS Specific Optimizations
    add_compile_options(-O3 -ffast-math -march=native -funroll-loops -flto)
    
elseif(MSVC)
    message(STATUS "🪟 Detected System: Windows (MSVC)")
    # Windows/MSVC Specific Optimizations
    add_compile_options(/MP /W4 /permissive- /O2 /Oi /Ot /GF /Gy /arch:AVX2)
    
elseif(UNIX AND NOT APPLE)
    message(STATUS "🐧 Detected System: Linux/Unix")
    # Linux Specific Optimizations
    add_compile_options(-O3 -ffast-math -march=native)
    
endif()

# Enable LTO (Interprocedural Optimization)
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Add JUCE
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    # Add JUCE using FetchContent (ensure it downloads if missing)
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
        Source/Modules/MultibandCompressorModule.cpp
        Source/Modules/MultibandCompressorModule.h
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

# Link against JUCE modules
target_link_libraries(oXygen
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

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


