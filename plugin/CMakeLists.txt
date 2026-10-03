juce_add_plugin(Bounce
    PRODUCT_NAME                "Bounce"
    COMPANY_NAME                "Bounce Audio"
    BUNDLE_ID                   "audio.bounce.bounce"
    PLUGIN_MANUFACTURER_CODE    Bnce
    PLUGIN_CODE                 Bnc1
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           TRUE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Generator
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE)

juce_generate_juce_header(Bounce)

file(GLOB BOUNCE_STYLE_PRESETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/presets/styles/*.json")
juce_add_binary_data(BounceData
    NAMESPACE BounceBinary
    SOURCES ${BOUNCE_STYLE_PRESETS})

target_sources(Bounce PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/State/Parameters.cpp
    Source/State/StyleLibrary.cpp
    Source/State/Session.cpp
    Source/Engine/PatternPlayer.cpp
    Source/Engine/KeysSynth.cpp
    Source/Midi/MidiExport.cpp
    Source/UI/BounceLookAndFeel.cpp
    Source/UI/ChordWheel.cpp
    Source/UI/ChordStrip.cpp
    Source/UI/PianoRollPreview.cpp
    Source/UI/DragMidiButton.cpp
    Source/UI/TopBar.cpp
    Source/UI/Widgets.cpp)

target_include_directories(Bounce PRIVATE Source)

target_compile_definitions(Bounce PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(Bounce
    PRIVATE
        bounce::core
        BounceData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(Bounce PRIVATE /utf-8)
endif()
