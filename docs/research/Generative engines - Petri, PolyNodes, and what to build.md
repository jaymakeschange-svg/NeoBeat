# Generative engines: Petri, PolyNodes, Genopatch — the science, and what to build

23 November 2026. Petri studied by running it; PolyNodes from its published description; Synplant
from its binary (see the Chassis teardown for that one).

**The headline: only one small part of any of this is a neural network, and it isn't the part that
makes the sounds.**

---

## 1. What Petri actually is

Observed directly from the running app (demo mode, 436 of 709 sounds).

The interface is two separate systems sharing a window.

### 1.1 The map — this is the neural part

Several hundred coloured dots in clusters, each a sound from a 709-sound corpus. Colour marks the
cluster. You *"drag across the field to audition · click a dot to pin"*.

Top right is a search field with a **brain icon**, carrying free text — *"acoustic ride"*, *"soft
sub with a long tail"*. That is the giveaway: matching an arbitrary English phrase to a position
among audio clips requires **a joint text-audio embedding** — one vector space that both a sound
and a sentence can be projected into, so nearness means "sounds like what those words describe".
CLAP is the well-known public example of this architecture.

So the map is: embed every sound → project to 2D for display → embed the typed text into the same
space → highlight what lands nearby. ~38 MB with no external files is entirely consistent with a
small audio encoder plus a text encoder compiled in.

**This is navigation. It does not synthesise anything.**

### 1.2 "grow" — this is where the sounds come from, and it is not neural

The left panel reads:

> *grow* — "pin a dot on the map · that's the sound to grow" / "click a dot on the map · grow
> evolves synth takes of it" / "pin a dot on the map · **four takes will climb toward it** here"

with modes **quick | complex**, and four controls at the bottom:

| Control | Value shown | What it is |
|---|---|---|
| `branches` | 4 | population size per generation |
| `gen` | — | number of generations |
| `best` | — | elites kept between generations |
| `beads` | — | a synth-engine parameter |

`branches` / `gen` / `best` is **textbook evolutionary-algorithm vocabulary**, and "four takes will
climb toward it" describes hill-climbing exactly. So: render a population of synth parameter sets,
score each by distance from the pinned target, keep the best, mutate them, repeat for N
generations.

**Petri = a learned embedding for choosing a target + an evolutionary search over synth parameters
to reach it.** The embedding makes the target findable; the search makes the sound.

That is the same family as Synplant's Genopatch, which does the same job with gradient descent
instead of evolution (see the Chassis teardown §7.2 — no ONNX, no libtorch, just `gradient`,
`descent`, `genome`, and a per-solution error).

---

## 2. What PolyNodes is — no neural network at all

From sonicLAB's own description: each sound generates a 3D geometric entity built from **onset-time
analysis at macro, meso and micro scales**; multiple playheads called **node agents**, carrying
re-synthesis and signal processing, navigate that geometry by selectable strategies per scale;
**DSP interactables** placed in the space apply processing by agent proximity; and **CuboidFX**
derives modulation from the geometry itself.

Stripped of the language, that is **multi-scale concatenative / granular resynthesis with a spatial
controller**. Analyse the input into events at three time scales, lay those events out in 3D by
their features, then move playheads through that space, sounding whatever they pass near. It is
Sinan Bokesoy's multi-scale stochastic lineage, not machine learning.

**Nothing in PolyNodes requires a model.** Everything it does is classical DSP plus a geometry and
some agents.

---

## 3. The science, in three boxes

Every one of these tools is one or both of two ideas. Only the third box is machine learning.

### Box 1 — Parameter search (Petri's "grow", Synplant's Genopatch)

*You already have a synth. Find the knob settings whose output matches a target.*

Three pieces:

1. **A synth** with its parameters normalised to 0–1. Synplant's genome is literally this — named
   genes (`a_freq`, `a_mod`, `a_noise`, `adj_clip`…) scaled into ranges at use.
2. **An error function** — how far a rendered candidate is from the target. The standard answer is
   a **multi-resolution STFT loss**: take log-magnitude spectrograms at several window sizes
   (e.g. 512 / 1024 / 2048), sum the differences. Multiple resolutions matter because one window
   alone trades time precision against frequency precision, and percussion needs both.
3. **An optimiser.**
   - **Evolutionary / genetic** — population, score, keep elites, mutate, repeat. Needs no
     gradients, trivially parallel, handles a lumpy error surface. This is Petri's.
   - **CMA-ES** — the strong choice for 10–50 continuous parameters; adapts its own search shape.
   - **Gradient descent** — fastest when it works, but needs gradients. Either a differentiable
     synth, or finite differences (N+1 renders per step, affordable only if rendering is cheap).
     This is Synplant's.

**Nothing here is learned.** The "intelligence" is the search.

### Box 2 — Analysis-driven resynthesis (PolyNodes)

*Take input audio apart, lay the parts out in a space, move through that space.*

1. **Multi-scale onset analysis** — detect events at several time scales. Batchcut already does
   this at one scale.
2. **A layout** — place each event in 2D or 3D. Either directly from features (time, spectral
   centroid, loudness) or by MDS/UMAP over a feature-distance matrix.
3. **Agents** — playheads with a movement strategy, sounding the events they pass near, with
   per-agent processing.

Also entirely classical.

### Box 3 — Learned embedding (Petri's map, and only that)

*One vector space holding both sounds and words.*

This is the single piece that genuinely needs a trained model. It buys two things: a map where
nearness means perceptual similarity, and **search by typed description**.

Costs: a pretrained audio encoder is tens of megabytes, plus an inference runtime (ONNX Runtime is
~10 MB and would be the sane choice). Training your own is not realistic here.

**The cheap substitute:** handcrafted features — MFCCs, spectral centroid/flatness/rolloff, attack
time, decay — reduced to 2D with UMAP or t-SNE. You get a genuinely useful map of your own sample
library, clustered sensibly, with no model and no runtime. **You lose only the text search.**

---

## 4. So what should actually be built

**You do not need the neural network to get the sounds you like.** The sounds come from Box 1 and
Box 2, which are search and DSP. The neural part is a way of *finding* a target, and handcrafted
features get most of that for nothing.

Recommended order:

1. **Box 1 — parameter search against a target sample.** Highest value, and the most direct route
   to "make me a patch that sounds like this". Start with a genetic search; it needs no gradients
   and the code is short.
2. **A feature map** (Box 3's cheap substitute) so the sample library is navigable and targets are
   easy to pick.
3. **Box 2 — agents over an analysis geometry.** This is closest to what already exists: Batchcut
   detects onsets, MidiJaQ's Stereoroom already places sound sources in space and moves them along
   paths. An agent walking a geometry and sounding what it passes is *the Stereoroom with the
   arrows reversed*.
4. **A real embedding, only if text search turns out to matter** after living with the rest.

---

## 5. Which is cheaper: Batchcut or a trial JaqRack?

**Neither. Both are the expensive way to answer the question.**

The question that matters first is *"do these sounds actually please me?"*, and that question needs
no interface at all.

### Why not JaqRack

JaqRack does not exist. Building a graph engine, a scheduler, a cable UI and a module-wrapping
layer in order to trial one algorithm is backwards — it is weeks of work before the first sound,
and none of it tests the thing you want to know.

### Why not Batchcut (yet)

Batchcut has **no synthesis engine**. It cuts, analyses and exports. A parameter search needs a
synth whose parameters it can search, so "put it in Batchcut" silently also means "put a synth in
Batchcut". That is a real cost and it is hidden in the framing.

### What is actually cheapest

**A console test harness in the shared tree**, exactly like the dozen that already exist:

```
Source/neobeat/shared/grow/     the search: genome, error function, optimiser
tools/test_grow.cpp             load target.wav, run the search, write candidate wavs
```

No UI, no room, no plugin. It loads a target, runs N generations, writes the best candidates to
disk, and prints the error per generation so you can see it converging. A day or two of work, and
it answers the only question that matters. The repo's whole testing culture is already built this
way.

**Then** the UI goes into **Batchcut**, because Batchcut is the preset editor and "fit a patch to
this one-shot" is a preset-editing job. By then the engine is proven and the synth it searches is
whichever one you point it at — NeoBeat's, via the shared tree.

Cost, honestly:

| Route | Before you hear anything |
|---|---|
| Console harness | the search + an error function |
| Batchcut room | the above + a synth in Batchcut's tree + a room + preset plumbing |
| Trial JaqRack | the above + graph engine + scheduler + cable UI + module wrapping |

---

## 6. Where to start with JaqRack, realistically

JaqRack is worth building — but for its own reasons, not as a vehicle for this.

**The hard part is not the cable UI.** It is the graph. Order of work:

1. **A headless graph engine.** Modules with typed inputs and outputs; topological sort to decide
   processing order; cycle detection with a one-sample delay inserted to break feedback loops
   (every modular host needs this and it is the thing people get wrong); fixed-block processing.
   Test it with a console app that renders to a wav.
2. **The patch format.** Plain JSON: `modules[]` with `{id, model, version, params, data, pos}`
   and `cables[]` with `{outputModuleId, outputId, inputModuleId, inputId}`. **Version every
   module** and read old versions — Chassis does this, and patches that break on upgrade are what
   kills a modular host.
3. **Wrap the existing plugins as nodes.** NeoBeat, MidiJaQ and Batchcut are already
   `AudioProcessor`s. A node is a thin adapter over one.
4. **Parameter and modulation routing** — the generalised "send node to x", any source to any
   parameter of any module. Design this early; retrofitting it is painful.
5. **The cable UI last.** It is the visible part and the least risky.

**Milestone one, concretely:** a headless graph that loads a `.JRQ` containing two modules and one
cable, renders eight bars to a wav, and passes a test asserting the audio is what the two modules
in series should produce. Everything else follows from that working.

**Do not build on VCV Rack.** Chassis does, which is why it inherits GPLv3. Your modules are
already written; what you need is a graph, not an engine.

---

## 7. On "identical to Petri"

Two honest points, both practical rather than principled.

First, the part that would have to be copied — the trained embedding — **cannot be extracted and
would not help**. It is navigation, not synthesis. The sounds come from the search, and the search
is an algorithm anyone can write.

Second, an exact clone is the wrong target anyway. Petri's search is tied to Petri's synth. Yours
would be tied to **NeoBeat's** engine — the tiling percussion, the FM voices, the modal
resonators — which is a different and more interesting instrument to be searching. "Grow me a
patch that sounds like this" is far more valuable pointed at your own synthesis than at a
reimplementation of someone else's.

---

## 8. What is still unverified

- Petri's embedding is **inferred from the brain icon and free-text search**, which is strong but
  indirect evidence. Its binary (37.5 MB) has never staged — transfers here cap out around 14 MB —
  so the inference runtime and tensor names remain unconfirmed. A zip, or a split archive, would
  settle it.
- `beads` in Petri's grow panel is a synth parameter whose meaning is not established.
- PolyNodes is described from its published material, not studied. It is not installed here.
