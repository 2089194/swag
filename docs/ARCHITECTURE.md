# Architecture

```
┌─────────────────────────────────── plugin (JUCE) ────────────────────────────────────┐
│ UI (message thread, 60 fps vblank)            Session (message thread)               │
│  TopBar · ModulePanel tabs · ChordWheel  ──►  Idea (chords + seeds + locks + edits),  │
│  MixerPanel · ChordStrip · PartLanes     ◄──  undo/history, style, arrangement,       │
│  LabView                                      param polling → render all parts        │
│      │ reads atomics                                 │ 6 × TripleBuffer (lock-free)   │
│      ▼                                               ▼                                │
│ BounceProcessor (audio thread)                                                        │
│  6 × PatternPlayer ─► Keys / 808 / Lead / DrumSampler ─► Mixer (drive, LPF, sends) ─► │
│        └────────────────────────────────────────────────► MIDI out (all parts)        │
│ LabModel (background thread): decode → analysis::analyse → posted to message thread  │
└───────────────────────────────────────────────────────────────────────────────────────┘
                     ▲ pure functions, no JUCE
┌──────────────────────────────────── core (bounce_core) ──────────────────────────────┐
│ theory/    Pitch · Scale/Key · Chord (+parse, roman) · Voicing                        │
│ gen/       Harmony · StylePreset · ChordGenerator · ChordRenderer · Progression ·     │
│            BassGenerator · MelodyGenerator · DrumGenerator · Groove · Idea ·          │
│            IdeaHistory · Arrangement                                                  │
│ analysis/  Fft · AudioAnalysis (tempo, key, chords, downbeat, speed helper)           │
│ midi/      MidiClip (notes, CCs, markers) · SMF writer/reader · file naming           │
│ util/      Random (PCG32) · Json · TripleBuffer                                       │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

| Spec module  | Where |
|--------------|-------|
| `Generators` | `core/gen/` (pure, unit tested) |
| `Analysis`   | `core/analysis/` (pure, tested on synthesised audio) + `plugin/Source/Analysis/LabModel` (threading) |
| `Engine`     | `plugin/Source/Engine/` (PatternPlayer, KeysSynth, Voices, DrumSampler, Mixer) |
| `UI`         | `plugin/Source/UI/` |

## Data flow

1. **An `Idea`** holds the chord `Progression` (with per-chord locks, voicing edits and optional
   custom lengths), one seed per part, the melody's locked bars, and per-part hand edits.
   Undo, redo and the 50-idea history store whole Ideas.
2. **Rendering** happens in `Session::render()` on the message thread, and it is deterministic:
   - chords → `renderChords`
   - drums → `generateDrums`
   - 808 → `generateBass`, locked to `drums.kickTimes()`
   - melody → `generateMelody`, with locked bars
   - counter → `generateCounterMelody` against the melody
   - hand edits are applied on top
   - drums are rendered twice: mapped for MIDI out, and encoded for the internal sampler
3. **Each rendered clip** is copied into a fixed-capacity `PlaybackPattern` and published
   through a `TripleBuffer`. The audio thread picks up the newest one at the start of each
   block.
4. **Parameter changes are polled** at 30 Hz.
   - Chord harmony params (complexity, mood, borrowed, scale, bars, chord count) regenerate
     the chords with the same seed after a 200 ms debounce.
   - Key changes transpose the idea.
   - Everything else that shapes notes triggers a re-render.
   - Audio-only params (mixer, voice settings) are read directly by the audio thread.

## Determinism

`util::Random` is PCG32 with its own uniform, weighted and Gaussian-ish sampling, rather than
`<random>`, whose distributions differ between standard libraries. Every generator draws from
`deriveSeed(seed, stream)` sub-streams per slot, bar or lane. This keeps locking or regenerating
one element from reshuffling the others, and keeps swing/humanise from changing which notes
you get.

## Harmony engine

`ChordGenerator` first tries the style's **progression templates** (`harmony.majorProgressions` /
`minorProgressions`: loop shapes like `i bVI bIII bVII` with weights). A template is fitted
cyclically to the chord count and rejected if it breaks a lock, needs borrowed chords while
Borrowed is 0, or repeats a chord back to back. Templates are weighted by exact-length fit, mood
and the borrowed setting, and a function that recurs in a loop reuses the same chord.

When no template fits (or for reharmonising), a weighted Markov chain over roman-numeral
**functions** takes over, with separate tables for major- and minor-tonic modes. Each
candidate's weight is built like this:

```
weight(fn) = transition(prev → fn)             // or the start weight for slot 0
           × soft(transition(fn → next))       // only when the next chord is locked
           × soft(transition(last → fn))       // loop seam
           × exp(mood × family_sign × 0.8)
           × borrowed × 1.6                     // non-diatonic functions only
           × variety penalties
```

A weight of 0 in the preset forbids a move outright. The chord quality is chosen from colours
that stay inside the scale (diatonic) or the parallel scale (borrowed), weighted by the preset
(squared, so favourite colours clearly win) and by a bell curve around the complexity setting.

Voicing tries every octave placement of the chord tones in the register, not just close stacks.
It scores voice-leading distance, top-note motion, distance from the register centre and a
**clash penalty**: minor 2nds and minor 9ths between voices, and close intervals low down. So
Amaj9 comes out as A C# G# B rather than the cluster G# A B C#.

## Generators in brief

- **808.** One curated bar rhythm per loop (by mode, leaning busier with density) is repeated
  every bar; with lock to kick, the kick's hits are used instead. Every note is the chord's bass
  note. The octave of each root is chosen over the whole loop (all 2^n options) to minimise
  motion, seam included. Octave pops sit at fixed rhythm positions, and glides go into chosen
  chord changes (the turnaround first) and, with high glide, into the pops.
  Glides overlap the next note by a 32nd, and the clip starts with CC65/CC5 portamento hints.
- **Melody.** It works from pentatonic notes (unless the pentatonic control is low), a one-bar
  rhythm cell from a curated list per density and feel, and a motif contour in pool steps that
  never wanders more than three steps. Every bar re-anchors the motif on a chord tone near the
  home register, so the phrase follows the harmony. Phrases are A B A B' (call & response, B
  mirrors the contour) or A A A A'. Beats land on chord tones, and phrase ends resolve to the
  chord root, or the tonic at the loop end.
- **Counter-melody.** It places notes on an 8th grid where the melody rests. Candidates are
  weighted by step size, chord tones and contrary motion; 2nds, 7ths and tritones against
  the sounding melody note are rejected. Each note is cut at the next melody onset.
- **Drums.** It works per bar and per lane: kick A/B patterns, a clap phrase (style
  `snarePatterns` over 2 bars, else half-time or 2 & 4; kicks yield to claps), hats with
  rolls and stutters, open hats, percs and rims. Swing, then bounce, then humanise are
  applied, with the loop downbeat fixed at 0.

## Analysis

The input is downmixed to mono and decimated to about 22 kHz.

- **Onsets.** A log-magnitude spectral flux (1024/256 frames) is detrended so only onsets
  remain.
- **Tempo.** Autocorrelation at fractional lags over 60–200 BPM uses the sum of 1×, 2× and 4×
  the beat period, times a log-normal prior at 140 BPM. A 0.01 BPM refinement then adds 8×.
  The beat phase is the offset that collects the most onset energy.
- **Chroma.** 8192/2048 frames, 60 Hz–4.2 kHz, with triangular in-tune weighting.
- **Key.** The average of the Pearson correlations with the Krumhansl-Kessler and Temperley
  profiles, over 24 keys. Confidence comes from a softmax.
- **Chords.** Per-beat chroma is matched against templates by cosine similarity, plus a
  diatonic bonus and a penalty for 7ths. A Viterbi pass uses a constant switching penalty, and
  runs of the same state are merged into segments.
- **Downbeat.** The beat phase (of four) where the most chord changes fall on bar lines.

`progressionFromDetected` snaps segments to the half-beat grid inside the chosen window. It
drops slivers under one beat, merges down to 8 chords, and returns locked slots with custom
lengths.

## Real-time safety

- No allocation, locks or I/O on the audio thread. Every buffer, including the 60 s capture
  ring and the MIDI buffers, is sized in `prepareToPlay`.
- Parameter atomics are cached at construction (no name lookups per block).
- `PatternPlayer` is sample-accurate against the host PPQ, so loops start on the bar. It
  chases notes on start, jumps and pattern swaps (only notes that actually changed are
  restarted) and flushes everything on stop.
- `KeysSynth`, `LeadSynth`, `Bass808` and `DrumSampler` use their own voice allocation.
  `juce::Synthesiser` takes a lock while rendering, so it isn't used.
- Custom drum samples are swapped through atomics, with a timed release pool on the message
  thread.
- `getStateInformation` returns a snapshot refreshed after every edit, guarded by a spin lock
  that the audio thread never touches. `setStateInformation` hops to the message thread.

## UI

Everything is laid out at a fixed 1360×860 design size and scaled with an `AffineTransform`,
and every graphic is a vector path, so it stays crisp from 60% to 200%. The window opens at
the project's saved size, else the last size used anywhere (`%APPDATA%\Bounce\Bounce.settings`),
else `defaultWidthForScreen()`: about 62% of the screen height, capped at 80%.
Animation runs from a `VBlankAttachment` that reads atomics.

## Tests

| Suite | Covers |
|---|---|
| `bounce_tests` (85 cases) | Theory, voicing, the chord generator (in key, locks, determinism, borrowed sources, custom lengths), renderer, 808 (roots, register, lock to kick, glides), melody (in key, chord tones on strong beats, monophonic, density, repetition, bar locks), counter-melody (no clashes, fills gaps), drums (backbone, roll rates/curves/pitch, maps, style JSON), ideas/edits/arrangement, analysis of synthesised audio (BPM within 0.2%, key, chords, downbeat, detected → progression), speed helper, MIDI files with CCs and markers, JSON, presets, RNG, triple buffer |
| `bounce_engine_tests` (15 cases) | Pattern player (bar-aligned wrap, pairing, chase, swap, jumps, preview), 808 glide/release, lead types, keys wobble, generated chords played through the e-piano and pastel pad and recognised by the Lab's chord detector, synthesised kit, sampler hot-swap, mixer mute/tails |
| pluginval (CI) | Strictness 10 on Windows x64 |
