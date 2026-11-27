# Batchcut 3.0

Batchcut finds the events in a recording (a ball bouncing in a noisy yard, the hits of a drum loop, clanks in a
field recording) and cuts them into one-shots, as many files as you like at once. Then it makes things out of
them: drum kits, loops and playable instruments. It is a standalone app and a VST3 plugin.

The switch at the top opens its five rooms, which share one pool of one-shots:

* **CUT**: find the events and export the one-shots (and gather them into the pool).
* **KIT**: a drum kit for NeoBeat, played in NeoBeat's 128 patterns, saved as a `.NJQ` file.
* **LOOPS** (new in 3.0): loops of up to 8 bars and 8 layers, rolled from the pool by rules you set.
* **KEYS** (new in 3.0): an instrument to play from a keyboard, built from the pool, saved as a `.NJQ` instrument
  for MidiJaQ.
* **MATERIAL** (new in 3.0): objects learned by the MaterialLab learner, struck or with sound sent through them,
  and any sound fitted into such an object.

**Also new in 3.0, for big batches.** **CLEAR ALL** takes every file off the list (and **REMOVE** takes several at
once: Shift or Ctrl-click them). In the kit room, **RANDOM** (or **R**) deals fresh sounds: every sound that fits a
lane, ignoring stars and ranking, and none comes back until all of them have played. **CLEAR LANES** empties every
lane that is not locked, and the pool's **CLEAR ALL** empties the pool. Batchcut now takes MIDI in (a keyboard, or
the track in a DAW): LOOPS and KEYS play from it.

## Quick start

1. Drop recordings or whole folders onto Batchcut (or **ADD FILES**). The first one opens and its events are found.
   **REMOVE** takes the selected files off the list (Shift or Ctrl-click selects several), **CLEAR ALL** every
   file, after asking. The files themselves are never touched.
2. Click an event (the coloured blocks above the spectrogram) to hear it as it will be exported. Arrow keys step through them.
3. **EXPORT** writes the one-shots of the open file; **EXPORT ALL** does every file in the list.

One-shots are named after the recording, their group and a number: `yard_A001.wav`, `yard_B003.wav`
(`yard_like_001.wav` for FIND MORE, `_p2_` for later parts of long recordings, `_tone` and so on for layer files),
by default in a folder per recording under *Music/Batchcut/One-shots*.

## Finding events (DETECT)

Each frequency band is compared with its own background, so a quiet hit in a noisy recording is found where its
sound stands out, even when the waveform shows nothing.

| Control | What it does |
|---|---|
| Presets | **Field** (sparse events in noise), **Dense** (many events close together), **Loops** (cuts at the next hit) |
| Sensitivity | How quiet an event may be and still be found |
| Min gap | Starts closer than this are one event. Under 45 ms, fast bounces and flams are split too |
| Grouping | How finely events are sorted into groups of alike sounds |
| Pre-roll, Tail | Kept before each start and after the sound has sunk into the background |
| Max length | No one-shot is longer than this |
| Where an event ends | **Floor**: where it sinks into the background. **Next**: at the next event. **Gap**: at the quietest moment just before the next event. **−40 dB**: 40 dB below its peak |

Long recordings open in parts of 8 minutes (with ‹ › under the display). Nothing is lost at the seams: the
parts overlap by 2 s and each event belongs to one part.

## Events (EVENTS)

* **Groups**: events are sorted into groups of alike sounds ("low hit", "high click"). Click a group to leave it out of the export.
* **The selected event**: hear it, switch it off for export (**E**), delete it (**Delete**). Drag its edges in
  the event lane to change where it starts or ends; double-click an empty spot of the lane to add an event by hand.
* **Find more like this** (**F**): searches every file for sounds like the selected one, in that sound's own
  frequency bands. It also finds ones too quiet for general detection. **Similarity** sets how alike they must be.
* **Drag out**: drag the selected one-shot from the pad into your DAW or a folder.

## Layers and the spectral editor (LAYERS)

Every recording is split into four layers: **Background** (the steady noise), **Tone** (what rings),
**Transient** (the attacks) and **Noise** (the rest of a hit). Each can be muted, soloed and set in level, and what
you hear is what is exported: mute the background to export clean hits. Everything under 30 Hz (DC offset,
subsonic rumble, wind buffeting) belongs to the background, so muting it also cleans that away.

Select areas on the spectrogram with **Box**, **Brush**, **Lasso** or the **Magic wand** (keys 1 to 4; Shift adds to
the selection, Alt takes away; Alt + wheel sets the brush size or the wand's tolerance). Then **Erase**, make the
selection **−6 dB** or **+6 dB**, or move or copy it **to a layer** of its own. Click a layer in the list to make the
tools work on that layer only. Everything can be undone (Ctrl+Z) and redone (Ctrl+Shift+Z).

## Export (EXPORT)

* **Which**: **All** events that are switched on, the **Best** few of each group (clear of the background and
  typical of their group), or only the ones **Like** the FIND MORE sound.
* **Level**: **Off** (as recorded), **Peak** (every peak at the same level), or **Loudness**: every one-shot
  equally loud, measured as the broadcast loudness standard does (ITU-R BS.1770: K-weighted, the loudest 400 ms).
  No peak goes above the **ceiling**. With **Match all**, if some one-shots cannot reach the target without passing
  the ceiling, the others come down to them, so the whole set really matches. The export says which one set the level.
* Fades in and out, 16 or 24 bit or 32 bit float WAV, a file per layer, a folder per recording.

## Recording

**RECORD** records the input (in the standalone app: the audio input chosen under Options; in a DAW: the track
Batchcut is on). The recording is saved in *Music/Batchcut/Recordings*, added to the list and opened. In a DAW the
track's sound passes through Batchcut unchanged (parameter *Input through*); the standalone app never plays its
input back.

## The kit room (KIT)

**1. Fill the pool** (left). **THIS FILE** and **ALL FILES** gather the one-shots exactly as EXPORT would cut them
(the same events, fades and layers), without writing anything. **FOLDER** adds one-shots you already have; dropping
a folder on the kit room does the same. Each sound is listened to once: what it could play in a kit (the coloured
tag), how usable it is, how loud it is. The list shows what fits the selected lane first. The pool holds up to 2,000
sounds; **CLEAR ALL** empties it (sounds still on a lane stay: CLEAR LANES first to empty it completely).

**2. Fill the lanes** (middle). The 16 lanes are NeoBeat's: kick, kick 2, snare, snare 2, closed hat, open hat,
hat 3, rim, thud, clank, junk, drop, horn, stab, bell, FX. **AUTO-FILL** puts the best fit on every lane, each sound
once and no two alike. **SHUFFLE** makes another kit from each lane's four best, so your favourites come back.
**RANDOM** (or **R**) is for hearing fresh sounds: each lane deals from every sound that fits it (kick-like sounds
for the kicks, hats for the hats), ignoring stars and ranking, and no sound comes back until all of them have
played, the way Tetris deals all seven pieces before it repeats one. The message says how many are still to come.
**ANY LANE** lets any sound land on any lane, for stranger kits. **CLEAR LANES** empties the lanes. **LOCK** keeps
a lane through all of these. **<** and **>** step a lane through its candidates; double-clicking a pool sound puts
it on the selected lane. **M** mutes a lane in the pattern.

**3. Layer** (right). A lane holds up to three layers, each with its own level, offset (earlier or later than the
others), pitch, fade, the part of the sound that plays (drag the edges of the waveform) and reverse. The lane has a
level, a pan and a width (mono to the sound's own stereo). Every sound is first matched to the loudness of NeoBeat's
own sounds (-15 LUFS, never past -1 dBFS), so a kit sits at the right level before you touch a knob.

**4. Hear it in a pattern** (bottom). **PLAY** runs the kit through one of NeoBeat's 128 patterns; the strip on each
lane shows where the pattern plays it. Choosing a pattern takes its tempo and swing; **BPM** and **SWING** change
them. A limiter keeps the sum under the output's ceiling, as NeoBeat's kit bus does.

**5. SAVE .NJQ** writes one file into your NeoBeat preset folder: the 16 lanes' sounds (layers mixed, lossless),
the pattern as scene A, tempo, swing and name. In NeoBeat: **KIT / FILE > My sequences**. NeoBeat can then ask
whether to make it a default sequence. Tempo, pattern and every lane stay changeable there.

**Stars** teach Batchcut and NeoBeat your taste: rate a sound and sounds like it are offered earlier (or later) in
both apps. Putting a sound on a lane yourself also teaches it what that lane's kind sounds like to you. The learner
is a small file on your computer (`%APPDATA%/Marbelite/Shared/learner.txt`); nothing leaves it.

The kit (lanes, layers and their sounds) is kept with the project; the rest of the pool is gathered again with a
click. Kit room keys: Space plays, R deals RANDOM sounds, up and down walk the lanes, left and right step through
candidates, Enter hears the lane, Ctrl+Z / Ctrl+Shift+Z undo and redo changes to the lanes.

## The loops room (LOOPS)

A loop is up to **8 layers**, each **1, 2, 4 or 8 bars** long on a grid of 8ths, 16ths or their triplets. A shorter
layer repeats under a longer one, so a 1-bar hat under a 4-bar melody makes a 4-bar loop. Each layer is filled
with **chops** (sounds from the pool, cut to note values at the loop's tempo) by its **mode**:

| Mode | What it rolls |
|---|---|
| **Slice** | One source's chops in a new order, each running into the next (a break, re-cut) |
| **Pool** | Low, heavy chops on the strong beats, short bright ones between |
| **Stutter** | A few chops, repeated and ratcheted |
| **Melody** | Tuned chops, played in the loop's key and scale (32 scales) |

**1. Choose the sounds** (left): the whole pool, the one-shots of one recording, or one kind (kicks, hats, bells…).
Each layer takes its sounds from what is chosen when it is rolled. **THIS FILE**, **ALL FILES** and **FOLDER** fill
the pool as in the kit room.

**2. Roll** (right, for the selected layer). **DENSITY** is how many steps play. **DEJA VU** is how often the layer
repeats itself: at 0 every bar is new, all the way up one bar loops for ever, in between it comes back to what it
played before now and then. The rhythm can be the layer's own or **borrowed** from a lane of one of NeoBeat's 128
patterns. **RANDOM** deals chops from every sound of the source, none again until all have played (as RANDOM in the
kit room). **ROLL** (**R**) rolls the layer again, **ROLL ALL** (**Shift+R**) every layer; **LOCK** (**L**) keeps a
chop where it is through every roll. **+ ADD A LAYER** adds one (rolled from what is chosen on the left).

**3. Shape** (the grid, middle). Click a chop to hear it, drag it to move it, double-click an empty step to add one.
Right-click a chop to re-roll it, lock it, play it backwards, or ratchet it (2 to 4 hits in its step); **PITCH** and
**LEVEL** change it. Each layer has a level and a pan, **M** mutes it, **S** solos it. **BPM** and **SWING** are the
loop's (in a DAW the tempo follows the song, and PLAY follows the song's position while the song plays).

**4. Take it out.** **WAV** writes the loop so it loops seamlessly (tails wrap round to the start); **STEMS** writes
the loop and each layer as its own WAV, all the same length; **MIDI** writes the chops as notes on NeoBeat's lane
notes (C1 up); **.NJQ** saves it for NeoBeat (up to 16 chops as lanes, the loop as a pattern, and the layers
written into it too); **BOUNCE TO POOL** makes the loop one new sound in the pool, to layer or put on a kit
lane. Files go to *Music/Batchcut/Loops*; `.NJQ` files to your NeoBeat preset folder.

**From MIDI.** **Trigger**: a key plays the loop from its start, transposed (C3 as it is). **Slices**: each key from
C3 up plays one of the selected layer's chops.

The loop (layers, chops, seeds and their sounds) is kept with the project. LOOPS keys: Space plays, R / Shift+R
roll, up and down choose the layer, left and right step through its chops, Enter hears the chop, Delete removes it,
L locks it, Ctrl+Z / Ctrl+Shift+Z undo and redo.

## The keys room (KEYS)

KEYS builds an instrument out of the pool and plays it from a MIDI keyboard, the keys on screen or your computer
keyboard (**A** to **K** play C3 to C4, **W E T Y U** the black keys).

**1. Choose the sounds** (left), as in LOOPS, and **how it plays** (right):

| Mode | The instrument |
|---|---|
| **Tuned** | Sounds with a clear pitch, spread over the keys: each covers the keys nearest its own pitch. Sounds of the same pitch become **velocity layers** if one is 4 dB or more louder than another (up to four), and **round robins** (played in turn) if they are alike in loudness. Pitches are measured to a fraction of a cent, so every key is in tune |
| **Resonator** | Any sound, pitched or not (a drill, a tyre screech, a kick), struck into a resonator tuned to the key: up to 16 sounds, played in turn. **RING** is how long a note rings (50 ms to 6 s, high notes as long as low ones); **BRIGHT** how many overtones ring with it. Only what rings is heard, so a long noise or a kick's boom does not blur the note. Each sound is levelled by how it rings, so a click and a long drill come out alike |
| **Chops** | A different sound on each key from C2 up, as it is (a kit on the keys) |

**SUSTAIN**: **As recorded** (each sound plays out once), **Loop** (a loop found in the sound's body holds it while
the key is down, without a click at the seam) or **Hold** (the sound's body frozen, in tune, while the key is down).
**ATTACK**, **RELEASE**, **GLIDE** and **MONO** (one note at a time, a new key glides from the held one) shape how it
plays. **CHORDS** makes each key play a chord (major, minor, sevenths, sus, power, or **In key**: three notes of
LOOPS' key, a third apart).

**2. BUILD** (**B**). The map shows every sound's keys (across) and velocities (up); round robins share a cell. Click
a sound in the map or the list to hear it and choose it: **TUNE** retunes it in cents, **LEVEL** sets its level,
**LEAVE OUT** builds the instrument again without it.

**3. SAVE .NJQ** writes the instrument, its sounds lossless, into *Documents/Marbelite/Instruments*, where MidiJaQ
finds it. **OPEN .NJQ** opens one to play or change. The instrument is also kept with the project.

KEYS keys: B builds, up and down choose a sound, Ctrl+Z / Ctrl+Shift+Z undo and redo, and the computer keyboard
plays.

## The material room (MATERIAL)

MATERIAL plays objects learned by MaterialLab, the learner: each one a fitted model of how a particular piece of
metal, plastic or rubber rings when it is hit, kept as a `.json` material file.

* **FOLDER...** (or drop a material file or a folder on the room) lists the materials; click one to load it, **<**
  and **>** step through them.
* **STRIKE** hits the object; hit it again while it rings and the energy adds. **RENDER 8 HITS TO POOL** strikes it
  eight times at different strengths into the pool.
* **SEND THROUGH** sends the chosen pool sound through the object; the knob sets how much of the object you hear.
* **Fitting any sound.** Drop an audio file on the room, or choose a pool sound and press **FIT**: Batchcut learns
  the object that made it - the learner's own fit, run inside Batchcut - four times side by side, each started a
  hair apart, so the four end as four slightly different objects. Each becomes a material file in
  *Documents/Marbelite/Materials/(the sound's name)* and a sound in the pool (*... fit 1* is the closest), and the
  room opens them with the closest loaded. Progress and **CANCEL** are on the line under the picture. A fit takes
  the first strike in the sound and the 1.5 s after it, and takes about a minute (longer for a long, bright metal
  ring); it carries on with the window closed. MaterialLab opens these files as its own.
* **Breeding.** **SCRATCH** makes eight materials from nothing, one of each shape - a metal bar, a plate, a drum
  skin, a tube, a closed tube, a bell, metal junk and rubber - with **REACH** setting how far past real objects they
  go (stretched partials, big glides, highs that ring longer than lows). **WANDER** makes eight variants of the loaded material and of the sound it was fitted to: the
  material moved (transposed, more metallic, ringing longer, beating more...) or crossed with another, the sound
  stretched, smeared, granulated or sent through the material. **GROW** climbs toward the sound from the loaded
  material (or what you kept), twelve generations on its own, and shows the eight closest it made. **REACH** sets
  how far the children go. They appear along the bottom of the picture, with how close each came to the sound:
  click one to hear it, **KEEP** the ones you like - a kept child goes into the pool (and a kept material into a
  material file beside the fits), and the next generation breeds from what you kept.

## MIDI

Batchcut takes MIDI in: in the standalone app from the MIDI input chosen under Options, in a DAW from its track.
The room shown last of LOOPS and KEYS gets it, and keeps it while you work in CUT or KIT, so a DAW track can play
your instrument while you build a kit.

## Keys

| Key | |
|---|---|
| Space | Play / stop from the cursor (click the spectrogram to set it) |
| ← → | The event before / after (and hear it) |
| Enter | Hear the selected event |
| Delete | Erase the selection, or delete the selected event |
| E / F | Export the selected event or not / Find more like it |
| 1 2 3 4 | Box, brush, lasso, wand |
| [ ] | Brush size / wand tolerance |
| Ctrl+wheel, + − | Zoom; Home: everything |
| L | Loop the visible stretch |
| Ctrl+Z, Ctrl+Shift+Z | Undo, redo |

## How it works

* Onsets: spectral flux in the style of SuperFlux (Böck and Widmer, DAFx 2013), each band whitened by a quantile
  noise floor (Stahl et al., ICASSP 2000) and by its recent background, as per-channel energy normalisation does
  (Wang et al. 2017; Lostanlen et al., IEEE SPL 2019), so wind and passing traffic do not read as events. Each band's
  vote counts by how many independent frequency bins it holds, so the narrow lowest bands, which swing most by
  chance, cannot make an event out of rumble.
  Starts are placed to about 1 ms with a second, short analysis window; strikes closer than the main window
  resolves (the last bounces of a ball) are found on that short window too.
* Layers: median-filter harmonic/percussive separation with a residual (Fitzgerald, DAFx 2010; Driedger et al.,
  ISMIR 2014), resynthesised by overlap-add (an untouched recording comes back unchanged).
* Groups and FIND MORE: a description of each sound (cepstrum of its band profile, centroid, flatness, decay,
  tonal and transient share, with what was already sounding taken off) and where its onset rose, clustered by
  leader clustering and k-means.
* Loudness: ITU-R BS.1770 K-weighting at every sample rate, momentary (400 ms) loudness.
* LOOPS: each layer rolls from its own seed (the same seed always gives the same layer, on every computer). DEJA VU
  is a loop of remembered steps that a step is drawn from instead of a new one, with the chance set by the knob, in
  the spirit of the déjà vu of Mutable Instruments' Marbles. Borrowed rhythms are NeoBeat's own patterns.
* KEYS: pitch by autocorrelation, refined between samples (parabolic interpolation); loops where the sound's body
  is most alike one period apart, with a crossfade; Hold from overlapping grains started whole periods apart, so they
  add up in phase. The resonator is a tuned delay line with a damping filter in it, in the manner of Karplus and
  Strong: its length allows for the delay of the filter and of the in-between read at the note's pitch (else high
  notes go flat), and its feedback for what they take away each time round.

## Tested

* `test_engine`: run on 20 different noise draws as well as the default one (`BC_SEED`), so nothing is tuned to one
  lucky recording; the test recordings use platform-independent noise, so Windows and Linux test the same sound.
  Detection on recordings with known starts (bounces under noise, ticks in traffic rumble, a dense
  drum loop: 64 of 64 found, starts within 2 ms), layers (an untouched recording comes back to -139 dB), edits,
  export, the preview player, the BS.1770 meter against the Recommendation's tables and reference tone.
* `bench_detect`: harder field scenes (gusty wind, passing cars, talking, rain, a hall, bounce trains).
* `test_session`: files, parts of long recordings, edits that persist, export naming, matched loudness across files,
  gathering one-shots for the kit room (the same cuts as EXPORT).
* `test_kit`: the kit room's model without any framework: choosing a kit (each sound once, locked lanes kept),
  layers (offset, pitch, trim, reverse, no clipping), the audition player (32 steps in two bars, under the ceiling,
  silent after STOP), the dealer and RANDOM (in a pool of 2,000, every sound is dealt before the first repeat),
  Clear lanes.
* `test_gen`: the generators LOOPS shares with MidiJaQ: the same seed gives the same numbers on every platform,
  déjà vu, the shift register, Euclidean rhythms, conditions, swing, the 32 scales.
* `test_loops`: rolling in every mode, locks and re-rolls, borrowed rhythms, the player against the offline render
  (sample for sample), tails wrapped round, MIDI Trigger and Slices.
* `test_keys`: an instrument from seven horns (zones by pitch, a velocity layer, round robins), in tune within a
  tenth of a cent from C1 to C6, loops and holds that sustain without a click and stay in tune, the release, the
  resonator (in tune within 2 cents from A1 to A5, dark or bright; a kick rings at the key, not at its boom; it
  glides), chops on keys, mono with glide.
* `BatchcutHostTest` (Linux): the plugin and its editor driven as a DAW would, with the real mouse handlers, the
  kit room end to end (gather, auto-fill, layers, undo, rate, save `.NJQ`, read it back, the kit in the project),
  RANDOM, Clear lanes, Remove and Clear all (with an export under way), LOOPS (roll, edit, play from MIDI, every
  export, the loop in the project), KEYS (build, play from MIDI in tune, chords, save and open a `.NJQ` instrument,
  the instrument in the project, keys let go on leaving the room), MATERIAL (an audio file dropped and fitted four
  ways into the pool and into learner-format files that strike again to the same sound, a pool sound fitted and
  cancelled, a fit finishing with the window closed; Wander, Keep and Grow by their buttons and tiles), and a long
  random session in all five rooms. The
  `.NJQ` files it writes are loaded and played by NeoBeat 3.0's own host test.
* `test_learner` (Linux, not part of the Windows build): the learner's fit as ported, checked stage by stage against
  the MaterialLab learner itself on three of its training clips (`tools/learner_ref/README.md`).
* pluginval at strictness 10 on the Windows build.

## Batchcut, NeoBeat and MidiJaQ

They are separate programs. They share one file format and one learner, through a handful of headers kept
identical in every source tree (`Source/neobeat/` here): the sequencer and its 128 patterns, the sound analysis,
the learner, the loudness meter, the generators and the `.NJQ` reader and writer. None needs another installed.

`.NJQ` files are version 2 from 3.0 on. A file says what it is: a **kit** (16 lanes and a pattern, from the kit
room), a **loop** (the same, plus the loop's layers) or an **instrument** (from
KEYS, for MidiJaQ). NeoBeat 3.0 opens kits and loops (and older, version 1 files); instruments it leaves to
MidiJaQ.

## Building

```
cmake -B build                      # fetches JUCE 8.0.15 (or -DJUCE_DIR=path/to/JUCE)
cmake --build build --config Release --target Batchcut_VST3 Batchcut_Standalone test_engine test_session test_kit test_gen test_loops test_keys
```
