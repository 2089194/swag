BOUNCE - swag / bounce beat idea generator (VST3, Windows 64-bit)
================================================================

Install
  1. Close FL Studio.
  2. Right-click install.bat > Run as administrator.
     (Or copy the "Bounce.vst3" folder into C:\Program Files\Common Files\VST3 yourself.)
  3. Open FL Studio > Options > Manage plugins > Find installed plugins.
  4. Add > Bounce (it's an instrument / generator).

Bounce.exe is a standalone version for trying ideas without a DAW
(Options > Audio/MIDI settings to pick your sound card).

Getting the MIDI into FL Studio
  - Drag a part's tile (Chords / 808 / Melody / Drums, or Drag All) onto a channel's
    Piano Roll or onto the Playlist.
  - Or MIDI out: plugin wrapper (gear icon) > MIDI > Output port = 1, and set the
    target instrument's Input port to 1. Chords use the MIDI channel set in Bounce,
    808 = +1, melody = +2, counter = +3, drums = channel 10.
  - Or Export... (writes .mid files named by chords, key and BPM).

Your own style presets go in %APPDATA%\Bounce\Styles (the "Styles" button opens it).
