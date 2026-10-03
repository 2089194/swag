#pragma once

#include "State/Session.h"
#include "UI/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace bounce::ui
{

/** Logo, style browser, key/scale, tempo readout, Generate / dice, undo/redo and idea history. */
class TopBar : public juce::Component,
               private juce::ChangeListener
{
public:
    TopBar (Session& session, juce::AudioProcessorValueTreeState& state, std::function<double()> hostBpmSource);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    /** Called at UI rate to refresh the tempo readout. */
    void tick();

    void refreshStyles();

    /** 0 = generator, 1 = Key & BPM Lab. */
    std::function<void (int)> onViewChange;
    void setView (int v) { view = v; repaint(); }

private:
    Session& session;
    juce::AudioProcessorValueTreeState& apvts;
    std::function<double()> hostBpm;

    juce::ComboBox styleBox;
    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;

    IconButton transposeDown { "Transpose down a semitone", Icons::chevron (false) };
    IconButton transposeUp { "Transpose up a semitone", Icons::chevron (true) };
    juce::TextButton generateButton { "GENERATE" };
    IconButton diceButton { "Randomise: new seed plus random colour, mood, voicing and rhythm", Icons::dice(), Colours::text };
    IconButton undoButton { "Undo (Ctrl+Z)", Icons::undo() };
    IconButton redoButton { "Redo (Ctrl+Shift+Z)", Icons::redo() };
    IconButton historyButton { "Idea history: the last 50 generated ideas", Icons::history() };

    juce::Rectangle<int> tempoArea, seedArea, viewArea;
    int view = 0;
    double shownBpm = -1.0;
    int shownTempoView = -1;

    void showHistoryMenu();
    void showSeedMenu();
    std::unique_ptr<juce::AlertWindow> seedDialog;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace bounce::ui
