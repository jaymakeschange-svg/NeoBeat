# NeoBeat — build 1

A new jack swing drum machine built on the merged Liquid Swing / NeoSpectral
research: physically modelled rubber, metal and liquid hits, city sounds rebuilt
as live models, and a TurboSynth-style hollow chain on every pad.

Private, archival use.

## Install (Windows)

1. Download the `NeoBeat-Windows` artifact from the latest GitHub Actions run and unzip it.
2. Copy the `NeoBeat.vst3` folder to `C:\Program Files\Common Files\VST3\`.
3. In Ableton: Options → Preferences → Plug-ins → turn on **Use VST3 Plug-In System Folders** and **Rescan**.
   NeoBeat appears under Plug-ins → Marbelite.
4. `NeoBeat.exe` is the standalone version (press **PLAY** to run the sequencer at the standalone tempo).

## What's in build 1

* **Kit library, 44 sounds**: Round 4 (Clank, Latex, Metal Tail variations), Round 5
  (coupled resonators and metal network), Round 6 city one-shots (horn bonk, bell clank,
  drill hat, screech snare, bark kick, train door, brake stab, jet riser) rebuilt as live
  models, and the IronHead-style knock and ding.
* **Three kits** that keep the same pad roles (Rubber City, Clank Yard, Latex Float), so
  any pattern plays on any kit. Every pad can load any library sound or your own sample.
* **Eight macros per pad**: Tune, Decay, Tone, Drive, Hollow (dry → hollow chain),
  Evolve (how far the chain moves), Pan, Level; plus choke groups and mute.
* **Sequencer**: 16 tracks × 16/32/64 steps, swing, velocity (accent / normal / ghost),
  ratchets ×2/×3/×4, triplet rolls, per-step nudge. Locked to Ableton's tempo and bar position.
* **50 new jack swing presets** at 118–126 BPM in five families: Swingbeat, Triplet Roll,
  Go-Go Bounce, Hard Swing, Metal Silk.

## Playing it

| Mode | What happens |
|------|--------------|
| Host | The pattern runs whenever Ableton's transport runs. |
| Key  | Hold C3 or any key above: the whole pattern plays transposed by that key, at Ableton's tempo. |
| Pads | No sequencer; play the pads from MIDI. |

* MIDI notes **C1–D#2 (36–51)** play pads 1–16 in every mode.
* NeoBeat **sends its sequencer out as MIDI** (channel 10, notes 36–51). In Ableton, set another
  MIDI track's *MIDI From* to the NeoBeat track to drive another instrument with the pattern.
* Step grid: click = hit, shift-click = accent, alt-click = ghost, drag to paint or erase,
  mouse wheel = velocity, right-click = rolls, triplet rolls and nudge.
* Pads: click to play (higher on the pad = harder), right-click to load a sample, mute or clear;
  drag an audio file onto a pad to load it.

## Building from source

```
cmake -B build -A x64            # fetches JUCE 8.0.15
cmake --build build --config Release --target NeoBeat_VST3 NeoBeat_Standalone
```

`tools/neobeat_cli` renders any sound or preset offline with the same engine
(`neobeat_cli preset 0 out.wav 4`), and `tools/test_sequencer` checks the timing.

## Not in this build yet

The MetaSynth-style drawing grid, the top-down stage panner and spectral colour pan,
and the melodic Wave/Cycle-style bass, lead and pad voices are planned for later builds.
