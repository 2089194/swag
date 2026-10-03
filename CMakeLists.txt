cmake_minimum_required(VERSION 3.22)

project(Bounce VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BOUNCE_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(BOUNCE_BUILD_TESTS  "Build the unit tests for the core library" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Dependencies)

# Pure C++ core: music theory, generators, MIDI file I/O. No JUCE dependency.
add_subdirectory(core)

if(BOUNCE_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(BOUNCE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
