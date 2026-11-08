// Render NeoBeat's Wave-style voice with settings matching waveref (measurement only).
#include "../Source/dsp/WaveVoice.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
int main (int argc, char** argv)
{
    const float sr = 48000.0f;
    const float pos = (float) std::atof (argv[2]), drive = (float) std::atof (argv[3]);
    nb::WaveTable tab = nb::tableHollow();
    nb::WaveVoice v; v.prepare (sr);
    nb::WaveVoiceSpec s;
    s.pos = pos; s.scan = 0.0f; s.detuneCents = 7.0f; s.level1 = 0.55f; s.level2 = 0.45f;
    s.wenv.times = { 0.5f, 0, 0, 0, 0, 0, 0, 0 }; s.wenv.levels = { 0, 0, 0, 0, 0, 0, 0, 0 }; s.wenv.keyoff = 0;
    s.cutoff = 900.0f; s.res = 0.3f; s.fenvAmount = 30.0f / 12.0f; s.fa = 0.002f; s.fd = 0.3f; s.fs = 0.2f; s.fr = 0.3f;
    s.aa = 0.002f; s.ad = 0.5f; s.as = 0.4f; s.ar = 0.25f; s.drive = std::pow (10.0f, drive / 20.0f);
    v.start (&tab, s, 65.406f, 65.406f, 0.0f, 1.4f, 0.7f, 7u);
    const int n = (int) (1.4f * sr);
    FILE* f = std::fopen (argv[1], "wb");
    std::vector<float> x (n);
    for (int i = 0; i < n; ++i) x[i] = v.process();
    std::fwrite (x.data(), 4, n, f); std::fclose (f);
}
