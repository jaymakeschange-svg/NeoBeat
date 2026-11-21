# MidiJaQ handoff

Written 21 November 2026 for whoever picks this up next. It assumes you know C++ and JUCE and
nothing about this project.

Read the "Mistakes" section before you write any code. Every bug in this plugin on 20–21 November
came from the same cause, and it is a cause you can walk straight into.

---

## 1. What this is

Joshua Marshall (Marbelite) is building the **Neosystem**, three JUCE 8 plugins that share code:

| Plugin | What it is | Branch |
|---|---|---|
| **Batchcut** | The preset editor. Finds events in recordings, cuts one-shots, builds kits/loops/instruments. Used *before* opening a DAW. | `batchcut-3.0` |
| **NeoBeat** | Drum sequencing, plays Batchcut's presets | `neobeat-3.0` |
| **MidiJaQ** | Melodic sampling sequencer, plays Batchcut's presets | `midijaq` |

Plus **JaqRack**, a host for all three, not started.

All three live in one GitHub repo, `jaymakeschange-svg/NeoBeat`, as **orphan branches** — one tree
per plugin, no shared history. Never push to `main`.

**The ecosystem rule, in his words:** *"features one has, from the interface to the workflow,
should be interconnected to the other. If Batchcut has drag and drop, so does NeoBeat. If NeoBeat
has right-click send-to, so does MidiJaQ."* You are expected to propagate features without being
asked. This is not a nice-to-have; it is the architecture.

---

## 2. Where the code is

Worktrees on the build machine:

```
/home/claude/midijaq-wt     midijaq branch      /home/claude/mjbuild   (ninja)
/home/claude/batchcut-wt    batchcut-3.0        /home/claude/bcbuild   (ninja)
```

### Shared code, byte-identical across trees

This is the most important structural fact. These files are **copied, not submoduled**, and must
stay byte-identical:

```
Source/neobeat/shared/           (in Batchcut and MidiJaQ)
Source/shared/                   (the same files in NeoBeat)
```

Key pieces:

| File | What it holds |
|---|---|
| `rooms/Rooms.h` | `bc::Rooms` — base class with the KIT/LOOPS/KEYS/CUT models, players, ~55 actions. Both processors inherit it. |
| `rooms/RoomView.h` | `bc::RoomView` — the minimal view a room UI needs |
| `rooms/CutRooms.cpp` | Files side: `addPaths`, `isAudioFile`, `wireSession()` |
| `rooms/KitRooms.cpp` `LoopsRooms.cpp` `KeysRooms.cpp` | Each room's model logic and XML round-trip |
| `rooms/*RoomUi.cpp` | The three shared room interfaces |
| `rooms/Session.h` | The async file loader — see §4, this is where today's bugs were |
| `rooms/PresetBar.h/.cpp` | The preset bar (arrows, hover audition) |
| `StereoRoom.h` | `nsr::` — placement, travel, `RoomVerb`, `BassMono` |

**After editing any shared file:** copy it to the other trees, rebuild each, run each one's tests,
and verify with `cmp -s`. There is no tooling that does this for you. It is manual and easy to
forget.

### MidiJaQ-only code

```
Source/app/Processor.{h,cpp}   MjqProcessor : AudioProcessor, bc::Rooms, ChangeBroadcaster, AsyncUpdater
Source/app/Editor.{h,cpp}      MjqEditor — seven rooms, 1280x800
Source/app/CutRoom.{h,cpp}     ** every bug of 20-21 Nov was in this file **
Source/app/MapRoom.{h,cpp}     the grid: each mark carries its own source
Source/app/ModRoom.{h,cpp}     the Orb, modulation sources and routes
Source/app/RandomRoom.{h,cpp}  global randomiser with HOLD
Source/app/StereoRoomUi.{h,cpp} per-chop nodes on a perspective stage
Source/core/Map.h              mjq::Mark, MapPattern, noteOfRow()
```

Rooms are numbered 0–6: CUT, LOOPS, KEYS, MAP, MOD, RANDOM, STEREOROOM.

---

## 3. How to build and test

```bash
cd /home/claude/mjbuild && ninja                    # everything
cd /home/claude/mjbuild && ninja MidiJaQ_Standalone  # just the app (fast)
```

Fourteen tests, all currently passing:

```
test_jaq test_clock test_session test_orb test_room test_random test_evolving
test_inst test_audio test_chops test_map test_depth test_state test_endtoend
```

`test_endtoend` needs patches: generate with
`/home/claude/bcbuild/BatchcutMakePatches_artefacts/Release/BatchcutMakePatches patches`,
then `./test_endtoend_artefacts/Release/test_endtoend patches`.

Windows builds run on GitHub Actions on push to `midijaq` (~38 min; it is slow because eight test
targets each recompile the whole app — collapsing them into an OBJECT library is low-hanging
fruit). Artifacts are on the run page; the user downloads them himself.

### Running the plugin headlessly — do this, it matters

The most valuable thing set up this session. You can run the actual GUI and look at it:

```bash
Xvfb :99 -screen 0 1400x900x24 &
cd /tmp/testaudio && DISPLAY=:99 /home/claude/mjbuild/MidiJaQ_artefacts/Release/Standalone/MidiJaQ &
DISPLAY=:99 import -window root /tmp/shot.png     # then Read the png
python3 /tmp/click.py 124,149                      # click at x,y (python-xlib, XTEST)
```

`/tmp/click.py` takes `x,y` pairs. Typing via XTEST did not work reliably; navigate choosers by
launching the app with its cwd set to the folder you want.

Test audio: generate files with several sharp transients separated by silence. Single decaying
sine blips produce "0 sounds" because the event detector correctly finds no onsets — that is not
a bug, it is bad test data.

---

## 4. Mistakes I made — read this part

### The one cause

**Every bug found on 20–21 November was in `Source/app/CutRoom.cpp`, the one file I wrote fresh
instead of sharing Batchcut's working code.** Not one was in a shared file.

The user's instruction was that MidiJaQ *inherits* CUT, LOOPS and KEYS from Batchcut. I did that
for the models and three room UIs. But Batchcut's CUT room logic lives partly in **its Editor and
Processor**, not in a room file — so when I built MidiJaQ's CUT room I rewrote that part. Four
separate bugs came out of that rewrite, and the plugin shipped to the user unable to import audio
at all.

### The four bugs

1. **Drag and drop never worked.** `CutRoom` inherited `juce::FileDragAndDropTarget` **privately**.
   JUCE finds drop targets with `dynamic_cast<FileDragAndDropTarget*>`, and a cast to a non-public
   base yields null. The handlers compiled, linked and were never called. Guarded now with a
   `static_assert` on the conversion, in both `CutRoom.cpp` and `Editor.cpp`.

2. **The file chooser listed nothing.** `launchAsync` was called with
   `openMode | canSelectMultipleItems` — **`canSelectFiles` missing**. Without it (or
   `canSelectDirectories`) JUCE's browser has nothing it is permitted to show, so ADD opened onto
   an empty list in a folder full of wavs.

3. **No worker thread.** `session.start()` was never called, so every file queued behind
   "Reading files" for ever.

4. **The loader had no idea how to read audio.** `bc::Session` is deliberately format-agnostic: it
   takes `prober`, `loader`, `writer`, `makeDir` callbacks. MidiJaQ set none, so every read
   reported "failed". It also never called `applyResults()`, so finished work would have stayed in
   the queue regardless.

Fixed properly: the four callbacks now live in **`bc::Rooms::wireSession()`** in the shared tree.
Any plugin inheriting `bc::Rooms` calls `wireSession()`, sets `session.onChange`, calls
`session.start()`, and gets a working loader. Batchcut still has its own copy in its processor —
**consolidating that is a good first task** (see §6).

### The deeper mistake, and what to do instead

I reported "fourteen tests pass" and "pluginval passed at strictness 10" as evidence the plugin
worked. Both true. Both worthless for these bugs — **not one of those tests can see UI wiring**.
I verified the parts and inferred the whole, and the user found four broken features in software
I had told him was ready.

**Do not report green tests as evidence that a feature works.** Run the plugin (§3) and click the
thing. That is how every one of these was found, and it takes two minutes.

### Smaller errors worth knowing

- `test_chops` reported a travelling chop as "+103 dB", which is not a loudness or a position. It
  was a channel ratio floored at `1e-9`, so its magnitude was set by the epsilon. The user caught
  it. It now recovers the pan position with `atan2` over the two channels, bounded −1..+1, which
  can also check that a sway is symmetric — something the old number could not ask.
- Tests have passed vacuously on empty lists more than once (comparing nothing to nothing). Guard
  every loop-based assertion with a non-empty check.
- A `session` member on `MjqProcessor` would have silently shadowed `bc::Rooms::session`.
  MidiJaQ's own is named `song`. Watch for this when adding members to either processor.
- I once concluded "two dependencies" from a grep of guessed symbol names; there were four. Ask
  the compiler, not grep.

---

## 5. What works now (verified by running it)

- **CUT**: ADD and FOLDER import; detection finds events; CUT THEM ALL fills the pool; the pool
  classifies sounds (kicks/rims/stabs/FX); CLEAR ALL; drag and drop anywhere in the window
- **LOOPS**: sees the pool grouped by file and by kind; ROLL ALL lays chops; preset bar
- **KEYS**: builds instruments; on-screen keyboard plays; preset bar over `.NJQi`
- **MAP**: marks play — a row is a scale degree, a loop mark fires a chop (`test_map`)
- **STEREOROOM**: per-chop placement in the audio path (`test_chops`), with a real room behind the
  depth axis (`test_depth`, `nsr::RoomVerb` — 4-line Hadamard FDN, RT60 measured at −60.4 dB when
  set to 1.5 s)
- **State**: pool, loops, instrument, map marks and stage nodes all survive save/reload
  (`test_state`). Before 20 Nov none of this was written at all.

---

## 6. Next steps

In the order I would do them.

1. **Verify the Windows build the user actually downloads.** Run the standalone, import audio,
   roll a loop, play it. Do not trust the test suite for this.
2. **Consolidate Batchcut onto `wireSession()`** and move Batchcut's CUT room drop handling into
   the shared tree. This is the fix that stops the bug class. *Ask the user before changing
   Batchcut* — he has a standing rule about that, and Batchcut currently works.
3. **Propagate to NeoBeat** (`neobeat-3.0`): drag and drop, the preset bar, the Stereoroom UI (the
   engine is already in its shared tree). Audit it for the same four bugs — it may well have them.
4. **Right-click "send to"** — NeoBeat has it, MidiJaQ should. MidiJaQ's MOD room already
   advertises it ("right-click anything in any room to send…"); **I never verified it works.**
5. **Presets.** MidiJaQ ships with none, and the preset bar reads an empty folder. The user asked
   for Batchcut's existing presets to be moved to MidiJaQ, plus new techno and house ones for
   Batchcut and NeoBeat, all grounded in new jack swing.
6. **Bounce Delay** needs updating (user's request, details not captured — ask him).
7. **JaqRack** — the host, not started.
8. **CI speed**: eight test targets each recompile the whole app. An OBJECT library would cut the
   38-minute build substantially.

### Known gaps, honestly

- **Loose pool sounds are not saved.** `kitToXml` writes only what a lane uses. Deliberate in
  Batchcut (keeps projects small); possibly wrong for MidiJaQ, where the pool is the MAP's working
  set. The user has been told and has not decided.
- **`BassMono` covers the Stereoroom's output only**, not the whole mix. Applying it globally
  changed how every existing preset pans (a hard-left voice went from >12 dB to 9.2) so I scoped
  it. The user's call.
- **The MAP is wired to playback but has had no musical use** beyond tests.
- **Right-click sends, and anything in LOOPS/KEYS beyond what §5 lists, are unverified.**

---

## 7. Working with the user

- He provides the ideas; you assist and code. Do not redesign his concepts.
- **Ask before changing any existing feature of an existing plugin.** Batchcut especially — it
  works and he depends on it.
- **Ask before long builds. Commit and push only when asked**, to the plugin's own branch, never
  `main`.
- Do not keep records of files on his computer unless the task needs it.
- Standing constraints: no DRM circumvention, no reproducing proprietary code. Iftah's Sting and
  Art DSP Canvas are studied by **behaviour only** — their code and visual design stay out. The
  Wave emulator is AGPL: reimplement principles, never copy.
- He is working under limited credits and his time is expensive. Batch your verification; do not
  spend a round trip per bug. Be blunt about what you have not tested.

---

## 8. Git state at handoff

Branch `midijaq`, latest commit **"Audio import, properly: the loader was never set up at all"**.
All fourteen tests pass. Windows build triggered on push.

`batchcut-3.0` is pushed and green, but `Rooms.h` and `CutRooms.cpp` were copied into
`/home/claude/batchcut-wt` **after** its last commit and are **uncommitted and unverified there**.
Rebuild Batchcut and run its six tests (including the host test's 700 random actions) before
committing that tree.
