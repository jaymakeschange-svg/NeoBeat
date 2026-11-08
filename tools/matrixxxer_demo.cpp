// The Matrixxxer's cells at work, offline.
//   matrixxxer_demo oneshot <out.wav>   Swingbeat Classic (MaterialLab kit, 4 bars): chosen hits through cell effects
//   matrixxxer_demo sample  <out.wav>   the same 2-bar loop cut into 32 cells, every cell through a different effect
#include "../Source/engine/Engine.h"
#include "../Source/engine/Presets.h"
#include "../Source/engine/Kits.h"
#include "../Source/engine/MatrixxxerFx.h"
#include <cstdio>
#include <map>
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

struct Hit { double t; int pad; float vel; int nth; };

int main (int argc, char** argv)
{
    if (argc < 3) return 1;
    const std::string mode = argv[1];
    const auto& pr = nb::presets()[0];
    const double bpm = pr.tempo;
    auto e = std::make_unique<nb::Engine>(); e->prepare (kFs); nb::applyKit (*e, nb::kits()[3]);
    const auto& rubber = e->lib().tables.rubbers[0];
    const auto& roll = e->lib().tables.bodies[5];
    if (mode == "oneshot")
    {
        // which hit of which pad gets which cell (1-based count of that pad's hits)
        std::map<std::pair<int, int>, nb::CellFx> cells;
        nb::StretchParams st; st.stretch = 8; st.gateBeats = 2.0; st.decayMs = 700;
        nb::BounceParams bd; bd.rate.beats = 0.5; bd.panRandom = 0.8f; bd.distEnd = 8;
        cells[{ 2, 2 }] = nb::CellFx::library (nb::MatrixFx::Freezing);
        cells[{ 2, 2 }].settings.p[1] = 1.2f;                          // a short freeze tail
        cells[{ 2, 4 }] = nb::CellFx::library (nb::MatrixFx::Harmonizer);
        cells[{ 2, 7 }] = nb::CellFx::library (nb::MatrixFx::GlidingFilters);
        cells[{ 2, 9 }] = nb::CellFx::friction (st);
        cells[{ 0, 5 }] = nb::CellFx::library (nb::MatrixFx::PitchShift);
        cells[{ 0, 5 }].settings.p[0] = -7;
        cells[{ 7, 1 }] = nb::CellFx::bouncing (bd);
        cells[{ 7, 3 }] = nb::CellFx::library (nb::MatrixFx::Pulsing);
        cells[{ 5, 2 }] = nb::CellFx::library (nb::MatrixFx::Shimmer);
        cells[{ 5, 3 }] = nb::CellFx::library (nb::MatrixFx::GrainStreamer);
        // the groove, with the cells' hits left out (they are rendered on their own)
        nb::Sequencer seq; seq.prepare (kFs); seq.setPattern (pr.pattern);
        const double ppqLen = 16.0; const size_t n = (size_t) ((ppqLen * 60.0 / bpm + 4.0) * kFs);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        std::vector<nb::SeqEvent> ev; ev.reserve (1024);
        std::map<int, int> count; std::vector<Hit> cellHits;
        for (size_t pos = 0; pos < n; pos += 256)
        {
            const int len = (int) std::min<size_t> (256, n - pos);
            const double ppq0 = (double) pos / kFs * bpm / 60.0;
            ev.clear(); seq.collect (ppq0, bpm, len, ppq0 < ppqLen, pr.swing, 0.0f, ev);
            int done = 0;
            for (const auto& x : ev)
            {
                if (x.offset > done) { e->render (L.data() + pos + done, R.data() + pos + done, x.offset - done); done = x.offset; }
                const int nth = ++count[x.pad];
                if (cells.count ({ x.pad, nth })) cellHits.push_back ({ (double) (pos + (size_t) x.offset) / kFs, x.pad, x.velocity, nth });
                else e->trigger (x.pad, x.velocity);
            }
            if (len > done) e->render (L.data() + pos + done, R.data() + pos + done, len - done);
        }
        // each cell's hit, captured alone and processed
        auto cap = std::make_unique<nb::Engine>(); cap->prepare (kFs); nb::applyKit (*cap, nb::kits()[3]);
        for (const auto& h : cellHits)
        {
            std::vector<float> hl ((size_t) (2.0f * kFs), 0.0f), hr = hl, tmp (512);
            for (int g = 0; g < 400 && cap->activeVoices() > 0; ++g) { std::vector<float> a (512), b (512); cap->render (a.data(), b.data(), 512); }
            cap->trigger (h.pad, h.vel); cap->render (hl.data(), hr.data(), (int) hl.size());
            std::vector<float> ol, orr;
            nb::renderCell (cells[{ h.pad, h.nth }], hl.data(), hr.data(), (int) hl.size(), kFs, bpm, h.t, &rubber, &roll, ol, orr);
            const size_t s = (size_t) (h.t * kFs);
            for (size_t i = 0; i < ol.size() && s + i < n; ++i) { L[s + i] += ol[i]; R[s + i] += orr[i]; }
            std::printf ("pad %2d hit %d at %.2f s: %s\n", h.pad, h.nth, h.t,
                         cells[{ h.pad, h.nth }].kind == nb::CellFx::Library ? nb::matrixFxInfo()[(size_t) cells[{ h.pad, h.nth }].fx].name
                         : (cells[{ h.pad, h.nth }].kind == nb::CellFx::FrictionStretchFx ? "Friction Stretch" : "Bounce Delay"));
        }
        for (size_t i = 0; i < n; ++i) { L[i] = nb::Engine::safety (L[i]); R[i] = nb::Engine::safety (R[i]); }
        writeWav (argv[2], L, R);
    }
    else if (mode == "sample")
    {
        // two bars of the groove, dry
        nb::Sequencer seq; seq.prepare (kFs); seq.setPattern (pr.pattern);
        const double ppqLen = 8.0; const size_t n = (size_t) (ppqLen * 60.0 / bpm * kFs);
        std::vector<float> L (n, 0.0f), R (n, 0.0f);
        std::vector<nb::SeqEvent> ev; ev.reserve (1024);
        for (size_t pos = 0; pos < n; pos += 256)
        {
            const int len = (int) std::min<size_t> (256, n - pos);
            ev.clear(); seq.collect ((double) pos / kFs * bpm / 60.0, bpm, len, true, pr.swing, 0.0f, ev);
            int done = 0;
            for (const auto& x : ev) { if (x.offset > done) { e->render (L.data() + pos + done, R.data() + pos + done, x.offset - done); done = x.offset; } e->trigger (x.pad, x.velocity); }
            if (len > done) e->render (L.data() + pos + done, R.data() + pos + done, len - done);
        }
        // 32 cells; each through its own effect, kept to its slot (with a short crossfade into the next)
        const int cellsN = 32; const size_t slot = n / cellsN, fade = (size_t) (0.004f * kFs);
        std::vector<float> oL (n + (size_t) (2 * kFs), 0.0f), oR = oL;
        for (int c = 0; c < cellsN; ++c)
        {
            const size_t s0 = (size_t) c * slot;
            nb::CellFx cell;
            const int k = c % 26;
            if (k < 24) cell = nb::CellFx::library ((nb::MatrixFx) k);
            else if (k == 24) { nb::StretchParams st; st.stretch = 4; cell = nb::CellFx::friction (st); }
            else { nb::BounceParams bd; bd.rate.beats = 0.25; bd.panRandom = 1.0f; cell = nb::CellFx::bouncing (bd); }
            for (auto& v : cell.settings.p) (void) v;
            if (cell.kind == nb::CellFx::Library)
            {   // the long-tail effects keep to their slot here
                auto& p = cell.settings.p;
                const auto& info = nb::matrixFxInfo()[(size_t) k].params;
                for (size_t q = 0; q < info.size(); ++q) if (std::string (info[q].name) == "tail" || std::string (info[q].name) == "length") p[q] = 0.3f;
            }
            std::vector<float> ol, orr;
            nb::renderCell (cell, L.data() + s0, R.data() + s0, (int) slot, kFs, bpm, (double) s0 / kFs, &rubber, &roll, ol, orr);
            const size_t keep = std::min (ol.size(), slot + fade);
            for (size_t i = 0; i < keep; ++i)
            {
                float w = 1.0f;
                if (i < fade) w = (float) i / (float) fade;
                if (i > slot) w = (float) (slot + fade - i) / (float) fade;
                oL[s0 + i] += w * ol[i]; oR[s0 + i] += w * orr[i];
            }
            std::printf ("cell %2d: %s\n", c + 1, k < 24 ? nb::matrixFxInfo()[(size_t) k].name : (k == 24 ? "Friction Stretch" : "Bounce Delay"));
        }
        oL.resize (n); oR.resize (n);
        writeWav (argv[2], oL, oR);
        writeWav (std::string (argv[2]) + ".dry.wav", L, R);
    }
    return 0;
}
