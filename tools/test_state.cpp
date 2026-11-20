// What survives saving the project.
//
// Everything MidiJaQ inherited - the pool in CUT, the layers and chops in LOOPS, the instrument in
// KEYS - was absent from the plugin's state. You could load fifty samples, build a loop, map a
// phrase over it, save your set, reopen it, and find an empty plugin. The three serialisers already
// existed, with the audio embedded as FLAC; nothing called them.
//
// So this does the only thing worth doing about that: it builds something in every room, saves the
// state the way a host does, loads it into a second plugin that has never seen any of it, and asks
// whether what comes back is what went in - including whether the samples themselves are still
// there, because a loop whose chops point at nothing is not a loop.

#include "app/Processor.h"
#include "neobeat/gen/Scale.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { std::printf ("  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); ++failures; } \
                              else { std::printf ("  ok:   "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 256;

static std::shared_ptr<bc::PoolItem> tone (double hz, const char* name)
{
    auto it = std::make_shared<bc::PoolItem>();
    it->name = name; it->sr = kSr;
    const int n = (int) (0.30 * kSr);
    it->l.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / kSr;
        it->l[(size_t) i] = (float) (0.4 * std::sin (6.283185307 * hz * t) * std::exp (-3.0 * t));
    }
    it->r = it->l;
    it->f.valid = true; it->f.pitchHz = (float) hz; it->f.pitchStrength = 0.9f; it->f.low = 0.2f;
    return it;
}

static std::unique_ptr<MjqProcessor> fresh()
{
    auto p = std::make_unique<MjqProcessor>();
    p->setPlayConfigDetails (0, 2, kSr, kBlock);
    p->prepareToPlay (kSr, kBlock);
    return p;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce_;

    // ------------------------------------------------------------------ build something in every room
    auto a = fresh();

    for (int i = 0; i < 6; ++i)
        a->kit.pool.push_back (tone (180.0 * std::pow (1.25, i), ("pool " + std::to_string (i)).c_str()));
    a->kitChanged();

    a->loops.name = "a loop worth keeping";
    a->loops.bpm = 96.0;
    a->loops.swing = 0.62f;
    a->loops.numLayers = 2;
    for (int L = 0; L < 2; ++L)
    {
        auto& ly = a->loops.layers[(size_t) L];
        ly.bars = 1; ly.perBar = 16; ly.hits.clear();
        ly.levelDb = L == 0 ? 0.0f : -3.0f;
        for (int h = 0; h < 4; ++h)
        {
            bc::LoopHit hit;
            hit.step = h * 4 + L; hit.len = 2; hit.vel = 0.8f;
            hit.sound = a->kit.pool[(size_t) ((h + L) % (int) a->kit.pool.size())];
            hit.semis = (float) (L * 3);
            ly.hits.push_back (hit);
        }
    }
    a->loopsChanged();

    a->keys = {};
    a->keys.name = "keys worth keeping";
    a->keys.attackMs = 7.0f; a->keys.releaseMs = 420.0f;
    {
        std::vector<std::shared_ptr<const bc::PoolItem>> use (a->kit.pool.begin(), a->kit.pool.end());
        bc::buildInstrument (a->keys, use);
    }
    a->keysChanged();

    a->map = {};
    a->map.steps = 16; a->map.perBar = 16; a->map.root = 2; a->map.scale = 3; a->map.octave = 4;
    for (int s = 0; s < 8; ++s)
        a->map.marks.push_back ({ s * 2, s % 5, 2, 0.85f,
                                  s % 3 == 0 ? mjq::MarkSource::Loop : mjq::MarkSource::Keys, s % 2, 1, false });
    a->mapChanged();

    a->places.assign (4, nsr::Place());
    a->places[0].on = true; a->places[0].x = -0.8f; a->places[0].y = 0.1f; a->places[0].width = 0.3f;
    a->places[1].on = true; a->places[1].locked = true;
    a->places[2].on = true; a->places[2].x = 0.7f; a->places[2].y = 0.9f;
    a->places[2].travel = nsr::Travel::Sway; a->places[2].depth = 0.6f; a->places[2].bars = 2.0f;
    a->stereoChanged();

    const int poolWas = (int) a->kit.pool.size();
    const int zonesWas = (int) a->keys.zones.size();
    const int marksWas = (int) a->map.marks.size();
    std::printf ("built: %d sounds, 2 loop layers, %d zones, %d marks, 3 placed nodes\n",
                 poolWas, zonesWas, marksWas);

    // ------------------------------------------------------------------ save it the way a host does
    juce::MemoryBlock saved;
    a->getStateInformation (saved);
    std::printf ("saved %.0f KB of state\n\n", (double) saved.getSize() / 1024.0);
    CHECK (saved.getSize() > 2000, "the state is big enough to be holding audio (%.0f KB)",
           (double) saved.getSize() / 1024.0);

    // ------------------------------------------------------------------ into a plugin that has seen none of it
    auto b = fresh();
    CHECK (b->kit.pool.empty() && b->keys.zones.empty() && b->map.marks.empty(),
           "the second plugin starts empty, so nothing below is left over");
    b->setStateInformation (saved.getData(), (int) saved.getSize());

    std::printf ("\nthe sounds come back with the rooms that use them\n");
    {
        // What is promised, and what is not. kitToXml writes the sounds a kit lane actually uses,
        // not every loose sound in the pool - a deliberate choice, so a project does not carry
        // fifty megabytes of samples nobody placed. LOOPS and KEYS each embed the audio their own
        // chops and zones point at, which is what makes them reopen whole.
        //
        // The first run of this test asserted the whole pool came back, which the code never
        // promised, and then two checks after it passed on an empty list - comparing nothing to
        // nothing, which is the failure mode worth guarding every time.
        int sounds = 0, sounding = 0;
        for (int L = 0; L < b->loops.numLayers; ++L)
            for (const auto& h : b->loops.layers[(size_t) L].hits)
                if (h.sound != nullptr) { ++sounds; if (h.sound->l.size() > 64) ++sounding; }
        for (const auto& z : b->keys.zones)
            if (z.sound != nullptr) { ++sounds; if (z.sound->l.size() > 64) ++sounding; }

        CHECK (sounds > 0, "there are sounds to check at all (%d), so nothing below is vacuous", sounds);
        CHECK (sounding == sounds, "every one carries its audio, not just its name (%d of %d)", sounding, sounds);
        std::printf ("     (the %d loose pool sounds are not written: only what a room uses is)\n", poolWas);
    }

    std::printf ("\nthe loop comes back\n");
    {
        CHECK (juce::String (b->loops.name) == "a loop worth keeping", "its name (%s)", b->loops.name.c_str());
        CHECK (std::fabs (b->loops.bpm - 96.0) < 0.01, "its tempo (%.1f)", b->loops.bpm);
        CHECK (b->loops.numLayers == 2, "both layers (%d)", b->loops.numLayers);
        int hits = 0, withSound = 0;
        for (int L = 0; L < b->loops.numLayers; ++L)
            for (const auto& h : b->loops.layers[(size_t) L].hits)
            { ++hits; if (h.sound != nullptr && h.sound->l.size() > 64) ++withSound; }
        CHECK (hits == 8, "every chop (%d of 8)", hits);
        CHECK (hits > 0, "there are chops to check, so the line below is not vacuous");
        // the one that matters: a chop pointing at nothing is not a chop
        CHECK (withSound == hits, "and each chop still has its sound (%d of %d)", withSound, hits);
    }

    std::printf ("\nthe instrument comes back\n");
    {
        CHECK (juce::String (b->keys.name) == "keys worth keeping", "its name (%s)", b->keys.name.c_str());
        CHECK ((int) b->keys.zones.size() == zonesWas, "every zone (%d of %d)",
               (int) b->keys.zones.size(), zonesWas);
        CHECK (std::fabs (b->keys.releaseMs - 420.0f) < 0.5f, "how it plays (release %.0f ms)", b->keys.releaseMs);
        bool sounding = ! b->keys.zones.empty();     // an empty list must not pass this
        for (const auto& z : b->keys.zones) sounding = sounding && z.sound != nullptr && z.sound->l.size() > 64;
        CHECK (sounding, "and every zone still has its sound (%d zones)", (int) b->keys.zones.size());
    }

    std::printf ("\nthe map comes back\n");
    {
        CHECK ((int) b->map.marks.size() == marksWas, "every mark (%d of %d)",
               (int) b->map.marks.size(), marksWas);
        CHECK (b->map.root == 2 && b->map.scale == 3 && b->map.octave == 4, "the key it is in");
        bool same = b->map.marks.size() == a->map.marks.size();
        for (size_t i = 0; i < b->map.marks.size() && same; ++i)
        {
            const auto& x = a->map.marks[i]; const auto& y = b->map.marks[i];
            same = x.step == y.step && x.row == y.row && x.len == y.len && x.source == y.source && x.layer == y.layer;
        }
        // each mark carries its own source, so a keys mark must not come back a loop mark
        CHECK (same, "each mark where it was, with the source it had");
    }

    std::printf ("\nthe stage comes back\n");
    {
        CHECK ((int) b->places.size() >= 3, "the nodes are there (%d)", (int) b->places.size());
        if (b->places.size() >= 3)
        {
            CHECK (std::fabs (b->places[0].x - -0.8f) < 0.01f && std::fabs (b->places[0].y - 0.1f) < 0.01f,
                   "where each chop stands (%.2f, %.2f)", b->places[0].x, b->places[0].y);
            CHECK (b->places[1].locked, "which ones are locked to the middle");
            CHECK (b->places[2].travel == nsr::Travel::Sway && std::fabs (b->places[2].bars - 2.0f) < 0.01f,
                   "and the path a travelling one is on (sway over %.1f bars)", b->places[2].bars);
        }
    }

    std::printf ("\nand it plays, in the plugin that only read the file\n");
    {
        // the proof that it is not merely numbers in the right fields
        struct Transport final : public juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            { PositionInfo p; p.setPpqPosition (ppq); p.setBpm (120.0); p.setIsPlaying (true); return p; }
        } t;
        b->setPlayHead (&t);

        juce::AudioBuffer<float> buf (2, kBlock);
        float peak = 0.0f;
        for (int blk = 0; blk < (int) (2.0 * kSr / kBlock); ++blk)
        {
            buf.clear();
            juce::MidiBuffer midi;
            b->processBlock (buf, midi);
            peak = std::max (peak, buf.getMagnitude (0, kBlock));
            t.ppq += (double) kBlock * 120.0 / (60.0 * kSr);
        }
        CHECK (peak > 1.0e-3f, "the map plays what was reopened (peak %.4f)", peak);
    }

    std::printf ("\n%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
