#include "UI/ModulePanel.h"

#include "Midi/MidiExport.h"
#include "PluginProcessor.h"
#include "State/Parameters.h"
#include "UI/BounceLookAndFeel.h"
#include "UI/PartLanes.h"

namespace bounce::ui
{

using gen::Part;

namespace
{
const juce::Identifier kUiTab ("uiTab");

/** A page made of rows of controls, laid out top to bottom. */
class Page : public juce::Component
{
public:
    struct Row
    {
        std::vector<juce::Component*> items;
        int height;
        int slots; // columns in the row (knob rows keep a 4-wide grid even when shorter)
    };

    template <typename T, typename... Args>
    T& make (Args&&... args)
    {
        auto c = std::make_unique<T> (std::forward<Args> (args)...);
        auto& ref = *c;
        addAndMakeVisible (ref);
        owned.push_back (std::move (c));
        return ref;
    }

    void row (std::vector<juce::Component*> items, int height, int slots = 0)
    {
        const int n = static_cast<int> (items.size());
        rows.push_back ({ std::move (items), height, juce::jmax (n, slots) });
    }

    void resized() override
    {
        auto b = getLocalBounds();
        for (const auto& r : rows)
        {
            auto line = b.removeFromTop (r.height);
            b.removeFromTop (8);
            const int n = static_cast<int> (r.items.size());
            if (n == 0)
                continue;
            const int gap = 8;
            const int slots = r.slots;
            const int w = (line.getWidth() - gap * (slots - 1)) / slots;
            for (auto* c : r.items)
            {
                c->setBounds (line.removeFromLeft (w));
                line.removeFromLeft (gap);
            }
        }
    }

private:
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Row> rows;
};

/** Callout content: one note per drum lane for the custom map. */
class NoteMapEditor : public juce::Component
{
public:
    explicit NoteMapEditor (juce::AudioProcessorValueTreeState& state)
    {
        for (int l = 0; l < gen::numDrumLanes; ++l)
        {
            auto c = std::make_unique<LabeledCombo> (state, params::drumNoteId (l),
                                                     juce::String (std::string (gen::drumLaneName (static_cast<gen::DrumLane> (l)))));
            addAndMakeVisible (*c);
            combos.push_back (std::move (c));
        }
        setSize (300, 4 * 50 + 10);
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (6);
        for (size_t i = 0; i < combos.size(); ++i)
            combos[i]->setBounds (b.getX() + static_cast<int> (i % 2) * (b.getWidth() / 2 + 2), b.getY() + static_cast<int> (i / 2) * 50,
                                  b.getWidth() / 2 - 4, 44);
    }

private:
    std::vector<std::unique_ptr<LabeledCombo>> combos;
};

const char* const tabNames[] = { "CHORDS", "808", "MELODY", "DRUMS", "ARRANGE" };
const juce::Colour tabColours[] = { Colours::chords, Colours::bass, Colours::melody, Colours::drums, Colours::good };
} // namespace

//==============================================================================
ArrangementGrid::ArrangementGrid (Session& s) : session (s)
{
    session.addChangeListener (this);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

ArrangementGrid::~ArrangementGrid()
{
    session.removeChangeListener (this);
}

juce::Rectangle<float> ArrangementGrid::cell (int row, int col) const
{
    const auto b = getLocalBounds().toFloat();
    const float rowH = b.getHeight() / static_cast<float> (maxRows + 2); // header + rows + summary
    const float widths[] = { 0.24f, 0.12f, 0.08f, 0.08f, 0.08f, 0.08f, 0.08f, 0.12f, 0.12f };
    float x = b.getX();
    for (int c = 0; c < col; ++c)
        x += widths[c] * b.getWidth();
    return { x, b.getY() + rowH * static_cast<float> (row + 1), widths[col] * b.getWidth(), rowH };
}

void ArrangementGrid::paint (juce::Graphics& g)
{
    const auto& arr = session.arrangement();
    const char* headers[] = { "SECTION", "BARS", "C", "8", "M", "CT", "D", "SWEEP", "DROP" };
    g.setFont (uiFont (10.5f, true));
    for (int c = 0; c < numCols(); ++c)
    {
        auto h = cell (-1, c);
        g.setColour (Colours::textDim);
        g.drawText (headers[c], h, juce::Justification::centred);
    }

    const juce::Colour partCols[] = { partColour (Part::Chords), partColour (Part::Bass), partColour (Part::Melody),
                                      partColour (Part::Counter), partColour (Part::Drums) };
    for (int r = 0; r < static_cast<int> (arr.sections.size()) && r < maxRows; ++r)
    {
        const auto& s = arr.sections[static_cast<size_t> (r)];
        if (r % 2 == 0)
        {
            g.setColour (Colours::well.withAlpha (0.6f));
            g.fillRect (cell (r, 0).withRight (cell (r, numCols() - 1).getRight()));
        }
        g.setColour (Colours::text);
        g.setFont (uiFont (12.0f, true));
        g.drawText (juce::String (s.name), cell (r, 0).withTrimmedLeft (6.0f), juce::Justification::centredLeft);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String (s.bars), cell (r, 1), juce::Justification::centred);

        auto pill = [&] (int col, bool on, juce::Colour c)
        {
            auto box = cell (r, col).withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (on ? c : Colours::well);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (on ? c.brighter (0.3f) : Colours::outline);
            g.drawRoundedRectangle (box, 4.0f, 1.0f);
        };
        for (int p = 0; p < gen::numParts; ++p)
            pill (2 + p, s.parts[static_cast<size_t> (p)], partCols[p]);
        pill (7, s.filterSweep, Colours::good);
        pill (8, s.dropLastBar, Colours::bass);
    }

    g.setColour (Colours::textDim);
    g.setFont (uiFont (11.0f));
    const double seconds = arr.totalBars() * 4.0 * 60.0 / session.tempo();
    g.drawText (juce::String (arr.totalBars()) + u8 (" bars  \xc2\xb7  ") + juce::String (static_cast<int> (seconds) / 60) + ":"
                    + juce::String (static_cast<int> (seconds) % 60).paddedLeft ('0', 2),
                cell (maxRows, 0).withRight (cell (maxRows, numCols() - 1).getRight()),
                juce::Justification::centredRight);
}

void ArrangementGrid::mouseUp (const juce::MouseEvent& e)
{
    auto arr = session.arrangement();
    for (int r = 0; r < static_cast<int> (arr.sections.size()) && r < maxRows; ++r)
    {
        for (int c = 0; c < numCols(); ++c)
        {
            if (! cell (r, c).contains (e.position))
                continue;
            auto& s = arr.sections[static_cast<size_t> (r)];
            if (c == 0)
            {
                juce::PopupMenu m;
                const juce::StringArray names { "Intro", "Hook", "Verse", "Pre-Hook", "Bridge", "Breakdown", "Outro" };
                for (int i = 0; i < names.size(); ++i)
                    m.addItem (i + 1, names[i], true, names[i] == juce::String (s.name));
                m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cell (r, c).toNearestInt())),
                                 [this, r, names] (int result)
                {
                    if (result <= 0)
                        return;
                    auto a = session.arrangement();
                    a.sections[static_cast<size_t> (r)].name = names[result - 1].toStdString();
                    session.setArrangement (a);
                });
                return;
            }
            if (c == 1)
                s.bars = s.bars >= 16 ? 2 : s.bars * 2;
            else if (c >= 2 && c <= 6)
                s.parts[static_cast<size_t> (c - 2)] = ! s.parts[static_cast<size_t> (c - 2)];
            else if (c == 7)
                s.filterSweep = ! s.filterSweep;
            else if (c == 8)
                s.dropLastBar = ! s.dropLastBar;
            session.setArrangement (arr);
            return;
        }
    }
}

//==============================================================================
ModulePanel::ModulePanel (BounceProcessor& p) : processor (p)
{
    auto& st = p.getState();
    auto& session = p.getSession();

    // --- Chords.
    {
        auto page = std::make_unique<Page>();
        const auto a = Colours::chords;
        auto& complexity = page->make<LabeledKnob> (st, params::complexity, "Complexity", a);
        auto& mood = page->make<LabeledKnob> (st, params::mood, "Mood", a);
        auto& borrowed = page->make<LabeledKnob> (st, params::borrowed, "Borrowed", a);
        auto& humanise = page->make<LabeledKnob> (st, params::humanise, "Humanise", a);
        auto& swing = page->make<LabeledKnob> (st, params::swing, "Swing", a);
        auto& octave = page->make<LabeledKnob> (st, params::octave, "Octave", a);
        auto& wobble = page->make<LabeledKnob> (st, params::keysWobble, "Tape", a);
        auto& bars = page->make<LabeledCombo> (st, params::bars, "Bars");
        auto& count = page->make<LabeledCombo> (st, params::chordCount, "Chords");
        auto& rhythm = page->make<LabeledCombo> (st, params::rhythm, "Rhythm");
        auto& voicing = page->make<LabeledCombo> (st, params::voicing, "Voicing");
        auto& sound = page->make<LabeledCombo> (st, params::keysType, "Keys sound");
        auto& even = page->make<juce::TextButton> ("Even lengths");
        even.onClick = [&session] { session.resetSlotLengths(); };
        even.setTooltip ("Chord lengths can be changed per chord (right-click a chord). This resets them.");
        complexity.setTooltip ("Triads ... 7ths ... 9ths/11ths");
        mood.setTooltip ("Dark (minor, borrowed) ... Bright (major)");
        borrowed.setTooltip ("Modal mixture: bVI, bVII, iv in major; IV, V in minor");
        wobble.setTooltip ("Tape wow/flutter + lo-fi tone on the keys");
        page->row ({ &complexity, &mood, &borrowed, &humanise }, 88, 4);
        page->row ({ &swing, &octave, &wobble }, 88, 4);
        page->row ({ &bars, &count }, 44);
        page->row ({ &rhythm, &voicing }, 44);
        page->row ({ &sound, &even }, 44);
        pages.push_back (std::move (page));
    }

    // --- 808.
    {
        auto page = std::make_unique<Page>();
        const auto a = Colours::bass;
        auto& mode = page->make<LabeledCombo> (st, params::bassMode, "808 Mode");
        auto& octaves = page->make<LabeledCombo> (st, params::bassOctaves, "Octaves");
        auto& lock = page->make<PillToggle> (st, params::bassLockKick, "Lock to kick", a);
        auto& density = page->make<LabeledKnob> (st, params::bassDensity, "Density", a);
        auto& glide = page->make<LabeledKnob> (st, params::bassGlide, "Glide", a);
        auto& length = page->make<LabeledKnob> (st, params::bassLength, "Length", a);
        auto& reg = page->make<LabeledKnob> (st, params::bassLowNote, "Register", a);
        auto& decay = page->make<LabeledKnob> (st, params::bassDecay, "Decay", a);
        auto& punch = page->make<LabeledKnob> (st, params::bassPunch, "Punch", a);
        glide.setTooltip ("How often notes slide (written as overlapping notes + CC5/CC65 portamento) and the internal 808's glide time");
        page->row ({ &mode, &octaves }, 44);
        page->row ({ &lock }, 26);
        page->row ({ &density, &glide, &length, &reg }, 88, 4);
        page->row ({ &decay, &punch }, 88, 4);
        pages.push_back (std::move (page));
    }

    // --- Melody.
    {
        auto page = std::make_unique<Page>();
        const auto a = Colours::melody;
        auto& feel = page->make<LabeledCombo> (st, params::melFeel, "Feel");
        auto& lead = page->make<LabeledCombo> (st, params::leadType, "Lead sound");
        auto& cr = page->make<PillToggle> (st, params::melCallResponse, "Call & resp", a);
        auto& counter = page->make<PillToggle> (st, params::melCounter, "Counter", a);
        auto& density = page->make<LabeledKnob> (st, params::melDensity, "Density", a);
        auto& rep = page->make<LabeledKnob> (st, params::melRepetition, "Repetition", a);
        auto& catchy = page->make<LabeledKnob> (st, params::melCatchiness, "Catchiness", a);
        auto& pent = page->make<LabeledKnob> (st, params::melPentatonic, "Pentatonic", a);
        auto& oct = page->make<LabeledKnob> (st, params::melOctave, "Octave", a);
        auto& cDensity = page->make<LabeledKnob> (st, params::counterDensity, "Counter", a);
        page->row ({ &feel, &lead }, 44);
        page->row ({ &cr, &counter }, 26);
        page->row ({ &density, &rep, &catchy, &pent }, 88, 4);
        page->row ({ &oct, &cDensity }, 88, 4);
        pages.push_back (std::move (page));
    }

    // --- Drums.
    {
        auto page = std::make_unique<Page>();
        const auto a = Colours::drums;
        auto& kick = page->make<LabeledKnob> (st, params::drumKick, "Kicks", a);
        auto& swing = page->make<LabeledKnob> (st, params::drumSwing, "Swing", a);
        auto& hum = page->make<LabeledKnob> (st, params::drumHumanise, "Humanise", a);
        auto& bounce = page->make<LabeledKnob> (st, params::drumBounce, "Bounce", a);
        auto& rolls = page->make<LabeledKnob> (st, params::drumRolls, "Hat rolls", a);
        auto& rollPitch = page->make<LabeledKnob> (st, params::drumRollPitch, "Roll pitch", a);
        auto& open = page->make<LabeledKnob> (st, params::drumOpenHat, "Open hats", a);
        auto& perc = page->make<LabeledKnob> (st, params::drumPerc, "Percs", a);
        auto& r16 = page->make<PillToggle> (st, params::drumRoll16, "1/16", a);
        auto& r16t = page->make<PillToggle> (st, params::drumRoll16T, "1/16T", a);
        auto& r32 = page->make<PillToggle> (st, params::drumRoll32, "1/32", a);
        auto& r32t = page->make<PillToggle> (st, params::drumRoll32T, "1/32T", a);
        auto& curve = page->make<LabeledCombo> (st, params::drumRollCurve, "Roll velocity");
        auto& map = page->make<LabeledCombo> (st, params::drumMap, "Note map");
        auto& fx = page->make<PillToggle> (st, params::drumFx, "Crash", a);
        auto& pitchNotes = page->make<PillToggle> (st, params::drumPitchNotes, "Pitch notes", a);
        auto& editMap = page->make<juce::TextButton> ("Custom notes...");
        bounce.setTooltip ("Nudges kicks and percs late, off the grid");
        pitchNotes.setTooltip ("Write pitched hat rolls as different MIDI notes (for a hat on its own pitched channel)");
        editMap.onClick = [&st, &editMap]
        {
            juce::CallOutBox::launchAsynchronously (std::make_unique<NoteMapEditor> (st), editMap.getScreenBounds(), nullptr);
        };
        page->row ({ &kick, &swing, &hum, &bounce }, 78, 4);
        page->row ({ &rolls, &rollPitch, &open, &perc }, 78, 4);
        page->row ({ &r16, &r16t, &r32, &r32t }, 24);
        page->row ({ &curve, &map }, 44);
        page->row ({ &fx, &pitchNotes, &editMap }, 24);
        pages.push_back (std::move (page));
    }

    // --- Arrange.
    {
        auto page = std::make_unique<Page>();
        auto& grid = page->make<ArrangementGrid> (session);
        auto& add = page->make<juce::TextButton> ("+ Section");
        auto& remove = page->make<juce::TextButton> ("- Section");
        auto& reset = page->make<juce::TextButton> ("Template");
        auto& drag = page->make<DragMidiButton> ("Drag Arrangement", Colours::good, [&session]
        {
            DragMidiButton::Payload payload;
            payload.clips = session.clipsForExport ("arrangement");
            payload.bpm = session.tempo();
            payload.fileName = session.exportFileName ("arrangement");
            return payload;
        });
        add.onClick = [&session]
        {
            auto a = session.arrangement();
            if (a.sections.size() < 8)
                a.sections.push_back (a.sections.empty() ? gen::Section {} : a.sections.back());
            session.setArrangement (a);
        };
        remove.onClick = [&session]
        {
            auto a = session.arrangement();
            if (a.sections.size() > 1)
                a.sections.pop_back();
            session.setArrangement (a);
        };
        reset.onClick = [&session] { session.setArrangement (gen::Arrangement::defaultTemplate()); };
        drag.setTooltip ("All parts laid out over the sections, with mutes, drops, CC74 filter sweeps and section markers");
        page->row ({ &grid }, 200);
        page->row ({ &add, &remove, &reset }, 26);
        page->row ({ &drag }, 50);
        pages.push_back (std::move (page));
    }

    for (auto& page : pages)
        addChildComponent (*page);
    setTab (static_cast<int> (processor.getState().state.getProperty (kUiTab, 0)));
}

ModulePanel::~ModulePanel() = default;

void ModulePanel::setTab (int index)
{
    tab = juce::jlimit (0, static_cast<int> (pages.size()) - 1, index);
    for (size_t i = 0; i < pages.size(); ++i)
        pages[i]->setVisible (static_cast<int> (i) == tab);
    processor.getState().state.setProperty (kUiTab, tab, nullptr);
    repaint();
}

juce::Rectangle<int> ModulePanel::tabBounds (int index) const
{
    auto b = getLocalBounds().reduced (10, 8).removeFromTop (26);
    const int w = b.getWidth() / static_cast<int> (pages.size());
    return b.withX (b.getX() + w * index).withWidth (w);
}

void ModulePanel::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < static_cast<int> (pages.size()); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            setTab (i);
}

void ModulePanel::resized()
{
    auto b = getLocalBounds().reduced (14);
    b.removeFromTop (34);
    for (auto& p : pages)
        p->setBounds (b);
}

void ModulePanel::paint (juce::Graphics& g)
{
    const auto accent = tabColours[tab];
    drawPanel (g, getLocalBounds().toFloat(), {}, accent);

    for (int i = 0; i < static_cast<int> (pages.size()); ++i)
    {
        const auto r = tabBounds (i).toFloat().reduced (2.0f, 0.0f);
        const bool on = i == tab;
        if (on)
        {
            juce::Path p;
            p.addRoundedRectangle (r, 6.0f);
            drawGlow (g, p, tabColours[i], 5.0f, 1.0f);
            g.setColour (tabColours[i].withAlpha (0.18f));
            g.fillPath (p);
            g.setColour (tabColours[i]);
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        g.setColour (on ? Colours::text : Colours::textDim);
        g.setFont (uiFont (11.5f, true));
        g.drawFittedText (tabNames[i], r.toNearestInt(), juce::Justification::centred, 1);
    }
}

//==============================================================================
namespace
{
class Strip : public juce::Component
{
public:
    Strip (BounceProcessor& p, AudioPart part, const juce::String& name, juce::Colour accent)
        : processor (p), audioPart (part), title (name), colour (accent),
          level (p.getState(), params::mixId (part, params::fLevel), "Level", accent, true),
          drive (p.getState(), params::mixId (part, params::fDrive), "Drive", accent, true),
          tone (p.getState(), params::mixId (part, params::fTone), "Tone", accent, true),
          delay (p.getState(), params::mixId (part, params::fDelay), "Delay", accent, true),
          reverb (p.getState(), params::mixId (part, params::fReverb), "Verb", accent, true),
          mute (p.getState(), params::mixId (part, params::fMute), "M", Colours::bass),
          solo (p.getState(), params::mixId (part, params::fSolo), "S", Colours::good)
    {
        for (auto* c : std::initializer_list<juce::Component*> { &level, &drive, &tone, &delay, &reverb, &mute, &solo })
            addAndMakeVisible (c);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (22);
        auto ms = b.removeFromBottom (22);
        mute.setBounds (ms.removeFromLeft (ms.getWidth() / 2).reduced (2, 0));
        solo.setBounds (ms.reduced (2, 0));
        b.removeFromBottom (4);
        const int h = b.getHeight() / 5;
        for (auto* k : { &level, &drive, &tone, &delay, &reverb })
            k->setBounds (b.removeFromTop (h));
    }

    void paint (juce::Graphics& g) override
    {
        auto top = getLocalBounds().removeFromTop (20).toFloat();
        g.setColour (colour);
        g.setFont (uiFont (11.5f, true));
        g.drawFittedText (title, top.withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 1);

        // Peak meter.
        const float peak = juce::jlimit (0.0f, 1.0f, processor.getPartPeak (audioPart));
        auto meter = top.removeFromRight (5.0f).reduced (0.0f, 2.0f);
        g.setColour (Colours::well);
        g.fillRect (meter);
        g.setColour (peak > 0.95f ? Colours::bass : colour);
        g.fillRect (meter.withTop (meter.getBottom() - meter.getHeight() * std::sqrt (peak)));
    }

private:
    BounceProcessor& processor;
    AudioPart audioPart;
    juce::String title;
    juce::Colour colour;
    LabeledKnob level, drive, tone, delay, reverb;
    PillToggle mute, solo;
};
} // namespace

MixerPanel::MixerPanel (BounceProcessor& p)
    : processor (p),
      internalSound (p.getState(), params::internalSound, "Sound", Colours::chords),
      midiOut (p.getState(), params::midiOut, "MIDI Out", Colours::melody),
      syncHost (p.getState(), params::syncHost, "Sync FL", Colours::good),
      dragAll ("Drag All", Colours::text, [this]
      {
          DragMidiButton::Payload payload;
          auto& s = processor.getSession();
          payload.clips = s.clipsForExport ("all");
          payload.bpm = s.tempo();
          payload.fileName = s.exportFileName ("all");
          return payload;
      })
{
    for (auto* c : std::initializer_list<juce::Component*> { &internalSound, &midiOut, &syncHost, &dragAll, &exportButton, &stylesFolderButton, &reloadStylesButton })
        addAndMakeVisible (c);

    strips.push_back (std::make_unique<Strip> (p, AudioPart::Chords, "KEYS", Colours::chords));
    strips.push_back (std::make_unique<Strip> (p, AudioPart::Bass, "808", Colours::bass));
    strips.push_back (std::make_unique<Strip> (p, AudioPart::Melody, "LEAD", Colours::melody));
    strips.push_back (std::make_unique<Strip> (p, AudioPart::Drums, "DRUMS", Colours::drums));
    for (auto& s : strips)
        addAndMakeVisible (*s);

    dragAll.setCompact (true);
    dragAll.setTooltip ("Every part as one multi-track MIDI file (drop on FL's Playlist)");
    syncHost.setTooltip ("Play along with FL's transport. Leave it off to drag notes into FL's Piano Roll without Bounce doubling them; use Play / the lane play buttons to listen inside Bounce.");
    midiOut.setTooltip ("Send all parts out of the plugin: chords on the MIDI channel, 808 +1, melody +2, counter +3, drums on 10");
    exportButton.setTooltip ("Write every part (and the arrangement) as .mid files into a folder");
    exportButton.onClick = [this] { exportToFolder(); };
    stylesFolderButton.onClick = [this] { openStylesFolder(); };
    reloadStylesButton.onClick = [this] { reloadStyles(); };
    startTimerHz (20);
}

void MixerPanel::exportToFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Export MIDI to folder", MidiExport::defaultExportFolder());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (folder == juce::File())
            return;
        folder.createDirectory();
        auto& s = processor.getSession();
        int written = 0;
        for (const auto* part : { "chords", "808", "melody", "counter", "drums", "all", "arrangement" })
        {
            const auto clips = s.clipsForExport (part);
            if (clips.empty() || clips.front().notes.empty())
                continue;
            written += MidiExport::writeFile (folder.getChildFile (s.exportFileName (part)), clips, s.tempo()) ? 1 : 0;
        }
        status = written > 0 ? "Saved " + juce::String (written) + " files to " + folder.getFileName() : "Couldn't write to " + folder.getFullPathName();
        repaint();
    });
}

void MixerPanel::openStylesFolder()
{
    auto& session = processor.getSession();
    const auto folder = StyleLibrary::userStyleFolder();
    folder.createDirectory();
    if (folder.findChildFiles (juce::File::findFiles, false, "*.json").isEmpty())
    {
        auto starter = session.currentStyle();
        starter.id = "my_style";
        starter.name = "My Style";
        starter.description = "Copy of " + session.currentStyle().name + ". Edit me, then press Reload.";
        folder.getChildFile ("my_style.json").replaceWithText (juce::String (starter.toJson().dump (2)));
    }
    folder.revealToUser();
}

void MixerPanel::reloadStyles()
{
    const auto warnings = processor.getSession().reloadStyles();
    if (! warnings.isEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Style presets", warnings.joinIntoString ("\n"));
    if (auto* parent = getParentComponent())
        parent->repaint();
}

void MixerPanel::resized()
{
    auto b = getLocalBounds().reduced (14);
    b.removeFromTop (28);
    auto pills = b.removeFromTop (26);
    const int pw = (pills.getWidth() - 12) / 3;
    internalSound.setBounds (pills.removeFromLeft (pw));
    pills.removeFromLeft (6);
    midiOut.setBounds (pills.removeFromLeft (pw));
    pills.removeFromLeft (6);
    syncHost.setBounds (pills);
    b.removeFromTop (8);

    auto bottom = b.removeFromBottom (28);
    b.removeFromBottom (status.isNotEmpty() ? 16 : 8);
    dragAll.setBounds (bottom.removeFromLeft (96));
    bottom.removeFromLeft (6);
    const int bw = (bottom.getWidth() - 12) / 3;
    exportButton.setBounds (bottom.removeFromLeft (bw));
    bottom.removeFromLeft (6);
    stylesFolderButton.setBounds (bottom.removeFromLeft (bw));
    bottom.removeFromLeft (6);
    reloadStylesButton.setBounds (bottom);

    const int sw = (b.getWidth() - 3 * 6) / 4;
    for (auto& s : strips)
    {
        s->setBounds (b.removeFromLeft (sw));
        b.removeFromLeft (6);
    }
}

void MixerPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat(), "Mix & Output", Colours::melody);
    if (status.isNotEmpty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (10.5f));
        g.drawFittedText (status, exportButton.getBounds().translated (0, -15).withHeight (13).withX (14).withRight (getWidth() - 14),
                          juce::Justification::centredLeft, 1);
    }
}

} // namespace bounce::ui
