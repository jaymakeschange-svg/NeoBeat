// MaterialLab demos through the NeoBeat engine (exactly what the plugin plays).
//
//   ml_demo places <sound-id> <out.wav>       the sound in the eight MaterialLab places
//   ml_demo variations <sound-id> <out.wav>   as learned, then the learner's ten variations
//   ml_demo gestures <out-dir>                the five basketball gestures and six materials thrown away
//   ml_demo family <out.wav>                  every Stretch & Snap sound, each snap landing on a beat
//   ml_demo tour <kit-index> <out.wav>        every pad of a kit
#include "../Source/engine/Engine.h"
#include "../Source/engine/Kits.h"
#include <cstdio>
#include <string>
#include <vector>

namespace
{
const float kRate = 48000.0f;

void writeWav24 (const std::string& path, std::vector<float> L, std::vector<float> R, float peakTo = 0.0f)
{
    float peak = 1e-9f;
    for (size_t i = 0; i < L.size(); ++i) peak = std::max (peak, std::max (std::fabs (L[i]), std::fabs (R[i])));
    if (peakTo > 0.0f) { const float g = peakTo / peak; for (size_t i = 0; i < L.size(); ++i) { L[i] *= g; R[i] *= g; } peak = peakTo; }
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 6, rate = (uint32_t) kRate;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (rate); u32 (rate * 6); u16 (6); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { L[i], R[i] })
        {
            const int32_t v = (int32_t) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 8388607.0f);
            const unsigned char b[3] = { (unsigned char) (v & 0xff), (unsigned char) ((v >> 8) & 0xff), (unsigned char) ((v >> 16) & 0xff) };
            std::fwrite (b, 1, 3, f);
        }
    std::fclose (f);
    std::printf ("%-70s %6.1f s  peak %6.2f dBFS\n", path.c_str(), frames / kRate, 20.0 * std::log10 (peak));
}

// A timeline of triggers on one engine.
struct Take
{
    nb::Engine& e;
    std::vector<float> L, R;
    explicit Take (nb::Engine& eng) : e (eng) {}
    void runTo (double seconds)
    {
        const size_t want = (size_t) (seconds * kRate);
        if (want <= L.size()) return;
        const size_t n = want - L.size();
        std::vector<float> l (n, 0.0f), r (n, 0.0f);
        e.render (l.data(), r.data(), (int) n);
        L.insert (L.end(), l.begin(), l.end()); R.insert (R.end(), r.begin(), r.end());
    }
    void hit (double at, int pad, float vel) { runTo (at); e.trigger (pad, vel); }
};

int soundId (nb::Engine& e, const std::string& id)
{
    const int i = e.lib().find (id);
    if (i < 0) { std::fprintf (stderr, "no sound %s\n", id.c_str()); std::exit (1); }
    return i;
}

void padFor (nb::Engine& e, int pad, const std::string& id)
{
    auto& ps = e.pads[(size_t) pad];
    ps = nb::PadSettings();
    ps.sound = soundId (e, id);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "see the header of tools/ml_demo.cpp\n"); return 1; }
    static nb::Engine e;
    e.prepare (kRate);
    const std::string mode = argv[1];

    if (mode == "places" && argc >= 4)
    {
        Take t (e);
        padFor (e, 0, argv[2]);
        for (int p = 0; p < nb::kNumPlaces; ++p)
        {
            e.pads[0].place = p;
            t.hit (0.1 + 4.44 * p, 0, 0.8f);
        }
        t.runTo (0.1 + 4.44 * nb::kNumPlaces);
        writeWav24 (argv[3], t.L, t.R);
        return 0;
    }
    if (mode == "variations" && argc >= 4)
    {
        Take t (e);
        padFor (e, 0, argv[2]);
        for (int v = 0; v < (int) nb::Variation::Count; ++v)
        {
            e.pads[0].variation = v;
            t.hit (0.1 + 1.6 * v, 0, 0.8f);
        }
        t.runTo (0.1 + 1.6 * (int) nb::Variation::Count + 1.0);
        writeWav24 (argv[3], t.L, t.R);
        return 0;
    }
    if (mode == "gestures")
    {
        const std::string dir = argv[2];
        // gestures_demo.py: the wooden ball's measured restitution, bounce after bounce
        const std::vector<float> measured { 0.861f, 0.806f, 0.76f, 0.737f, 0.786f };
        const char* names[] = { "", "1 natural bounces", "2 bounce away (your downward dash)", "3 about to impact (upward dash)",
                                "4 impossible - recedes while rising", "5 impossible - gains energy" };
        auto bounces = [&] (float first, const std::vector<float>& es, int count)
        {
            std::vector<float> times, speeds; float t = 0.05f, fl = first, v = 1.0f;
            for (int i = 0; i < count; ++i) { times.push_back (t); speeds.push_back (v); const float ee = es[(size_t) std::min (i, (int) es.size() - 1)]; fl *= ee; v *= ee; t += fl; }
            return std::make_pair (times, speeds);
        };
        for (int g = 1; g <= 5; ++g)
        {
            Take t (e);
            padFor (e, 0, "ml/basketball");
            auto& ps = e.pads[0];
            ps.place = 1; ps.gesture = g;
            const bool gains = g == 5;
            auto b = bounces (gains ? 0.12f : 0.7f, gains ? std::vector<float> { 1.12f } : measured, gains ? 9 : 12);
            if (gains) { float mx = 0; for (float s : b.second) mx = std::max (mx, s); for (float& s : b.second) s /= mx; }
            ps.gestureSeconds = b.first.back() + 0.4f;
            for (size_t i = 0; i < b.first.size(); ++i) t.hit (b.first[i], 0, 0.8f * b.second[i]);
            t.runTo (b.first.back() + 2.0);
            writeWav24 (dir + "/Basketball " + names[g] + ".wav", t.L, t.R, 0.89f);
        }
        // every material thrown away with the same stroke
        Take t (e);
        double at = 0.0;
        for (const char* id : { "ml/clank", "ml/tube_clank", "ml/hollow_pan", "ml/tin_can", "ml/pvc_pipe", "ml/bucket" })
        {
            padFor (e, 0, id);
            auto& ps = e.pads[0];
            ps.place = 1; ps.gesture = 2;
            auto b = bounces (0.35f, measured, 8);
            ps.gestureSeconds = b.first.back() + 0.4f;
            for (size_t i = 0; i < b.first.size(); ++i) t.hit (at + b.first[i], 0, 0.8f * b.second[i]);
            at += b.first.back() + 2.2;
        }
        t.runTo (at + 0.5);
        writeWav24 (dir + "/Six materials thrown away.wav", t.L, t.R, 0.89f);
        return 0;
    }
    if (mode == "family" && argc >= 3)
    {
        // each snap lands on a beat 1.2 s after the previous sound ends; a soft click marks the beat
        Take t (e);
        double beat = 0.0;
        padFor (e, 1, "ml/pvc_pipe");
        e.pads[1].levelDb = -18.0f; e.pads[1].tune = 12.0f; e.pads[1].decay = 0.2f;
        for (const auto& sd : e.lib().sounds)
        {
            if (sd.group != "Stretch & Snap") continue;
            padFor (e, 0, sd.id);
            const float pre = e.prerollSeconds (0);
            beat = std::max (beat, t.L.size() / (double) kRate + pre + 0.2);
            t.hit (beat - pre, 0, 0.8f);
            if (pre > 0) t.hit (beat, 1, 0.8f);
            std::printf ("  %-22s pull %.2f s before the snap\n", sd.id.c_str(), pre);
            beat += pre > 0 ? 1.4 : 2.6;
            t.runTo (beat - 0.6);
        }
        t.runTo (beat + 1.0);
        writeWav24 (argv[2], t.L, t.R);
        return 0;
    }
    if (mode == "tour" && argc >= 4)
    {
        nb::applyKit (e, nb::kits()[(size_t) std::atoi (argv[2])]);
        Take t (e);
        double at = 0.0;
        for (int p = 0; p < 16; ++p)
        {
            const float pre = e.prerollSeconds (p);
            at = std::max (at, t.L.size() / (double) kRate) + pre;
            t.hit (at - pre, p, 0.8f);
            at += 1.3;
        }
        t.runTo (at + 1.0);
        writeWav24 (argv[3], t.L, t.R);
        return 0;
    }
    std::fprintf (stderr, "unknown command\n");
    return 1;
}
