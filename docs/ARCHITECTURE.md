# Architecture

```
┌────────────────────────────── plugin (JUCE) ──────────────────────────────┐
│                                                                           │
│  UI (message thread, 60 fps vblank)       Session (message thread)        │
│  TopBar · ChordWheel · ChordStrip   ───►  progression, undo/ideas, style, │
│  PianoRollPreview · DragMidiButton  ◄───  param polling, render + publish │
│              │  reads atomics                    │ TripleBuffer           │
│              ▼                                   ▼ (lock-free)            │
│  BounceProcessor (audio thread): PatternPlayer ─► KeysSynth ─► audio out  │
│                                         └──────────────────► MIDI out     │
└───────────────────────────────────────────────────────────────────────────┘
                 ▲ uses (pure functions, no JUCE)
┌──────────────────────────── core (bounce_core) ───────────────────────────┐
│ theory/  Pitch · Scale/Key · Chord (+ parse, roman numerals) · Voicing    │
│ gen/     StylePreset · ChordGenerator · ChordRenderer · Progression ·     │
│          IdeaHistory                                                      │
│ midi/    MidiClip · SMF writer/reader · file naming                       │
│ util/    Random (PCG32) · Json · TripleBuffer                             │
└───────────────────────────────────────────────────────────────────────────┘
```

The spec's four modules map onto the code like this:

| Spec module  | Where                                                     |
|--------------|-----------------------------------------------------------|
| `Generators` | `core/` (pure, JUCE-free, unit tested)                    |
| `Engine`     | `plugin/Source/Engine/` (real-time DSP + MIDI playback)   |
| `UI`         | `plugin/Source/UI/`                                       |
| `Analysis`   | `core/analysis/` + `plugin/Source/Analysis/` (milestone 4)|

## Key decisions

### 1. A pure core

All of the musical logic lives in `bounce_core`. It's plain C++20 with no JUCE, and it does no
I/O and starts no threads. This gives us:

- fast, deterministic unit tests (about 114k assertions in under 0.2 s) that run on every OS in CI;
- the option to reuse the generators elsewhere later (a CLI, a web demo, or batch-testing styles);
- a hard boundary that stops UI or audio-thread concerns from leaking into the theory code.

The core has its own small JSON parser (`util/Json`), so style presets can be parsed and tested
without JUCE. It accepts `//` comments and trailing commas, because the presets are meant to
be edited by hand.

### 2. Determinism

`util::Random` is a PCG32 generator with its own uniform, weighted and Gaussian-ish sampling. The
`<random>` distributions aren't used because their output differs between MSVC, libc++ and
libstdc++. With this choice, a seed saved on Windows reproduces the same idea on a Mac.

Each chord slot draws from its own sub-stream (`deriveSeed(seed, slot)`). Locking one chord
therefore doesn't reshuffle the random choices made for the others. Humanisation also has its
own streams, so changing the swing doesn't change which chords you get.

### 3. The harmony engine

`ChordGenerator` is a weighted Markov chain over **harmony functions**. A function is a roman
numeral relative to the tonic, measured against the major scale (`i`, `bVI`, `bVII`, `iv`, `V`).
Major- and minor-tonic modes have separate tables. For each slot:

```
weight(fn) = transition(prev → fn)                 // or start weight for slot 0
           × soft(transition(fn → next))           // only when the next chord is locked
           × soft(transition(last → fn))           // slot 0: the loop seam
           × exp(mood × family_sign × 0.8)         // dark favours minor, bright favours major
           × borrowed_amount × 1.6                 // only for non-diatonic functions
           × variety penalties                     // no immediate repeats, fewer re-uses
```

The chord **quality** is then picked from the colours allowed for that function. A diatonic
function only gets tones from the scale. A borrowed function gets tones from the parallel
major/minor scale. Each candidate quality is weighted by the preset's `colours` and by a bell
curve around the complexity target (3 tones at complexity 0, up to 5–6 at 1). This is
why the output stays in key, and the tests check this property for every mode and seed.

Turning Complexity, Mood or Borrowed regenerates **with the same seed**, so a knob re-colours
the current idea instead of rolling a new one. Generate and the dice are the only controls
that pick new seeds.

### 4. Voicing and voice leading

`voiceChord` splits a chord into a left hand and a right-hand upper structure, according to the
style (for example, Spread puts root + 5th in the left hand). It then enumerates every
inversion × octave of the upper structure inside the register. With a previous voicing, it picks the
candidate with the smallest movement plus a top-voice penalty. Without one, it picks the candidate
nearest the middle of the register. `voiceProgression` runs twice, so chord 1 is voice-led
out of the last chord and the loop seam stays smooth. A forced inversion (from the ↑/↓ arrows)
pins the rotation.

### 5. Threading and real-time safety

- **Message thread.** `Session` owns every bit of non-parameter state. Host parameters are read
  by a 30 Hz timer that **polls** the APVTS atomics, so we never react on the audio thread to
  automation. After every change it re-renders the clip and publishes it.
- **Hand-over.** `TripleBuffer<PlaybackPattern>` is a lock-free single-producer/single-consumer
  "latest value" exchange. `PlaybackPattern` has a fixed capacity (1024 notes), so publishing
  never allocates on the audio side and the reader never sees a half-written loop.
- **Audio thread.** `PatternPlayer` turns the loop into sample-accurate MIDI against the host's
  PPQ position, so the loop always starts on the bar. It chases notes on start, jumps and
  pattern swaps (it diffs held vs. should-hold, so unchanged notes aren't retriggered) and
  flushes everything on stop. `KeysSynth` is a custom voice allocator (not
  `juce::Synthesiser`, whose render path takes a lock). Nothing on this path allocates, locks
  or does I/O. The `MidiBuffer` is pre-sized in `prepareToPlay`.
- **UI.** It reads `getLoopPosition()`/`getHostBpm()` atomics and repaints from a
  `VBlankAttachment`, at 60 fps and without touching the audio thread.
- **State.** Hosts may call `getStateInformation` from any thread, so `Session` keeps a
  snapshot that's refreshed on the message thread after every edit, guarded by a spin lock that
  is never touched by the audio thread. `setStateInformation` hops to the message thread if
  needed.

### 6. Getting MIDI into FL Studio

- **Drag.** `DragMidiButton` writes `<temp>/Bounce/<name>.mid` and calls
  `performExternalDragDropOfFiles`. One part is written as SMF format 0, which is what FL's
  Piano Roll handles best. Several parts are written as format 1, one named track per part,
  which the Playlist splits into channels.
- **Writer guarantees.** Note-offs come before note-ons on the same tick. Same-key overlaps are
  trimmed, so on/off pairs always match. Notes are clipped to the loop, and every track is
  padded to the full loop length so it lines up when dropped.
- **MIDI out.** The plugin is built with `NEEDS_MIDI_OUTPUT`. The generated events, plus anything
  played in, are emitted every block.

### 7. UI scaling

The editor lays everything out at a fixed design size (1120×720) inside one `Content`
component and scales it with an `AffineTransform`. Every graphic is a vector path, so
it stays crisp from 75% to 200%. The chosen size is saved in the plugin state.

## Testing

| Suite                 | What it covers                                                               |
|-----------------------|------------------------------------------------------------------------------|
| `bounce_tests`        | scales/modes, chord spelling + parsing for all 25 qualities × 12 roots, diatonic stacking, roman numerals, voicing constraints, voice leading, generator determinism, staying in key, borrowed-chord sources, locks, reharmonise, transpose, complexity/mood behaviour, renderer timing/rhythms, SMF round trip, file naming, JSON, every bundled preset, RNG, triple buffer under contention |
| `bounce_engine_tests` | the real-time `PatternPlayer`: bar-aligned loop wrap (±1 sample), note on/off pairing, chase on start/jump, pattern swap without retrigger, re-struck notes, flush on stop, preview clock |
| pluginval (CI)        | strictness 10: state round trips, automation, threading, fuzzing, editor     |
