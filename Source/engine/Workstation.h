// NeoBeat 3.0 — the workstation: the real-time side of everything. Framework-free, so the plugin,
// the offline renderer and the tests run exactly the same code.
//
// Each block: the transport (host, standalone run, key mode), MIDI notes, auditions, the sequencer's
// scenes, Layermax and the Matrixxxer's marks become events; every note goes through the key map; a
// hit on a lane plays its voice, or, when a node or an insert has an effect on it, a live effect unit
// (FxUnits.h) fed by that hit. Sound is rendered in segments between events.
//
// Two states are read each block: the live state (LiveState.h: every effect as it is set now, built
// by the editor at once) and the render worker's (RenderService.h: the prepared sound: each lane's own
// hit for the units to work on, and the few effects that cannot run live).
#pragma once

#include "Engine.h"
#include "../shared/JackCore.h"
#include "../shared/Bands.h"
#include "FxUnits.h"
#include "LiveState.h"
#include "MarkSynth.h"
#include "Players.h"
#include "RenderService.h"
#include "Sequencer.h"

namespace nb
{
struct MidiIn { int offset = 0; int type = 0; int note = 60; float velocity = 0.8f; };   // type 0 on, 1 off, 2 all off
struct MidiOut { int offset = 0; bool on = true; int note = 36; int velocity = 100; int channel = 1; };

struct Transport { bool hostPlaying = false; double hostPpq = 0.0; double bpm = 120.0; bool hostHasPpq = false; };

struct BlockParams
{
    int mode = 0;          // 0 host, 1 key, 2 pads
    bool run = false;      // standalone: run the sequencer without a host transport
    float swing = 0.62f;
    std::array<bool, kNumPads> mute {};
    int tapPad = 0;
    int scene = 0;         // the scene to play when the chain is off (a change waits for the bar)
};

class Workstation
{
public:
    static constexpr int kPadNoteBase = 36, kKeyRoot = 60, kFrame = LiveFx::kN, kSegment = 1024;

    Workstation() : engine (std::make_unique<Engine>()), players (std::make_unique<PlayerPool>()), marks (std::make_unique<MarkSynth>()),
                    renderer (std::make_unique<RenderService> (publisher)), units (std::make_unique<FxUnits>()), scenes ((size_t) kNumScenes)
    {
        { bc::BandsState d; bandsDry = d.dsp.dry; d.dsp.dry = 0.0f; bandsFx.setState (d); }
        events.reserve (8192);
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
        units->prepare (sr, kSegment);
        scratchL.assign ((size_t) maxN, 0.0f); scratchR.assign ((size_t) maxN, 0.0f); tapBuf.assign ((size_t) maxN, 0.0f);
        bandsFx.prepare (sr, kSegment);
        for (auto* v : { &bandBusL, &bandBusR, &bandWetL, &bandWetR }) v->assign ((size_t) kSegment, 0.0f);
        bandsOn = false; bandsQuiet = 0;
        heldKeys = 0; internalPpq = 0.0; keyPpq = 0.0;
        for (auto& o : pendingOffs) o = {};
    }

    void startRenderer() { renderer->start (sr); }

    // the audio thread hands over a scene when it changed
    void setScene (int scene, const Pattern& p) noexcept
    {
        if (scene < 0 || scene >= kNumScenes) return;
        scenes[(size_t) scene] = p; sequencer.setScene (scene, p);
    }
    // one pattern in every scene
    void setPattern (const Pattern& p) noexcept { for (int s = 0; s < kNumScenes; ++s) setScene (s, p); }

    // the editor: everything as it is set now (any thread but the audio thread)
    void publishLive (std::unique_ptr<LiveState> s) { livePublisher.publish (std::move (s)); }

    void audition (int pad, float vel) noexcept { if (pad >= 0 && pad < kNumPads) pendingAudition[(size_t) pad].store (vel); }
    void auditionNote (int note, float vel) noexcept { pendingNote.store (note); pendingNoteVel.store (vel); }
    void midiTest (int lane) noexcept { pendingMidiTest.store (lane); }

    Engine& kit() noexcept { return *engine; }
    RenderService& render() noexcept { return *renderer; }
    Sequencer& seq() noexcept { return sequencer; }
    RtPublisher<RtState> publisher;
    RtPublisher<LiveState> livePublisher;

    // ---------------------------------------------------------------- the block
    void process (float* L, float* R, int n, const Transport& tr, const BlockParams& bp,
                  const MidiIn* midi, int numMidi, std::vector<MidiOut>& midiOut, float* tap) noexcept
    {
        if (n <= 0) return;
        const RtState* rt = publisher.beginBlock();
        const LiveState* lv = livePublisher.beginBlock();
        if (n > maxN) { livePublisher.endBlock(); publisher.endBlock(); return; }   // the host broke its promise: stay silent rather than allocate
        cur = rt; live = lv;
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
        if (lv != nullptr) { sequencer.setSong (lv->song); midiCfg = lv->midi; }

        // ---- transport
        const bool keysWereHeld = heldKeys > 0;
        events.clear();
        for (int i = 0; i < numMidi; ++i)
        {
            const auto& m = midi[i];
            const int at = std::max (0, std::min (n - 1, m.offset));
            if (m.type == 2) { heldKeys = 0; engine->allNotesOff(); players->allOff(); marks->allOff(); units->allOff(); gridKeyOn = false; continue; }
            if (bp.mode == 1 && m.note >= kKeyRoot)
            {
                if (m.type == 0) { ++heldKeys; keySemis = m.note - kKeyRoot; }
                else heldKeys = std::max (0, heldKeys - 1);
                continue;
            }
            addEvent ({ at, m.type == 0 ? Ev::NoteOn : Ev::NoteOff, m.note, 0, m.velocity, 0.0f, 0, 0, 0.0, 0 });
        }
        if (bp.mode == 1 && heldKeys > 0 && ! keysWereHeld) keyPpq = 0.0;
        for (int p = 0; p < kNumPads; ++p)
        {
            const float a = pendingAudition[(size_t) p].exchange (0.0f);
            if (a > 0.0f) addEvent ({ 0, Ev::PadDirect, p, 0, a, 0.0f, 0, 0, 0.0, 0 });
        }
        {
            const int note = pendingNote.exchange (-1);
            if (note >= 0) addEvent ({ 0, Ev::NoteOn, note, 0, pendingNoteVel.load(), 0.0f, 0, 0, 0.0, 0 });
            const int lane = pendingMidiTest.exchange (-1);
            if (lane >= 0 && lane < kNumPads) sendNote (lane, 0.8f, 0, 0.0f, midiOut);
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

        // ---- IDM Jack: settings, held or let go, and how hard its offsets apply this block
        {
            nsr::JackSettings js;
            if (jackSettings.read (js)) jack.setSettings (js);
            const bool want = jackHold.load (std::memory_order_relaxed);
            if (want && ! jack.isHeld())
            {
                jack.engage (ppq0);
                jackPendingCount = 0;
                jackBursts.store (0, std::memory_order_relaxed);
                jackSeized.store (0, std::memory_order_relaxed);
            }
            else if (! want && jack.isHeld())
            {
                jack.release (ppq0);
                if (! jack.isRecovering()) jackPendingCount = 0;     // a hard cut: nothing left over
            }
            if (jack.isRecovering() && ! seqOn) { jack.cut(); jackPendingCount = 0; }   // no transport, no recovery
            jackScaleNow = jack.scaleAt (ppq0);
            jackMaskNow = jackMask.load (std::memory_order_relaxed);
            jackReplacing = jack.isHeld() && jack.settings().replace;
            jackState.store (jack.isHeld() ? 1 : (jack.isRecovering() ? 2 : 0), std::memory_order_relaxed);
        }

        // ---- BANDS: the bank's settings, and each lane's send for this block
        {
            bc::BandsState bs;
            if (bandsSettings.read (bs))
            {
                // The lane's own direct level is the Dry (the duck below); the bank adds wet only,
                // so the dry is never counted twice.
                bandsDry = std::max (0.0f, std::min (1.0f, bs.dsp.dry));
                bs.dsp.dry = 0.0f;
                bandsFx.setState (bs);
            }
            const float all = std::max (0.0f, std::min (1.0f, bandsAll.load (std::memory_order_relaxed)));
            bandsAny = false;
            for (int p = 0; p < kNumPads; ++p)
            {
                const float s = std::max (0.0f, std::min (1.0f, std::max (engine->pads[(size_t) p].bands, all)));
                bandSendNow[(size_t) p] = s;
                bandDuckNow[(size_t) p] = 1.0f - s * (1.0f - bandsDry);
                bandsAny = bandsAny || s > 0.0f;
            }
            if (bandsAny) { bandsOn = true; bandsQuiet = 0; }
            bandsPpq0 = ppq0; bandsPps = ppqPerSample;
            bandsBeat.store (ppq0, std::memory_order_relaxed);
            bandsRunning.store (bandsOn, std::memory_order_relaxed);
        }

        // the scene: a change waits for the next bar while the sequencer runs
        {
            const int want = std::max (0, std::min (bp.scene, kNumScenes - 1));
            if (! seqOn) sequencer.setLiveSceneNow (want);
            else
            {
                sequencer.settle (ppq0);
                if (want != sequencer.currentLiveScene() && want != sequencer.queuedScene()) sequencer.setLiveScene (want, std::ceil (ppq0 / 4.0 - 1.0e-9) * 4.0);
                else if (want == sequencer.currentLiveScene() && sequencer.queuedScene() >= 0) sequencer.setLiveScene (want, ppq0);   // changed their mind
            }
        }

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
                addEvent ({ e.offset, Ev::SeqNote, e.pad, e.step, e.velocity, semis, e.sub, 0, e.ppq, e.scene });
            // the hits a frame (or several) from now: their spectral effects start early, so they land on the step
            if (lv != nullptr && lv->anyLatency)
                for (int k = 1; k < kMaxChain; ++k)
                {
                    seqEvents.clear();
                    sequencer.collect (ppq0 + k * kFrame * ppqPerSample, bpm, n, true, swing, semis, seqEvents);
                    for (const auto& e : seqEvents)
                        if (e.sub == 0) addEvent ({ e.offset, Ev::PreHit, e.pad, e.step, e.velocity, semis, e.sub, k * kFrame, e.ppq, e.scene });
                }
            if (lv != nullptr) scheduleFollows (*lv, ppq0, ppq1, ppqPerSample, n);
            if (rt != nullptr) { scheduleGrid (*rt, ppq0, ppq1, ppqPerSample, n); scheduleMarks (*rt, ppq0, ppq1, ppqPerSample, n, semis); }
            const auto w = sequencer.whereAt (ppq0);
            playStep.store (w.step); playScene.store (w.scene); playLink.store (w.link);
        }
        else
        {
            playStep.store (-1); playLink.store (-1); playScene.store (sequencer.currentLiveScene());
            if (wasSeqOn) marks->allOff();
        }
        queuedScene.store (sequencer.queuedScene());
        wasSeqOn = seqOn;
        if (bp.mode == 0 && ! tr.hostPlaying && bp.run) internalPpq += ppqPerSample * n;
        else if (! bp.run) internalPpq = 0.0;
        if (bp.mode == 1 && heldKeys > 0 && ! tr.hostPlaying) keyPpq += ppqPerSample * n;
        transposeSemis = semis;

        // ---- IDM Jack's bursts. Only with a transport: the lattice is musical time.
        if (seqOn && jack.isHeld() && ppqPerSample > 0.0)
        {
            int64_t k = jack.pendingStep();
            // A transport jump - a loop, a locate - re-aims the lattice where the transport now is.
            // Playing on, k is the first step at or after this block's start. Jumped forwards, k is
            // well behind it; jumped back, the step before k is already ahead of it. (A step a hair
            // behind is rounding between hosts' block positions, and still plays.)
            if (jack.beatOfStep (k) < ppq0 - 0.25 || jack.beatOfStep (k - 1) >= ppq0 + 1.0e-6) k = jack.firstStepAtOrAfter (ppq0);
            while (jack.beatOfStep (k) < ppq1)
            {
                nsr::JackFigure f;
                if (jack.planStep (k, jackMaskNow, kNumPads, f))
                {
                    jackBursts.fetch_add (1, std::memory_order_relaxed);
                    const double b0 = jack.beatOfStep (k), len = jack.stepBeats (k);
                    for (int i = 0; i < f.count && jackPendingCount < (int) jackPending.size(); ++i)
                    {
                        const auto& h = f.hits[i];
                        auto& p = jackPending[(size_t) jackPendingCount++];
                        p.at = jackAbs + (int64_t) std::llround ((b0 + (double) h.at * len - ppq0) / ppqPerSample)
                             + (int64_t) ((double) h.timingMs * 0.001 * sr);
                        p.pad = h.pad; p.vel = h.velocity;
                        std::copy (h.off, h.off + nsr::JoCount, p.off);
                    }
                }
                ++k;
            }
            jack.setPendingStep (k);
        }
        jackDueCount = 0;
        for (int i = 0; i < jackPendingCount; )
        {
            const auto& p = jackPending[(size_t) i];
            if (p.at < jackAbs + n)
            {
                if (jackDueCount < (int) jackDue.size())
                {
                    const int off = (int) std::max<int64_t> (0, p.at - jackAbs);
                    std::copy (p.off, p.off + nsr::JoCount, jackDue[(size_t) jackDueCount].begin());
                    addEvent ({ off, Ev::JackHit, p.pad, 0, p.vel, semis, 0, jackDueCount, ppq0 + off * ppqPerSample, 0 });
                    ++jackDueCount;
                }
                jackPending[(size_t) i] = jackPending[(size_t) --jackPendingCount];
            }
            else ++i;
        }

        std::stable_sort (events.begin(), events.end(), [] (const Ev& a, const Ev& b) { return a.offset < b.offset; });

        // controls that moved reach the effects that are sounding
        if (lv != nullptr && units->activeCount() > 0)
            units->refresh ([this, lv] (uint32_t id, const HitContext& hit, LiveSpec& out) { return specFor (*lv, id, hit, out); });

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
            if (o.samplesLeft < n) { if (midiOut.size() < midiOut.capacity()) midiOut.push_back ({ std::max (0, o.samplesLeft), false, o.note, 0, o.channel }); o.note = -1; }
            else o.samplesLeft -= n;
        }
        // the limiter is in while Jack works and for a second after, so a release under a ringing
        // tail is not left to the knee
        if (jack.active()) jackTail = (int) sr;
        else jackTail = std::max (0, jackTail - n);
        engine->limiting = jack.active() || jackTail > 0;
        jackAbs += n;

        cur = nullptr; live = nullptr;
        livePublisher.endBlock();
        publisher.endBlock();
    }

    // ---- IDM Jack. Spec: docs/PROPOSED-FEATURES.md, "IDM Jack" - the user's.
    //
    // The message thread writes these; the audio thread reads them at the top of each block. Jack
    // never writes a stored parameter: its offsets go onto the copy of a hit's settings that hitPad
    // already makes, so releasing it is the return to the original by construction.
    nsr::TripleBuffer<nsr::JackSettings> jackSettings;
    std::atomic<bool> jackHold { false };
    std::atomic<uint32_t> jackMask { 0xffffu };        // which lanes it may grab: bit p is lane p
    std::atomic<int> jackState { 0 };                  // 0 idle, 1 held, 2 recovering - for the UI
    std::atomic<int> jackBursts { 0 };                 // bursts fired since it was engaged
    std::atomic<int> jackSeized { 0 };                 // hits that carried offsets since it was engaged

    // ---- BANDS, the spectral delay on a send. Decisions: docs/DECISIONS.md, "BANDS in NeoBeat"
    // and "Next in the same build". The window writes the bank's settings; each lane's send is its
    // PadSettings::bands, and `bandsAll` sends every lane at least that much.
    nsr::TripleBuffer<bc::BandsState> bandsSettings;
    std::atomic<float> bandsAll { 0.0f };
    std::atomic<bool> bandsRunning { false };          // for the window: is the bank processing
    std::atomic<double> bandsBeat { 0.0 };             // for the window: where the bands' travel is

    // UI feedback
    std::atomic<int> playStep { -1 }, gridCol { -1 }, playScene { 0 }, queuedScene { -1 }, playLink { -1 };
    std::atomic<double> seqBeat { -1.0 };
    std::array<std::atomic<uint32_t>, kNumPads> cellHits {};
    int activePlayers() const noexcept { return players->activeCount(); }
    int activeMarks() const noexcept { return marks->activeCount(); }
    int activeUnits() const noexcept { return units->activeCount(); }
    uint32_t lateHits() const noexcept { return units->lateHits.load (std::memory_order_relaxed); }           // played by hand
    uint32_t lateSeqHits() const noexcept { return units->lateSeqHits.load (std::memory_order_relaxed); }     // the sequencer's (only at a start or a jump)
    float masterBase = 0.35f;

private:
    struct Ev
    {
        enum Kind : uint8_t { NoteOn, NoteOff, SeqNote, PadDirect, GridCol, Mark, PreHit, Follow, JackHit };
        int offset; Kind kind; int a; int b; float vel; float semis; int sub; int c; double ppq; int scene;
    };
    void addEvent (const Ev& e) noexcept { if (events.size() < events.capacity()) events.push_back (e); }

    // The bank on this segment's bus, into the mix ahead of the master. With no send left it keeps
    // running until its tail has died - half a second of silence - then stops costing anything.
    void renderBands (int start, int m) noexcept
    {
        std::fill (bandWetL.begin(), bandWetL.begin() + m, 0.0f);
        std::fill (bandWetR.begin(), bandWetR.begin() + m, 0.0f);
        bandsFx.process (bandWetL.data(), bandWetR.data(), bandBusL.data(), bandBusR.data(), m, bandsPpq0 + start * bandsPps);
        float pk = 0.0f;
        for (int i = 0; i < m; ++i)
        {
            scratchL[(size_t) i] += bandWetL[(size_t) i]; scratchR[(size_t) i] += bandWetR[(size_t) i];
            pk = std::max (pk, std::max (std::fabs (bandWetL[(size_t) i]), std::fabs (bandWetR[(size_t) i])));
        }
        if (bandsAny || pk > 1.0e-6f) bandsQuiet = 0;
        else if ((bandsQuiet += m) > (int) (0.5f * sr)) { bandsOn = false; bandsFx.reset(); engine->band = {}; }
    }

    void renderSegment (float* L, float* R, int start, int len, int tapPad) noexcept
    {
        while (len > 0)
        {
            const int m = std::min (len, kSegment);
            std::fill (scratchL.begin(), scratchL.begin() + m, 0.0f);
            std::fill (scratchR.begin(), scratchR.begin() + m, 0.0f);
            if (bandsOn)
            {
                std::fill (bandBusL.begin(), bandBusL.begin() + m, 0.0f);
                std::fill (bandBusR.begin(), bandBusR.begin() + m, 0.0f);
                engine->band = { bandSendNow.data(), bandDuckNow.data(), bandBusL.data(), bandBusR.data(), kNumPads };
            }
            else engine->band = {};
            players->render (scratchL.data(), scratchR.data(), m, tapBuf.data() + start, tapPad, engine->band);
            marks->render (scratchL.data(), scratchR.data(), m);
            units->begin (*engine, m);
            engine->renderVoices (m, tapBuf.data() + start, tapPad);
            units->render (*engine, scratchL.data(), scratchR.data(), m, tapBuf.data() + start, tapPad);
            if (bandsOn) renderBands (start, m);
            engine->finishMix (L + start, R + start, m, scratchL.data(), scratchR.data());
            start += m; len -= m;
        }
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
            addEvent ({ off, Ev::GridCol, pi, k, 1.0f, 0.0f, 0, 0, at, 0 });
        }
        const double rel = ppq0 - origin;
        gridCol.store ((int) std::fmod (std::max (0.0, rel) / rb, (double) len));
    }

    // Layermax rows that follow a lane off the beat, or fill its gaps (those on the beat are played with the hit itself)
    void scheduleFollows (const LiveState& lv, double ppq0, double ppq1, double ppqPerSample, int n) noexcept
    {
        if (lv.anyFollowShift)
            for (int lane = 0; lane < kNumPads; ++lane)
                for (size_t fi = 0; fi < lv.follow[(size_t) lane].size(); ++fi)
                {
                    const auto& f = lv.follow[(size_t) lane][fi].f;
                    if (f.mode == 1 || std::fabs (f.offsetMs) <= 0.01f) continue;
                    seqEvents.clear();
                    sequencer.collect (ppq0, bpm, n, true, swing, 0.0f, seqEvents, 1u << lane, (double) f.offsetMs / 1000.0 * bpm / 60.0);
                    for (const auto& e : seqEvents)
                        if (e.sub == 0) addEvent ({ e.offset, Ev::Follow, lane, e.step, e.velocity, 0.0f, 0, (int) fi, e.ppq, e.scene });
                }
        if (lv.anyFollowFill)
        {
            const int g0 = (int) std::floor (ppq0 * 4.0) - 2, g1 = (int) std::ceil (ppq1 * 4.0) + 2;
            for (int g = std::max (0, g0); g <= g1; ++g)
            {
                const double nominal = g * 0.25;
                const auto w = sequencer.whereAt (nominal + 1.0e-6);
                const double beat = nominal + ((w.step & 1) ? 0.5 * ((double) swing - 0.5) : 0.0);
                for (int lane = 0; lane < kNumPads; ++lane)
                    for (size_t fi = 0; fi < lv.follow[(size_t) lane].size(); ++fi)
                    {
                        const auto& f = lv.follow[(size_t) lane][fi].f;
                        if (f.mode != 1) continue;
                        if (scenes[(size_t) w.scene].steps[(size_t) lane][(size_t) w.step].vel != 0) continue;
                        const double at = beat + (double) f.offsetMs / 1000.0 * bpm / 60.0;
                        if (at < ppq0 - 1.0e-9 || at >= ppq1) continue;
                        const int off = std::max (0, std::min (n - 1, (int) std::floor ((at - ppq0) / ppqPerSample)));
                        addEvent ({ off, Ev::Follow, lane, w.step, 0.8f, 0.0f, 0, (int) fi, at, w.scene });
                    }
            }
        }
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
                const auto w = sequencer.whereAt (at + 1.0e-6);
                bool any = false;
                for (int t = 0; t < Pattern::kTracks && ! any; ++t) any = scenes[(size_t) w.scene].steps[(size_t) t][(size_t) w.step].vel > 0;
                if (! any) continue;
            }
            const int off = std::max (0, std::min (n - 1, (int) std::floor ((at - ppq0) / ppqPerSample)));
            addEvent ({ off, Ev::Mark, i, 0, 1.0f, 0.0f, 0, 0, at, 0 });
        }
    }

    // ---------------------------------------------------------------- MIDI out
    void sendNote (int lane, float vel, int offset, float semis, std::vector<MidiOut>& midiOut) noexcept
    {
        if (midiOut.size() + 2 > midiOut.capacity()) return;
        const int note = std::max (0, std::min (127, (int) midiCfg.note[(size_t) lane] + (midiCfg.transpose ? (int) std::lround (semis) : 0)));
        const int channel = std::max (1, std::min (16, (int) midiCfg.channel[(size_t) lane]));
        const int length = std::max ((int) (0.005 * sr), (int) (std::max (0.05f, midiCfg.lengthSteps) * 15.0 / bpm * sr));
        PendingOff* slot = nullptr;
        for (auto& o : pendingOffs)
        {
            if (o.note == note && o.channel == channel) { midiOut.push_back ({ offset, false, note, 0, channel }); slot = &o; break; }   // still held: let go first
            if (o.note < 0 && slot == nullptr) slot = &o;
        }
        midiOut.push_back ({ offset, true, note, std::max (1, std::min (127, (int) std::lround (vel * 127.0f))), channel });
        if (slot != nullptr) { slot->note = note; slot->channel = channel; slot->samplesLeft = offset + length; }
    }

    // ---------------------------------------------------------------- events
    void handle (const Ev& e, std::vector<MidiOut>& midiOut) noexcept
    {
        // replace: while Jack holds, the written pattern does not play - the kit runs on Jack alone.
        // KEY MAP marks are left playing: they are held notes with their own endings, and cutting
        // them here could leave one sounding.
        if (jackReplacing && (e.kind == Ev::SeqNote || e.kind == Ev::PreHit || e.kind == Ev::GridCol || e.kind == Ev::Follow))
            return;
        switch (e.kind)
        {
            case Ev::JackHit:
                if (mute[(size_t) e.a]) break;
                jackHitOff = jackDue[(size_t) e.c].data();
                hitPad (e.a, e.vel, e.semis, 0.0f, 0.0f, false, 0, 0, kPadNoteBase + e.a, -1, e.ppq, 0, -1);
                jackHitOff = nullptr;
                break;
            case Ev::NoteOn: noteOn (e.a, e.vel, 0.0f, false, 0, 0, blockPpq, 0, -1); break;
            case Ev::NoteOff: noteOff (e.a); break;
            case Ev::PadDirect: hitPad (e.a, e.vel, 0.0f, 0.0f, 0.0f, false, 0, 0, kPadNoteBase + e.a, -1, blockPpq, 0, -1); break;
            case Ev::SeqNote:
                // the Layermax rows that play with this lane (whether or not the lane itself is muted)
                if (live != nullptr && e.sub == 0)
                    for (const auto& lf : live->follow[(size_t) e.a])
                        if (lf.f.mode != 1 && std::fabs (lf.f.offsetMs) <= 0.01f) playFollow (lf, e.a, e.b, e.scene, e.vel);
                if (mute[(size_t) e.a]) break;
                if (midiCfg.mode != 2 && (e.sub == 0 || midiCfg.rolls)) sendNote (e.a, e.vel, e.offset, e.semis, midiOut);
                if (midiCfg.mode == 1) { engine->hitCounter[(size_t) e.a].fetch_add (1, std::memory_order_relaxed); break; }   // MIDI only: the lane is silent
                noteOn (kPadNoteBase + e.a, e.vel, e.semis, true, e.b, e.sub, e.ppq, e.scene, -1);
                break;
            case Ev::PreHit:
                if (mute[(size_t) e.a] || midiCfg.mode == 1) break;
                noteOn (kPadNoteBase + e.a, e.vel, e.semis, true, e.b, e.sub, e.ppq, e.scene, e.c);
                break;
            case Ev::Follow:
                if (live != nullptr && (size_t) e.c < live->follow[(size_t) e.a].size()) playFollow (live->follow[(size_t) e.a][(size_t) e.c], e.a, e.b, e.scene, e.vel);
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

    // every note lands in the key map. lead >= 0: not the hit itself but its warning, `lead` samples ahead.
    void noteOn (int note, float vel, float semis, bool fromSeq, int step, int sub, double ppq, int scene, int lead) noexcept
    {
        const RtState* rt = cur;
        if (rt == nullptr || rt->zones.empty())
        {   // no key map yet: the drum map
            if (note >= kPadNoteBase && note < kPadNoteBase + kNumPads) hitPad (note - kPadNoteBase, vel, semis, 0.0f, 0.0f, fromSeq, step, sub, note, -1, ppq, scene, lead);
            return;
        }
        const bool real = lead < 0;
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
                uint32_t& counter = rrCounter[(size_t) (z.rrGroup & 15)];
                const int turn = (int) (counter % (uint32_t) std::max (1, members));
                if (real) ++counter;
                int c = 0;
                for (size_t zj = 0; zj < rt->zones.size(); ++zj)
                {
                    const auto& o = rt->zones[zj].z;
                    if (o.rrGroup == z.rrGroup && note >= o.loKey && note <= o.hiKey && v127 >= o.loVel && v127 <= o.hiVel) { if (c == turn) { pick = (int) zj; break; } ++c; }
                }
                if (rrCount < 16) rrSeen[(size_t) rrCount++] = z.rrGroup;
                if (pick >= 0) playZone (rt->zones[(size_t) pick], pick, note, vel, semis, fromSeq, step, sub, ppq, scene, lead);
                continue;
            }
            playZone (rz, (int) zi, note, vel, semis, fromSeq, step, sub, ppq, scene, lead);
        }
    }

    void playZone (const RtZone& rz, int zi, int note, float vel, float semis, bool fromSeq, int step, int sub, double ppq, int scene, int lead) noexcept
    {
        const auto& z = rz.z;
        const bool real = lead < 0;
        const float key = z.keytrack ? (float) (note - z.root) : 0.0f;
        const LiveSpec* zfx = live != nullptr && (size_t) zi < live->zoneFx.size() && live->zoneFx[(size_t) zi].on() ? &live->zoneFx[(size_t) zi] : nullptr;
        switch (z.src)
        {
            case ZoneSource::Pad:
                if (z.index >= 0 && z.index < kNumPads)
                    hitPad (z.index, vel, semis + z.tune + key, z.gainDb, z.pan, fromSeq, step, sub, note, zi, ppq, scene, lead);
                break;
            case ZoneSource::Sample:
            {
                if (zfx != nullptr && rz.sample)
                {   // the zone's effect, live, on its sample
                    UnitRequest rq; rq.sample = rz.sample; rq.semis = semis + z.tune + key;
                    addFx (rq, *zfx, zoneFxId (zi), false);
                    rq.gain = dbToGain (z.gainDb) * vel; rq.pan = z.pan; rq.choke = z.choke;
                    rq.bpm = bpm; rq.hitSeconds = ppq * 60.0 / bpm; rq.hit.beats = ppq; rq.hit.bpm = bpm; rq.hit.velocity = vel; rq.hit.key = note;
                    rq.signature = signatureOf (0x5000 + zi, scene, step, sub); rq.byHand = ! fromSeq;
                    if (spawn (rq, lead, nullptr, 0, 0.0f, 0.0f, nullptr)) { if (real && z.choke > 0) { engine->chokeGroup (z.choke); players->chokeGroup (z.choke); } break; }
                }
                if (! real) break;
                PlayRequest r;
                if (z.fx.kind != CellFx::None && rz.fxAudio) r.rendered = rz.fxAudio; else r.sample = rz.sample;
                r.gain = dbToGain (z.gainDb) * vel; r.pan = z.pan; r.semis = semis + z.tune + key; r.choke = z.choke;
                if (z.choke > 0) { engine->chokeGroup (z.choke); units->chokeGroup (z.choke); }
                players->play (r);
                break;
            }
            case ZoneSource::Kit:
            {
                if (! real || cur == nullptr || cur->kitPads.empty()) break;
                const int k = std::max (0, std::min (z.index, (int) cur->kitPads.size() - 1));
                const int pad = ((note - z.loKey) % kNumPads + kNumPads) % kNumPads;
                PadSettings ps = cur->kitPads[(size_t) k][(size_t) pad];
                ps.levelDb += z.gainDb; ps.pan = std::max (-1.0f, std::min (1.0f, ps.pan + z.pan));
                if (ps.choke > 0) { players->chokeGroup (ps.choke); units->chokeGroup (ps.choke); }
                engine->triggerWith (pad, ps, vel, semis + z.tune, nullptr, nullptr, 0.0f);
                break;
            }
            case ZoneSource::OneShot:
            {
                if (! real || cur == nullptr || z.index < 0 || z.index >= (int) cur->cells.size()) break;
                const auto& c = cur->cells[(size_t) z.index];
                if (c.audio)
                {
                    PlayRequest r; r.rendered = c.audio; r.gain = dbToGain (z.gainDb) * vel / 0.8f; r.pan = z.pan; r.semis = semis + z.tune + key; r.pad = c.pad;
                    players->play (r);
                }
                else   // a live node, played from a key: its lane's hit with that node on it
                    hitPad (c.pad, vel, semis + z.tune + key, z.gainDb, z.pan, true, c.step, 0, note, -1, ppq, c.scene, -1);
                break;
            }
            case ZoneSource::GridPattern:
                if (! real) break;
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

    // ---------------------------------------------------------------- live effects
    static void addFx (UnitRequest& rq, const LiveSpec& s, uint32_t id, bool moved) noexcept
    {
        if (! s.on() || rq.numFx >= kMaxChain) return;
        rq.fx[(size_t) rq.numFx] = s; rq.fxId[(size_t) rq.numFx] = id; rq.moved[(size_t) rq.numFx] = moved; ++rq.numFx;
    }
    static uint64_t signatureOf (int who, int scene, int step, int sub) noexcept
    {
        return ((uint64_t) (uint32_t) who << 32) | ((uint64_t) (uint32_t) scene << 24) | ((uint64_t) (uint32_t) step << 8) | (uint64_t) (uint32_t) (sub & 255);
    }
    // a node's effect for this hit: its height moved by its modulation
    LiveSpec nodeSpec (const LiveNode& n, const HitContext& hc, bool& moved) const noexcept
    {
        moved = false;
        if (n.routes.empty()) return n.spec;
        float h = n.height;
        for (const auto& r : n.routes) h += r.depth * sourceAtHit (r.src, liveMod, hc);
        LiveSpec s = n.spec;
        n.main.apply (s, std::max (0.0f, std::min (1.0f, h)));
        moved = true;
        return s;
    }
    LiveSpec bounceSpec (const LiveBounce& b, const HitContext& hc, bool& moved) const noexcept
    {
        moved = false;
        if (b.routes.empty()) return b.spec;
        LiveSpec s = b.spec;
        for (const auto& r : b.routes) applyBounceMod (s.bounce, r.param, r.depth * sourceAtHit (r.src, liveMod, hc));
        moved = true;
        return s;
    }
    // whoever's effect `id` is, as it is set now (for the units that are sounding)
    bool specFor (const LiveState& lv, uint32_t id, const HitContext& hit, LiveSpec& out) const noexcept
    {
        HitContext hc = hit; hc.beats = blockPpq; hc.bpm = bpm;      // song-wide sources keep moving through the hit
        bool moved = false;
        if (id >= 0x10000u)
        {
            const uint32_t key = id - 0x10000u;
            const LiveNode* n = lv.nodeAt ((int) (key >> 13), (int) ((key >> 9) & 15), (int) (key & 511));
            if (n == nullptr || n->prepared || ! n->spec.on()) return false;
            out = nodeSpec (*n, hc, moved); return true;
        }
        if (id >= 0x1000u)
        {
            const size_t zi = id - 0x1000u;
            if (zi >= lv.zoneFx.size() || ! lv.zoneFx[zi].on()) return false;
            out = lv.zoneFx[zi]; return true;
        }
        if (id >= 0x200u)
        {
            const size_t si = id - 0x200u;
            if (si >= lv.bounce.size() || ! lv.bounce[si].on) return false;
            out = bounceSpec (lv.bounce[si], hc, moved); return true;
        }
        if (id >= 0x100u)
        {
            const int pad = (int) (id - 0x100u) / 4, k = (int) (id - 0x100u) % 4;
            if (pad >= kNumPads || k >= kNumInserts || lv.insertMode[(size_t) pad] != 1 || ! lv.inserts[(size_t) pad][(size_t) k].on()) return false;
            out = lv.inserts[(size_t) pad][(size_t) k]; return true;
        }
        return false;
    }

    // Start (or, for the hit itself, take over) a unit. No prepared copy of the hit: the lane's own voice feeds it.
    bool spawn (UnitRequest& rq, int lead, const PadSettings* ps, int pad, float vel, float semis, const VoiceMod* vm) noexcept
    {
        if (rq.numFx == 0) return false;
        const bool real = lead < 0;
        const bool voice = ! rq.rendered && ! rq.sample;
        if (voice && ps == nullptr) return false;
        const int lat = rq.latency();
        auto routeVoice = [&] (int ui)
        {
            PadSettings neutral = *ps; neutral.levelDb = 0.0f; neutral.pan = 0.0f;
            engine->triggerRouted (ui, pad, neutral, vel, semis, vm);
        };
        if (! real)
        {
            if (lat != lead || lat <= 0) return true;                 // another warning's business, or no need of one
            UnitRequest pre = rq; if (voice) pre.mod = VoiceMod();
            const int ui = units->start (pre, true);
            if (ui >= 0 && voice) routeVoice (ui);
            return true;
        }
        if (voice) rq.mod = VoiceMod();                               // the voice carries its own modulation
        if (lat > 0 && units->claim (rq) >= 0) return true;
        const int ui = units->start (rq, false);
        if (ui < 0) return false;
        if (voice) routeVoice (ui);
        return true;
    }

    void hitPad (int pad, float vel, float semis, float gainDb, float panAdd, bool fromSeq, int step, int sub, int key,
                 int zoneIndex, double ppq, int scene, int lead) noexcept
    {
        if (pad < 0 || pad >= kNumPads || vel <= 0.0f) return;
        const bool real = lead < 0;
        const RtState* rt = cur;
        const LiveState* lv = live;
        PadSettings ps = engine->pads[(size_t) pad];
        ps.levelDb += gainDb; ps.pan = std::max (-1.0f, std::min (1.0f, ps.pan + panAdd));
        // IDM Jack: this hit's own offsets, on this copy only. A burst hit brings the offsets it was
        // planned with; any other hit of an included lane gets fresh ones. A pattern hit's are keyed
        // on where it falls, so the same pass plays the same way twice and the copy an effect
        // prepares ahead gets the same offsets as the hit. A hit played by hand has no position to
        // key on - every one arrives at the block's - so it is keyed on the hit count as well, or
        // each key press on a lane would get the same offsets.
        if (jackScaleNow > 0.0f && ((jackMaskNow >> pad) & 1u))
        {
            float own[nsr::JoCount];
            const float* o = jackHitOff;
            float s = 1.0f;
            if (o == nullptr)
            {
                const uint64_t where = (uint64_t) std::llround (ppq * 960.0) * 31u + (uint64_t) pad;
                jack.hitOffsets (fromSeq ? where : where ^ ((uint64_t) (hitSeed + 1u) << 32), own);
                o = own; s = jackScaleNow;
            }
            applyJack (ps, semis, o, s);
            if (real) jackSeized.fetch_add (1, std::memory_order_relaxed);
        }
        // a node on this hit
        const LiveNode* node = lv != nullptr && fromSeq && sub == 0 && lv->oneShotOn ? lv->nodeAt (scene, pad, step) : nullptr;
        int prepared = -1;                                            // a node made ahead (Friction Stretch)
        if (node != nullptr && node->prepared) { prepared = rt != nullptr ? rt->cellAt (scene, pad, step) : -1; if (prepared >= 0 && ! rt->cells[(size_t) prepared].audio) prepared = -1; }
        HitContext hc; hc.beats = ppq; hc.bpm = bpm; hc.velocity = vel; hc.key = key; hc.seed = real ? ++hitSeed : hitSeed + 1;
        hc.height = node != nullptr ? node->height : 0.5f;
        hc.step = (float) step / (float) std::max (1, sequencer.sceneLength (scene));
        VoiceMod vm; float tuneMod = 0.0f;
        padMods (pad, hc, ps, vm, tuneMod);
        const PadAlgo* algo = nullptr; float algoOffset = 0.0f;
        if (rt != nullptr && rt->mod.algos[(size_t) pad].type > 0)
        {
            algo = &rt->mod.algos[(size_t) pad];
            algoOffset = algo->srcDepth * sourceAtHit (algo->src, liveMod, hc);
        }
        if (real && ps.choke > 0) { players->chokeGroup (ps.choke); units->chokeGroup (ps.choke); }
        const float level = dbToGain (ps.levelDb);
        const float pitchHz = engine->lib().sounds.empty() ? 200.0f : engine->lib().sounds[(size_t) std::max (0, std::min (ps.sound, (int) engine->lib().sounds.size() - 1))].pitchHz;
        auto request = [&] (const RenderedPtr& r, float gain)
        {
            PlayRequest q; q.rendered = r; q.gain = gain; q.pan = ps.pan; q.semis = semis + tuneMod; q.pad = pad; q.choke = ps.choke;
            q.algo = algo; q.algoOffset = algoOffset; q.pitchHz = pitchHz; q.mod = vm;
            return q;
        };
        // the lane's own hit, prepared at three strengths (none yet: the voice itself feeds the effects)
        static constexpr float kLayers[3] = { 0.4f, 0.7f, 1.0f };
        const int layer = vel < 0.55f ? 0 : (vel < 0.85f ? 1 : 2);
        const RenderedPtr* dry = rt != nullptr && rt->padDry[(size_t) pad][(size_t) layer] && rt->padDry[(size_t) pad][(size_t) layer]->size() > 1
                               ? &rt->padDry[(size_t) pad][(size_t) layer] : nullptr;
        const int insertMode = lv != nullptr ? lv->insertMode[(size_t) pad] : 0;
        const RenderedPtr* madeInserts = insertMode == 2 && rt != nullptr && rt->insertAudio[(size_t) pad][(size_t) layer] ? &rt->insertAudio[(size_t) pad][(size_t) layer] : nullptr;
        // a request for a unit on this lane's hit: source, then the caller adds the chain
        auto unitFor = [&] (int who)
        {
            UnitRequest rq;
            if (dry != nullptr) { rq.rendered = *dry; rq.gain = level * vel / kLayers[layer]; rq.mod = vm; }
            else rq.gain = level;
            rq.semis = dry != nullptr ? semis + tuneMod : 0.0f;
            rq.pan = ps.pan; rq.pad = pad; rq.choke = ps.choke; rq.algo = algo; rq.algoOffset = algoOffset; rq.pitchHz = pitchHz;
            rq.hit = hc; rq.bpm = bpm; rq.hitSeconds = ppq * 60.0 / bpm;
            rq.signature = signatureOf (who * 16 + pad, scene, step, sub);
            rq.byHand = ! fromSeq;
            return rq;
        };
        auto addInserts = [&] (UnitRequest& rq)
        {
            if (insertMode == 1) for (int k = 0; k < kNumInserts; ++k) addFx (rq, lv->inserts[(size_t) pad][(size_t) k], insertFxId (pad, k), false);
        };
        const int stepSamples = (int) (15.0 / bpm * sr);
        auto addNode = [&] (UnitRequest& rq)
        {
            bool moved = false;
            const LiveSpec s = nodeSpec (*node, hc, moved);
            addFx (rq, s, nodeFxId (node->key), moved);
            addInserts (rq);
            rq.gateAfter = rq.numFx - 1;
            const int own = dry != nullptr ? (*dry)->size() : (int) (0.25f * sr);
            rq.gateSamples = std::max (node->span * stepSamples, std::min (own, (int) (0.25f * sr)));
        };
        const float tune = semis + tuneMod;
        const VoiceMod* vmp = vm.state != nullptr ? &vm : nullptr;

        bool replaced = false;
        // the windows that listen to this lane (or to this node)
        if (real && rt != nullptr)
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
            if (node != nullptr && node->cellIndex < (int) rt->cellSlots.size()) playSlots (rt->cellSlots[(size_t) node->cellIndex]);
        }
        if (lv != nullptr)
        {
            auto bounceOn = [&] (int si, bool onNode)
            {
                const LiveBounce& b = lv->bounce[(size_t) si];
                bool moved = false;
                const LiveSpec bs = bounceSpec (b, hc, moved);
                UnitRequest rq = unitFor (2 + si);
                if (onNode && prepared >= 0) { rq.rendered = rt->cells[(size_t) prepared].audio; rq.gain = level; rq.semis = tune; rq.mod = vm; }
                else if (onNode && ! node->prepared) addNode (rq);
                else if (madeInserts != nullptr) { rq.rendered = *madeInserts; rq.gain = level * vel / kLayers[layer]; rq.semis = tune; rq.mod = vm; }
                else addInserts (rq);
                addFx (rq, bs, bounceFxId (si), moved);
                rq.gain *= b.level; rq.choke = 0; rq.algo = nullptr;
                if (spawn (rq, lead, &ps, pad, vel, tune, vmp) && b.replace) replaced = true;
            };
            for (int si : lv->padBounce[(size_t) pad]) bounceOn (si, false);
            if (node != nullptr)
                for (int si = 0; si < kNumSlots; ++si)
                    if (lv->bounce[(size_t) si].on && lv->bounce[(size_t) si].kind == SourceKind::Cell && lv->bounce[(size_t) si].cellKey == node->key) bounceOn (si, true);
        }
        auto counted = [&] (bool cell)
        {
            if (! real) return;
            engine->chokeGroup (ps.choke);
            engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
            if (cell) cellHits[(size_t) pad].fetch_add (1, std::memory_order_relaxed);
        };
        if (replaced) { if (real) engine->hitCounter[(size_t) pad].fetch_add (1, std::memory_order_relaxed); return; }

        // the node on this hit
        if (node != nullptr)
        {
            if (prepared >= 0)
            {
                if (real) { players->play (request (rt->cells[(size_t) prepared].audio, level)); counted (true); }
                return;
            }
            if (! node->prepared)
            {
                UnitRequest rq = unitFor (0);
                addNode (rq);
                const bool voice = ! rq.rendered;
                if (spawn (rq, lead, &ps, pad, vel, tune, vmp)) { if (real && ! voice) counted (true); else if (real) cellHits[(size_t) pad].fetch_add (1, std::memory_order_relaxed); return; }
            }
        }
        // a zone's effect
        if (zoneIndex >= 0 && lv != nullptr && (size_t) zoneIndex < lv->zoneFx.size())
        {
            if (lv->zoneFx[(size_t) zoneIndex].on())
            {
                UnitRequest rq = unitFor (1);
                addFx (rq, lv->zoneFx[(size_t) zoneIndex], zoneFxId (zoneIndex), false);
                const bool voice = ! rq.rendered;
                if (spawn (rq, lead, &ps, pad, vel, tune, vmp)) { if (real && ! voice) counted (false); return; }
            }
            else if (rt != nullptr && (size_t) zoneIndex < rt->zones.size() && rt->zones[(size_t) zoneIndex].fxAudio)
            {
                if (real) { players->play (request (rt->zones[(size_t) zoneIndex].fxAudio, level * vel / 0.8f)); counted (false); }
                return;
            }
        }
        // the lane's inserts
        if (insertMode == 1)
        {
            UnitRequest rq = unitFor (0);
            addInserts (rq);
            const bool voice = ! rq.rendered;
            if (spawn (rq, lead, &ps, pad, vel, tune, vmp)) { if (real && ! voice) counted (false); return; }
        }
        else if (madeInserts != nullptr)
        {
            if (real) { players->play (request (*madeInserts, level * vel / kLayers[layer])); counted (false); }
            return;
        }
        if (real) engine->triggerWith (pad, ps, vel, tune, vmp, algo, algoOffset);
    }

    // a Layermax row playing along with a lane
    void playFollow (const LiveFollow& lf, int lane, int step, int scene, float vel) noexcept
    {
        const RtState* rt = cur;
        if (rt == nullptr || rt->gridSamples.empty()) return;
        const auto& f = lf.f;
        if (f.mode == 2)
        {   // every Nth hit of the lane, counted from the scene's start
            int ordinal = 0;
            const auto& row = scenes[(size_t) std::max (0, std::min (scene, kNumScenes - 1))].steps[(size_t) lane];
            for (int s = 0; s < step && s < Pattern::kMaxSteps; ++s) ordinal += row[(size_t) s].vel > 0 ? 1 : 0;
            if (ordinal % std::max (1, f.nth) != 0) return;
        }
        const auto& smp = rt->gridSamples[(size_t) (lf.row % (int) rt->gridSamples.size())];
        if (! smp) return;
        PlayRequest q; q.sample = smp; q.gain = f.level * (f.velocity ? vel / 0.8f : 1.0f) * rt->gridLevel; q.pan = f.pan; q.semis = f.semis + transposeSemis; q.bus = 1;
        players->play (q);
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
            if ((GridPattern::flags (c) & GridPattern::kFs) != 0)
                for (int idx : rt->rowSlots[(size_t) row])
                {
                    const auto& s = rt->slots[(size_t) idx];
                    if (! s.fs) continue;
                    const RenderedPtr& a = s.audio[(size_t) (col & 1)] ? s.audio[(size_t) (col & 1)] : s.audio[0];
                    if (! a) continue;
                    PlayRequest q; q.rendered = a; q.gain = s.level * vol * lv; q.pan = pan; q.semis = semis; q.bus = 2;
                    players->play (q);
                    if (s.replace) replaced = true;
                }
            if ((GridPattern::flags (c) & GridPattern::kBd) != 0 && live != nullptr && smp)
                for (int si : live->rowBounce[(size_t) row])
                {   // the row's Bounce Delay window, live
                    const LiveBounce& b = live->bounce[(size_t) si];
                    bool moved = false;
                    UnitRequest rq; rq.sample = smp; rq.semis = semis;
                    addFx (rq, bounceSpec (b, hc, moved), bounceFxId (si), moved);
                    rq.gain = b.level * vol * lv; rq.pan = pan;
                    rq.hit = hc; rq.bpm = bpm; rq.hitSeconds = ppq * 60.0 / bpm;
                    rq.signature = signatureOf (0x6000 + row, pi, col, si);
                    if (spawn (rq, -1, nullptr, 0, 0.0f, 0.0f, nullptr) && b.replace) replaced = true;
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
    std::unique_ptr<FxUnits> units;
    Sequencer sequencer;
    std::vector<Pattern> scenes;
    std::vector<Ev> events;
    std::vector<SeqEvent> seqEvents;
    std::vector<float> scratchL, scratchR, tapBuf;
    const RtState* cur = nullptr;
    const LiveState* live = nullptr;
    MidiSettings midiCfg;
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
    std::atomic<int> pendingNote { -1 }, pendingMidiTest { -1 };
    std::atomic<float> pendingNoteVel { 0.8f };
    struct PendingOff { int note = -1; int channel = 1; int samplesLeft = 0; };
    std::array<PendingOff, 128> pendingOffs {};

    // ---- BANDS, audio thread only
    bc::BandsEngine bandsFx;
    std::array<float, kNumPads> bandSendNow {}, bandDuckNow {};
    std::vector<float> bandBusL, bandBusR, bandWetL, bandWetR;
    float bandsDry = 0.4f;
    bool bandsOn = false, bandsAny = false;      // on: processing (a send, or a tail still ringing)
    int bandsQuiet = 0;                          // samples with no send and nothing coming out
    double bandsPpq0 = 0.0, bandsPps = 0.0;      // the block's start and pace, for the bands' travel

    // ---- IDM Jack, audio thread only
    nsr::JackCore jack;
    // A burst subdivides a lattice step, and a step is far longer than a block, so its hits are
    // planned when the step is reached and held here until their sample comes round.
    struct JackPending { int64_t at = 0; int pad = 0; float vel = 0.0f; float off[nsr::JoCount] {}; };
    std::array<JackPending, 256> jackPending {};
    int jackPendingCount = 0;
    std::array<std::array<float, nsr::JoCount>, 64> jackDue {};    // this block's burst hits, by event
    int jackDueCount = 0;
    int64_t jackAbs = 0;                 // samples since the start: the clock the pending hits wait on
    int jackTail = 0;                    // the limiter stays on this long after Jack stops
    float jackScaleNow = 0.0f;
    uint32_t jackMaskNow = 0;
    bool jackReplacing = false;
    const float* jackHitOff = nullptr;   // set while a burst hit is being played

    // This hit's offsets, onto this copy of its settings. Pitch goes into the hit's transpose and
    // level and pan into the copy, so they reach every path a hit can take, prepared copies
    // included. Decay, tone, drive, hollow, evolve and width shape the synthesis itself, so they
    // reach a live voice only: a lane whose effects are fed a copy prepared in advance hears pitch,
    // pan and level from Jack and not the rest.
    static void applyJack (PadSettings& ps, float& semis, const float* o, float s) noexcept
    {
        auto cl = [] (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
        semis     += o[nsr::JoTune] * 12.0f * s;
        ps.decay   = cl (ps.decay * std::pow (2.0f, o[nsr::JoDecay] * 1.5f * s), 0.1f, 8.0f);
        ps.tone    = cl (ps.tone + o[nsr::JoTone] * s, -1.0f, 1.0f);
        ps.drive   = cl (ps.drive + o[nsr::JoDrive] * s, 0.0f, 1.0f);
        ps.hollow  = cl (ps.hollow + o[nsr::JoHollow] * s, 0.0f, 1.0f);
        ps.evolve  = cl (ps.evolve + o[nsr::JoEvolve] * s, 0.0f, 1.0f);
        ps.pan     = cl (ps.pan + o[nsr::JoPan] * s, -1.0f, 1.0f);
        ps.width   = cl (ps.width + o[nsr::JoWidth] * s, 0.0f, 2.0f);
        ps.levelDb += o[nsr::JoLevel] * 6.0f * s;
    }
};
} // namespace nb
