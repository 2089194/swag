#pragma once

#include "PluginProcessor.h"
#include "UI/BounceLookAndFeel.h"
#include "UI/ChordStrip.h"
#include "UI/ChordWheel.h"
#include "UI/DragMidiButton.h"
#include "UI/PianoRollPreview.h"
#include "UI/TopBar.h"
#include "UI/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace bounce
{

/** The whole UI is laid out at a fixed design size inside `content`, then scaled with an
    AffineTransform, so it stays crisp (everything is vector) from 75% to 200%. */
class BounceEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int designWidth = 1120;
    static constexpr int designHeight = 720;

    explicit BounceEditor (BounceProcessor&);
    ~BounceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (BounceProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();

    private:
        BounceProcessor& processor;
        Session& session;

        ui::TopBar topBar;
        ui::ChordWheel wheel;
        ui::ChordStrip strip;
        ui::PianoRollPreview roll;

        // Chords module.
        ui::LabeledKnob complexity, mood, borrowed, humanise, swing, octave;
        ui::LabeledCombo bars, chordCount, rhythm, voicing;

        // Output / export.
        ui::PillToggle internalSound, midiOut, preview, mute;
        ui::LabeledKnob level;
        ui::LabeledCombo midiChannel;
        ui::DragMidiButton dragChords;
        juce::TextButton exportButton { "Export to folder..." };
        juce::TextButton stylesFolderButton { "Styles folder" };
        juce::TextButton reloadStylesButton { "Reload styles" };
        juce::String exportStatus;

        juce::Rectangle<int> leftPanel, rightPanel, wheelPanel, bottomPanel;
        std::unique_ptr<juce::FileChooser> chooser;

        void exportToFolder();
        void openStylesFolder();
        void reloadStyles();
    };

    BounceProcessor& processor;
    ui::BounceLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    Content content;
    juce::VBlankAttachment vblank;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BounceEditor)
};

} // namespace bounce
