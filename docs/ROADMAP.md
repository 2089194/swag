# Roadmap

## M1: skeleton, theory core, Chord Generator, drag-to-FL ✅
See [MILESTONE-1.md](MILESTONE-1.md).

## M2: 808/bass and melody, internal voices
- `core/gen/BassGenerator`: modes Root Follow, Syncopated Bounce, Octave Jumper, Glide Heavy and
  Sustain. Controls: density, glide, octave range, lock-to-kick, note length. Glides are written
  as overlapping notes (FL's slide convention) and also carry a per-note glide flag for the
  internal 808.
- `core/gen/MelodyGenerator`: motif → variation, with scale/chord-tone targeting, straight,
  swung or triplet feel, a catchiness/repetition control, and call-and-response. A
  counter-melody uses contrary motion and fills rhythmic gaps. It can lock or regenerate per bar.
- Engine: an 808 voice (sine + saturation + pitch envelope, glide, drive, LPF) and a lead voice
  (bell, pluck, flute). The keys voice gets a lo-fi/tape-wobble option. Each part gets volume,
  mute/solo and saturation, reverb, delay and LPF.
- Multi-part drag ("All") as format 1.

## M3: drums
- Lanes: kick, snare/clap, closed hat, open hat, perc, rim, FX. Hat rolls in 1/16, 1/16T, 1/32
  and 1/32T with pitch ramps and velocity curves. Swing, per-lane humanise, and a "bounce" nudge.
- GM / FPC / custom note maps.
- A bundled kit. **Proposal:** synthesise the bundled one-shots ourselves (808-style kick, clap,
  hats, rim and percs, rendered from our own DSP), so we know exactly where every sample came from
  and there are no licensing questions. Users can still load their own samples.

## M4: Key & BPM Lab
- Decode WAV, AIFF, FLAC and MP3 with JUCE's built-in readers (`JUCE_USE_MP3AUDIOFORMAT`, the
  MP3 patents have expired), or capture audio from the plugin input.
- BPM: an onset-strength envelope (spectral flux) feeds autocorrelation and a comb-filter tempo
  map, with a half/double-time disambiguation biased to 130–170.
- Key: a chromagram (constant-Q-style folding of `juce::dsp::FFT` bins) is matched against the
  Krumhansl–Schmuckler and Temperley profiles. The UI shows the top 3 candidates with confidence
  and the relative major/minor.
- Chords: beat-synchronous chroma, matched against chord templates and smoothed with an HMM /
  Viterbi pass, shown as an editable lane. "Send to Chord Generator" turns them into locked slots.
- A pitch/speed helper shows the resulting key and BPM for ±N semitones or ±%.
- All of it runs on a background `juce::Thread` with progress, and never on the audio thread.
- **No new dependencies planned.** Stem separation (Demucs/Spleeter via ONNX Runtime) would add
  roughly 50–200 MB of model and runtime. It's out of scope unless you ask for it, and we'll
  check with you first as requested.

## M5: polish and the FL pass
- Note-editable mini piano rolls (a manual-override layer above the generated notes).
- Per-slot chord durations.
- The Arrangement Sketcher (stretch goal): Intro/Hook/Verse/Bridge/Outro sections exported as
  MIDI plus automation markers.
- Whole-plugin user presets, a preset browser, and 60 fps animation polish.
- The FL Studio 2026 test pass (checklist in MILESTONE-1.md), plus pluginval on all formats.
