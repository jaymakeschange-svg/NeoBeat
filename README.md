# MidiJaQ

MidiJaQ is the melodic one: a MIDI sequencer that writes lines and plays them, in the same swing and the
same words as NeoBeat. It sits on a MIDI track and drives any plugin or hardware you point it at, or it
plays a `.NJQ` instrument built in Batchcut. It is a standalone app and a VST3 plugin.

This is the first build: the sequencer itself, its clock and its MIDI out. The instrument side, the chord
track and the other generators come next.

## What it does

**JAQ writes a family, not a pattern.** One press writes every step at once — and writes two notes for
each, with the point at which it changes between them. Turning a knob afterwards moves through that
family instead of rewriting it, so the idea is never lost:

* **TRAFFIC** — how many steps play. Each step has a weight, and TRAFFIC opens them in that order, so a
  busier line keeps every note of a sparser one: the groove grows instead of changing. Heavy beats come
  in first (about twice as often as chance), but no two lines fall on the same steps.
* **DIALECT** — what they play, from free intervals to the root and its octave. Each step changes over at
  its own point, so the line moves a note at a time. Nothing else moves: the steps, the accents and the
  lengths stay exactly where they were.

**The rows are written apart and locked apart.** Rhythm, Note, Accent, Gate, Octave, Roll and Slide each
have their own seed and their own lock, so JAQ can keep the groove and change the notes, or the reverse.
Click a row's name to lock it (or **L**); **JAQ** (or **J**) writes everything that is not locked, and
**REROLL ROW** writes just the one.

**The line.** 1 to 32 steps on a grid of 8ths, 16ths or their triplets, turned by **OFFSET**, walked
forward, backward or as a pendulum, swung as NeoBeat swings. **ACCENT** opens the three velocities out
from 100 (at full, 40 and 127). **GATE** scales the three note lengths — a quarter of a step, five ninths
of it, and the whole of it; a long note into a different pitch leans 9% into it (a slide), into the same
pitch it holds exactly one step (a tie). A step can ratchet into up to four hits.

**Where the pitches come from.** **Free** writes its own line. **Follow** takes the pitches from the keys
you hold and keeps the line's rhythm. **Bass** reads the chord you hold and plays under it.

**The clock.** **Pattern** starts the line again whenever the transport does. **Song** puts the line where
the song is, so a bar always plays the same thing. **Gate** plays only while keys are held.

## Routing

MidiJaQ makes MIDI, so the plugin it drives sits somewhere else:

* **Ableton Live** — MidiJaQ on a MIDI track, the instrument on a second one; set the second track's
  *MIDI From* to the first, and *Monitor* to In.
* **Bitwig, REAPER** — MidiJaQ before the instrument in the same chain.
* **Logic** — as an AU MIDI FX slot above the instrument.

## Building

```
cmake -B build -DJUCE_DIR=<your JUCE> -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target MidiJaQ_VST3 MidiJaQ_Standalone test_jaq test_clock
```

JUCE 8.0.15 is fetched at configure time if `JUCE_DIR` is not given.

## Tests

Framework-free, on the exact code the plugin runs:

* `test_jaq` — the same seed writes the same family on every computer; the rows are written apart and
  locked apart; TRAFFIC nests and favours the strong beats; DIALECT moves the pitches and nothing else,
  a note at a time, always in the key; the three velocities; the three lengths, slides and ties; steps,
  offset, swing, direction; and the knobs read the family without rewriting it.
* `test_clock` — the player sends exactly what the line says, within two samples of each beat, and lets
  every note go again; the three clock modes; stopping the transport releases what is sounding; Follow
  and Bass take their pitches from the held keys; and the block size never changes a note.

## MidiJaQ, NeoBeat and Batchcut

They are separate programs that share one file format and one set of generators, through headers kept
identical in every source tree (`Source/neobeat/` here): the sequencer and its 128 patterns, the random
source and the 32 scales, and the `.NJQ` reader and writer. None needs another installed.
