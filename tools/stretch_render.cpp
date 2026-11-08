// Render a MaterialLab rubber material (friction v2 + stretch) through NeoBeat's real-time port.
//   stretch_render <file.nbf> <out.wav> [speed= from= tight=1 slack=1 climbs=1 one=1 light=1 metal=1 nosnap=1 mono=1 seed= seconds=]
#include "../Source/dsp/Friction2.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int rate)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 8;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f); std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3); u16 (2);
    u32 ((uint32_t) rate); u32 ((uint32_t) rate * 8); u16 (8); u16 (32); std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}
int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    std::ifstream in (argv[1]); std::stringstream ss; ss << in.rdbuf();
    nb::FrictionMaterial m; const auto err = nb::parseFrictionMaterial (ss.str(), m);
    if (! err.empty()) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    nb::StretchPlay p; float seconds = -1;
    for (int a = 3; a < argc; ++a)
    {
        const char* eq = std::strchr (argv[a], '='); if (! eq) continue;
        const std::string k (argv[a], (size_t) (eq - argv[a])); const float v = (float) std::atof (eq + 1);
        if (k == "speed") p.speed = v; else if (k == "from") p.from = v; else if (k == "tight") p.tight = v > 0.5f;
        else if (k == "slack") p.slack = v > 0.5f; else if (k == "climbs") p.climbs = v > 0.5f; else if (k == "one") p.oneContact = v > 0.5f;
        else if (k == "light") p.lightBody = v > 0.5f; else if (k == "metal") p.metalBody = v > 0.5f; else if (k == "nosnap") p.snap = v < 0.5f;
        else if (k == "mono") p.width = 0; else if (k == "seed") p.seed = (uint32_t) v; else if (k == "seconds") seconds = v; else if (k == "gain") p.handGain = v > 0.5f;
        else { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 1; }
    }
    const float fs = 48000;
    static nb::StretchVoice voice; voice.prepare (fs);
    voice.start (m, p);
    if (seconds < 0) seconds = (float) m.V.size() * m.hop / fs / p.speed * (1.0f - p.from) + 0.4f;
    const size_t n = (size_t) (seconds * fs);
    std::vector<float> L (n), R (n);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; ++i) voice.process (L[i], R[i]);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    writeWav (argv[2], L, R, (int) fs);
    std::printf ("%s: %.2f s in %.0f ms (%.0f%% of a core), snap at %.3f s\n", m.name.c_str(), seconds, secs * 1000, 100 * secs / seconds, voice.snapTime());
    return 0;
}
