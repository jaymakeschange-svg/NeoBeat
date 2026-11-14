// NeoBeat 3.0 — export: the sequencer's pattern as sound and as MIDI. Framework-free: the sound is
// the Workstation itself run offline (so the file is what you hear, effects and all); the MIDI file is
// written by hand (a standard MIDI file, type 1, one track per lane).
#pragma once

#include "Workstation.h"
#include <functional>

namespace nb
{
struct ExportRange
{
    int mode = 0;            // 0 one scene, once through; 1 the chain, once through; 2 bars of a scene
    int scene = 0;
    int barFrom = 0, barTo = 1;      // mode 2: [from, to)
    double tailSeconds = 12.0;       // let effects ring out (cut as soon as it is silent)
};

inline double exportBeats (const RenderInputs& in, const ExportRange& r, double& startBeat)
{
    startBeat = 0.0;
    const int sc = std::max (0, std::min (r.scene, kNumScenes - 1));
    if (r.mode == 1 && in.project.song.chainOn && in.project.song.numLinks > 0)
    {
        double t = 0.0;
        for (int i = 0; i < in.project.song.numLinks; ++i)
        {
            const auto& l = in.project.song.chain[(size_t) i];
            t += 0.25 * std::max (1, in.scenes[(size_t) std::min ((int) l.scene, kNumScenes - 1)].length) * std::max (1, (int) l.repeats);
        }
        return t;
    }
    const double scene = 0.25 * std::max (1, in.scenes[(size_t) sc].length);
    if (r.mode == 2)
    {
        const int a = std::max (0, std::min (r.barFrom, r.barTo)), b = std::max (a + 1, std::max (r.barFrom, r.barTo));
        startBeat = 4.0 * a;
        return std::min (scene, 4.0 * b) - std::min (scene - 0.25, startBeat);
    }
    return scene;
}

// The sound. onlyLane >= 0: that lane alone (a stem); onlyLane == -2: everything but the lanes
// (Layermax and the Draw view). progress (optional) is called with 0..1 and may return false to stop.
inline Rendered renderSong (const RenderInputs& input, float sr, const ExportRange& range, float masterDb = 0.0f, int onlyLane = -1,
                            const std::function<bool (float)>& progress = {})
{
    RenderInputs in = input;
    double startBeat = 0.0;
    const double beats = exportBeats (in, range, startBeat);
    if (range.mode != 1) in.project.song.chainOn = false;
    if (onlyLane >= 0) { in.project.grid.play = false; in.project.matrix.seqOn = false; for (auto& f : in.project.grid.follow) f.on = false; }
    auto ws = std::make_unique<Workstation>();
    constexpr int B = 512;
    ws->prepare (sr, B);
    for (int p = 0; p < kNumPads; ++p) ws->kit().pads[(size_t) p] = in.pads[(size_t) p];
    for (int s = 0; s < kNumScenes; ++s) ws->setScene (s, in.scenes[(size_t) s]);
    ws->publishLive (buildLiveState (in.project));
    ws->render().renderNow (in, sr);
    ws->masterBase = 0.35f * std::pow (10.0f, masterDb / 20.0f);

    const double bpm = std::max (20.0, in.bpm);
    const int body = (int) std::ceil (beats * 60.0 / bpm * sr);
    const int maxTail = (int) (std::max (0.0, range.tailSeconds) * sr);
    Rendered out; out.sampleRate = sr;
    out.l.assign ((size_t) (body + maxTail), 0.0f); out.r.assign ((size_t) (body + maxTail), 0.0f);
    BlockParams bp; bp.swing = in.swing; bp.scene = std::max (0, std::min (range.scene, kNumScenes - 1));
    if (onlyLane >= 0) for (int p = 0; p < kNumPads; ++p) bp.mute[(size_t) p] = p != onlyLane;
    if (onlyLane == -2) bp.mute.fill (true);
    std::vector<MidiOut> mo; mo.reserve (1024);
    // one silent block first, so the scene is the one asked for when the song starts
    { Transport tr; tr.bpm = bpm; std::vector<float> zl ((size_t) B), zr ((size_t) B); ws->process (zl.data(), zr.data(), B, tr, bp, nullptr, 0, mo, nullptr); }
    int pos = 0, quiet = 0;
    const double ppqPerSample = bpm / 60.0 / sr;
    while (pos < body + maxTail)
    {
        const bool inBody = pos < body;
        const int n = std::min (B, (inBody ? body : body + maxTail) - pos);
        Transport tr; tr.bpm = bpm; tr.hostPlaying = inBody; tr.hostPpq = startBeat + pos * ppqPerSample; tr.hostHasPpq = true;
        mo.clear();
        ws->process (out.l.data() + pos, out.r.data() + pos, n, tr, bp, nullptr, 0, mo, nullptr);
        if (! inBody)
        {
            float pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (out.l[(size_t) (pos + i)]), std::fabs (out.r[(size_t) (pos + i)])));
            quiet = pk < 1.0e-4f ? quiet + n : 0;
            if (quiet > (int) (0.25f * sr) && ws->activeUnits() == 0) { pos += n; break; }
        }
        pos += n;
        if (progress && ! progress ((float) pos / (float) (body + maxTail / 4))) { out.l.clear(); out.r.clear(); return out; }
    }
    int end = std::max (body, pos);
    while (end > body && std::fabs (out.l[(size_t) end - 1]) < 5.0e-5f && std::fabs (out.r[(size_t) end - 1]) < 5.0e-5f) --end;
    out.l.resize ((size_t) end); out.r.resize ((size_t) end);
    return out;
}

// ------------------------------------------------------------------ MIDI
struct SongNote { double beat = 0.0, length = 0.125; int note = 36, velocity = 100, channel = 1, lane = 0; };

// The notes of the range. swing < 0: straight 16ths (for a host that applies its own groove).
inline std::vector<SongNote> songNotes (const RenderInputs& in, const ExportRange& range, float swing)
{
    std::vector<SongNote> notes;
    const auto& midi = in.project.midi;
    Sequencer seq;
    for (int s = 0; s < kNumScenes; ++s) seq.setScene (s, in.scenes[(size_t) s]);
    const float sw = swing < 0.0f ? 0.5f : swing;
    auto pass = [&] (int scene, double origin, double from, double to)
    {
        seq.forEachHit (scene, sw, [&] (const Sequencer::Hit& h)
        {
            if (h.sub > 0 && ! midi.rolls) return;
            if (h.beats < from - 1.0e-9 || h.beats >= to - 1.0e-9) return;
            SongNote n; n.beat = std::max (0.0, origin + h.beats - from); n.lane = h.pad;
            n.note = midi.note[(size_t) h.pad]; n.channel = std::max (1, std::min (16, (int) midi.channel[(size_t) h.pad]));
            n.velocity = std::max (1, std::min (127, (int) std::lround (h.velocity * 127.0f)));
            n.length = 0.25 * std::max (0.05f, midi.lengthSteps);
            notes.push_back (n);
        });
    };
    if (range.mode == 1 && in.project.song.chainOn && in.project.song.numLinks > 0)
    {
        double t = 0.0;
        for (int i = 0; i < in.project.song.numLinks; ++i)
        {
            const auto& l = in.project.song.chain[(size_t) i];
            const int sc = std::min ((int) l.scene, kNumScenes - 1);
            const double len = 0.25 * std::max (1, in.scenes[(size_t) sc].length);
            for (int r = 0; r < std::max (1, (int) l.repeats); ++r) { pass (sc, t, 0.0, len); t += len; }
        }
    }
    else
    {
        double start = 0.0; const double len = exportBeats (in, range, start);
        pass (std::max (0, std::min (range.scene, kNumScenes - 1)), 0.0, start, start + len);
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const SongNote& a, const SongNote& b) { return a.beat < b.beat; });
    // a note ends before the same note starts again
    for (size_t i = 0; i < notes.size(); ++i)
        for (size_t j = i + 1; j < notes.size(); ++j)
            if (notes[j].note == notes[i].note && notes[j].channel == notes[i].channel)
            {
                if (notes[j].beat > notes[i].beat) notes[i].length = std::min (notes[i].length, notes[j].beat - notes[i].beat);
                break;
            }
    return notes;
}

// A standard MIDI file (type 1, 480 ticks per quarter): a tempo track, then one track per lane that has notes.
inline std::vector<uint8_t> writeMidiFile (const std::vector<SongNote>& notes, double bpm, double totalBeats, const std::vector<std::string>& laneNames)
{
    constexpr int kPpq = 480;
    std::vector<uint8_t> f;
    auto u32 = [] (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) (x >> 24)); v.push_back ((uint8_t) (x >> 16)); v.push_back ((uint8_t) (x >> 8)); v.push_back ((uint8_t) x); };
    auto u16 = [] (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) (x >> 8)); v.push_back ((uint8_t) x); };
    auto var = [] (std::vector<uint8_t>& v, uint32_t x)
    {
        uint8_t b[5]; int n = 0;
        b[n++] = (uint8_t) (x & 0x7f);
        while ((x >>= 7) != 0) b[n++] = (uint8_t) ((x & 0x7f) | 0x80);
        while (n > 0) v.push_back (b[--n]);
    };
    auto text = [&] (std::vector<uint8_t>& v, uint8_t type, const std::string& s)
    {
        var (v, 0); v.push_back (0xff); v.push_back (type); var (v, (uint32_t) s.size()); v.insert (v.end(), s.begin(), s.end());
    };
    auto chunk = [&] (const std::vector<uint8_t>& body) { f.insert (f.end(), { 'M', 'T', 'r', 'k' }); u32 (f, (uint32_t) body.size()); f.insert (f.end(), body.begin(), body.end()); };
    const uint32_t endTick = (uint32_t) std::lround (std::max (0.0, totalBeats) * kPpq);

    std::vector<int> lanes;
    for (int l = 0; l < kNumPads; ++l) for (const auto& n : notes) if (n.lane == l) { lanes.push_back (l); break; }
    f.insert (f.end(), { 'M', 'T', 'h', 'd' }); u32 (f, 6); u16 (f, 1); u16 (f, (uint16_t) (1 + lanes.size())); u16 (f, (uint16_t) kPpq);
    {
        std::vector<uint8_t> t;
        text (t, 0x03, "NeoBeat");
        const uint32_t us = (uint32_t) std::lround (60000000.0 / std::max (20.0, bpm));
        var (t, 0); t.insert (t.end(), { 0xff, 0x51, 0x03, (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us });
        var (t, 0); t.insert (t.end(), { 0xff, 0x58, 0x04, 4, 2, 24, 8 });
        var (t, endTick); t.insert (t.end(), { 0xff, 0x2f, 0x00 });
        chunk (t);
    }
    for (int l : lanes)
    {
        struct E { uint32_t tick; bool on; int note, vel, ch; };
        std::vector<E> ev;
        for (const auto& n : notes)
        {
            if (n.lane != l) continue;
            const uint32_t a = (uint32_t) std::lround (n.beat * kPpq), b = std::max (a + 1, (uint32_t) std::lround ((n.beat + n.length) * kPpq));
            ev.push_back ({ a, true, n.note, n.velocity, n.channel }); ev.push_back ({ b, false, n.note, 0, n.channel });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const E& a, const E& b) { return a.tick != b.tick ? a.tick < b.tick : (! a.on && b.on); });
        std::vector<uint8_t> t;
        text (t, 0x03, l < (int) laneNames.size() && ! laneNames[(size_t) l].empty() ? laneNames[(size_t) l] : "Lane " + std::to_string (l + 1));
        uint32_t at = 0;
        for (const auto& e : ev)
        {
            var (t, e.tick - at); at = e.tick;
            t.push_back ((uint8_t) ((e.on ? 0x90 : 0x80) | ((e.ch - 1) & 15))); t.push_back ((uint8_t) (e.note & 127)); t.push_back ((uint8_t) (e.vel & 127));
        }
        var (t, std::max (at, endTick) - at); t.insert (t.end(), { 0xff, 0x2f, 0x00 });
        chunk (t);
    }
    return f;
}
} // namespace nb
