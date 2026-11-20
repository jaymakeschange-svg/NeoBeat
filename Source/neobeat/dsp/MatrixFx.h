// NeoBeat — the Matrixxxer's effect library: 24 spectral and time-domain effects, each one usable on a
// One Shot cell's hit, on a Sample cell's slice, and as a set of mod-matrix destinations.
//
// The list (and what each does) follows the descriptions in Michael Norris's Soundmagic Spectral guide;
// the implementations are NeoBeat's own. Spectral effects share one phase-vocoder engine: every frame is
// a magnitude and a true frequency per bin; an effect rewrites those (moving energy to new bins when it
// moves frequencies), and the engine turns them back into sound, each bin's phase turning at its own
// frequency. Everything is offline (a hit or a slice in, the processed sound out, tails included).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace nb
{
enum class MatrixFx
{
    // smoothing
    Averaging, Blurring, DroneMaker,
    // sustaining
    Freezing, GateHold, Weave,
    // excitement
    Shimmer, Shuffle, Pulsing,
    // pitch
    Harmonizer, BinShift, PitchShift, Stretch,
    // filtering
    Tracing, Filterbank, GlidingFilters,
    // texture
    Granulation,
    // partials
    Emergence, PartialGlide,
    // time domain
    IdeeFixer, Chorus, CombFilterbank, GrainStreamer, SuperFilterbank,
    Count
};

struct FxParamInfo { const char* name; float lo, hi, def; const char* unit; };
struct FxInfo
{
    const char* id; const char* name; const char* category; const char* what;
    std::vector<FxParamInfo> params;
};

// Every effect's controls: the names, ranges and defaults the cells, the UI and the mod matrix use.
inline const std::vector<FxInfo>& matrixFxInfo()
{
    static const std::vector<FxInfo> info = {
        { "averaging", "Spectral Averaging", "Smoothing", "Each frequency is averaged over the last few frames: fast changes settle.",
          { { "frames", 1, 64, 12, "" }, { "mix", 0, 1, 1, "" } } },
        { "blurring", "Spectral Blurring", "Smoothing", "Changes smear from frame to frame, so the detail melts away.",
          { { "blur", 10, 5000, 400, "ms" }, { "mix", 0, 1, 1, "" }, { "tail", 0, 20, 2, "s" } } },
        { "dronemaker", "Spectral DroneMaker", "Smoothing", "Any sound into a slowly turning drone, through a moving comb, with a gate on the faint bins.",
          { { "smoothing", 0.2f, 10, 2.5f, "s" }, { "comb", 0, 1000, 110, "Hz" }, { "comb amount", 0, 1, 0.6f, "" }, { "gate", -90, 0, -55, "dB" },
            { "flange rate", 0, 2, 0.15f, "Hz" }, { "tail", 0, 30, 8, "s" } } },
        { "freezing", "Spectral Freezing", "Sustaining", "Each frequency holds its loudest moment until a louder one replaces it, then fades.",
          { { "decay", 0, 60, 4, "dB/s" }, { "tail", 0, 30, 6, "s" }, { "mix", 0, 1, 1, "" } } },
        { "gatehold", "Spectral Gate and Hold", "Sustaining", "Frequencies that cross a threshold are frozen for a while, faded in and out, with random hold times.",
          { { "threshold", -60, 0, -24, "dB" }, { "hold", 20, 4000, 700, "ms" }, { "fade", 1, 500, 60, "ms" }, { "decay", 0, 40, 4, "dB/s" },
            { "random", 0, 1, 0.6f, "" }, { "tail", 0, 20, 4, "s" } } },
        { "weave", "Spectral Weave", "Sustaining", "Snapshots of the spectrum are saved and faded in and out of each other in waves.",
          { { "snapshot", 20, 1000, 140, "ms" }, { "snapshots", 2, 8, 4, "" }, { "rate", 0.05f, 4, 0.8f, "Hz" }, { "live", 0, 1, 0.35f, "" }, { "tail", 0, 20, 6, "s" } } },
        { "shimmer", "Spectral Shimmer", "Excitement", "Every frequency jitters and pulses, and the top sizzles.",
          { { "jitter", 0, 100, 18, "cents" }, { "pulse depth", 0, 1, 0.6f, "" }, { "pulse rate", 0.5f, 30, 9, "Hz" }, { "sizzle", 0, 24, 9, "dB" }, { "sizzle from", 1000, 12000, 4500, "Hz" } } },
        { "shuffle", "Spectral Shuffle", "Excitement", "Blocks of frequencies swap places across the spectrum.",
          { { "amount", 0, 100, 40, "%" }, { "range", 1, 512, 96, "bins" }, { "group", 1, 64, 6, "bins" }, { "reshuffle", 5, 1000, 60, "ms" } } },
        { "pulsing", "Spectral Pulsing", "Excitement", "Frequencies switch on and off in patterns that stretch with pitch and with time.",
          { { "on", 5, 1000, 70, "ms" }, { "off", 5, 1000, 110, "ms" }, { "on per octave", 0.25f, 4, 0.8f, "x" }, { "off per octave", 0.25f, 4, 1.25f, "x" },
            { "per second", 0.25f, 4, 1.3f, "x" }, { "wrap", 0, 1, 0, "" } } },
        { "harmonizer", "Spectral Harmonizer", "Pitch", "Up to three transposed copies mixed back in.",
          { { "shift 1", -24, 24, 7, "st" }, { "shift 2", -24, 24, -12, "st" }, { "shift 3", -24, 24, 19, "st" }, { "level 1", 0, 1, 0.7f, "" },
            { "level 2", 0, 1, 0.55f, "" }, { "level 3", 0, 1, 0.35f, "" }, { "dry", 0, 1, 0.8f, "" } } },
        { "binshift", "Spectral Bin Shift", "Pitch", "The whole spectrum slides by a number of bins: every partial moves by the same Hz, so harmony breaks.",
          { { "shift", -200, 200, 18, "bins" }, { "mix", 0, 1, 1, "" } } },
        { "pitchshift", "Spectral Pitch Shift", "Pitch", "Each peak moves on its own, by an interval or by bins, keeping its shape.",
          { { "interval", -24, 24, 5, "st" }, { "by bins", 0, 1, 0, "" }, { "bins", -200, 200, 30, "bins" }, { "threshold", -90, 0, -60, "dB" } } },
        { "stretch", "Spectral Stretch", "Pitch", "Partials stretched inharmonically: each moves by beta x f + alpha x f squared, beta can wander.",
          { { "alpha", -1, 1, 0.18f, "" }, { "beta", -1, 1, 0.0f, "" }, { "variance", 0, 1, 0.3f, "" }, { "variance type", 0, 3, 1, "" },
            { "variance rate", 0.1f, 20, 3, "Hz" }, { "quantize", 0, 1, 0, "" } } },
        { "tracing", "Spectral Tracing", "Filtering", "Keeps (or removes) frequencies by how loud they rank, by a range, or by a threshold.",
          { { "mode", 0, 2, 0, "" }, { "keep", 1, 512, 18, "bins" }, { "invert", 0, 1, 0, "" }, { "low", 20, 20000, 300, "Hz" }, { "high", 20, 20000, 4000, "Hz" },
            { "threshold", -80, 0, -30, "dB" } } },
        { "filterbank", "Spectral Filterbank", "Filtering", "A bank of razor-thin band-pass filters on a harmonic series or a scale.",
          { { "base", 20, 2000, 98, "Hz" }, { "interval", 0, 12, 0, "st" }, { "count", 1, 64, 28, "" }, { "width", 1, 200, 14, "cents" }, { "detune", 0, 100, 0, "cents" } } },
        { "glidingfilters", "Spectral Gliding Filters", "Filtering", "Filters that drift in pitch for a random lifetime, with silences between.",
          { { "count", 1, 16, 7, "" }, { "width", 5, 300, 45, "cents" }, { "glide", 0, 4, 1.2f, "oct/s" }, { "life", 50, 5000, 700, "ms" }, { "gap", 0, 3000, 250, "ms" },
            { "low", 40, 4000, 180, "Hz" }, { "high", 200, 16000, 7000, "Hz" } } },
        { "granulation", "Spectral Granulation", "Texture", "The spectrum cut into grains of time and frequency, each delayed, shifted and faded on its own.",
          { { "grain", 10, 500, 90, "ms" }, { "bands", 1, 32, 10, "" }, { "delay", 0, 2000, 450, "ms" }, { "shift", 0, 12, 0, "st" }, { "density", 0, 1, 0.75f, "" }, { "tail", 0, 10, 1.5f, "s" } } },
        { "emergence", "Spectral Emergence", "Partials", "Each partial pulses with its own envelope, differently in left and right.",
          { { "rate", 0.2f, 20, 3.5f, "Hz" }, { "spread", 0, 1, 0.7f, "" }, { "depth", 0, 1, 0.95f, "" }, { "stereo", 0, 1, 1, "" }, { "threshold", -90, 0, -60, "dB" } } },
        { "partialglide", "Spectral Partial Glide", "Partials", "Each partial glides in pitch at its own rate, some up and some down.",
          { { "rate", 0, 24, 5, "st/s" }, { "spread", 0, 1, 1, "" }, { "direction", -1, 1, 0, "" }, { "limit", 0, 36, 12, "st" }, { "threshold", -90, 0, -60, "dB" } } },
        { "ideefixer", "Idee Fixer", "Time domain", "A big granular brassage: the sound is caught in a buffer and played back as a cloud of grains.",
          { { "start", 0, 1, 0.08f, "" }, { "scatter", 0, 1, 0.25f, "" }, { "grain", 5, 500, 70, "ms" }, { "spacing", 2, 200, 22, "ms" }, { "transpose", -24, 24, 0, "st" },
            { "transpose random", 0, 12, 3, "st" }, { "length", 0.5f, 30, 6, "s" } } },
        { "chorus", "Chorus", "Time domain", "A moving chorus and flanger.",
          { { "rate", 0.05f, 8, 0.7f, "Hz" }, { "depth", 0, 20, 5, "ms" }, { "delay", 1, 40, 11, "ms" }, { "feedback", -0.95f, 0.95f, 0.35f, "" }, { "voices", 1, 4, 3, "" }, { "mix", 0, 1, 0.6f, "" } } },
        { "combfilterbank", "Comb Filterbank", "Time domain", "A set of ringing comb filters, tuned in a chord.",
          { { "base", 20, 2000, 98, "Hz" }, { "count", 1, 12, 5, "" }, { "interval", 0, 12, 7, "st" }, { "feedback", 0, 0.995f, 0.93f, "" }, { "damping", 0, 1, 0.3f, "" },
            { "mix", 0, 1, 0.7f, "" }, { "tail", 0, 10, 2, "s" } } },
        { "grainstreamer", "Grain Streamer", "Time domain", "Grains are caught from the sound and repeated in streams, each repeat softer (and optionally shifted).",
          { { "grain", 5, 300, 45, "ms" }, { "repeats", 1, 32, 10, "" }, { "stream", 5, 500, 55, "ms" }, { "capture", 10, 1000, 140, "ms" }, { "decay", 0, 1, 0.82f, "" },
            { "transpose", -12, 12, 0, "st" }, { "tail", 0, 10, 3, "s" } } },
        { "superfilterbank", "Super Filterbank", "Time domain", "A bank of resonant band-pass filters across the spectrum.",
          { { "base", 20, 2000, 70, "Hz" }, { "count", 1, 32, 14, "" }, { "spacing", 1, 12, 5, "st" }, { "q", 1, 200, 45, "" }, { "tilt", -6, 6, 0, "dB/oct" }, { "mix", 0, 1, 1, "" } } },
    };
    return info;
}

struct FxSettings
{
    std::vector<float> p;     // one per parameter, in matrixFxInfo() order
    uint32_t seed = 1;
    static FxSettings defaults (MatrixFx fx)
    {
        FxSettings s; for (const auto& q : matrixFxInfo()[(size_t) fx].params) s.p.push_back (q.def); return s;
    }
};

// ------------------------------------------------------------------------------------------ engine
class MatrixFxEngine
{
public:
    static constexpr int kN = 2048, kBins = kN / 2 + 1, kHop = 256;

    // in: stereo (L, R), n samples. out: resized to the processed length (tails included), as loud as the input.
    void process (MatrixFx fx, const FxSettings& S, const float* inL, const float* inR, int n, float fs,
                  std::vector<float>& outL, std::vector<float>& outR)
    {
        processRaw (fx, S, inL, inR, n, fs, outL, outR);
        autoLevel (inL, inR, n, outL, outR);
    }
    void processRaw (MatrixFx fx, const FxSettings& S, const float* inL, const float* inR, int n, float fs,
                     std::vector<float>& outL, std::vector<float>& outR)
    {
        sr = fs; P = S.p; seed = S.seed; lastInSec = (double) n / fs;
        if (P.size() < matrixFxInfo()[(size_t) fx].params.size()) { const auto d = FxSettings::defaults (fx); P = d.p; }
        prepareFft();
        switch (fx)
        {
            case MatrixFx::IdeeFixer:       ideeFixer (inL, inR, n, outL, outR); return;
            case MatrixFx::Chorus:          chorus (inL, inR, n, outL, outR); return;
            case MatrixFx::CombFilterbank:  combs (inL, inR, n, outL, outR); return;
            case MatrixFx::GrainStreamer:   grainStreamer (inL, inR, n, outL, outR); return;
            case MatrixFx::SuperFilterbank: superFilterbank (inL, inR, n, outL, outR); return;
            default: break;
        }
        // spectral: the tail each effect rings on for
        float tail = 0.0f;
        switch (fx)
        {
            case MatrixFx::Blurring: tail = P[2]; break;
            case MatrixFx::DroneMaker: tail = P[5]; break;
            case MatrixFx::Freezing: tail = P[1]; break;
            case MatrixFx::GateHold: tail = P[5]; break;
            case MatrixFx::Weave: tail = P[4]; break;
            case MatrixFx::Granulation: tail = P[5] + P[2] / 1000.0f; break;
            default: tail = 0.1f; break;
        }
        spectral (fx, inL, inR, n, (int) (tail * fs), outL, outR);
    }

    // As loud as what went in (over the input's own length; tails ring on at that level), within +-18 dB.
    static void autoLevel (const float* inL, const float* inR, int n, std::vector<float>& outL, std::vector<float>& outR)
    {
        double ei = 0, eo = 0;
        for (int i = 0; i < n; ++i) ei += (double) inL[i] * inL[i] + (double) inR[i] * inR[i];
        const int m = std::min (n, (int) outL.size());
        for (int i = 0; i < m; ++i) eo += (double) outL[(size_t) i] * outL[(size_t) i] + (double) outR[(size_t) i] * outR[(size_t) i];
        if (eo < 1e-20 || ei < 1e-20) return;
        double g = std::min (7.94, std::max (0.126, std::sqrt (ei / eo)));
        float pk = 1e-9f; for (size_t i = 0; i < outL.size(); ++i) pk = std::max (pk, std::max (std::fabs (outL[i]), std::fabs (outR[i])));
        g = std::min (g, 0.95 / pk);
        for (size_t i = 0; i < outL.size(); ++i) { outL[i] *= (float) g; outR[i] *= (float) g; }
    }

private:
    // ---------------------------------------------------------------- FFT
    void prepareFft()
    {
        if (! tw.empty()) return;
        tw.resize (kN / 2); for (int k = 0; k < kN / 2; ++k) tw[(size_t) k] = std::polar (1.0f, -6.2831853f * (float) k / kN);
        rev.resize (kN); for (int i = 0, j = 0; i < kN; ++i) { rev[(size_t) i] = j; int bit = kN >> 1; while (j & bit) { j ^= bit; bit >>= 1; } j |= bit; }
        win.resize (kN); for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / kN);
    }
    void fft (std::vector<std::complex<float>>& a, bool inverse) const
    {
        for (int i = 0; i < kN; ++i) if (i < rev[(size_t) i]) std::swap (a[(size_t) i], a[(size_t) rev[(size_t) i]]);
        for (int len = 2; len <= kN; len <<= 1)
        {
            const int step = kN / len;
            for (int i = 0; i < kN; i += len)
                for (int j = 0; j < len / 2; ++j)
                {
                    std::complex<float> w = tw[(size_t) (j * step)]; if (inverse) w = std::conj (w);
                    const auto u = a[(size_t) (i + j)], v = a[(size_t) (i + j + len / 2)] * w;
                    a[(size_t) (i + j)] = u + v; a[(size_t) (i + j + len / 2)] = u - v;
                }
        }
        if (inverse) for (auto& v : a) v /= (float) kN;
    }
    static float princarg (float p) noexcept { return p - 6.2831853f * std::floor ((p + 3.14159265f) / 6.2831853f); }
    float binHz() const noexcept { return sr / kN; }

    // ---------------------------------------------------------------- the spectral frame loop
    struct Frame { std::vector<float> mag, freq; };
    void deposit (Frame& o, float f, float m) const
    {
        const int k = (int) std::lround (f / binHz());
        if (k <= 0 || k >= kBins - 1 || m <= 0.0f) return;
        if (m > o.mag[(size_t) k]) o.freq[(size_t) k] = f;
        o.mag[(size_t) k] += m;
    }

    void spectral (MatrixFx fx, const float* inL, const float* inR, int n, int tailN, std::vector<float>& outL, std::vector<float>& outR)
    {
        const int total = n + tailN;
        const int frames = total / kHop + kN / kHop + 1;
        outL.assign ((size_t) total + kN, 0.0f); outR.assign ((size_t) total + kN, 0.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = ch == 0 ? inL : inR;
            std::vector<float>& out = ch == 0 ? outL : outR;
            std::vector<float> lastPh ((size_t) kBins, 0.0f), synPh ((size_t) kBins, 0.0f);
            std::vector<std::complex<float>> buf ((size_t) kN);
            state = {}; state.ch = ch;
            rng.seed (seed * 7919u + 13u);   // the same randomness in both channels unless an effect asks otherwise
            Frame a { std::vector<float> ((size_t) kBins), std::vector<float> ((size_t) kBins) };
            for (int f = 0; f < frames; ++f)
            {
                const int start = f * kHop - kN / 2;
                for (int i = 0; i < kN; ++i) { const int s = start + i; buf[(size_t) i] = { s >= 0 && s < n ? in[s] * win[(size_t) i] : 0.0f, 0.0f }; }
                fft (buf, false);
                for (int b = 0; b < kBins; ++b)
                {
                    const float m = std::abs (buf[(size_t) b]), ph = std::arg (buf[(size_t) b]);
                    const float expect = 6.2831853f * (float) b * kHop / kN;
                    const float dev = princarg (ph - lastPh[(size_t) b] - expect);
                    lastPh[(size_t) b] = ph;
                    a.mag[(size_t) b] = m;
                    a.freq[(size_t) b] = ((float) b + dev * kN / (6.2831853f * kHop)) * binHz();
                }
                const double t = (double) f * kHop / sr;
                Frame o { std::vector<float> ((size_t) kBins, 0.0f), std::vector<float> ((size_t) kBins, 0.0f) };
                processFrame (fx, a, o, t, start >= n);
                for (int b = 0; b < kBins; ++b)
                {
                    synPh[(size_t) b] = princarg (synPh[(size_t) b] + 6.2831853f * o.freq[(size_t) b] * kHop / sr);
                    buf[(size_t) b] = std::polar (o.mag[(size_t) b], synPh[(size_t) b]);
                }
                for (int b = 1; b < kBins - 1; ++b) buf[(size_t) (kN - b)] = std::conj (buf[(size_t) b]);
                buf[0] = { buf[0].real(), 0.0f }; buf[(size_t) (kBins - 1)] = { buf[(size_t) (kBins - 1)].real(), 0.0f };
                fft (buf, true);
                for (int i = 0; i < kN; ++i) { const int s = start + i; if (s >= 0 && s < (int) out.size()) out[(size_t) s] += buf[(size_t) i].real() * win[(size_t) i] / 3.0f; }
            }
            out.resize ((size_t) total);
        }
    }

    // per-channel state the frame effects keep
    struct State
    {
        int ch = 0;
        std::vector<std::vector<float>> histM, histF;   // averaging
        std::vector<float> m1, f1, m2, f2, timer, hold;  // generic per-bin state
        std::vector<Frame> snaps; double lastSnap = -1e9;
        std::vector<int> perm; double lastShuffle = -1e9;
        std::vector<float> phase, rate;
        std::vector<std::array<float, 6>> filt;            // gliding filters
        std::vector<std::vector<float>> past;              // granulation: past frames' mags
        std::vector<std::vector<float>> pastF;
        int frameNo = 0;
    } state;
    std::mt19937 rng { 1u };
    float uni() { return std::uniform_real_distribution<float> (0.0f, 1.0f) (rng); }
    float gauss() { return std::normal_distribution<float> (0.0f, 1.0f) (rng); }
    static uint32_t hash (uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
    static float hash01 (uint32_t x) { return (float) (hash (x) & 0xffffff) / 16777216.0f; }

    // peaks: each bin's region (the peak it belongs to), for the partial effects
    std::vector<int> peakOf (const Frame& a, float thresholdDb) const
    {
        std::vector<int> owner ((size_t) kBins, -1);
        float pk = 1e-12f; for (float m : a.mag) pk = std::max (pk, m);
        const float thr = pk * std::pow (10.0f, thresholdDb / 20.0f);
        std::vector<int> peaks;
        for (int b = 2; b < kBins - 2; ++b)
            if (a.mag[(size_t) b] > thr && a.mag[(size_t) b] >= a.mag[(size_t) b - 1] && a.mag[(size_t) b] > a.mag[(size_t) b + 1]) peaks.push_back (b);
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            int lo = peaks[i], hi = peaks[i];
            while (lo > 1 && a.mag[(size_t) lo - 1] < a.mag[(size_t) lo] && (i == 0 || lo - 1 > peaks[i - 1])) --lo;
            while (hi < kBins - 2 && a.mag[(size_t) hi + 1] < a.mag[(size_t) hi] && (i + 1 == peaks.size() || hi + 1 < peaks[i + 1])) ++hi;
            for (int b = lo; b <= hi; ++b) owner[(size_t) b] = peaks[i];
        }
        return owner;
    }

    void ensure (std::vector<float>& v, float init = 0.0f) const { if ((int) v.size() != kBins) v.assign ((size_t) kBins, init); }

    void processFrame (MatrixFx fx, const Frame& a, Frame& o, double t, bool after)
    {
        auto& s = state;
        const float hopS = (float) kHop / sr;
        const float bh = binHz();
        switch (fx)
        {
            case MatrixFx::Averaging:
            {
                const int N = std::max (1, (int) P[0]);
                s.histM.push_back (a.mag); s.histF.push_back (a.freq);
                while ((int) s.histM.size() > N) { s.histM.erase (s.histM.begin()); s.histF.erase (s.histF.begin()); }
                for (int b = 0; b < kBins; ++b)
                {
                    float m = 0, f = 0, w = 0;
                    for (size_t k = 0; k < s.histM.size(); ++k) { m += s.histM[k][(size_t) b]; f += s.histF[k][(size_t) b] * s.histM[k][(size_t) b]; w += s.histM[k][(size_t) b]; }
                    m /= (float) s.histM.size();
                    o.mag[(size_t) b] = P[1] * m + (1 - P[1]) * a.mag[(size_t) b];
                    o.freq[(size_t) b] = w > 0 ? f / w : a.freq[(size_t) b];
                }
                break;
            }
            case MatrixFx::Blurring:
            {
                ensure (s.m1); ensure (s.f1);
                const float g = 1.0f - std::exp (-hopS / std::max (0.005f, P[0] / 1000.0f));
                for (int b = 0; b < kBins; ++b)
                {
                    s.m1[(size_t) b] += g * (a.mag[(size_t) b] - s.m1[(size_t) b]);
                    if (s.f1[(size_t) b] == 0) s.f1[(size_t) b] = a.freq[(size_t) b];
                    s.f1[(size_t) b] += g * (a.freq[(size_t) b] - s.f1[(size_t) b]);
                    o.mag[(size_t) b] = P[1] * s.m1[(size_t) b] + (1 - P[1]) * a.mag[(size_t) b];
                    o.freq[(size_t) b] = s.f1[(size_t) b];
                }
                break;
            }
            case MatrixFx::DroneMaker:
            {
                ensure (s.m1); ensure (s.f1);
                const float g = 1.0f - std::exp (-hopS / std::max (0.05f, P[0]));
                float pk = 1e-12f;
                for (int b = 0; b < kBins; ++b)
                {
                    // while the sound plays the drone gathers it; after, it holds and slowly fades
                    if (! after) s.m1[(size_t) b] += g * (a.mag[(size_t) b] * 4.0f - s.m1[(size_t) b]);
                    else s.m1[(size_t) b] *= std::exp (-hopS / std::max (0.5f, P[5] * 0.5f));
                    if (s.f1[(size_t) b] == 0) s.f1[(size_t) b] = (float) b * bh;
                    if (! after) s.f1[(size_t) b] += g * (a.freq[(size_t) b] - s.f1[(size_t) b]);
                    pk = std::max (pk, s.m1[(size_t) b]);
                }
                const float gate = pk * std::pow (10.0f, P[3] / 20.0f);
                const float comb = P[1] * (1.0f + 0.03f * std::sin (6.2831853f * P[4] * (float) t));
                for (int b = 0; b < kBins; ++b)
                {
                    float m = s.m1[(size_t) b];
                    if (m < gate) m = 0;
                    if (comb > 1.0f)
                    {
                        const float c = 0.5f + 0.5f * std::cos (6.2831853f * s.f1[(size_t) b] / comb);
                        m *= (1.0f - P[2]) + P[2] * c * c * c * c;
                    }
                    o.mag[(size_t) b] = m;
                    o.freq[(size_t) b] = s.f1[(size_t) b] * (1.0f + 0.0015f * std::sin (0.7f * (float) t + 0.37f * b));   // a slow drift
                }
                break;
            }
            case MatrixFx::Freezing:
            {
                ensure (s.m1); ensure (s.f1);
                const float dec = std::pow (10.0f, -P[0] * hopS / 20.0f);
                for (int b = 0; b < kBins; ++b)
                {
                    s.m1[(size_t) b] *= dec;
                    if (a.mag[(size_t) b] > s.m1[(size_t) b]) { s.m1[(size_t) b] = a.mag[(size_t) b]; s.f1[(size_t) b] = a.freq[(size_t) b]; }
                    o.mag[(size_t) b] = P[2] * s.m1[(size_t) b] + (1 - P[2]) * a.mag[(size_t) b];
                    o.freq[(size_t) b] = s.f1[(size_t) b];
                }
                break;
            }
            case MatrixFx::GateHold:
            {
                ensure (s.m1); ensure (s.f1); ensure (s.timer, -1.0f); ensure (s.hold);
                float pk = 1e-12f; for (float m : a.mag) pk = std::max (pk, m);
                if (s.m2.empty()) s.m2.assign (1, pk);
                s.m2[0] = std::max (s.m2[0] * 0.999f, pk);            // the running loudest
                const float thr = s.m2[0] * std::pow (10.0f, P[0] / 20.0f);
                const float fade = std::max (0.001f, P[2] / 1000.0f), dec = std::pow (10.0f, -P[3] * hopS / 20.0f);
                for (int b = 0; b < kBins; ++b)
                {
                    float& tm = s.timer[(size_t) b];
                    if (tm < 0.0f && a.mag[(size_t) b] > thr)
                    {   // caught: hold it for a while
                        tm = 0.0f; s.m1[(size_t) b] = a.mag[(size_t) b]; s.f1[(size_t) b] = a.freq[(size_t) b];
                        s.hold[(size_t) b] = std::max (0.02f, P[1] / 1000.0f * (1.0f + P[4] * (2.0f * uni() - 1.0f)));
                    }
                    if (tm >= 0.0f)
                    {
                        const float H = s.hold[(size_t) b];
                        const float env = std::min ({ 1.0f, tm / fade, std::max (0.0f, (H - tm) / fade) });
                        o.mag[(size_t) b] = s.m1[(size_t) b] * env; o.freq[(size_t) b] = s.f1[(size_t) b];
                        s.m1[(size_t) b] *= dec;
                        tm += hopS;
                        if (tm > H) tm = -1.0f;
                    }
                }
                break;
            }
            case MatrixFx::Weave:
            {
                const int K = std::max (2, (int) P[1]);
                if (! after && t - s.lastSnap >= P[0] / 1000.0f)
                {
                    s.snaps.push_back (a); s.lastSnap = t;
                    while ((int) s.snaps.size() > K) s.snaps.erase (s.snaps.begin());
                }
                const float live = after ? 0.0f : P[3];
                for (int b = 0; b < kBins; ++b) { o.mag[(size_t) b] = live * a.mag[(size_t) b]; o.freq[(size_t) b] = a.freq[(size_t) b]; }
                const float fadeOut = after ? std::exp (-(float) (t - (double) inLenSec()) / std::max (0.3f, P[4] * 0.35f)) : 1.0f;
                for (size_t k = 0; k < s.snaps.size(); ++k)
                {
                    const float w = std::pow (0.5f + 0.5f * std::sin (6.2831853f * P[2] * (float) t + 6.2831853f * (float) k / (float) s.snaps.size()), 2.0f) * fadeOut;
                    for (int b = 0; b < kBins; ++b)
                    {
                        const float m = w * s.snaps[k].mag[(size_t) b];
                        if (m > o.mag[(size_t) b]) o.freq[(size_t) b] = s.snaps[k].freq[(size_t) b];
                        o.mag[(size_t) b] += m;
                    }
                }
                break;
            }
            case MatrixFx::Shimmer:
            {
                ensure (s.phase); ensure (s.rate);
                if (s.frameNo == 0) for (int b = 0; b < kBins; ++b) { s.phase[(size_t) b] = 6.2831853f * hash01 ((uint32_t) b * 3u + 1u); s.rate[(size_t) b] = P[2] * (0.5f + hash01 ((uint32_t) b * 7u + 5u)); }
                for (int b = 0; b < kBins; ++b)
                {
                    s.phase[(size_t) b] += 6.2831853f * s.rate[(size_t) b] * hopS;
                    float m = a.mag[(size_t) b] * (1.0f - P[1] * (0.5f + 0.5f * std::sin (s.phase[(size_t) b])));
                    const float f = a.freq[(size_t) b];
                    if (f > P[4]) m *= std::pow (10.0f, P[3] / 20.0f * std::min (1.0f, (f - P[4]) / P[4] + 0.3f)) * (0.6f + 0.8f * uni());
                    o.mag[(size_t) b] = 0; o.freq[(size_t) b] = 0;
                    deposit (o, f * std::pow (2.0f, P[0] * gauss() / 1200.0f), m);
                }
                break;
            }
            case MatrixFx::Shuffle:
            {
                const int G = std::max (1, (int) P[2]), groups = kBins / G;
                if (t - s.lastShuffle >= P[3] / 1000.0f || s.perm.empty())
                {
                    s.lastShuffle = t;
                    s.perm.resize ((size_t) groups); for (int g = 0; g < groups; ++g) s.perm[(size_t) g] = g;
                    const int range = std::max (1, (int) P[1] / G);
                    for (int g = 0; g < groups; ++g)
                        if (uni() * 100.0f < P[0])
                        {
                            const int h = std::min (groups - 1, std::max (0, g + (int) std::lround ((2.0f * uni() - 1.0f) * range)));
                            std::swap (s.perm[(size_t) g], s.perm[(size_t) h]);
                        }
                }
                for (int g = 0; g < groups; ++g)
                    for (int j = 0; j < G; ++j)
                    {
                        const int src = s.perm[(size_t) g] * G + j, dst = g * G + j;
                        if (src >= kBins || dst >= kBins) continue;
                        o.mag[(size_t) dst] = a.mag[(size_t) src];
                        o.freq[(size_t) dst] = a.freq[(size_t) src] + (float) (dst - src) * bh;
                    }
                break;
            }
            case MatrixFx::Pulsing:
            {
                ensure (s.timer); ensure (s.hold, 1.0f);   // timer: time in the current state; hold: 1 = on
                if (s.frameNo == 0) for (int b = 0; b < kBins; ++b) s.timer[(size_t) b] = hash01 ((uint32_t) b * 13u + 3u) * P[0] / 1000.0f;
                for (int b = 0; b < kBins; ++b)
                {
                    const float oct = std::log2 (std::max (20.0f, (float) b * bh) / 1000.0f);
                    const float tm = std::pow (P[4], (float) t);
                    auto lim = [&] (float d) { const float lo = 0.005f, hi = 2.0f; if (d <= hi) return std::max (lo, d);
                        if (P[5] > 0.5f) return lo + std::fmod (d - lo, hi - lo);                     // wrap
                        const float span = hi - lo, u = std::fmod (d - lo, 2 * span); return lo + (u < span ? u : 2 * span - u); };   // ping-pong
                    const float on = lim (P[0] / 1000.0f * std::pow (P[2], oct) * tm), off = lim (P[1] / 1000.0f * std::pow (P[3], oct) * tm);
                    float& tmr = s.timer[(size_t) b]; float& st = s.hold[(size_t) b];
                    tmr += hopS;
                    if (st > 0.5f && tmr > on) { st = 0; tmr = 0; } else if (st < 0.5f && tmr > off) { st = 1; tmr = 0; }
                    const float edge = std::min (1.0f, tmr / 0.006f);
                    o.mag[(size_t) b] = a.mag[(size_t) b] * (st > 0.5f ? edge : 1.0f - edge);
                    o.freq[(size_t) b] = a.freq[(size_t) b];
                }
                break;
            }
            case MatrixFx::Harmonizer:
            {
                for (int b = 0; b < kBins; ++b) { o.mag[(size_t) b] = P[6] * a.mag[(size_t) b]; o.freq[(size_t) b] = a.freq[(size_t) b]; }
                for (int k = 0; k < 3; ++k)
                {
                    const float r = std::pow (2.0f, P[(size_t) k] / 12.0f), lv = P[(size_t) (3 + k)];
                    if (lv <= 0) continue;
                    for (int b = 1; b < kBins; ++b) deposit (o, a.freq[(size_t) b] * r, a.mag[(size_t) b] * lv);
                }
                break;
            }
            case MatrixFx::BinShift:
            {
                const int sh = (int) std::lround (P[0]);
                for (int b = 0; b < kBins; ++b) { o.mag[(size_t) b] = (1 - P[1]) * a.mag[(size_t) b]; o.freq[(size_t) b] = a.freq[(size_t) b]; }
                for (int b = 1; b < kBins; ++b) deposit (o, a.freq[(size_t) b] + (float) sh * bh, a.mag[(size_t) b] * P[1]);
                break;
            }
            case MatrixFx::PitchShift:
            {
                const auto owner = peakOf (a, P[3]);
                const float r = std::pow (2.0f, P[0] / 12.0f);
                for (int b = 1; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) continue;
                    float f;
                    if (P[1] > 0.5f) f = a.freq[(size_t) b] + P[2] * bh;
                    else f = a.freq[(size_t) b] + (a.freq[(size_t) pkb] * (r - 1.0f));   // the peak moves by the interval, its shape around it moves along
                    deposit (o, f, a.mag[(size_t) b]);
                }
                break;
            }
            case MatrixFx::Stretch:
            {
                float beta = P[1];
                if (P[2] > 0)
                {
                    float v = 0;
                    float pk = 0; for (float m : a.mag) pk = std::max (pk, m);
                    if (s.m2.empty()) s.m2.assign (1, pk);
                    s.m2[0] = std::max (s.m2[0] * 0.995f, pk);
                    const float amp = std::min (1.0f, pk / std::max (1e-9f, s.m2[0]));
                    switch ((int) std::lround (P[3]))
                    {
                        case 0: v = 2.0f * uni() - 1.0f; break;                                   // random
                        case 1: v = std::sin (6.2831853f * P[4] * (float) t); break;              // LFO
                        case 2: v = 2.0f * amp - 1.0f; break;                                     // follows the loudness
                        default: v = 1.0f - 2.0f * amp; break;                                    // against it
                    }
                    beta += P[2] * 0.5f * v;
                    if (P[5] > 0) { const float q = 0.02f + 0.3f * P[5]; beta = std::round (beta / q) * q; }
                }
                const float ny = 0.5f * sr;
                for (int b = 1; b < kBins; ++b)
                {
                    const float w = a.freq[(size_t) b] / ny;
                    deposit (o, a.freq[(size_t) b] + (beta * w + P[0] * w * w) * ny * 0.5f, a.mag[(size_t) b]);
                }
                break;
            }
            case MatrixFx::Tracing:
            {
                std::vector<char> keep ((size_t) kBins, 0);
                const int mode = (int) std::lround (P[0]);
                if (mode == 0)
                {
                    std::vector<int> idx ((size_t) kBins); for (int b = 0; b < kBins; ++b) idx[(size_t) b] = b;
                    const int K = std::min (kBins, std::max (1, (int) P[1]));
                    std::partial_sort (idx.begin(), idx.begin() + K, idx.end(), [&] (int x, int y) { return a.mag[(size_t) x] > a.mag[(size_t) y]; });
                    for (int k = 0; k < K; ++k) keep[(size_t) idx[(size_t) k]] = 1;
                }
                else if (mode == 1) { for (int b = 0; b < kBins; ++b) { const float f = (float) b * bh; keep[(size_t) b] = f >= P[3] && f <= P[4]; } }
                else
                {
                    float pk = 1e-12f; for (float m : a.mag) pk = std::max (pk, m);
                    const float thr = pk * std::pow (10.0f, P[5] / 20.0f);
                    for (int b = 0; b < kBins; ++b) keep[(size_t) b] = a.mag[(size_t) b] >= thr;
                }
                const bool inv = P[2] > 0.5f;
                for (int b = 0; b < kBins; ++b) { const bool k = keep[(size_t) b] != inv; o.mag[(size_t) b] = k ? a.mag[(size_t) b] : 0.0f; o.freq[(size_t) b] = a.freq[(size_t) b]; }
                break;
            }
            case MatrixFx::Filterbank:
            {
                std::vector<float> centres;
                const int count = std::max (1, (int) P[2]);
                for (int k = 0; k < count; ++k)
                {
                    float f = P[1] < 0.01f ? P[0] * (float) (k + 1) : P[0] * std::pow (2.0f, P[1] * (float) k / 12.0f);
                    f *= std::pow (2.0f, P[4] * (2.0f * hash01 ((uint32_t) k * 31u + seed) - 1.0f) / 1200.0f);
                    if (f < 0.45f * sr) centres.push_back (f);
                }
                const float wc = std::max (1.0f, P[3]);
                float makeup = std::sqrt ((float) kBins / std::max (1.0f, (float) centres.size() * (wc / 30.0f) * 3.0f));
                for (int b = 1; b < kBins; ++b)
                {
                    const float f = std::max (1.0f, a.freq[(size_t) b]);
                    float g = 0;
                    for (float c : centres) { const float d = 1200.0f * std::log2 (f / c) / wc; g += std::exp (-0.5f * d * d); }
                    o.mag[(size_t) b] = a.mag[(size_t) b] * std::min (1.0f, g) * std::min (8.0f, makeup);
                    o.freq[(size_t) b] = a.freq[(size_t) b];
                }
                break;
            }
            case MatrixFx::GlidingFilters:
            {
                const int count = std::max (1, (int) P[0]);
                if ((int) s.filt.size() != count)
                {   // each filter: [freq, glide oct/s, age, life, gap left, live]
                    s.filt.assign ((size_t) count, {});
                    for (auto& q : s.filt) { q[4] = uni() * P[4] / 1000.0f; q[5] = 0; }
                }
                for (auto& q : s.filt)
                {
                    if (q[5] < 0.5f)
                    {
                        q[4] -= hopS;
                        if (q[4] <= 0)
                        {
                            q[5] = 1; q[2] = 0; q[3] = std::max (0.05f, P[3] / 1000.0f * (0.5f + uni()));
                            q[0] = P[5] * std::pow (P[6] / std::max (1.0f, P[5]), uni());
                            q[1] = P[2] * (2.0f * uni() - 1.0f);
                        }
                    }
                    else
                    {
                        q[2] += hopS; q[0] *= std::pow (2.0f, q[1] * hopS);
                        if (q[2] > q[3] || q[0] < 30.0f || q[0] > 0.45f * sr) { q[5] = 0; q[4] = P[4] / 1000.0f * (0.5f + uni()); }
                    }
                }
                const float wc = std::max (1.0f, P[1]);
                for (int b = 1; b < kBins; ++b)
                {
                    const float f = std::max (1.0f, a.freq[(size_t) b]);
                    float g = 0;
                    for (const auto& q : s.filt)
                    {
                        if (q[5] < 0.5f) continue;
                        const float env = std::min ({ 1.0f, q[2] / (0.2f * q[3]), (q[3] - q[2]) / (0.2f * q[3]) });
                        const float d = 1200.0f * std::log2 (f / q[0]) / wc;
                        g += env * std::exp (-0.5f * d * d);
                    }
                    o.mag[(size_t) b] = a.mag[(size_t) b] * std::min (1.0f, g) * 4.0f;
                    o.freq[(size_t) b] = a.freq[(size_t) b];
                }
                break;
            }
            case MatrixFx::Granulation:
            {
                s.past.push_back (a.mag); s.pastF.push_back (a.freq);
                const int bands = std::max (1, (int) P[1]);
                const float grainS = std::max (0.01f, P[0] / 1000.0f);
                const int fr = (int) s.past.size() - 1;
                for (int k = 0; k < bands; ++k)
                {
                    const float lo = 40.0f * std::pow (18000.0f / 40.0f, (float) k / bands), hi = 40.0f * std::pow (18000.0f / 40.0f, (float) (k + 1) / bands);
                    const int gi = (int) std::floor (t / grainS + hash01 ((uint32_t) k * 101u + seed) );
                    const uint32_t key = (uint32_t) (gi * 977 + k * 131) + seed * 17u;
                    if (hash01 (key) > P[4]) continue;                                           // this grain is silent
                    const float delay = P[2] / 1000.0f * hash01 (key * 3u + 1u);
                    const float shift = std::pow (2.0f, P[3] * (2.0f * hash01 (key * 5u + 2u) - 1.0f) / 12.0f);
                    const float ph = (float) std::fmod (t / grainS + hash01 ((uint32_t) k * 101u + seed), 1.0);
                    const float env = std::sin (3.14159265f * ph);
                    const int src = fr - (int) std::lround (delay * sr / kHop);
                    if (src < 0 || src >= (int) s.past.size()) continue;
                    for (int b = (int) (lo / bh); b <= std::min (kBins - 1, (int) (hi / bh)); ++b)
                        deposit (o, s.pastF[(size_t) src][(size_t) b] * shift, s.past[(size_t) src][(size_t) b] * env * 1.4f);
                }
                break;
            }
            case MatrixFx::Emergence:
            {
                const auto owner = peakOf (a, P[4]);
                for (int b = 0; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) { o.mag[(size_t) b] = a.mag[(size_t) b] * (1 - P[2]); o.freq[(size_t) b] = a.freq[(size_t) b]; continue; }
                    const float pitch = 12.0f * std::log2 (std::max (20.0f, a.freq[(size_t) pkb]) / 440.0f);
                    const uint32_t key = (uint32_t) (int) std::lround (pitch * 2.0f + 400.0f) * 2654435761u + seed;   // the same partial keeps its envelope
                    const float rate = P[0] * (1.0f + P[1] * (2.0f * hash01 (key) - 1.0f) * 0.9f);
                    const float ph0 = 6.2831853f * hash01 (key ^ 0x5bd1e995u) + (s.ch == 1 ? 3.14159265f * P[3] : 0.0f);
                    const float env = (1 - P[2]) + P[2] * std::pow (0.5f + 0.5f * std::sin (6.2831853f * rate * (float) t + ph0), 1.5f);
                    o.mag[(size_t) b] = a.mag[(size_t) b] * env; o.freq[(size_t) b] = a.freq[(size_t) b];
                }
                break;
            }
            case MatrixFx::PartialGlide:
            {
                const auto owner = peakOf (a, P[4]);
                for (int b = 1; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) { deposit (o, a.freq[(size_t) b], a.mag[(size_t) b]); continue; }
                    const float pitch = 12.0f * std::log2 (std::max (20.0f, a.freq[(size_t) pkb]) / 440.0f);
                    const uint32_t key = (uint32_t) (int) std::lround (pitch + 200.0f) * 2246822519u + seed;
                    const float dir = P[2] + (1.0f - std::fabs (P[2])) * (hash01 (key) < 0.5f ? -1.0f : 1.0f);
                    const float rate = P[0] * (1.0f - P[1] + P[1] * hash01 (key * 7u + 3u)) * (dir >= 0 ? 1.0f : -1.0f) * std::max (0.2f, std::fabs (dir));
                    const float st = std::max (-P[3], std::min (P[3], rate * (float) t));
                    deposit (o, a.freq[(size_t) b] * std::pow (2.0f, st / 12.0f), a.mag[(size_t) b]);
                }
                break;
            }
            default: o.mag = a.mag; o.freq = a.freq; break;
        }
        ++s.frameNo;
    }
    double inLenSec() const noexcept { return lastInSec; }

    // ---------------------------------------------------------------- time domain
    static float cubic (const float* x, int n, double pos) noexcept
    {
        if (pos < 0 || pos >= n - 1) return 0.0f;
        const int i = (int) pos; const float f = (float) (pos - i);
        auto at = [&] (int k) { return x[std::min (n - 1, std::max (0, k))]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        return ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f + y1);
    }

    void ideeFixer (const float* inL, const float* inR, int n, std::vector<float>& oL, std::vector<float>& oR)
    {
        const int len = (int) (P[6] * sr);
        oL.assign ((size_t) len, 0.0f); oR.assign ((size_t) len, 0.0f);
        std::mt19937 r (seed * 31u + 3u); std::uniform_real_distribution<float> u (0.0f, 1.0f);
        const float g = std::max (0.005f, P[2] / 1000.0f), sp = std::max (0.002f, P[3] / 1000.0f);
        double t = 0;
        while (t < len / (double) sr)
        {
            const float startPos = std::min (0.999f, std::max (0.0f, P[0] + P[1] * (u (r) * 2.0f - 1.0f))) * (float) n;
            const float semis = P[4] + P[5] * (u (r) * 2.0f - 1.0f);
            const float rho = std::pow (2.0f, semis / 12.0f);
            const int gN = (int) (g * sr);
            const float pan = u (r) * 2.0f - 1.0f;
            const float gl = std::sqrt (0.5f * (1 - pan)), gr = std::sqrt (0.5f * (1 + pan));
            const int o0 = (int) (t * sr);
            for (int k = 0; k < gN && o0 + k < len; ++k)
            {
                const float w = std::sin (3.14159265f * k / gN); const double q = startPos + k * rho;
                if (q >= n - 2) break;
                oL[(size_t) (o0 + k)] += 0.7f * w * w * cubic (inL, n, q) * gl * 1.41f;
                oR[(size_t) (o0 + k)] += 0.7f * w * w * cubic (inR, n, q) * gr * 1.41f;
            }
            t += sp * (0.6f + 0.8f * u (r));
        }
    }
    void chorus (const float* inL, const float* inR, int n, std::vector<float>& oL, std::vector<float>& oR)
    {
        const int len = n + (int) (0.3f * sr);
        oL.assign ((size_t) len, 0.0f); oR.assign ((size_t) len, 0.0f);
        const int V = std::max (1, (int) P[4]);
        std::vector<float> bl ((size_t) len, 0.0f), br ((size_t) len, 0.0f);   // the delay lines (with feedback)
        for (int i = 0; i < len; ++i)
        {
            const float xl = i < n ? inL[i] : 0.0f, xr = i < n ? inR[i] : 0.0f;
            float wl = 0, wr = 0, fbL = 0, fbR = 0;
            for (int v = 0; v < V; ++v)
            {
                const float ph = 6.2831853f * (P[0] * (float) i / sr + (float) v / V);
                const float dl = (P[2] + P[1] * (0.5f + 0.5f * std::sin (ph))) / 1000.0f * sr;
                const float dr = (P[2] + P[1] * (0.5f + 0.5f * std::sin (ph + 1.5708f))) / 1000.0f * sr;
                const float yl = cubic (bl.data(), len, i - dl), yr = cubic (br.data(), len, i - dr);
                wl += yl; wr += yr; if (v == 0) { fbL = yl; fbR = yr; }
            }
            bl[(size_t) i] = xl + P[3] * fbL; br[(size_t) i] = xr + P[3] * fbR;
            oL[(size_t) i] = (1 - P[5]) * xl + P[5] * wl / std::sqrt ((float) V);
            oR[(size_t) i] = (1 - P[5]) * xr + P[5] * wr / std::sqrt ((float) V);
        }
    }
    void combs (const float* inL, const float* inR, int n, std::vector<float>& oL, std::vector<float>& oR)
    {
        const int len = n + (int) (P[6] * sr);
        oL.assign ((size_t) len, 0.0f); oR.assign ((size_t) len, 0.0f);
        const int count = std::max (1, (int) P[1]);
        for (int k = 0; k < count; ++k)
        {
            const float f = P[0] * std::pow (2.0f, P[2] * (float) k / 12.0f);
            const float D = sr / std::max (20.0f, f);
            std::vector<float> lineL ((size_t) len, 0.0f), lineR ((size_t) len, 0.0f);
            float lpL = 0, lpR = 0;
            const float g = 1.0f / std::sqrt ((float) count) * (1.0f - P[3]) * 4.0f;
            for (int i = 0; i < len; ++i)
            {
                const float xl = i < n ? inL[i] : 0.0f, xr = i < n ? inR[i] : 0.0f;
                const float yl = cubic (lineL.data(), len, i - D), yr = cubic (lineR.data(), len, i - D);
                lpL += (1.0f - P[4]) * (yl - lpL); lpR += (1.0f - P[4]) * (yr - lpR);
                lineL[(size_t) i] = xl + P[3] * lpL; lineR[(size_t) i] = xr + P[3] * lpR;
                oL[(size_t) i] += P[5] * g * yl; oR[(size_t) i] += P[5] * g * yr;
            }
        }
        for (int i = 0; i < n; ++i) { oL[(size_t) i] += (1 - P[5]) * inL[i]; oR[(size_t) i] += (1 - P[5]) * inR[i]; }
    }
    void grainStreamer (const float* inL, const float* inR, int n, std::vector<float>& oL, std::vector<float>& oR)
    {
        const int len = n + (int) (P[6] * sr);
        oL.assign ((size_t) len, 0.0f); oR.assign ((size_t) len, 0.0f);
        const float g = std::max (0.005f, P[0] / 1000.0f);
        const int gN = (int) (g * sr), reps = std::max (1, (int) P[1]);
        const float stream = std::max (0.005f, P[2] / 1000.0f), cap = std::max (0.01f, P[3] / 1000.0f);
        std::mt19937 r (seed * 53u + 9u); std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        for (double c = 0; c < (double) n / sr; c += cap)
        {
            const int c0 = (int) (c * sr);
            float amp = 1.0f, rho = 1.0f;
            const float pan = 0.6f * u (r);
            for (int k = 0; k < reps; ++k)
            {
                const int o0 = c0 + (int) (k * stream * sr);
                const float gl = std::sqrt (0.5f * (1 - pan * (k % 2 ? -1.0f : 1.0f))) * 1.41f, gr = std::sqrt (0.5f * (1 + pan * (k % 2 ? -1.0f : 1.0f))) * 1.41f;
                for (int i = 0; i < gN && o0 + i < len; ++i)
                {
                    const float w = std::sin (3.14159265f * i / gN); const double q = c0 + i * rho;
                    oL[(size_t) (o0 + i)] += 0.6f * amp * w * w * cubic (inL, n, q) * gl;
                    oR[(size_t) (o0 + i)] += 0.6f * amp * w * w * cubic (inR, n, q) * gr;
                }
                amp *= P[4]; rho *= std::pow (2.0f, P[5] / 12.0f);
            }
        }
    }
    void superFilterbank (const float* inL, const float* inR, int n, std::vector<float>& oL, std::vector<float>& oR)
    {
        const int len = n + (int) (0.5f * sr);
        oL.assign ((size_t) len, 0.0f); oR.assign ((size_t) len, 0.0f);
        const int count = std::max (1, (int) P[1]);
        for (int k = 0; k < count; ++k)
        {
            const float f = P[0] * std::pow (2.0f, P[2] * (float) k / 12.0f);
            if (f >= 0.45f * sr) break;
            const float w0 = 6.2831853f * f / sr, al = std::sin (w0) / (2.0f * std::max (0.5f, P[3]));
            const float a0 = 1 + al, b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos (w0) / a0, a2 = (1 - al) / a0;
            const float tilt = std::pow (10.0f, P[4] * std::log2 (f / 1000.0f) / 20.0f) * std::sqrt (P[3] / 10.0f) / std::sqrt ((float) count) * 1.6f;
            float x1l = 0, x2l = 0, y1l = 0, y2l = 0, x1r = 0, x2r = 0, y1r = 0, y2r = 0;
            for (int i = 0; i < len; ++i)
            {
                const float xl = i < n ? inL[i] : 0.0f, xr = i < n ? inR[i] : 0.0f;
                const float yl = b0 * xl + b2 * x2l - a1 * y1l - a2 * y2l; x2l = x1l; x1l = xl; y2l = y1l; y1l = yl;
                const float yr = b0 * xr + b2 * x2r - a1 * y1r - a2 * y2r; x2r = x1r; x1r = xr; y2r = y1r; y1r = yr;
                oL[(size_t) i] += P[5] * tilt * yl; oR[(size_t) i] += P[5] * tilt * yr;
            }
        }
        for (int i = 0; i < n; ++i) { oL[(size_t) i] += (1 - P[5]) * inL[i]; oR[(size_t) i] += (1 - P[5]) * inR[i]; }
    }

    float sr = 48000.0f;
    std::vector<float> P;
    uint32_t seed = 1;
    std::vector<std::complex<float>> tw;
    std::vector<int> rev;
    std::vector<float> win;
    double lastInSec = 0.0;
public:
    void setInputSeconds (double s) noexcept { lastInSec = s; }
};
} // namespace nb
