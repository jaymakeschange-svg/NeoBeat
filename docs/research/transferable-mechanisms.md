Transferable mechanisms for the Neosystem

2026-11-26 · Hope

Reverse engineering notes across fourteen plugins, mapped onto the plugin in the family that each mechanism belongs in. No implementation — analysis and routing only.

Method, and what each claim rests on

Evidence quality varies sharply across these fourteen targets, so every claim below should be weighted by how it was obtained. Three were taken apart as binaries; four turned out to contain no code at all.

Target

How it was examined

What that licenses

VirSyn TERA 3

Binary. PE32+ dissected: sections, RTTI class names, full string table, statically linked library versions

Architecture stated as fact; DSP internals inferred

Chassis, chassisFX

Binary teardown, 23 Nov

Module set and mechanisms stated as fact

Synplant

Binary teardown, 23 Nov

Genome and search method stated as fact

Petri

Binary plus running app, 23 Nov

Map and "grow" separation stated as fact

SonicLab PolyNodes

Teardown, 23 Nov

Resynthesis method stated as fact

ConcreteFX Chimecho

Complete published parameter list

Algorithm fully specified; no code seen or needed

Sound Particles inDelay

Vendor documentation and press, two release cycles

Feature set and movement modes reliable; DSP inferred

Energy Panner, Brightness Panner

Vendor manuals plus VST3 manifests

Detector and traversal behaviour reliable

Beat Panner

Press coverage, June–July 2026

Sequencer concept reliable; detail thin

Sound Particles Density

Vendor documentation plus two reviews

Mode structure and granular basis reliable

GRM Tools Atelier

Vendor material and press

Architecture reliable; module internals unknown

jackboyman Chimera

VST3 manifest plus vendor one-liner

Category certain; mechanism only named, not explained

Majken's Chimera

Vendor description only — binary was a stub

Synthesis method named, nothing verified

black, Ironhead-Bonk

Nothing. Both uploads were stubs

No claims made

One structural finding spans the whole batch: every VST3 examined here is built on JUCE, identifiable from the class ID prefixes ABCDEF01 plus 9182FAEB, 1234ABCD or C0DEF00D, with the plugin name encoded as ASCII in the suffix. That includes Chassis, GRM Atelier and all the Sound Particles plugins. Same framework as the Neosystem, which means their binaries are far more tractable than arbitrary C++ should more need taking apart.

The transfer map

One mechanism per plugin, and the plugin in the family it belongs in. Where two studied plugins converge on the same mechanism, that is noted — convergence is the strongest signal that a mechanism is worth building.

Studied

The one mechanism worth taking

Belongs in

inDelay

16 taps, each a positioned node with its own movement mode; air absorption per tap; swing on tap times

Bounce Delay

Energy, Brightness, Beat Panner

One engine, swappable trigger source; position accumulates rather than returns

Bounce Delay, then Stereoroom

Chimecho

16-band split with per-band delay; two knobs drive sixteen by interpolation; cross-band feedback

New room, shared

VirSyn TERA

FFT analysis of any sample into editable harmonic or inharmonic partials; morph between two spectra

MaterialLab, in Batchcut

Density

Granular voice multiplication with independent pitch, timing and position offset per voice

NeoBeat, inside IDM Jack

GRM Tools Atelier

Processors and Modulators as two distinct module classes; modulation assignable to any parameter

JaqRack

Chassis — frappe

The object is a tiling and the picture is the object; two listening points rather than a pan

MaterialLab

Chassis — grille

A grid of patterns with a continuous reading point; seeded randomness with a repeat probability

NeoBeat, inside IDM Jack

Chassis — coupe

Eight playback heads over one buffer

Batchcut CUT room

Chassis — fracas

A router from any control source to any internal setting

JaqRack

Chassis — tidy

Theming as a module, not a stylesheet

All three, theme layer

Synplant

Genopatch is gradient descent over a parameter vector, not a neural network

MaterialLab

Petri

The map is a learned 2D embedding; "grow" is parameter search and is not neural

Deferred

PolyNodes

Analysis-driven resynthesis with no network at all

MaterialLab

Chimera (jackboyman)

Convolution merge — source A played through the resonance of source B

MaterialLab

Chimera (Majken)

Noise excitation through pitch-tracked resonant bandpasses

MaterialLab

SOPHIE research

Interaural delay of a few ms under constant LFO; cross-channel feedback

Stereoroom

Four mechanisms are arrived at independently by more than one of these, which is the clearest guidance in the document: per-element spatial movement with named modes (inDelay, the panners, Chassis frappe, the existing Travel enum), analysis then resynthesis (TERA, PolyNodes, Majken's Chimera, MaterialLab as it stands), randomisation across three axes at once (Density, Chassis grille, the IDM Jack spec), and two-knob control of many elements (Chimecho's Start/Finish, inDelay's Filter and Movement Link, inDelay's Gauss curve editor).

Sound Particles panners — and why they refine Bounce Delay

Your instinct here is right, and the vendor has since confirmed it architecturally. 2026-10-08 Energy Panner 2 and Brightness Panner 2 now run on the same engine that powers Beat Panner — three products, one engine, three front ends. That is not a marketing detail; it tells you the correct shape for Bounce Delay.

The engine has three separable parts.

A trigger source. This is the only thing that differs between the three products. Energy Panner watches amplitude, with compressor-shaped threshold, ratio, attack and release controls. Brightness Panner watches spectral content — 2026-10-08 it turns brightness, detected pitch or incoming MIDI notes directly into movement with no manual automation. Beat Panner replaces the detector entirely with a step sequencer, 2026-06-03 a step-based panning sequencer producing rhythmic movement locked to tempo.

A trajectory, defined by two endpoints rather than a pan position. The signal does not sit somewhere; it travels between A and B.

A traversal rule, and this is the part worth copying exactly. In Pan mode the position returns toward the start when the trigger falls away. In Sliding mode the position accumulates: a loud or sustained passage advances it along the trajectory and it stays there, waiting, until the next trigger advances it further. The sound ratchets through space over the course of a phrase instead of pumping back and forth.

Beat Panner adds two shaping controls on top — 2026-06-03 a choice between smooth transitions and abrupt rhythmic changes, plus float and divergence for subtle variation across the sequence.

What this means for Bounce Delay

Bounce Delay currently needs rebuilding anyway, so this is the cheapest moment to restructure it. The proposal is to stop treating it as a delay that happens to pan, and build it as a tap engine whose positions are driven by a selectable source:

Taps provide the time structure.

Position per tap comes from one of: a fixed trajectory, an amplitude detector, a brightness detector, or a step sequencer on the Neosystem's own swung grid.

Traversal is Pan or Sliding, switchable.

Sliding mode is the one that earns its place in a new jack swing context. With taps on a swung sixteenth grid and Sliding traversal, a repeated snare does not bounce left-right-left; it walks across the image over four bars and holds position between hits. That is a motion you cannot easily automate by hand, which is the whole argument the vendor makes for the product.

The detector work is also reusable rather than single-purpose: an amplitude and brightness detector pair, once written, is what IDM Jack needs to decide which parameter to seize, and what the Stereoroom needs to place a voice by its own content rather than by a dial.

Beat Delay — it does not exist

There is no Sound Particles plugin called Beat Delay. Their delay is inDelay; their beat-synced product is Beat Panner, released 3 June 2026 and updated to 1.1 on 31 July. Their current catalogue is Energy Panner 2, Brightness Panner 2, Beat Panner, Density, inDelay, SkyDust 3D and MiniDust. Nothing named Beat Delay appears in it, and nothing matching that description has been announced.

That said, the thing you were reaching for is real — it is just inside inDelay rather than a separate product. inDelay 1.5, May 2025, added three features that put it squarely in Bounce Delay's territory:

Swing and sync time fine-tuning, described by the vendor as giving a human-touch feeling to delays. A commercial delay shipping a swing control is worth knowing about.

A linear/Gauss curve editor, for distributing up to 100 delays across a span using a chosen statistical shape rather than placing each one.

Filter and movement link, letting you either apply a change to every tap at once or customise each tap individually.

The swing control is the headline for your purposes. A delay whose taps land on a swung grid rather than a straight division is close to the core premise of the whole research project, and this is evidence that the idea has commercial validation rather than being a private enthusiasm.

The curve editor is the more immediately useful of the three, though. It is the same insight as Chimecho's Start/Finish interpolation and inDelay's own movement link: when an engine has sixteen or a hundred elements, the interface problem is larger than the DSP problem, and the answer is to expose a distribution rather than a list. Bounce Delay should ship with that from the start rather than growing per-tap controls and then needing a way to manage them.

Beat Panner's 1.1 addition is worth noting as a workflow idea even though the Neosystem has no object-based output: 2026-07-31 it lets you copy the movement the plugin generates and paste it as automation onto object tracks. The transferable form is that a generative movement engine should be able to hand back what it produced, so you can keep a take you liked. For IDM Jack, which is explicitly chaotic and explicitly intended to be recorded in session, the equivalent is a way to freeze and keep whatever a chaotic pass just did.

inDelay — the closest existing thing to Bounce Delay

This is the most directly relevant commercial product in the batch, and it independently validates several decisions already made in the Neosystem.

The vendor's own summary gives the structure plainly: up to 16 taps and up to 100 delays at once with time, gain and feedback controls; a particle system for generating and placing those delays in space; movement modes for the delays; air simulation adding absorption and distance to each delay particle; and a soundscape mode with per-tap input selection.

Four parts worth separating out.

Taps are positioned objects, not pan values. Each tap exists at a place, and the source of each tap can be chosen independently. This is the same decision as the Stereoroom's Place, arrived at separately.

Movement modes are named and enumerated — static, rotate, random, float and up-down. Set against the existing Travel{Still, Sway, Swing, Circle, Drift}, the correspondence is close enough to be striking: static maps to Still, rotate to Circle, random and float to Drift. Two observations follow. The Travel enum is sound, and does not need revisiting. And the one mode inDelay has that the Neosystem lacks is up-down, which is elevation — unavailable in stereo, so it is a genuine gap only if JaqRack ever outputs more than two channels. Sway and Swing have no inDelay counterpart, which is the Neosystem's own contribution and should stay.

Air absorption per tap. High frequencies are attenuated as a function of modelled distance, so a far tap is dull and a near tap is bright. This is cheap to implement — a one-pole lowpass per tap whose coefficient is derived from the tap's distance — and it does something no amount of level automation achieves: it makes distance legible. The Stereoroom's Place currently has no distance dimension at all. Adding one, with air absorption as its audible consequence, is a small change with a large perceptual return, and it belongs in the shared tree rather than in Bounce Delay alone.

The particle system generates taps rather than requiring you to place them: a density figure and a distribution produce 100 delays from 16 declared taps. This is the same Gauss-curve thinking as above and the same problem Chimecho solves by interpolation.

What to take

For Bounce Delay: tap count up to 16, each tap a Stereoroom node with its own Travel, per-tap air absorption driven by distance, and generation of taps from a distribution rather than placement one by one.

For the shared Stereoroom: distance as a real dimension of Place, with air absorption as the mechanism that makes it audible. Every plugin in the family benefits, since Place is inherited everywhere.

Density — the missing half of IDM Jack

Density turns one source into many voices. The vendor describes it as creating ensemble-like sounds using particle systems, granular synthesis and stereo or 3D panning — a voice becomes a choir, a violin becomes a string section.

The mechanism, from Best Service's description, is that granular synthesis plus the particles engine applies movement, pitch and timing differences to create multiple voices from the source audio. TapeOp's review sets out the three modes — Basic, Detune and Pitch, with increasingly more controls — and notes the grain timescale as 1 to 100 ms. Basic mode offers a small or large ensemble, a detune amount and a button to add voices an octave below. Sound On Sound puts the ceiling at up to 100 voices with multi-channel support.

The detail that matters: the three randomisation axes are movement, pitch and timing, applied per voice, independently.

That is the IDM Jack specification, written by someone else. The brief you gave was that the mode should rapidly and randomly repeat drum parts while the individual parameters of each sound get pitched up, down, driven and hollowed. Density's three axes are two of yours plus one you had not named — timing offset per voice, in the 1 to 100 ms range, which is exactly the window that reads as flam, ghost note or smear rather than as a separate hit.

So the transfer is not "add a Density room". It is: IDM Jack's randomiser should operate on three axes rather than two, and one voice becoming several at micro-timing offsets is a fourth kind of chaos alongside repeat, parameter seizure and effect seizure. For a kit sound, a single hit becoming four grains spread over 40 ms with independent pitch offsets is the difference between a stutter and the smeared, granular quality in the Autechre material you described.

Two further details worth lifting. Density separates control of the input voice from the generated voices, which for IDM Jack means the original hit should be independently controllable from its chaos. And it draws its layers as a circle in 3D space — the same decision as Chassis frappe and the same decision the Stereoroom display already makes, which is a third independent arrival at the same answer.

This belongs in NeoBeat, inside IDM Jack, not as a room of its own. A standalone ensemble generator is a different product; the granular multiplication is only interesting here as one of the things chaos can do to a drum hit.

Chimecho — fully specified, and cheap to build

Chimecho is a 16-band spectral delay by ConcreteFX. Its entire algorithm is recoverable from the published control list, so no decompilation is needed and no proprietary code need be touched — which also keeps it clean against the no-reproducing-proprietary-code rule.

The parameter list is: Start/Finish Freq for band frequencies, Q for band bandwidth, Start/Finish Len for delay lengths, Start/Finish feedback, Start/Finish volume, Start/Finish LFO Speed and Width for a delay LFO, Pan Width and Speed, Side Delay and Side Feedback, and Dry.

The control scheme is the real idea

Sixteen bands with six parameters each would be ninety-six controls. Instead, as the documentation puts it, start and finish sliders select the first and last value and everything between is interpolated. Two knobs drive sixteen delay lines. This is the same answer inDelay reaches with its Gauss curve editor and its filter/movement link, and it is the answer to a problem the Neosystem already has in the Stereoroom and will have much worse in JaqRack.

Cross-band feedback is the character

The one control that makes this more than a filterbank feeding delay lines: Side Delay is a band offset. At +1, band 1 takes feedback from band 2, band 2 from band 3, and so on; Side Feedback sets the amount. Energy therefore walks up or down the spectrum with each repeat. A sound does not echo — it migrates in frequency, each repetition arriving brighter or darker than the last. Soundshock's feature summary lists this as cross-band echo alongside the interpolated controls.

Band split: filters, not FFT

The presence of a Q control is the evidence. Q is meaningless on FFT bins; it only makes sense for resonant bandpass filters. That also means the bands overlap and leak, and the leakage is part of the sound — an FFT reimplementation would be cleaner, more surgical, latent, and would not sound like this.

What it becomes in the Neosystem

A shared room, available in Batchcut and NeoBeat, built as: 16 resonant bandpasses with interpolated centre frequencies and shared Q; a delay line per band with interpolated time, feedback and level; a delay-time LFO per band with interpolated rate and depth; cross-band feedback with a signed band offset; and — the Neosystem's addition — each band as a Stereoroom node with its own Travel rather than Chimecho's single pan LFO.

That last substitution is the ecosystem-consistency point. Chimecho predates the kind of spatial engine already sitting in the shared tree, so it pans each band with an LFO. There is no reason to reimplement a weaker version of something already built and inherited everywhere. Sixteen bands each travelling independently through the Stereoroom is strictly more capable, costs nothing extra in design, and makes the room feel like part of the family rather than a port.

It also happens to be the cheapest significant thing in this document to build: a biquad bank, sixteen delay lines, an interpolator and a cross-feed matrix. No analysis, no FFT, no new data format.

VirSyn TERA — the highest-value finding in the batch

TERA 3 was the only genuine binary among today's four uploads, and what is inside it addresses the single largest structural gap in MaterialLab.

What the binary shows

Six oscillators with per-oscillator sample playback, FM with per-operator index and selectable input, hard sync to oscillator 1, and subharmonic generation. Four ADSR envelopes with delay stages. Three filters: 1 and 2 with cutoff, resonance, shift and input overdrive, selectable between 12 dB LP/HP/Band/Notch and 24 dB LP I, LP II and HP; filter 3 has frequency, level and resonance but no cutoff, which reads as a resonator rather than a filter. A 12-formant filter with selectable input. Effects: reverb, chorus, flanger, phaser, ensemble, overdrive, and mono/stereo/cross delay. Morph A to B with a selectable morph source.

Statically linked: FFTW 2.1.5, and libsndfile with FLAC and Vorbis. RTTI class names survive, including CUIPartialEditor, CUIWaveEditor, CEngine, CSynth, CTerrainFilter and CMultiSample. The build path leaked too: C:\\Users\\Harry\\Documents\\Development\\TERA\\win\\x64\\Release\\TERA.pdb.

The part that matters

TERA performs FFT analysis of an arbitrary sample into editable partials, and resynthesises from them. The evidence is unambiguous: FFTW linked in, an "Analyzed spectrum files (*.vrd)" file type, menu entries for opening an analysed spectrum and for copy/paste of spectra, a CUIPartialEditor class, a Partial raster control, and a per-partial selection state. Two spectrum oscillators exist, A and B, alongside the Morph A to B control — so two analysed spectra can be crossfaded.

And critically, the mode is switchable between Harmonic and Inharmonic.

Why this is the one to take

MaterialLab as it stands is a resonator bank that loads the learner's JSON material files. The engine is sound — the test suite confirms the damping law, the ring times, the strike accumulation and the audio-through path. The limitation is the front end: materials can only come from files the learner produced. There is no path from a recording to a material.

TERA is a working demonstration of exactly that path, and the inharmonic mode is the proof it applies here. Harmonic-only analysis would be useless for this work — metal, glass and struck bodies are inharmonic by nature, and MaterialLab's data model already stores an arbitrary frequency array per mode rather than integer multiples. TERA's inharmonic mode validates that choice and shows the analysis side is tractable.

Three transfers, in descending order of value:

An analysis front end for MaterialLab. FFT a sample, pick peaks, estimate each peak's frequency, amplitude and decay rate, and write them into the existing nsr::Material structure. The resonator engine already consumes that structure, so this is additive — nothing existing changes. It would mean any recording in the pool could become a struck body.

Morph between two materials. Two MaterialVoice instances with interpolated mode parameters, which is the cross-breeding idea already on the proposed list, now with a demonstrated precedent.

A formant stage. Twelve formants with selectable input is a separate shaping layer from the resonator bank — body resonance after mode resonance. Cheap, and it does something the mode bank cannot: broad vocal-tract-like colouring rather than discrete ringing.

The decay-rate estimation in step 1 is the only genuinely hard part, and it is well-understood: fit an exponential to each peak's magnitude envelope across successive frames. Everything else is standard peak-picking.

GRM Tools Atelier — what it settles about JaqRack

Atelier is not an effect and should not be treated as a source of effects. It is a competitor to JaqRack, and its value is that it answers a design question already open.

From INA GRM — Pierre Schaeffer's institution, founded 1958 — Atelier is a modular environment for creative sound manipulation and generative synthesis, comprising freely chainable processor and modulator modules including granular sample playback, spectral filtering, pitch shifting, multi-tap delay and comb filtering, with a polyadic modulation engine and unbounded multichannel processing. The vendor is explicit that it comprises Modules which are freely chainable and connectible: Processors which generate or transform sound, and Modulators which generate modulations, freely assignable to any parameter. It launched with eight modules, and shares no single line of code with GRM Tools 3.

The bus question, answered partly

The JaqRack sketch so far proposed four buses: audio, events, modulation and groove. Atelier has two module classes — Processors and Modulators — and one assignment layer letting any modulator reach any parameter. That is the audio and modulation buses, confirmed by a serious institution's considered design, and it suggests the module class distinction matters more than the cable count: a module either makes sound or makes control, and the patching rules follow from which it is.

Atelier has nothing corresponding to an events bus or a groove bus, because it is not a rhythm system. It is a musique concrète workshop, descended from an institution concerned with sound objects rather than with time grids.

That absence is the useful finding. The groove bus is the Neosystem's differentiator and no competitor in this batch has one. Chassis has patterns but not a shared groove; Atelier has modulation but not metre; inDelay has swing but only its own. A modular host where swing is a first-class signal carried on its own bus — so that every module in the patch inherits the same swung grid, and a module can modulate the groove itself — is a genuinely unoccupied position. It is also the one feature that follows directly from the research premise rather than from imitating a competitor.

So: keep four buses, but understand that two of them are table stakes and two are the reason for building it at all. If JaqRack ships with only audio and modulation it is a weaker Atelier. The build order should reflect that — groove and events early, not as a later addition.

"Polyadic" means many-to-many: one modulator reaching many parameters and many modulators summing into one. The specific thing worth copying is making that cheap to express at the interface, since a modulation matrix that is technically complete but tedious to patch goes unused.

Chassis — five mechanisms, already catalogued

From the teardown of 23 November, held in the repo at docs/research/Chassis teardown and what to take from it.md. Chassis is a modular synthesis and sampling environment by Lux Cache, built on JUCE, and chassisFX turned out to be the same plugin declared a second time with a different category rather than a separate product.

Five of its modules each contribute one idea.

frappe — the object is a tiling, and the picture is the object. The display is not a visualisation of a model held elsewhere; the geometry on screen is the data the DSP reads. This is the same decision MaterialLab's mode display half-makes already, drawing modes as log-frequency bars with alpha carrying ring time. Making that picture editable rather than merely indicative is the step, and TERA's CUIPartialEditor is the precedent for it.

frappe — two listening points, not a pan. Stereo comes from sampling the object at two positions rather than from panning a mono result. MaterialVoice already does exactly this, with sign alternating by mode index and depth following a frequency-dependent law. Independent arrival at the same answer, which is reassurance rather than new work.

grille — a grid of patterns with a continuous reading point. A 5×5 arrangement of patterns, read not by selection but by a position that moves continuously between them, interpolating. This is the strongest unbuilt idea in the Chassis set for NeoBeat: a pattern bank you drift across rather than switch between.

grille — seeded randomness with a repeat probability. Randomness that can recur. A seed makes a chaotic pass reproducible, and a repeat probability decides whether the next bar re-rolls or repeats what it just did. This is the mechanism IDM Jack needs to produce motifs rather than noise — your stated goal was generative chaos with motifs, and a repeat probability is precisely what turns one into the other. Without it, every bar is unrelated to the last and nothing coheres; with it set high, a chaotic figure persists long enough to register as an idea before mutating.

coupe — eight heads over one buffer. Multiple independent playback positions in a single sample. Batchcut's CUT room has one. Eight would make it a performance instrument rather than an editor.

fracas — a router from any control source to any internal setting. The modulation-matrix-as-module idea, which Atelier reaches independently with its polyadic engine. Two independent arrivals at the same design for JaqRack.

tidy — theming as a module. Appearance handled as a patchable component rather than a stylesheet. Synplant does something comparable with 304 tokens driving a vector drawing language. The transferable form for the Neosystem's planned theme layer is that themes should be data — a token set — rather than code, so a skin is a file rather than a build.

Petri, Synplant, PolyNodes — and the honest cost

From the 23 November study, in docs/research/Generative engines - Petri, PolyNodes, and what to build.md. The headline finding stands and is worth restating because the marketing of all three obscures it: almost none of this is machine learning.

Three distinct mechanisms sit behind the four products.

Parameter search. Petri's "grow" and Synplant's Genopatch both work this way: take a synth with parameters normalised to 0–1, define an error function measuring how far a rendered candidate sits from a target sound, and optimise. The standard error function is a multi-resolution STFT loss — log-magnitude spectrograms at several window sizes, differences summed, because one window size alone trades time precision against frequency precision and percussion needs both. Petri uses an evolutionary optimiser; Synplant uses gradient descent. Nothing is learned. The intelligence is the search.

Analysis-driven resynthesis. PolyNodes has no neural network at all. It performs multi-scale onset analysis, lays each detected event out in 2D or 3D space by feature or by dimensionality reduction, and runs agents — playheads with movement strategies — that sound the events they pass near. Entirely classical. Batchcut already does the onset analysis, at one scale.

Learned embedding. Petri's map, and only the map, is genuine machine learning: one vector space holding both sounds and words, which buys perceptual nearness and search by typed description. It needs a pretrained audio encoder of tens of megabytes plus an inference runtime. The cheap substitute is handcrafted features — MFCCs, spectral centroid, flatness, rolloff, attack and decay times — reduced to 2D by UMAP or t-SNE. That gives a genuinely useful clustered map of your own library with no model and no runtime. The only thing lost is text search.

The cost finding, which has not changed

The conclusion of that study was that neither Batchcut nor a trial JaqRack is the cheap route, and it is worth repeating here because this document is otherwise full of proposals for rooms.

Batchcut has no synthesis engine — it cuts, analyses and exports. A parameter search needs a synth whose parameters it can search, so "put it in Batchcut" silently also means "put a synth in Batchcut". JaqRack does not exist, and building a graph engine, scheduler, cable UI and module-wrapping layer to trial one algorithm is weeks of work before the first sound.

The cheapest route is a console test harness in the shared tree, matching the dozen that already exist there: Source/neobeat/shared/grow/ for the genome, error function and optimiser, and tools/test_grow.cpp to load a target, run the search and write candidate wavs with the error printed per generation. A day or two, and it answers the only question that matters — whether the sounds please you — with no interface at all.

This is the one item in this document that should not become a room. It becomes a room later, in Batchcut, once the engine is proven and pointed at NeoBeat's synth via the shared tree.

The two Chimeras — different plugins, both useful

There are two unrelated plugins called Chimera in play, and conflating them would have sent the work in the wrong direction.

jackboyman Chimera — a merge engine

A free Windows VST3 instrument. The vendor's entire description is that it takes two sounds in and produces one blend out, with spectral, granular and convolution merge modes. Category confirmed as Instrument, Synth from the VST3 manifest, so it is a sound engine rather than an effect.

Three merge modes means three separate pieces of work, with very different costs:

Convolution is the cheapest and the most relevant. Source A played through the impulse response of source B. MaterialLab's resonator path already accepts arbitrary audio input, so a large part of this exists — what is missing is deriving the response from a second pool sound rather than from a mode set.

Spectral merge means FFT both, combine magnitudes and phases by some rule, inverse FFT. Moderate cost, and the rule is where all the character lives.

Granular merge means interleaving grains from both sources. Cheapest to implement badly, hardest to make musical.

Convolution is the one to take, and it is close to free given what MaterialLab already has.

Majken's Chimera — noise-excited resonance

A different plugin entirely, by a developer who makes instruments and effects as a creative pastime in SynthEdit and Reaktor. It is a noise-driven synth where white noise generators are connected through three pitch-controlled resonant bandpass filters, described elsewhere as producing organic sounds ranging from warm pads to atmospheres with elements of wind and water, with all sounds based on white noise filtered into notes.

The uploaded binary was a stub, so none of this is verified against code — but the method does not need verifying, because it is the same principle MaterialLab already tests. The test suite includes a case confirming that noise through a 420 Hz body emerges at the body's pitch. Majken's Chimera is that idea taken as a whole instrument rather than as one capability.

The transferable observation is narrow but worth recording: three resonant bandpasses are enough to make a playable instrument. MaterialLab routinely loads 128 modes. If three filters tracking pitch produce usable pads, then a deliberately reduced material — the three or four strongest modes, pitch-tracked to the keyboard rather than fixed — is a different and simpler instrument hiding inside the engine already built. That is a mode switch, not a new engine: take the existing mode set, keep the loudest few, track them to incoming notes.

The SOPHIE research — and why Chimecho delivers it

The practice-led finding from the 2020 project was that the metallic sounds carry a very slight interaural delay, a couple of milliseconds, constantly moved by LFOs; that this produces a spatiality with essentially zero distance; and that the sounds therefore seem to be inside the listener rather than positioned around them. The suggestion was that turning up feedback on a stereo phaser does something similar by feeding each channel back into the other.

The psychoacoustics support this precisely, and the mechanism is worth stating because it tells you the parameter ranges.

The maximum interaural time difference a real head can produce is about 690 microseconds — the time for sound to travel the extra distance around the skull from one ear to the other. Below roughly 1 millisecond, two channel signals fuse into one perceived event whose apparent position depends on the delay, which is summing localisation. Above roughly 30 to 40 milliseconds the second arrival is heard as a separate echo.

A delay of two to three milliseconds sits in neither regime. It is far too long to correspond to any physical head position — no real source anywhere in space could produce it — and far too short to resolve as an echo. The auditory system receives an unambiguous localisation cue that maps to no possible external location. The documented result is inside-the-head locatedness: the sound is definitely somewhere, and that somewhere is not in the room. That is the effect described, and the explanation for why it reads as the body being opened rather than as a wide stereo image.

Modulating that delay continuously prevents the system from settling, so the position never resolves. And the cross-channel feedback observation is correct: feeding each channel's delayed output into the other builds a recursive interaural structure, which is why phaser feedback produces a related sensation.

The convergence

Chimecho and this research describe the same module from two directions.

Chimecho is a 16-band split where each band has its own delay, its own delay LFO, and cross-band feedback. The SOPHIE mechanism is a short interaural delay under LFO with cross-channel feedback. Combine them and the module is: sixteen bands, each with an independently modulated left-right delay offset in the 0.5 to 5 ms range, with feedback crossing both between channels and between bands.

That is one build satisfying both findings, and it is strictly more capable than either. A broadband version applies one interaural delay to everything; a per-band version can place the metallic upper bands inside the head while the low end stays solidly external and mono — which is what BassMono is already there to guarantee.

The parameter ranges follow from the psychoacoustics rather than from taste: offsets must stay above 690 microseconds to break the physical-position mapping, and below about 20 milliseconds to avoid echo perception. The usable window is roughly 0.7 to 10 ms, and the interesting part of it is the bottom third.

Dead ends, and what is still needed

Four of today's five uploads contained no code

black.64.dll, Ironhead-Bonk.64.dll, Majkens_Chimera.64.dll and Chimecho.64.dll are all byte-identical: 3,584 bytes, SHA-256 7e9ce081f78e646fd5d664771628fbf32c7c88e99fff5ef59b0f35a64cb13da4. They are jBridge bootstrap stubs. Their strings make the mechanism plain — they read the adjacent .txt file, look up Software\\JBridge then Proxy64 in the registry, load the proxy DLL and call BridgeMain.

The accompanying .txt files are not documentation; they are the stub's input, which is why each one names a path under D:\\.32bit 01\\ or D:\\.32bit 16\\. The real DSP lives in those 32-bit DLLs.

Decoded VST2 identifiers, for the record: black is blak, Ironhead-Bonk is isbo, Majken's Chimera is chim, Chimecho is Chmc.

What would actually help

Target

Still needed?

Why

Chimecho

No

Fully specified by its published parameter list

Majken's Chimera

No

Method is known and already proven in MaterialLab's tests

black

Yes — D:\\.32bit 01\\black\\black.dll

Nothing at all is known about it

Ironhead-Bonk

Yes — D:\\.32bit 01\\Ironhead-Spawn (1)\\Ironhead-Bonk.dll

Nothing at all is known about it

jackboyman Chimera

Useful

Only a one-line vendor description; the merge rules are the interesting part

GRM Atelier

Useful

Architecture is clear, module internals are not

Limits of what was established

TERA was analysed from its section table, string table, statically linked library versions and surviving RTTI class names. That is strong evidence for what the engine contains and weak evidence for how each part is implemented. The claim that it performs FFT additive resynthesis with harmonic and inharmonic modes is well supported; any claim about its specific peak-tracking or envelope-fitting method would not be. Decompiling for that detail is possible with the reverse-engineering tooling now available, but is probably unnecessary — the method is standard and the hard part is parameter choice, which reading their code would not settle.

The open-source claim for Chimecho could not be verified. ConcreteFX released around 34 freeware Win32 plugins and no source repository was found for any of them. It makes no practical difference, since the published specification is complete and building from it touches no proprietary code at all — which is the better position anyway.

On transfer method

The file-upload route worked for all five of today's targets where the device bridge had failed on every binary. That is now the known-good path: upload the DLL or executable directly rather than staging from the connected folders.

Priority order

Ranked by value per hour of work, not by interest. The first item is not from today's research but outranks everything that is.

#

Work

Cost

Why here

1

Audit NeoBeat for the four import bugs MidiJaQ had

Hours

Pure risk reduction. Private inheritance on the drop target, missing canSelectFiles, unstarted session, unwired callbacks. If NeoBeat shares them, they are already shipped

2

Spectral delay room, with per-band interaural offset

1–2 days

Cheapest significant new capability, and it delivers the SOPHIE finding in the same build. Biquad bank, 16 delay lines, interpolator, cross-feed

3

Distance and air absorption in Place

Hours

Smallest change with the largest perceptual return, and every plugin inherits it

4

MaterialLab analysis front end

2–4 days

Highest ceiling of anything here. Removes the dependency on the learner's JSON entirely

5

Bounce Delay restructure as a tap engine with selectable position source

2–3 days

It needs rebuilding regardless, so restructure now rather than twice

6

IDM Jack, with three randomisation axes and seeded repeat probability

2–3 days

Already specced; today's findings improved the spec before it was built

7

Convolution merge in MaterialLab

~1 day

Nearly free — the audio-through path already exists

8

Formant stage after the mode bank

~1 day

Cheap, and does something the mode bank cannot

9

Pattern grid with a continuous reading point

2–3 days

Strongest unbuilt Chassis idea for NeoBeat

10

Generative search console harness

1–2 days

Answers whether the sounds please you, with no interface

11

Eight heads in the CUT room

2–3 days

Turns an editor into an instrument

12

JaqRack

Weeks

Defer. But design the groove bus first when it starts

Three items already decided and still unimplemented sit outside this ranking because they are commitments rather than proposals: MIDI file export from MidiJaQ with baked timing, saving loose pool sounds in project state, and BassMono across the whole mix.

Items 2 and 3 together are the best single week available. They are cheap, they compound — the spectral delay's bands become Stereoroom nodes that immediately benefit from distance — and between them they satisfy two separate research threads that have been open for a while.

Proposed new rooms

The test for whether something is a room

Worth settling first, because today's research generates more ideas than should become rooms. A room earns its place when it has its own working surface — something to look at and manipulate that no existing room provides — and it produces or consumes pool items. If it has no surface of its own it is a parameter page; if it touches no pool items it is a shared engine that other rooms call. By that test, six of today's findings are rooms, two are extensions of existing rooms, and three are not rooms at all.

Batchcut — rooms that make material

Batchcut already analyses, already holds the pool, and is the preset editor. New rooms here should produce material for the rest of the family to play.

SPECTRUM — from TERA. Takes a pool sound, runs FFT analysis, picks peaks, fits a decay rate to each, and writes the result into the existing nsr::Material structure. The surface is a partial editor: modes drawn on a log-frequency axis, each one draggable in frequency and level, with ring time as a third dimension. Harmonic and inharmonic modes, following TERA. Inherits bc::analyseItem and the pool; produces materials the MATERIAL room plays. This is the room that ends the dependency on the learner's JSON files — any recording becomes a struck body.

MERGE — from jackboyman Chimera. Two pool items in, one out. Convolution first, since MaterialVoice already accepts arbitrary audio input and the work is deriving a response from the second item rather than from a mode set. Spectral merge later if convolution proves interesting. The surface is two drop targets and a blend control. Produces pool items, which is what makes it a room rather than a right-click action.

NeoBeat — rooms that generate and mangle time

JACK — IDM Jack, already specced, now improved by two of today's findings. Three randomisation axes rather than two, following Density: pitch, parameter, and micro-timing in the 1–100 ms window that reads as flam or smear rather than as a separate hit. And seeded randomness with a repeat probability, following Chassis grille — this is the mechanism that produces the motifs you asked for rather than undifferentiated noise. A seed makes a pass reproducible so a take you liked can be recovered; a high repeat probability lets a chaotic figure persist long enough to register as an idea before it mutates. Held control, as decided: chaos while on, original pattern restored the moment it is released.

GRID — from Chassis grille. A 5×5 bank of patterns read by a continuous position rather than by selection, interpolating between neighbours. The surface is the grid itself with a draggable reading point. This is the strongest unbuilt idea in the Chassis set and it is distinct from JACK: GRID is deliberate navigation between composed patterns, JACK is chaos. They answer different needs and should not be merged.

MidiJaQ — the room that emits notes

MOTIF — JACK's logic in the MIDI domain. Seeded chaos with a repeat probability placing notes on the invisible swung sixteenth-triplet grid, emitting MIDI rather than mangling audio. This matters because of a decision already taken: MIDI file export with baked timing. A chaotic pass that emits notes can be exported, dropped into Ableton and edited, which an audio-domain chaos pass cannot. For sound design you want JACK; for a part you will keep and revise, MOTIF.

Shared — the convergence room

BANDS — Chimecho and the SOPHIE research in one build, in Source/neobeat/shared/rooms/ so Batchcut, NeoBeat and MidiJaQ all inherit it byte-identically. Sixteen resonant bandpasses with interpolated centre frequencies and shared Q; a delay line per band with interpolated time, feedback and level; a delay LFO per band; cross-band feedback with a signed offset; each band a Stereoroom node with its own Travel; and a per-band interaural offset in the 0.7–10 ms window. Two knobs drive sixteen of everything, following Chimecho's Start/Finish scheme.

This is the cheapest significant room in the document and it closes two research threads at once.

Extensions, not new rooms

CUT gains eight heads, from Chassis coupe. Eight independent playback positions over one buffer turns the CUT room from an editor into a performance instrument. It is the same room with more heads, so it does not need its own surface.

Place gains distance, from inDelay's air simulation, with a one-pole lowpass per node whose coefficient follows modelled distance. This is shared-tree work in StereoRoom.h, inherited everywhere, and it is the single cheapest perceptual improvement available.

Not rooms

Bounce Delay is a single-purpose effect and should stay one. What it needs is the architectural restructure in the panner section — a tap engine whose positions come from a selectable source — not a room system.

The generative search stays a console harness in the shared tree until it is proven, for the reasons set out above. It becomes a Batchcut room later, once there is a synth for it to search.

The formant stage is a parameter page on MATERIAL, not a room. It shapes what the mode bank produces and has nothing of its own to display.

Skins: the theme layer

The stated direction is glassy surfaces, deep dimensional curves, detailed shadowing, under-liquid textures, iridescence, CD-disc materials, futuristic industrial and alien, and window shapes that are not rectangles. A live proof of the whole stack is at Neosystem skin proof — four colourways across three outlines, cursor-driven, drawn with nothing JUCE lacks.

Starting position

The three trees contain no OpenGL, no shader module, and no embedded image assets at all. Every pixel today is drawn procedurally through juce::Graphics. A LookAndFeel already exists in the shared Ui.h, so the hook is in place.

A skin is two data files

A token set, and a set of outlines.

The tokens: surface tint, depth, specular (intensity and tightness), iridescence (amount, hue span, driver), rim, grain, accent, ink. One JSON file.

The outlines: one SVG path per panel. This is the part that matters, because it is what separates a theme from a skin. Recolouring one fixed rectangle gives you a theme. Changing the silhouette gives you an object, which is why the Winamp collection reads as twenty different devices rather than one player in twenty colours. JUCE parses SVG path data natively, so arbitrary outlines cost nothing structural.

A plugin editor is always a rectangle the host hands you — the OS window cannot be made non-rectangular. But none of the reference material depends on that. The shaped silhouette is painted against a dark field, which is how every skin of this lineage has always worked.

Technique per quality

Quality

Method

Cost

Non-rectangular panels

juce::Path built from SVG data

Free

Deep curves, detailed shadowing

Inner shadow opposite the light, inner glow toward it, both stroked inside a clip

Low

Glass

Vertical base ramp, radial lift toward the light, rim gradient along the outline

Low

Iridescence

Multi-stop hue ramp whose axis rotates with the light, laid on at low alpha

Low

CD / disc diffraction

180 interpolated wedges between two radii — JUCE has no conic gradient

Moderate

Grain

Cached noise tile, not per-frame dots

Low

True refraction, per-pixel normals

GLSL via juce_opengl

High, and not advised

OpenGL is the one to avoid. GL contexts inside plugin hosts are a known source of blank editors and crashes, and everything above except genuine refraction is reachable without it.

Iridescence needs a driver

Thin-film colour and disc diffraction are both angle-dependent in the world, and a flat interface has no viewing angle. Something has to supply one. Cursor position is the right choice: the hue ramp rotates and the specular slides as the pointer crosses the panel, so the surface reads as a physical thing being tilted. It costs one gradient recompute per mouse move. Panels the cursor is not over fall back to a slow drift, and control value or audio level can drive them instead where that suits.

Harvesting Winamp skins

Over 100,000 classic skins are preserved and browsable. A .wsz is a renamed ZIP holding around 45 files, mostly BMPs, plus text files describing how they are applied. Among them is region.txt, an INI-format file defining the window as connect-the-dots polygons.

The bitmaps are not usable: classic skins target Winamp 2's 275 × 116 pixel window, far too small for a plugin chassis, and a media player's play, volume and balance controls do not map onto rooms, pads and mode displays.

region.txt is the prize. Those polygons are coordinates, so they are resolution-independent and convert straight into juce::Path. That is a very large library of free, scalable panel silhouettes — exactly the scarce input for a shaped-skin system. Palette extraction from the BMPs is a secondary benefit.

The importer is small: juce::ZipFile, an INI parser, and a short BMP reader if palettes are wanted (JUCE does not decode BMP by default). Build the importer; do not ship the skins — the artwork belongs to the people who made it, and a skin a user supplies for their own use is a different matter from redistribution.

Keep the ornament off the data

Several of the strongest references are display pieces rather than working interfaces. A sixteen-band spectral delay or the MATERIAL mode display cannot be read through chrome tribal flames.

The references that solve this — DESKMAA, and the plugin faceplates — put the ornament in the chassis and keep the working surface flat and high contrast. That split is the rule: glass, iridescence and disc materials in the frame, bezels and transport; flat legible rendering for the stage, the mode bars, the band grid and the pattern matrix.

Where the skin work stands — paused 26 Nov, unfinished

Built, tested, and working in the container. Nothing is committed — all of it sits uncommitted across the three worktrees and would be lost if they go.

Done:

.nsk format, draft 1 — a renamed ZIP of skin.json, colors.json and optional sprite sheets. Fixed layout, swappable pixels.

nsr::SkinPack in Skin.h, byte-identical across all three trees. Loads the archive, parses colours, exposes 9-slice chassis, filmstrip knob, sprite and meter draws. Every draw returns false when its sheet is absent so the caller falls back.

nsr::SkinLibrary — scans a folder for .nsk, reads only skin.json from each, sorts by name.

The bridge in Ui.h: a pack's colors.json folds into the existing ~70 nsui::Skin tokens, so every C:: call site picked it up with no change. applyNamed, currentPackName, restorePackFromState, refreshLook.

Live switching, verified across five skins. Cheap because every paint helper reads skin() at draw time rather than caching — any future code that caches a colour in a member breaks this.

Deeper drawing: bevelRing rolled bevels, four-stop plate and well gradients, and an LCD with bezel, phosphor bloom, scanlines and glass wipe. New tokens: bevelLight, bevelDark, plateBevel, wellBevel, lcdGlow, lcdGlowAmount, lcdScanlines, lcdSheen, lcdBezel.

nsk.py — generates the blank labelled template, and wraps a real .wsz. Verified against the stock Winamp 2.91 skin: its PLEDIT.TXT and VISCOLOR.TXT populate colors.json directly.

tools/test_skin.cpp, added to MidiJaQ's CMakeLists. All green, including the check that each switch actually changes the look — which exists because a silent no-op once passed as success.

Two bugs worth remembering, both found by looking rather than by the console: Editor.cpp called setSkin during construction and wiped the loaded pack, fixed with nsui::lookFor(base); and parseColour turned any eight-letter word into a colour because getHexValue skips what it cannot read.

Not done, in rough order of what blocks what:

Freezing per-room geometry — decided 26 Nov: it happens last, once the plugins are finished. Winamp's hundred thousand skins came from a layout contract that never moved for twenty-five years, not from its file format, so the same promise is what radically different skins need here. But a contract frozen while rooms are still being added would be broken within the week, and every skin made against it broken with it. Two consequences follow: painted plates wait, so skins stay colours-plus-sheets until then and cannot yet carry a Pool Party or Sonique face; and the format must stay layout-free in the meantime — which it is, by design — so that skins made now survive rooms moving.

Painted plates instead of the 9-slice chassis. A stretchable frame can only recolour. Both plugins already scale uniformly at a fixed 1.6 aspect — MidiJaQ fixed at 1280 × 800, NeoBeat 0.5× to 1.5× of 1440 × 900 — so a 1:1 painting at design size would work with no resize problem. The 9-slice solved a problem these plugins do not have.

buttons.png needs splitting. It is specced as 8 glyphs × 4 states, but nearly every button in the family is a text button with no glyph. Needs a separate button.png holding a 9-slice background × 4 states.

The in-plugin skin menu. The library and switching exist; nothing surfaces them in the UI yet, and the chosen name is not yet written into plugin state.

Chassis tiling. Winamp's MAIN.BMP border was never meant to tile, so the wrapper's centre cell repeats visibly. Fix in nsk.py — flatter centre crop, or set tile: false.

A four-face detailed UI study (Machined, Abyssal, Reactor, Lumen — stacked grain, bevel, perforation, rivets, wear, occlusion, screen bloom) was written but never published. The file is at /home/claude/detailed-ui/index.html in that session's container and will not survive it; it would need rebuilding.