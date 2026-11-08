// Render a MaterialLab material through NeoBeat's real-time port of engine2.
//   body_render <file.nbm> <out.wav> [key=value ...]
//   keys: speed pitch decay variation(0..10) width level seed samples nonoise=1 (modes only) mono=1
#include "../Source/dsp/MaterialBody.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

static void writeWavFloat (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int rate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 8;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3); u16 (2); u32 ((uint32_t) rate); u32 ((uint32_t) rate * 8); u16 (8); u16 (32);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "usage: body_render <file.nbm> <out.wav> [key=value ...]\n"); return 1; }
    std::ifstream in (argv[1]); std::stringstream ss; ss << in.rdbuf();
    nb::Engine2Material m;
    const auto err = nb::parseEngine2Material (ss.str(), m);
    if (! err.empty()) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    const float fs = 48000.0f;
    nb::BodyPlay play;
    int samples = m.samples;
    bool noNoise = false;
    for (int a = 3; a < argc; ++a)
    {
        const char* eq = std::strchr (argv[a], '=');
        if (! eq) continue;
        const std::string k (argv[a], (size_t) (eq - argv[a]));
        const float v = (float) std::atof (eq + 1);
        if (k == "speed") play.speed = v; else if (k == "pitch") play.pitch = v; else if (k == "decay") play.decay = v;
        else if (k == "variation") play.variation = (nb::Variation) (int) v; else if (k == "width") play.width = v;
        else if (k == "level") play.level = v; else if (k == "seed") play.seed = (uint32_t) v;
        else if (k == "samples") samples = (int) v; else if (k == "nonoise") noNoise = v > 0.5f;
        else if (k == "mono") play.width = 0.0f;
        else { std::fprintf (stderr, "unknown key %s\n", k.c_str()); return 1; }
    }
    if (noNoise) { m.noise.fill (0.0f); m.dn.fill (0.0f); }
    auto noise = std::make_shared<nb::MaterialNoise>();
    noise->prepare (fs);
    static nb::MaterialBody body;
    body.prepare (fs, noise);
    const auto t0 = std::chrono::steady_clock::now();
    body.start (m, play);
    std::vector<float> L ((size_t) samples), R ((size_t) samples);
    for (int i = 0; i < samples; ++i) body.process (L[(size_t) i], R[(size_t) i]);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    writeWavFloat (argv[2], L, R, (int) fs);
    std::printf ("%s: %d modes, %.2f s in %.1f ms (%.1f%% of a core)\n", m.name.c_str(), m.modes(), samples / fs, secs * 1000,
                 100.0 * secs / (samples / fs));
    return 0;
}
