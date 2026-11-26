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
#include "Chaos.h"
#include "Ceiling.h"

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

    // ---------------------------------------------------------------- the short end, made audible
    //
    // The first set of defaults was inaudible, and measurement said why: sixteen bands over
    // 1.6-14 ms is a 12 ms spread, inside the window where the ear fuses everything into one
    // event, and at feedback 0.25-0.45 a band damped out in four repeats - 33 ms, which is still
    // part of the transient. The result was a comb filter: the same drum, differently coloured.
    //
    // For the short end to be an effect rather than a colour, the bands have to RING. A 5 ms
    // delay at 0.85 feedback is a tuned resonator, and that is what the bubble is. Same band,
    // same delay, 189 ms of ring instead of 33.
    { bc::BandsState s; s.dsp.startFeedback = 0.80f; s.dsp.finishFeedback = 0.90f;
      render (dir, "09-short-ringing", s, l, r); }
    { bc::BandsState s; s.dsp.startFeedback = 0.80f; s.dsp.finishFeedback = 0.90f; s.dsp.wet = 0.9f;
      render (dir, "10-ringing-wet-up", s, l, r); }
    { bc::BandsState s; s.dsp.startFeedback = 0.80f; s.dsp.finishFeedback = 0.90f;
      s.dsp.wet = 1.0f; s.dsp.dry = 0.4f;
      render (dir, "11-ringing-wet-forward", s, l, r); }
    { bc::BandsState s; s.dsp.startFeedback = 0.88f; s.dsp.finishFeedback = 0.93f;
      s.dsp.startTimeMs = 0.8f; s.dsp.finishTimeMs = 26.0f;   // a wider spread as well
      s.dsp.wet = 1.0f; s.dsp.dry = 0.5f;
      render (dir, "12-ringing-wide-spread", s, l, r); }

    // ------------------------------------------------------------------ IDM Jack, seizing BANDS
    //
    // The room UI does not exist yet, and does not need to for this: the chaos engine takes a
    // registered list of live parameters, and BANDS' own are a real list. So this is IDM Jack
    // actually running, on a real effect, before a single control has been drawn.
    {
        bc::BandsState st;                       // the shipped defaults
        auto& d = st.dsp;

        nsr::Chaos jack;
        // room, name, value, lo, hi, reach, on, never
        jack.addTarget ({ "BANDS", "length",     &d.length,        0.0f,  1.0f,  0.55f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "start time", &d.startTimeMs,   0.3f,  60.0f, 0.70f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "end time",   &d.finishTimeMs,  1.0f, 200.0f, 0.70f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "feedback",   &d.finishFeedback,0.2f,  0.93f, 0.45f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "start Hz",   &d.startHz,       40.0f, 900.0f,0.60f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "end Hz",     &d.finishHz,    1500.0f,14000.f,0.60f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "Q",          &d.q,             0.8f,  9.0f,  0.70f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "side feed",  &d.sideFeedback,  0.0f,  0.6f,  0.80f, true, false, 0.0f });
        jack.addTarget ({ "BANDS", "wet",        &d.wet,           0.5f,  1.0f,  0.30f, true, false, 0.0f });
        // the never-list: the dry path, so a seizure can never silence the source entirely
        jack.addTarget ({ "BANDS", "dry",        &d.dry,           0.0f,  1.0f,  0.0f, false, true,  0.0f });

        auto renderJack = [&] (const char* name, const nsr::ChaosParams& cp)
        {
            bc::BandsState s2;
            auto& e = s2.dsp;
            nsr::Chaos j;
            for (const auto& t : jack.allTargets())
            {
                // re-point the targets at this render's own state
                nsr::ChaosTarget c = t;
                const size_t off = (size_t) ((const char*) t.value - (const char*) &d);
                c.value = (float*) ((char*) &e + off);
                j.addTarget (c);
            }
            j.setParams (cp);

            const int n = (int) l.size();
            std::vector<float> outL ((size_t) n, 0.0f), outR ((size_t) n, 0.0f);
            std::vector<float> sl ((size_t) kBlock, 0.0f), sr2 ((size_t) kBlock, 0.0f);
            bc::BandsEngine eng;
            eng.prepare (kSR, kBlock);
            eng.setSendBus (sl.data(), sr2.data());

            // Jack can push the bank well over: seizing feedback upward at short delays stacks
            // sixteen resonators. The limiter rides the gain so the seizure keeps its character.
            nsr::Limiter lim;
            lim.prepare (kSR, 3.0f, 120.0f);
            lim.setCeiling (0.95f);

            const double bps = 96.0 / 60.0 / kSR;
            j.engage (0.0);
            int events = 0;
            for (int done = 0; done < n; done += kBlock)
            {
                const int m = std::min (kBlock, n - done);
                const double at = (double) done * bps;
                events += (int) j.advance (at, (double) m * bps).size();
                eng.setState (s2);                 // the seizures reach the engine here
                std::fill (sl.begin(), sl.end(), 0.0f); std::fill (sr2.begin(), sr2.end(), 0.0f);
                eng.process (outL.data() + done, outR.data() + done,
                             l.data() + done, r.data() + done, m, at);
                lim.process (outL.data() + done, outR.data() + done, m);
            }
            const float pk = peakOf (outL, outR);
            writeWav (std::string (dir) + "/" + name + ".wav", outL, outR);
            std::printf ("  %-26s peak %.2f   %d seizures   limiter took %.1f dB%s\n",
                         (std::string (name) + ".wav").c_str(), pk, events,
                         -lim.worstReductionDb(), pk > 0.99f ? "   <- STILL clipping" : "");
        };

        nsr::ChaosParams cp;
        cp.division = 6; cp.swing = 0.58f; cp.density = 0.45f; cp.extremity = 0.55f;
        cp.repeatProb = 0.6f; cp.windowSteps = 8; cp.seed = 3;
        renderJack ("13-jack-moderate", cp);

        cp.density = 0.9f; cp.extremity = 1.0f; cp.repeatProb = 0.1f; cp.seed = 5;
        renderJack ("14-jack-total", cp);

        // high repeat probability: the figure should persist long enough to hear as an idea
        cp.density = 0.7f; cp.extremity = 0.8f; cp.repeatProb = 0.95f; cp.windowSteps = 6; cp.seed = 9;
        renderJack ("15-jack-motifs", cp);

        // and its own settings rolled on switch-on
        cp.selfRandomise = 0.8f; cp.seed = 21;
        renderJack ("16-jack-self-rolled", cp);
    }

    std::printf ("done\n");
    return 0;
}
