#pragma once

#include "Analysis/LabModel.h"
#include "State/Session.h"
#include "UI/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace bounce
{
class BounceProcessor;
}

namespace bounce::ui
{

/** Key & BPM Lab: load or capture audio, see tempo / key candidates / chords, adjust them,
    preview a sped-up flip, and send the chords to the generator. */
class LabView : public juce::Component,
                public juce::FileDragAndDropTarget,
                private juce::ChangeListener,
                private juce::Timer
{
public:
    explicit LabView (BounceProcessor& processor);
    ~LabView() override;

    /** Called after "Send to Chord Generator" so the editor can switch views. */
    std::function<void()> onSent;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    BounceProcessor& processor;
    LabModel& lab;

    juce::TextButton openButton { "Open audio..." };
    juce::TextButton captureButton { "Capture input" };
    juce::TextButton cancelButton { "Cancel" };
    juce::TextButton sendButton { "SEND TO CHORD GENERATOR" };
    juce::Slider speedSlider;
    juce::ComboBox barsBox, startBarBox;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;

    juce::Rectangle<int> waveArea, tempoArea, keyArea, speedArea, laneArea;

    juce::Rectangle<float> tempoChoiceRect (int i) const;
    juce::Rectangle<float> keyRect (int i) const;
    juce::Rectangle<float> chordRect (int index) const;
    void showChordMenu (int index);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace bounce::ui
