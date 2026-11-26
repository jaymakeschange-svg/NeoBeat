# The Neosystem build list

The Neosystem build list

2026-11-19 · Hope

Everything in your note, turned into work: what changes in each plugin, how each change will be made, and the order it comes in. Nothing here is built yet.

Four things the survey turned up

I read through NeoBeat, Batchcut and MidiJaQ before planning any of this. Four findings change what the work actually costs.

**1. The real-time effects already exist.** `Source/dsp/LiveFx.h` in NeoBeat is all 24 Matrixxxer effects *and* Bounce Delay as streaming processors — sound in block by block, a moved control heard in the next block, even on a tail still ringing. So \\"make Bounce Delay real time\\" is largely already done, and the Orb's effect stack has its engine waiting. This is the single biggest saving in your whole note.

**2. What actually makes Bounce Delay click is that nothing is smoothed.** Every parameter is read raw, with no ramp anywhere: pan and wet are straight multiplies, Speed jumps the resample ratio (so the pitch steps), and moving Rate, Bounciness or Grid re-lays the landing list *underneath bounces that are already sounding*, so their timings and gains jump. That is a precise, fixable problem, not a rewrite.

**3. NeoBeat is still on the old file format.** Batchcut and MidiJaQ write `.NJQ` version 2, which carries a `kind` (kit, loop, instrument) and keeps anything it does not understand. NeoBeat's copy is version 1 — about 28 lines behind — so it quietly drops what it does not know when it saves. Categories have to ride in that manifest, so NeoBeat needs the newer header before the preset browser can work.

**4. The look is not shared, and there are no categories anywhere.** NeoBeat draws from `Source/ui/UI.h` and `Look.cpp`; Batchcut and MidiJaQ from `Source/app/Ui.h`. Same look, two codebases, every colour hard-coded. And nothing — not a `.NJQ`, not a saved preset — carries a category today. Both are worth fixing properly once rather than three times badly.

Built once, used three times

Six of the things in your note are the same thing in two or three plugins. Each is written once into the shared headers (`Source/neobeat/`, copied identical into every tree) and then wired up per plugin, which is usually an afternoon rather than a week.

The piece

Where it is written

Who uses it

**Assigned folders** — one per kind: WAV, MIDI, `.NJQ`, `.NJQi`. Remembered after every save; importing stays loose

`shared/Njq.h`, the `Settings` struct, plus a settings panel

All three

**Categories** — a category written into the manifest, so a file recalls under the category it was given

`shared/Njq.h` (manifest), the picker in Batchcut, the browser in the other two

All three

**The browser** — one list, no factory-and-mine split, grouped by category, in the manner of Omnisphere or Diva

A shared component, fed a list and a category per entry

NeoBeat, MidiJaQ

**The randomiser** — one press randomises everything; right-click opens the box: speed, swing, extremity, cycling rhythmically and landing back on the original

A shared randomiser that walks a plugin's own parameter list

NeoBeat, MidiJaQ

**The stereo room** — elements placed in a picture of the stereo image, each with a drawn rhythmic modulation

A shared room; each plugin gives it its own elements and graphic

NeoBeat, MidiJaQ

**The Orb** — 8 sequences, a joystick between them, stacked effects on the way

A shared module over `LiveFx.h`

MidiJaQ (full), NeoBeat (simpler)

Skins are the seventh, and the odd one out: NeoBeat's drawing code and Batchcut's are two separate files that happen to look alike. Doing skins properly means merging them into one shared `Ui.h` first — half a day of moving code with no visible change — and then a skin is just a named set of colours, and a new one costs an hour.

**The stereo room, from the pictures you sent** (19 November). Two of them are mix diagrams — coloured bubbles in a box, pan across, frequency up, distance into the picture, each bubble's size showing how much space it takes. The third is RØDE's SoundField, which is precision for one element: a top-down and a side view with a head in the middle and exact angles.

The room will have both. The bubble field is the main picture, so the whole mix reads at a glance; the pair of circles below it places whichever element you have picked, exactly. Each element also carries a drawn modulation that runs on the swung grid, which is what makes a hat bounce left to right in time rather than sit still.

The depth axis is not cosmetic. NeoBeat already models distance the way Chowning describes it — the direct sound falling as 1/r, the room only as 1/√r, air taking the highs, Doppler when something moves — and `Space.h` adds the early lateral reflections that give a sound size without smearing its strike. Dragging a bubble back will move it, not just turn it down. That is a large part of this room already written.

MidiJaQ

The longest build of the three, because it is the least finished: it has a sequencer, a clock, MIDI out and instrument playback, and nothing else from your note.

Change

How it will be made

Size

**The Orb room**

8 slots, each holding a sequence. A joystick gives a weight to each; the weights drive both the mix and the effects. The effects are `LiveFx.h`'s, which already run live and already take a moved control mid-sound — the Orb's work is the geometry (where the joystick is, what that means for each slot) and the stacking: several effects on one voice, in order, without the sound dying between them. The joystick is recordable, so a move becomes a modulation like any other

**Largest**

**The stereo room**

The shared room, with MidiJaQ's elements: bass mono with width, plucks and strings placed, each with a drawn rhythmic path

Large

**Multis: layering ****`.NJQ`**** and ****`.NJQi`**** on one key**

Today one instrument plays at a time. This becomes a small rack: several sequences and instruments, each with a key range, playing together — so C1 can be a piano figure and a bassline at once

Large

**Sequences across the keys**, as NeoBeat does them

NeoBeat's own key handling, lifted: a held key transposes the running sequence. Its engine is already written and you are happy with it, so this is a port, not a design

Medium

**The randomiser**

The shared one, walking MidiJaQ's parameters: sequence, tuning, effects, everything

Medium

**`.NJQi`**

It already opens `.NJQ` instruments. It keeps doing so, and opens `.NJQi` as well, so nothing you have saved stops working

Small

**Bounce Delay and the Matrixxxer effects**

`LiveFx.h` is shared in as it stands, once its smoothing is fixed in NeoBeat

Small, after NeoBeat

**Browser, categories, folders, skin**

The shared pieces, wired up

Small each

**What is already on the branch.** The `midijaq` branch has five commits from 17 November on top of mine, and they are not a separate MidiJaQ — they build on the same code. They added a real `Source/core/Session.h`: **128 slots**, **four tracks**, a **chord track** (root, quality, inversion, bass note, a progression over 8 bars) with a **Parts** mode beside Free, Follow and Bass, and **five generators** — JAQ, HABIT, DRIFT, ORBIT and CUES — with evolving rounds and a chain. It builds and passes on Windows.

So three things I had down as outstanding are in fact started. Where it looks thin, and what I would expect to be behind \\"barely usable\\": the four generators other than JAQ mostly decide *which steps are allowed* rather than writing material of their own, and nine new dropdowns were added to a side panel built for six controls, with no room of their own. **Before I touch it, tell me what is unusable about it when you play it** — that will aim the work far better than my reading of the code.

**Live MIDI out comes out** (decided 19 November). Neither MidiJaQ nor NeoBeat sends MIDI to other plugins any more; a Max for Live device will do that, built after everything else. Batchcut keeps writing MIDI files and NeoBeat keeps exporting MIDI sequences.

This makes MidiJaQ simpler and better defined: it is an instrument that plays its own sequences through its own sounds, not something to route. Its plugin type does not change — it is already an instrument — and the \\"second MIDI track\\" setup disappears. Two things follow: playing `.NJQi` instruments stops being optional, since it is now the only way to hear anything; and MidiJaQ should probably export MIDI files like the other two, or a line made in it cannot leave it at all until the Max device exists.

Small one while it is open: the **\\"the melodic one\\" line comes out of MidiJaQ's header**. The plugins are named the same plain way, with no tagline.

NeoBeat

Change

How it will be made

Size

**The stereo room**

A picture of the stereo image with the drum parts placed in it: hats free to move left, right, forward and back; kick and snare held in the centre, the kick mono but able to be given width and brought forward or back. Each element gets a drawn modulation that runs rhythmically, on the swung grid, so a hat bounces left to right in time. Per-lane pan and width already exist and are already saved in the `.NJQ`, so this room is drawing and modulation over wiring that is in place

Large

**The Orb**, the simpler one

The same module as MidiJaQ's, with fewer slots and the sequence-level effects rather than per-voice. Built second, so it inherits everything learned in MidiJaQ

Medium, after MidiJaQ

**The preset browser**

One list, no \\"MINE |\\" split: every pattern, kit and `.NJQ` together, grouped by the category each carries, in a dropdown that behaves like Omnisphere's or Diva's. Needs the newer `Njq.h` first, so a category can live in the file

Medium

**Bounce Delay, smoothed**

Not a rewrite — `LiveFx.h` already runs it live. Each parameter gets a ramp over a few milliseconds; pan and wet become ramped gains; Speed's resample ratio glides instead of stepping; and a change to Rate, Bounciness or Grid re-plans only the bounces that have not started, leaving the ones already ringing alone. That last one is what makes it feel like an instrument rather than a render

Medium

**The randomiser**

The shared one, over NeoBeat's parameters: sequence, kit, tuning, effects

Medium

**The newer ****`.NJQ`**

Its `Source/shared/Njq.h` is replaced with the version 2 copy the other two already use, so it stops dropping what it does not recognise, and can carry categories

Small

**Assigned folders**

The shared Settings. Today every save dialog resets to a computed default — this is the fix for the thing that slows you down daily

Small

**Opening ****`.NJQi`**

It does not open instruments and is not meant to: `.NJQi` is MidiJaQ's. NeoBeat will say so plainly rather than failing oddly

Small

**Skins**

After the two drawing files are merged into one shared `Ui.h`

Medium once, then an hour each

Batchcut

The shortest build, and the one that pays off every day you use it. All of it is small.

Change

How it will be made

Size

**Assigned folders**

A settings page: a folder each for WAV, MIDI, `.NJQ` and `.NJQi`. Every save dialog opens in its own folder and writes back where you actually chose, so it never resets. Importing stays loose, as you asked. This is the fix for the thing that makes you hunt through your drive

Small

**Clear all in every room**

LOOPS and KEYS get one. The three rooms share one pool, so each gets both: clear the pool, and clear what that room built (the layers, the instrument) — with the same confirm the Cut room already uses

Small

**`.NJQi`**** for instruments**

KEYS writes `.NJQi` from now on, and opens both. MidiJaQ opens both, so nothing already saved goes missing. NeoBeat keeps ignoring them

Small

**Categories on export**

A box beside the name when you save: texture, clubby, experimental, or one you type. It goes into the file's manifest, and NeoBeat and MidiJaQ group by it. Batchcut is the program editor, so this is where categories are assigned

Small

**Skin**

Follows the shared `Ui.h`

Small

One thing worth deciding while we are here: the instruments folder is currently a fixed sibling of the preset folder, which is part of why saving feels rigid. With folders assigned per kind, it simply becomes whichever folder you point `.NJQi` at.

JaqRack

It stays last, and not only because it is the biggest. Everything in it is a lighter version of something that has to exist first: the Orb it modulates with, the rack versions of NeoBeat and MidiJaQ, and the effects the wires carry. Build it before those and it gets built twice.

What it needs, when it comes:

**The modules.** NeoBeat and MidiJaQ as rack versions — the same engines, fewer rooms. The limitation is the point: what they give up, the rack supplies.

**The patch bay.** A room of its own where wires are drawn between modules: a hat in NeoBeat exciting a pluck in MidiJaQ, a bass exciting a clap. A line at the bottom of the window says in plain words what the wire under the mouse does. I will bring you documented modular patching practice — what is usually wired to what, and why — and we go through it a wire at a time rather than my guessing.

**Transwave playback.** Single samples played Ensoniq-style, swept through a wavetable, alongside the sequences. This is close to work already in NeoBeat (`WaveVoice.h`, `CycleVoice.h`), so it starts from there.

**`.JRQ`**** patches.** Its own file: the rack, its modules, the wires, the Orb's position. It does not open `.NJQ` or `.NJQi` directly — its modules do.

The honest sizing: JaqRack is bigger than NeoBeat and MidiJaQ put together, and it is the one piece in your note where I would want a plan of its own before a line is written.

How the Orb could work

Research into the two things you named, and into what actually sits under them. The short version: **vector synthesis and morphing are different operations**, and the Orb is interesting precisely because it does both at once.

Vector synthesis is a mix, not a morph

On the Prophet VS and the Wavestation, vector synthesis is dynamic timbre control over two or four sources: the volume blend between oscillators is varied by a dedicated mix envelope, by the joystick, or by other modulation such as LFOs and aftertouch. On the Prophet VS the transition from waveform to waveform was a simple crossfade — the Wavestation's leap was adding wave sequencing underneath it, not changing the blend.

Two details are worth copying exactly. First, the joystick and the envelope work together: when envelope modulation is on, the joystick sets the envelope's start position rather than fighting it. Second, on the modern Wavestate the vector envelope and joystick mix the four layers *and* can modulate any other parameter — which is exactly what you want the Orb for: the position is a modulation source, not just a mixer.

Morphing is a different operation, and crossfading is not it

This is the part worth being precise about, because it is where most \\"morph\\" controls disappoint. Cross-fading two spectrograms does not produce convincing morphs — the components have to be matched first. Zynaptiq say the same of their own field: what is usually called audio morphing is a simple interpolation of parameter values or spectral data, where theirs builds wire-frame models of both inputs in real time and shapes one into the other.

The documented techniques, cheapest first:

Technique

What it does

Cost

**Log-magnitude interpolation**

Interpolates the magnitude spectra, keeping A's phase

One FFT pair

**Full complex**

Interpolates magnitude *and* unwrapped phase

Same, more smearing

**Formant / envelope**

Separates the cepstral envelope from the fine structure and interpolates the envelope, the excitation staying from A

Slightly more

**Sinusoidal modelling**

Spectral envelope morphing, interpolation of partial frequencies, and temporal envelope morphing, built on partial-to-partial correspondences between the two sounds

Expensive, and the most convincing

Two tricks from Zynaptiq worth taking. They compute both directions at once — A shaped toward B, and B shaped toward A — and the two sound completely different, which is free material from the same analysis. And their transient bypass splits the transients out of both inputs before the morph and adds them back afterwards. For percussion that is not a nicety: morph the spectra of two hits and you get mush unless the attacks are taken out of the maths and put back on top.

Morphing between effects

Two honest options, and they are not the same. **Interpolate the parameters** when both ends are the same effect with the same topology — the way Kore 2 did it across a grid of variations, true parameter interpolation rather than crossfading sounds, which only works for continuous parameters. **Crossfade the outputs** when they are different effects. For the crossfade, the DAFx work on power-complementary fading with curves derived from the correlation between the two signals is the right reference: equal-power is the usual answer, but it is only exactly right for uncorrelated signals, and two effects fed the same sequence are anything but uncorrelated.

What I would build

The Orb as three layers over one position:

**The mix** — Wavestation's own operation: eight slots, the joystick's position giving each a weight, with a looping envelope that the joystick offsets rather than overrides.

**The morph** — one analysis, not eight chains. Each slot's sound is analysed into the same spectral frame; the position interpolates magnitudes and envelopes *in that shared frame*; one resynthesis comes out. This is both cheaper than eight parallel FFT chains and the difference between morphing and crossfading. Transients are split off first and added back after.

**The effects** — `LiveFx.h`'s effects stacked on the way out, their parameters interpolated by position where the topology matches and power-complementary crossfaded where it does not.

That gives the thing you described: a percussive sequence warbled into a bassline, where what comes out has frequencies of both rather than being the two of them playing at once. The joystick is recordable, so a move is a modulation like any other — which is also how the randomiser gets to cycle it rhythmically.

**What I need from you here:** send the plugins you want me to work from. I will measure what they do to sound — what comes out for a known input, as we did with Sting — and build from the principles above. I will not take apart their code.

The order of work

Longest first, as you asked, with one change I would argue for: **the shared groundwork goes first**, because it is what makes everything after it cheap. It is two days, not two weeks, and it is the difference between building the Orb once and building it twice.

#

What

Lands as

0

**Groundwork**: the folders, the newer `.NJQ` in NeoBeat, categories in the manifest, the two drawing files merged into one `Ui.h`

A Batchcut build with the folder fix, so you feel it immediately

1

**MidiJaQ**: the Orb, the stereo room, multis, sequences across the keys, the randomiser, and finishing what is on the branch

A MidiJaQ build

2

**NeoBeat**: the stereo room, the Orb, the browser, Bounce Delay smoothed, the randomiser

A NeoBeat build

3

**Batchcut**: clear all, `.NJQi`, the category picker

A Batchcut build

4

**Skins**, once your examples are in, across all three

One build each

5

**JaqRack**, with its own plan first

—

Each numbered step ends the way the last few have: committed to its own branch, built on Windows with every test and pluginval run, and the download pushed to you. You play it and tell me what is wrong before the next one starts.

One caveat on step 1 against step 2: the Orb is the largest single thing in your note, and it is the one I would most like to build *second* rather than first — NeoBeat's is simpler, and building the simple one first usually teaches you the shape of the hard one. If you would rather have the hard one working first, say so and I will take it as it stands.

After step 5 comes the **Max for Live device** that sends MIDI, now that the plugins themselves no longer do. The question to settle then is whether it plays a sequence exported from MidiJaQ — cheap, and it works the week it is written — or regenerates lines itself, which means the generators written a second time in JavaScript.

What I need from you

**Permission, one by one.** You asked to be asked before any existing feature changes. These are the ones in this list that do:

**`.NJQi`** — agreed already.

**Bounce Delay's smoothing**, which changes how it sounds while a knob moves (that is the point, but it is a change).

**The preset browser**, which replaces NeoBeat's pattern menu and the \\"MINE |\\" split.

**Merging the two drawing files**, which touches every window in all three plugins without changing how any of them looks.

**Two things to send.** The skin examples and the stereo-field picture you mentioned. Neither blocks the early steps, so work can start before they arrive.

**One thing to tell me.** What is unusable about MidiJaQ as it stands when you play it. The branch has far more in it than I expected, and your ear will aim this better than my reading of the code.

**One decision.** Whether to take the Orb first, as the order above has it, or to build NeoBeat's simpler one first and MidiJaQ's second.

**And a question worth settling now:** categories. I have texture, clubby and experimental from your note. Send me the list you actually want, or say that you would rather type your own as you go and have the browser collect them.