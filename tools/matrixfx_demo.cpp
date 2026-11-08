// Previews of the Matrixxxer's 24 effects, offline: each on a single hit and on a two-bar groove.
//   matrixfx_demo <outdir> [fx index | all] [key=value overrides as p0=.. p1=..]
// Writes <id>_hit.wav (the dry hit, then the processed hit) and <id>_loop.wav (the processed loop),
// plus dry_loop.wav once.
#include "../Source/engine/Engine.h"
#include "../Source/engine/Presets.h"
#include "../Source/engine/Kits.h"
#include "../Source/dsp/MatrixFx.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static constexpr float kFs = 48000.0f;
static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R)
{
    FILE* f = std::fopen (path.c_str(), "wb"); if (! f) return;
    const uint32_t frames = (uint32_t) L.size(), bytes = frames * 8;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); }; auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVE", 1, 4, f); std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (3); u16 (2);
    u32 ((uint32_t) kFs); u32 ((uint32_t) kFs * 8); u16 (8); u16 (32); std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < frames; ++i) { std::fwrite (&L[i], 4, 1, f); std::fwrite (&R[i], 4, 1, f); }
    std::fclose (f);
}
static void trimTail (std::vector<float>& L, std::vector<float>& R)
{
    float pk = 1e-9f; for (size_t i = 0; i < L.size(); ++i) pk = std::max (pk, std::max (std::fabs (L[i]), std::fabs (R[i])));
    size_t end = L.size(); while (end > 1 && std::max (std::fabs (L[end - 1]), std::fabs (R[end - 1])) < pk * 3e-4f) --end;
    end = std::min (L.size(), end + (size_t) (0.1f * kFs)); L.resize (end); R.resize (end);
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "matrixfx_demo <outdir> [index|all] [pN=value ...]\n"); return 1; }
    const std::string dir = argv[1];
    const std::string which = argc > 2 ? argv[2] : "all";
    // the sources: the MaterialLab clank, and two bars of Swingbeat Classic on the MaterialLab kit
    std::vector<float> hL, hR, lL, lR;
    {
        auto e = std::make_unique<nb::Engine>(); e->prepare (kFs); nb::applyKit (*e, nb::kits()[3]);
        e->trigger (9, 1.0f);
        hL.assign ((size_t) (2.5f * kFs), 0.0f); hR = hL;
        e->render (hL.data(), hR.data(), (int) hL.size());
        trimTail (hL, hR);
    }
    {
        auto e = std::make_unique<nb::Engine>(); e->prepare (kFs); nb::applyKit (*e, nb::kits()[3]);
        const auto& pr = nb::presets()[0];
        nb::Sequencer seq; seq.prepare (kFs); seq.setPattern (pr.pattern);
        for (int pad = 0; pad < nb::Engine::kPads; ++pad) seq.setPreroll (pad, e->prerollSeconds (pad));
        const double ppqLen = 8.0; const size_t n = (size_t) (ppqLen * 60.0 / pr.tempo * kFs);
        lL.assign (n, 0.0f); lR = lL;
        std::vector<nb::SeqEvent> ev; ev.reserve (1024);   // collect() never allocates
        for (size_t pos = 0; pos < n; pos += 256)
        {
            const int len = (int) std::min<size_t> (256, n - pos);
            ev.clear(); seq.collect ((double) pos / kFs * pr.tempo / 60.0, pr.tempo, len, true, pr.swing, 0.0f, ev);
            int done = 0;
            for (const auto& x : ev) { if (x.offset > done) { e->render (lL.data() + pos + done, lR.data() + pos + done, x.offset - done); done = x.offset; } e->trigger (x.pad, x.velocity); }
            if (len > done) e->render (lL.data() + pos + done, lR.data() + pos + done, len - done);
        }
        writeWav (dir + "/dry_loop.wav", lL, lR);
    }
    const auto& info = nb::matrixFxInfo();
    nb::MatrixFxEngine eng;
    for (int fx = 0; fx < (int) nb::MatrixFx::Count; ++fx)
    {
        if (which != "all" && std::atoi (which.c_str()) != fx) continue;
        auto S = nb::FxSettings::defaults ((nb::MatrixFx) fx);
        for (int a = 3; a < argc; ++a) if (argv[a][0] == 'p') { const char* eq = std::strchr (argv[a], '='); if (eq) { const int k = std::atoi (argv[a] + 1); if (k >= 0 && k < (int) S.p.size()) S.p[(size_t) k] = (float) std::atof (eq + 1); } }
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<float> oL, oR;
        eng.process ((nb::MatrixFx) fx, S, hL.data(), hR.data(), (int) hL.size(), kFs, oL, oR);
        trimTail (oL, oR);
        // the dry hit, a breath, then the processed hit
        std::vector<float> L (hL), R (hR);
        L.resize (L.size() + (size_t) (0.4f * kFs), 0.0f); R.resize (L.size(), 0.0f);
        L.insert (L.end(), oL.begin(), oL.end()); R.insert (R.end(), oR.begin(), oR.end());
        writeWav (dir + "/" + info[(size_t) fx].id + "_hit.wav", L, R);
        eng.process ((nb::MatrixFx) fx, S, lL.data(), lR.data(), (int) lL.size(), kFs, oL, oR);
        trimTail (oL, oR);
        writeWav (dir + "/" + info[(size_t) fx].id + "_loop.wav", oL, oR);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("%2d %-26s %5.1f s to render (hit and loop)\n", fx, info[(size_t) fx].name, secs);
    }
    return 0;
}
