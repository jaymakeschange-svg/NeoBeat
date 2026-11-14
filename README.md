# Batchcut 2.0

Batchcut finds the events in a recording (a ball bouncing in a noisy yard, the hits of a drum loop, clanks in a
field recording) and cuts them into one-shots, as many files as you like at once. It is a standalone app and a
VST3 plugin.

**New in 2.0: the kit room.** The **CUT | KIT** switch at the top opens a second room that builds a drum kit for
NeoBeat out of the one-shots: it works out which of them could be a kick, a snare, a hat and so on, lets you hear
and change every pick, layers up to three sounds on a lane, plays the kit inside NeoBeat's 128 patterns, and saves
kit, pattern and tempo as one `.NJQ` file that NeoBeat loads.

## Quick start

1. Drop recordings or whole folders onto Batchcut (or **ADD FILES**). The first one opens and its events are found.
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
tag), how usable it is, how loud it is. The list shows what fits the selected lane first.

**2. Fill the lanes** (middle). The 16 lanes are NeoBeat's: kick, kick 2, snare, snare 2, closed hat, open hat,
hat 3, rim, thud, clank, junk, drop, horn, stab, bell, FX. **AUTO-FILL** puts the best fit on every lane, each sound
once and no two alike. **SHUFFLE** makes another kit from the same pool. **LOCK** keeps a lane through both.
**<** and **>** step a lane through its candidates; double-clicking a pool sound puts it on the selected lane.
**M** mutes a lane in the pattern.

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
click. Kit room keys: Space plays, up and down walk the lanes, left and right step through candidates, Enter hears
the lane, Ctrl+Z / Ctrl+Shift+Z undo and redo changes to the lanes.

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
  silent after STOP).
* `BatchcutHostTest` (Linux): the plugin and its editor driven as a DAW would, with the real mouse handlers, the
  kit room end to end (gather, auto-fill, layers, undo, rate, save `.NJQ`, read it back, the kit in the project)
  and a long random session. The `.NJQ` it writes is loaded and played by NeoBeat 3.0's own host test.
* pluginval at strictness 10 on the Windows build.

## Batchcut and NeoBeat

The two are separate programs. They share one file format and one learner, through a handful of headers kept
identical in both source trees (`Source/neobeat/` here): the sequencer and its 128 patterns, the sound analysis,
the learner, the loudness meter and the `.NJQ` reader and writer. Neither needs the other installed.

## Building

```
cmake -B build                      # fetches JUCE 8.0.15 (or -DJUCE_DIR=path/to/JUCE)
cmake --build build --config Release --target Batchcut_VST3 Batchcut_Standalone test_engine test_session test_kit
```
