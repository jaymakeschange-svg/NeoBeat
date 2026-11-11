# Batchcut

Batchcut finds the events in a recording (a ball bouncing in a noisy yard, the hits of a drum loop, clanks in a
field recording) and cuts them into one-shots, as many files as you like at once. It is a standalone app and a
VST3 plugin.

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
you hear is what is exported: mute the background to export clean hits.

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
  (Wang et al. 2017; Lostanlen et al., IEEE SPL 2019), so wind and passing traffic do not read as events.
  Starts are placed to about 1 ms with a second, short analysis window; strikes closer than the main window
  resolves (the last bounces of a ball) are found on that short window too.
* Layers: median-filter harmonic/percussive separation with a residual (Fitzgerald, DAFx 2010; Driedger et al.,
  ISMIR 2014), resynthesised by overlap-add (an untouched recording comes back unchanged).
* Groups and FIND MORE: a description of each sound (cepstrum of its band profile, centroid, flatness, decay,
  tonal and transient share, with what was already sounding taken off) and where its onset rose, clustered by
  leader clustering and k-means.
* Loudness: ITU-R BS.1770 K-weighting at every sample rate, momentary (400 ms) loudness.

## Tested

* `test_engine`: detection on recordings with known starts (bounces under noise, ticks in traffic rumble, a dense
  drum loop: 64 of 64 found, starts within 2 ms), layers (an untouched recording comes back to -139 dB), edits,
  export, the preview player, the BS.1770 meter against the Recommendation's tables and reference tone.
* `bench_detect`: harder field scenes (gusty wind, passing cars, talking, rain, a hall, bounce trains).
* `test_session`: files, parts of long recordings, edits that persist, export naming, matched loudness across files.
* `BatchcutHostTest` (Linux): the plugin and its editor driven as a DAW would, with the real mouse handlers, and
  a long random session.
* pluginval at strictness 10 on the Windows build.

## Building

```
cmake -B build                      # fetches JUCE 8.0.15 (or -DJUCE_DIR=path/to/JUCE)
cmake --build build --config Release --target Batchcut_VST3 Batchcut_Standalone test_engine test_session
```
