#include "PluginEditor.h"

#include "Midi/MidiExport.h"
#include "State/Parameters.h"

namespace bounce
{

using namespace ui;

namespace
{
const juce::Identifier kUiWidth ("uiWidth");
} // namespace

BounceEditor::Content::Content (BounceProcessor& p)
    : processor (p),
      session (p.getSession()),
      topBar (session, p.getState(), [&p] { return p.getHostBpm(); }),
      wheel (session, [&p] { return p.getLoopPosition(); }),
      strip (session),
      roll (session, [&p] { return p.getLoopPosition(); }),
      complexity (p.getState(), params::complexity, "Complexity", Colours::chords),
      mood (p.getState(), params::mood, "Mood", Colours::chords),
      borrowed (p.getState(), params::borrowed, "Borrowed", Colours::chords),
      humanise (p.getState(), params::humanise, "Humanise", Colours::chords),
      swing (p.getState(), params::swing, "Swing", Colours::chords),
      octave (p.getState(), params::octave, "Octave", Colours::chords),
      bars (p.getState(), params::bars, "Bars"),
      chordCount (p.getState(), params::chordCount, "Chords"),
      rhythm (p.getState(), params::rhythm, "Rhythm"),
      voicing (p.getState(), params::voicing, "Voicing"),
      internalSound (p.getState(), params::internalSound, "Sound", Colours::chords),
      midiOut (p.getState(), params::midiOut, "MIDI Out", Colours::melody),
      preview (p.getState(), params::preview, "Preview", Colours::good),
      mute (p.getState(), params::chordsMute, "Mute", Colours::bass),
      level (p.getState(), params::chordsLevel, "Level", Colours::chords),
      midiChannel (p.getState(), params::midiChannel, "MIDI Ch"),
      dragChords ("Drag Chords MIDI", Colours::chords, [this]
      {
          ui::DragMidiButton::Payload payload;
          payload.clips = session.clipsForExport ("chords");
          payload.bpm = session.tempo();
          payload.fileName = session.exportFileName ("chords");
          return payload;
      })
{
    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &topBar, &wheel, &strip, &roll, &complexity, &mood, &borrowed, &humanise, &swing, &octave,
             &bars, &chordCount, &rhythm, &voicing, &internalSound, &midiOut, &preview, &mute, &level,
             &midiChannel, &dragChords, &exportButton, &stylesFolderButton, &reloadStylesButton })
        addAndMakeVisible (c);

    complexity.setTooltip ("Triads ... 7ths ... 9ths/11ths");
    mood.setTooltip ("Dark (minor, borrowed) ... Bright (major)");
    borrowed.setTooltip ("Modal mixture: bVI, bVII, iv in major; IV, V in minor");
    humanise.setTooltip ("Strum, timing and velocity variation");
    preview.setTooltip ("Loop the idea while the host is stopped");
    internalSound.setTooltip ("Play through Bounce's built-in keys");
    midiOut.setTooltip ("Send the notes out of the plugin (FL: set a MIDI output port in the wrapper)");

    exportButton.onClick = [this] { exportToFolder(); };
    stylesFolderButton.onClick = [this] { openStylesFolder(); };
    reloadStylesButton.onClick = [this] { reloadStyles(); };
    exportButton.setTooltip ("Write the .mid files (named by chords, key and BPM) into a folder");
}

void BounceEditor::Content::tick()
{
    topBar.tick();
    wheel.tick();
    roll.tick();
    const double pos = processor.getLoopPosition();
    strip.setPlayingSlot (pos >= 0.0 ? session.progression().slotAt (pos) : -1);
}

void BounceEditor::Content::exportToFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI to folder", MidiExport::defaultExportFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (folder == juce::File())
            return;
        folder.createDirectory();

        const auto file = folder.getChildFile (session.exportFileName ("chords"));
        const bool ok = MidiExport::writeFile (file, session.clipsForExport ("chords"), session.tempo());
        exportStatus = ok ? "Saved " + file.getFileName() : "Couldn't write to " + folder.getFullPathName();
        repaint();
    });
}

void BounceEditor::Content::openStylesFolder()
{
    const auto folder = StyleLibrary::userStyleFolder();
    folder.createDirectory();

    // First visit: drop a starter file the user can copy and edit.
    if (folder.findChildFiles (juce::File::findFiles, false, "*.json").isEmpty())
    {
        auto starter = session.currentStyle();
        starter.id = "my_style";
        starter.name = "My Style";
        starter.description = "Copy of " + session.currentStyle().name + ". Edit me, then press Reload styles.";
        folder.getChildFile ("my_style.json").replaceWithText (juce::String (starter.toJson().dump (2)));
    }
    folder.revealToUser();
}

void BounceEditor::Content::reloadStyles()
{
    const auto warnings = session.reloadStyles();
    topBar.refreshStyles();
    if (! warnings.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Style presets",
                                                warnings.joinIntoString ("\n"));
}

void BounceEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    drawPanel (g, leftPanel.toFloat(), "Chords", Colours::chords);
    drawPanel (g, rightPanel.toFloat(), "Output", Colours::melody);
    drawPanel (g, bottomPanel.toFloat(), "Progression", Colours::chords);

    // Hint line under the wheel.
    g.setColour (Colours::textFaint);
    g.setFont (uiFont (11.5f));
    g.drawText (juce::String::fromUTF8 ("click a chord to select  \xc2\xb7  right-click for lock / reharm / voicing  \xc2\xb7  G generate  \xc2\xb7  L lock"),
                wheelPanel.removeFromBottom (18).toFloat(), juce::Justification::centred);

    if (exportStatus.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (exportStatus, exportButton.getBounds().translated (0, exportButton.getHeight() + 1).withHeight (12),
                          juce::Justification::centredTop, 1);
    }
}

void BounceEditor::Content::resized()
{
    auto b = getLocalBounds();
    topBar.setBounds (b.removeFromTop (60));
    b.reduce (12, 12);

    bottomPanel = b.removeFromBottom (262);
    b.removeFromBottom (12);

    leftPanel = b.removeFromLeft (262);
    b.removeFromLeft (12);
    rightPanel = b.removeFromRight (262);
    b.removeFromRight (12);
    wheelPanel = b;
    wheel.setBounds (wheelPanel.withTrimmedBottom (18));

    // Chords module: 3x2 knobs, then 2x2 combos.
    {
        auto p = leftPanel.reduced (14).withTrimmedTop (30);
        auto knobs = p.removeFromTop (196);
        const int kw = knobs.getWidth() / 3;
        auto row1 = knobs.removeFromTop (98), row2 = knobs;
        for (auto* k : { &complexity, &mood, &borrowed })
            k->setBounds (row1.removeFromLeft (kw));
        for (auto* k : { &humanise, &swing, &octave })
            k->setBounds (row2.removeFromLeft (kw));

        p.removeFromTop (8);
        const int cw = (p.getWidth() - 10) / 2;
        auto c1 = p.removeFromTop (44), c2 = (p.removeFromTop (6), p.removeFromTop (44));
        bars.setBounds (c1.removeFromLeft (cw));
        chordCount.setBounds (c1.removeFromRight (cw));
        rhythm.setBounds (c2.removeFromLeft (cw));
        voicing.setBounds (c2.removeFromRight (cw));
    }

    // Output module.
    {
        auto p = rightPanel.reduced (14).withTrimmedTop (30);
        const int pw = (p.getWidth() - 8) / 2;
        auto r1 = p.removeFromTop (28);
        internalSound.setBounds (r1.removeFromLeft (pw));
        midiOut.setBounds (r1.removeFromRight (pw));
        p.removeFromTop (8);
        auto r2 = p.removeFromTop (28);
        preview.setBounds (r2.removeFromLeft (pw));
        mute.setBounds (r2.removeFromRight (pw));
        p.removeFromTop (8);

        auto r3 = p.removeFromTop (80);
        level.setBounds (r3.removeFromLeft (pw));
        midiChannel.setBounds (r3.removeFromRight (pw).withSizeKeepingCentre (pw, 44));
        p.removeFromTop (8);

        dragChords.setBounds (p.removeFromTop (58));
        p.removeFromTop (8);
        exportButton.setBounds (p.removeFromTop (30));
        auto r4 = p.removeFromBottom (28);
        stylesFolderButton.setBounds (r4.removeFromLeft (pw));
        reloadStylesButton.setBounds (r4.removeFromRight (pw));
    }

    // Progression: chord cards over the mini piano roll.
    {
        auto p = bottomPanel.reduced (14).withTrimmedTop (28);
        strip.setBounds (p.removeFromTop (104));
        p.removeFromTop (8);
        roll.setBounds (p);
    }
}

//==============================================================================
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
    const int savedWidth = static_cast<int> (p.getState().state.getProperty (kUiWidth, designWidth));
    const int w = juce::jlimit (designWidth * 3 / 4, designWidth * 2, savedWidth);

    setResizable (true, true);
    setResizeLimits (designWidth * 3 / 4, designHeight * 3 / 4, designWidth * 2, designHeight * 2);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    setSize (w, juce::roundToInt (w * static_cast<double> (designHeight) / designWidth));
    sizeRestored = true;
    setWantsKeyboardFocus (true);
}

BounceEditor::~BounceEditor()
{
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
        processor.getState().state.setProperty (kUiWidth, getWidth(), nullptr);
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
