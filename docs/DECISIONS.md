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

### IDM Jack — the two questions were answered on the morning of 26 Nov

~~An entry here previously claimed the answers were "held, non-destructive takeover" and
"whatever is sounding".~~ That was written from a stale copy of `PROPOSED-FEATURES.md`: the file had
been overwritten that evening with its 09:15 version, erasing the commits that recorded the real
answers. See *Restored 27 Nov* below. The answers, from the spec:

- **Overlay or replace — both, as a toggle.** Keep the written pattern and glitch over it, or clear
  it and run on kit sounds alone.
- **Source — everything in the kit.** Any lane is fair game; a burst picks from the whole kit.

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

**Closed 27 Nov — the RNG.** `gen/Rng.h` is now in NeoBeat's tree as `Source/shared/Rng.h`, so
`Chaos.h` and `Ceiling.h` are in all three and each compiles in each.

**Closed 27 Nov — level.** Limiting on the output, decided by the user over a feedback ceiling.
`nsr::Limiter` in `shared/Ceiling.h`: lookahead with a flat peak hold, fast attack, slow release,
and the knee underneath as a last resort. The moderate render needed 5.2 dB and now lands exactly
on the ceiling instead of at 1.75.

Three faults on the way, all in the limiter and all mine. The envelope started at **zero**, so
every render opened by fading up from silence through the release time and the "worst reduction"
reading was that startup ramp rather than anything the music did. Then the gain was computed from
the sample arriving rather than the loudest in the lookahead window, so a peak landing late in the
window had no time left to be reduced. Then the peak hold **decayed during** the window, so the
gain chased a target rising away from it and never arrived. A hold has to stay flat until the
delayed output has passed the peak.

**The knee is now shared.** `nsr::safetyKnee` in `shared/Ceiling.h`, lifted from NeoBeat's
`Engine::safety` with the arithmetic unchanged; `Engine::safety` is a one-line alias. Note that
`dsp/Metal.h` has a function of the same name doing a different job — it bends toward 1.8, not 1.0
— and is deliberately **not** merged.

**The panel, 27 Nov.** `shared/rooms/JackRoom.h` and `JackRoomUi.cpp`: ON, ROLL, RESTORE, the
lattice division, the seven settings, the five axis toggles, and the target list with the
never-targets struck through. Plain on purpose — enough to play with, so the room it becomes can
be drawn once it is known what the thing wants.

#### Wired into MidiJaQ, 27 Nov

JACK is room 6 of 7, in RANDOM's slot. `Source/app/RandomRoom.cpp` and `.h` are **deleted**, with
`makeRandomTargets`, `rollRandom` and `toggleRandomTarget` gone from the processor. The panel's
ROLL button is the global randomiser.

**What the old randomiser actually did**, found on reading it before deleting it. Of its twelve
listed targets:

| Target | What it did |
|---|---|
| KEYS zone mapping | **nothing** — no handler |
| KEYS attack / release | **nothing** — no handler |
| MAP traffic | **nothing** — matched no branch |
| LOOPS chop pitch | fell into the `room == "LOOPS"` branch and re-rolled the chop order |
| MOD LFO rates | fell into the `room == "MOD"` branch and moved the orb |
| the other five | worked |

Six of the ten active targets were dead or duplicated a sibling, and it used
`juce::Random::getSystemRandom()`, so no pass was ever reproducible and "landing back on the
original" was never possible. That is the whole of "wasn't really controlling much", and it
settles the delete-and-start-clean decision.

**What is registered now.** Eleven targets, every one of which does something: `LINE` traffic,
dialect, accent, gate and swing as plain floats — traffic included, which the old list named and
never touched — plus `MAP` which-degrees and note-length, `MOD` orb position, `LOOPS` chop order
and `STEREOROOM` chop places as actions, and the two never-targets. Four are **deliberately left
out and named in the source**: KEYS zone mapping and attack/release (the instrument is a const
`shared_ptr`), LOOPS chop pitch and MOD LFO rates (no handle on the processor). A registered
target that silently does nothing is the bug this replaces.

**`nsr::ChaosTarget` grew an escape hatch** for this: `apply`, `keep` and `giveBack`, for the
things that are not a single float — a permutation, an integer row, a pair of orb coordinates.

**Jack runs on the message thread**, driven by the panel's timer from a transport position the
processor publishes. `advance()` walks a vector of `std::function` and can allocate, so it has no
business in `processBlock`. It seizes parameters, which need the right beat rather than the right
sample. The retrigger and micro-timing events it reports have **no audio-thread path yet**.

**Pre-existing, found not caused.** MidiJaQ's host test fails 5 of its checks on every seed — and
fails identically at the commit before any of this work. Notes not sending, accent not changing,
undo and redo, two lines on their own channels. Unrelated to Jack, and it means pushes have been
going out with it red.

#### Where `nsr::Chaos` conflicts with the IDM Jack spec

Found 27 Nov, once the spec was restored. The engine was built the evening before against the stale
file and gets four things wrong. **The spec is the user's and wins.**

| The spec says | `nsr::Chaos` does |
|---|---|
| Parameter chaos **must be an overlay applied at playback — per-hit offsets over the stored values, never written back.** "Cheap to get right at design time and expensive afterwards." | **Writes back** to the live values and restores them on release. Saving a project while held saves the scrambled values; editing a parameter while held is clobbered on release; closing the editor while held (it is driven by the panel's timer) leaves them scrambled |
| **Each repeat in a burst gets its own parameter offsets** — what separates it from an ordinary stutter | Seizes globally at a lattice step; every hit until the next step shares one value |
| **Motifs need a memory, not just a seed.** Per-position randomness gives texture. A **figure buffer** stores which lanes, which positions, how many retriggers *and the parameter moves*, and replays stored figures with some probability | Seed plus repeat probability only. The claim made that evening — that repeat probability "is what makes motifs rather than noise" — is contradicted by the spec |
| **Overlay or replace, as a toggle** | Not built |

What survives unchanged: the lattice, the seed per window, density, extremity, the never-list,
self-randomise, and the global randomiser's `rollOnce()` — which **should** write back, because the
randomiser is a destination and the spec's overlay rule is about Jack.

**Consequence for the architecture.** An overlay applied per hit belongs where hits happen: in the
engine, on the audio thread, at voice trigger. That also removes the message-thread driving, the
save-while-held problem and the editor-closed problem in one move — they all came from writing
back.

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

### Restored 27 Nov — `PROPOSED-FEATURES.md`

On the evening of 26 Nov, rebuilding the docs, the file was taken from its 09:15 commit and copied
over all three trees. That erased five later commits: the answered IDM Jack spec (849e695,
db02512), the three settled decisions (eed01a7), interiority and detector-driven placement
(6851506). It is why the user had to say "I already answered those questions" while the answers
could not be found — they had been deleted.

Restored from the 16:09 version (`875ad01`) with the evening's decisions re-applied on top. The
lesson is the same one this file exists for, one level down: the newest version of a document has
to be found by asking git which commit is last, not by remembering which one was read first.

## 27 November 2026 — the test build: the randomiser and IDM Jack in NeoBeat

**Asked (the user, 27 Nov):** both today, to test. *"Do rando first, and after that do jack. Do not
give two separate downloads. Only ship when both are finished."* The repo is made public so the
Windows build runs. On Jack's targets: *"per pad. So in the jack room, there should be an option to
choose which pads are going in. Meaning, if I choose the kick, all its parameters are affected."*
On the panel: *"no need for a fancy UI, just make it usable for a test."*

Both live in a fifth NeoBeat room, **JACK** (`Source/ui/JackRoom.cpp`). MidiJaQ is not in this
build — see the end of this section.

### B — the global randomiser: ROLL

`NeoBeatProcessor::roll()` → `nsr::Chaos::rollOnce()` over sixteen lane targets → `rollPad()`.
One press, every included lane once: its own parameters, through the host parameters so the host
sees them, and the parameters of every effect already on that lane (library effects, Bounce Delay,
Friction Stretch). It **writes** - a destination, not a visit - and each press is **one undo step
in the JACK room**. Left alone, deliberately: which sound is on a lane, which effects are on it,
choke, mute. Changing those is rebuilding the kit, not auditioning it.

| Parameter | How far one press moves it |
|---|---|
| tune | half its range (±12 st of travel) from where it is |
| decay, drive, hollow | 80 % of range, from where it is |
| tone, evolve | anywhere |
| pan / width | 60 % / 70 %, from where it is |
| level | **a fixed window, −9 to +1 dB, picked fresh each press** (see below) |
| lane effects | library effects 80 % of each declared range; Bounce and Friction across set ranges |

**Changed during testing — level.** First built as a walk (reach 0.2, about ±6 dB from wherever it
was). The first snapshot landed a lane on +6.0 dB, the ceiling, at the first press; a walk drifts
some lanes towards silence and pins others over a dozen presses. Claude's call, recorded here.

### C — IDM Jack, built to the spec in `PROPOSED-FEATURES.md`

**Where it runs.** On the audio thread, in the workstation (`Source/engine/Workstation.h`), with
the engine in `Source/shared/JackCore.h` (`nsr::JackCore`: lattice, windows, figure buffer, per-hit
offsets; realtime-safe, no allocation). Not on `nsr::Chaos`, which was ruled out for Jack on 26
Nov because it writes parameters back.

| Spec item | How it is built |
|---|---|
| Parameter chaos is an overlay, never written back | Offsets go onto the copy of a hit's settings `hitPad` already makes. Nothing stored is touched; release is the return to the original by construction. **Tested** (below). |
| Each repeat its own offsets | Every hit of a burst carries its own drawn offsets. Pattern hits of an included lane get offsets too, keyed on where they fall, so a pass replays the same from its seed |
| The lattice: 16th-triplet swung, invisible | `division` per beat (1/4, 1/6, 1/8, 1/12; 1/6 default), swing within the group. Bursts are planned when their step is reached and fire sample-accurately, however a step falls across blocks |
| Density, extremity, repeat probability, windows | As specced: a window of steps shares a seed; repeat decides whether the next window reuses it |
| Figure buffer (motifs need a memory) | RECALL: how often a burst replays a stored figure instead of a new one; MEMORY: how many it keeps (1–16). Recalled figures drop lanes no longer included |
| Overlay or replace — both, as a toggle | OVERLAY / REPLACE. Replace silences the written pattern (sequencer, Layermax, follows) while held. KEY MAP marks keep playing: they are held notes with their own endings, and cutting them could leave one sounding |
| Held primary | ON. Chaos while on; let go, the original back on the next block |
| Recovery | 0 = hard cut. Above that: no new bursts, and pattern-hit offsets fade out over that many bars. Without a transport there is nothing to fade over, so it cuts |
| Self-randomise, with depth | Rolls Jack's own settings at switch-on, at that depth, **seeded by the pass** - so a locked seed replays the same rolled settings too. Never reaches itself, the lanes or the seed |
| Seizure targets — per pad | LANES: sixteen toggles, ALL, NONE. A lane in is all of its parameters in. Nothing in + overlay = the plain kit, sample for sample (tested) |
| Never-list | The lane toggles plus the axes. Never seized at all: which sound, which effects, choke, mute |
| Micro-timing 1–100 ms | TIMING sets the latest a hit may land |
| Capture in Ableton | ON is also the host parameter **"IDM Jack"** - map it to a pad or a key, or automate it, and record the output |

**What reaches which lane.** Pitch, pan and level reach every hit. Decay, tone, drive, hollow,
evolve and width shape the synthesis, so they reach live voices only: a lane whose effects need its
sound **prepared ahead** (Friction Stretch, the spectral library effects) hears pitch, pan and level
from Jack and not the rest. Said in the PARAM axis's tooltip.

**Burst hits enter as played hits, not as pattern hits.** Two consequences. A burst hit on a lane
whose spectral effect has no prepared copy of the sound yet can be late by up to 43 ms per effect,
and NeoBeat's existing notice about that ("ONE HIT NEEDED A SHORT BUFFER") may appear once - the
same as for a key press. And hits played by hand (KEY, PADS) get their offsets keyed on the hit
count as well as the position: they all arrive at the block's position, so without it every press
on a lane would get the same offsets (found reading the code, 27 Nov, before testing).

**The EFFECT axis is off in NeoBeat.** Lane effects are prepared ahead of the hit, so there is no
per-hit effect parameter for Jack to move; the switch is left off the panel rather than shown doing
nothing. In MidiJaQ it has things to act on.

**Output limiting** (asked 26 Nov: *"Limiting on the output"*): `nsr::PeakLimiter` in
`Ceiling.h` - zero latency, instant attack, 120 ms release - sits in front of the safety knee in
`Engine::finishMix`, in only while Jack works and for a second after. Off, it multiplies by exactly
1. At full density and extremity on the test kit it never acted (peak 0.60): it is a guard.

### Claude's calls in this build — for the user to keep or overrule

1. **A new seed at every switch-on**, shown on the panel; **LOCK** keeps it, so a take worth keeping
   can be played again. The spec says "seeded so a pass can be recovered" and nothing about when the
   seed changes.
2. **Self-randomise does not move the knobs.** The panel keeps the user's settings; what the pass is
   actually using is shown on the status line. Moving the knobs would make the settings drift
   further with every switch-on.
3. **ON as a host parameter.** For mapping and for capture. It is saved with the session like any
   host parameter, so **loading a state lets it go** - a session saved mid-seizure would otherwise
   open seizing (found by the host test, below).
4. **Jack's lane list is ROLL's lane list.** One list of which lanes are in, for both. The user
   asked for per-pad choice in the Jack room; sharing it with ROLL was not asked.
5. **Burst hits play through the same path as any played hit**, so they advance the per-hit random
   seed the kit's own variation uses. After a release the stored parameters are exactly as they
   were; the hits after it are not sample-identical to a run that never engaged Jack, in the same
   way any extra played note makes them differ.

### Tests

| Test | Result |
|---|---|
| Jack off vs the engine before any of this work (`7a3835b`), three presets, and kit 3 (14 body lanes) | **Bit-identical** |
| `tools/test_jack.cpp` - the real workstation: overlay never written back (after a held pass, after release); nothing included = the plain kit, sample for sample; replace silences the pattern; same seed = same pass, sample for sample, and a different seed differs; hard cut idle next block; recovery idle after its bar; no transport = no bursts; one-bar loop keeps firing; full density and extremity under full scale | **All pass**, added to the Windows CI |
| `NeoBeatHostTest` with ROLL, Jack held/let go, random Jack settings and lanes in its random edits, the JACK room in its room tour and monkey, and a check that a held state opens let go. Now takes a seed (`NeoBeatHostTest 20 <seed>`) and replays a saved state (`--state file`) | Seeds 7, 2026, 31337, 5, 1234 on the final code: **all pass**. Seed 7 failed before the body fix below |
| The existing engine tests: `test_sequencer`, `test_room`, `test_analysis`, `test_livefx`, `test_fx`, `test_workstation` | **All pass** |
| `NeoBeatSnapshot` 19–21: the room idle, held (bursts and seizures counting, three lanes out, self-rolled settings shown), and a lane after ROLL | Looked at |

### Found and fixed on the way

- **Pre-existing: MaterialLab body sounds went NaN at +72 st and above.** Found by the host test on
  seed 7, in KEY mode: a high MIDI note transposes a lane by up to about +67 st on its own. Eleven of
  the forty body and rubber sounds broke (Clank and PVC Pipe variants). Cause: the body model
  (`Source/dsp/MaterialBody.h`) scales an elastic pitch term by energy relative to frame 1; that far
  up every mode is damped to nothing by frame 1, the ratio runs to 1e19, and `log1p` is handed less
  than −1. Bounded both; all forty finite at every transposition tried; output **bit-identical** to
  before on the kit with fourteen body lanes. Jack made it easier to reach (its pitch axis adds up to
  ±12 st), which is how it surfaced. A NaN in a DAW can silence the track it sits on until reload.
- **A one-bar loop stalled the lattice** (mine): a jump back re-aimed only when more than four
  beats out. Now any jump back re-aims. Test 7.
- **A test of mine was wrong**: its "plain kit" reference had Jack held from −1 s, which made three
  checks fail against a corrupted reference. Fixed before any result was used.

### Not in this build

- **MidiJaQ's Jack** still runs on `nsr::Chaos`, which writes back - the flaw recorded above. It is
  to move onto `JackCore` next; not touched here, so MidiJaQ is unchanged by this build.
- The EFFECT axis in NeoBeat, and offsets on lanes with prepared effects beyond pitch, pan and level.
- The panel is a test panel, per the user.

---

### Next in the same build: BANDS with per-lane sends, and the preset bar (27 Nov, 11:38)

**Asked (the user):** *"I want you to include the bands, per pad sending, and preset menu."* In
the same NeoBeat build as ROLL and Jack - still one download.

| Question | Answer (the user, 27 Nov) |
|---|---|
| Which preset menu? | **Batchcut's preset bar** - arrows, and a list that auditions on hover and puts back what was loaded if nothing is clicked. The shared component, so the family stays standardised. Not the category browser (still proposed) |
| Where does BANDS live? | **A window**, opened from the header next to FRICTION and BOUNCE, as NeoBeat's effects already are. Not a sixth room |
| The send | A small knob per lane, with the lane's standard utilities (decided 26 Nov, "BANDS in NeoBeat - per-pad send"), plus an ALL control so every lane can go through at once |

**Then, 27 Nov 12:47-14:42 (the user):** *"did you include the techno and house new presets for
batchcut and neobeat?"* - no, in neither. *"Finish working on neobeat, dont build yet, and ship it
with those presets."* So NeoBeat ships with them; Batchcut's are on the to-do list
(`PROPOSED-FEATURES.md`, last section).

#### How BANDS was built

- **A lane's send keeps the by-ear balance.** The defaults were chosen with the drums going
  *through* the bank (wet 1.0 over dry 0.4). As a send, a lane at *s* puts *s* of itself on the bus
  and keeps `1 - s(1 - Dry)` of its direct path; the bank adds wet only. So a lane at full sounds
  the way the bank was set by ear, and at 0 nothing about it changes. The window's **Dry** knob is
  therefore "how much of a fully-sent lane's own sound remains". Claude's call.
- **Where the send taps**: at the one place each sound path adds a lane to the mix - live voices,
  prepared copies, live effect units (`BandSend` in `Rt.h`). So a lane with effects sends its
  *effected* sound. Not lane-separated, so not sent: a MaterialLab place's room return, and
  MATRIXXXER marks.
- **ALL** (host parameter "Bands All") sends every lane at least that much. Each lane's knob is the
  host parameter "N Bands"; the bank's settings and band positions save with the session.
- **Costs nothing unused**: the bank runs only while something sends or its tail rings, and stops
  after half a second of silence. **Exports include it** (stems too).
- **ROLL leaves the send alone**: it is a routing utility, like mute or choke, not a sound
  parameter. Claude's call.
- **Header re-spaced** to fit the BANDS button: the LCD keeps its width (a first try narrowed it
  and truncated the pattern name - caught in the snapshot), the space came from around MASTER.

#### How the preset bar was built

- It is Batchcut's shared component. NeoBeat's patterns are built in, not files, so the bar gained
  an **item mode** (a list of names, loaded by index) beside its folder mode; Batchcut and MidiJaQ
  use the folder mode, unchanged. **The audition length became settable**: Batchcut's 1.2 s is under
  a bar of a NeoBeat pattern, so NeoBeat plays one bar at the current tempo.
- **What it lists**: the 144 factory patterns, then the ones saved with SAVE (still marked MINE |).
- **Hover**: a row loads into the current scene and plays; moving off the list puts back the
  pattern, swing, tempo and number that were there; a click keeps it. With the host stopped, the
  audition runs NeoBeat's own transport (a flag, not the host parameter "Run", so nothing is written
  to the host's automation). Hovering loads count as edits, so they can leave steps in the room's
  undo history.

#### The house and techno patterns — 129 to 144

Written from `docs/research/New jack swing parts and grooves.md` (MidiJaQ tree), "House and techno
invert which lane carries the motion". **Appended after the 128**, so no existing pattern or number
changes (checked: the first 128 are identical to before).

| # | Name | Style it follows | BPM | Swing |
|---|---|---|---|---|
| 129 | Chicago Jack | Chicago house: 4-floor + extra offbeat + ghost kick, snare and clap on 2 and 4, clap on step 16, alternating 16th hats, 707-style rim on even 16ths | 122 | 0.52 |
| 130 | Piston Floor | Chicago house | 124 | 0.54 |
| 131 | Warehouse Stab | Chicago house, stabs between the kicks | 123 | 0.53 |
| 132 | Garage Shuffle | Garage house: the shuffled one; extra kick in bar 2; clap 4 % of a step early, beat 4 of bar 2 pushed 12 % (the "jack"); noise hit after kick 1 | 122 | 0.74 |
| 133 | Jersey Sunday | Garage house, stabs | 121 | 0.72 |
| 134 | Night Bus Garage | Garage house | 123 | 0.70 |
| 135 | Pearl Basement | Deep house, Detroit-style cross-rhythm rim | 120 | 0.56 |
| 136 | Swing House | **Crossover**: house skeleton + new jack swing's snare on 2 late / 4 straight, swung pickup kick at the bar's tail, triplet hat roll at the phrase end | 120 | 0.66 |
| 137 | Motor Grid | Detroit techno: invariant kick, two hat layers, a dotted-eighth clank layer | 124 | 0.57 |
| 138 | Belleville Steel | Detroit techno: cross-rhythm rim never on a kick | 122 | 0.58 |
| 139 | Assembly Line | Detroit techno, 5- and 7-step layers | 125 | 0.56 |
| 140 | Clank Polymeter | Detroit techno: 3, 5 and 7-step layers at once | 126 | 0.58 |
| 141 | Minimal Rubber | Minimal techno | 128 | 0.55 |
| 142 | Click Floor | Minimal techno | 130 | 0.56 |
| 143 | Tunnel Pressure | Club techno: a low lane kept off the kick steps (a contemporary tech-house rule, not 1980s practice - the research says so) | 132 | 0.57 |
| 144 | Swing Techno | **Crossover**, as Swing House, on a techno skeleton | 124 | 0.62 |

Rules every one holds (checked by a program, not by eye): the kick on every beat; the open hat on
every offbeat eighth; the rim never on a kick; swing inside NeoBeat's 0.50-0.75. **No fills** - the
research's techno arrangement rule is that a fill every 8 bars is already too many. Swing is
NeoBeat's own scale, the MPC one (0.50 straight); the skeleton sits on even 16ths, which swing never
moves, so only the 16th hats swing, which is the scope the research describes for these genres.

**Limits, said plainly.** The figures are magazine craft recipes, not measurements (the research
found none), so they are defaults to trust your ears over. A pattern can't tune a lane, so garage's
"extra kick tuned down two semitones" is on the Kick 2 lane at whatever the kit has there. The
garage "jack" push is on the Snare lane, because a nudge applies to a whole lane and that one hit
needed more than the clap's. Nobody has listened to them yet.

#### Found on the way

- **The host test died, not stalled**: the monkey clicked the preset bar, whose list opens as a
  desktop window; the test's virtual display has no window manager and refused it (X `BadAtom`),
  and the process exits. A test-environment limit - the same list works on Windows in Batchcut.
  The monkey now leaves the bar's list alone, as it already did combo boxes, and section 6b tests
  the bar's real wiring in NeoBeat: load, put back (pattern, swing, number), audition on and off.

### Tests for this part of the build

| Test | Result |
|---|---|
| Every send at 0 (BANDS in the build) vs the engine before any of this work, kits 0 and 3 | **Bit-identical** |
| `test_jack` section 9 - BANDS: sends at 0 = the plain kit and the bank never runs; a lane sending runs it and changes the sound; ALL; wet 0 leaves a fully-sent lane at its Dry level (measured 0.0234 against 0.0225 expected - the difference is the room returns and marks, which are not sent); the bank stops once its tail dies | **All pass** |
| House and techno rules, by program; the first 128 patterns unchanged | **Hold** |
| All engine tests | **Pass** |
| Host test with the bar's item mode (4b), its wiring (6b), BANDS sends and ALL in the random edits and the window in the room tour | Seeds 7, 2026, 31337, 5, 1234 on the final code: **all pass** |
| Snapshots 22-25: a lane's BANDS knob, the window, ALL following its parameter (it did not at first - fixed), the bar at 1/144 | Looked at |


## Still open — questions asked and not yet answered

| Question | Asked | Where it blocks |
|---|---|---|
| IDM Jack's third axis (micro-timing 1–100 ms) — confirmed? | 26 Nov | It came from research, not from the brief |
| The five calls in "Claude's calls in this build" (27 Nov): seed per switch-on with LOCK, self-randomise not moving the knobs, ON as a host parameter, one lane list for Jack and ROLL, burst hits advancing the per-hit seed | 27 Nov | Nothing - built that way for the test; any can change |
| NeoBeat's flat `Source/shared/` vs the other two trees' nested `Source/neobeat/shared/rooms/` | 26 Nov | Byte-identity across trees is impossible by construction; has produced two unbuildable headers so far |
