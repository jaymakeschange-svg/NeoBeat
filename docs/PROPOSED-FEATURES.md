# Proposed features — NeoBeat and MidiJaQ

Running list. Updated as features are proposed, built or dropped. Covers both plugins plus the
shared tree; Batchcut appears only where a change touches it.

Status key: **proposed** (not specced), **specced** (design settled, not built), **building**,
**done**, **open question** (needs a decision before it can be specced).

Last updated 26 November 2026. Four decisions settled this session: MIDI export (baked),
loose pool sounds (save), BassMono (whole mix), and the IDM Jack spec.

> **Restored 27 Nov.** On the evening of 26 Nov this file was overwritten with its 09:15 version,
> erasing five later commits: the answered IDM Jack spec, the three settled decisions, interiority,
> and detector-driven placement. This is the 16:09 version (`875ad01`) with that evening's
> decisions re-applied on top. See `DECISIONS.md` for the evening's structure; where it disagrees
> with the IDM Jack spec below, **the spec below is the user's and wins.**

---

## New this session

### IDM Jack — NeoBeat and MidiJaQ, later JaqRack. Not Batchcut — *built in NeoBeat for testing, 27 Nov; MidiJaQ next*

*How each item below was built, what was left out and why: [`DECISIONS.md`](DECISIONS.md), "27 November 2026 — the test build".*

*Scope and the settings list were set on the evening of 26 Nov; everything else in this section is
the earlier spec and stands.* Seven settings approved: seizure targets with a never-list, density,
extremity, repeat probability, lattice division, self-randomise with a depth control, and recovery.
In NeoBeat the targets are **per pad** — choosing a pad puts all of its parameters in.

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

### Interiority — a deliberate anti-externalisation processor — *proposed, both plugins*

From practice-led research into SOPHIE's sound design (c. 2020): the metallic sounds carry a very
slight delay between left and right, a couple of milliseconds, **continuously modulated by LFOs**.
The result is not a sound placed somewhere around the listener but one that seems to be *inside*
them - spatiality with effectively zero distance.

**Why that works, in numbers.** The largest inter-aural time difference a real head can produce is
about **690 microseconds** - the transit time around the skull. A 2 ms inter-channel delay is some
three times larger than any physically possible ITD, so there is no direction it can be mapped to.
It also sits above the summing-localisation region (below ~1 ms, where a delay reads as a
direction) and below the echo threshold (~30-40 ms, where it separates into a discrete repeat).
What remains is a fused image with no location. With no matching level difference, no pinna cue
and no room reflections to support externalisation, the percept defaults to intracranial - the
documented "inside-the-head locatedness" that untreated headphone audio produces. Modulating the
delay means localisation never gets the stable window it needs to settle.

**A second consequence of the same process.** Modulating a delay line resamples it, so one channel
gets a micro pitch shift the other does not. That is a continuously varying detuning between
channels - a comb filter whose notches are moving. On harmonically dense metallic material this
does much of the timbral work. The spatial effect and the metallic sheen come out of one mechanism.

**This is the inverse of what the Stereoroom currently does.** Equal-power pan, mid/side width and
the RoomVerb depth are all externalising cues; they put a sound outside the listener. Nothing in
the Neosystem currently pushes the other way.

**Proposed:** a per-node inter-channel delay in the 0.5-5 ms range, LFO-modulated in rate and
depth, available alongside the existing placement rather than replacing it. Pairs naturally with
the MATERIAL engine, since metallic modal sounds are exactly the material the moving comb acts on.

**Related but distinct:** stereo phaser with cross-channel feedback reaches a similar destination
by a different road - allpass phase shift and inter-channel phase incoherence rather than an
impossible ITD. Separately buildable; they do not sound the same. Worth having both.

### Detector-driven placement — *proposed, both plugins*

From studying the Sound Particles panners. All of them are one engine with a different **detector**
driving it, and the panning target is a trajectory between two points with two traversal
behaviours:

- **Energy Panner** - amplitude drives position, with compressor-shaped controls: threshold, ratio,
  attack, release. Sidechain input available.
- **Brightness Panner** - the same engine driven by spectral brightness via a variable filter, by
  detected note pitch over an adjustable range, or by MIDI.
- **BeatPanner** - the same engine driven by a rhythmic sequencer.

**Pan mode** moves start-to-end and returns to the start when the trigger falls away. **Sliding
mode** is progressive: a loud or sustained signal advances the position along the trajectory, and
when it goes quiet the sound *stays where it is* until the next trigger advances it further.

**Sliding is the idea worth taking.** Position accumulates rather than resetting, so a sequence of
short events produces long-form movement across a passage. That is a different musical behaviour
from an LFO and from the Stereoroom's current Travel paths, both of which are periodic.

**Proposed:** a node's position along its existing path can be driven by a detector - its own
amplitude, its brightness, or the beat - with compressor-shaped controls and a choice of return or
accumulate.

---

## NeoBeat

| Feature | Status | Note |
|---|---|---|
| IDM Jack | **built 27 Nov** (JACK room) | see above; DECISIONS.md, "27 November 2026 - the test build" |
| Stereoroom UI | proposed | engine already in its shared tree; UI not built |
| Interiority (anti-externalisation) | proposed | see above |
| Detector-driven placement | proposed | see above |
| Drag and drop | **unaudited** | may carry the same four bugs MidiJaQ had — see HANDOFF §5 |
| Preset bar (arrows, hover audition) | **built 27 Nov** | replaces the pattern dropdown; the shared bar gained an item mode for NeoBeat's built-in patterns |
| MATERIAL engine | available | `nsr::Material` now in the shared tree; no UI in NeoBeat yet |
| Orb | deferred | from stage 2 |
| Bounce Delay smoothing | proposed | |
| Techno and house presets | **built 27 Nov** - 16 patterns, numbers 129-144 | from the research doc; see DECISIONS.md. Batchcut's still to do (Batchcut section below) |
| Tiling percussion engine | proposed | from the Chassis `frappe` study — modes from a geometry, the drawing *is* the instrument |
| Two listening points per voice | proposed | width from the body rather than applied to it |
| 5×5 pattern grid, x/y reading point | proposed | 25 patterns as a continuous space, bilinear over per-step velocities |
| Global randomiser | **built 27 Nov** (ROLL, JACK room) | one press, every parameter once, for auditioning. In NeoBeat it reaches each kit sound's parameters and the lane effects. Writes values - a destination, unlike Jack - see DECISIONS.md |
| BANDS (spectral delay) with per-lane sends | **built 27 Nov** | a window next to FRICTION and BOUNCE; a BANDS knob per lane, and ALL |

## MidiJaQ

| Feature | Status | Note |
|---|---|---|
| IDM Jack | specced | see above - sound design / capture, not composition |
| Right-click "send node to" | **unverified** | MOD room advertises it; never tested |
| Shipped presets | missing | preset bars read an empty folder |
| Interiority (anti-externalisation) | proposed | see above |
| Detector-driven placement | proposed | see above |
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
| BANDS with per-lane sends and ALL; ROLL; IDM Jack; preset bar; 16 house and techno patterns | NeoBeat, 27 Nov |
| MATERIAL room — learned objects played, sound run through them | Batchcut, 26 Nov |
| Per-chop placement in the Stereoroom audio path | MidiJaQ |
| `nsr::RoomVerb` — depth as a room, not a level | shared |
| MAP drives playback (row = degree, loop mark = chop) | MidiJaQ |
| Plugin state saves the rooms, map and stage | MidiJaQ |
| Preset bars with hover audition | Batchcut, MidiJaQ |
| `.njqloop` round-trip loop preset | shared |
| Audio import and drag-and-drop fixes | MidiJaQ |

## Learner: the 16-band dense-noise layer — requested 26 Nov, not started

MaterialLab's resonator form carried the modes across from the learner but not the 16-band dense
noise layer that sits under them. Struck bodies are not only modes: the strike itself is broadband,
and real material has a noise floor with its own spectral shape that the mode bank cannot produce.
Without it a struck object is cleaner than anything physical, which is audible as plastic.

Wanted: the learner's 16-band noise restored, so generated sounds are accurate rather than only
tonally correct. Shares its band layout with `nsr::SpectralDelay`, which already splits 16 log
spaced bands and is now in the shared tree — the split is the same piece of maths and should not
be written twice.

## Batchcut: generate pools of variants from any file — requested 27 Nov, after the current NeoBeat build

**Asked (the user, 27 Nov):** *"I want batchcut to be able to generate pools of sounds based on any
file."* *"The purpose of the room for me is to have a more powerful sound design tool that can
generate and create variants of my own input like synplant and petri."* To come back to once the
current NeoBeat build (ROLL, Jack, BANDS, preset bar) is done.

**The gap, plainly.** The MATERIAL room built on 26 Nov opens only the learner's own material files
(`.json`, written by the MaterialLab learner after it has fitted a recording offline, in Python).
It cannot take an arbitrary audio file: the fitting - the part that turns a recording into a model -
is not in Batchcut. So today a new source means a trip through the Python learner first. That is
not what the room is for.

**What it needs, from the research already done** (`docs/research/Generative engines - Petri,
PolyNodes, and what to build.md`, in the MidiJaQ tree, §3-4):

- **Analysis of any dropped file inside Batchcut**, so a pool can start from any input without the
  Python step. Either the learner's fitting ported in, or a search that does not need it.
- **Box 1 - parameter search against the input** (Petri's "grow", Synplant's Genopatch): breed and
  mutate patches toward the dropped sound, keeping the variants along the way. The research ranks
  this first: highest value, short code, no neural network needed.
- **The output is a pool**: every variant lands in the pool as an ordinary sound, as the MATERIAL
  room's renders already do, so cutting, kits and .NJQ export work on it unchanged.
- Related and already listed: the learner's 16-band dense-noise layer (above), without which a
  fitted body is cleaner than anything physical.

**Settled (the user, 27 Nov, ~16:00):**

| Question | Answer |
|---|---|
| What does the search breed variants from? | **Everything, including the file itself**: the MaterialLab model, NeoBeat's patch voices and recipes, and transforms of the dropped audio (stretch, spectral, granular) - so some variants stay recognisably the recording |
| How does a dropped file become a model? | **Port the learner's fitting** into Batchcut, then the search varies around what it fits. Needs the learner's source, which is on the user's computer (MaterialLab folder) |

Not started. First step: read the learner's source and its fitting, with the user's permission to
open that folder.

## Batchcut: house and techno presets — asked 27 Nov, *built 27 Nov (in the repo, not yet in a Batchcut download)*

**Asked (the user, 27 Nov):** *"did you include the techno and house new presets for batchcut and
neobeat?"* - they were in neither. NeoBeat's sixteen were written the same day (NeoBeat table
above). Batchcut's are still to do: its patterns come from the same pattern bank file as NeoBeat's
(`PatternBank.h`, which its Kit room uses to audition a kit), so carrying the sixteen across is the
first step; whether Batchcut also needs loop or KEYS presets in these genres is a question for the
user.

**Done, 27 Nov:** the same pattern bank file carried into Batchcut's tree, and MidiJaQ's (it shares
the file, and the rule is that shared files stay identical across the family). The sixteen appear
in Batchcut's Kit room pattern list and the Loops room's rhythm list, as 129-144; the first 128 are
unchanged (checked). Batchcut's host test now draws random patterns from all 144 and passes, the
Kit room section and the long random session included. Loop and KEYS presets in these genres: not
made, and not asked yet. Earlier record of the wish: the plugin-family notes, "techno and house pattern presets in
Batchcut and NeoBeat too ... while everything stays grounded in new jack swing".

