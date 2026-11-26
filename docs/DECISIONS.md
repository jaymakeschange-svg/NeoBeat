# Decision log — Neosystem

Every decision, who made it, and where it is recorded. Append-only: a superseded decision is struck
through and the superseding one added below it, never deleted.

**Why this file exists.** On 26 November, three separate wrong answers were given about this
project's own history — the origin of a feature, the reason for a parameter count, and whether two
questions had been answered — in every case by recollection, while the document holding the answer
sat unopened. Two of those documents were Claude artifacts, invisible to `grep` from a build
container; the third was a committed file that had gone stale within two hours of being written and
was read in preference to the newer document that superseded it.

**The rule this imposes:** before stating where a design came from, what was decided about it, or
whether a question is still open — grep `docs/`. Not memory. If the answer is not in `docs/`, it
is not a decision yet, and the thing to do is ask and then record it here.

Dates are when the decision was made, not when it was written down.

---

## 26 November 2026

### BANDS — the spectral delay room

| | |
|---|---|
| **Decided** | Build it, from the Chimecho control list |
| **By** | The user, selecting item 2 off the priority ranking |
| **Recorded in** | [`research/transferable-mechanisms.md`](research/transferable-mechanisms.md) — "Chimecho", and "Shared — the convergence room" |
| **Built** | `shared/SpectralDelay.h`, `shared/rooms/Bands.h`, `shared/rooms/BandsRoom.h`, `BandsRoomUi.cpp` |

Sixteen bands is Chimecho's own band count, fixed in the research document before the learner's
noise layer was mentioned. An earlier account in chat claimed the count was derived from the
Batchcut learner's 16-band noise; that was wrong and is recorded here so it is not repeated.

**Control provenance.** Start/Finish Freq, Q, Start/Finish Len, Start/Finish feedback,
Start/Finish volume, Start/Finish LFO Speed and Width, Pan Width/Speed, Side Delay, Side Feedback,
Dry — all from Chimecho's published list. Side Delay is a *band offset*, confirmed against the
published description. The per-band interaural offset is from the SOPHIE research, not Chimecho.

### BANDS — a global Length control

| | |
|---|---|
| **Decided** | Add one; default the bank to the very short end |
| **By** | The user, from using Chimecho |
| **Reason given** | Chimecho's initial settings make the delay very long and loud, and reeling them in for the short bubbly effect is almost impossible — the controls are confusing and there is no global delay length option |
| **Shape** | Start/Finish keep their job (the shape across the spectrum); Length scales the whole bank, 1× to 80×. Defaults 1.6–14 ms |

### BANDS — the defaults, set by listening

| | |
|---|---|
| **Decided** | Feedback 0.80 / 0.90, Wet 1.0, Dry 0.4, Length 0 |
| **By** | The user, choosing render `11-ringing-wet-forward` from a set of four |
| **Settled by** | Listening. The first defaults were inaudible and no numeric check had caught it |

**The failure the harness found.** The first defaults — feedback 0.25/0.45, wet 0.45 — produced
nothing a person could hear. The user reported dry, the short default and the interaural split as
identical. They were not identical: the render measured 2.76 dB of difference from the dry
reference. But the *kind* of difference was wrong.

Two measurements explain it. Sixteen bands spread over 1.6–14 ms is a 12 ms total spread, which
sits inside the window where the ear fuses arrivals into one event. And at feedback 0.25–0.45,
band 8 damped out in four repeats — 33 ms, still inside the transient. So the bank was acting as a
comb filter: the same drum, differently coloured. Measurable, inaudible.

**Short delays only work as an effect if the bands ring.** The same band at 0.85 feedback rings for
189 ms instead of 33 — a 5 ms delay at high feedback is a tuned resonator, and that is where the
bubble comes from. The fix was feedback, not longer delays.

**Wet is not a taste control on this effect.** Measured against the dry reference:

| Setting | Distance from dry |
|---|---|
| feedback 0.80/0.90, wet 0.45 | **−3.3 dB** — the bank rings, the dry buries it |
| same, wet 0.9 | **+2.7 dB** — crosses over |
| same, wet 1.0, dry 0.4 | **+4.2 dB** — chosen |
| feedback 0.88/0.93, spread 0.8–26 ms, dry 0.5 | +6.4 dB, but peaks at 1.10 — clips |

That sign change is the finding. Below it you are equalising the drums; above it you are hearing
the bank. A polite 0.45 wet is what made the first set inaudible, so the default sits well clear of
the line rather than near it.

**Length stays at 0.** Every approved render was at the short end, and that was the original
instruction: open extremely short, reach echoes by turning one knob.

### BANDS in NeoBeat — per-pad send, not a master send

| | |
|---|---|
| **Decided** | A small send knob on each kit sound, alongside the standard utilities |
| **By** | The user |
| **Supersedes** | ~~Master send off the finished mix~~ — chosen earlier the same evening on a bad cost estimate: per-pad was priced as "days, high risk", but `renderVoices` already accumulates per-voice sends into buses for the MaterialLab rooms, so it is the existing mechanism with one more bus |
| **Status** | Not built |

### The interaural window

Fixed at 0.7–10 ms, which is what the research document specifies. An earlier build used 12 ms,
which was unjustified.

### IDM Jack — the two open questions, answered

Both were recorded in [`research/transferable-mechanisms.md`](research/transferable-mechanisms.md)
on 26 Nov, and both were still listed as open in `PROPOSED-FEATURES.md`, written two hours
earlier. The features list has been corrected.

- **Overlay or replace** → neither: a **held, non-destructive takeover**. Chaos while held, the
  original pattern restored the moment it is released. The written pattern is never altered.
- **What it repeats** → whatever is sounding. The brief was *rapidly and randomly repeat drum
  parts* while each sound's parameters get pitched up, down, driven and hollowed.
- **Still open**: the third randomisation axis (micro-timing, 1–100 ms) came from the Density
  study, not from the brief.

### The randomiser and IDM Jack are different things

The **RANDOM room** is built, in **MidiJaQ** — one roll that reaches every room, with a per-target
reach, an on/off per target, and two hard exclusions. It does **not** exist in NeoBeat.
**IDM Jack** is a separate, unbuilt glitch mode. Conflating them was an error.

RANDOM's target list, from `midijaq/Source/app/Processor.cpp`:

| Room | Target | Reach | Default |
|---|---|---|---|
| KEYS | Zone mapping | 0.35 | on |
| KEYS | Attack / release | 0.20 | on |
| LOOPS | Chop order | 0.60 | on |
| LOOPS | Chop pitch | 0.25 | on |
| MAP | Which degrees | 0.45 | on |
| MAP | Note length | 0.30 | on |
| MAP | Traffic | 0.50 | on |
| MOD | Orb position | 0.70 | on |
| MOD | LFO rates | 0.15 | off |
| STEREOROOM | Chop places | 0.40 | on |
| STEREOROOM | Mono locks | — | **never** |
| RANDOM | Its own controls | — | **never** |

Two exclusions, with the reasons as given in the source: randomising a mono lock turns a mix
inside out and has to be undone by hand; letting the randomiser reach its own rate means its rate
randomises its rate. The struct comment names a third never-category, "the low end", which has no
entry in the list — **open: should it have one?**

### Chaos and randomisation — the settled structure

Decided 26 Nov, and it **supersedes the entry made earlier the same evening** which recorded the
randomiser as dropped outright. It was not dropped; the 19 November spec **split**. Its "one press
randomises everything" half became the global randomiser; its "speed, swing, extremity, cycling
rhythmically, landing back on the original" half became IDM Jack. What is dropped is MidiJaQ's
RANDOM room *implementation*, because it was laggy and was not controlling much.

Three separate things:

#### A — Sequence and sample randomisers. Untouched.

Batchcut's RANDOM (deals from the pool without repeating a sound until all have played), KIT's
Auto-fill / Shuffle / Random, MidiJaQ's per-row seeds and chop order. These pick **content**.
**No touching.**

#### B — The global randomiser. NeoBeat and MidiJaQ.

One press randomises the parameters of every single thing in the plugin, **once**. It is for
auditioning: press, listen, press again, until something lands. Modelled on Batchcut's kit
randomiser. In NeoBeat it reaches further than in MidiJaQ — each kit sound's own parameters, plus
the effects on the lanes. Not in Batchcut, which already has its own.

**Built clean.** `Source/app/RandomRoom.cpp` and its processor wiring (`makeRandomTargets`,
`rollRandom`, `toggleRandomTarget`) are to be **deleted**, not rebuilt on. The deletion happens as
part of building B, so MidiJaQ does not sit with a dead slot in its room list in the meantime.

#### Built 27 Nov — `nsr::Chaos`, the shared spine

`Source/neobeat/shared/Chaos.h`. One engine, two entry points: `rollOnce()` is the global
randomiser, `engage()`/`release()` is IDM Jack. What it may touch is a registered list of live
parameters — each with a range, a reach and a never-flag — which is the only way one engine can
seize "every imaginable parameter" in programs whose parameters have nothing in common.

Twenty-two assertions. Three bugs it caught, all in code written the same hour:

- **The randomiser stuck at the rails.** `rollOnce` moved each parameter *relative* to where it
  already sat, so after one press a value was at 0 or 1 and the next press clamped it to the same
  place about half the time — pressing again stopped changing anything, which defeats auditioning.
  The randomiser is a **destination** and IDM Jack is a **disturbance**; they need different
  functions, and the comment saying so was already in the file while both used the same one.
- **Repeat probability did nothing.** The window seed was mixed with the *absolute* lattice step,
  so two windows sharing a seed still produced different figures. Mixed with the step's position
  *within* the window it works: 100% recurrence at 1.0 against 0% at 0. That knob is the whole
  difference between motifs and noise.
- **One test error of mine**: a fresh rig starts at seed 1, so 200 "samples" of a reach
  measurement were 200 copies of a single draw.

**Known gap.** `Chaos.h` is in `batchcut-3.0` and `midijaq` only. NeoBeat has no `gen/Rng.h` at
all, so the header cannot compile there; bringing the RNG across is part of wiring IDM Jack into
NeoBeat, and the header has deliberately **not** been placed in that tree in the meantime.

**Known gap — level.** Seizing feedback upward while the delays are short stacks sixteen
resonators, and the first render peaked at 1.75. IDM Jack needs either a ceiling on what the
feedback targets may reach, or limiting on its output. Not yet decided.

#### C — IDM Jack. A room. NeoBeat and MidiJaQ, later JaqRack. Not Batchcut.

Switch it on and it takes over: continuous, uncontrollable seizures of every parameter and feature
in the plugin. The chaotic version of B, with options.

Settings, all approved 26 Nov:

| Setting | What it does |
|---|---|
| **Seizure targets** | Which of pitch, parameter, effect, retrigger, micro-timing it may grab — with a never-list for what it must not |
| **Density** | How often the lattice fires at all |
| **Extremity** | How far each seizure goes when it fires |
| **Repeat probability** | Whether the next window re-rolls or repeats — this is what makes motifs rather than noise |
| **Lattice division** | 16th-triplet is the default; 32nds and straight 16ths are different characters |
| **Self-randomise** | It rolls its own settings on switch-on, with a depth control so it can be mild or total |
| **Recovery** | How it lands back on release: hard cut, or decay over a bar |

Plus the base behaviour already specced: the invisible swung lattice (randomness decides whether
and how much, never where), held control with the original pattern restored on release and never
altered, seeded so a pass can be recovered, and three randomisation axes — pitch, parameter and
micro-timing in the 1–100 ms window.

### What NeoBeat has instead

A random *modulation source* (`ModSrc::Random`), draggable from the dock onto any knob, with
`randBeats` and `randScope`, saved in project state. Plus `panRandom` on Bounce Delay. No
randomiser.

### Documentation discipline

| | |
|---|---|
| **Decided** | Everything discussed gets a record in a document, and that document gets referenced specifically. Everything from November onward backed up into the repo |
| **By** | The user |
| **Implemented** | This file, [`REFERENCES.md`](REFERENCES.md), provenance headers in each module naming the spec it was built to, and the research documents committed as markdown |

---

## Still open — questions asked and not yet answered

| Question | Asked | Where it blocks |
|---|---|---|
| IDM Jack's third axis (micro-timing 1–100 ms) — confirmed? | 26 Nov | It came from research, not from the brief |
| NeoBeat's flat `Source/shared/` vs the other two trees' nested `Source/neobeat/shared/rooms/` | 26 Nov | Byte-identity across trees is impossible by construction; has produced two unbuildable headers so far |
