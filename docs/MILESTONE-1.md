# Milestone 1: skeleton, theory core, Chord Generator, drag-to-FL

## Delivered

| Spec item                                   | Status | Where |
|---------------------------------------------|--------|-------|
| CMake + JUCE 8 project, VST3/AU/Standalone  | ✅ | `CMakeLists.txt`, `plugin/CMakeLists.txt` |
| `core` split from JUCE (Generators)         | ✅ | `core/` |
| Music theory: scales/modes, chord spelling, parsing, roman numerals, voicing, voice leading | ✅ | `core/include/bounce/theory/` |
| Style presets as editable JSON (5 styles), user folder + reload | ✅ | `presets/styles/`, `StyleLibrary` |
| Chord generator: key, mode, style, bars 2/4/8, rhythm, complexity, mood | ✅ | `ChordGenerator`, `ChordRenderer` |
| Lock + regenerate, invert/revoice, reharmonise one chord, transpose, humanise | ✅ | `Session`, `ChordStrip`, wheel menu |
| Chord names + roman numerals                | ✅ | everywhere |
| Drag-and-drop MIDI into FL                  | ✅ | `DragMidiButton`, `MidiExport` |
| MIDI output from the plugin                 | ✅ | `NEEDS_MIDI_OUTPUT`, `PatternPlayer` |
| Export to folder, key/BPM file names        | ✅ | `makeMidiFileName` |
| Host tempo/transport sync, starts on the bar | ✅ | `PatternPlayer` (+ tests) |
| Seeds, undo/redo, 50-idea history, full state recall | ✅ | `IdeaHistory`, `Session` |
| Chord wheel, chord cards, mini piano roll, arc knobs, pill toggles, 75–200% scaling | ✅ | `plugin/Source/UI/` |
| Unit tests: theory + generators (in key, locks, seed determinism) | ✅ | `tests/` |
| pluginval                                   | ✅ Linux, strictness 10 (Windows/macOS run in CI) |

Beyond the brief, I added a minimal built-in keys voice so ideas can be heard right away,
real-time engine tests, and a CI workflow for all three OSes.

## Design decisions (summary)

The full write-up is in [ARCHITECTURE.md](ARCHITECTURE.md).

1. **Pure core.** All the musical logic is JUCE-free C++20, which makes it fast to test and
   portable.
2. **Our own PCG32 RNG.** `<random>` distributions differ between standard libraries, and a seed
   must mean the same idea on Windows and macOS.
3. **Per-slot random streams.** Locking a chord doesn't reshuffle the other chords' choices.
4. **Roman numerals are relative to major in every mode**, so A minor's F chord is `bVI`. This is
   how producers talk, and it lets one token set describe both major and minor tables.
5. **Knobs re-colour, the Generate button rolls.** Complexity, mood and borrowed regenerate with
   the *same* seed. Only Generate and the dice pick a new seed.
6. **Borrowed chords take their colours from the parallel scale.** With *Borrowed* at 0 the output
   is provably diatonic, and a test checks this for every mode.
7. **Format 0 for single-part drags.** It's the most reliable format for FL's Piano Roll. Format 1
   is reserved for multi-part drops.
8. **Parameter polling instead of listeners**, so host automation never triggers work on the audio
   thread.
9. **A lock-free triple buffer with fixed-size patterns** between the message and audio threads.
10. **A custom synth voice allocator.** `juce::Synthesiser` takes a lock while rendering.

## Known limitations

- **Not yet tested inside FL Studio 2026.** This environment is Linux. I verified the VST3 with
  pluginval (strictness 10, in-process) and checked the standalone UI under Xvfb. The Windows and
  macOS builds and their pluginval runs are set up in CI but **haven't run yet**. The first
  push will show whether they pass. The FL checklist is in the testing section below.
- **The mini piano roll doesn't edit notes yet.** A click selects the chord and a right-click
  opens the chord menu. Editing individual notes needs a "manual override" layer on top of the
  generated notes, which is planned for milestone 5.
- **Chords split the loop evenly.** There are 1–8 chords per loop, on a half-beat grid. Mixed
  durations within one loop (one chord for a bar, then two chords in the next) aren't supported
  yet. They'd need a per-slot length.
- **Only the chord part exists**, so *Export* and drag write chords only. The format 1 "all parts"
  path is in place for M2/M3.
- **The built-in sound is a placeholder.** It's one keys voice with chorus and reverb. The 808,
  lead and drum sampler engines and per-part FX come in M2/M3.
- **Enharmonic spelling is per key, not per chord.** In D♭ major the bVI chord is spelled `A`
  rather than `B♭♭`. Producers are fine with this, but it isn't textbook spelling.
- **Tempo view (½× / 2×) only changes the display.** The loop always follows the host's actual
  tempo.
- **AU is built but hasn't been validated** (no Mac here). CI validates the VST3 on macOS.
- **FL's MIDI out needs port setup.** VST3 MIDI output reaches other plugins in FL through the
  wrapper's output port. See the README.
- **External drag on Linux.** It depends on the desktop. FL Studio doesn't run there natively, so
  this doesn't matter for the target.

## Build & run

See the [README](../README.md#building). In short:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Artefacts are written to `build/plugin/Bounce_artefacts/Release/{VST3,AU,Standalone}`.

## FL Studio test checklist (for the M5 pass, or try it now)

1. Load the plugin as an instrument → the UI opens at 100%. Resizing from the corner keeps the
   aspect ratio, and the size is remembered.
2. Press play → the loop starts on bar 1, the chord wheel and cards follow the playhead, and
   looping is seamless.
3. Drag the MIDI tile → Piano Roll: notes start at bar 1, the loop length is correct, and the
   file name shows the chords, key and BPM.
4. Drag the MIDI tile → Playlist: a pattern is created.
5. MIDI out: set the wrapper output port to 1, set FLEX/Sytrus input port to 1, turn off Sound
   in Bounce → FLEX plays the chords.
6. Save the project, close FL, reopen → the same progression, locks, style, history and UI size
   come back.
7. Automate Complexity → the chords re-colour after the debounce, with no clicks or stuck notes.
8. Stop/start, loop a range, and jump the playhead → no stuck notes.
