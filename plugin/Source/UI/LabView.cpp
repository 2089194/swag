#include "UI/LabView.h"

#include "PluginProcessor.h"
#include "UI/BounceLookAndFeel.h"

namespace bounce::ui
{

namespace
{
const juce::Colour labAccent = Colours::good;

juce::String keyLabel (const theory::Key& k)
{
    return juce::String (k.name());
}
} // namespace

LabView::LabView (BounceProcessor& p) : processor (p), lab (p.getLab())
{
    for (auto* c : std::initializer_list<juce::Component*> { &openButton, &captureButton, &cancelButton, &sendButton, &speedSlider, &barsBox, &startBarBox })
        addAndMakeVisible (c);

    openButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open audio to analyse", juce::File(),
                                                       processor.getFormatManager().getWildcardForAllFormats());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                lab.analyseFile (fc.getResult());
        });
    };

    captureButton.setClickingTogglesState (false);
    captureButton.setTooltip ("Records the plugin's audio input (route audio into Bounce's input/sidechain), up to 60 s");
    captureButton.onClick = [this]
    {
        if (processor.isCapturing())
        {
            auto audio = processor.stopCapture();
            if (audio.getNumSamples() > 0)
                lab.analyseBuffer (std::move (audio), processor.getSampleRateForCapture(), "Captured input");
        }
        else
            processor.startCapture();
        captureButton.setButtonText (processor.isCapturing() ? "Stop & analyse" : "Capture input");
    };

    cancelButton.onClick = [this] { lab.cancel(); };

    sendButton.getProperties().set ("primary", true);
    setAccent (sendButton, labAccent);
    sendButton.setTooltip ("The detected chords (from the start bar, for the chosen length) become a locked progression: unlock the ones you want to vary, or reharmonise them");
    sendButton.onClick = [this]
    {
        const int bars = barsBox.getSelectedId() > 0 ? barsBox.getSelectedId() : 4;
        auto prog = lab.makeProgression (bars, startBarBox.getSelectedItemIndex());
        processor.getSession().applyProgression (prog);
        if (onSent)
            onSent();
    };

    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setRange (-12.0, 12.0, 0.1);
    speedSlider.setDoubleClickReturnValue (true, 0.0);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setAccent (speedSlider, labAccent);
    speedSlider.setColour (juce::Slider::trackColourId, labAccent);
    speedSlider.setColour (juce::Slider::thumbColourId, Colours::text);
    speedSlider.setColour (juce::Slider::backgroundColourId, Colours::well);
    speedSlider.onValueChange = [this] { lab.setSpeedSemitones (speedSlider.getValue()); };

    for (int bars : { 2, 4, 8 })
        barsBox.addItem (juce::String (bars) + " bars", bars);
    barsBox.setSelectedId (4, juce::dontSendNotification);

    lab.addChangeListener (this);
    changeListenerCallback (nullptr);
    startTimerHz (15);
}

LabView::~LabView()
{
    lab.removeChangeListener (this);
}

void LabView::timerCallback()
{
    if (lab.getStatus() == LabModel::Status::Working || processor.isCapturing())
        repaint (waveArea);
}

void LabView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const bool ready = lab.getStatus() == LabModel::Status::Ready;
    sendButton.setEnabled (ready && ! lab.getResult().chords.empty());
    cancelButton.setVisible (lab.getStatus() == LabModel::Status::Working);
    speedSlider.setEnabled (ready);
    if (! juce::exactlyEqual (speedSlider.getValue(), lab.getSpeedSemitones()))
        speedSlider.setValue (lab.getSpeedSemitones(), juce::dontSendNotification);

    // Start-bar choices follow the analysed length.
    const auto& r = lab.getResult();
    const double bpm = lab.getChosenBpm();
    const int totalBars = bpm > 0.0 ? juce::jmax (1, static_cast<int> ((r.durationSeconds - r.tempo.firstBeat) * bpm / 240.0)) : 1;
    if (startBarBox.getNumItems() != totalBars)
    {
        startBarBox.clear (juce::dontSendNotification);
        for (int b = 0; b < totalBars; ++b)
            startBarBox.addItem ("from bar " + juce::String (b + 1), b + 1);
        startBarBox.setSelectedItemIndex (0, juce::dontSendNotification);
    }
    repaint();
}

bool LabView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void LabView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (juce::File (f).existsAsFile())
        {
            lab.analyseFile (juce::File (f));
            break;
        }
}

juce::Rectangle<float> LabView::tempoChoiceRect (int i) const
{
    auto b = tempoArea.toFloat().reduced (14.0f).withTrimmedTop (96.0f).removeFromTop (28.0f);
    const float w = b.getWidth() / 3.0f;
    return { b.getX() + w * static_cast<float> (i), b.getY(), w - 4.0f, b.getHeight() };
}

juce::Rectangle<float> LabView::keyRect (int i) const
{
    auto b = keyArea.toFloat().reduced (14.0f).withTrimmedTop (30.0f);
    return { b.getX(), b.getY() + static_cast<float> (i) * 34.0f, b.getWidth(), 30.0f };
}

juce::Rectangle<float> LabView::chordRect (int index) const
{
    const auto& r = lab.getResult();
    const auto lane = laneArea.toFloat().reduced (14.0f).withTrimmedTop (28.0f).withHeight (46.0f);
    if (r.durationSeconds <= 0.0 || index < 0 || index >= static_cast<int> (r.chords.size()))
        return {};
    const auto& c = r.chords[static_cast<size_t> (index)];
    const float x0 = lane.getX() + static_cast<float> (c.start / r.durationSeconds) * lane.getWidth();
    const float x1 = lane.getX() + static_cast<float> (c.end / r.durationSeconds) * lane.getWidth();
    return { x0, lane.getY(), juce::jmax (2.0f, x1 - x0), lane.getHeight() };
}

void LabView::showChordMenu (int index)
{
    const auto& c = lab.getResult().chords[static_cast<size_t> (index)].chord;
    const auto spelling = lab.getChosenKey().preferredSpelling();

    juce::PopupMenu roots, qualities, m;
    for (int pc = 0; pc < 12; ++pc)
        roots.addItem (100 + pc, juce::String (theory::pitchClassName (pc, spelling)), true, pc == c.root);
    using Q = theory::ChordQuality;
    const Q options[] = { Q::Major, Q::Minor, Q::Major7, Q::Minor7, Q::Dominant7, Q::Minor9, Q::Major9, Q::Add9, Q::Sus2, Q::Diminished };
    for (auto q : options)
    {
        const juce::String suffix (std::string (theory::qualitySuffix (q)));
        qualities.addItem (200 + static_cast<int> (q), suffix.isEmpty() ? "maj" : suffix, true, q == c.quality);
    }

    m.addSectionHeader (juce::String (c.name (spelling)));
    m.addSubMenu ("Root", roots);
    m.addSubMenu ("Quality", qualities);
    m.addItem (1, "Merge into previous chord");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chordRect (index).toNearestInt())),
                     [this, index] (int result)
    {
        if (index >= static_cast<int> (lab.getResult().chords.size()))
            return;
        auto chord = lab.getResult().chords[static_cast<size_t> (index)].chord;
        if (result == 1)
            lab.deleteChord (index);
        else if (result >= 200)
        {
            chord.quality = static_cast<theory::ChordQuality> (result - 200);
            lab.setChord (index, chord);
        }
        else if (result >= 100)
        {
            chord.root = result - 100;
            lab.setChord (index, chord);
        }
    });
}

void LabView::mouseUp (const juce::MouseEvent& e)
{
    if (lab.getStatus() != LabModel::Status::Ready)
        return;
    for (int i = 0; i < 3; ++i)
        if (tempoChoiceRect (i).contains (e.position))
            lab.setTempoChoice (i);
    for (int i = 0; i < 4; ++i)
        if (keyRect (i).contains (e.position))
            lab.setKeyChoice (i);
    for (int i = 0; i < static_cast<int> (lab.getResult().chords.size()); ++i)
        if (chordRect (i).contains (e.position))
            showChordMenu (i);
}

void LabView::resized()
{
    auto b = getLocalBounds();
    waveArea = b.removeFromTop (190);
    b.removeFromTop (12);
    laneArea = b.removeFromBottom (170);
    b.removeFromBottom (12);
    const int w = (b.getWidth() - 24) / 3;
    tempoArea = b.removeFromLeft (w);
    b.removeFromLeft (12);
    keyArea = b.removeFromLeft (w);
    b.removeFromLeft (12);
    speedArea = b;

    auto buttons = waveArea.reduced (14).removeFromBottom (30);
    openButton.setBounds (buttons.removeFromLeft (140));
    buttons.removeFromLeft (8);
    captureButton.setBounds (buttons.removeFromLeft (140));
    buttons.removeFromLeft (8);
    cancelButton.setBounds (buttons.removeFromLeft (90));

    speedSlider.setBounds (speedArea.reduced (14).withTrimmedTop (34).removeFromTop (28));

    auto controls = laneArea.reduced (14).removeFromBottom (36);
    sendButton.setBounds (controls.removeFromRight (290));
    controls.removeFromRight (10);
    barsBox.setBounds (controls.removeFromRight (110).reduced (0, 4));
    controls.removeFromRight (8);
    startBarBox.setBounds (controls.removeFromRight (130).reduced (0, 4));
}

void LabView::paint (juce::Graphics& g)
{
    const auto& r = lab.getResult();
    const bool ready = lab.getStatus() == LabModel::Status::Ready;

    // --- Source: waveform / drop zone / progress.
    drawPanel (g, waveArea.toFloat(), u8 ("Key & BPM Lab  \xc2\xb7  ") + (lab.getSourceName().isNotEmpty() ? lab.getSourceName() : juce::String ("no audio")), labAccent);
    auto wave = waveArea.toFloat().reduced (14.0f).withTrimmedTop (30.0f).withTrimmedBottom (40.0f);
    g.setColour (dragOver ? labAccent.withAlpha (0.15f) : Colours::well);
    g.fillRoundedRectangle (wave, 8.0f);
    if (dragOver)
    {
        g.setColour (labAccent);
        g.drawRoundedRectangle (wave, 8.0f, 1.5f);
    }

    const auto& peaks = lab.getOverview();
    if (! peaks.empty())
    {
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.45f;
        const float step = wave.getWidth() / static_cast<float> (peaks.size());
        float maxPeak = 0.001f;
        for (const auto& [lo, hi] : peaks)
            maxPeak = juce::jmax (maxPeak, -lo, hi);
        g.setColour (labAccent.withAlpha (0.55f));
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = wave.getX() + step * static_cast<float> (i);
            g.fillRect (x, mid - peaks[i].second / maxPeak * half, juce::jmax (1.0f, step - 0.5f),
                        (peaks[i].second - peaks[i].first) / maxPeak * half + 1.0f);
        }
        // Beat grid from the detected tempo.
        if (ready && r.tempo.bpm > 0.0 && r.durationSeconds > 0.0)
        {
            const double bar = 240.0 / lab.getChosenBpm();
            for (double t = r.tempo.firstBeat; t < r.durationSeconds; t += bar)
            {
                const float x = wave.getX() + static_cast<float> (t / r.durationSeconds) * wave.getWidth();
                g.setColour (Colours::text.withAlpha (0.18f));
                g.drawVerticalLine (juce::roundToInt (x), wave.getY(), wave.getBottom());
            }
        }
    }

    g.setFont (uiFont (13.0f));
    g.setColour (lab.getStatus() == LabModel::Status::Failed ? Colours::bass : Colours::textDim);
    juce::String status = lab.getStatusText();
    if (processor.isCapturing())
        status = "Recording input... " + juce::String (processor.getCaptureSeconds(), 1) + " s";
    if (peaks.empty() || lab.getStatus() == LabModel::Status::Working || processor.isCapturing())
        g.drawFittedText (status, wave.reduced (10.0f).toNearestInt(), juce::Justification::centred, 2);

    if (lab.getStatus() == LabModel::Status::Working)
    {
        auto bar = wave.withTrimmedTop (wave.getHeight() - 6.0f).reduced (8.0f, 0.0f);
        g.setColour (Colours::outline);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (labAccent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * static_cast<float> (lab.getProgress())), 3.0f);
    }

    // --- Tempo.
    drawPanel (g, tempoArea.toFloat(), "Tempo", Colours::bass);
    {
        auto t = tempoArea.toFloat().reduced (14.0f).withTrimmedTop (28.0f);
        g.setColour (Colours::text);
        g.setFont (uiFont (40.0f, true));
        g.drawText (ready ? juce::String (lab.getChosenBpm(), 1) : juce::String ("--"), t.removeFromTop (46.0f), juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (ready ? u8 ("BPM  \xc2\xb7  confidence ") + juce::String (juce::roundToInt (r.tempo.confidence * 100.0)) + "%" : juce::String ("BPM"),
                    t.removeFromTop (18.0f), juce::Justification::centredLeft);

        const char* labels[] = { "As detected", "\xc2\xbd-time", "2x" };
        for (int i = 0; i < 3; ++i)
        {
            const auto rr = tempoChoiceRect (i);
            const bool on = lab.getTempoChoice() == i;
            g.setColour (on ? Colours::bass.withAlpha (0.25f) : Colours::well);
            g.fillRoundedRectangle (rr, rr.getHeight() / 2.0f);
            g.setColour (on ? Colours::bass : Colours::outline);
            g.drawRoundedRectangle (rr, rr.getHeight() / 2.0f, 1.0f);
            g.setColour (on ? Colours::text : Colours::textDim);
            g.setFont (uiFont (11.5f, true));
            g.drawText (juce::String::fromUTF8 (labels[i]), rr, juce::Justification::centred);
        }

        const double host = processor.getHostBpm();
        g.setColour (Colours::textDim);
        g.setFont (uiFont (11.5f));
        if (ready && host > 0.0)
            g.drawFittedText ("Project tempo " + juce::String (host, 1) + " BPM: play the sample at "
                                  + juce::String (host / lab.getChosenBpm() * 100.0, 1) + "% speed to match",
                              tempoChoiceRect (0).withY (tempoChoiceRect (0).getBottom() + 10.0f).withHeight (32.0f)
                                  .withRight (tempoChoiceRect (2).getRight()).toNearestInt(),
                              juce::Justification::topLeft, 2);
    }

    // --- Key candidates.
    drawPanel (g, keyArea.toFloat(), "Key (top 3)", Colours::chords);
    if (ready && ! r.keys.empty())
    {
        for (int i = 0; i < 4; ++i)
        {
            const auto rr = keyRect (i);
            const bool on = lab.getKeyChoice() == i;
            const auto key = i < 3 && i < static_cast<int> (r.keys.size()) ? r.keys[static_cast<size_t> (i)].key : r.keys.front().key.relative();
            const double conf = i < 3 && i < static_cast<int> (r.keys.size()) ? r.keys[static_cast<size_t> (i)].confidence : -1.0;

            g.setColour (on ? Colours::chords.withAlpha (0.2f) : Colours::well);
            g.fillRoundedRectangle (rr, 6.0f);
            g.setColour (on ? Colours::chords : Colours::outline);
            g.drawRoundedRectangle (rr, 6.0f, 1.0f);
            if (conf >= 0.0)
            {
                g.setColour (Colours::chords.withAlpha (0.35f));
                g.fillRoundedRectangle (rr.withWidth (rr.getWidth() * static_cast<float> (conf)).reduced (0.0f, rr.getHeight() * 0.75f).withY (rr.getBottom() - 4.0f), 1.5f);
            }
            g.setColour (Colours::text);
            g.setFont (uiFont (14.0f, true));
            g.drawText ((i == 3 ? "Relative: " : juce::String()) + keyLabel (key), rr.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
            if (conf >= 0.0)
            {
                g.setColour (Colours::textDim);
                g.setFont (uiFont (12.0f));
                g.drawText (juce::String (juce::roundToInt (conf * 100.0)) + "%", rr.reduced (10.0f, 0.0f), juce::Justification::centredRight);
            }
        }
    }

    // --- Speed / pitch helper.
    drawPanel (g, speedArea.toFloat(), "Flip helper: speed / pitch", Colours::melody);
    {
        auto t = speedArea.toFloat().reduced (14.0f).withTrimmedTop (70.0f);
        const auto sc = lab.getSpeedChange();
        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.0f));
        if (! ready)
            g.setColour (Colours::textFaint);
        g.drawText ((sc.semitones >= 0 ? "+" : "") + juce::String (sc.semitones, 1) + u8 (" st  \xc2\xb7  ")
                        + juce::String (sc.ratio * 100.0, 1) + "% speed",
                    t.removeFromTop (18.0f), juce::Justification::centredLeft);
        t.removeFromTop (10.0f);
        g.setColour (Colours::text);
        g.setFont (uiFont (26.0f, true));
        g.drawText (ready ? keyLabel (sc.key) + (sc.cents != 0 ? juce::String (" ") + (sc.cents > 0 ? "+" : "") + juce::String (sc.cents) + "c" : juce::String()) : juce::String ("--"),
                    t.removeFromTop (34.0f), juce::Justification::centredLeft);
        g.drawText (ready ? juce::String (sc.bpm, 1) + " BPM" : juce::String(), t.removeFromTop (34.0f), juce::Justification::centredLeft);
        g.setColour (Colours::textFaint);
        g.setFont (uiFont (11.0f));
        g.drawFittedText ("Varispeed (like speeding up a sample): pitch and tempo move together. Send to generator uses the resulting key.",
                          t.toNearestInt(), juce::Justification::topLeft, 3);
    }

    // --- Chord lane.
    drawPanel (g, laneArea.toFloat(), "Detected chords  (click a chord to edit)", Colours::chords);
    if (ready)
    {
        const auto spelling = lab.getChosenKey().preferredSpelling();
        const auto key = lab.getChosenKey();
        for (int i = 0; i < static_cast<int> (r.chords.size()); ++i)
        {
            const auto rr = chordRect (i).reduced (1.0f, 0.0f);
            const auto& c = r.chords[static_cast<size_t> (i)];
            const bool diatonic = c.chord.isDiatonicTo (key.heptatonicParent());
            g.setColour ((diatonic ? Colours::chords : Colours::bass).withAlpha (0.18f + 0.5f * static_cast<float> (c.confidence)));
            g.fillRoundedRectangle (rr, 4.0f);
            if (rr.getWidth() > 26.0f)
            {
                g.setColour (Colours::text);
                g.setFont (uiFont (juce::jmin (13.0f, rr.getWidth() / 3.0f), true));
                g.drawFittedText (juce::String (c.chord.name (spelling)), rr.reduced (3.0f, 2.0f).removeFromTop (rr.getHeight() * 0.55f).toNearestInt(),
                                  juce::Justification::centred, 1);
                g.setColour (Colours::textDim);
                g.setFont (uiFont (10.0f));
                g.drawFittedText (juce::String (theory::romanNumeral (c.chord, key)), rr.reduced (3.0f, 2.0f).removeFromBottom (rr.getHeight() * 0.4f).toNearestInt(),
                                  juce::Justification::centred, 1);
            }
        }

        // Highlight the region that "send" will use.
        const double bpm = lab.getChosenBpm();
        if (bpm > 0.0 && r.durationSeconds > 0.0)
        {
            const int bars = barsBox.getSelectedId() > 0 ? barsBox.getSelectedId() : 4;
            const double a = r.tempo.firstBeat + startBarBox.getSelectedItemIndex() * 240.0 / bpm;
            const double b = a + bars * 240.0 / bpm;
            const auto lane = laneArea.toFloat().reduced (14.0f).withTrimmedTop (28.0f).withHeight (52.0f);
            const float x0 = lane.getX() + static_cast<float> (a / r.durationSeconds) * lane.getWidth();
            const float x1 = lane.getX() + static_cast<float> (juce::jmin (b, r.durationSeconds) / r.durationSeconds) * lane.getWidth();
            g.setColour (labAccent);
            g.drawRoundedRectangle (juce::Rectangle<float> (x0, lane.getY() - 3.0f, x1 - x0, lane.getHeight()), 4.0f, 1.5f);
        }
    }
    g.setColour (Colours::textFaint);
    g.setFont (uiFont (11.0f));
    g.drawFittedText ("Analysis is offline and never touches the audio thread. No audio is stored: only the chords you send.",
                      laneArea.reduced (14).removeFromBottom (36).withWidth (laneArea.getWidth() - 600), juce::Justification::centredLeft, 2);
}

} // namespace bounce::ui
