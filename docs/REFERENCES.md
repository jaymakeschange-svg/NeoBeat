# Reference index — Neosystem

Every document and demo produced for this project, with what it settles. Committed here because
the originals live as Claude artifacts, which are invisible to `grep` from a build container — and
answering from recollection instead of opening them has already produced wrong answers.

**Rule: before stating where a design came from, or what was decided about it, grep this directory.**

## Documents

| Doc | Date | What it settles |
|---|---|---|
| [`research/transferable-mechanisms.md`](research/transferable-mechanisms.md) | 26 Nov 2026 | Reverse-engineering notes across fourteen plugins, routed to the plugin each mechanism belongs in. Source of the BANDS spec (Chimecho), the Bounce Delay restructure (Sound Particles panners), the IDM Jack improvements (Density), and the priority ranking. |
| [`PROPOSED-FEATURES.md`](PROPOSED-FEATURES.md) | 26 Nov 2026 | Running status of every proposed feature across NeoBeat, MidiJaQ and the shared tree. |
| [`DECISIONS.md`](DECISIONS.md) | ongoing | Every decision, who made it, and where it is recorded. Append-only. |
| [`house-rules.md`](house-rules.md) | 16 Nov 2026 | How to work on this project: the division of labour, what must never be done, the house style, the shared-header convention. |
| [`research/neosystem-build-list.md`](research/neosystem-build-list.md) | 19 Nov 2026 | What changes in each plugin and in what order. **Source of the randomiser spec** (one press randomises everything; right-click opens the box: speed, swing, extremity, cycling rhythmically and landing back on the original), the Orb design, and the stereo room. |
| [`DECISIONS.md`](DECISIONS.md), "Next in the same build: BANDS with per-lane sends, and the preset bar" | 27 Nov 2026 | BANDS as a send (how the by-ear balance was kept, where the sends tap, what is not sent), the preset bar's item mode, the 16 house and techno patterns (129-144) with the rules they hold and their limits, the host test's X window failure. Code: `Source/ui/BandsWindow.cpp`, `BandSend` in `Source/engine/Rt.h`, `Source/shared/PresetBar.*`, `clubDefs()` in `Source/engine/Presets.h`. |
| [`DECISIONS.md`](DECISIONS.md), "27 November 2026 — the test build" | 27 Nov 2026 | How the randomiser (ROLL) and IDM Jack were built in NeoBeat, the five calls made without being asked, the tests, and the pre-existing body-sound NaN found on the way. Code: `Source/shared/JackCore.h`, `Source/ui/JackRoom.cpp`, `Source/engine/Workstation.h` (search "IDM Jack"), `tools/test_jack.cpp`. |
| [`research/materiallab-research-log.md`](research/materiallab-research-log.md) | 5 Nov 2026 | The learner: impacts, friction, liquids, gestures, and the models fitted to recordings. |

## Demos and visual studies, by link

These are web pages rather than prose, so they are indexed rather than inlined — the text of a
rendered demo is not what makes it useful.

| Demo | Date | What it shows |
|---|---|---|
| [Transferable mechanisms for the Neosystem](https://claude.ai/artifact/CSjXxpZM3BNaNYFBtoTXeH) | 26 Nov | The research doc itself, as a live doc |
| [.nsk — Neosystem skin format](https://claude.ai/artifact/Dm9CXhk8HiDW3NLAL1pEyq) | 26 Nov | The skin format spec, draft 1 |
| [.nsk — template and a wrapped Winamp skin](https://claude.ai/artifact/Tyuu598PiuAfFF1sBh5e1u) | 26 Nov | A blank labelled template, and a real `.wsz` wrapped into it |
| [Where the detail comes from](https://claude.ai/artifact/8ExdmDA2kxqqVEsLvtayv7) | 26 Nov | Nine-pass material study: what needs images rather than gradients |
| [NeoBeat — six skins, four rooms](https://claude.ai/artifact/P7YUnfmmD6SynnYi7cRTYd) | 26 Nov | Six skin treatments across NeoBeat's rooms |
| [Winamp skin import](https://claude.ai/artifact/4XsAHgS9L8RUdTCc5Yxtsb) | 26 Nov | Importing a legacy `.wsz` into the plugin chassis |
| [Neosystem skin proof](https://claude.ai/artifact/W3cAsZGoByfGELKL2wmNaK) | 26 Nov | First proof that a skin pack changes the render |
| [The Neosystem build list](https://claude.ai/artifact/APNEGryYj8Xs1TyioMBgL6) | 19 Nov | Earlier build ordering |
| [MidiJaQ and Batchcut: handover](https://claude.ai/artifact/DXuWnTuGspj4sHv4anHgeN) | 16 Nov | Handover state, incl. the four import bugs |
| [NeoBeat + Batchcut: the instrumental expansion](https://claude.ai/artifact/MoFMWRWoXMmEnmZww3CpfS) | 15 Nov | Before and after of the instrument work |
| [MaterialLab research log](https://claude.ai/artifact/APJZGMV7aTVFDHorc8Duoq) | 5 Nov | The learner: impacts, friction, liquids, gestures |
| [MaterialLab Listening Room](https://claude.ai/artifact/FjzpDmwqfujaeffVmtV2Qn) | 5 Nov | Every learner output, playable |
| [Reference Instrument Analysis](https://claude.ai/artifact/9tjv1D45RAbbJLpts4HsG7) | 3 Nov | Design notes for the merged plugin |
| [Liquid Swing: The Sound Plan](https://claude.ai/artifact/LRRcsyvUMKhtbSVD5xG4qd) | 26 Oct | The original sound plan |
| [Liquid Swing: Grid Engine Feasibility](https://claude.ai/artifact/NXjHFittyTta1jHE5eKp66) | 27 Oct | Grid engine feasibility |
