cmake_minimum_required(VERSION 3.22)

project(AGC2 VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(agc_dsp STATIC
    Source/PredictiveAGC.cpp
    Source/PredictiveDucker.cpp)
target_include_directories(agc_dsp PUBLIC Source)
target_compile_features(agc_dsp PUBLIC cxx_std_17)
set_target_properties(agc_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(agc_tests tests/agc_tests.cpp)
target_link_libraries(agc_tests PRIVATE agc_dsp)
target_compile_options(agc_tests PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_test(NAME agc_tests COMMAND agc_tests)

# JUCE 8 rejects the Xcode 15.0 linker. That toolchain is still common, and it
# ships the previous linker as ld_classic, which does not have the bug.
if(APPLE)
    string(APPEND CMAKE_C_FLAGS " -DJUCE_SILENCE_XCODE_15_LINKER_WARNING=1")
    string(APPEND CMAKE_CXX_FLAGS " -DJUCE_SILENCE_XCODE_15_LINKER_WARNING=1")

    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -dM -E -x c /dev/null
        OUTPUT_VARIABLE AGC_APPLE_PREDEFS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(AGC_APPLE_PREDEFS MATCHES "__apple_build_version__ ([0-9]+)")
        set(AGC_APPLE_BUILD "${CMAKE_MATCH_1}")
        if(AGC_APPLE_BUILD GREATER_EQUAL 15000000 AND AGC_APPLE_BUILD LESS 15000100)
            message(STATUS "Xcode 15.0 detected. Linking with ld_classic.")
            foreach(agc_kind EXE SHARED MODULE)
                string(APPEND CMAKE_${agc_kind}_LINKER_FLAGS " -Wl,-ld_classic")
            endforeach()
        endif()
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(AGC2_FORMATS AU Standalone)
else()
    set(AGC2_FORMATS Standalone)
endif()

juce_add_plugin(AGC2
    COMPANY_NAME "AGC"
    BUNDLE_ID com.agc.agc2
    PLUGIN_MANUFACTURER_CODE AgcL
    PLUGIN_CODE Ag20
    FORMATS ${AGC2_FORMATS}
    PRODUCT_NAME "AGC 2.0"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(AGC2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(AGC2
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(AGC2
    PRIVATE
        agc_dsp
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Logic skips an unsigned Audio Unit. Ad-hoc sign the bundle after each Mac build.
if(TARGET AGC2_AU)
    add_custom_command(TARGET AGC2_AU POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:AGC2_AU>"
        VERBATIM)
endif()

# CMake 3.28 drops these rules while configuring JUCE. Put them back so generate succeeds.
if(NOT CMAKE_C_COMPILE_OBJECT)
    set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
endif()
if(NOT CMAKE_CXX_COMPILE_OBJECT)
    set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
endif()
