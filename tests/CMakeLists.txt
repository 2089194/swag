add_executable(bounce_tests
    main.cpp
    TheoryTests.cpp
    VoicingTests.cpp
    GeneratorTests.cpp
    RendererTests.cpp
    MidiFileTests.cpp
    JsonTests.cpp
    StylePresetTests.cpp
    UtilTests.cpp)

find_package(Threads REQUIRED)
target_link_libraries(bounce_tests PRIVATE bounce::core doctest::doctest Threads::Threads)
target_compile_definitions(bounce_tests PRIVATE
    BOUNCE_PRESET_DIR="${CMAKE_SOURCE_DIR}/presets/styles")

if(MSVC)
    target_compile_options(bounce_tests PRIVATE /utf-8)
endif()

add_test(NAME bounce_tests COMMAND bounce_tests)

# Real-time engine tests (need JUCE, so only when the plugin is built).
if(BOUNCE_BUILD_PLUGIN)
    juce_add_console_app(bounce_engine_tests PRODUCT_NAME "Bounce Engine Tests")
    target_sources(bounce_engine_tests PRIVATE
        engine/PatternPlayerTests.cpp
        ${CMAKE_SOURCE_DIR}/plugin/Source/Engine/PatternPlayer.cpp)
    target_include_directories(bounce_engine_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin/Source)
    target_compile_definitions(bounce_engine_tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(bounce_engine_tests PRIVATE
        bounce::core doctest::doctest juce::juce_audio_basics
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    add_test(NAME bounce_engine_tests COMMAND bounce_engine_tests)
endif()
