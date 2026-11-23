# Chassis (Lux Cache) — teardown, and what the Neosystem should take from it

Studied 23 November 2026 from the installed VST3: the patch format, the module preset schemas, and
the strings and symbols in the 14 MB binary. Not from marketing copy or a web search.

**Nothing proprietary is reproduced here.** What follows is architecture and technique described in
the author's own in-app documentation, plus what the data formats reveal. Every proposal below is
an original design for the Neosystem, not a port.

---

## 1. What Chassis actually is

**A VCV Rack engine, hosted inside a JUCE VST3 wrapper, with Lux Cache's own module set on top.**

The evidence is unambiguous:

- The patch format (`default-patch.json`) is Rack's schema exactly: `modules[{id, plugin, model,
  version, params, leftModuleId, rightModuleId, data, pos}]` and
  `cables[{id, outputModuleId, outputId, inputModuleId, inputId, color}]`.
- Mangled C++ symbols in the binary carry `rack::math::Vec`.
- Two of the three plugin namespaces in the default patch are third-party Rack plugins — `Biset`
  and `Inklen-CableColourKey`. Only `LuxCache` is theirs.
- `juce::Slider` symbols sit alongside, which is the wrapper.
- A private UI framework called `fleet` supplies fonts and icons.

**This matters for JaqRack** (§4). It is a working proof that a rack of modules can ship as a
single VST3 instrument. It is also a licence constraint: VCV Rack's engine is GPLv3, so anything
built on it inherits that. For private, archival software that is no obstacle — but it rules the
approach out if the Neosystem is ever distributed under other terms.

### The module set

29 modules, named in French. Their jobs, from the preset names and the embedded docs:

| Module | What it is |
|---|---|
| `frappe` (strike) | Modal percussion from a **geometric tiling** — see §2 |
| `fracas` (crash) | 4 FM percussion voices, 2 operators each, with a CV router |
| `corde` (string) | Karplus-Strong / waveguide string |
| `essaim` (swarm) | Resonator bank with 8 macros |
| `coupe` (cut) | Sampler, one buffer, **8 playback heads** |
| `trame` (weft) | Spectral multi-effect, **16 processes** through one STFT |
| `grille` (grid) | Drum sequencer with a **5×5 pattern grid** and an x/y reading point |
| `volet` (shutter) | Gate/chop sequencer |
| `xtrack` / `xarrange` | Track and scene arrangement |
| `qmod` / `qmodplus` / `qmap` | Modulation generators and a mapping layer |
| `quatre` (four) | Macro lanes |
| `liasse` (bundle) | Module grouping, with `deepRandom` |
| `atelier` (workshop) | CV utilities: clocks, logic, probability gates, shift register |
| `gamme` (scale) | Scale quantiser |
| `crible` (sieve) | Vocoder |
| `foudre` (lightning) | Filter |
| `biais` (bias) | Frequency shifter |
| `pli` (fold) | Wavefolder / distortion |
| `presse` (press) | Compressor |
| `nef` (nave) | Reverb |
| `ruban` (ribbon) | Delay |
| `sillage` (wake) | Envelope follower |
| `brin` (strand) | Multiband EQ (104 params) |
| `tresse` (braid) | Mixer / crossfader |
| `vanne` (valve) | VCAs and attenuverters |
| `envoi` (send) | Sends |
| `ton` (tone) / `scie` (saw) | Oscillators |
| `clavier` (keyboard) | Keyboard, arps, chords |
| `dig` / `bac` | File browser |
| `grabplus` | Recorder, writes dated subfolders |
| `tidy` | **The theming system** — see §3.7 |

---

## 2. The seven ideas worth stealing (as principles, not code)

### 2.1 frappe: the object is a tiling, and the picture *is* the object

This is the best idea in the plugin and the closest to what the Neosystem is already chasing.

From its own documentation:

> *"The object is a tiling: a pattern of shapes that fills a flat surface. The pattern in the
> window is the object itself. The window and the sound read from the same table."*
>
> *"Every polygon in the tiling is a resonator, and its size sets its pitch. Smaller polygons ring
> higher: frequency goes as 1 over the polygon [size]."*
>
> *"Each resonator rings at several frequencies, called modes. frappe collects the modes of every
> polygon family in the tiling. It weights each family by how much of the surface it covers."*
>
> *"frappe merges and sorts all the modes, and keeps the lowest 48. Each mode remembers which
> polygon made it."*

So the pipeline is: **a geometry generates a mode table; the mode table is the instrument; the
drawing of the geometry is the same table rendered visually.** Seven tilings plus a rod and a
chime. Mode provenance is kept, so the display can light the triangles and the hexagons on
separate decay timings — you *see* the inharmonicity.

Three shaping controls, each with a clear physical meaning:

- `matr` (material) — low values read as metal
- `aspt` (aspect) — splits twinned modes so partials beat against each other
- `disp` (dispersion) — stretches modes past harmonic spacing (*"the partials stretch sharp and a
  drum sounds more like a bar"*)
- `exciter` — nine kinds of strike along a continuous field split into named zones (tap, brush,
  press, tick…)

And it rebuilds within 64 samples, filters following within 16, so you can **retune a ringing
object by hand**.

**Why this matters here:** this is precisely "sounds modelled after physical things that defy
physics". A tiling is a real object with real modal behaviour, and nothing says the tiling has to
be physically realisable. It is also inherently visual, which the Matrixxxer already wants to be.

### 2.2 frappe: two listening points, not a pan

> *"The 2 outputs listen to the object from 2 points, one on each side of the strike. Each point
> hears the modes at different levels and slightly different times. So the pair sounds wide, at
> equal level."*

**This is better than what the Stereoroom currently does**, and it is a small change. Today a chop
is a mono source placed by equal-power pan with mid/side width. That is a position. This is a
*body*: two pickups on one object, each hearing a different mix of modes with slightly different
arrival times. Width emerges from the object rather than being applied to it.

### 2.3 grille: a 5×5 grid of patterns with a continuous reading point

> *"Each bank holds a 5 by 5 grid of 32-step patterns. x and y place a reading point anywhere on
> that grid. grille works out the velocity of every step on every lane from the 4 grid points
> around the reading point."*

Twenty-five patterns become a continuous two-dimensional space you can move through, with bilinear
interpolation of per-step velocities. Not pattern switching — pattern *morphing*.

### 2.4 grille: seeded randomness with a repeat probability

> *"Every random decision grille makes inside one window comes from a single seed. turing is the
> chance that the next window uses the same seed again."*

One seed per window, and a probability that the next window keeps it. That single control spans
"locked loop" to "new every bar" continuously, and everything between is musically coherent
because a window is internally consistent. Separate amounts for `chaos`, `humanise`, `ratchet` and
`break`, all starting at zero.

### 2.5 coupe: eight heads, one buffer

> *"coupe is a sampler with one stereo buffer and 8 playback heads. Each head is a region of that
> buffer, with its own start and end. Each head also has its own speed, direction, tone, level and
> pan. The result is 8 windows onto one sound."*

Not slices triggered in sequence — eight independent simultaneous views of one recording. Plus six
fixed-kind modulators (3 LFOs, 2 envelopes, a sample-and-hold) with an "arm dot" per tile, and a
recorder that writes its take to a wav and then points itself at that file *as if you had loaded
it*.

### 2.6 fracas: a router from any CV to any internal setting

Four FM percussion voices, two operators each, and a router that sends any of four CVs to any
setting inside any voice — so each hit can sound different. This is the generalised form of
"send node to x".

### 2.7 tidy: theming as a module

`tidy`'s data keys are the whole skin system: `masterEnable`, `hidePlugHeads`, `autoColor`,
`darkModuleStrength`, `colorOpacity`, plus per-`modules` overrides and saved `presets`.

A single global theme object, a strength control rather than a binary dark mode, per-module
override, and themes saved as presets. That is the shape the Neosystem's skin layer should take.

---

## 3. Proposals

Ordered by value per unit of work. None of these require Chassis's code.

### For NeoBeat

**3.1 — A tiling percussion engine (`FRAPPE`-class, original).** The strongest single idea
available. Generate a mode table from a geometry; make the drawing of that geometry the control
surface; keep mode provenance so the display shows which family is ringing. Material, aspect and
dispersion as the three shaping axes, driven by the existing modulation system. This gets the
metallic / rubbery / latex percussion the research has been after, and it is visual by nature — it
could live as a Matrixxxer view rather than a separate window.

*Work:* substantial, but modal synthesis is 48 resonant bandpasses and a table. The hard part is
the geometry→modes mapping, which is arithmetic, not DSP.

**3.2 — Two listening points instead of a pan, for synthesised voices.** §2.2. Each voice gets two
pickup positions; width emerges from mode-level and arrival-time differences. Feeds straight into
the Stereoroom as a per-node property ("body width") distinct from the existing mid/side width.

*Work:* small, and it directly serves the stated goal of comprehensive, dynamic, 3D presets.

**3.3 — A 5×5 pattern grid with an x/y reading point.** §2.3. Twenty-five new jack swing variations
as a continuous space. The reading point is modulatable, so the Orb or the randomiser can walk it.
This is a flagship feature for a drum machine, and it fits the existing pattern storage.

*Work:* moderate. The interpolation is bilinear over per-step velocities.

**3.4 — Replace the randomiser's amount with a seed-repeat probability.** §2.4. The RANDOM room's
HOLD is already adjacent to this; `turing` is the refined version. One seed per window, a
probability it survives into the next, and separate zeroed amounts for chaos, humanise, ratchet
and break.

*Work:* small. Mostly a reframing of what exists.

### For MidiJaQ

**3.5 — Eight heads on one buffer, as a LOOPS mode.** §2.5. MidiJaQ chops a loop into hits and
sequences them. A head mode gives eight simultaneous windows onto the same recording, each with
its own speed, direction, tone and level — and each is already a Stereoroom node, so the existing
per-chop placement applies unchanged. The combination (eight heads, each placed and travelling
independently) is something neither plugin can currently do.

*Work:* moderate. The `LoopPlayer` already has per-chop voices with independent placement; a head
is a voice with its own region and rate.

**3.6 — Record a take, then point at it.** §2.5. coupe writes its take to a wav and then loads that
file as if the user had. MidiJaQ has no recording path at all; Batchcut writes to a recordings
folder. The "record, and the thing you just played is now the sample" loop is worth having.

*Work:* small — Batchcut's `grabplus` equivalent already exists in `bc::Rooms::recordingsDir()`.

**3.7 — A theme module.** §2.7. Before building individual skins, build the layer: one theme
object, `strength` rather than a dark toggle, per-room override, themes saved as presets. The
`nsui::C::` colour layer and `nsui::setSkin()` are already the hook; what is missing is the
override and preset structure.

*Work:* small for the structure, then each skin is content.

### For JaqRack

**3.8 — Chassis is the proof of concept, and the warning.** A rack of modules shipping as one VST3
instrument works, and Chassis shows the shape: a host engine, a module set, a patch format of
`modules[]` + `cables[]` with left/right adjacency, and a JUCE wrapper.

Two concrete lessons:

- **Make `.JRQ` a plain JSON graph of modules and cables, versioned per module.** Chassis stores a
  `version` on every module and reads old ones — which is how a rack survives its own updates.
  Patches that break on upgrade are the thing that kills a modular host.
- **Do not build on VCV Rack.** Its engine is GPLv3 and inheriting that constrains everything
  downstream. JaqRack's modules are already written (NeoBeat, MidiJaQ, Batchcut) — the work is a
  graph, a scheduler and a cable UI, not an engine.

**3.9 — A CV router as the universal "send to".** §2.6. The ecosystem already has "send node to x"
in NeoBeat. In a rack, that generalises to: any modulation source to any parameter of any module.
Building that routing model into JaqRack from the start, and having NeoBeat and MidiJaQ expose
their parameters through the same interface, is what makes the rack more than three plugins in a
window.

---

## 4. Petri — not yet studied

Petri is a **single 38 MB binary with no external resources** — no presets, no model files, just
`moduleinfo.json` and the DLL. Chassis is 14 MB with all of its presets and fonts outside the
binary. The difference strongly suggests the network weights are embedded.

**It could not be staged for analysis: the 38 MB transfer timed out three times.** Nothing about
its architecture below the file size is known yet, and this document asserts nothing about it.

To get it studied, any of these would work:

- Zip it (it will compress, and the archive may transfer where the raw DLL did not)
- Copy it to a connected folder outside `Program Files`, which may be slower to read
- Or let me retry when the connection is less loaded

Once it stages, the useful questions are answerable from the binary: which inference runtime it
embeds (ONNX Runtime, libtorch, or a hand-rolled kernel all leave distinctive strings), the tensor
and layer names, the input representation, and whether generation is a search over a latent space
or a direct decode. That determines whether anything equivalent is realistic in the Neosystem or
whether it is better treated as an external tool.

---

## 5. Honest limits

- This is a **static** study: the patch format, the preset schemas, the embedded documentation and
  the symbol table. The plugins were not run, and no audio was measured. Where the documentation
  describes behaviour, I have quoted it rather than inferred it.
- The documentation is the author's, and describes intent. Measuring the output would be the next
  step for anything where the exact algorithm matters.
- Nothing above reproduces Chassis's code, and every proposal in §3 is an original design.
