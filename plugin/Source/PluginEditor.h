#pragma once

#include "PluginProcessor.h"
#include "UI/BounceLookAndFeel.h"
#include "UI/ChordStrip.h"
#include "UI/ChordWheel.h"
#include "UI/LabView.h"
#include "UI/ModulePanel.h"
#include "UI/PartLanes.h"
#include "UI/TopBar.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace bounce
{

/** The whole UI is laid out at a fixed design size inside `content`, then scaled with an
    AffineTransform, so it stays crisp (everything is vector) from 75% to 200%. The first time it
    opens it picks the largest size that fits the screen (up to 100%); after that the user's size
    is remembered in the plugin state. */
class BounceEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr int designWidth = 1360;
    static constexpr int designHeight = 860;

    explicit BounceEditor (BounceProcessor&);
    ~BounceEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Window width that fits the current screen (used for the first open). */
    static int defaultWidthForScreen();

private:
    class Content : public juce::Component
    {
    public:
        explicit Content (BounceProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();
        void setView (int v);

    private:
        BounceProcessor& processor;
        Session& session;
        int view = 0;

        ui::TopBar topBar;
        ui::ModulePanel modules;
        ui::ChordWheel wheel;
        ui::MixerPanel mixer;
        ui::ChordStrip strip;
        std::vector<std::unique_ptr<ui::PartLane>> lanes;
        ui::LabView lab;

        juce::Rectangle<int> wheelPanel, partsPanel;
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
