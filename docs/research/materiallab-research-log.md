# MaterialLab research log

MaterialLab research log: one physical-modeling synth

2026-11-05 · Hope

The goal and principles

One new private plugin replaces Liquid Swing (NeoBeat) and NeoSpectral (Metalab), built from scratch on the earlier work. Its sound comes from physical models of metal, liquids, latex, rubber and everyday city sounds, pushed into physics that cannot exist.

**Roles.** You provide the ideas. Claude is assistant and coder: it brings research that strengthens an idea or suggests a better way to implement it, and asks before building anything that takes long.

**Core principle.** Sounds that defy physics but are modelled on physical things. Imagine a universe where SOPHIE and Aphex Twin sounds are the default presets of a physical-modelling synth. The research is in bending the models to add impossible depth, space, body and buoyancy.

**References you named**

Yamaha VL1 (you own an emulation) and Chromaphone 3: the most advanced physical-modelling references.

SOPHIE and Aphex Twin: the timbres to reach.

MetaSynth: its engine and characteristic unnatural warble (also in the Æon Flux soundtrack), plus drawn ticks, dots and dashes that drive several destinations at once. Photo-to-sound is not the interest.

Amaranth Cycle: the interface should look very similar.

City sounds (dogs, horns, metal clanks, drills, church bells, trains, planes, tyre screech) turned into playable synths that behave like the events. Belief: human existence is musical.

**Example gesture.** A downward dash on a bouncing basketball lowers the pitch, adds reverb and widens the stereo field. A rising pitch feels like it is about to hit you.

Early research questions

The chat began with MetaSynth and ended at building our own engine, because sounds are not copyrighted but code is.

**A Windows MetaSynth.** Feasible for personal use as a re-implementation of its ideas, not its code. You only needed to render audio to sample, so running it in the cloud was acceptable.

**Virtual ANS** (a software version of the ANS photoelectronic synthesiser) and other open-source spectral tools were reviewed as the closest open options to build on.

**Modalys** (IRCAM's modal synthesis) fits the MetaSynth idea, but you don't use Max/MSP and don't have it installed. You asked about a VST interface for it, then chose to start with physical-model tuning instead.

**RAVE and CataRT** were checked for open-source status.

**Open-source physics-based sound projects** were surveyed and their licences checked, so you could listen to and tune by their approaches without copying their code.

**Your stance:** describing sounds to an AI in words is hard, because it cannot hear. What it needs are algorithms. You point to what to study and reverse-engineer, and the work goes into your private research model.

An earlier interactive app built around jaxdiffmodal was stopped as unnecessary. Since then, builds that take long are asked about first.

The learner

The learner tunes a physical model until it matches one of your recordings, then saves the result as a material file. The synth loads that file to keep producing new sounds in the same family as your recordings, not copies of them. The families are fine-tuned on your own recordings, not on generic objects.

**How it learns.** It measures your recording and the model the same way: pitch paths, how each partial rings and decays, slips and pulses, stereo spread, spectral balance. Then it adjusts the model until the two agree. Smooth physics is fitted by gradient descent in JAX. Behaviour that is chaotic or event-based (friction, bubbles) is fitted by search.

**The training set.** It lives in `D:\\\\01TxtSamplerSounds\\\\Learner\\\\Training`: single-hit impacts, bounces, friction, plucks and liquid clips cut from your sources, each with measured targets.

**What the research showed.** Every family fits one structure: an exciter (strike, slide, pour, stretch-and-snap), driving a body (modes learned from a recording or calculated from physics), heard in a space (two listening points and a room). The learner's rule for separating them: whatever rings the same way after every event belongs to the object; whatever changes from event to event belongs to the exciter.

The learner turns recordings into material files offline; in the plugin, an exciter drives the body, the body pushes back, and the space places the result.

Impacts: from flat to living

Engine v3 matches seven struck materials, and every one is closer to your recording than engine v2 was.

**Engine v1** matched pitch and decay roughly but sounded flat. Your verdict: the sounds didn't evolve, barely followed the pitch bends, had none of the harmonic motion in the mids and highs, and lacked the 3D dimension of a real object.

**The clank analysis** tracked about 550 partials per hit and found what v1 missed:

partials that beat by 2–8 dB

an upward energy cascade, with partials peaking 70–160 ms after the strike

one resonance sweeping from 3 kHz down to 760 Hz

metal-like damping

a shared slow pulse in the low body

damping that switches on when the object is touched

**Engine v2, the living modal body,** added all of that. Your verdict: super accurate; the clank is synthetic, but in a synth it is exactly the noise and timbre you want in many variations, so it is the base to build on.

**Engine v3** closed three gaps. The learner now sees sweeping partials itself, learns when a hand or the floor stops the ring, and adds a dense body layer (the hiss between the clear partials). The basketball was added.

Material

v2 → v3 (lower = closer)

Measured, yours / model

Clank (synthetic reference)

1.61 → 1.48

ring identical (0.26 s); a bit steady (wobble 5.0 / 3.6 dB)

Tube clank

2.81 → 2.64

the light touch 7 ms before the strike is reproduced; ring 0.36 / 0.33 s

Hollow pan

4.35 → 1.88

biggest gain, from the body layer; ring 0.26 / 0.26 s

Tin can

3.07 → 1.38

ring 0.095 / 0.10 s

PVC pipe

2.18 → 0.67

sharp strike now; rings too short (0.51 / 0.38 s)

Bucket

1.94 → 0.85

ring 0.13 / 0.16 s

Basketball (new)

— → 0.87

close overall, but duller and shorter than yours

Each material has variations that change one law at a time: higher, bigger body, longer ring, more warble, frozen, bloom, soft striker, buoyant rise, elastic drop, struck elsewhere.

Space and panning

Every object is now heard from two listening points in a room, calibrated on your stereo clank. You said the stereo experiments sound great.

**How it works.** Low partials reach both listening points almost identically; high partials differ more, as they do around a real object. A room adds early reflections and a tail. Distance follows Chowning's cues: the sound gets quieter and duller, the room's share grows, and a moving object bends in pitch (Doppler).

**Calibration.** Against your stereo clank (left/right correlation, low / mid / high bands): target 0.96 / 0.90 / 0.62, achieved 0.95 / 0.79 / 0.66.

**Eight places per material:** mono and dry, calibrated, close and dry, far (12 m), small room, large hall, narrow, and hyper-wide. Hyper-wide is impossible: wider than any real object, and it collapses when summed to mono.

**Panning decision (A + C).** The timeline canvas runs left to right in time, so a horizontal stroke cannot also pan. You chose:

**A. A stage view.** A top-down view where you draw where the object is: left–right across, near–far up the page. Pan, distance, Doppler and room all follow from that one path (Chowning 1971).

**C. Spectral colour pan.** Colour in a frequency view places each partial on its own, so highs can fly left while lows stay centred. That is impossible for a real object, and the engine already places each mode separately.

Both should look striking in the plugin. Pan should move the object rather than turn one side down: a small time difference between the ears (up to about 0.7 ms) and a shadow that dulls the far side (Brown & Duda 1998). A speakers/headphones switch is needed, because time differences carry less on speakers. For reference, MetaSynth's Image Synth pans by colour: red is left, green is right, yellow is centre.

Gestures and the basketball

One drawn stroke moves pitch, distance, width and room together. Each destination has a value at the bottom and at the top of the canvas, and the stroke slides between them.

**Bounce physics.** Each bounce keeps about 79 % of the last one's speed, measured from your wooden-ball recording, so flights get shorter by that factor. Faster impacts shorten the contact, which brings out the higher modes.

**The demos:**

Natural bounces.

Bounce away (your downward dash): pitch drops 7 semitones, the ball travels from 1 m to 18 m with a slight Doppler fall, highs fade, the room grows, the image widens.

About to impact (upward dash): pitch rises 12 semitones as it approaches and the room closes in.

Impossible: it recedes while its pitch rises.

Impossible: it gains energy on every bounce.

The same downward dash applied to all six other materials.

The pitch was measured on the renders and follows each stroke as drawn.

Rubber and friction

The rubber model went from your worst to your most realistic. You picked the 3rd and 4th passes of the rubber_tabs_01 A-B file (the mono models) as the closest to the original.

**What was wrong (friction v1).** You heard it as distorted, lo-fi, missing the body, brightness and mids, and without the 3D feel. Measured against your recording:

Each slip in yours stands 11 dB above the gaps; the old model's gaps were louder than its slips, a smear of noise.

Yours rings 5–7 ms after each slip; the old model died in 2 ms.

Above 8 kHz the old model was 13 dB too loud. Its contact switched from stick to slip in one sample, which folds high frequencies back as grit, and it added a layer of white noise.

**Friction v2.**

A smooth contact (the LuGre friction law): the surface flexes slightly before it lets go. It is computed at 4× the sample rate, then filtered down.

The pulled tab is a mass on a spring.

A ringing body learned from about 100 slips: 70, 141, 1242, 1383, 2250, 3164, 4336 and 6562 Hz for rubber_tabs_01, with weaker modes getting denser towards the highs, as in a plate.

Two-way coupling: the body pushes back on the contact and can lock into a squeal.

Three contact points along the peeling edge. The rest of the rubber rubs between slips as tiny contacts that pass through the body.

Two listening points and a room.

rubber_tabs_01

Your recording

Friction v1

Friction v2

Slips stand out from the gaps

10.8 dB

−1.6 dB

9.3 dB

Ring after a slip, 0.5–2 kHz

6 ms

2 ms

4 ms

Spectrum after a slip, distance from yours

—

9.1 dB

0.6 dB

Level above 8 kHz after a slip

−33.4 dB

−20.0 dB

−34.1 dB

Slips per second

39.8

48.1

42.2

**The stretch and the snap.** You pointed to the curvature in the spectrogram: the pitch bending as the rubber is stretched, which is also what lets it snap back. Each curved line is one of the stretched strip's own modes, and its pitch follows how far the tab has been pulled, not the clock:

Here d is the fraction of the pull done. As the tab is drawn out, its free part gets about 1.5–2.8× longer, so the pitch falls gently. The tension comes from friction on the part still trapped under the battery; as less of it remains trapped, the tension sags and the fall steepens. The strongest line dives into the snap with its tension at about an eighth. At the release the stretch slaps the object, the strip goes silent, and the object rings freely for about 60 ms until it is caught.

The learner traced 6 lines in rubber_tabs_01 and 8 in rubber_tabs_05, each fitted to within 18–55 cents.

Lines stand out 3.5–7.6 dB in the model against 3.5–7.3 dB in yours. In rubber_tabs_05 all eight are within 0.5 dB.

The snap: 26.1 dB above the slips (yours 25.5); 30 / 60 ms later −11.7 / −28.3 dB (yours −11.4 / −27.8).

**Variations:** slower and faster pull (the curves bend later or sooner, and the snap moves with them), tight rubber (no dive), slack rubber (deeper bends), one contact, a light body that squeals, and two impossible ones: rubber that tightens as it is pulled, so its pitch climbs, and rubber on a body that rings like metal.

Stick-slip is chaotic: a tiny change in pressure gives a different pattern of slips, so every render is a new pull in the same family.

Liquid: water poured into a glass

The click is gone, and the glass is calculated from physics rather than copied. You said these sound great.

**Liquid v1.** Single bubbles rising in pitch (van den Doel's model) and a bubble crowd that matched your pour's density (14 a second) and pitch spread. Your diagnosis: a distinct click on every transient that does not exist in reality. The water is hitting a glass, a dull resonant impact, and the model was treating it as plastic.

**What is in the recording:**

**The glass:** narrow lines that keep their pitch and ring for a quarter to half a second.

**Soft impacts:** the water's push is spread over about a millisecond.

**One big bubble per impact** (Prosperetti & Oguz 1993), rising in pitch: the glugs.

**A cloud of tiny bubbles per impact:** under a millimetre, ringing at 3–16 kHz and dying within milliseconds. This is the sparkle the old noise burst was faking. A steady fizz comes from the stream between impacts.

**The air above the water,** resonating near 1066 Hz.

Your recording

Liquid v1

Liquid v2

Brightness of an impact's first 3 ms

1110 Hz

2673 Hz (the click)

652 Hz

Top octave, 12–20 kHz

−52.7 dB

−35.9 dB

−55.0 dB

Overall spectrum, average difference

—

4.6 dB

3.8 dB

Bubble pitch, low / middle / high

197 / 440 / 1004 Hz

261 / 465 / 1103 Hz

215 / 470 / 888 Hz

Glass lines above their surroundings

4.5–8.4 dB

0.2–3.4 dB

4.1–9.5 dB

**The calculated glass.** You asked to calculate how the glass should sound instead of recording it. A glass wall rings in shapes with 2, 3, 4… waves around the rim, at fixed ratios (1 : 2.83 : 5.42 : 8.77 : 12.9 : 17.7), and your recording's lines fall on that series, each as a close pair. Assuming a tumbler 10 cm tall and 7.6 cm across, two lines give the rest. The 1066 Hz air line puts the water at about 42 % full, and the 843 Hz ring tone puts the wall at about 1.9 mm. From those alone it predicts:

Mode

Calculated

In your recording

n=3

2389 / 2452 Hz

2455 / 2520 Hz

n=4

4632 / 4678 Hz

4705 / 4740 Hz

n=7

15.2 kHz

14.6 kHz

Played through your pour, the calculated glass matches as closely as the learned one (3.8 dB). How fast it dies depends on how it is held, so that part is calibrated once to your recording.

**Filling.** Water weighs the wall down (Jundt et al. 2006): the pitch holds, then drops as the water reaches the upper wall (n=2: about 850 → 560 Hz). Meanwhile the air above the water rises (about 700 Hz → 3 kHz).

**Other glasses:** a lead-crystal wine glass, a thick tumbler, a shot glass (about 4 kHz), a big vase, and two impossible ones: a liquid as heavy as mercury, which makes the pitch collapse, and a glass the size of a bell.

**Pour variations:** fuller, emptier, a heavy pour, a gentle trickle, a plastic cup, no tiny bubbles, bubbles that sink, and a glass of bell metal. There is also a new 6-second pour drawn from what was learned, with nothing copied.

Your decisions and feedback

These steered the work, in roughly the order you gave them.

Topic

What you decided or said

The plugins

Dissolve Liquid Swing and NeoSpectral and build one plugin that does everything, still building on the earlier work

Process

Establish concrete ideas and references before organizing the build; ask before long builds

The learner

Fine-tune the model families on your own recordings, so the synth keeps making new sounds in the same family

Reference palette

Restart with single-hit sounds (the Training set), keeping the clank progress; drop A/B reference 2 and keep 3

Engine v1

Too flat: no evolution, barely any pitch bend, no harmonic motion in the mids and highs, no 3D

Engine v2 clank

Super accurate; the timbre and noise you want in many variations; the base for the engine

Engine v3 and the rest

All sounded great; happy with the direction

Stereo

The experiments sound great

Panning

A + C: a stage view plus spectral colour pan, both visually striking in the plugin

Rubber, first pass

Most off of all: distorted, lo-fi, missing body, brightness, mids and 3D

Rubber, second pass

Much closer, but the pitch curvature from the stretch (and the snap back) was missing

Rubber, final

The 3rd and 4th passes of rubber_tabs_01 (mono) are the most realistic

Liquid, first pass

The click on the transients doesn't exist; it should be water hitting a glass, a dull resonant impact

The glass

Not recorded; calculate how it should sound

Liquid, final

These sound great

Where everything lives

All results are on your computer in `D:\\\\01TxtSamplerSounds\\\\Learner\\\\MaterialLab`; each folder has a README that says what to play first.

Folder

What's in it

`MaterialLab.zip`

All the code, plus every learned material and the earlier results (v1 fits, clank analysis)

`README.md`

Index of the folder and the modules

`Engine v2`

The living modal body on six impacts, with variations

`Engine v3\\\\Impacts`

Seven materials, original vs model, variations, `materials\\\\*.json`

`Engine v3\\\\Stereo`

Each material in eight places

`Engine v3\\\\Gestures`

The basketball demos and all materials thrown away

`Engine v3\\\\Friction`

Friction v1 (creak, balloons, rubber tabs)

`Engine v3\\\\Friction v2 - rubber`

The smooth contact and ringing body; `Stretch and snap` holds the final rubber model

`Engine v3\\\\Liquid`

Liquid v1 (bubbles)

`Engine v3\\\\Liquid v2 - pour`

The pour into a glass; `Calculated glass` holds your glass, filling and other glasses

**Code modules** (inside `MaterialLab.zip`, in `materiallab`):

Module

What it does

`engine2.py`, `fit2.py`

The living modal body and its learner

`partials.py`, `analysis.py`, `measure.py`

Measuring recordings: partials, sweeps, contact stops, the Training set's own measures

`spatial.py`

Two listening points, room, distance

`gesture.py`

Strokes, bounces, Doppler

`friction.py`, `friction2.py`, `slips.py`, `stretch.py`

Friction v1, friction v2, slip analysis, the stretched strip and snap

`liquid.py`, `pour.py`, `glass_physics.py`

Bubbles, the pour model, the calculated glass

The `tools` folder holds the scripts that ran each fit and render. The learner runs offline (Python and JAX); the plugin only needs to load the material JSON files.

Open gaps and next steps

The research phase is nearly done. The next decision is a build plan for the plugin, or one more research item first.

**Known gaps**

Impacts: the basketball is duller and shorter than yours; the PVC pipe rings about a quarter too short; the clank is slightly too steady.

Rubber: the dive before the snap stands out 1.5 dB less than yours; the gaps between slips are slightly busy; below 1 kHz is a little full.

Liquid: impacts a little duller than yours (0.65 vs 1.1 kHz in the first 3 ms); 1–2.5 kHz about 8 dB quiet. Fingertip taps on the same glass, empty and partly full, would replace the assumed height and wall.

Stereo is calibrated on one recording; more stereo recordings would make it learned per material.

Everything runs offline in Python; nothing is real-time yet.

**Not yet touched:** the balloons and the creak with friction v2; the rubber band plucks and the catapult; city sounds (drills, trains, bells, horns; the Training set already has drill clips); the MetaSynth warble and drawn ticks, dots and dashes that trigger and sequence events.

**Recommended next: a build plan, before any code**

One engine for every family: exciter × body × space, the controls each family exposes, and what is learned versus calculated.

The learner as a separate offline tool that exports material files; the plugin only loads them.

Real-time cost: modal bodies are cheap; friction at 4× oversampling is the heaviest part, but should fit a few voices on one CPU core.

The interface: an Amaranth Cycle-style layout, the stage view, the spectral colour view, and how drawn gestures map to controls.

Build order: impacts + space + gestures as the first playable instrument, then friction and liquid.

**Alternative:** one more research item first: the balloons, the rubber band plucks, or the first city sound (the drill).

Sources

Jundt et al. 2006, Vibrational modes of partly filled wine glasses (JASA)

Wineglass resonance with different liquids (arXiv 1801.07514)

Sound On Sound: MetaSynth review (Image Synth colour panning)

MetaSynth manual

Cited from the literature, not opened here: French 1983 (wineglass acoustics); Avanzini, Serafin & Rocchesso 2005 (friction sounds); Chowning 1971 (moving sound sources); Brown & Duda 1998 (binaural model); van den Doel 2005 (bubbles); Prosperetti & Oguz 1993 (drop impacts); Canudas de Wit et al. 1995 (LuGre friction).