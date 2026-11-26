# Proposed features — NeoBeat and MidiJaQ

Running list. Updated as features are proposed, built or dropped. Covers both plugins plus the
shared tree; Batchcut appears only where a change touches it.

Status key: **proposed** (not specced), **specced** (design settled, not built), **building**,
**done**, **open question** (needs a decision before it can be specced).

Last updated 26 November 2026 (revised 21:20 — IDM Jack's two open questions were
already answered in the transferable-mechanisms doc; this list had gone stale within two hours
of being written, and was read in preference to the newer document. See REFERENCES.md.)

---

## New this session

### IDM Jack — both plugins — *specced, with two open questions*

A glitch mode that stays rhythmic. Reference points: Autechre, Aphex Twin.

**The premise, as stated:** most IDM-style glitch comes from generative drum synthesis, which is
uncontrolled and tends to fall back on four-on-the-floor. IDM Jack differs by constraining the
chaos to a lattice.

**The lattice is the whole mechanism.** Glitch events snap to a **16th-triplet swung grid** — six
per beat, twenty-four per bar, with swing displacement applied within triplet groupings. The grid
is **invisible**: not drawn, not editable, not something the user places events on. Events can
only land on it, which is why the result reads as rhythmic rather than broken. Randomness decides
*whether* and *how much*, never *where*.

**Behaviour:**
- Some probability per lattice position that a glitch fires; one amount controls density
- When it fires, a drum part is **repeated rapidly** — N retriggers subdividing the lattice
- **FX are driven by the same process**: the same seed and the same lattice, so an effect jump
  lands on the same instant as the retrigger rather than smearing across it

**Seeding.** One seed per window, with a probability that the next window reuses it (the
`turing` idea from the Chassis study, §2.4 of the teardown). This is what makes a bar of chaos
internally coherent and repeatable instead of noise — and it means a passage can be kept.

**Answered — overlay or replace.** Neither, strictly: it is a **held, non-destructive takeover**.
Chaos while the control is held, the original pattern restored the moment it is released. The
written pattern is never altered, so there is nothing to undo. (Recorded in the transferable-
mechanisms doc, 26 Nov, which supersedes the open question first written here.)

**Answered — what it repeats.** Whatever is sounding. The brief was that the mode should *rapidly
and randomly repeat drum parts* while each sound's own parameters get pitched up, down, driven and
hollowed — so it grabs the kit sound that is playing, not a lane chosen in advance.

**Still open — the third axis.** Micro-timing offset per voice (1–100 ms) came from the Density
study, not from the brief; the doc notes it as "two of yours plus one you had not named". Worth
confirming before it is built.

---

## NeoBeat

| Feature | Status | Note |
|---|---|---|
| IDM Jack | specced | see above |
| Stereoroom UI | proposed | engine already in its shared tree; UI not built |
| Drag and drop | **unaudited** | may carry the same four bugs MidiJaQ had — see HANDOFF §5 |
| Preset bar (arrows, hover audition) | proposed | shared component exists, built for Batchcut/MidiJaQ |
| MATERIAL engine | available | `nsr::Material` now in the shared tree; no UI in NeoBeat yet |
| Orb | deferred | from stage 2 |
| Bounce Delay smoothing | proposed | |
| Techno and house presets | proposed | grounded in new jack swing |
| Tiling percussion engine | proposed | from the Chassis `frappe` study — modes from a geometry, the drawing *is* the instrument |
| Two listening points per voice | proposed | width from the body rather than applied to it |
| 5×5 pattern grid, x/y reading point | proposed | 25 patterns as a continuous space, bilinear over per-step velocities |
| Seed-repeat probability for the randomiser | proposed | replaces a plain amount control |

## MidiJaQ

| Feature | Status | Note |
|---|---|---|
| IDM Jack | specced | see above |
| Right-click "send node to" | **unverified** | MOD room advertises it; never tested |
| Shipped presets | missing | preset bars read an empty folder |
| Eight heads on one buffer | proposed | LOOPS mode — eight simultaneous windows onto one recording, each a Stereoroom node |
| Record a take, then point at it | proposed | `recordingsDir()` already exists |
| MATERIAL engine | available | as above |
| MIDI file export | **open question** | never answered: should MidiJaQ write MIDI files? |

## Shared / both

| Feature | Status | Note |
|---|---|---|
| Groove bus | proposed | one swing map read by both plugins — timing plane separate from timbre plane |
| Theme layer (tokens + vector drawing) | proposed | 304-token precedent in Synplant; build the layer before any individual skin |
| Loose pool sounds in saved state | **open question** | `kitToXml` writes only what a lane uses; may be wrong for MidiJaQ |
| BassMono scope | **open question** | currently Stereoroom output only; globally it changed every preset's panning |

## JaqRack

Not started. Starting point is the graph, not the cables: topological order, cycle detection with
a one-sample delay, versioned JSON patch format, existing plugins wrapped as nodes. Do not build
on VCV Rack (GPLv3). See the generative-engines doc §6.

---

## Recently done

| Feature | Where |
|---|---|
| MATERIAL room — learned objects played, sound run through them | Batchcut, 26 Nov |
| Per-chop placement in the Stereoroom audio path | MidiJaQ |
| `nsr::RoomVerb` — depth as a room, not a level | shared |
| MAP drives playback (row = degree, loop mark = chop) | MidiJaQ |
| Plugin state saves the rooms, map and stage | MidiJaQ |
| Preset bars with hover audition | Batchcut, MidiJaQ |
| `.njqloop` round-trip loop preset | shared |
| Audio import and drag-and-drop fixes | MidiJaQ |
