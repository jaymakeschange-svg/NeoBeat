// NeoBeat command-line renderer: drives exactly the plugin's engine and
// sequencer offline, for listening, measuring and calibration.
//
//   neobeat_cli list
//   neobeat_cli sound <id|index> <out.wav> [velocity] [hollow] [tune] [decay] [tone] [evolve] [seconds]
//   neobeat_cli r6 <outdir>                      every Round 6 recipe, dry and hollow
//   neobeat_cli calibrate <Calibration.h>        level-match every sound, write the table
//   neobeat_cli preset <index> <out.wav> [bars] [kit]   render a sequencer preset
//   neobeat_cli presets <outdir> [bars]          render all presets
//   neobeat_cli bench                            real-time cost of the busiest preset
//   neobeat_cli kittour <kit> <out.wav>          every pad of a kit, one after another
#include "../Source/engine/Engine.h"
#include "../Source/engine/Sequencer.h"
#include "../Source/engine/Presets.h"
#include "../Source/engine/Kits.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
const float kRate = [] { const char* v = std::getenv ("NB_RATE"); return v ? (float) std::atof (v) : 48000.0f; }();

bool writeWav24 (const std::string& path, const std::vector<float>& L, const std::vector<float>& R)
{
    float peak = 0.0f;
    for (size_t i = 0; i < L.size(); ++i) peak = std::max (peak, std::max (std::fabs (L[i]), std::fabs (R[i])));
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) { std::fprintf (stderr, "cannot write %s\n", path.c_str()); return false; }
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 6, rate = (uint32_t) kRate;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (rate); u32 (rate * 6); u16 (6); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float s : { L[i], R[i] })
        {
            const int32_t v = (int32_t) std::lround (ls::clampf (s, -1.0f, 1.0f) * 8388607.0f);
            const unsigned char b[3] = { (unsigned char) (v & 0xff), (unsigned char) ((v >> 8) & 0xff), (unsigned char) ((v >> 16) & 0xff) };
            std::fwrite (b, 1, 3, f);
        }
    std::fclose (f);
    std::printf ("%-60s peak %6.2f dBFS%s\n", path.c_str(), 20.0 * std::log10 (std::max (peak, 1.0e-9f)), peak > 1.0f ? "  CLIPPED" : "");
    return true;
}

// Momentary loudness proxy: K-weighting (high shelf + low cut), the loudest 400 ms window.
struct KWeight
{
    nb::Biquad shelf, hp;
    explicit KWeight (float sr)
    {
        // ITU-R BS.1770 stage 1 (high shelf ~+4 dB above 1.5 kHz) and stage 2 (RLB high-pass ~38 Hz), bilinear forms
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (nb::kPiD * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (float) ((Vh + Vb * K / Q + K * K) / a0); shelf.b1 = (float) (2.0 * (K * K - Vh) / a0);
        shelf.b2 = (float) ((Vh - Vb * K / Q + K * K) / a0); shelf.a1 = (float) (2.0 * (K * K - 1.0) / a0); shelf.a2 = (float) ((1.0 - K / Q + K * K) / a0);
        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan (nb::kPiD * f1 / sr);
        const double d = 1.0 + K1 / Q1 + K1 * K1;
        hp.b0 = (float) (1.0 / d); hp.b1 = (float) (-2.0 / d); hp.b2 = (float) (1.0 / d);
        hp.a1 = (float) (2.0 * (K1 * K1 - 1.0) / d); hp.a2 = (float) ((1.0 - K1 / Q1 + K1 * K1) / d);
    }
};

float momentaryMaxDb (const std::vector<float>& L, const std::vector<float>& R, float window = 0.4f)
{
    KWeight kl (kRate), kr (kRate);
    std::vector<double> e (L.size());
    for (size_t i = 0; i < L.size(); ++i)
    {
        const float a = kl.hp.process (kl.shelf.process (L[i])), b = kr.hp.process (kr.shelf.process (R[i]));
        e[i] = (double) a * a + (double) b * b;
    }
    const size_t w = (size_t) (window * kRate);
    double sum = 0.0, best = 1.0e-20;
    for (size_t i = 0; i < e.size(); ++i)
    {
        sum += e[i];
        if (i >= w) sum -= e[i - w];
        best = std::max (best, sum / (double) w);
    }
    return (float) (-0.691 + 10.0 * std::log10 (best));
}

float rmsOf (const std::vector<float>& L, const std::vector<float>& R)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = 0; i < L.size(); ++i) { s += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]); ++n; }
    return (float) std::sqrt (s / std::max<size_t> (n, 1));
}

void renderPad (nb::Engine& e, const nb::PadSettings& ps, float velocity, float seconds, std::vector<float>& L, std::vector<float>& R)
{
    e.allNotesOff();
    std::vector<float> junkL (4800), junkR (4800);
    e.render (junkL.data(), junkR.data(), 4800);   // let any choked voice finish
    e.pads[0] = ps;
    const size_t n = (size_t) (seconds * kRate);
    L.assign (n, 0.0f); R.assign (n, 0.0f);
    e.trigger (0, velocity);
    e.render (L.data(), R.data(), (int) n);
}

int soundIndex (const nb::Engine& e, const std::string& s)
{
    const int byId = e.lib().find (s);
    if (byId >= 0) return byId;
    return std::atoi (s.c_str());
}

std::string slug (std::string s)
{
    for (auto& c : s) c = (char) (std::isalnum ((unsigned char) c) ? std::tolower ((unsigned char) c) : '_');
    return s;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "usage: see the top of neobeat_cli.cpp\n"); return 1; }
    const std::string mode = argv[1];
    auto engine = std::make_unique<nb::Engine>();
    engine->prepare (kRate);
    if (const char* g = std::getenv ("NB_GLUE")) engine->glue = (float) std::atof (g);
    auto& e = *engine;
    const auto& lib = e.lib();

    if (mode == "list")
    {
        for (size_t i = 0; i < lib.sounds.size(); ++i)
            std::printf ("%2zu  %-34s %-28s %-16s f0 %7.1f  trim %+5.1f  hollow %+5.1f\n", i, lib.sounds[i].id.c_str(), lib.sounds[i].name.c_str(),
                         lib.sounds[i].group.c_str(), lib.sounds[i].pitchHz, lib.sounds[i].trimDb, lib.sounds[i].hollowTrimDb);
        return 0;
    }
    if (mode == "sound" && argc >= 4)
    {
        nb::PadSettings ps;
        ps.sound = soundIndex (e, argv[2]);
        const float vel = argc > 4 ? (float) std::atof (argv[4]) : 0.8f;
        ps.hollow = argc > 5 ? (float) std::atof (argv[5]) : 0.0f;
        ps.tune = argc > 6 ? (float) std::atof (argv[6]) : 0.0f;
        ps.decay = argc > 7 ? (float) std::atof (argv[7]) : 1.0f;
        ps.tone = argc > 8 ? (float) std::atof (argv[8]) : 0.0f;
        ps.evolve = argc > 9 ? (float) std::atof (argv[9]) : 0.5f;
        const float secs = argc > 10 ? (float) std::atof (argv[10]) : 4.0f;
        if (const char* c = std::getenv ("NB_CHAR")) ps.character = std::atoi (c) != 0;
        std::vector<float> L, R;
        renderPad (e, ps, vel, secs, L, R);
        writeWav24 (argv[3], L, R);
        std::printf ("momentary max %.1f LUFS, rms %.4f\n", momentaryMaxDb (L, R), rmsOf (L, R));
        return 0;
    }
    if (mode == "series" && argc >= 4)
    {
        // five hits at rising velocity, one second apart (like the Round 4 renders)
        nb::PadSettings ps;
        ps.sound = soundIndex (e, argv[2]);
        if (const char* c = std::getenv ("NB_CHAR")) ps.character = std::atoi (c) != 0;
        e.pads[0] = ps;
        const size_t n = (size_t) (5.55f * kRate);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        size_t pos = 0;
        const float vels[5] = { 0.3f, 0.5f, 0.7f, 0.85f, 1.0f };
        for (int k = 0; k < 5; ++k)
        {
            const size_t at = (size_t) ((0.05f + (float) k) * kRate);
            e.render (L.data() + pos, R.data() + pos, (int) (at - pos));
            pos = at;
            e.trigger (0, vels[k]);
        }
        e.render (L.data() + pos, R.data() + pos, (int) (n - pos));
        writeWav24 (argv[3], L, R);
        return 0;
    }
    if (mode == "kittour" && argc >= 4)
    {
        nb::applyKit (e, nb::kits()[(size_t) std::atoi (argv[2])]);
        const float gap = 0.7f;
        const size_t n = (size_t) ((16 * gap + 3.0f) * kRate);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        size_t pos = 0;
        for (int p = 0; p < 16; ++p)
        {
            const size_t at = (size_t) ((float) p * gap * kRate);
            e.render (L.data() + pos, R.data() + pos, (int) (at - pos));
            pos = at;
            e.trigger (p, 0.85f);
        }
        e.render (L.data() + pos, R.data() + pos, (int) (n - pos));
        writeWav24 (argv[3], L, R);
        return 0;
    }
    if (mode == "r6" && argc >= 3)
    {
        const std::string dir = argv[2];
        for (size_t i = 0; i < lib.sounds.size(); ++i)
        {
            if (lib.sounds[i].kind != nb::HitSource::Kind::Recipe) continue;
            const float len = lib.sounds[i].hollow.length;
            for (int h = 0; h < 2; ++h)
            {
                nb::PadSettings ps; ps.sound = (int) i; ps.hollow = (float) h;
                std::vector<float> L, R;
                renderPad (e, ps, 0.8f, len, L, R);
                writeWav24 (dir + "/" + slug (lib.sounds[i].name) + (h ? "_hollow.wav" : "_dry.wav"), L, R);
            }
        }
        return 0;
    }
    if (mode == "calibrate" && argc >= 3)
    {
        // Every sound's dry hit sits at the same momentary loudness; the hollow
        // path is matched to its dry path (as the lab matched before/after).
        // Measured before the master: master at -12 dB keeps the safety knee out of the
        // way, and every reading adds the 12 dB back.
        const float target = -12.0f, window = 0.05f, peakCap = 2.0f, back = 12.0412f;
        e.master = 0.25f;
        e.glue = 0.0f;   // measure each sound alone, without the kit bus
        std::string out = "// Generated by tools/neobeat_cli calibrate -- do not edit by hand.\n#pragma once\n\nnamespace nb\n{\n"
                          "struct CalibrationEntry { const char* id; float trimDb, hollowTrimDb, levelRms, peakRef = 0.3f; };\ninline const CalibrationEntry kCalibration[] = {\n";
        for (size_t i = 0; i < lib.sounds.size(); ++i)
        {
            const auto& sd = lib.sounds[i];
            const float len = sd.kind == nb::HitSource::Kind::Recipe ? sd.hollow.length : 4.0f;
            // measure untrimmed: temporarily zero this sound's trims
            auto& mut = const_cast<nb::SoundDef&> (sd);
            const float oldTrim = mut.trimDb, oldHollow = mut.hollowTrimDb;
            mut.trimDb = 0.0f; mut.hollowTrimDb = 0.0f;
            nb::PadSettings ps; ps.sound = (int) i; ps.hollow = 0.0f;
            std::vector<float> L, R;
            // 1. raw hit (no character): the hollow shaper's RMS reference and the drive's peak reference
            ps.character = false;
            renderPad (e, ps, 0.8f, len, L, R);
            size_t last = 0;
            float rawPeak = 1.0e-6f;
            for (size_t k = 0; k < L.size(); ++k)
            {
                if (std::fabs (L[k]) + std::fabs (R[k]) > 1.0e-4f) last = k;
                rawPeak = std::max (rawPeak, std::max (std::fabs (L[k]), std::fabs (R[k])));
            }
            L.resize (std::max<size_t> (last, 1)); R.resize (std::max<size_t> (last, 1));
            const float rms = rmsOf (L, R) * 4.0f;
            mut.levelRms = std::max (rms, 1.0e-4f);
            mut.peakRef = rawPeak * 4.0f;
            // 2. the voiced hit
            ps.character = true;
            renderPad (e, ps, 0.8f, len, L, R);
            const float dryDb = momentaryMaxDb (L, R, window) + back;
            ps.hollow = 1.0f;
            renderPad (e, ps, 0.8f, sd.kind == nb::HitSource::Kind::Recipe ? sd.hollow.length : sd.hollow.length + 0.2f, L, R);
            const float wetDb = momentaryMaxDb (L, R, window) + back;
            // peaks at full velocity, dry and hollow: no hit may pass -3 dBFS on its own
            auto peakDb = [] (const std::vector<float>& a, const std::vector<float>& b)
            {
                float pk = 1.0e-9f;
                for (size_t k = 0; k < a.size(); ++k) pk = std::max (pk, std::max (std::fabs (a[k]), std::fabs (b[k])));
                return 20.0f * std::log10 (pk) + 12.0412f;
            };
            ps.hollow = 0.0f; renderPad (e, ps, 1.0f, len, L, R); const float dryPk = peakDb (L, R);
            ps.hollow = 1.0f; renderPad (e, ps, 1.0f, len, L, R); const float wetPk = peakDb (L, R);
            const float hollowTrim = dryDb - wetDb;
            const float trim = std::min (target - dryDb, std::min (peakCap - dryPk, peakCap - (wetPk + hollowTrim)));
            mut.trimDb = oldTrim; mut.hollowTrimDb = oldHollow;
            char line[256];
            std::snprintf (line, sizeof line, "    { \"%s\", %.2ff, %.2ff, %.5ff, %.5ff },\n", sd.id.c_str(), trim, hollowTrim, rms, mut.peakRef);
            out += line;
            std::printf ("%-34s dry %6.1f  hollow %6.1f  trim %+6.2f  hollowTrim %+6.2f  rms %.4f  peaks %5.1f/%5.1f\n", sd.id.c_str(), dryDb, wetDb, trim, hollowTrim, rms, dryPk + trim, wetPk + hollowTrim + trim);
        }
        out += "};\n} // namespace nb\n";
        std::ofstream (argv[2]) << out;
        return 0;
    }
    if ((mode == "preset" && argc >= 4) || (mode == "presets" && argc >= 3) || mode == "bench")
    {
        const bool bench = mode == "bench";
        const int bars = mode == "preset" ? (argc > 4 ? std::atoi (argv[4]) : 4) : (argc > 3 ? std::atoi (argv[3]) : 4);
        std::vector<int> which;
        if (mode == "preset") which.push_back (std::atoi (argv[2]));
        else for (int i = 0; i < nb::kNumPresets; ++i) which.push_back (i);
        double worst = 0.0;
        for (int pi : which)
        {
            const auto& pr = nb::presets()[(size_t) pi];
            const int kit = mode == "preset" && argc > 5 ? std::atoi (argv[5]) : (std::getenv ("NB_KIT") ? std::atoi (std::getenv ("NB_KIT")) : 0);
            nb::applyKit (e, nb::kits()[(size_t) kit]);
            nb::Sequencer seq;
            seq.prepare (kRate);
            seq.setPattern (pr.pattern);
            for (int pad = 0; pad < nb::Engine::kPads; ++pad) seq.setPreroll (pad, e.prerollSeconds (pad));
            const double bpm = pr.tempo;
            const double ppqLen = 4.0 * bars;
            const size_t n = (size_t) ((ppqLen * 60.0 / bpm + 1.5) * kRate);
            std::vector<float> L (n, 0.0f), R (n, 0.0f);
            const int block = 256;
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<nb::SeqEvent> events;
            events.reserve (2048);
            for (size_t pos = 0; pos < n; pos += block)
            {
                const int len = (int) std::min<size_t> (block, n - pos);
                const double ppq0 = (double) pos / kRate * bpm / 60.0;
                const bool playing = ppq0 < ppqLen;
                events.clear();
                seq.collect (ppq0, bpm, len, playing, pr.swing, 0.0f, events);
                int done = 0;
                for (const auto& ev : events)
                {
                    if (ev.offset > done) { e.render (L.data() + pos + done, R.data() + pos + done, ev.offset - done); done = ev.offset; }
                    e.trigger (ev.pad, ev.velocity);
                }
                if (len > done) e.render (L.data() + pos + done, R.data() + pos + done, len - done);
            }
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            const double audio = (double) n / kRate;
            worst = std::max (worst, secs / audio);
            if (bench) { std::printf ("%2d %-28s %5.1f%% of one core\n", pi, pr.name.c_str(), 100.0 * secs / audio); continue; }
            const std::string path = mode == "preset" ? std::string (argv[3])
                                                      : std::string (argv[2]) + "/" + (pi < 9 ? "0" : "") + std::to_string (pi + 1) + "_" + slug (pr.name) + ".wav";
            writeWav24 (path, L, R);
        }
        if (bench) std::printf ("busiest: %.1f%% of one core\n", 100.0 * worst);
        return 0;
    }
    std::fprintf (stderr, "unknown command\n");
    return 1;
}
