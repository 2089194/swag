# bounce_core: pure, JUCE-free C++20. Everything here must be unit-testable and must not
# touch GUI, audio devices, files or threads.
add_library(bounce_core STATIC
    src/theory/Pitch.cpp
    src/theory/Scale.cpp
    src/theory/Chord.cpp
    src/theory/Voicing.cpp
    src/gen/StylePreset.cpp
    src/gen/Progression.cpp
    src/gen/ChordGenerator.cpp
    src/gen/ChordRenderer.cpp
    src/gen/IdeaHistory.cpp
    src/gen/Idea.cpp
    src/gen/DrumGenerator.cpp
    src/gen/BassGenerator.cpp
    src/gen/MelodyGenerator.cpp
    src/gen/Arrangement.cpp
    src/analysis/Fft.cpp
    src/analysis/AudioAnalysis.cpp
    src/midi/MidiFile.cpp
    src/util/Json.cpp)

add_library(bounce::core ALIAS bounce_core)

target_include_directories(bounce_core PUBLIC include)
target_compile_features(bounce_core PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(bounce_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(bounce_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
