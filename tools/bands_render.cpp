// Renders BANDS to WAV, so the thing can be judged by ear.
//
// Every other check on this engine is numeric or visual - sample counts, decay rates, PNGs. None
// of them can tell you whether 1.6 to 14 ms across the bank actually sounds like the bubbly
// register or just sounds broken. That is a listening question, and it needs audio.
//
// Needs nothing but the standard library: SpectralDelay.h, StereoRoom.h and Bands.h are all
// framework-free, so this builds in seconds and runs headless.
//
//   bands_render <out-dir>
//
// Writes a dry reference first, then the same bars through the bank at each setting worth
// hearing. Listen to 00 and 01 back to back; that pair is the whole question.

#include "rooms/Bands.h"

#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <random>

namespace
{

constexpr double kSR = 48000.0;
constexpr int    kBlock = 512;

// ---------------------------------------------------------------------------------- the test bars
//
// Synthesised rather than sampled, so the file is self-contained and the source is identical every
// run. A swung sixteenth pattern, because that is the music this is for: what a spectral delay
// does to a shuffled hat is the point, and a straight four-on-the-floor would not show it.
struct Kit
{
    std::mt19937 rng { 12345u };
    std::uniform_real_distribution<float> uni { -1.0f, 1.0f };

    void kick (std::vector<float>& l, std::vector<float>& r, int at, float vel)
    {
        const int n = (int) (0.35 * kSR);
        float phase = 0.0f;
        for (int i = 0; i < n && at + i < (int) l.size(); ++i)
        {
            const float t = (float) i / (float) kSR;
            const float env = std::exp (-t * 14.0f);
            const float hz = 52.0f + 90.0f * std::exp (-t * 38.0f);     // the drop
            phase += 6.2831853f * hz / (float) kSR;
            const float s = std::sin (phase) * env * vel * 0.9f;
            l[(size_t) (at + i)] += s; r[(size_t) (at + i)] += s;
        }
    }

    void snare (std::vector<float>& l, std::vector<float>& r, int at, float vel)
    {
        const int n = (int) (0.22 * kSR);
        float p1 = 0.0f, p2 = 0.0f, lp = 0.0f;
        for (int i = 0; i < n && at + i < (int) l.size(); ++i)
        {
            const float t = (float) i / (float) kSR;
            const float env = std::exp (-t * 22.0f);
            p1 += 6.2831853f * 185.0f / (float) kSR;
            p2 += 6.2831853f * 278.0f / (float) kSR;
            const float body = (std::sin (p1) + std::sin (p2) * 0.7f) * 0.35f;
            const float nz = uni (rng);
            lp += 0.45f * (nz - lp);                                     // tilted noise
            const float s = (body + (nz - lp) * 0.9f) * env * vel * 0.6f;
            l[(size_t) (at + i)] += s; r[(size_t) (at + i)] += s;
        }
    }

    void hat (std::vector<float>& l, std::vector<float>& r, int at, float vel, bool open)
    {
        const int n = (int) ((open ? 0.17 : 0.045) * kSR);
        float hp = 0.0f, prev = 0.0f;
        for (int i = 0; i < n && at + i < (int) l.size(); ++i)
        {
            const float t = (float) i / (float) kSR;
            const float env = std::exp (-t * (open ? 16.0f : 60.0f));
            const float nz = uni (rng);
            hp = 0.86f * (hp + nz - prev); prev = nz;                    // one-pole highpass
            const float s = hp * env * vel * 0.34f;
            // hats slightly off-centre, as they would be in a mix
            l[(size_t) (at + i)] += s * 0.78f; r[(size_t) (at + i)] += s * 1.0f;
        }
    }
};

// Two bars of swung sixteenths at 96 BPM.
void writeBars (std::vector<float>& l, std::vector<float>& r)
{
    Kit kit;
    const double bpm = 96.0;
    const double beat = 60.0 / bpm * kSR;
    const double sixteenth = beat * 0.25;
    const double swing = 0.62;                 // how far the offbeat sixteenth is pushed

    auto at = [&] (int bar, int step)
    {
        const double base = (double) bar * 4.0 * beat + (double) (step / 2) * beat * 0.5;
        const double off = (step % 2) ? sixteenth * 2.0 * swing : 0.0;
        return (int) (base + off);
    };

    for (int bar = 0; bar < 2; ++bar)
    {
        kit.kick (l, r, at (bar, 0), 1.0f);
        kit.kick (l, r, at (bar, 6), 0.75f);
        kit.kick (l, r, at (bar, 11), 0.6f);
        kit.snare (l, r, at (bar, 4), 0.95f);
        kit.snare (l, r, at (bar, 12), 1.0f);
        if (bar == 1) kit.snare (l, r, at (bar, 14), 0.5f);         // the pickup
        for (int s = 0; s < 16; ++s)
            kit.hat (l, r, at (bar, s), (s % 4 == 0) ? 0.85f : ((s % 2) ? 0.42f : 0.6f), s == 7);
    }
}

// ----------------------------------------------------------------------------------- the wav file
bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;
    const uint32_t frames = (uint32_t) l.size();
    const uint32_t dataBytes = frames * 2u * 2u;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f); u32 (36u + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16u); u16 (1); u16 (2);
    u32 ((uint32_t) kSR); u32 ((uint32_t) kSR * 4u); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);

    for (uint32_t i = 0; i < frames; ++i)
        for (const auto* ch : { &l, &r })
        {
            float v = (*ch)[i];
            v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
            u16 ((uint16_t) (int16_t) std::lround (v * 32767.0f));
        }
    std::fclose (f);
    return true;
}

float peakOf (const std::vector<float>& l, const std::vector<float>& r)
{
    float p = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) p = std::max (p, std::max (std::abs (l[i]), std::abs (r[i])));
    return p;
}

// Render the bars through one BandsState and write it. Returns the peak, so a setting that is
// quietly clipping shows up in the console rather than only in the file.
void render (const std::string& dir, const char* name, const bc::BandsState& st,
             const std::vector<float>& srcL, const std::vector<float>& srcR)
{
    const int n = (int) srcL.size();
    std::vector<float> outL ((size_t) n, 0.0f), outR ((size_t) n, 0.0f);
    std::vector<float> sendL ((size_t) kBlock, 0.0f), sendR ((size_t) kBlock, 0.0f);

    bc::BandsEngine eng;
    eng.prepare (kSR, kBlock);
    eng.setState (st);
    eng.setSendBus (sendL.data(), sendR.data());

    // Fed in blocks, as a host would. A Strip glides to its target across one block, so a single
    // giant call would spend the whole render mid-glide and every position would read washed out.
    const double beatsPerSample = 96.0 / 60.0 / kSR;
    for (int done = 0; done < n; done += kBlock)
    {
        const int m = std::min (kBlock, n - done);
        std::fill (sendL.begin(), sendL.end(), 0.0f);
        std::fill (sendR.begin(), sendR.end(), 0.0f);
        eng.process (outL.data() + done, outR.data() + done,
                     srcL.data() + done, srcR.data() + done, m, (double) done * beatsPerSample);
    }

    // Render the same thing again with the dry path silenced, so the console can report what the
    // bank itself is contributing. Peaks that match the dry reference to two decimal places tell
    // you nothing about whether the wet is audible.
    float wetPk = 0.0f;
    {
        bc::BandsState wetOnly = st;
        wetOnly.dsp.dry = 0.0f;
        std::vector<float> wl ((size_t) n, 0.0f), wr ((size_t) n, 0.0f);
        std::vector<float> sl ((size_t) kBlock, 0.0f), sr ((size_t) kBlock, 0.0f);
        bc::BandsEngine w;
        w.prepare (kSR, kBlock);
        w.setState (wetOnly);
        w.setSendBus (sl.data(), sr.data());
        for (int done = 0; done < n; done += kBlock)
        {
            const int m = std::min (kBlock, n - done);
            std::fill (sl.begin(), sl.end(), 0.0f); std::fill (sr.begin(), sr.end(), 0.0f);
            w.process (wl.data() + done, wr.data() + done,
                       srcL.data() + done, srcR.data() + done, m, (double) done * beatsPerSample);
        }
        wetPk = peakOf (wl, wr);
        writeWav (dir + "/" + name + "-wet.wav", wl, wr);
    }

    const float pk = peakOf (outL, outR);
    const std::string path = dir + "/" + name + ".wav";
    if (writeWav (path, outL, outR))
        std::printf ("  %-26s peak %.2f   wet alone %.3f%s\n",
                     (std::string (name) + ".wav").c_str(), pk, wetPk,
                     pk > 0.99f ? "   <- clipping" : (wetPk < 0.005f ? "   <- wet is silent" : ""));
    else
        std::printf ("  FAILED to write %s\n", path.c_str());
}

} // namespace

int main (int argc, char* argv[])
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::printf ("BANDS, rendered to %s\n", dir.c_str());

    // a little tail, so the repeats are not cut off
    const int n = (int) (6.5 * kSR);
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    writeBars (l, r);

    // Normalise the source to leave headroom. The kit as synthesised peaks above 1, and a dry
    // reference that is already clipping makes every comparison after it worthless.
    {
        const float pk = peakOf (l, r);
        if (pk > 0.0f)
        {
            const float g = 0.62f / pk;
            for (size_t i = 0; i < l.size(); ++i) { l[i] *= g; r[i] *= g; }
        }
    }

    if (writeWav (dir + "/00-dry.wav", l, r))
        std::printf ("  %-26s peak %.2f   (the source, for reference)\n", "00-dry.wav", peakOf (l, r));

    // 01 — the defaults, exactly as they ship
    { bc::BandsState s; render (dir, "01-default-short", s, l, r); }

    // 02, 03 — the Length control, which is the fix for Chimecho's one usability failure
    { bc::BandsState s; s.dsp.length = 0.5f; render (dir, "02-length-half", s, l, r); }
    { bc::BandsState s; s.dsp.length = 1.0f; render (dir, "03-length-full", s, l, r); }

    // 04 — the pan LFO: two knobs over sixteen places
    { bc::BandsState s; s.panLfo (0.8f, 2.0f); render (dir, "04-pan-lfo", s, l, r); }

    // 05, 06 — the neighbour feed. +2 walks energy up the spectrum with each repeat, -2 walks it
    // down. These two should sound clearly different; when they did not, the bandpass was inside
    // the feedback loop and was deleting exactly the content that was supposed to migrate.
    { bc::BandsState s; s.dsp.length = 0.45f; s.dsp.sideOffset = 2; s.dsp.sideFeedback = 0.45f;
      render (dir, "05-climbing", s, l, r); }
    { bc::BandsState s; s.dsp.length = 0.45f; s.dsp.sideOffset = -2; s.dsp.sideFeedback = 0.45f;
      render (dir, "06-descending", s, l, r); }

    // 07 — the interaural split. Headphones: the bank should sit inside the head rather than in a
    // place in front of you.
    { bc::BandsState s; s.dsp.startInterMs = 1.0f; s.dsp.finishInterMs = 9.0f;
      render (dir, "07-interaural-split", s, l, r); }

    // 08 — placement off, so the placement itself can be A/B'd against 01
    { bc::BandsState s; s.placed = false; render (dir, "08-placement-off", s, l, r); }

    std::printf ("done\n");
    return 0;
}
