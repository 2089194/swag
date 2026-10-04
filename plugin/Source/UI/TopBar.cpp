#include "UI/TopBar.h"

#include "State/Parameters.h"

namespace bounce::ui
{

TopBar::TopBar (Session& s, juce::AudioProcessorValueTreeState& state, std::function<double()> bpmSource)
    : session (s), apvts (state), hostBpm (std::move (bpmSource))
{
    addAndMakeVisible (styleBox);
    styleBox.setTooltip ("Style preset (editable JSON, see the Styles folder)");
    styleBox.onChange = [this]
    {
        const int idx = styleBox.getSelectedItemIndex();
        if (idx >= 0 && idx != session.currentStyleIndex())
            session.setStyle (idx);
    };
    setAccent (styleBox, Colours::chords);
    refreshStyles();

    if (auto* keyParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (params::key)))
        keyBox.addItemList (keyParam->choices, 1);
    if (auto* scaleParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (params::scale)))
        scaleBox.addItemList (scaleParam->choices, 1);
    keyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::key, keyBox);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, params::scale, scaleBox);
    keyBox.setTooltip ("Key: changing it transposes the current idea");
    scaleBox.setTooltip ("Scale / mode: changing it re-colours the current idea (same seed)");
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);

    addAndMakeVisible (transposeDown);
    addAndMakeVisible (transposeUp);
    transposeDown.onClick = [this] { session.transpose (-1); };
    transposeUp.onClick = [this] { session.transpose (1); };

    generateButton.getProperties().set ("primary", true);
    setAccent (generateButton, Colours::chords);
    generateButton.setTooltip ("New idea with the current settings (G). Locked chords are kept.");
    generateButton.onClick = [this] { session.generate(); };
    addAndMakeVisible (generateButton);

    playButton.onClick = [this] { session.setPlaying (! session.isPlaying()); tick(); };
    addAndMakeVisible (playButton);
    diceButton.onClick = [this] { session.randomise(); };
    undoButton.onClick = [this] { session.undo(); };
    redoButton.onClick = [this] { session.redo(); };
    historyButton.onClick = [this] { showHistoryMenu(); };
    for (auto* b : { &diceButton, &undoButton, &redoButton, &historyButton })
        addAndMakeVisible (b);

    session.addChangeListener (this);
    changeListenerCallback (nullptr);
}

TopBar::~TopBar()
{
    session.removeChangeListener (this);
}

void TopBar::refreshStyles()
{
    styleBox.clear (juce::dontSendNotification);
    int id = 1;
    for (const auto& p : session.styles().presets())
        styleBox.addItem (juce::String (p.name), id++);
    styleBox.setSelectedItemIndex (session.currentStyleIndex(), juce::dontSendNotification);
}

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    undoButton.setEnabled (session.canUndo());
    redoButton.setEnabled (session.canRedo());
    if (styleBox.getSelectedItemIndex() != session.currentStyleIndex())
        styleBox.setSelectedItemIndex (session.currentStyleIndex(), juce::dontSendNotification);
    repaint (seedArea);
}

void TopBar::showHistoryMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Idea history (newest first)");
    const auto& ideas = session.ideas();
    if (ideas.empty())
        m.addItem (-1, "Generate something first", false);

    int id = 1;
    for (const auto& idea : ideas)
    {
        juce::String text (idea.label);
        text << "   (" << idea.idea.chords.key.name() << ")";
        m.addItem (id++, text, true, idea.idea == session.idea());
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&historyButton),
                     [this] (int result)
    {
        if (result > 0)
            session.recallIdea (result - 1);
    });
}

void TopBar::tick()
{
    const double bpm = hostBpm ? hostBpm() : 0.0;
    const int tempoView = juce::roundToInt (apvts.getRawParameterValue (params::tempoView)->load());
    if (! juce::exactlyEqual (bpm, shownBpm) || tempoView != shownTempoView)
    {
        shownBpm = bpm;
        shownTempoView = tempoView;
        repaint (tempoArea);
    }

    if (const bool playing = session.isPlaying(); playing != shownPlaying)
    {
        shownPlaying = playing;
        playButton.setIcon (playing ? Icons::stop() : Icons::play());
        playButton.setToggleState (playing, juce::dontSendNotification);
    }
}

void TopBar::mouseUp (const juce::MouseEvent& e)
{
    if (viewArea.contains (e.getPosition()))
    {
        const int v = e.x < viewArea.getCentreX() ? 0 : 1;
        if (v != view)
        {
            view = v;
            repaint();
            if (onViewChange)
                onViewChange (view);
        }
    }
    else if (tempoArea.contains (e.getPosition()))
    {
        // Click cycles Normal -> Half-time -> Double-time.
        if (auto* p = apvts.getParameter (params::tempoView))
        {
            const int next = (juce::roundToInt (p->convertFrom0to1 (p->getValue())) + 1) % 3;
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (next)));
            p->endChangeGesture();
        }
    }
    else if (seedArea.contains (e.getPosition()))
    {
        showSeedMenu();
    }
}

void TopBar::showSeedMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Copy seed");
    m.addItem (2, "Enter seed...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (seedArea)),
                     [this] (int result)
    {
        const auto hex = juce::String::toHexString (static_cast<juce::int64> (session.seed())).toUpperCase();
        if (result == 1)
            juce::SystemClipboard::copyTextToClipboard (hex);
        if (result != 2)
            return;

        seedDialog = std::make_unique<juce::AlertWindow> ("Enter seed",
                                                          "Same seed + same settings = same idea (hex).",
                                                          juce::MessageBoxIconType::NoIcon, this);
        seedDialog->addTextEditor ("seed", hex);
        seedDialog->addButton ("Generate", 1, juce::KeyPress (juce::KeyPress::returnKey));
        seedDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        seedDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
        {
            if (r == 1 && seedDialog != nullptr)
            {
                const auto text = seedDialog->getTextEditorContents ("seed").trim().retainCharacters ("0123456789abcdefABCDEF");
                if (text.isNotEmpty())
                    session.generateWithSeed (static_cast<uint64_t> (text.getHexValue64()));
            }
            seedDialog.reset();
        }), false);
    });
}

void TopBar::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Colours::panelLight, 0.0f, 0.0f, Colours::panel, 0.0f, b.getBottom(), false));
    g.fillRect (b);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, b.getWidth());

    // Wordmark with a magenta-to-orange glow.
    auto logo = juce::Rectangle<float> (18.0f, 0.0f, 130.0f, b.getHeight());
    juce::GlyphArrangement ga;
    ga.addLineOfText (uiFont (26.0f, true), "BOUNCE", logo.getX(), logo.getCentreY() + 9.0f);
    juce::Path text;
    ga.createPath (text);
    drawGlow (g, text, Colours::chords, 9.0f, 0.5f);
    g.setGradientFill (juce::ColourGradient (Colours::chords, logo.getX(), 0.0f, Colours::bass, logo.getX() + 120.0f, 0.0f, false));
    g.fillPath (text);

    // Generator / Lab switch.
    {
        const auto v = viewArea.toFloat();
        g.setColour (Colours::well);
        g.fillRoundedRectangle (v, v.getHeight() / 2.0f);
        const char* names[] = { "GENERATOR", "LAB" };
        const juce::Colour cols[] = { Colours::chords, Colours::good };
        for (int i = 0; i < 2; ++i)
        {
            auto seg = v.withWidth (v.getWidth() / 2.0f).translated (v.getWidth() / 2.0f * static_cast<float> (i), 0.0f).reduced (2.0f);
            if (i == view)
            {
                juce::Path p;
                p.addRoundedRectangle (seg, seg.getHeight() / 2.0f);
                drawGlow (g, p, cols[i], 5.0f, 1.0f);
                g.setColour (cols[i].withAlpha (0.25f));
                g.fillPath (p);
                g.setColour (cols[i]);
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            g.setColour (i == view ? Colours::text : Colours::textDim);
            g.setFont (uiFont (11.5f, true));
            g.drawText (names[i], seg, juce::Justification::centred);
        }
    }

    // Tempo readout (host-synced).
    const double bpm = shownBpm > 0.0 ? shownBpm : session.tempo();
    const int tempoView = juce::jmax (0, shownTempoView);
    const double shown = tempoView == 1 ? bpm / 2.0 : tempoView == 2 ? bpm * 2.0 : bpm;
    const auto t = tempoArea.toFloat();
    g.setColour (Colours::well);
    g.fillRoundedRectangle (t, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (t.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (Colours::text);
    g.setFont (uiFont (17.0f, true));
    g.drawText (juce::String (shown, juce::exactlyEqual (shown, std::floor (shown)) ? 0 : 1), t.withTrimmedBottom (14.0f), juce::Justification::centredBottom);
    g.setColour (shownBpm > 0.0 ? Colours::good : Colours::textDim);
    g.setFont (uiFont (9.5f, true));
    const char* viewName[] = { "BPM", "BPM \xc2\xbd-TIME", "BPM 2X" };
    g.drawText (juce::String (shownBpm > 0.0 ? "HOST " : "STYLE ") + juce::String::fromUTF8 (viewName[tempoView]), t.withTrimmedTop (t.getHeight() - 16.0f),
                juce::Justification::centredTop);

    // Seed (click to copy).
    g.setColour (Colours::textFaint);
    g.setFont (uiFont (10.0f, true));
    g.drawText ("SEED", seedArea.toFloat().removeFromTop (14.0f), juce::Justification::centredLeft);
    g.setColour (Colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText (juce::String::toHexString (static_cast<juce::int64> (session.seed())).toUpperCase(),
                seedArea.toFloat().withTrimmedTop (14.0f), juce::Justification::centredLeft);
}

void TopBar::resized()
{
    auto b = getLocalBounds().reduced (12, 10);
    b.removeFromLeft (150); // logo
    viewArea = b.removeFromLeft (176).reduced (0, 4);
    b.removeFromLeft (16);

    styleBox.setBounds (b.removeFromLeft (150));
    b.removeFromLeft (10);
    keyBox.setBounds (b.removeFromLeft (84));
    b.removeFromLeft (4);
    scaleBox.setBounds (b.removeFromLeft (132));
    b.removeFromLeft (2);
    transposeDown.setBounds (b.removeFromLeft (26));
    transposeUp.setBounds (b.removeFromLeft (26));
    b.removeFromLeft (10);
    tempoArea = b.removeFromLeft (92).expanded (0, 2);

    // Right side, from the edge inwards.
    historyButton.setBounds (b.removeFromRight (36));
    b.removeFromRight (2);
    redoButton.setBounds (b.removeFromRight (36));
    undoButton.setBounds (b.removeFromRight (36));
    b.removeFromRight (10);
    seedArea = b.removeFromRight (104);
    b.removeFromRight (6);
    diceButton.setBounds (b.removeFromRight (40));
    b.removeFromRight (6);
    generateButton.setBounds (b.removeFromRight (124));
    b.removeFromRight (6);
    playButton.setBounds (b.removeFromRight (40));
}

} // namespace bounce::ui
