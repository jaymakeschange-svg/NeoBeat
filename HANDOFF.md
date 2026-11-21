# The Neosystem — state of the project, 21 November 2026

**Neosystem: an experimental new jack swing sonic research project.**

A complete handover for whoever continues this work, human or AI. It assumes C++ and JUCE
knowledge and nothing about the project.

**Read §5 (Mistakes) before writing any code.** Every bug found on 20–21 November had one cause,
and it is easy to walk into.

---

## 1. What the Neosystem is

**Neosystem is an experimental new jack swing sonic research project** — a family of JUCE 8
plugins built to make the music the research is about.

The guiding idea behind the sound: *sounds that defy physics yet are modelled after physical
things* — imagine a universe where SOPHIE and Aphex Twin timbres are the default presets of a
physical modelling synth. The research is in manipulating models to add impossible depth, space,
body and buoyancy. Native kits should sound like synthesised city and industrial noise: metallic,
rubbery, latex percussion.

### The hierarchy

```
            Batchcut            the preset editor — used before opening a DAW
           /        \
     NeoBeat        MidiJaQ     equal, under it: drums / melodic
           \        /
           JaqRack              the rack host, under those
```

**The ecosystem rule, as stated:** *"features one has, from the interface to the workflow,
should be interconnected to the other. If Batchcut has drag and drop, so does NeoBeat. If NeoBeat
has right-click send-to, so does MidiJaQ."* Features propagate **without being asked**. This is the
architecture, not a preference.

### The plugins

| Plugin | Role | Branch |
|---|---|---|
| **Batchcut** | Detects events in recordings, cuts one-shots, builds kits, loops and instruments. The preset editor for the whole family. | `batchcut-3.0` |
| **NeoBeat** | Drum sequencing. Hybrid synth workstation. Plays Batchcut's presets. | `neobeat-3.0` |
| **MidiJaQ** | Melodic sampling sequencer. Plays Batchcut's presets. | `midijaq` |
| **JaqRack** | Rack host — all three as lighter modules playing in unison. | not started |
| **Bounce Delay** | Standalone FX (also inside NeoBeat) | needs updating |
| **Friction Stretch** | Standalone FX (also inside NeoBeat) | needs updating |

### File formats

| Extension | What it is | Written by | Read by |
|---|---|---|---|
| `.NJQ` | Sequence / kit | Batchcut, NeoBeat | NeoBeat, MidiJaQ |
| `.NJQi` | Keys / instrument | Batchcut | MidiJaQ only |
| `.njqloop` | Loop preset (**new, 21 Nov**) — round-trips exactly, audio embedded as FLAC | MidiJaQ | MidiJaQ |
| `.JRQ` | Rack patch | JaqRack | JaqRack |

### Workflow

Batchcut before Ableton is even open. NeoBeat and MidiJaQ are where the presets get played, edited
and effected.

---

## 2. Build status

### Batchcut — working, in use, do not break

| Feature | Status |
|---|---|
| Event detection, one-shot cutting | done |
| CUT / KIT / LOOPS / KEYS rooms | done |
| Drag and drop (window-wide) | done |
| `.NJQ` / `.NJQi` export, MIDI export, clear-all | done |
| Undo/redo | done |
| Techno and house presets | **not done** |
| Presets moved across to MidiJaQ | **not done** |

Six tests pass including a host test with 700 random actions. **In daily use.** Ask before
changing anything in it.

### MidiJaQ — rebuilt 20–21 Nov, this is where the work went

| Feature | Status |
|---|---|
| Seven rooms: CUT, LOOPS, KEYS, MAP, MOD, RANDOM, STEREOROOM | done |
| CUT/LOOPS/KEYS inherited from Batchcut (shared code) | done |
| Audio import (ADD, FOLDER, detection, cut to pool, classification) | **fixed 21 Nov** — was broken four ways |
| Drag and drop, window-wide | **fixed 21 Nov** — never worked |
| MAP drives playback (row = scale degree, loop mark = chop) | done, `test_map` |
| Per-chop placement in the Stereoroom audio path | done, `test_chops` |
| Depth as a real room (`nsr::RoomVerb`) | done, `test_depth` |
| Plugin state saves the rooms, map and stage | done, `test_state` — **saved nothing before 20 Nov** |
| Preset bars in KEYS and LOOPS (arrows, hover audition) | done |
| `.njqloop` round-trip preset format | done |
| On-screen keyboard plays | **fixed 21 Nov** — was silent |
| Right-click "send to" from NeoBeat | advertised in MOD, **never verified** |
| Any preset content shipped | **none** — the preset bar reads an empty folder |
| Orb, modulation, randomiser rooms | built, **lightly tested** |

Fourteen tests pass. The Windows build is green.

### NeoBeat — untouched this session

| Feature | Status |
|---|---|
| Sequencer, Matrixxxer, Layermax, effects | from earlier work |
| Stereoroom engine present in its shared tree | yes, **UI not built** |
| Stereoroom UI | **not done** |
| Drag and drop | **unaudited** — may have the same bugs as MidiJaQ did |
| Preset bar | **not done** |
| Orb | deferred |
| Bounce Delay smoothing | **not done** |
| Techno and house presets | **not done** |

### JaqRack — not started

---

## 3. What was done on 20–21 November

In order.

1. **The Stereoroom engine** (`nsr::` in `StereoRoom.h`): `Place`, `Stage`, `Travel`
   (Still/Sway/Swing/Circle/Drift), equal-power pan referred to the middle (×√2, so switching the
   room on changes nothing and moving doesn't change loudness), mid/side width, mono lock by
   measured content rather than by name.
2. **Per-chop placement wired into the audio path.** A loop voice carries which chop it is and the
   placement that chop was given, with its own filter state. "Ear to ear" is successive hits
   landing in different places. `test_chops`.
3. **`nsr::RoomVerb`** — the room behind the depth axis. Four delay lines of coprime length mixed
   by a Hadamard matrix (orthogonal, so decay is set by loop gain rather than luck), one-pole
   damper per loop, stereo send so a chop's room arrives from the chop's side. Asked for a 1.5 s
   RT60, measured −60.4 dB at 1.5 s. A far chop returns **19 dB more room than a near one, level
   for level** — the cue a volume knob cannot fake. `test_depth`.
4. **The MAP plays.** A row is a degree of the key, so a mark cannot be out of key; a loop mark
   fires one chop. One grid, both sources, each mark carrying its own. `test_map`.
5. **The inherited rooms were rendered at all.** LOOPS and KEYS had models and players but
   `processBlock` never rendered either — they drew correctly and made no sound.
6. **Plugin state.** Pool, loop layers, keys instrument, map marks and stage nodes now survive
   save and reload. Before this, building a loop and saving your Ableton set lost everything.
   `test_state`.
7. **Preset bars** in KEYS and LOOPS: arrows load as you step, the list loads on hover and
   restores the room if you move away without choosing.
8. **`.njqloop`** — LOOPS could only export a one-way NeoBeat kit, so no preset could come back.
9. **Drag and drop fixed**, window-wide, routed per room.
10. **Audio import fixed** — four separate faults, see §5.
11. **`bc::Rooms::wireSession()`** — the loader's four callbacks moved into shared code.
12. **A headless GUI harness** (§4) — how every UI bug was actually found.

---

## 4. The code

### Worktrees

```
/home/claude/midijaq-wt     midijaq         build: /home/claude/mjbuild
/home/claude/batchcut-wt    batchcut-3.0    build: /home/claude/bcbuild
```

All plugins live in one GitHub repo (`<owner>/NeoBeat` — the clone URL is in each worktree's
`git remote -v`), as **orphan branches** — one tree per
plugin, no shared history. **Never push to `main`.**

### Shared code — copied, not submoduled

```
Source/neobeat/shared/      (Batchcut, MidiJaQ)
Source/shared/              (the same files in NeoBeat)
```

| File | Holds |
|---|---|
| `rooms/Rooms.h` | `bc::Rooms` — the KIT/LOOPS/KEYS/CUT models, players, ~55 actions, `wireSession()`. Both processors inherit it. |
| `rooms/RoomView.h` | `bc::RoomView` — the minimal view a room UI needs |
| `rooms/CutRooms.cpp` | Files side: `addPaths`, `isAudioFile`, `wireSession` |
| `rooms/{Kit,Loops,Keys}Rooms.cpp` | Model logic and XML round-trips (audio embedded as FLAC) |
| `rooms/*RoomUi.cpp` | The three shared room interfaces |
| `rooms/Session.h` | The async file loader — where the import bugs were |
| `rooms/PresetBar.{h,cpp}` | Preset bar with hover audition |
| `StereoRoom.h` | `nsr::` — placement, travel, `RoomVerb`, `BassMono` |

**After editing any shared file:** copy to the other trees, rebuild each, run each one's tests,
confirm with `cmp -s`. No tooling does this for you.

### MidiJaQ-only

```
Source/app/Processor.{h,cpp}    MjqProcessor : AudioProcessor, bc::Rooms, ChangeBroadcaster, AsyncUpdater
Source/app/Editor.{h,cpp}       seven rooms, 1280x800
Source/app/CutRoom.{h,cpp}      ** every bug of 20-21 Nov was here **
Source/app/MapRoom.{h,cpp}      the grid
Source/app/ModRoom.{h,cpp}      the Orb, sources, routes
Source/app/RandomRoom.{h,cpp}   global randomiser with HOLD
Source/app/StereoRoomUi.{h,cpp} per-chop nodes on a perspective stage
Source/core/Map.h               mjq::Mark, MapPattern, noteOfRow()
```

Rooms are numbered 0–6: CUT, LOOPS, KEYS, MAP, MOD, RANDOM, STEREOROOM.

**Naming trap:** `MjqProcessor`'s own session is called `song`, because a member named `session`
would silently shadow `bc::Rooms::session`.

### Build and test

```bash
cd /home/claude/mjbuild && ninja                     # everything
cd /home/claude/mjbuild && ninja MidiJaQ_Standalone  # just the app, fast
```

Fourteen tests: `test_jaq test_clock test_session test_orb test_room test_random test_evolving
test_inst test_audio test_chops test_map test_depth test_state test_endtoend`.

`test_endtoend` needs patches:
`/home/claude/bcbuild/BatchcutMakePatches_artefacts/Release/BatchcutMakePatches patches`.

Windows builds run on GitHub Actions on push to the plugin's branch (~38 min). It is slow because
eight test targets each recompile the whole app — collapsing them into an OBJECT library is easy
and worthwhile.

### Running the GUI headlessly — do this

The most valuable thing set up this session, and how every UI bug was found:

```bash
Xvfb :99 -screen 0 1400x900x24 &
cd /tmp/testaudio && DISPLAY=:99 .../Standalone/MidiJaQ &
DISPLAY=:99 import -window root /tmp/shot.png    # then read the png
python3 /tmp/click.py 124,149                     # click x,y via python-xlib XTEST
```

Typing via XTEST was unreliable; navigate file choosers by launching the app with its working
directory set to the folder you want.

**Test audio must have sharp transients separated by silence.** Single decaying sine blips give
"0 sounds" because the detector correctly finds no onsets — bad test data, not a bug.

---

## 5. Mistakes — read this

### The one cause

**Every bug found on 20–21 November was in `Source/app/CutRoom.cpp`, the one file written fresh
instead of sharing Batchcut's working code.** Not one was in a shared file.

MidiJaQ was supposed to *inherit* CUT, LOOPS and KEYS. That was done for the models and three room
UIs. But Batchcut's CUT-room logic lives partly in **its Editor and Processor**, not in a room
file — so building MidiJaQ's CUT room meant rewriting that part, and the rewrite produced five
bugs that shipped to the user in a plugin that could not import audio at all.

### The bugs

1. **Drag and drop never worked.** `CutRoom` inherited `juce::FileDragAndDropTarget` **privately**.
   JUCE finds drop targets with `dynamic_cast<FileDragAndDropTarget*>`; a cast to a non-public base
   yields null. The handlers compiled, linked, and were never called. Now guarded by
   `static_assert` on the conversion in both `CutRoom.cpp` and `Editor.cpp`.
2. **The chooser listed nothing.** `launchAsync` called with `openMode | canSelectMultipleItems` —
   **`canSelectFiles` missing**. Without it (or `canSelectDirectories`) JUCE's browser may show
   nothing, so ADD opened onto an empty list in a folder full of wavs.
3. **No worker thread.** `session.start()` never called — every file queued behind "Reading files"
   for ever.
4. **The loader could not read audio.** `bc::Session` is deliberately format-agnostic and takes
   `prober`, `loader`, `writer`, `makeDir` callbacks. MidiJaQ set none, so every read reported
   "failed". `applyResults()` was never called either, so finished work would have stayed queued.
5. **The KEYS on-screen keyboard was never read** in `processBlock` — the room's own keys were
   silent.

Fixed structurally: the four callbacks now live in **`bc::Rooms::wireSession()`** in the shared
tree. Batchcut still has its own copy in its processor — **consolidating that is a good first
task**, with permission.

### The deeper mistake

"Fourteen tests pass" and "pluginval passed at strictness 10" were reported as evidence the plugin
worked. Both true. Both **worthless for these bugs — no test in that suite can see UI wiring.**
The parts were verified and the whole was inferred, and four broken features were found in
software that had been reported as ready.

**Never report green tests as evidence a feature works. Run the plugin and click the thing.** It
takes two minutes and found all of this.

### Smaller errors worth inheriting

- `test_chops` reported a travelling chop as "+103 dB" — not a loudness or a position, but a
  channel ratio floored at `1e-9`, so its magnitude was set by the epsilon. **This was caught in review, not by the suite.** It now recovers pan position with `atan2` over the two channels, bounded −1..+1, which
  can also test that a sway is symmetric.
- Tests have passed **vacuously on empty lists** more than once. Guard every loop-based assertion
  with a non-empty check.
- A grep of guessed symbol names gave "two dependencies" where there were four. Ask the compiler.
- Private inheritance is correct for `ListBoxModel` (handed to the `ListBox` explicitly) and wrong
  for anything JUCE finds by `dynamic_cast`.

---

## 6. Research

Two full reports are in `docs/research/` in this branch.

### Stereo placement and 3D mix visualisation

Everything the Stereoroom needs can be written from permissive parts, and should be. For 4–16
elements to ordinary stereo the whole placement engine is: a pair of time-varying gains per
element, a side-channel high-pass for mono-locked ones, a small inter-channel delay for those that
can take it, and **one shared reverb send that is O(1) in element count**.

The two techniques that look like the obvious answer are both wrong here on documented grounds:
**HRTF binaural** cues are destroyed by loudspeaker crosstalk and need a sweet spot of about ±4°;
**ambisonics** adds an encode/decode round-trip and a normalisation convention to get wrong, for
capability this product doesn't use.

This is implemented. `nsr::RoomVerb` is the shared send; `BassMono` is the side-channel high-pass.

### New jack swing parts and grooves

The style has **no documented numeric specification**, but the machine that made it does, and that
asymmetry is how to write the style with entirely different sounds:

- The **Akai/Linn swing percentage** is the share of each eighth-note pair given to the *first*
  sixteenth, 50% (straight) to 75% (3:1), and **it only ever delays the even-numbered sixteenths**
  (Akai MPC3000 manual, p.63).
- There is **no documented tempo range** for the style anywhere in the literature. The report
  supplies none, deliberately.
- One quantitative study of a neighbouring repertoire (30 sampled drum breaks): median swing
  **1.2:1 ≈ 54.5%**, uncorrelated with tempo, snare late on beat 2 but **not** beat 4.

Useful when writing the techno/house and new-jack-swing presets (§8).

### Local AI, for continuing without credits

- The deciding variable is **VRAM**, not the model. Exceeding it by ~2 GB drops you from ~50 tok/s
  to ~5.
- **24 GB card or 32 GB+ Mac → Qwen3-Coder 30B-A3B** (~19 GB in Ollama, 256K context, Apache 2.0,
  trained for the Cline/Aider tool-call format).
- **12 GB → 13B/14B coder models only**; 30B does not fit usefully, and a 4K context is close to
  useless on a codebase this size.
- Harness: Ollama or LM Studio serving, **Aider** (terminal, closest to this workflow) or **Cline**
  (VS Code) driving.
- **Honest expectation:** the best open weights score ~46 on Artificial Analysis against Opus 5.5
  at 58, and those are models too large for one GPU. A 30B on a desk is below that again. It will
  write and edit single files well and **lose the thread on cross-file work** — which is exactly
  what this project is made of (three trees, byte-identical shared headers).
- The bigger practical win: run the agent **on the development machine itself**, where the loop collapses from a
  38-minute CI build to seconds.

---

## 7. Skins — planned, not started

No skinning system exists yet. The stated direction, to be implemented once the builds are
functional (deferred deliberately, for efficiency):

- **Y2K chrome / green-silver skinned desktop** — the NeoBeat reference
- **Pearlescent circuit board** with silver traces and rhinestones
- **MOTU-style brushed hardware**
- **Sculpted organic relief art**
- General taste: deep contours, interesting palette, darker tones, organic, mechanic, expressive.
  Some references flat, others 3D. Reference board: `https://pin.it/4W8acWDLM`
- **Everything should be a skin** — i.e. a swappable theme layer, not hard-coded colours.

Current state: `nsui::setSkin (nsui::skins::chromePearl())` exists in MidiJaQ and there is an
`nsui::C::` colour layer, so the hook is there. A real skin system means auditing every room for
hard-coded colours. Batchcut can share NeoBeat's skin.

---

## 8. What is left, in order

1. **Verify the Windows build the user downloads.** Run it, import audio, roll a loop, play it.
   Do not trust the suite.
2. **Audit NeoBeat for the same five bugs.** It is the only tree not checked, and it was built the
   same way.
3. **Consolidate Batchcut onto `wireSession()`** and move Batchcut's CUT-room drop handling into
   the shared tree. This is the fix that ends the bug class. **Ask first** — Batchcut works.
4. **Propagate to NeoBeat:** drag and drop, preset bar, Stereoroom UI (engine already in its tree).
5. **Right-click "send to"** — NeoBeat has it, MidiJaQ advertises it in MOD; **never verified**.
6. **Presets.** MidiJaQ ships with none. Move Batchcut's across, then write new techno and house
   patterns for Batchcut and NeoBeat — all grounded in new jack swing (§6 has the swing arithmetic).
7. **Bounce Delay and Friction Stretch** — both need updating and building as standalones.
8. **JaqRack** — the rack host. Each plugin inserted as a lighter module, multiple instances in a
   rack, like Propellerheads ReBirth RB-338. The NeoBeat module keeps only a basic drum sequencer
   (loads `.NJQ`, basic edits); its effects live in the rack instead, like hardware split into
   budget rack units with external effects. JaqRack has an **FX Matrixxxer** controlling everything
   in it. Patches save as `.JRQ`.
9. **Skins** (§7).
10. **CI speed** — the OBJECT library change.

### Known gaps, stated honestly

- **Loose pool sounds are not saved.** `kitToXml` writes only what a lane uses — deliberate in
  Batchcut, possibly wrong for MidiJaQ where the pool is the MAP's working set. **Not yet decided.**
- **`BassMono` covers the Stereoroom's output only**, not the whole mix. Applied globally it
  changed how every existing preset pans (a hard-left voice went from >12 dB to 9.2 dB), so it was
  scoped. **Owner's call.**
- **The MAP has had no musical use** beyond tests.
- **Right-click sends, the Orb, the randomiser, and most of LOOPS/KEYS are unverified by hand.**
- **Open question never answered:** should MidiJaQ export MIDI files? (Batchcut writes MIDI;
  NeoBeat exports MIDI sequences; neither sends live MIDI — a Max for Live device will do that
  later.)

---

## 9. Working practices

- **The ideas come from the project owner; you assist and code.** Do not redesign the concepts.
- **Ask before changing any existing feature of an existing plugin.** Batchcut especially.
- **Ask before long builds. Commit and push only when asked**, to the plugin's own branch, never
  `main`.
- Do not keep records of files on the owner's computer unless the task needs it.
- **Propagate features across the family without being told** (§1).
- Constraints: no DRM circumvention, no reproducing proprietary code. Iftah's **Sting** and **Art
  DSP Canvas** are studied **by behaviour only** — their code and visual design stay out. The Wave
  emulator is AGPL: reimplement principles, never copy. Don't read AAS licence files or type
  serials.
- Work is done under constrained time and budget. **Batch verification; do not spend a round trip
  per bug. Be blunt about what you have not tested.**

---

## 10. Git state at handoff

`midijaq` — latest commit "Handoff: where the code is, what broke, what is next". Fourteen tests
pass, Windows build green.

`batchcut-3.0` — pushed and green, **but** `Rooms.h` and `CutRooms.cpp` were copied into
`/home/claude/batchcut-wt` after its last commit and are **uncommitted and unverified there**.
Rebuild Batchcut and run its six tests (host test included) before committing that tree.

`neobeat-3.0` — untouched this session.
