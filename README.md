# NeoBeat 3.0

A new jack swing drum machine built on the merged Liquid Swing / NeoSpectral /
MaterialLab research: physically modelled rubber, metal and liquid hits, city
sounds rebuilt as live models, a hollow chain on every lane, and a workstation
around them: four scenes of up to 32 bars, the Matrixxxer, Layermax, a key map
with splits and layers, and modulation you drag onto any knob.

Private, archival use.

## New in 3.0

* **Effects are live.** The 24 library effects and Bounce Delay run as you play: turn a knob, swap an
  effect, move a node, and the next block hears it, also on a tail that is still ringing. Nothing waits
  for a render, and nothing needs the plugin reopened. Hits the sequencer plays through spectral effects
  land exactly on the step (NeoBeat sees them coming and starts the effect a frame early).
  **Friction Stretch** is the one effect still prepared ahead (it needs the whole hit before it can
  start); a notice says so the first time you use it, and the header shows PREPARING while it works.
  The one case a hit can sound late (a hit played by hand through a spectral effect before that lane's
  sound is prepared, or through several spectral effects in a row) also gets a notice.
  Limits: 24 effected hits at once, 4 per lane, 14 spectral effects sounding at once; beyond that the
  oldest tail fades.
* **Scenes.** Four scenes (A to D), each up to 32 bars. The grid shows 1, 2 or 4 bars at a time, so a
  node is never too small to hit; the strip above it shows every bar and takes you there. While it
  plays, a new scene starts on the next bar. **CHAIN** plays scenes one after another, each a number of
  times (click + to add the scene shown; click a link to change its scene, wheel for repeats,
  right-click for more).
* **Undo and redo in every room** (the two buttons in the header; Ctrl+Z and Ctrl+Shift+Z in the
  standalone). Each room keeps its own history of the last 100 steps.
* **One set of words.** A *room* is one of the four tabs. A *lane* is one of the 16 rows of the
  sequencer and its sound. A *node* is a box in a sequencer that holds a hit. You *send* a node, a lane
  or a row to an effect; until then a node is *Init* (no effect). Right-click any node: **Send node
  to**, Copy, Paste, Clear; its settings open in the inspector on the right.
* **KIT / FILE** (header): load and save **.NJQ** files (a kit and its sequence in one file, samples
  included, lossless), your sequences from the preset folder, a folder of samples into the lanes,
  export, MIDI out. After saving or loading a .NJQ, NeoBeat asks whether to make it a **default
  sequence**: *Add to my sequences* (it is listed in every project) or *Add and start with it* (new
  NeoBeats open with it).
* **Import a folder of samples.** NeoBeat listens to every file, works out which lane each could play
  and puts them there in place of the stock sounds, each brought to the stock sounds' loudness.
  *Import all* places every sound (the rest become Layermax's rows); *Pick the most musical* only takes
  clean single hits, no two alike. Drop a file on another pad, or rate a lane's sample in the
  inspector, and it learns: the learner is a small neural network kept on your computer
  (`%APPDATA%\Marbelite\Shared\learner.txt`), shared with Batchcut.
* **Export** the pattern as WAV (what you hear, effects and tails included; optionally one file per
  lane) and as a MIDI file, into a folder you choose; drag the files straight into your host.
* **MIDI out: NeoBeat as a sequencer.** A note and channel per lane (a Drum Rack from C1 by default),
  note length, rolls as notes, and *MIDI only* (the lanes stay silent). The panel has the three-step
  patch for Ableton Live and a TEST button per lane.
* **Layermax** (the grid, renamed) has its own tab, and any row can **follow a lane** of the
  sequencer: with every hit (layer), in the gaps (fill) or every Nth hit, early or late by milliseconds.
  *Unlink and keep* writes it into the pattern.
* **Sound:** WIDTH on every lane (mono to wide; the widening disappears in mono), LEVEL up to +24 dB,
  and MATCH LEVEL for samples.

## Install (Windows)

1. Unzip the `NeoBeat-Windows` download.
2. Copy the `NeoBeat.vst3` folder to `C:\Program Files\Common Files\VST3\`
   (replace the older one; Live sets saved with 2.0 still open, their pattern becomes scene A).
3. In Ableton: Options → Preferences → Plug-ins → turn on **Use VST3 Plug-In System Folders** and **Rescan**.
   NeoBeat appears under Plug-ins → Marbelite.
4. `NeoBeat.exe` is the standalone version (press **PLAY** to run the sequencer at the standalone tempo).

Your own presets are saved under `%APPDATA%\Marbelite\NeoBeat\Presets\` (one folder per section;
samples a preset uses are copied into `Presets\Samples` so the preset still opens elsewhere).

## The layout

Everything sits in one window, scaled to whatever size you drag it to:

* **Header**: the four rooms (SEQUENCER, MATRIXXXER, LAYERMAX, KEY MAP), undo and redo for the room
  you are in, the display, KIT / FILE, the Friction Stretch and Bounce Delay windows, the status and
  the master level.
* **Tree** (left): every pad, the Matrixxxer views, the grid, the key map layers and the FS / BD slots.
  Click a line to go straight there.
* **Room** (middle): the thing you are working on.
* **Inspector** (right): the settings of whatever is selected (a pad, a cell, a box, a mark, a zone),
  in three tabs: SOUND, FX and MOD.
* **Mod dock** (bottom): the modulation sources. Drag a tile onto a knob to route it; the knob grows
  a coloured ring for each source that reaches it. Right-click a knob to add or remove sources, and set
  the depths in the inspector's MOD tab. **LIST ALL** in the dock shows every route in one place.

## The rooms

### SEQUENCER
16 lanes (their pads above, their nodes below), four scenes, and the kit controls: 44 library sounds plus the MaterialLab
bodies and Stretch & Snap rubbers, three kits, eight macros per lane, choke groups, 1 to 32 bars per scene,
swing, accents, ghosts, ratchets, triplet rolls, nudge. **128 patterns** (the 50 originals plus 78
variations: Ghosts, Rolls, Push, Lift). Each pad has **three insert effects** and velocity layers
for them (soft / medium / hard).

### MATRIXXXER (three views)
* **One Shot**: lanes for the 16 pads under the pattern. Draw a cell on a lane and that hit plays
  through the cell's effect. The **cell's height sets the effect's main control** (decay for Freezing,
  interval for Pitch Shift, stretch for Friction Stretch, and so on); its **span** is how long the
  effect rings. 32 starting recipes.
* **Sample**: pad 16's sound as a spectrogram. Draw boxes over time and frequency; each box sends just
  that band of that moment through an effect. A taller box (more octaves) pushes the main control
  further. 31 presets.
* **Draw**: a drawn score with five brushes (dot, dash, warble, tick, spray) played by a synth
  with four engines (additive, FM, FFT bands, grains from a pad). Scales, mirror, 36 rows. 31 presets.

### LAYERMAX
A 64 × 64 drawing grid: each row is a pad of the kit (or a sample from a folder
you choose), each column a step. Colour is the hit: red = volume, green = pan, blue = pitch. Tools:
pencil, line, air (spray), erase, pick. 16 patterns, rates of 1/8, 1/16, 1/16 triplets and 1/32,
and the kit's swing.
Cells can also feed the Friction Stretch and Bounce Delay slots.

### KEY MAP
Map sounds across the keyboard like Falcon or Kontakt: draw **zones** (a key range × a velocity range)
in **layers**. A zone plays a pad, a sample, a whole kit spread over the keys, a One Shot cell, or
a grid pattern while the key is held. Root key, keytracking, tuning, gain, pan, round robin groups,
choke and its own effect per zone. Five starting layouts: one key per pad, a three-way split,
snare velocity layers, a kit across the keys, round robin hats.

## Effects

24 Matrixxxer effects, all NeoBeat's own code: Averaging, Blurring, Drone Maker, Freezing, Gate Hold,
Weave, Shimmer, Shuffle, Pulsing, Harmonizer, Bin Shift, Pitch Shift, Stretch, Tracing, Filterbank,
Gliding Filters, Granulation, Emergence, Partial Glide, Idee Fixer, Chorus, Comb Filterbank,
Grain Streamer, Super Filterbank.

Plus the two big ones, each with its own window and 8 slots:
* **Friction Stretch**: the hit stretched up to 64× and peeled like duct tape, with the yanks on the
  swung grid (sonogram view with numbered lines).
* **Bounce Delay**: echoes that bounce like a ball in a room you can see (each flight shorter than the last).

A slot listens to a pad, a grid row or a One Shot cell. **REPLACES** puts the effect in place of the
hit (an accent); **ADDS** keeps the hit and adds the effect.

## The two effects on their own

`Friction Stretch.vst3` and `Bounce Delay.vst3` are the same two effects as separate audio-effect
plugins (each also comes as a standalone `.exe`), for testing them away from NeoBeat. Copy them to
`C:\Program Files\Common Files\VST3\` like NeoBeat; they appear under Plug-ins → Marbelite.

Each works on one whole hit at a time, and has two ways of getting one:

* **LOAD A HIT**: load or drop a sample of up to 4 s (a built-in clank is there to start with). It
  re-renders in the background whenever you move a knob. **PLAY**, a MIDI note, or **LOOP** (every
  1, 2 or 4 bars while the host runs) plays it. **KEYTRACK** makes MIDI keys transpose it (C3 = as it
  is). To send MIDI in Ableton, set a MIDI track's *MIDI To* to the track holding the plugin and pick
  the plugin in the second chooser.
* **CATCH HITS**: put it on a drum track. Every hit that rises over the white line on the meter is
  caught (LENGTH sets how much of it), rendered, and joined in where it would be by then, so the yanks
  and bounces still land on the song's swung grid. Bounce Delay keeps up live. A long Friction Stretch
  takes a moment to render, so you hear it from a little way in. **KEEP LAST HIT** turns the last caught
  hit into the loaded hit.

**CHOKE** makes a new hit cut the one before. **SAVE WAV** writes what the display shows. The preset
box holds six starting points for each. Every knob is a host parameter (automation works); the loaded
hit and Friction Stretch's lines are saved with the project. In Bounce Delay the ROOM knob is live here
(small room, studio or large hall).

## Modulation

Sources: LFO 1, LFO 2 (draw its shape), Envelope, Random, Velocity, Key, Height, Step, XY X, XY Y.
Each can run per kit, per pad or per cell. Destinations: every pad macro, every cell's main control,
the FS and BD slot settings, the sequencer synth and the grid. Each pad can also run one of five **algorithms** (ring, comb, crush,
filter, fold) driven by a mod source; six starting presets.

## Playing it

| Mode | What happens |
|------|--------------|
| Host | The pattern runs whenever Ableton's transport runs. |
| Key  | Hold C3 or any key above: the whole pattern plays transposed by that key, at Ableton's tempo. |
| Pads | No sequencer; play the pads (and the key map) from MIDI. |

* MIDI notes go through the key map. The default layout is build 1's: **C1–D#2 (36–51)** play pads 1–16.
* NeoBeat **sends its sequencer out as MIDI**: see KIT / FILE > MIDI out (by default notes 36–51 on channel 1).

## Mouse reference

| Where | Gesture |
|-------|---------|
| Step grid | click = hit, shift-click = accent, alt-click = ghost, drag = paint/erase, wheel = velocity, right-click = the node's menu (Send node to, copy, paste, rolls, nudge) |
| Pads | click to play (higher = harder), right-click to load a sample, drop an audio file to load it |
| One Shot lanes | click an empty step to draw a cell with the current brush; drag a cell sideways to move it, up/down to set its height; drag its right edge for the span; right-click deletes |
| Sample view | drag on empty space to draw a box; drag a box to move it in time and pitch, its edges to resize; right-click deletes |
| Draw view | pick a brush and click/drag to draw a mark; drag a mark to move it; right-click deletes |
| Layermax | pencil/line/air paint, erase (or right-drag), pick copies a cell's colour |
| Key map | drag on empty space to draw a zone; drag a zone to move it, its edges for key and velocity range; click the keyboard to audition; right-click deletes |
| Knobs | drag or wheel, double-click to reset, drop a mod tile to route, right-click for the routes |

## Good to know

* Effects that change a whole hit (cells, slots, boxes, inserts, zones) are **rendered in the
  background** a moment after you stop editing. The status in the header shows the progress; the
  previous version keeps playing until the new one is ready. Most take well under a second; a
  64× Friction Stretch takes a few seconds.
* Friction Stretch reads up to 4 s of the hit and fades out at 24 s.
* Tested before release: engine tests (every preset of every view, keytracking, layers, round robin,
  grid swing, modulation, 128 patterns), sanitizers (memory, undefined behaviour, threads),
  pluginval at its strictest level, a host stress test with a random-click "monkey" on every control,
  and a CPU benchmark (about 10% of one core for a busy project; the room reverbs spread their work so
  no audio block spikes).

## Building from source

```
cmake -B build -A x64            # fetches JUCE 8.0.15
cmake --build build --config Release --target NeoBeat_VST3 NeoBeat_Standalone
```

`tools/neobeat_cli` renders any sound or preset offline (`neobeat_cli preset 0 out.wav 4`),
`tools/test_sequencer` checks the timing, `tools/test_workstation` runs the 2.0 engine tests and
`tools/test_fx` the stand-alone effects' engine.
