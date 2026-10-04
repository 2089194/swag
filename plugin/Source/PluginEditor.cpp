#include "PluginEditor.h"

#include "State/Parameters.h"

namespace bounce
{

using namespace ui;
using gen::Part;

namespace
{
const juce::Identifier kUiWidth ("uiWidth");
const juce::Identifier kUiView ("uiView");
} // namespace

BounceEditor::Content::Content (BounceProcessor& p)
    : processor (p),
      session (p.getSession()),
      topBar (session, p.getState(), [&p] { return p.getHostBpm(); }),
      modules (p),
      wheel (session, [&p] { return p.getLoopPosition(); }),
      mixer (p),
      strip (session),
      lab (p)
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { &topBar, &modules, &wheel, &mixer, &strip })
        addAndMakeVisible (c);
    addChildComponent (lab);

    for (auto part : { Part::Chords, Part::Bass, Part::Melody, Part::Drums })
    {
        lanes.push_back (std::make_unique<PartLane> (session, part, [&p] { return p.getLoopPosition(); }));
        addAndMakeVisible (*lanes.back());
    }

    // Right-click a drum lane name: load / reset a custom one-shot.
    if (auto* grid = lanes.back()->getDrumGrid())
        grid->onLaneMenu = [this, grid] (gen::DrumLane lane)
        {
            juce::PopupMenu m;
            m.addSectionHeader (juce::String (std::string (gen::drumLaneName (lane))) + ": " + processor.getDrumSampleName (lane));
            m.addItem (1, "Load sample...");
            m.addItem (2, "Back to built-in sound", processor.hasCustomDrumSample (lane));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (grid), [this, lane] (int r)
            {
                if (r == 2)
                    processor.resetDrumSample (lane);
                if (r != 1)
                    return;
                auto chooser = std::make_shared<juce::FileChooser> ("Load a one-shot", juce::File(),
                                                                    processor.getFormatManager().getWildcardForAllFormats());
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this, lane, chooser] (const juce::FileChooser& fc)
                {
                    if (fc.getResult().existsAsFile() && ! processor.loadDrumSample (lane, fc.getResult()))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Drum sample",
                                                                "Couldn't read " + fc.getResult().getFileName());
                });
            });
        };

    topBar.onViewChange = [this] (int v) { setView (v); };
    lab.onSent = [this] { setView (0); topBar.setView (0); };
    view = static_cast<int> (p.getState().state.getProperty (kUiView, 0));
    topBar.setView (view);
    setView (view);
}

void BounceEditor::Content::setView (int v)
{
    view = juce::jlimit (0, 1, v);
    const bool gen = view == 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &modules, &wheel, &mixer, &strip })
        c->setVisible (gen);
    for (auto& l : lanes)
        l->setVisible (gen);
    lab.setVisible (! gen);
    processor.getState().state.setProperty (kUiView, view, nullptr);
    repaint();
}

void BounceEditor::Content::tick()
{
    topBar.tick();
    if (view != 0)
        return;
    wheel.tick();
    for (auto& l : lanes)
        l->tick();
    const double pos = processor.getLoopPosition();
    strip.setPlayingSlot (pos >= 0.0 ? session.progression().slotAt (pos) : -1);
}

void BounceEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
    if (view != 0)
        return;

    drawPanel (g, partsPanel.toFloat(), "Parts", Colours::chords);
    g.setColour (Colours::textFaint);
    g.setFont (uiFont (11.0f));
    g.drawText (juce::String::fromUTF8 ("click a note to delete \xc2\xb7 double-click to add \xc2\xb7 dice re-rolls one part \xc2\xb7 "
                                        "drag a part's tile into FL"),
                partsPanel.reduced (14, 10).removeFromTop (18).toFloat(), juce::Justification::centredRight);
    g.drawText (juce::String::fromUTF8 ("click a chord to select \xc2\xb7 right-click: lock / reharm / voicing / length \xc2\xb7 G generate"),
                wheelPanel.withTop (wheelPanel.getBottom() - 16).toFloat(), juce::Justification::centred);
}

void BounceEditor::Content::resized()
{
    auto b = getLocalBounds();
    topBar.setBounds (b.removeFromTop (60));
    b.reduce (12, 12);
    lab.setBounds (b);

    auto row = b.removeFromTop (404);
    b.removeFromTop (12);
    modules.setBounds (row.removeFromLeft (336));
    row.removeFromLeft (12);
    mixer.setBounds (row.removeFromRight (380));
    row.removeFromRight (12);
    wheelPanel = row;
    wheel.setBounds (row.withTrimmedBottom (18));

    partsPanel = b;
    auto p = b.reduced (14).withTrimmedTop (24);
    strip.setBounds (p.removeFromTop (84));
    p.removeFromTop (8);
    const int gap = 6;
    const int h = (p.getHeight() - gap * (static_cast<int> (lanes.size()) - 1)) / static_cast<int> (lanes.size());
    for (auto& l : lanes)
    {
        l->setBounds (p.removeFromTop (h));
        p.removeFromTop (gap);
    }
}

//==============================================================================
int BounceEditor::defaultWidthForScreen()
{
    // A comfortable window that leaves FL's Playlist and Piano Roll visible: about 60% of the
    // screen height, never more than 80% of the design size, never below the 60% minimum.
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const double fit = juce::jmin (0.8, area.getWidth() * 0.6 / designWidth, area.getHeight() * 0.62 / designHeight);
        return juce::roundToInt (designWidth * juce::jmax (0.6, fit));
    }
    return designWidth * 7 / 10;
}

namespace
{
/** Remembers the last window width across every Bounce instance and project
    (%APPDATA%\Bounce\Bounce.settings), so a new instance opens at the size you last chose. */
std::unique_ptr<juce::PropertiesFile> openGlobalSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Bounce";
    o.filenameSuffix = ".settings";
    o.folderName = "Bounce";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}
} // namespace

BounceEditor::BounceEditor (BounceProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      content (p),
      vblank (this, [this] { content.tick(); })
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel); // popups and alerts too
    addAndMakeVisible (content);
    content.setSize (designWidth, designHeight);

    // Read the saved size first: setting the resize limits triggers a resize of its own.
    // A size saved in this project wins, then the last size used anywhere, then a default
    // that fits beside FL's windows. (Sizes saved before "uiDesign3" came from the old
    // fill-the-screen default, so they're ignored.)
    int saved = p.getState().state.hasProperty ("uiDesign3") ? static_cast<int> (p.getState().state.getProperty (kUiWidth, 0)) : 0;
    if (saved <= 0)
        if (auto settings = openGlobalSettings())
            saved = settings->getIntValue (kUiWidth.toString(), 0);
    const int w = juce::jlimit (designWidth * 6 / 10, designWidth * 2, saved > 0 ? saved : defaultWidthForScreen());

    setResizable (true, true);
    setResizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (w, juce::roundToInt (w * static_cast<double> (designHeight) / designWidth));
    sizeRestored = true;
    setWantsKeyboardFocus (true);
}

BounceEditor::~BounceEditor()
{
    if (auto settings = openGlobalSettings())
        settings->setValue (kUiWidth.toString(), getWidth()); // written when `settings` closes
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void BounceEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);
}

void BounceEditor::resized()
{
    const float scale = static_cast<float> (getWidth()) / static_cast<float> (designWidth);
    content.setTransform (juce::AffineTransform::scale (scale));
    if (sizeRestored)
    {
        processor.getState().state.setProperty (kUiWidth, getWidth(), nullptr);
        processor.getState().state.setProperty ("uiDesign3", true, nullptr);
    }
}

bool BounceEditor::keyPressed (const juce::KeyPress& key)
{
    auto& session = processor.getSession();
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    const int slots = static_cast<int> (session.progression().slots.size());
    const int selected = session.getSelectedSlot();

    if (mods.isCommandDown() && (code == 'Z' || code == 'z'))
    {
        mods.isShiftDown() ? session.redo() : session.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'Y' || code == 'y'))
    {
        session.redo();
        return true;
    }
    if (mods.isAnyModifierKeyDown() && ! mods.isShiftDown())
        return false;

    switch (code)
    {
        case 'G': case 'g': session.generate(); return true;
        case 'R': case 'r': session.reharmonise (selected); return true;
        case 'L': case 'l': session.toggleLock (selected); return true;
        default: break;
    }
    if (code >= '1' && code <= '8' && code - '1' < slots)
    {
        session.setSelectedSlot (code - '1');
        return true;
    }
    if (key == juce::KeyPress::leftKey)  { session.setSelectedSlot ((selected + slots - 1) % slots); return true; }
    if (key == juce::KeyPress::rightKey) { session.setSelectedSlot ((selected + 1) % slots); return true; }
    if (key == juce::KeyPress::upKey)    { session.invert (selected, 1); return true; }
    if (key == juce::KeyPress::downKey)  { session.invert (selected, -1); return true; }
    return false;
}

} // namespace bounce
