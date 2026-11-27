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

**What the learner's source shows (read 27 Nov, from `MaterialLab.zip` in the user's Learner
folder - nothing on their computer was changed):**

- The objects the MATERIAL room plays come from `engine2.py` (the living modal body) fitted by
  `fit2.py`: `partials.py` tracks every partial (Spectral Modeling Synthesis style), the tracks give
  a starting body (frequency, strike, decay law, beating, swelling, sweeps, contact stop, the dense
  noise layer by band), then **Adam refines it through the differentiable engine** - multi-resolution
  log-spectral loss (256/1024/4096), an RMS envelope term, a 1 ms attack term and a log-frequency
  optimal-transport term, with light regularisation so the laws stay playable.
- **The refinement is the point**: on the six impacts it cuts the loss 2.5-6x from the analysis start
  (tube clank 16.6 to 2.6; clank 5.8 to 1.5; pan 4.8 to 1.9). A port without it would be well short.
- Each fit moves about **1,000-1,100 numbers** (128 modes x 8, ~25 body laws, 32 noise bands) and
  took **2.5-31 minutes** in JAX.
- **Licence**: the package is GPL-3.0-or-later because the version-1 engine (`physics.py`) builds
  on jaxdiffmodal (GPL-3). Only `physics.py` imports it; `engine2.py`, `fit2.py` and `partials.py`
  - the chain to port - do not, and are the user's own code.
- JAX computes the gradients automatically; C++ has no equivalent built in, so the port needs the
  gradients written out (or a C++ automatic-differentiation library).

**Settled (the user, 27 Nov): write the gradients by hand** - the same loss and the same optimiser
as the learner, checked against JAX, rather than a C++ autodiff library (slow and memory-hungry at
this size) or calling the Python learner from Batchcut (needs Python and JAX on the PC).

**The plan, each stage checked against the user's own learner running in the build container as the
reference before the next starts:**

1. The reference: the learner (engine2, fit2, partials) running here on one of the training clips.
2. The engine in C++, rendering the same samples as `engine2.py` from the same parameters. (The
   MATERIAL room's player leaves out the cascade, low-body rocking, the elastic term and the dense
   noise layer, so it is not enough: a fit is only as good as the engine it fits.)
3. The loss in C++, the same value as `make_loss`.
4. The analysis and starting point (`partials.py`, `initial_params`), the same starting body.
5. The gradients, by hand, checked against JAX's at random points.
6. Adam, the same schedule: a fit whose final loss matches the learner's on the same clip.
7. Into Batchcut: drop any file, it fits in the background, the material lands in the pool; then the
   variant search across the material model, NeoBeat's voices and transforms of the file itself.

**Progress, 27 Nov** (updated 27 Nov evening, stages 4-7a; code: `Source/neobeat/shared/learner/` in Batchcut's tree; checks:
`tools/test_learner.cpp`; the reference scripts: `tools/learner_ref/`):

| Stage | Result |
|---|---|
| 1. The reference | The learner runs here unchanged. Re-scoring the user's fitted PVC pipe gives 0.66861 + 0.00213 regularisation = **0.67074, the recorded loss**. Refitting from scratch lands on **exactly the recorded losses**: PVC pipe 0.67074525 (36 s), tin can 1.38107181 (78 s). Deterministic, so the port can be held to these numbers |
| 2. The body (`Body2.h`) | Renders the learner's samples from the same parameters and noise. Against the learner run in double precision: 2.6e-8 relative rms on all three clips (PVC 27 modes, tin can 124, tube clank 130 with four glides and a contact stop). Against the learner as it runs (float32): 1.3e-5 / 6.2e-5 / 2.7e-3 - the tube's larger figure is the learner's own float32 drift on gliding phases (its float32 render is 2.7e-3 from its double render) |
| 3. The loss (`Loss2.h`) | The learner's render scores the same here as in the learner to six decimals (PVC 3.155455 / 0.668609; tin can 5.208192 / 1.379871) |
| 4. Analysis and starting point (`Analysis2.h`, `NumpyRandom.h`) | `partials.py` and `initial_params` ported: partial tracking finds the same tracks (28 / 214 / 1161; frequencies within 2e-13), the residual and noise match, the tube clank's **4 sweeps and contact stop (0.805 s, 300 Hz) are found** as in the learner, and the starting body matches the learner's to 1.2e-6 (27 / 124 / 130 modes). numpy's random generator reproduced bit for bit |
| 5a. The loss's gradient (`Loss2::valueAndGrad`) | Written by hand, term by term. Equal to JAX's double-precision gradient: spectral 3e-9, envelope 2e-15, transport 4e-9, attack 2e-16 (relative), all three clips, start and fitted. Against the learner's own float32 gradient: cosine 0.99989-1.0 - float32 STFT noise flips the sign of the log-spectral term in near-silent bins. The window is rounded to float32 as the learner does (that rounding alone moves the gradient 0.5%) |
| 5b. Back through the body to the parameters (`Grad2.h`) | Written by hand, line by line in reverse through the render (one extra pass over the samples; phases recomputed, not stored). Against JAX's gradient of the learner's objective in double precision: **all parameters (278 to 1102) within 3e-7**, PVC, tin can and tube clank, start and fitted (contact damping exercised on the tube). One value and gradient: 0.19 s (PVC) to 0.64 s (tube) |
| 6. Adam (`Fit2.h`) | Level match, Adam with the learner's rates (0.04, logf 0.002), schedule, regularisation, freeze mask and best-point keeping. Step for step against the learner run in double precision: the first three steps agree to 1e-7 on all three clips. A whole 300-step fit: 60 s (PVC), 74 s (tin can), 210 s (tube), one core |
| 6, finding | **The learner's fit is path-dependent.** Adam moves every parameter by about its rate however small its gradient, so parameters whose gradient is at roundoff level step whichever way roundoff says; any change of rounding parts the paths after 3-5 steps and the fits end in different places. Measured: the learner itself in double precision ends PVC at 0.672, or 1.096 with the window and start rounded to float32 (its recorded float32 run: 0.671). The port, nudging the start by 1e-7 six times: PVC 0.583-1.073 (three runs near 0.58, three near 1.0; recorded 0.671), tin can 1.166-1.422 (recorded 1.381), tube clank 2.420-2.538 over four runs (recorded 2.644). So the port fits as well as the learner, and the learner's recorded results are one draw each. **Decided by the user, 27 Nov:** *"Several, all into the pool"* - Batchcut runs several fits of a dropped file side by side (e.g. 4, one per core), each nudged at the start, and every one lands in the pool as its own material, ranked by loss, the closest the default. The spread becomes variants |
| 7a. Into Batchcut | **Built 27 Nov (evening).** Drop any audio file on the MATERIAL room, or press Fit on a pool sound: four fits side by side, each saved as a learner-format material and struck into the pool, closest first. With it, a step is 4-5x faster (tube clank: 0.66 s to 0.14 s) and every check above still holds. Record below |
| 7b. The variant search | next: the breeding design, shown before building |

**Stage 7a, built 27 Nov (evening, in a second chat - the first handed over to save usage).** The handoff's
five steps, in its order. The reference data was rebuilt here from the user's `MaterialLab.zip` and the three
training clips and targets (read from the Learner folder; nothing on the computer was changed).

1. **The fit job tested** (`test_learner REFDIR CLIP.wav`, stage 7a; `LEARNER_JOB=1` adds the whole job):
   - *The cut:* each training clip comes out of `prepareClip` exactly as the learner fits it - cut at the same
     sample (PVC 552, tin can 312, tube clank 216) and every sample after it identical.
   - *Changed on the way:* the 30 ms fade now applies only where the 1.5 s length cuts the sound short. The
     training clips were already faded when the training set was built; fading them again broke the match above and
     moved the tin can's noise floor by 1 dB.
   - *The noise floor:* the training set measured its floor on the whole source recording (`source_floor` in the
     Training folder's `tools/targets.py`), which a dropped file does not come with. The clip's own quietest 5%,
     measured before any fade, stands in: -45.1 / -60.6 / -29.1 dB against the targets' -45.4 / -59.6 / -29.5 (the
     check allows 1.5 dB). The handoff's "within 0.9 dB" was measured on the untrimmed clips.
   - *Any rate, any length:* the clip at 44.1 kHz, 20 s into 60 s of quiet: strike found within its 1 ms frame
     (0.45 ms off), level unchanged, prepared in 72-84 ms. Only the stretch around the strike is resampled now (found
     first at the file's own rate), and the resampler's Kaiser window comes from a table - computed tap by tap, a
     second of audio took a second.
   - *The whole job:* two fits of 300 steps on two threads, best first. PVC 13 s (best 0.752 and 0.773), tube clank
     46 s (2.426 and 2.617; the learner's recorded 2.644). Fit 0 in the job is the same fit run directly, to the
     last bit. Progress only climbs and ends at 1. Cancel stops within 0.04-0.14 s and keeps no partial fit. A
     silent clip is refused with a sentence.
2. **Speed: needed, and done.** One step (value and gradient), before and after, on this container's cores:

   | | render | loss and its gradient | back through the body | a step |
   |---|---|---|---|---|
   | PVC pipe (27 modes, 0.78 s) | 44 to 13 ms | 82 to 13 ms | 59 to 19 ms | 185 to 45 ms |
   | tube clank (130 modes, 0.97 s) | 251 to 51 ms | 97 to 16 ms | 316 to 71 ms | **664 to 138 ms** |

   How. *Render and gradient pass:* each mode's pair of phasors is turned by complex multiplication through a
   control frame (its frequency is a straight line across the frame), with sin and cos taken once per frame from
   the phases, which are still summed sample by sample as the learner sums them (`PairPhasors`, `Body2.h`); the
   dense layer's per-band decay is a running product reset each frame (`BandDecay`). *Loss:* FFT twiddles
   tabulated once per size; two frames per complex FFT, forwards and in the gradient (through the Hermitian form);
   the transport's band weights skip their zeros (bit-identical); the target's side of the log-spectral term
   computed once. *Checks held, all three clips:* stage 2 at 2.47-2.57e-8 from the learner's double render (was
   2.6e-8); 5a's spectral gradient now 4e-11 from JAX's (was 3e-9 - the tabulated twiddles are the more
   accurate); 5b within 3.1e-7 on every parameter; 6a's first three steps within 6e-8.
3. **Into Batchcut** (`Source/app/Processor.*`). `fitStartFile` (the first minute of the file, channels averaged)
   and `fitStartPool`; `ml::FitJob` now also takes the sound as it is and prepares the clip on its own thread, and
   strikes each finished fit on its worker thread, so the message thread never fits or renders. Four fits on cores
   minus one (at least 1, at most 4). The processor's own timer collects a finished job, so it lands with the
   editor closed. Each fit is written to `Documents/Marbelite/Materials/<sound>/<sound> fit k.json` (a second job on
   the same sound goes to `<sound> 2/`) by `learner/MaterialFile.h`: the learner's format (`name`, `loss`,
   `start_loss`, `seconds`, `info` {source, fit, of, noise_floor_db, made_by}, `n_samples`, `params` with every key
   of engine2), numbers to 17 digits. And into the pool as "<sound> fit k", closest first, at peak 0.89 (as
   Render 8 hits does). *The learner opens them:* a Batchcut-written PVC fit, re-scored by the learner's own
   `ref1.py`, gives 0.8539; this port gives 0.8540 for the same file with the learner's noise.
4. **The MATERIAL room** (`Source/app/MaterialRoom.*`): an audio file dropped on the room is fitted (material files
   and folders open as before); **Fit**, right of the pool list, fits the chosen pool sound; on the message line,
   the progress (percent and time left, with a thin bar) and **Cancel**; when a job ends the room opens its folder
   with the closest fit loaded. Strike, Render 8 hits, Send through, Folder and the arrows are unchanged. *Fixed
   on the way:* the room's material list was dark text on a dark list, unreadable except the loaded row (the
   snapshot showed it); it now sits on a plate like the CUT room's file list.
5. **Tests.** Host test section 11 (new): an audio file dropped at 44.1 kHz is fitted four ways into the pool and
   four files (every engine2 key, ranked, each struck again to its pool sound within 6e-8), the closest loaded;
   Render 8 hits on a fitted material; Fit and Cancel by their buttons (stopped in 63 ms, nothing added); a second
   fit refused while one runs; a fit finishing with the editor closed, into a folder of its own. The random session
   now visits MATERIAL too (fits started and cancelled, strikes, renders). Seeds 1, 7, 2026, 31337, and 5 on the
   final code: all pass.
   Snapshots 18-20 (empty, fitting, fitted) looked at. `test_engine`, `test_session`, `test_kit`, `test_gen`,
   `test_loops`, `test_keys`, `test_material`, `test_shared`: all pass. Not yet in a Batchcut download.

**Found: a fit is partly a fit to its own noise.** The body renders its strike click and dense layer from noise
given as input, and the fit is scored and tuned against one draw of it. The same material scored against the same
clip with other draws of the same colour (this port's loss; the learner's draw matches the learner's own score):

| Material | its own draw | the learner's draw | another draw |
|---|---|---|---|
| PVC pipe, fitted by the learner (recorded 0.671) | 0.669 | 0.669 | 0.986, 1.057 |
| tin can, fitted by the learner (recorded 1.381) | 1.380 | 1.380 | 2.432, 2.008 |
| tube clank, fitted by the learner (recorded 2.644) | 2.643 | 2.643 | 3.376, 3.092 |
| PVC pipe, fitted by Batchcut | 0.751 | 0.854 | 0.956 |

So the recorded losses are a little better than the material is: part of each fit tunes the noise layers to the
particular noise samples. What is heard does not depend on it (any draw is the same colour, and the pool's strikes
use a longer, different one), but it matters to 7b: ranking variants by loss against one draw rewards that tuning,
so the search should score each candidate on a draw it was not fitted to, or on a few.

**Also seen:** refitting from scratch here (JAX 0.12.1) gave PVC 0.665, tin can 1.468, tube clank 2.503 - not the
recorded losses the first chat reproduced exactly with its JAX build. The same path dependence (stage 6): a
different build rounds differently.

**Next: 7b.** Before building, show the user a breeding design - how a generation is made, how favourites are kept
and bred - across the material model, NeoBeat's voices and transforms of the file. *Reference data* is still not in
the repository: rebuild it with `tools/learner_ref/README.md` (the user's `MaterialLab.zip`, the training clips
and targets from their Learner folder, and JAX).

### 7b, the breeding design - proposed 27 Nov (evening), not built, for the user to settle

Built on what the user settled (above: breed from everything, the file itself included; every variant into the
pool) and the research (`Generative engines - Petri, PolyNodes, and what to build.md`, MidiJaQ tree, §1.2 and
§3 Box 1: population, score against the target, keep the best, mutate, repeat).

**A generation is eight children.** Each comes from the parents in one of four ways:

| Kind | What it does |
|---|---|
| **Body** | A parent material moved. Its laws and noise bands take a random step in the fit's own unconstrained numbers (so every child is still a playable body), plus one larger move drawn at random: transpose (every mode times a ratio), stretch (harmonic towards metallic, f to the power 1 + s), damping tilt, beating, the cascade's strength, glide, the contact stop |
| **Cross** | Two kept materials crossed: each law from one parent or the other; the modes split at a frequency, those below from one parent and above from the other |
| **File** | The dropped sound itself transformed - stretched (0.5-2x, pitch kept), smeared or frozen in the spectrum, or granulated (grain size, density, jitter) - or sent through a kept material (the room's Send through, used as a breeding move). These stay recognisably the recording |
| **Voice** | A NeoBeat voice patch (`ls::Voice`, its plain-text patch format) moved or crossed the same way - see question 1 |

**Parents.** At first the closest fit and the dropped sound. After that, whatever the user keeps.

**Scored, shown, kept.** Each child is struck (as long as it rings, at most 6 s) and scored against the dropped
sound with the learner's loss, on a noise draw it was not fitted to (the finding above), shown as how close it
came. Every child lands in the pool, named `<sound> g2 3` (generation 2, child 3). **Keep** on a child makes it a
parent, and a kept body is also saved as a material file beside the fits. With two or more kept, half of the next
generation are crosses.

**One knob, REACH:** how far children go from their parents, from near (variants you would mistake for the parent)
to far (wild).

**Two ways to run it,** the two tools the user named: **Grow** (Petri's "four takes climb toward it"): runs
generations by itself, keeping the closest each time, so the sound converges on the file - the search is the
learner's loss, so Grow finishes what the fit started, along the way. **Wander** (Synplant's branches): one
generation per press; only what the user keeps breeds, and closeness is shown but not used.

**Cost:** a body child strikes in 0.1-0.3 s and scores in about 20 ms, so a generation of eight takes 1-2 s and
twenty generations of Grow about half a minute.

**Questions for the user (asked 27 Nov, evening):**

1. NeoBeat's voices live only in NeoBeat's tree (`Source/dsp/Voice.h` and its patch text). Using them here means
   bringing the voice engine into the shared headers. Now, with the first build of 7b, or after body and file
   children work?
2. Every child into the pool (as decided), knowing eight per generation adds up quickly - or only kept children
   and the fits?
3. Grow and Wander both in the first build, or one first?

**Settled (the user, 27 Nov, 19:46):** *"1. After 2. Does it affect performance? If yes, then take the alternative
3. Both, but how much time will it take?"*

| Question | Answer |
|---|---|
| NeoBeat's voices | **After** body and file children work. The first build of 7b breeds bodies, crosses and the file |
| Every child into the pool? | **It does affect performance, so the alternative: only kept children (and the fits) go into the pool.** Measured: each pool sound is 0.4-2.3 MB in memory (1-6 s, stereo) and 30-45 ms of analysis on the message thread (`bc::analyseItem`, which every pool sound gets). One Grow of 20 generations would add 160 sounds: 60-370 MB, and 5-7 s of the window not answering; the kit room's auto-fill and RANDOM also go through the whole pool. Projects are not affected (they keep only the sounds on kit lanes). So a generation's eight children live in the room only, replaced by the next; **Keep** puts one in the pool and saves it as a material file |
| Grow and Wander | **Both** in the first build |

**Found while answering:** the File children need not be written from scratch. Batchcut's tree already has the
Matrixxxer's effect library (`Source/neobeat/dsp/MatrixFx.h`: 24 offline spectral and time-domain effects on one
phase-vocoder engine), which covers the transforms the user named - Spectral Blurring (smear), Freezing,
Granulation, Idee Fixer and the Grain Streamer (granular). A time stretch that keeps the pitch is a small addition
to that engine.

**Estimate given (27 Nov, 19:50):** about 3-4 hours of work, in two halves - the engine first (children, scoring
on a fresh noise draw, Grow and Wander on worker threads, its checks in `test_learner`), then the room (the
generation of eight, Keep, REACH, Grow and Wander with progress and Cancel, the host test, snapshots, docs).
Waiting for the user's go.

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

