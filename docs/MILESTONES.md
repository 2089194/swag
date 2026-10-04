# Milestone reports

Each section lists what was delivered, the design decisions behind it, and its known
limitations. Build and run instructions are in the [README](../README.md#building). The
architecture is described in [ARCHITECTURE.md](ARCHITECTURE.md).

**Verification status (all milestones).** The core has 84 test cases (~150k assertions) and the
engine has 14 test cases (~95k assertions). Both pass, and the core is also clean under
AddressSanitizer and UBSan. pluginval passes at strictness 10 on **Windows x64 in CI**, where the core and engine tests
also pass. I checked the UI through scripted screenshots of the standalone app, including a
real Lab analysis of a WAV file and the hand-off to the generator. The target is Windows 64-bit
only.
**I haven't run it inside FL Studio**: that checklist is at the end.

---

## M1: skeleton, theory core, Chord Generator, drag-to-FL

**Delivered**
- A CMake + JUCE 8 project (VST3 + Standalone, Windows x64) with a pure-C++ `core`.
- Music theory: 10 scales/modes, 25 chord qualities (spelling, parsing, roman numerals),
  diatonic stacking, 5 voicing styles with voice leading.
- The chord generator: a weighted Markov chain on functional harmony, with locks, reharmonise,
  transpose, invert/revoice and humanise.
- Five JSON style presets, plus a user folder with reload.
- MIDI out: drag-and-drop, plugin MIDI output, and export to a folder with key/BPM file names.
- Host-synced, bar-aligned playback.
- Seeds, undo, a 50-idea history and full state recall.

**Decisions**
- **Pure core.** All of the musical logic lives in a JUCE-free library, so it's fast to test,
  portable and kept separate from the plugin.
- **Our own PCG32 RNG.** `<random>` distributions differ between standard libraries, and a
  seed must give the same idea on every OS.
- **Per-slot random streams.** Locking one chord doesn't reshuffle the others.
- **Roman numerals measured against the major scale in every mode**, so A minor's F chord is
  `bVI`.
- **Knobs re-colour, Generate rolls.** Complexity, mood and borrowed regenerate with the
  same seed.
- **Single-part drags are written as SMF format 0**, which FL's Piano Roll handles best.

---

## M2: 808/bass, melody, internal voices

**Delivered**
- **808 generator** with five modes (Root Follow, Syncopated Bounce, Octave Jumper, Glide Heavy,
  Sustain) and controls for density, glide, octave range, note length, register and lock-to-kick.
  It follows slash-chord bass notes.
- **Melody generator.** It builds a motif rhythm and contour, then varies it with repetition
  and catchiness. It also offers call & response (odd bars answer and resolve downward),
  straight/swung/triplet feel and a pentatonic preference. Strong beats land on chord tones,
  every note is in key, and bars can be locked one at a time.
- **Counter-melody.** It only starts notes where the melody rests, moves in contrary motion,
  rejects minor 2nds, major 7ths and tritones against sounding melody notes, and is cut when
  a new melody note begins.
- **Voices.**
  - A mono 808: sine with saturation, a pitch "punch" envelope, decay, and legato glide on
    overlapping notes.
  - A lead with Bell (2-operator FM), Pluck (filtered saw) and Flute (sine, breath, vibrato)
    sounds.
  - Keys with a tape-wobble/lo-fi option (wow, flutter, darker tone, hiss).
- **Mixer.** Each part has level, mute/solo, drive, low-pass, and delay and reverb sends. The
  delay and reverb are shared buses, and the delay is a tempo-synced dotted 8th.

**Decisions**
- **Glides are written as overlapping notes plus CC65 (portamento on) and CC5 (portamento
  time).** FL's 808 presets and most samplers slide on overlaps, and the internal 808 does too.
  The same MIDI therefore works everywhere.
- **The 808 is generated after the drums** so it can lock to the actual kick times, including
  swing and bounce.
- **Every part has its own seed inside an `Idea`.** Re-rolling one part leaves the others
  untouched, and undo and history store the whole idea.
- **The melody locks a bar by baking it.** The bar's current notes, including hand edits, are
  stored in the idea, so later chord changes or re-rolls don't touch it.

**Known limitations**
- The voices are deliberately lightweight: no wavetables, unison stacks or sample-based keys.
- The counter-melody shares the lead voice and the melody's mixer strip.
- Locked melody bars aren't re-fitted when you change the chords under them. That's by design,
  since they're "frozen".

---

## M3: drums / bounce

**Delivered**
- Lanes: kick, snare/clap, closed hat, open hat, perc, rim and FX.
- **Kicks** use an A/B pattern pair across 2-bar phrases, with density-driven extra and dropped
  hits and pickups at the loop end. The snare/clap sits on beat 3 (half-time), with ghost notes.
- **Hats** have rolls in 1/16, 1/16T, 1/32 and 1/32T (only the rates you enable), with Up, Down,
  Flat or Wave velocity curves and **pitch ramps** of ±12 semitones. Triplet stutters and open
  hats fall on off-beats.
- Percs and rim fills are included.
- Feel controls are swing, per-hit humanise, and **bounce**, which pushes non-downbeat kicks
  and percs off-grid. The loop downbeat always stays exactly on the bar.
- Note maps: GM/FPC (36/38/42/46/63/37/49) or custom notes per lane. You can choose whether
  pitched rolls become note numbers.
- **A built-in kit synthesised at startup**: 808-style kick, clap, metallic hats, conga, rim and
  crash, plus loading your own samples per lane (right-click a lane name).

**Decisions**
- **Two drum streams.** MIDI out uses your note map. The internal sampler gets its own encoding
  (lane + pitch offset), so pitched rolls always play correctly inside Bounce whatever map you
  choose.
- **No third-party samples.** Synthesising the kit means there are no licensing questions, as I
  proposed at M1.
- **Swapping samples without locks.** The audio thread reads atomics, and replaced samples are
  kept in a release pool for 30 s before they're freed, so the audio thread never frees memory.
- **Drum-pattern vocabulary comes from the style preset** (`drums.kickPatterns`,
  `percPatterns`, `openHatSteps`, `hats16ths`, `halfTime`).

**Known limitations**
- The step grid edits on a 1/16 grid. Roll hits can be deleted but not drawn at 1/32.
- The GM/FPC map follows the GM standard. If your FPC kit is laid out differently, use
  *Custom notes*.
- Custom samples are referenced by path. If you move a file, the lane falls back to the
  built-in sound.

---

## M4: Key & BPM Lab

**Delivered**
- **Input.** Load WAV, AIFF, FLAC, OGG or MP3 (the first 10 minutes), by dragging a file onto
  the Lab or with *Open audio*. You can also **capture the plugin input** (up to 60 s, from a
  buffer preallocated in `prepareToPlay`).
- **Tempo.** A spectral-flux onset envelope goes through autocorrelation over 60–200 BPM with a
  log-normal prior centred at 140. It's then refined on a 0.01 BPM grid using autocorrelation
  at 1×, 2×, 4× and 8× the beat period. The result is shown with ½× and 2× alternatives and
  a confidence value.
- **Key.** The chromagram is correlated against both the Krumhansl-Kessler and Temperley
  profiles. You get the top 3 keys with confidence plus a relative major/minor choice.
- **Chords.** Beat-synchronous chroma is matched against triad and 7th-chord templates, with a
  key-aware bias and **Viterbi smoothing**, and shown as a clickable, editable lane. You can
  change a chord's root or quality, or merge it into the previous chord.
- **Downbeat.** Of the four possible beat phases, it picks the one where most chord changes
  land on a bar line.
- **Send to Chord Generator.** It takes a start bar and a length, snaps the chords to the beat
  grid, keeps their real lengths (custom chord lengths) and locks them. You then vary them by
  unlocking chords and pressing Generate, or by reharmonising.
- **Flip helper.** It shows the resulting key (with cents) and BPM after varispeed by ±12
  semitones, plus the speed % that matches the project tempo. Sending uses the shifted key.
- Analysis runs on a background thread with a progress bar and cancel, and never on the audio
  thread.

**Decisions**
- **No new dependencies.** The FFT, onset detection, key profiles and HMM are written in the
  core (about 600 lines) and tested on synthesised audio. Decoding uses JUCE's built-in
  readers; the MP3 patents have expired. **Stem separation was not added**, as you asked to be
  consulted first.
- **Chords are detected per beat, not per frame**, which makes them far more stable on real
  music. The Viterbi switching penalty stops them flickering.

**Known limitations**
- Accuracy was verified on **synthesised** audio: 140 BPM to within 0.2%, the correct key, and
  over 80% of beat-chords correct. On full mixes with heavy 808s and vocals, expect more chord
  errors and some half/double-time choices. That's why the BPM toggles and the click-to-fix
  chord lane exist. I haven't benchmarked it on a labelled dataset.
- Only major/minor keys are reported. Modes are left to you.
- Chord templates cover triads, maj7, m7 and 7. The richer colours come back when you
  reharmonise or regenerate with complexity.
- Capture needs the host to feed audio into the instrument's input bus. In FL, that means
  routing audio to the plugin's input or sidechain; loading files is the main workflow.
- Lab results aren't saved with the project: only the chords you send are saved.

---

## M5: polish, presets, state, FL pass

**Delivered**
- A **larger UI** with a 1360×860 design size. It opens at the largest size that fits the screen
  (since the musicality pass: a moderate default), resizes with a fixed aspect ratio, remembers the size, and is
  all vector graphics at 60 fps.
- **Module tabs** (Chords, 808, Melody, Drums, Arrange), each with its own accent colour.
- A **4-strip mixer** with peak meters.
- **Part lanes** with a re-roll dice per part, clear-edits, a drag tile, and **click-to-edit
  mini piano rolls** (click deletes, double-click adds). Your edits survive knob tweaks.
- **Per-chord lengths** (longer/shorter from the chord menu, or reset to even).
- **Arrangement Sketcher:** sections with per-part toggles, drops, filter sweeps and markers,
  exported via *Drag Arrangement* or Export.
- Every style preset now also sets 808, melody and drum defaults.
- State keeps parameters, the idea (chords, seeds, locks, edits), history, arrangement, UI
  size/tab/view and custom sample paths. **Milestone-1 states still load.**

**Decisions**
- **Edits are layered on generated notes, not baked into them.** That keeps knobs live after
  you edit. Re-rolling a part clears its edits on purpose.
- **Parameter atomics are cached** at construction. The audio thread never looks parameters
  up by name, because building those strings would allocate.

**Known limitations**
- There are no **whole-plugin user presets** beyond the host's own preset system. FL's preset
  save/load stores the full state, and the 50-idea history covers recall inside a session.
- Note editing is add/delete only: no dragging or resizing notes yet.

---

## Musicality and playback pass (after M5, from FL Studio feedback)

**Problems reported:** chords didn't sound like chords, the 808 sounded random, the melody had
no sense of phrase, Bounce doubled the notes dragged into FL whenever FL played, and the window
opened full screen.

**Delivered**
- **Chords.** Loops now come from per-style progression templates, the loop shapes this style
  uses, such as i–bVI–bIII–bVII, bVI–iv–i–v and IV–V–iii–vi. Colours lean on maj7, m7, m9 and
  add9 (6/9, #11 and sus colours are rare). Voicings are searched across octaves with a clash
  penalty, so no minor 2nds or minor 9ths between voices.
- **808.** Roots only (no stray 5ths), in the octave that keeps the line smooth over the whole
  loop. One programmed bar is repeated, with octave pops at fixed spots and glides into chosen
  chord changes.
- **Melody.** A pentatonic motif and a one-bar rhythm cell, re-anchored on a chord tone each bar,
  in A B A B' or A A A A' phrases that resolve to the root and end on the tonic.
- **Keys** are an FM electric piano, so every chord tone is distinct.
- **Playback.** Bounce no longer follows FL's transport by default. **▶** in the top bar loops
  the idea inside Bounce, **▶ on each lane** auditions one part, and **Sync FL** restores
  following FL. A reopened project never starts playing.
- **Window.** Opens at about 60% of the screen height, with a 60% minimum size. The last size
  is remembered across instances.

**Verification.** In a new engine test, generated loops played through the keys are run
through the Lab's chord detector, and 40 of 48 chords (83%) are recognised as the chord played.
Theory tests cover bass roots and smoothness, the repeated bar rhythm, melody resolution,
range and stepwise motion, and cluster-free voicings.

**Honest limits.** I can't listen to the producers' tracks. The rules encode widely known
traits of the style (loop shapes, extended-chord colours, root-locked 808s with octave pops and
slides, short pentatonic motifs), not analysis of their actual songs.

---

## FL Studio test checklist

Run these on Windows with FL Studio 2026:

1. **Load.** Add Bounce as a generator. The window fits the screen, and resizing from the corner
   keeps the aspect ratio and is remembered.
2. **Playback.** With Sync FL off, FL's play button leaves Bounce silent, while ▶ in Bounce
   loops it and a lane's ▶ plays only that part. With Sync FL on, press play: the loop starts on bar 1, the wheel, cards and lanes follow the
   playhead, and looping is seamless with no stuck notes on stop or jump.
3. **Drag a part.** Drag each part's tile to a channel's Piano Roll: notes start at bar 1, the
   length is right, and the file name shows the chords, key and BPM.
4. **Drag All / Drag Arrangement** onto the Playlist: you get one track per part, plus section
   markers.
5. **MIDI out.** Set the wrapper output port to 1 and the input port of FLEX/Sytrus to 1, turn
   off Sound in Bounce: FLEX plays the chords (channel 1) and the 808 plays on channel 2.
6. **808 glides.** Drop the 808 part onto FL's 808 preset with slide or portamento enabled:
   the notes glide.
7. **Recall.** Save the project, restart FL and reopen it: the progression, locks, edits,
   history, arrangement, style, UI size and custom drum samples all come back.
8. **Automation.** Automate Complexity, Kick density and the levels: no clicks or stuck notes.
9. **Lab.** Drop a WAV or MP3 from FL's Browser onto the Lab: analysis finishes without
   stuttering audio, and Send works.
