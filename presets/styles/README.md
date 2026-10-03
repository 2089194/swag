# Style presets

Each `.json` file here defines one **Style Preset**. They are bundled into the plugin, and
Bounce also loads every `*.json` from the user styles folder at startup (a user file with the
same `id` as a bundled one replaces it):

| OS      | User styles folder                                       |
|---------|----------------------------------------------------------|
| Windows | `%APPDATA%\Bounce\Styles`                                |
| macOS   | `~/Library/Application Support/Bounce/Styles`            |
| Linux   | `~/.config/Bounce/Styles`                                |

Use the **Styles ▸ Open folder** button in the plugin to get there, and **Reload** to pick up
edits without restarting the host. `//` line comments and trailing commas are allowed.

Every field is optional. Anything you leave out falls back to the built-in defaults, so
a preset only needs to state what makes it different.

## Fields

```jsonc
{
  "id": "my_style",               // unique, used in saved projects
  "name": "My Style",             // shown in the UI
  "description": "...",

  "tempo": { "bpm": 150, "halfTime": true },  // preview clock when the host isn't playing

  "harmony": {
    "key": "A",                   // C, C#, Db, ... B
    "scale": "natural_minor",     // major, natural_minor, dorian, phrygian, lydian, mixolydian,
                                  // harmonic_minor, melodic_minor, major_pentatonic, minor_pentatonic
    "bars": 4,                    // 2, 4 or 8
    "chordCount": 4,              // chords per loop (1..8)
    "complexity": 0.65,           // 0 = triads ... 1 = 9ths/11ths
    "mood": 0.0,                  // -1 dark ... +1 bright
    "borrowed": 0.35,             // 0 = strictly diatonic ... 1 = lots of modal mixture

    // Markov transition weights, from-chord -> { to-chord: weight }.
    // Merged into the built-in tables: set a weight to 0 to forbid a move.
    // Major-tonic modes use majorTransitions, minor-tonic modes use minorTransitions.
    "majorTransitions": { "I": { "vi": 3, "IV": 2.5 } },
    "minorTransitions": { "i": { "bVI": 3, "bVII": 2 } },

    // How likely each chord is to open the loop.
    "majorStart": { "I": 3, "vi": 2 },
    "minorStart": { "i": 3, "bVI": 2 },

    // Relative preference for chord colours (multiplies the complexity curve; 0 disables).
    "colours": { "maj7": 1.8, "min9": 1.8, "add9": 1.2, "dom7": 0.2 }
  },

  "voicing": {
    "style": "spread",            // close, spread, open, third_on_top, flip_stab
    "low": 55, "high": 82         // right-hand register as MIDI notes (60 = C4 / FL's C5)
  },

  "performance": {
    "rhythm": "sustain",          // sustain, half, stabs, pulse8
    "stabSteps": [0, 3, 6, 10, 12], // for "stabs": 16th-note steps in each bar
    "stabGate": 1.5,              // stab length in 16ths
    "swing": 0.1,                 // 0..1
    "strumMs": 12,
    "velocity": 92,
    "velocityRandom": 8,
    "timingRandomMs": 6
  }
}
```

### Chord tokens

Transition tables use roman numerals measured against the **major scale of the tonic**, in
every mode, the way producers usually talk about loops: in A minor, `i` = Am, `bVI` = F,
`bVII` = G, `bIII` = C, `iv` = Dm and `V` = E (the harmonic-minor dominant).

- Upper case = major chord, lower case = minor chord.
- `b` / `#` prefix for chromatic roots (`bII`, `bIII`, `bVI`, `bVII`).
- `o`, `dim` or `°` suffix for diminished (`iio`, `viio`); `+` for augmented.

Chords that aren't in the current scale are treated as **borrowed** and only appear when the
Borrowed control is above zero; their extensions are drawn from the parallel major/minor scale.

### Colour ids

`maj min dim aug sus2 sus4 maj7 min7 dom7 m7b5 dim7 minmaj7 maj6 min6 add9 madd9 6_9 m6_9
maj9 min9 dom9 min11 maj7s11 dom7sus4 maj7sus2`
