# Proposed features — NeoBeat and MidiJaQ

Running list. Updated as features are proposed, built or dropped. Covers both plugins plus the
shared tree; Batchcut appears only where a change touches it.

Status key: **proposed** (not specced), **specced** (design settled, not built), **building**,
**done**, **open question** (needs a decision before it can be specced).

Last updated 26 November 2026. Four decisions settled this session: MIDI export (baked),
loose pool sounds (save), BassMono (whole mix), and the IDM Jack spec.

---

## New this session

### IDM Jack — both plugins — *specced*

A glitch mode that stays rhythmic. Reference points: Autechre, Aphex Twin.

**Purpose, as stated:** generative chaos with motifs, to be recorded in session and used in
production. This is a **sound-design and capture tool, not a composition tool** — the goal is not
that any given bar is "good", but that long takes yield material worth keeping. That changes what
matters: variety over minutes, and recurring figures, rather than per-bar musicality.

**The lattice is the core mechanism.** Glitch events snap to a **16th-triplet swung grid** — six
per beat, twenty-four per bar, swing displacement applied within triplet groupings. The grid is
**invisible**: not drawn, not editable, not something events are placed on by hand. Events can
only land on it, so randomness decides *whether* and *how much*, never *where*. That constraint is
what keeps the result rhythmic rather than broken.

**Resolved:**
- **Overlay or replace — both, as a toggle.** Either keep the written pattern and glitch over it,
  or clear it and run on kit sounds alone.
- **Source — everything in the kit.** Any lane is fair game; a burst picks from the whole kit.

**Behaviour:**
- A density amount sets the chance a burst fires at each lattice position
- A burst **retriggers rapidly**, subdividing the lattice
- **FX run off the same seed and the same grid**, so a parameter jump lands on the same instant as
  a retrigger rather than smearing across it
- One seed per window, with a probability the next window reuses it — bar-level coherence, and a
  passage that can be kept

**Motifs need a memory, not just a seed.** Per-position randomness produces texture, not motifs.
Proposed mechanism: a **figure buffer**. When a burst fires, store what it was — which lanes, which
lattice positions, how many retriggers. With some probability a later burst replays a stored figure
instead of generating a new one, optionally varied. A few slots gives recurring shapes; many
approaches pure texture. This is also what keeps a long take from drifting into uniform noise,
which matters when the output is being recorded rather than listened to once.

**Per-hit parameter chaos.** The mode also randomises each sound's own parameters, fast: pitch up
and down, drive, "hollowed" (a narrowing — comb, bandpass or formant; exact mapping still to pick).
This is what separates the mode from an ordinary stutter effect, where repeats are identical and
therefore mechanical. Each repeat in a burst gets its own parameter offsets.

**The revert requirement makes this non-destructive by necessity.** Chaos exists while the control
is held and stops the instant it is released, returning to whatever was playing. That means the
parameter randomisation **must be an overlay applied at playback — per-hit offsets layered over the
stored values, never written back to them.** Written back, releasing the control would leave every
sound in the kit scrambled. Worth stating because it is cheap to get right at design time and
expensive afterwards.

**Performance:** several modes, of which the primary is **held** — chaos while the control is on,
instant return to the original pattern when off. A latch and a per-bar probability are variants of
the same engine.

**Capture:** in Ableton. No WAV export needed from the plugin.

**Consequence for the figure buffer:** a stored figure should store its parameter moves alongside
its lanes and positions. A motif recurring with different pitch and drive each time is not
recognisable as the same motif.

---

## NeoBeat

| Feature | Status | Note |
|---|---|---|
| IDM Jack | specced | see above - sound design / capture, not composition |
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
| IDM Jack | specced | see above - sound design / capture, not composition |
| Right-click "send node to" | **unverified** | MOD room advertises it; never tested |
| Shipped presets | missing | preset bars read an empty folder |
| Eight heads on one buffer | proposed | LOOPS mode — eight simultaneous windows onto one recording, each a Stereoroom node |
| Record a take, then point at it | proposed | `recordingsDir()` already exists |
| MATERIAL engine | available | as above |
| MIDI file export | specced | yes. **Baked timing** - swing written into the note positions, so it keeps its feel when dropped into Ableton. Straight-grid export not built unless asked for later |

## Shared / both

| Feature | Status | Note |
|---|---|---|
| Groove bus | proposed | one swing map read by both plugins — timing plane separate from timbre plane |
| Theme layer (tokens + vector drawing) | proposed | 304-token precedent in Synplant; build the layer before any individual skin |
| Loose pool sounds in saved state | specced | yes, save them. Costs project size in proportion to unplaced samples, FLAC-compressed - a few MB for a 48-sound pool. Changes `kitToXml`, which currently writes only what a lane uses, so it affects Batchcut too |
| BassMono scope | specced | **whole mix**. Measured consequence: a hard-left voice goes from >12 dB channel separation to 9.2 dB because its low end is centred. That is correct for a mix safety net, so `test_audio`'s expectation gets updated rather than treated as a regression. Presets made before the change will sit slightly differently after it |

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
