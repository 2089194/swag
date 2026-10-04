#pragma once

#include "State/Session.h"
#include "UI/DragMidiButton.h"
#include "UI/Widgets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace bounce
{
class BounceProcessor;
}

namespace bounce::ui
{

/** Section grid for the Arrangement Sketcher: name, bars, which parts play, sweep, drop. */
class ArrangementGrid : public juce::Component,
                        private juce::ChangeListener
{
public:
    explicit ArrangementGrid (Session& session);
    ~ArrangementGrid() override;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Session& session;
    static constexpr int maxRows = 8;

    juce::Rectangle<float> cell (int row, int col) const;
    int numCols() const { return 9; } // name, bars, C, 8, M, Ct, D, sweep, drop
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

/** Left-hand module panel with one tab per generator (each with its own accent colour). */
class ModulePanel : public juce::Component
{
public:
    ModulePanel (BounceProcessor& processor);
    ~ModulePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void setTab (int index);

private:
    BounceProcessor& processor;
    int tab = 0;
    std::vector<std::unique_ptr<juce::Component>> pages;

    juce::Rectangle<int> tabBounds (int index) const;
};

/** Right-hand panel: I/O pills, four mixer strips, drag-all / export / styles buttons. */
class MixerPanel : public juce::Component,
                   private juce::Timer
{
public:
    explicit MixerPanel (BounceProcessor& processor);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BounceProcessor& processor;
    PillToggle internalSound, midiOut, syncHost;
    std::vector<std::unique_ptr<juce::Component>> strips;
    DragMidiButton dragAll;
    juce::TextButton exportButton { "Export..." };
    juce::TextButton stylesFolderButton { "Styles" };
    juce::TextButton reloadStylesButton { "Reload" };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;

    void exportToFolder();
    void openStylesFolder();
    void reloadStyles();
    void timerCallback() override { repaint(); }
};

} // namespace bounce::ui
