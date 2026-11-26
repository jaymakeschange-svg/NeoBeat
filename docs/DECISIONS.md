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

### The randomiser, as specified

| | |
|---|---|
| **Recorded in** | [`research/neosystem-build-list.md`](research/neosystem-build-list.md), 19 Nov |
| **The spec, verbatim** | *"one press randomises everything; right-click opens the box: speed, swing, extremity, cycling rhythmically and landing back on the original"* — a shared randomiser that walks a plugin's own parameter list. For NeoBeat and MidiJaQ. |
| **Built** | Partially, in MidiJaQ only |

What exists in MidiJaQ: the one-press roll, and the box (a per-target reach and an on/off, which
covers *extremity*). What exists nowhere: **speed**, **swing**, **cycling rhythmically**, and
**landing back on the original**. Those four are the character of it — not a one-shot roll but a
rhythmic cycle that returns home — and they are the part still to build.

The same document notes the mechanism it should ride on: *"the joystick is recordable, so a move
is a modulation like any other — which is also how the randomiser gets to cycle it rhythmically."*

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
| Should RANDOM's never-list include "the low end"? | 26 Nov | The struct comment names it; the list does not |
| IDM Jack's third axis (micro-timing 1–100 ms) — confirmed? | 26 Nov | It came from research, not from the brief |
| NeoBeat's flat `Source/shared/` vs the other two trees' nested `Source/neobeat/shared/rooms/` | 26 Nov | Byte-identity across trees is impossible by construction; has produced two unbuildable headers so far |
