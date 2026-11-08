// NeoBeat 2.0 — the workstation: the real-time side of everything. Framework-free, so the plugin,
// the offline renderer and the tests run exactly the same code.
//
// Each block: the transport (host, standalone run, key mode), MIDI notes, auditions, the main
// sequencer, the grid and the Matrixxxer's marks become events; every note goes through the key
// map; a pad hit plays its One Shot cell, its inserts' render or a live voice, plus the window
// slots that listen to it. Sound is rendered in segments between events.
#pragma once

#include "Engine.h"
#include "MarkSynth.h"
#include "Players.h"
#include "RenderService.h"
#include "Sequencer.h"

namespace nb
{
struct MidiIn { int offset = 0; int type = 0; int note = 60; float velocity = 0.8f; };   // type 0 on, 1 off, 2 all off
struct MidiOut { int offset = 0; bool on = true; int note = 36; int velocity = 100; };

struct Transport { bool hostPlaying = false; double hostPpq = 0.0; double bpm = 120.0; bool hostHasPpq = false; };

struct BlockParams
{
    int mode = 0;          // 0 host, 1 key, 2 pads
    bool run = false;      // standalone: run the sequencer without a host transport
    float swing = 0.62f;
    std::array<bool, kNumPads> mute {};
    int tapPad = 0;
};

class Workstation
{
public:
    static constexpr int kPadNoteBase = 36, kKeyRoot = 60;

    Workstation() : engine (std::make_unique<Engine>()), players (std::make_unique<PlayerPool>()), marks (std::make_unique<MarkSynth>()),
                    renderer (std::make_unique<RenderService> (publisher))
    {
        events.reserve (4096);
        seqEvents.reserve (4096);
    }
    ~Workstation() { renderer->stop(); }

    void prepare (float sampleRate, int maxBlock)
    {
        sr = sampleRate;
        engine->prepare (sr);
        players->prepare (sr);
        marks->prepare (sr);
        sequencer.prepare (sr);
        maxN = std::max (1, maxBlock);
        scratchL.assign ((size_t) maxN, 0.0f); scratchR.assign ((size_t) maxN, 0.0f); tapBuf.assign ((size_t) maxN, 0.0f);
        heldKeys = 0; internalPpq = 0.0; keyPpq = 0.0;
        for (auto& o : pendingOffs) o = {};
    }

    void startRenderer() { renderer->start (sr); }

    // the audio thread hands over the main pattern when it changed
    void setPattern (const Pattern& p) noexcept { pattern = p; sequencer.setPattern (p); }

    void audition (int pad, float vel) noexcept { if (pad >= 0 && pad < kNumPads) pendingAudition[(size_t) pad].store (vel); }
    void auditionNote (int note, float vel) noexcept { pendingNote.store (note); pendingNoteVel.store (vel); }

    Engine& kit() noexcept { return *engine; }
    RenderService& render() noexcept { return *renderer; }
    RtPublisher<RtState> publisher;

    // ---------------------------------------------------------------- the block
    void process (float* L, float* R, int n, const Transport& tr, const BlockParams& bp,
                  const MidiIn* midi, int numMidi, std::vector<MidiOut>& midiOut, float* tap) noexcept
    {
        if (n <= 0) return;
        const RtState* rt = publisher.beginBlock();
        if (n > maxN) { publisher.endBlock(); return; }   // the host broke its promise: stay silent rather than allocate
        cur = rt;
        bpm = tr.bpm > 0.0 ? tr.bpm : 120.0;
        swing = bp.swing;
        mute = bp.mute;
        if (rt != nullptr)
        {
            static_cast<ModParams&> (liveMod) = static_cast<const ModParams&> (rt->mod);
            for (int p = 0; p < kNumPads; ++p)
                if (rt->padSample[(size_t) p])
                {
                    auto& ps = engine->pads[(size_t) p];
                    ps.useSample = true; ps.sample = rt->padSample[(size_t) p]; ps.sampleRms = rt->padSampleRms[(size_t) p];
                }
            engine->glue = rt->glue;
        }

        // ---- transport
        const bool keysWereHeld = heldKeys > 0;
        events.clear();
        for (int i = 0; i < numMidi; ++i)
        {
            const auto& m = midi[i];
            const int at = std::max (0, std::min (n - 1, m.offset));
            if (m.type == 2) { heldKeys = 0; engine->allNotesOff(); players->allOff(); marks->allOff(); gridKeyOn = false; continue; }
            if (bp.mode == 1 && m.note >= kKeyRoot)
            {
                if (m.type == 0) { ++heldKeys; keySemis = m.note - kKeyRoot; }
                else heldKeys = std::max (0, heldKeys - 1);
                continue;
            }
            addEvent ({ at, m.type == 0 ? Ev::NoteOn : Ev::NoteOff, m.note, 0, m.velocity, 0.0f, 0, 0, 0.0 });
        }
        if (bp.mode == 1 && heldKeys > 0 && ! keysWereHeld) keyPpq = 0.0;
        for (int p = 0; p < kNumPads; ++p)
        {
            const float a = pendingAudition[(size_t) p].exchange (0.0f);
            if (a > 0.0f) addEvent ({ 0, Ev::PadDirect, p, 0, a, 0.0f, 0, 0, 0.0 });
        }
        {
            const int note = pendingNote.exchange (-1);
            if (note >= 0) addEvent ({ 0, Ev::NoteOn, note, 0, pendingNoteVel.load(), 0.0f, 0, 0, 0.0 });
        }

        bool seqOn = false; double ppq0 = 0.0; float semis = 0.0f;
        if (bp.mode == 0)
        {
            if (tr.hostPlaying) { seqOn = true; ppq0 = tr.hostPpq; }
            else if (bp.run) { seqOn = true; ppq0 = internalPpq; }
        }
        else if (bp.mode == 1 && heldKeys > 0) { seqOn = true; ppq0 = tr.hostPlaying ? tr.hostPpq : keyPpq; semis = (float) keySemis; }
        const double ppqPerSample = bpm / 60.0 / sr;
        const double ppq1 = ppq0 + ppqPerSample * n;
        playing = seqOn; blockPpq = ppq0;
        computeFrame (frame, liveMod, ppq0, bpm);

        // master modulation
        if (rt != nullptr && ! rt->masterRoutes.empty())
        {
            float m = 0.0f;
            for (const auto& r : rt->masterRoutes) m += r.depth * frame.v[(size_t) r.src];
            engine->master = masterBase * std::pow (10.0f, 18.0f * m / 20.0f);
        }
        else engine->master = masterBase;

        if (seqOn)
        {
            seqEvents.clear();
            sequencer.collect (ppq0, bpm, n, true, swing, semis, seqEvents);
            for (const auto& e : seqEvents)
                addEvent ({ e.offset, Ev::SeqNote, e.pad, e.step, e.velocity, semis, e.sub, 0, e.ppq });
            if (rt != nullptr) { scheduleGrid (*rt, ppq0, ppq1, ppqPerSample, n); scheduleMarks (*rt, ppq0, ppq1, ppqPerSample, n, semis); }
            playStep.store (sequencer.stepAt (ppq0));
        }
        else
        {
            playStep.store (-1);
            if (wasSeqOn) marks->allOff();
        }
        wasSeqOn = seqOn;
        if (bp.mode == 0 && ! tr.hostPlaying && bp.run) internalPpq += ppqPerSample * n;
        else if (! bp.run) internalPpq = 0.0;
        if (bp.mode == 1 && heldKeys > 0 && ! tr.hostPlaying) keyPpq += ppqPerSample * n;
        transposeSemis = semis;

        std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });

        // ---- render in segments between events
        std::fill (tapBuf.begin(), tapBuf.begin() + n, 0.0f);
        int done = 0;
        for (const auto& e : events)
        {
            if (e.offset > done) { renderSegment (L, R, done, e.offset - done, bp.tapPad); done = e.offset; }
            handle (e, midiOut);
        }
        if (n > done) renderSegment (L, R, done, n - done, bp.tapPad);
        if (tap != nullptr) std::copy (tapBuf.begin(), tapBuf.begin() + n, tap);

        // MIDI note-offs for the notes the sequencer sent
        for (auto& o : pendingOffs)
        {
            if (o.note < 0) continue;
            if (o.samplesLeft < n) { midiOut.push_back ({ std::max (0, o.samplesLeft), false, o.note, 0 }); o.note = -1; }
            else o.samplesLeft -= n;
        }
        cur = nullptr;
        publisher.endBlock();
    }

    // UI feedback
    std::atomic<int> playStep { -1 }, gridCol { -1 };
    std::atomic<double> seqBeat { -1.0 };
    std::array<std::atomic<uint32_t>, kNumPads> cellHits {};
    int activePlayers() const noexcept { return players->activeCount(); }
    int activeMarks() const noexcept { return marks->activeCount(); }
    float masterBase = 0.35f;

private:
    struct Ev
    {
        enum Kind : uint8_t { NoteOn, NoteOff, SeqNote, PadDirect, GridCol, Mark };
        int offset; Kind kind; int a; int b; float vel; float semis; int sub; int c; double ppq;
    };
    void addEvent (const Ev& e) noexcept { if (events.size() < events.capacity()) events.push_back (e); }

    void renderSegment (float* L, float* R, int start, int len, int tapPad) noexcept
    {
        std::fill (scratchL.begin(), scratchL.begin() + len, 0.0f);
        std::fill (scratchR.begin(), scratchR.begin() + len, 0.0f);
        players->render (scratchL.data(), scratchR.data(), len, tapBuf.data() + start, tapPad);
        marks->render (scratchL.data(), scratchR.data(), len);
        engine->render (L + start, R + start, len, tapBuf.data() + start, tapPad, scratchL.data(), scratchR.data());
    }

    // ---------------------------------------------------------------- scheduling
    double gridStepPos (int k, double rb, float sw) const noexcept
    {
        if (rb > 0.2) return 2.0 * rb * (k >> 1) + ((k & 1) ? 2.0 * rb * sw : 0.0);   // 1/8 and 1/16 swing in pairs
        return rb * k;
    }

    void scheduleGrid (const RtState& rt, double ppq0, double ppq1, double ppqPerSample, int n) noexcept
    {
        if (! rt.grid) return;
        const bool on = gridKeyOn || rt.gridPlay;
        if (! on) { gridCol.store (-1); return; }
        const int pi = gridKeyOn ? std::max (0, std::min (gridKeyPattern, 15)) : rt.gridCurrent;
        const auto& pat = (*rt.grid)[(size_t) pi];
        const double rb = gridRateBeats (pat.rate);
        const float sw = pat.swing >= 0.0f ? pat.swing : swing;
        const int len = std::max (1, std::min (pat.length, GridPattern::kSize));
        const double loop = rb * len;
        const double origin = gridKeyOn ? gridKeyPpq : 0.0;
        for (int k = 0; k < len; ++k)
        {
            const double p = origin + gridStepPos (k, rb, sw);
            const double m = std::ceil ((ppq0 - p) / loop - 1.0e-9);
            const double at = p + m * loop;
            if (at < ppq0 - 1.0e-9 || at >= ppq1) continue;
            const int off = std::max (0, std::min (n - 1, (int) std::floor ((at - ppq0) / ppqPerSample)));
            addEvent ({ off, Ev::GridCol, pi, k, 1.0f, 0.0f, 0, 0, at });
        }
        const double rel = ppq0 - origin;
        gridCol.store ((int) std::fmod (std::max (0.0, rel) / rb, (double) len));
    }

    double markPos (const SeqPreset& p, float start) const noexcept
    {
        const double gb = gridBeats (p.grid);
        if (p.grid != 0) return start * gb;
        const int k = (int) std::floor (start); const double f = start - k;
        const double a = gridStepPos (k, 0.25, swing), b = gridStepPos (k + 1, 0.25, swing);
        return a + f * (b - a);
    }

    void scheduleMarks (const RtState& rt, double ppq0, double ppq1, double ppqPerSample, int n, float semis) noexcept
    {
        (void) semis;
        if (! rt.seqOn || rt.seq.marks.empty()) { seqBeat.store (-1.0); return; }
        const auto& p = rt.seq;
        const double loop = std::max (1, std::min (p.bars, 32)) * 4.0;
        seqBeat.store (std::fmod (std::max (0.0, ppq0), loop));
        for (int i = 0; i < (int) p.marks.size(); ++i)
        {
            const auto& m = p.marks[(size_t) i];
            const double pos = markPos (p, m.start);
            const double k = std::ceil ((ppq0 - pos) / loop - 1.0e-9);
            const double at = pos + k * loop;
            if (at < ppq0 - 1.0e-9 || at >= ppq1) continue;
            if (p.mirror && (m.type == MarkType::Dot || m.type == MarkType::Tick || m.type == MarkType::Spray))
            {
                const int len = std::max (1, sequencer.patternLength());
                const int step = (int) std::lround (std::fmod (at, len * 0.25) / 0.25) % len;
                bool any = false;
                for (int t = 0; t < Pattern::kTracks && ! any; ++t) any = pattern.steps[(size_t) t][(size_t) step].vel > 0;
                if (! any) continue;
            }
            const int off = std::max (0, std::min (n - 1, (int) std::floor ((at - ppq0) / ppqPerSample)));
            addEvent ({ off, Ev::Mark, i, 0, 1.0f, 0.0f, 0, 0, at });
        }
    }

    // ---------------------------------------------------------------- events
    void handle (const Ev& e, std::vector<MidiOut>& midiOut) noexcept
    {
        switch (e.kind)
        {
            case Ev::NoteOn: noteOn (e.a, e.vel, 0.0f, false, 0, 0, blockPpq); break;
            case Ev::NoteOff: noteOff (e.a); break;
            case Ev::PadDirect: hitPad (e.a, e.vel, 0.0f, 0.0f, 0.0f, false, 0, 0, kPadNoteBase + e.a, nullptr, blockPpq); break;
            case Ev::SeqNote:
                if (mute[(size_t) e.a]) break;
                noteOn (kPadNoteBase + e.a, e.vel, e.semis, true, e.b, e.sub, e.ppq);
                if (midiOut.size() < midiOut.capacity())
                {
                    const int note = kPadNoteBase + e.a;
                    midiOut.push_back ({ e.offset, true, note, std::max (1, std::min (127, (int) std::lround (e.vel * 127.0f))) });
                    for (auto& o : pendingOffs) if (o.note < 0) { o.note = note; o.samplesLeft = e.offset + (int) (0.06 * sr); break; }
                }
                break;
            case Ev::GridCol: gridColumn (e.a, e.b, e.ppq); break;
            case Ev::Mark: playMark (e.a, e.ppq); break;
        }
    }

    void noteOff (int note) noexcept
    {
        if (cur == nullptr) return;
        for (const auto& z : cur->zones)
            if (z.z.src == ZoneSource::GridPattern && note >= z.z.loKey && note <= z.z.hiKey && gridKeyOn && gridKeyNote == note) gridKeyOn = false;
    }

    // every note lands in the key map
    void noteOn (int note, float vel, float semis, bool fromSeq, int step, int sub, double ppq) noexcept
    {
        const RtState* rt = cur;
        if (rt == nullptr || rt->zones.empty())
        {   // no key map yet: the drum map
            if (note >= kPadNoteBase && note < kPadNoteBase + kNumPads) hitPad (note - kPadNoteBase, vel, semis, 0.0f, 0.0f, fromSeq, step, sub, note, nullptr, ppq);
            return;
        }
        const int v127 = std::max (1, std::min (127, (int) std::lround (vel * 127.0f)));
        // round robin: zones of a group take turns among those that match
        std::array<int, 16> rrSeen {}; int rrCount = 0;
        for (size_t zi = 0; zi < rt->zones.size(); ++zi)
        {
            const auto& rz = rt->zones[zi];
            const auto& z = rz.z;
            if (note < z.loKey || note > z.hiKey || v127 < z.loVel || v127 > z.hiVel) continue;
            if (z.layer >= 0 && z.layer < (int) rt->layers.size() && ! rt->layers[(size_t) z.layer].on) continue;
            if (z.rrGroup > 0)
            {
                bool seen = false; for (int i = 0; i < rrCount; ++i) seen |= rrSeen[(size_t) i] == z.rrGroup;
                if (seen) continue;
                // the members of this group that match, in order
                int members = 0, pick = -1;
                for (size_t zj = 0; zj < rt->zones.size(); ++zj)
                {
                    const auto& o = rt->zones[zj].z;
                    if (o.rrGroup == z.rrGroup && note >= o.loKey && note <= o.hiKey && v127 >= o.loVel && v127 <= o.hiVel) ++members;
                }
                const int turn = (int) (rrCounter[(size_t) (z.rrGroup & 15)]++ % (uint32_t) std::max (1, members));
                int c = 0;
                for (size_t zj = 0; zj < rt->zones.size(); ++zj)
                {
                    const auto& o = rt->zones[zj].z;
                    if (o.rrGroup == z.rrGroup && note >= o.loKey && note <= o.hiKey && v127 >= o.loVel && v127 <= o.hiVel) { if (c == turn) { pick = (int) zj; break; } ++c; }
                }
                if (rrCount < 16) rrSeen[(size_t) rrCount++] = z.rrGroup;
                if (pick >= 0) playZone (rt->zones[(size_t) pick], note, vel, semis, fromSeq, step, sub, ppq);
                continue;
            }
            playZone (rz, note, vel, semis, fromSeq, step, sub, ppq);
        }
    }

    void playZone (const RtZone& rz, int note, float vel, float semis, bool fromSeq, int step, int sub, double ppq) noexcept
    {
        const auto& z = rz.z;
        const float key = z.keytrack ? (float) (note - z.root) : 0.0f;
        switch (z.src)
        {
            case ZoneSource::Pad:
                if (z.index >= 0 && z.index < kNumPads)
                    hitPad (z.index, vel, semis + z.tune + key, z.gainDb, z.pan, fromSeq, step, sub, note, z.fx.kind != CellFx::None ? &rz.fxAudio : nullptr, ppq);
                break;
            case ZoneSource::Sample:
            {
                PlayRequest r;
                if (z.fx.kind != CellFx::None && rz.fxAudio) r.rendered = rz.fxAudio; else r.sample = rz.sample;
                r.gain = dbToGain (z.gainDb) * vel; r.pan = z.pan; r.semis = semis + z.tune + key; r.choke = z.choke;
                if (z.choke > 0) engine->chokeGroup (z.choke);
                players->play (r);
                break;
            }
            case ZoneSource::Kit:
            {
                if (cur == nullptr || cur->kitPads.empty()) break;
                const int k = std::max (0, std::min (z.index, (int) cur->kitPads.size() - 1));
                const int pad = ((note - z.loKey) % kNumPads + kNumPads) % kNumPads;
                PadSettings ps = cur->kitPads[(size_t) k][(size_t) pad];
                ps.levelDb += z.gainDb; ps.pan = std::max (-1.0f, std::min (1.0f, ps.pan + z.pan));
                if (ps.choke > 0) players->chokeGroup (ps.choke);
                engine->triggerWith (pad, ps, vel, semis + z.tune, nullptr, nullptr, 0.0f);
                break;
            }
            case ZoneSource::OneShot:
            {
                if (cur == nullptr || z.index < 0 || z.index >= (int) cur->cells.size()) break;
                const auto& c = cur->cells[(size_t) z.index];
                if (! c.audio) break;
                PlayRequest r; r.rendered = c.audio; r.gain = dbToGain (z.gainDb) * vel / 0.8f; r.pan = z.pan; r.semis = semis + z.tune + key; r.pad = c.pad;
                players->play (r);
                break;
            }
            case ZoneSource::GridPattern:
                gridKeyOn = true; gridKeyPattern = z.index; gridKeyNote = note; gridKeyPpq = ppq;
                break;
        }
    }

    static float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    // per-hit macro modulation and the voice's lanes
    void padMods (int pad, const HitContext& hc, PadSettings& ps, VoiceMod& vm, float& tuneOffset) noexcept
    {
        tuneOffset = 0.0f;
        vm = VoiceMod(); vm.state = &liveMod; vm.frame = &frame;
        vm.velocity = hc.velocity; vm.key = std::max (-1.0f, std::min (1.0f, (hc.key - 60) / 24.0f)); vm.height = hc.height; vm.step = hc.step;
        vm.rnd = hashUnit (hc.seed * 2246822519u + 3u);
        if (cur == nullptr) return;
        for (const auto& r : cur->padRoutes[(size_t) pad])
        {
            const float x = sourceAtHit (r.src, liveMod, hc) * r.depth;
            switch (r.dest)
            {
                case Dest::PadTune: tuneOffset += 24.0f * x; break;
                case Dest::PadDecay: ps.decay *= std::pow (4.0f, x); break;
                case Dest::PadHollow: ps.hollow = std::max (0.0f, std::min (1.0f, ps.hollow + x)); break;
                case Dest::PadEvolve: ps.evolve = std::max (0.0f, std::min (1.0f, ps.evolve + x)); break;
                case Dest::PadTone: if (vm.numLanes < 8) vm.lanes[(size_t) vm.numLanes++] = { r.src, VDest::Tone, r.depth }; break;
                case Dest::PadDrive: if (vm.numLanes < 8) vm.lanes[(size_t) vm.numLanes++] = { r.src, VDest::Drive, r.depth }; break;
                case Dest::PadPan: if (vm.numLanes < 8) vm.lanes[(size_t) vm.numLanes++] = { r.src, VDest::Pan, r.depth }; break;
                case Dest::PadLevel: if (vm.numLanes < 8) vm.lanes[(size_t) vm.numLanes++] = { r.src, VDest::Level, r.depth }; break;
                default: break;
            }
        }
        if (vm.numLanes == 0) vm.state = nullptr;
    }

    void hitPad (int pad, float vel, float semis, float gainDb, float panAdd, bool fromSeq, int step, int sub, int key,
                 const RenderedPtr* zoneFx, double ppq) noexcept
    {
        if (pad < 0 || pad >= kNumPads || vel <= 0.0f) return;
        const RtState* rt = cur;
        PadSettings ps = engine->pads[(size_t) pad];
        ps.levelDb += gainDb; ps.pan = std::max (-1.0f, std::min (1.0f, ps.pan + panAdd));
        // a One Shot cell on this hit
        int cell = -1;
        if (rt != nullptr && fromSeq && sub == 0 && rt->oneShotOn && step >= 0 && step < Pattern::kMaxSteps)
            cell = rt->cellAt[(size_t) pad][(size_t) step];
        HitContext hc; hc.beats = ppq; hc.bpm = bpm; hc.velocity = vel; hc.key = key; hc.seed = ++hitSeed;
        hc.height = cell >= 0 ? rt->cells[(size_t) cell].height : 0.5f;
        hc.step = (float) step / (float) std::max (1, sequencer.patternLength());
        VoiceMod vm; float tuneMod = 0.0f;
        padMods (pad, hc, ps, vm, tuneMod);
        const PadAlgo* algo = nullptr; float algoOffset = 0.0f;
        if (rt != nullptr && rt->mod.algos[(size_t) pad].type > 0)
        {
            algo = &rt->mod.algos[(size_t) pad];
            algoOffset = algo->srcDepth * sourceAtHit (algo->src, liveMod, hc);
        }
        if (ps.choke > 0) players->chokeGroup (ps.choke);
        const float level = dbToGain (ps.levelDb);
        const float pitchHz = engine->lib().sounds.empty() ? 200.0f : engine->lib().sounds[(size_t) std::max (0, std::min (ps.sound, (int) engine->lib().sounds.size() - 1))].pitchHz;
        auto request = [&] (const RenderedPtr& r, float gain)
        {
            PlayRequest q; q.rendered = r; q.gain = gain; q.pan = ps.pan; q.semis = semis + tuneMod; q.pad = pad; q.choke = ps.choke;
            q.algo = algo; q.algoOffset = algoOffset; q.pitchHz = pitchHz; q.mod = vm;
            return q;
        };
        bool replaced = false;
        // the window slots that listen to this pad (or to this cell)
        if (rt != nullptr)
        {
            const int parity = fromSeq ? (step & 1) : 0;
            auto playSlots = [&] (const std::vector<int>& list)
            {
                for (int idx : list)
                {
                    const auto& s = rt->slots[(size_t) idx];
                    const RenderedPtr& a = s.audio[(size_t) parity] ? s.audio[(size_t) parity] : s.audio[0];
                    if (! a) continue;
                    PlayRequest q = request (a, s.level * level * vel / 0.85f);
                    q.choke = 0; q.algo = nullptr; q.bus = 2;
                    players->play (q);
                    if (s.replace) replaced = true;
                }
            };
            playSlots (rt->padSlots[(size_t) pad]);
            if (cell >= 0 && cell < (int) rt->cellSlots.size()) playSlots (rt->cellSlots[(size_t) cell]);
        }
        if (replaced) { engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed); return; }
        if (cell >= 0 && rt->cells[(size_t) cell].audio)
        {
            players->play (request (rt->cells[(size_t) cell].audio, level));
            engine->chokeGroup (ps.choke);
            engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
            cellHits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
            return;
        }
        if (zoneFx != nullptr && *zoneFx)
        {
            players->play (request (*zoneFx, level * vel / 0.8f));
            engine->chokeGroup (ps.choke);
            engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
            return;
        }
        if (rt != nullptr && rt->hasInserts[(size_t) pad])
        {
            static constexpr float kLayers[3] = { 0.4f, 0.7f, 1.0f };
            const int L = vel < 0.55f ? 0 : (vel < 0.85f ? 1 : 2);
            if (rt->insertAudio[(size_t) pad][(size_t) L])
            {
                players->play (request (rt->insertAudio[(size_t) pad][(size_t) L], level * vel / kLayers[L]));
                engine->chokeGroup (ps.choke);
                engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
                return;
            }
        }
        engine->triggerWith (pad, ps, vel, semis + tuneMod, vm.state != nullptr ? &vm : nullptr, algo, algoOffset);
    }

    void gridColumn (int pi, int col, double ppq) noexcept
    {
        const RtState* rt = cur;
        if (rt == nullptr || ! rt->grid || rt->gridSamples.empty() || (size_t) pi >= rt->grid->size()) return;
        const auto& pat = (*rt->grid)[(size_t) pi];
        const int ns = (int) rt->gridSamples.size();
        HitContext hc; hc.beats = ppq; hc.bpm = bpm;
        float lvMod = 0.0f, panMod = 0.0f, pitchMod = 0.0f;
        for (const auto& r : rt->gridRoutes)
        {
            const float x = sourceAtHit (r.src, liveMod, hc) * r.depth;
            if (r.dest == Dest::GridLevel) lvMod += x; else if (r.dest == Dest::GridPan) panMod += x; else pitchMod += 24.0f * x;
        }
        const float lv = rt->gridLevel * std::pow (10.0f, 24.0f * lvMod / 20.0f);
        for (int row = 0; row < GridPattern::kSize; ++row)
        {
            const uint32_t c = pat.cells[(size_t) (row * GridPattern::kSize + col)];
            if ((c & GridPattern::kOn) == 0) continue;
            const auto& smp = rt->gridSamples[(size_t) (row % ns)];
            const float vol = std::pow (GridPattern::red (c) / 255.0f, 1.5f);
            const float pan = std::max (-1.0f, std::min (1.0f, (GridPattern::green (c) - 128) / 127.0f + panMod));
            const float semis = (GridPattern::blue (c) - 128) / 128.0f * 12.0f + pitchMod + transposeSemis;
            bool replaced = false;
            for (int idx : rt->rowSlots[(size_t) row])
            {
                const auto& s = rt->slots[(size_t) idx];
                const bool wants = s.fs ? (GridPattern::flags (c) & GridPattern::kFs) != 0 : (GridPattern::flags (c) & GridPattern::kBd) != 0;
                if (! wants) continue;
                const RenderedPtr& a = s.audio[(size_t) (col & 1)] ? s.audio[(size_t) (col & 1)] : s.audio[0];
                if (! a) continue;
                PlayRequest q; q.rendered = a; q.gain = s.level * vol * lv; q.pan = pan; q.semis = semis; q.bus = 2;
                players->play (q);
                if (s.replace) replaced = true;
            }
            if (replaced || ! smp) continue;
            PlayRequest q; q.sample = smp; q.gain = vol * lv; q.pan = pan; q.semis = semis; q.bus = 1;
            players->play (q);
        }
    }

    void playMark (int i, double ppq) noexcept
    {
        const RtState* rt = cur;
        if (rt == nullptr || i < 0 || i >= (int) rt->seq.marks.size()) return;
        const auto& p = rt->seq;
        const auto& m = p.marks[(size_t) i];
        HitContext hc; hc.beats = ppq; hc.bpm = bpm; hc.velocity = m.level; hc.key = rowNote (p, m.row); hc.seed = (uint32_t) i * 977u + (uint32_t) (ppq * 7.0);
        float lv = 0.0f, pan = 0.0f, pitch = 0.0f;
        for (const auto& r : rt->seqRoutes)
        {
            const float x = sourceAtHit (r.src, liveMod, hc) * r.depth;
            if (r.dest == Dest::SeqLevel) lv += x; else if (r.dest == Dest::SeqPan) pan += x; else pitch += 24.0f * x;
        }
        const double dur = std::max (0.01, (markPos (p, m.start + m.length) - markPos (p, m.start)) * 60.0 / bpm);
        marks->trigger (m, p, dur, rt->grains, hc.seed, p.level * std::pow (10.0f, 24.0f * lv / 20.0f), pan, pitch + transposeSemis);
    }

    std::unique_ptr<Engine> engine;
    std::unique_ptr<PlayerPool> players;
    std::unique_ptr<MarkSynth> marks;
    std::unique_ptr<RenderService> renderer;
    Sequencer sequencer;
    Pattern pattern;
    std::vector<Ev> events;
    std::vector<SeqEvent> seqEvents;
    std::vector<float> scratchL, scratchR, tapBuf;
    const RtState* cur = nullptr;
    ModState liveMod;      // only the ModParams part is used on the audio thread
    ModFrame frame;
    float sr = 48000.0f, swing = 0.62f, transposeSemis = 0.0f;
    double bpm = 120.0, internalPpq = 0.0, keyPpq = 0.0, blockPpq = 0.0, gridKeyPpq = 0.0;
    int maxN = 512, heldKeys = 0, keySemis = 0, gridKeyPattern = 0, gridKeyNote = -1;
    bool playing = false, wasSeqOn = false, gridKeyOn = false;
    uint32_t hitSeed = 1;
    std::array<uint32_t, 16> rrCounter {};
    std::array<bool, kNumPads> mute {};
    std::array<std::atomic<float>, kNumPads> pendingAudition {};
    std::atomic<int> pendingNote { -1 };
    std::atomic<float> pendingNoteVel { 0.8f };
    struct PendingOff { int note = -1; int samplesLeft = 0; };
    std::array<PendingOff, 128> pendingOffs {};
};
} // namespace nb
