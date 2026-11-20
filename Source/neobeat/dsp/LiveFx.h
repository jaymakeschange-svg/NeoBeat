// NeoBeat 3.0 — the effects, live. The 24 library effects (MatrixFx.h describes them) and Bounce Delay
// as streaming processors: sound goes in block by block, the effect comes out at once, and a control
// that moves is heard in the next block, also on a tail that is still ringing.
//
// The spectral effects need the sound 2048 samples ahead (one FFT frame). A hit is a known sound (a
// rendered hit or a sample), so the unit that hosts the effect feeds it that far ahead and nothing is
// late. Effects that play pieces of the hit again (Bounce Delay, Grain Streamer, Idee Fixer) read it
// from the hit itself (HitView).
//
// Real-time rules: prepare() allocates everything; start(), update() and process() never allocate,
// lock or wait.
#pragma once

#include "BounceDelay.h"
#include "MatrixFx.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace nb
{
// What a live effect is and how it is set. Plain data: the audio thread copies it.
struct LiveSpec
{
    uint8_t kind = 0;                 // 0 none, 1 library, 2 Bounce Delay
    MatrixFx fx = MatrixFx::Freezing;
    std::array<float, 8> p {};        // library parameters, in matrixFxInfo() order
    uint32_t seed = 1;
    BounceParams bounce;
    bool wetOnly = false;             // Bounce Delay: the bounces without the hit itself (a slot beside the dry hit)
    uint64_t version = 0;             // changes whenever anything above does
    bool on() const noexcept { return kind != 0; }
};

// The hit an effect works on, as far as it is known.
struct HitView
{
    const float* l = nullptr; const float* r = nullptr;
    int len = 0;
    bool final = false;               // len is the whole hit
};

// ------------------------------------------------------------------ FFT, 2048 points
struct LiveFft
{
    static constexpr int kN = 2048;
    struct Tables
    {
        std::array<float, kN / 2> c {}, s {};
        std::array<uint16_t, kN> rev {};
        std::array<float, kN> win {};
        Tables()
        {
            for (int k = 0; k < kN / 2; ++k) { c[(size_t) k] = (float) std::cos (-6.283185307179586 * k / kN); s[(size_t) k] = (float) std::sin (-6.283185307179586 * k / kN); }
            for (int i = 0, j = 0; i < kN; ++i) { rev[(size_t) i] = (uint16_t) j; int bit = kN >> 1; while (j & bit) { j ^= bit; bit >>= 1; } j |= bit; }
            for (int i = 0; i < kN; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (6.283185307179586 * i / kN));
        }
    };
    static const Tables& tables() { static const Tables t; return t; }

    // in place; inverse scales by 1/N
    static void run (float* re, float* im, bool inverse) noexcept
    {
        const auto& t = tables();
        for (int i = 0; i < kN; ++i) { const int j = t.rev[(size_t) i]; if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); } }
        for (int len = 2; len <= kN; len <<= 1)
        {
            const int half = len >> 1, step = kN / len;
            for (int i = 0; i < kN; i += len)
                for (int j = 0, k = 0; j < half; ++j, k += step)
                {
                    const float wr = t.c[(size_t) k], wi = inverse ? -t.s[(size_t) k] : t.s[(size_t) k];
                    const int a = i + j, b = a + half;
                    const float vr = re[b] * wr - im[b] * wi, vi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - vr; im[b] = im[a] - vi;
                    re[a] += vr; im[a] += vi;
                }
        }
        if (inverse) { const float k = 1.0f / kN; for (int i = 0; i < kN; ++i) { re[i] *= k; im[i] *= k; } }
    }
};

// ------------------------------------------------------------------ shared memory for the effects that need a lot
// Frame memory for the effects that look back (Averaging and Weave a little, Granulation two seconds),
// delay lines for the chorus and the combs. A few blocks each, taken at a hit's start and given back at
// its end, so the many effects that need none stay small.
class LiveHistPool
{
public:
    static constexpr int kBins = LiveFft::kN / 2 + 1;
    void prepare (int count, int numFrames)
    {
        frames = std::max (8, numFrames);
        blocks.assign ((size_t) count, {});
        for (auto& b : blocks) { b.data.assign ((size_t) 2 * (size_t) frames * 2 * kBins, 0.0f); b.used = false; }   // [ch][frame][mag|freq][bin]
    }
    float* acquire() noexcept { for (auto& b : blocks) if (! b.used) { b.used = true; return b.data.data(); } return nullptr; }
    void release (float* p) noexcept { for (auto& b : blocks) if (b.data.data() == p) b.used = false; }
    int frames = 64;
private:
    struct Block { std::vector<float> data; bool used = false; };
    std::vector<Block> blocks;
};

class LiveDelayPool
{
public:
    static constexpr int kLines = 24;
    void prepare (int count, float sr)
    {
        ringSize = 1; while (ringSize < (int) (sr / 20.0f) + 16) ringSize <<= 1;
        blocks.assign ((size_t) count, {});
        for (auto& b : blocks) { b.data.assign ((size_t) ringSize * kLines, 0.0f); b.used = false; }
    }
    float* acquire() noexcept
    {
        for (auto& b : blocks) if (! b.used) { b.used = true; std::fill (b.data.begin(), b.data.end(), 0.0f); return b.data.data(); }
        return nullptr;
    }
    void release (float* p) noexcept { for (auto& b : blocks) if (b.data.data() == p) b.used = false; }
    int ringSize = 4096;
private:
    struct Block { std::vector<float> data; bool used = false; };
    std::vector<Block> blocks;
};

struct LiveFxPools
{
    LiveHistPool small, big;
    LiveDelayPool delay;
    void prepare (float sr, int numSmall = 10, int numBig = 4, int numDelay = 10)
    {
        small.prepare (numSmall, 66);
        big.prepare (numBig, (int) (2.05f * sr / 256.0f) + 4);
        delay.prepare (numDelay, sr);
    }
};

// ------------------------------------------------------------------ one live effect
class LiveFx
{
public:
    static constexpr int kN = LiveFft::kN, kBins = kN / 2 + 1;
    static constexpr int kGrains = 96, kStreams = 48, kMaxBounces = 40;

    void prepare (float sampleRate, LiveFxPools* sharedPools)
    {
        sr = sampleRate; pools = sharedPools;
        LiveFft::tables();
        for (auto& c : ch)
        {
            c.in.assign (kN, 0.0f); c.out.assign (2 * kN, 0.0f); c.lastPh.assign (kBins, 0.0f); c.synPh.assign (kBins, 0.0f);
            for (auto* v : { &c.m1, &c.f1, &c.timer, &c.hold, &c.phase, &c.rate }) v->assign (kBins, 0.0f);
            c.perm.assign (kBins, 0);
        }
        eRing.assign (kN, 0.0f);
        for (auto* v : { &magA, &freqA, &magO, &freqO }) v->assign (kBins, 0.0f);
        re.assign (kN, 0.0f); im.assign (kN, 0.0f);
        owner.assign (kBins, -1); peaks.assign (kBins, 0); idx.assign (kBins, 0); keep.assign (kBins, 0);
        ringSize = pools != nullptr ? pools->delay.ringSize : 4096;
        active = false;
    }

    int latency() const noexcept { return spectral ? kN : 0; }
    // the level trim it settled on (0 until it has heard enough)
    float measuredGain() const noexcept { return learned; }
    bool isActive() const noexcept { return active; }
    bool finished() const noexcept { return done; }
    const LiveSpec& current() const noexcept { return spec; }

    // bpm and hitSeconds (the hit's place in the song) put Bounce Delay's bounces on the swung grid.
    // economy: half the frames (when many effects sound at once): lighter, a little rougher on pitch effects.
    // knownGain: the level trim this effect needed the last time it played with these settings (0: not
    // known yet, it finds it as it plays). exact: the settings have not changed since, so it is used as it is.
    void start (const LiveSpec& s, double bpmNow, double hitSeconds, float knownGain = 0.0f, bool exact = false, bool economy = false) noexcept
    {
        release();
        hop = economy ? 512 : 256;
        spec = s; bpm = bpmNow; hitTime = hitSeconds;
        prior = knownGain > 0.0f ? knownGain : 1.0f; priorExact = exact && knownGain > 0.0f;
        gain = gainTarget = prior; rawPeak = 1.0e-9f; learned = 0.0f;
        spectral = spec.kind == 1 && (int) spec.fx < (int) MatrixFx::IdeeFixer;
        inCount = 0; inputLen = -1; done = false; active = spec.kind != 0;
        ein = eout = 0.0;
        for (auto& c : ch)
        {
            std::fill (c.in.begin(), c.in.end(), 0.0f); std::fill (c.out.begin(), c.out.end(), 0.0f);
            std::fill (c.lastPh.begin(), c.lastPh.end(), 0.0f); std::fill (c.synPh.begin(), c.synPh.end(), 0.0f);
            for (auto* v : { &c.m1, &c.f1, &c.timer, &c.hold, &c.phase, &c.rate }) std::fill (v->begin(), v->end(), 0.0f);
            c.m2 = 0.0f; c.m2Set = false; c.lastSnap = -1.0e9; c.lastShuffle = -1.0e9; c.permSet = false; c.filtCount = 0; c.frameNo = 0;
            c.histCount = 0; c.histHead = 0;
            c.rng.seed (spec.seed * 7919u + 13u);
        }
        std::fill (eRing.begin(), eRing.end(), 0.0f);
        if (spec.kind == 1)
        {
            const auto fx = spec.fx;
            if (pools != nullptr && (fx == MatrixFx::Averaging || fx == MatrixFx::Weave || fx == MatrixFx::Granulation))
            {
                histPool = fx == MatrixFx::Granulation ? &pools->big : &pools->small;
                hist = histPool->acquire(); histFrames = histPool->frames;
            }
            if (pools != nullptr && (fx == MatrixFx::Chorus || fx == MatrixFx::CombFilterbank)) delayMem = pools->delay.acquire();   // the chorus (2 lines), the combs (24)
            for (auto& b : bq) b = {};
            for (auto& l : lp) l = 0.0f;
            numGrains = 0; numStreams = 0;
            tdRng.seed (fx == MatrixFx::IdeeFixer ? spec.seed * 31u + 3u : spec.seed * 53u + 9u);
            nextCapture = 0.0; nextCloud = 0.0;
        }
        if (spec.kind == 2) { planBounces(); for (auto& b : bstate) b = {}; nextBounce = 0; }
        setTails();
    }

    // new settings for a sound that is already playing
    void update (const LiveSpec& s) noexcept
    {
        if (! active || s.kind != spec.kind || (s.kind == 1 && s.fx != spec.fx)) return;   // another effect altogether: the next hit takes it
        spec = s;
        if (spec.kind == 2) planBounces();
        setTails();
    }

    void stop() noexcept { release(); active = false; done = true; }

    // does this effect play pieces of the hit again (so it needs the hit itself, HitView)?
    static bool needsHit (const LiveSpec& s) noexcept
    {
        return s.kind == 2 || (s.kind == 1 && (s.fx == MatrixFx::GrainStreamer || s.fx == MatrixFx::IdeeFixer));
    }
    static int latencyOf (const LiveSpec& s) noexcept { return s.kind == 1 && (int) s.fx < (int) MatrixFx::IdeeFixer ? kN : 0; }

    // n samples in, n samples out (out is written, not added). inputDone: the sound has ended (in is silence).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const HitView& hit, bool inputDone) noexcept
    {
        if (! active) { for (int i = 0; i < n; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; } return; }
        if (inputDone && inputLen < 0) inputLen = inCount;
        if (spec.kind == 2) { bounce (inL, inR, outL, outR, n, hit); return; }
        const bool level = true;
        for (int i = 0; i < n; ++i)
        {
            float l = 0.0f, r = 0.0f;
            const float xl = inL[i], xr = inR[i];
            const int64_t s = inCount - latency();     // the output sample this call produces
            float eIn = xl * xl + xr * xr;
            if (spectral)
            {
                const int w = (int) (inCount & (kN - 1));
                eIn = eRing[(size_t) w]; eRing[(size_t) w] = xl * xl + xr * xr;   // the input, a frame ago: what this output answers
                ch[0].in[(size_t) w] = xl; ch[1].in[(size_t) w] = xr;
                ++inCount;
                if (inCount >= kN / 2 && ((inCount - kN / 2) % hop) == 0) { frame (0); frame (1); }
                if (s >= 0)
                {
                    const int o = (int) (s & (2 * kN - 1));
                    l = ch[0].out[(size_t) o]; r = ch[1].out[(size_t) o];
                    ch[0].out[(size_t) o] = 0.0f; ch[1].out[(size_t) o] = 0.0f;
                }
            }
            else
            {
                timeDomain (xl, xr, l, r, hit);
                ++inCount;
            }
            if (s >= 0 && level)
            {
                // as loud as what went in: the running energies of the input and the output, over the hit itself
                // (the settled value is kept for the next hit: measuredGain())
                const bool during = inputLen < 0 || s < inputLen;
                rawPeak = std::max (rawPeak, std::max (std::fabs (l), std::fabs (r)));
                if (during) { ein += eIn; eout += (double) l * l + (double) r * r; }
                if ((s & 31) == 0 && ein > 1.0e-12 && eout > 1.0e-12)
                {
                    const float target = std::min ((float) std::min (7.94, std::max (0.126, std::sqrt (ein / eout))), 0.95f / rawPeak);
                    if (! during || s > (int64_t) (0.03f * sr)) learned = target;
                    if (! priorExact && during)
                    {
                        const float trust = std::min (1.0f, (float) s / (0.2f * sr));   // the first moments tell little
                        gainTarget = prior * std::pow (target / prior, trust);
                    }
                }
                gain += (gainTarget - gain) * gainCoef;
                l *= gain; r *= gain;
            }
            // the tail's end
            if (inputLen >= 0 && s >= inputLen)
            {
                const int64_t into = s - inputLen;
                if (into >= tailN) { l = 0.0f; r = 0.0f; done = true; }
                else if (into > tailN - fadeN) { const float g = (float) (tailN - into) / (float) fadeN; l *= g; r *= g; }
            }
            outL[i] = l; outR[i] = r;
        }
    }

private:
    // ================================================================= shared
    void release() noexcept
    {
        if (hist != nullptr && histPool != nullptr) histPool->release (hist);
        if (delayMem != nullptr && pools != nullptr) pools->delay.release (delayMem);
        hist = nullptr; delayMem = nullptr;
    }
    const float* P() const noexcept { return spec.p.data(); }
    static float princarg (float p) noexcept { return p - 6.2831853f * std::floor ((p + 3.14159265f) / 6.2831853f); }
    // quick phase maths for the frames (errors around 1e-5: far below anything heard)
    static float fastAtan2 (float y, float x) noexcept
    {
        const float ax = std::fabs (x), ay = std::fabs (y);
        const float mx = std::max (ax, ay), mn = std::min (ax, ay);
        if (mx < 1.0e-30f) return 0.0f;
        const float a = mn / mx, s = a * a;
        float r = a * (0.99997726f + s * (-0.33262347f + s * (0.19354346f + s * (-0.11643287f + s * (0.05265332f + s * -0.01172120f)))));
        if (ay > ax) r = 1.57079637f - r;
        if (x < 0.0f) r = 3.14159274f - r;
        return y < 0.0f ? -r : r;
    }
    // sine of x in [-pi/2, pi/2]
    static float sinCore (float x) noexcept
    {
        const float s = x * x;
        return x * (1.0f - s * (1.0f / 6.0f) * (1.0f - s * (1.0f / 20.0f) * (1.0f - s * (1.0f / 42.0f) * (1.0f - s * (1.0f / 72.0f)))));
    }
    // sine and cosine of x in [-pi, pi]
    static void fastSinCos (float x, float& sn, float& cs) noexcept
    {
        float xs = x;
        if (xs > 1.57079637f) xs = 3.14159274f - xs; else if (xs < -1.57079637f) xs = -3.14159274f - xs;
        sn = sinCore (xs);
        float xc = std::fabs (x);                     // cos x = sin (pi/2 - |x|)
        cs = sinCore (1.57079637f - xc);
    }
    float binHz() const noexcept { return sr / kN; }
    static uint32_t hash (uint32_t x) noexcept { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
    static float hash01 (uint32_t x) noexcept { return (float) (hash (x) & 0xffffff) / 16777216.0f; }

    void setTails() noexcept
    {
        const float* p = P();
        float tail = 0.1f;
        if (spec.kind == 1)
            switch (spec.fx)
            {
                case MatrixFx::Blurring: tail = p[2]; break;
                case MatrixFx::DroneMaker: tail = p[5]; break;
                case MatrixFx::Freezing: tail = p[1]; break;
                case MatrixFx::GateHold: tail = p[5]; break;
                case MatrixFx::Weave: tail = p[4]; break;
                case MatrixFx::Granulation: tail = p[5] + p[2] / 1000.0f; break;
                case MatrixFx::IdeeFixer: tail = p[6]; break;          // the cloud's whole length (from the hit's start)
                case MatrixFx::Chorus: tail = 0.3f; break;
                case MatrixFx::CombFilterbank: tail = p[6]; break;
                case MatrixFx::GrainStreamer: tail = p[6]; break;
                case MatrixFx::SuperFilterbank: tail = 0.5f; break;
                default: break;
            }
        tailN = (int64_t) (std::max (0.05f, tail) * sr) + (spectral ? kN : 0);
        fadeN = (int64_t) std::min ((double) tailN, 0.03 * sr);
        gainCoef = 1.0f - std::exp (-1.0f / (0.015f * sr));
        if (spec.kind == 1 && spec.fx == MatrixFx::SuperFilterbank) designBank();
    }

    // ================================================================= spectral
    struct Chan
    {
        std::vector<float> in, out, lastPh, synPh;
        std::vector<float> m1, f1, timer, hold, phase, rate;
        std::vector<int> perm;
        float m2 = 0.0f; bool m2Set = false, permSet = false;
        double lastSnap = -1.0e9, lastShuffle = -1.0e9;
        std::array<std::array<float, 6>, 16> filt {};
        int filtCount = 0, frameNo = 0, histCount = 0, histHead = 0;
        std::mt19937 rng { 1u };
    };
    float uni (Chan& c) noexcept { return (float) (c.rng() >> 8) * (1.0f / 16777216.0f); }
    float gauss (Chan& c) noexcept
    {   // Box-Muller (no library distribution: those may keep state and differ between platforms)
        const float u1 = std::max (1.0e-7f, uni (c)), u2 = uni (c);
        return std::sqrt (-2.0f * std::log (u1)) * std::cos (6.2831853f * u2);
    }
    float* histMag (int c, int slot) noexcept { return hist + ((size_t) c * (size_t) histFrames + (size_t) slot) * 2 * kBins; }
    float* histFreq (int c, int slot) noexcept { return histMag (c, slot) + kBins; }

    void deposit (float f, float m) noexcept
    {
        const int k = (int) std::lround (f / binHz());
        if (k <= 0 || k >= kBins - 1 || m <= 0.0f) return;
        if (m > magO[(size_t) k]) freqO[(size_t) k] = f;
        magO[(size_t) k] += m;
    }

    // each bin's peak (the partial it belongs to), or -1
    void peakOf (float thresholdDb) noexcept
    {
        std::fill (owner.begin(), owner.end(), -1);
        float pk = 1.0e-12f; for (float m : magA) pk = std::max (pk, m);
        const float thr = pk * std::pow (10.0f, thresholdDb / 20.0f);
        int np = 0;
        for (int b = 2; b < kBins - 2; ++b)
            if (magA[(size_t) b] > thr && magA[(size_t) b] >= magA[(size_t) b - 1] && magA[(size_t) b] > magA[(size_t) b + 1]) peaks[(size_t) np++] = b;
        for (int i = 0; i < np; ++i)
        {
            int lo = peaks[(size_t) i], hi = peaks[(size_t) i];
            while (lo > 1 && magA[(size_t) lo - 1] < magA[(size_t) lo] && (i == 0 || lo - 1 > peaks[(size_t) i - 1])) --lo;
            while (hi < kBins - 2 && magA[(size_t) hi + 1] < magA[(size_t) hi] && (i + 1 == np || hi + 1 < peaks[(size_t) i + 1])) ++hi;
            for (int b = lo; b <= hi; ++b) owner[(size_t) b] = peaks[(size_t) i];
        }
    }

    void frame (int c) noexcept
    {
        auto& C = ch[(size_t) c];
        const auto& T = LiveFft::tables();
        const int w = (int) (inCount & (kN - 1));          // the oldest sample in the ring
        for (int i = 0; i < kN; ++i) { re[(size_t) i] = C.in[(size_t) ((w + i) & (kN - 1))] * T.win[(size_t) i]; im[(size_t) i] = 0.0f; }
        LiveFft::run (re.data(), im.data(), false);
        const float bh = binHz();
        const float k1 = (float) kN / (6.2831853f * hop);
        for (int b = 0; b < kBins; ++b)
        {
            const float m = std::sqrt (re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b]);
            const float ph = fastAtan2 (im[(size_t) b], re[(size_t) b]);
            const float expect = 6.2831853f * (float) ((b * hop) % kN) / kN;
            const float dev = princarg (ph - C.lastPh[(size_t) b] - expect);
            C.lastPh[(size_t) b] = ph;
            magA[(size_t) b] = m;
            freqA[(size_t) b] = ((float) b + dev * k1) * bh;
        }
        const int64_t frameIndex = (inCount - kN / 2) / hop;
        const int64_t start = inCount - kN;
        const double t = (double) frameIndex * hop / sr;
        std::fill (magO.begin(), magO.end(), 0.0f); std::fill (freqO.begin(), freqO.end(), 0.0f);
        processFrame (C, c, t, inputLen >= 0 && start >= inputLen);
        const float k2 = 6.2831853f * hop / sr;
        for (int b = 0; b < kBins; ++b)
        {
            const float ph = princarg (C.synPh[(size_t) b] + k2 * freqO[(size_t) b]);
            C.synPh[(size_t) b] = ph;
            if (magO[(size_t) b] <= 0.0f) { re[(size_t) b] = 0.0f; im[(size_t) b] = 0.0f; continue; }
            float sn, cs; fastSinCos (std::max (-3.14159274f, std::min (3.14159274f, ph)), sn, cs);
            re[(size_t) b] = magO[(size_t) b] * cs; im[(size_t) b] = magO[(size_t) b] * sn;
        }
        im[0] = 0.0f; im[(size_t) (kBins - 1)] = 0.0f;
        for (int b = 1; b < kBins - 1; ++b) { re[(size_t) (kN - b)] = re[(size_t) b]; im[(size_t) (kN - b)] = -im[(size_t) b]; }
        LiveFft::run (re.data(), im.data(), true);
        const float norm = hop == 256 ? 1.0f / 3.0f : 1.0f / 1.5f;   // what the overlapping Hann-squared windows sum to
        for (int i = 0; i < kN; ++i)
        {
            const int64_t s = start + i;
            if (s < 0) continue;
            C.out[(size_t) (s & (2 * kN - 1))] += re[(size_t) i] * T.win[(size_t) i] * norm;
        }
    }

    // One frame of an effect: magA/freqA in, magO/freqO out (both zeroed before). The rules are MatrixFx.h's.
    void processFrame (Chan& s, int c, double t, bool after) noexcept
    {
        const float* p = P();
        const float hopS = (float) hop / sr;
        const float bh = binHz();
        const float hopRatio = (float) hop / 256.0f;       // the settings count frames of 256
        const uint32_t seed = spec.seed;
        switch (spec.fx)
        {
            case MatrixFx::Averaging:
            {
                const int N = std::max (1, std::min (histFrames, (int) std::lround (p[0] / hopRatio)));
                if (hist == nullptr) { passThrough(); break; }
                std::copy (magA.begin(), magA.end(), histMag (c, s.histHead)); std::copy (freqA.begin(), freqA.end(), histFreq (c, s.histHead));
                s.histHead = (s.histHead + 1) % histFrames; s.histCount = std::min (s.histCount + 1, histFrames);
                const int use = std::min (N, s.histCount);
                for (int b = 0; b < kBins; ++b)
                {
                    float m = 0, f = 0, w = 0;
                    for (int k = 1; k <= use; ++k)
                    {
                        const int slot = (s.histHead - k + 2 * histFrames) % histFrames;
                        const float hm = histMag (c, slot)[b];
                        m += hm; f += histFreq (c, slot)[b] * hm; w += hm;
                    }
                    m /= (float) use;
                    magO[(size_t) b] = p[1] * m + (1 - p[1]) * magA[(size_t) b];
                    freqO[(size_t) b] = w > 0 ? f / w : freqA[(size_t) b];
                }
                break;
            }
            case MatrixFx::Blurring:
            {
                const float g = 1.0f - std::exp (-hopS / std::max (0.005f, p[0] / 1000.0f));
                for (int b = 0; b < kBins; ++b)
                {
                    s.m1[(size_t) b] += g * (magA[(size_t) b] - s.m1[(size_t) b]);
                    if (s.f1[(size_t) b] == 0) s.f1[(size_t) b] = freqA[(size_t) b];
                    s.f1[(size_t) b] += g * (freqA[(size_t) b] - s.f1[(size_t) b]);
                    magO[(size_t) b] = p[1] * s.m1[(size_t) b] + (1 - p[1]) * magA[(size_t) b];
                    freqO[(size_t) b] = s.f1[(size_t) b];
                }
                break;
            }
            case MatrixFx::DroneMaker:
            {
                const float g = 1.0f - std::exp (-hopS / std::max (0.05f, p[0]));
                const float fadeK = std::exp (-hopS / std::max (0.5f, p[5] * 0.5f));
                float pk = 1.0e-12f;
                for (int b = 0; b < kBins; ++b)
                {
                    if (! after) s.m1[(size_t) b] += g * (magA[(size_t) b] * 4.0f - s.m1[(size_t) b]);
                    else s.m1[(size_t) b] *= fadeK;
                    if (s.f1[(size_t) b] == 0) s.f1[(size_t) b] = (float) b * bh;
                    if (! after) s.f1[(size_t) b] += g * (freqA[(size_t) b] - s.f1[(size_t) b]);
                    pk = std::max (pk, s.m1[(size_t) b]);
                }
                const float gate = pk * std::pow (10.0f, p[3] / 20.0f);
                const float comb = p[1] * (1.0f + 0.03f * std::sin (6.2831853f * p[4] * (float) t));
                for (int b = 0; b < kBins; ++b)
                {
                    float m = s.m1[(size_t) b];
                    if (m < gate) m = 0;
                    if (comb > 1.0f)
                    {
                        const float cc = 0.5f + 0.5f * std::cos (6.2831853f * s.f1[(size_t) b] / comb);
                        m *= (1.0f - p[2]) + p[2] * cc * cc * cc * cc;
                    }
                    magO[(size_t) b] = m;
                    freqO[(size_t) b] = s.f1[(size_t) b] * (1.0f + 0.0015f * std::sin (0.7f * (float) t + 0.37f * b));
                }
                break;
            }
            case MatrixFx::Freezing:
            {
                const float dec = std::pow (10.0f, -p[0] * hopS / 20.0f);
                for (int b = 0; b < kBins; ++b)
                {
                    s.m1[(size_t) b] *= dec;
                    if (magA[(size_t) b] > s.m1[(size_t) b]) { s.m1[(size_t) b] = magA[(size_t) b]; s.f1[(size_t) b] = freqA[(size_t) b]; }
                    magO[(size_t) b] = p[2] * s.m1[(size_t) b] + (1 - p[2]) * magA[(size_t) b];
                    freqO[(size_t) b] = s.f1[(size_t) b];
                }
                break;
            }
            case MatrixFx::GateHold:
            {
                if (s.frameNo == 0) std::fill (s.timer.begin(), s.timer.end(), -1.0f);
                float pk = 1.0e-12f; for (float m : magA) pk = std::max (pk, m);
                if (! s.m2Set) { s.m2 = pk; s.m2Set = true; }
                s.m2 = std::max (s.m2 * std::pow (0.999f, hopRatio), pk);
                const float thr = s.m2 * std::pow (10.0f, p[0] / 20.0f);
                const float fade = std::max (0.001f, p[2] / 1000.0f), dec = std::pow (10.0f, -p[3] * hopS / 20.0f);
                for (int b = 0; b < kBins; ++b)
                {
                    float& tm = s.timer[(size_t) b];
                    if (tm < 0.0f && magA[(size_t) b] > thr)
                    {
                        tm = 0.0f; s.m1[(size_t) b] = magA[(size_t) b]; s.f1[(size_t) b] = freqA[(size_t) b];
                        s.hold[(size_t) b] = std::max (0.02f, p[1] / 1000.0f * (1.0f + p[4] * (2.0f * uni (s) - 1.0f)));
                    }
                    if (tm >= 0.0f)
                    {
                        const float H = s.hold[(size_t) b];
                        const float env = std::min ({ 1.0f, tm / fade, std::max (0.0f, (H - tm) / fade) });
                        magO[(size_t) b] = s.m1[(size_t) b] * env; freqO[(size_t) b] = s.f1[(size_t) b];
                        s.m1[(size_t) b] *= dec;
                        tm += hopS;
                        if (tm > H) tm = -1.0f;
                    }
                }
                break;
            }
            case MatrixFx::Weave:
            {
                const int K = std::max (2, std::min (8, (int) p[1]));
                if (hist != nullptr && ! after && t - s.lastSnap >= p[0] / 1000.0f)
                {
                    std::copy (magA.begin(), magA.end(), histMag (c, s.histHead)); std::copy (freqA.begin(), freqA.end(), histFreq (c, s.histHead));
                    s.histHead = (s.histHead + 1) % histFrames; s.histCount = std::min (s.histCount + 1, histFrames);
                    s.lastSnap = t;
                }
                const float live = after ? 0.0f : p[3];
                for (int b = 0; b < kBins; ++b) { magO[(size_t) b] = live * magA[(size_t) b]; freqO[(size_t) b] = freqA[(size_t) b]; }
                const double inSec = inputLen >= 0 ? (double) inputLen / sr : t;
                const float fadeOut = after ? std::exp (-(float) (t - inSec) / std::max (0.3f, p[4] * 0.35f)) : 1.0f;
                const int n = std::min (K, s.histCount);
                for (int k = 0; k < n && hist != nullptr; ++k)
                {
                    // the snapshots, oldest first (as they were saved)
                    const int slot = (s.histHead - n + k + 2 * histFrames) % histFrames;
                    const float wv = 0.5f + 0.5f * std::sin (6.2831853f * p[2] * (float) t + 6.2831853f * (float) k / (float) n);
                    const float w = wv * wv * fadeOut;
                    const float* hm = histMag (c, slot); const float* hf = histFreq (c, slot);
                    for (int b = 0; b < kBins; ++b)
                    {
                        const float m = w * hm[b];
                        if (m > magO[(size_t) b]) freqO[(size_t) b] = hf[b];
                        magO[(size_t) b] += m;
                    }
                }
                break;
            }
            case MatrixFx::Shimmer:
            {
                if (s.frameNo == 0) for (int b = 0; b < kBins; ++b) { s.phase[(size_t) b] = 6.2831853f * hash01 ((uint32_t) b * 3u + 1u); s.rate[(size_t) b] = 0.5f + hash01 ((uint32_t) b * 7u + 5u); }
                const float sizzle = std::pow (10.0f, p[3] / 20.0f);
                for (int b = 0; b < kBins; ++b)
                {
                    s.phase[(size_t) b] += 6.2831853f * p[2] * s.rate[(size_t) b] * hopS;
                    float m = magA[(size_t) b] * (1.0f - p[1] * (0.5f + 0.5f * std::sin (s.phase[(size_t) b])));
                    const float f = freqA[(size_t) b];
                    if (f > p[4]) m *= std::pow (sizzle, std::min (1.0f, (f - p[4]) / p[4] + 0.3f)) * (0.6f + 0.8f * uni (s));
                    deposit (f * std::pow (2.0f, p[0] * gauss (s) / 1200.0f), m);
                }
                break;
            }
            case MatrixFx::Shuffle:
            {
                const int G = std::max (1, (int) p[2]), groups = kBins / G;
                if (t - s.lastShuffle >= p[3] / 1000.0f || ! s.permSet)
                {
                    s.lastShuffle = t; s.permSet = true;
                    for (int g = 0; g < groups; ++g) s.perm[(size_t) g] = g;
                    const int range = std::max (1, (int) p[1] / G);
                    for (int g = 0; g < groups; ++g)
                        if (uni (s) * 100.0f < p[0])
                        {
                            const int h = std::min (groups - 1, std::max (0, g + (int) std::lround ((2.0f * uni (s) - 1.0f) * range)));
                            std::swap (s.perm[(size_t) g], s.perm[(size_t) h]);
                        }
                }
                for (int g = 0; g < groups; ++g)
                    for (int j = 0; j < G; ++j)
                    {
                        const int src = s.perm[(size_t) g] * G + j, dst = g * G + j;
                        if (src >= kBins || dst >= kBins || src < 0) continue;
                        magO[(size_t) dst] = magA[(size_t) src];
                        freqO[(size_t) dst] = freqA[(size_t) src] + (float) (dst - src) * bh;
                    }
                break;
            }
            case MatrixFx::Pulsing:
            {
                if (s.frameNo == 0) for (int b = 0; b < kBins; ++b) { s.timer[(size_t) b] = hash01 ((uint32_t) b * 13u + 3u) * p[0] / 1000.0f; s.hold[(size_t) b] = 1.0f; }
                const float tm = std::pow (p[4], (float) t);
                auto lim = [&] (float d)
                {
                    const float lo = 0.005f, hi = 2.0f;
                    if (d <= hi) return std::max (lo, d);
                    if (p[5] > 0.5f) return lo + std::fmod (d - lo, hi - lo);
                    const float span = hi - lo, u = std::fmod (d - lo, 2 * span); return lo + (u < span ? u : 2 * span - u);
                };
                const float l2on = std::log2 (std::max (1.0e-3f, p[2])), l2off = std::log2 (std::max (1.0e-3f, p[3]));
                for (int b = 0; b < kBins; ++b)
                {
                    const float oct = std::log2 (std::max (20.0f, (float) b * bh) / 1000.0f);
                    const float on = lim (p[0] / 1000.0f * std::exp2 (l2on * oct) * tm), off = lim (p[1] / 1000.0f * std::exp2 (l2off * oct) * tm);
                    float& tmr = s.timer[(size_t) b]; float& st = s.hold[(size_t) b];
                    tmr += hopS;
                    if (st > 0.5f && tmr > on) { st = 0; tmr = 0; } else if (st < 0.5f && tmr > off) { st = 1; tmr = 0; }
                    const float edge = std::min (1.0f, tmr / 0.006f);
                    magO[(size_t) b] = magA[(size_t) b] * (st > 0.5f ? edge : 1.0f - edge);
                    freqO[(size_t) b] = freqA[(size_t) b];
                }
                break;
            }
            case MatrixFx::Harmonizer:
            {
                for (int b = 0; b < kBins; ++b) { magO[(size_t) b] = p[6] * magA[(size_t) b]; freqO[(size_t) b] = freqA[(size_t) b]; }
                for (int k = 0; k < 3; ++k)
                {
                    const float r = std::pow (2.0f, p[(size_t) k] / 12.0f), lv = p[(size_t) (3 + k)];
                    if (lv <= 0) continue;
                    for (int b = 1; b < kBins; ++b) deposit (freqA[(size_t) b] * r, magA[(size_t) b] * lv);
                }
                break;
            }
            case MatrixFx::BinShift:
            {
                const int sh = (int) std::lround (p[0]);
                for (int b = 0; b < kBins; ++b) { magO[(size_t) b] = (1 - p[1]) * magA[(size_t) b]; freqO[(size_t) b] = freqA[(size_t) b]; }
                for (int b = 1; b < kBins; ++b) deposit (freqA[(size_t) b] + (float) sh * bh, magA[(size_t) b] * p[1]);
                break;
            }
            case MatrixFx::PitchShift:
            {
                peakOf (p[3]);
                const float r = std::pow (2.0f, p[0] / 12.0f);
                for (int b = 1; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) continue;
                    const float f = p[1] > 0.5f ? freqA[(size_t) b] + p[2] * bh : freqA[(size_t) b] + (freqA[(size_t) pkb] * (r - 1.0f));
                    deposit (f, magA[(size_t) b]);
                }
                break;
            }
            case MatrixFx::Stretch:
            {
                float beta = p[1];
                if (p[2] > 0)
                {
                    float v = 0, pk = 0; for (float m : magA) pk = std::max (pk, m);
                    if (! s.m2Set) { s.m2 = pk; s.m2Set = true; }
                    s.m2 = std::max (s.m2 * std::pow (0.995f, hopRatio), pk);
                    const float amp = std::min (1.0f, pk / std::max (1.0e-9f, s.m2));
                    switch ((int) std::lround (p[3]))
                    {
                        case 0: v = 2.0f * uni (s) - 1.0f; break;
                        case 1: v = std::sin (6.2831853f * p[4] * (float) t); break;
                        case 2: v = 2.0f * amp - 1.0f; break;
                        default: v = 1.0f - 2.0f * amp; break;
                    }
                    beta += p[2] * 0.5f * v;
                    if (p[5] > 0) { const float q = 0.02f + 0.3f * p[5]; beta = std::round (beta / q) * q; }
                }
                const float ny = 0.5f * sr;
                for (int b = 1; b < kBins; ++b)
                {
                    const float w = freqA[(size_t) b] / ny;
                    deposit (freqA[(size_t) b] + (beta * w + p[0] * w * w) * ny * 0.5f, magA[(size_t) b]);
                }
                break;
            }
            case MatrixFx::Tracing:
            {
                std::fill (keep.begin(), keep.end(), (uint8_t) 0);
                const int mode = (int) std::lround (p[0]);
                if (mode == 0)
                {
                    for (int b = 0; b < kBins; ++b) idx[(size_t) b] = b;
                    const int K = std::min (kBins, std::max (1, (int) p[1]));
                    std::nth_element (idx.begin(), idx.begin() + (K - 1), idx.end(), [&] (int x, int y) { return magA[(size_t) x] > magA[(size_t) y]; });
                    for (int k = 0; k < K; ++k) keep[(size_t) idx[(size_t) k]] = 1;
                }
                else if (mode == 1) { for (int b = 0; b < kBins; ++b) { const float f = (float) b * bh; keep[(size_t) b] = f >= p[3] && f <= p[4]; } }
                else
                {
                    float pk = 1.0e-12f; for (float m : magA) pk = std::max (pk, m);
                    const float thr = pk * std::pow (10.0f, p[5] / 20.0f);
                    for (int b = 0; b < kBins; ++b) keep[(size_t) b] = magA[(size_t) b] >= thr;
                }
                const bool inv = p[2] > 0.5f;
                for (int b = 0; b < kBins; ++b) { const bool k = (keep[(size_t) b] != 0) != inv; magO[(size_t) b] = k ? magA[(size_t) b] : 0.0f; freqO[(size_t) b] = freqA[(size_t) b]; }
                break;
            }
            case MatrixFx::Filterbank:
            {
                std::array<float, 64> centres {}; int nc = 0;
                const int count = std::max (1, std::min (64, (int) p[2]));
                for (int k = 0; k < count; ++k)
                {
                    float f = p[1] < 0.01f ? p[0] * (float) (k + 1) : p[0] * std::pow (2.0f, p[1] * (float) k / 12.0f);
                    f *= std::pow (2.0f, p[4] * (2.0f * hash01 ((uint32_t) k * 31u + seed) - 1.0f) / 1200.0f);
                    if (f < 0.45f * sr) centres[(size_t) nc++] = std::log2 (std::max (1.0f, f));
                }
                const float wc = std::max (1.0f, p[3]);
                const float makeup = std::min (8.0f, std::sqrt ((float) kBins / std::max (1.0f, (float) nc * (wc / 30.0f) * 3.0f)));
                const float k = 1200.0f / wc;
                for (int b = 1; b < kBins; ++b)
                {
                    const float lf = std::log2 (std::max (1.0f, freqA[(size_t) b]));
                    float g = 0;
                    for (int i = 0; i < nc; ++i) { const float d = (lf - centres[(size_t) i]) * k; if (d > -4.0f && d < 4.0f) g += std::exp (-0.5f * d * d); }
                    magO[(size_t) b] = magA[(size_t) b] * std::min (1.0f, g) * makeup;
                    freqO[(size_t) b] = freqA[(size_t) b];
                }
                break;
            }
            case MatrixFx::GlidingFilters:
            {
                const int count = std::max (1, std::min (16, (int) p[0]));
                if (s.filtCount != count)
                {
                    s.filtCount = count;
                    for (int i = 0; i < count; ++i) { s.filt[(size_t) i] = {}; s.filt[(size_t) i][4] = uni (s) * p[4] / 1000.0f; }
                }
                for (int i = 0; i < count; ++i)
                {
                    auto& q = s.filt[(size_t) i];
                    if (q[5] < 0.5f)
                    {
                        q[4] -= hopS;
                        if (q[4] <= 0)
                        {
                            q[5] = 1; q[2] = 0; q[3] = std::max (0.05f, p[3] / 1000.0f * (0.5f + uni (s)));
                            q[0] = p[5] * std::pow (p[6] / std::max (1.0f, p[5]), uni (s));
                            q[1] = p[2] * (2.0f * uni (s) - 1.0f);
                        }
                    }
                    else
                    {
                        q[2] += hopS; q[0] *= std::pow (2.0f, q[1] * hopS);
                        if (q[2] > q[3] || q[0] < 30.0f || q[0] > 0.45f * sr) { q[5] = 0; q[4] = p[4] / 1000.0f * (0.5f + uni (s)); }
                    }
                }
                const float k = 1200.0f / std::max (1.0f, p[1]);
                std::array<float, 16> lc {}, env {};
                for (int i = 0; i < count; ++i)
                {
                    const auto& q = s.filt[(size_t) i];
                    env[(size_t) i] = q[5] < 0.5f ? 0.0f : std::min ({ 1.0f, q[2] / (0.2f * q[3]), (q[3] - q[2]) / (0.2f * q[3]) });
                    lc[(size_t) i] = std::log2 (std::max (1.0f, q[0]));
                }
                for (int b = 1; b < kBins; ++b)
                {
                    const float lf = std::log2 (std::max (1.0f, freqA[(size_t) b]));
                    float g = 0;
                    for (int i = 0; i < count; ++i)
                    {
                        if (env[(size_t) i] <= 0.0f) continue;
                        const float d = (lf - lc[(size_t) i]) * k;
                        if (d > -4.0f && d < 4.0f) g += env[(size_t) i] * std::exp (-0.5f * d * d);
                    }
                    magO[(size_t) b] = magA[(size_t) b] * std::min (1.0f, g) * 4.0f;
                    freqO[(size_t) b] = freqA[(size_t) b];
                }
                break;
            }
            case MatrixFx::Granulation:
            {
                if (hist == nullptr) { passThrough(); break; }
                std::copy (magA.begin(), magA.end(), histMag (c, s.histHead)); std::copy (freqA.begin(), freqA.end(), histFreq (c, s.histHead));
                const int newest = s.histHead;
                s.histHead = (s.histHead + 1) % histFrames; s.histCount = std::min (s.histCount + 1, histFrames);
                const int bands = std::max (1, (int) p[1]);
                const float grainS = std::max (0.01f, p[0] / 1000.0f);
                for (int k = 0; k < bands; ++k)
                {
                    const float lo = 40.0f * std::pow (18000.0f / 40.0f, (float) k / bands), hi = 40.0f * std::pow (18000.0f / 40.0f, (float) (k + 1) / bands);
                    const float off = hash01 ((uint32_t) k * 101u + seed);
                    const int gi = (int) std::floor (t / grainS + off);
                    const uint32_t key = (uint32_t) (gi * 977 + k * 131) + seed * 17u;
                    if (hash01 (key) > p[4]) continue;
                    const float delay = p[2] / 1000.0f * hash01 (key * 3u + 1u);
                    const float shift = std::pow (2.0f, p[3] * (2.0f * hash01 (key * 5u + 2u) - 1.0f) / 12.0f);
                    const float ph = (float) std::fmod (t / grainS + off, 1.0);
                    const float env = std::sin (3.14159265f * ph);
                    const int back = (int) std::lround (delay * sr / hop);
                    if (back >= s.histCount) continue;
                    const int slot = (newest - back + 2 * histFrames) % histFrames;
                    const float* hm = histMag (c, slot); const float* hf = histFreq (c, slot);
                    for (int b = (int) (lo / bh); b <= std::min (kBins - 1, (int) (hi / bh)); ++b) deposit (hf[b] * shift, hm[b] * env * 1.4f);
                }
                break;
            }
            case MatrixFx::Emergence:
            {
                peakOf (p[4]);
                for (int b = 0; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) { magO[(size_t) b] = magA[(size_t) b] * (1 - p[2]); freqO[(size_t) b] = freqA[(size_t) b]; continue; }
                    const float pitch = 12.0f * std::log2 (std::max (20.0f, freqA[(size_t) pkb]) / 440.0f);
                    const uint32_t key = (uint32_t) (int) std::lround (pitch * 2.0f + 400.0f) * 2654435761u + seed;
                    const float rate = p[0] * (1.0f + p[1] * (2.0f * hash01 (key) - 1.0f) * 0.9f);
                    const float ph0 = 6.2831853f * hash01 (key ^ 0x5bd1e995u) + (c == 1 ? 3.14159265f * p[3] : 0.0f);
                    const float env = (1 - p[2]) + p[2] * std::pow (0.5f + 0.5f * std::sin (6.2831853f * rate * (float) t + ph0), 1.5f);
                    magO[(size_t) b] = magA[(size_t) b] * env; freqO[(size_t) b] = freqA[(size_t) b];
                }
                break;
            }
            case MatrixFx::PartialGlide:
            {
                peakOf (p[4]);
                for (int b = 1; b < kBins; ++b)
                {
                    const int pkb = owner[(size_t) b];
                    if (pkb < 0) { deposit (freqA[(size_t) b], magA[(size_t) b]); continue; }
                    const float pitch = 12.0f * std::log2 (std::max (20.0f, freqA[(size_t) pkb]) / 440.0f);
                    const uint32_t key = (uint32_t) (int) std::lround (pitch + 200.0f) * 2246822519u + seed;
                    const float dir = p[2] + (1.0f - std::fabs (p[2])) * (hash01 (key) < 0.5f ? -1.0f : 1.0f);
                    const float rate = p[0] * (1.0f - p[1] + p[1] * hash01 (key * 7u + 3u)) * (dir >= 0 ? 1.0f : -1.0f) * std::max (0.2f, std::fabs (dir));
                    const float st = std::max (-p[3], std::min (p[3], rate * (float) t));
                    deposit (freqA[(size_t) b] * std::pow (2.0f, st / 12.0f), magA[(size_t) b]);
                }
                break;
            }
            default: passThrough(); break;
        }
        ++s.frameNo;
    }
    void passThrough() noexcept { std::copy (magA.begin(), magA.end(), magO.begin()); std::copy (freqA.begin(), freqA.end(), freqO.begin()); }

    // ================================================================= time domain
    static float cubicAt (const float* x, int n, double pos) noexcept
    {
        if (x == nullptr || pos < 0 || pos >= n - 1) return 0.0f;
        const int i = (int) pos; const float f = (float) (pos - i);
        auto at = [&] (int k) { return x[std::min (n - 1, std::max (0, k))]; };
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        return ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f + y1);
    }
    // a delay line in delayMem: the sample `delay` samples before the one about to be written at index i
    float ringRead (int line, int64_t i, float delay) const noexcept
    {
        const double pos = (double) i - delay;
        if (pos < 0.0) return 0.0f;
        const int64_t i0 = (int64_t) pos; const float f = (float) (pos - (double) i0);
        const float* m = delayMem + (size_t) line * (size_t) ringSize;
        const int mask = ringSize - 1;
        auto at = [&] (int64_t k) { return k < 0 || k >= i ? 0.0f : m[(size_t) (k & mask)]; };
        const float y0 = at (i0 - 1), y1 = at (i0), y2 = at (i0 + 1), y3 = at (i0 + 2);
        return ((((0.5f * (y3 - y0) + 1.5f * (y1 - y2)) * f + (y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3)) * f + 0.5f * (y2 - y0)) * f + y1);
    }
    void ringWrite (int line, int64_t i, float v) noexcept { delayMem[(size_t) line * (size_t) ringSize + (size_t) (i & (ringSize - 1))] = v; }

    struct Grain { double q0 = 0, rho = 1; float amp = 0, gl = 1, gr = 1; int k = 0, n = 0; };
    struct Stream { int64_t c0 = 0, next = 0; int k = 0; float amp = 1, rho = 1, pan = 0; };
    float tdUni() noexcept { return (float) (tdRng() >> 8) * (1.0f / 16777216.0f); }
    void addGrain (const Grain& g) noexcept { if (numGrains < kGrains) grains[(size_t) numGrains++] = g; }
    void renderGrains (float& l, float& r, const HitView& hit) noexcept
    {
        for (int g = 0; g < numGrains;)
        {
            auto& G = grains[(size_t) g];
            const double q = G.q0 + G.k * G.rho;
            const bool beyond = q >= hit.len - 2;
            if (G.k >= G.n || (beyond && hit.final)) { grains[(size_t) g] = grains[(size_t) --numGrains]; continue; }
            if (! beyond)
            {
                const float w = std::sin (3.14159265f * (float) G.k / (float) G.n);
                const float a = G.amp * w * w;
                l += a * cubicAt (hit.l, hit.len, q) * G.gl;
                r += a * cubicAt (hit.r, hit.len, q) * G.gr;
            }
            ++G.k; ++g;
        }
    }

    void designBank() noexcept
    {
        const float* p = P();
        bankCount = 0;
        const int count = std::max (1, std::min (32, (int) p[1]));
        for (int k = 0; k < count; ++k)
        {
            const float f = p[0] * std::pow (2.0f, p[2] * (float) k / 12.0f);
            if (f >= 0.45f * sr) break;
            const float w0 = 6.2831853f * f / sr, al = std::sin (w0) / (2.0f * std::max (0.5f, p[3]));
            const float a0 = 1 + al;
            auto& c = bank[(size_t) bankCount++];
            c[0] = al / a0; c[1] = -al / a0; c[2] = -2 * std::cos (w0) / a0; c[3] = (1 - al) / a0;
            c[4] = std::pow (10.0f, p[4] * std::log2 (f / 1000.0f) / 20.0f) * std::sqrt (p[3] / 10.0f) / std::sqrt ((float) count) * 1.6f;
        }
    }

    void timeDomain (float xl, float xr, float& l, float& r, const HitView& hit) noexcept
    {
        const float* p = P();
        const int64_t i = inCount;
        const bool during = inputLen < 0;
        switch (spec.fx)
        {
            case MatrixFx::Chorus:
            {
                if (delayMem == nullptr) { l = xl; r = xr; break; }
                const int V = std::max (1, std::min (4, (int) p[4]));
                float wl = 0, wr = 0, fbL = 0, fbR = 0;
                for (int v = 0; v < V; ++v)
                {
                    const float ph = 6.2831853f * (p[0] * (float) ((double) i / sr) + (float) v / V);
                    const float dl = (p[2] + p[1] * (0.5f + 0.5f * std::sin (ph))) / 1000.0f * sr;
                    const float dr = (p[2] + p[1] * (0.5f + 0.5f * std::sin (ph + 1.5708f))) / 1000.0f * sr;
                    const float yl = ringRead (0, i, std::min (dl, (float) ringSize - 8.0f)), yr = ringRead (1, i, std::min (dr, (float) ringSize - 8.0f));
                    wl += yl; wr += yr; if (v == 0) { fbL = yl; fbR = yr; }
                }
                ringWrite (0, i, xl + p[3] * fbL); ringWrite (1, i, xr + p[3] * fbR);
                l = (1 - p[5]) * xl + p[5] * wl / std::sqrt ((float) V);
                r = (1 - p[5]) * xr + p[5] * wr / std::sqrt ((float) V);
                break;
            }
            case MatrixFx::CombFilterbank:
            {
                if (delayMem == nullptr) { l = xl; r = xr; break; }
                const int count = std::max (1, std::min (12, (int) p[1]));
                const float g = 1.0f / std::sqrt ((float) count) * (1.0f - p[3]) * 4.0f;
                float sl = 0, sr2 = 0;
                for (int k = 0; k < count; ++k)
                {
                    const float f = p[0] * std::pow (2.0f, p[2] * (float) k / 12.0f);
                    const float D = std::min ((float) ringSize - 8.0f, sr / std::max (20.0f, f));
                    const float yl = ringRead (2 * k, i, D), yr = ringRead (2 * k + 1, i, D);
                    float& a = lp[(size_t) (2 * k)]; float& b = lp[(size_t) (2 * k + 1)];
                    a += (1.0f - p[4]) * (yl - a); b += (1.0f - p[4]) * (yr - b);
                    ringWrite (2 * k, i, xl + p[3] * a); ringWrite (2 * k + 1, i, xr + p[3] * b);
                    sl += yl; sr2 += yr;
                }
                l = p[5] * g * sl + (during ? (1 - p[5]) * xl : 0.0f);
                r = p[5] * g * sr2 + (during ? (1 - p[5]) * xr : 0.0f);
                break;
            }
            case MatrixFx::SuperFilterbank:
            {
                float sl = 0, sr2 = 0;
                for (int k = 0; k < bankCount; ++k)
                {
                    const auto& c = bank[(size_t) k]; auto& q = bq[(size_t) k];
                    const float yl = c[0] * xl + c[1] * q[1] - c[2] * q[2] - c[3] * q[3]; q[1] = q[0]; q[0] = xl; q[3] = q[2]; q[2] = yl;
                    const float yr = c[0] * xr + c[1] * q[5] - c[2] * q[6] - c[3] * q[7]; q[5] = q[4]; q[4] = xr; q[7] = q[6]; q[6] = yr;
                    sl += c[4] * yl; sr2 += c[4] * yr;
                }
                l = p[5] * sl + (during ? (1 - p[5]) * xl : 0.0f);
                r = p[5] * sr2 + (during ? (1 - p[5]) * xr : 0.0f);
                break;
            }
            case MatrixFx::GrainStreamer:
            {
                const float g = std::max (0.005f, p[0] / 1000.0f);
                const int gN = std::max (8, (int) (g * sr)), reps = std::max (1, (int) p[1]);
                const float stream = std::max (0.005f, p[2] / 1000.0f), cap = std::max (0.01f, p[3] / 1000.0f);
                if (during && (double) i >= nextCapture)
                {   // a new capture: its grains are taken from here
                    if (numStreams < kStreams) { Stream s; s.c0 = i; s.next = i; s.pan = 0.6f * (2.0f * tdUni() - 1.0f); streams[(size_t) numStreams++] = s; }
                    nextCapture += (double) cap * sr;
                }
                for (int k = 0; k < numStreams;)
                {
                    auto& s = streams[(size_t) k];
                    if (i >= s.next)
                    {
                        const float pan = s.pan * ((s.k & 1) ? -1.0f : 1.0f);
                        Grain G; G.q0 = (double) s.c0; G.rho = s.rho; G.amp = 0.6f * s.amp; G.n = gN;
                        G.gl = std::sqrt (0.5f * (1 - pan)) * 1.41f; G.gr = std::sqrt (0.5f * (1 + pan)) * 1.41f;
                        addGrain (G);
                        s.amp *= p[4]; s.rho *= std::pow (2.0f, p[5] / 12.0f);
                        ++s.k; s.next = s.c0 + (int64_t) ((double) s.k * stream * sr);
                    }
                    if (s.k >= reps) { streams[(size_t) k] = streams[(size_t) --numStreams]; continue; }
                    ++k;
                }
                renderGrains (l, r, hit);
                break;
            }
            case MatrixFx::IdeeFixer:
            {
                const float g = std::max (0.005f, p[2] / 1000.0f), sp = std::max (0.002f, p[3] / 1000.0f);
                const double total = (double) p[6] * sr;
                while ((double) i >= nextCloud && nextCloud < total)
                {
                    const int n = std::max (4, hit.len);
                    Grain G;
                    G.q0 = std::min (0.999f, std::max (0.0f, p[0] + p[1] * (tdUni() * 2.0f - 1.0f))) * (float) n;
                    const float semis = p[4] + p[5] * (tdUni() * 2.0f - 1.0f);
                    G.rho = std::pow (2.0f, semis / 12.0f);
                    G.n = std::max (8, (int) (g * sr));
                    const float pan = tdUni() * 2.0f - 1.0f;
                    G.gl = std::sqrt (0.5f * (1 - pan)) * 1.41f; G.gr = std::sqrt (0.5f * (1 + pan)) * 1.41f;
                    G.amp = 0.7f;
                    addGrain (G);
                    nextCloud += (double) sp * (0.6f + 0.8f * tdUni()) * sr;
                }
                renderGrains (l, r, hit);
                // the cloud has its own length: it ends there whatever the hit does
                if ((double) i >= total && numGrains == 0) { if (inputLen < 0) inputLen = inCount; }
                break;
            }
            default: l = xl; r = xr; break;
        }
    }

    // ================================================================= Bounce Delay
    struct BState { double q = 0, dPrev = 0; float lpL = 0, lpR = 0; int n = 0, seg = 1; bool on = false, over = false; };

    // the landings, as BounceDelay::plan places them (times in seconds from the hit)
    void planBounces() noexcept
    {
        const auto& p = spec.bounce;
        RateSpec gr = p.rate; gr.beats = p.gridBeats; gr.ms = p.gridMs;
        const SwingGrid grid = SwingGrid::make (gr, bpm, hitTime, 0.0);
        const double e = std::min (0.97, std::max (0.2, (double) p.bounciness));
        double gap = std::max (grid.step, p.rate.stepSeconds (bpm)), phys = 0.0, amp = 1.0;
        int64_t last = grid.nearest (hitTime);
        std::mt19937 rng (p.seed * 2654435761u + 17u);
        numBounces = 0;
        const int maxB = std::min (kMaxBounces, std::max (1, p.maxBounces));
        for (int k = 1; k <= maxB; ++k)
        {
            phys += gap;
            amp *= std::pow (e, 0.6);
            int64_t idxG = grid.nearest (hitTime + phys);
            if (idxG <= last) idxG = last + 1;
            last = idxG;
            Bounce b;
            b.t = grid.timeOf (idxG) - hitTime;
            b.gain = (float) amp;
            const float u = (float) (rng() >> 8) * (2.0f / 16777216.0f) - 1.0f;
            b.pan = std::min (1.0f, std::max (-1.0f, p.panCenter + p.panRandom * u));
            b.dist = 1.0f; b.width = 1.0f;
            bounces[(size_t) numBounces++] = b;
            gap *= e;
            if (amp < 0.003 || gap < 0.12 * grid.step) break;
        }
        const double span = numBounces == 0 ? 1.0 : std::max (1.0e-3, bounces[(size_t) numBounces - 1].t);
        const float rNear = std::max (0.5f, std::min (p.distStart, p.distEnd));
        for (int k = 0; k < numBounces; ++k)
        {
            auto& b = bounces[(size_t) k];
            const float u = (float) (b.t / span);
            b.dist = std::max (0.3f, p.distStart + (p.distEnd - p.distStart) * u);
            b.gain *= std::pow (rNear / std::max (b.dist, rNear * 0.5f), 0.7f);
            b.width = p.width * std::min (1.6f, std::max (0.08f, std::sqrt (1.0f / b.dist) * 1.2f));
        }
        // the flight: landing points in the plane (the hit itself is the first)
        path[0] = { 0.0, p.distStart * std::sin (p.panCenter * 1.4f), p.distStart * std::cos (p.panCenter * 1.4f) };
        for (int k = 0; k < numBounces; ++k) path[(size_t) k + 1] = { bounces[(size_t) k].t, bounces[(size_t) k].dist * std::sin (bounces[(size_t) k].pan * 1.4f), bounces[(size_t) k].dist * std::cos (bounces[(size_t) k].pan * 1.4f) };
        bounceEnd = numBounces == 0 ? 0 : (int64_t) ((bounces[(size_t) numBounces - 1].t + p.tail + 0.05) * sr);
    }

    struct Pt { double t; float x, y; };
    double distanceAt (double t, int& seg) const noexcept
    {
        const int np = numBounces + 1;
        if (t <= path[0].t) return std::hypot ((double) path[0].x, (double) path[0].y, (double) BounceDelay::kEar);
        while (seg < np && path[(size_t) seg].t < t) ++seg;
        if (seg >= np) { const auto& q = path[(size_t) np - 1]; return std::hypot ((double) q.x, (double) q.y, (double) BounceDelay::kEar); }
        const auto& a = path[(size_t) seg - 1]; const auto& b = path[(size_t) seg];
        const double G = std::max (1.0e-4, b.t - a.t), s = t - a.t, u = s / G;
        const double x = a.x + (b.x - a.x) * u, y = a.y + (b.y - a.y) * u;
        const double z = 0.5 * BounceDelay::kGravity * G * s - 0.5 * BounceDelay::kGravity * s * s;
        return std::hypot (x, y, z - BounceDelay::kEar);
    }

    void bounce (const float* inL, const float* inR, float* outL, float* outR, int n, const HitView& hit) noexcept
    {
        const auto& p = spec.bounce;
        const float kappa = 1.0f + 79.0f * p.speed * p.speed;
        const double h = 1.0 / sr;
        const int tailLen = std::max (16, (int) ((double) p.tail * sr)), fadeLen = std::max (1, (int) (0.05f * sr));
        for (int i = 0; i < n; ++i)
        {
            const int64_t s = inCount++;
            float l = spec.wetOnly ? 0.0f : inL[i], r = spec.wetOnly ? 0.0f : inR[i];
            // landings due now
            while (nextBounce < numBounces && (int64_t) std::llround (bounces[(size_t) nextBounce].t * sr) <= s)
            {
                auto& st = bstate[(size_t) nextBounce];
                if (! st.over) { st = {}; st.on = true; st.seg = 1; st.dPrev = distanceAt (bounces[(size_t) nextBounce].t, st.seg); }
                ++nextBounce;
            }
            bool any = false;
            for (int k = 0; k < nextBounce; ++k)
            {
                auto& st = bstate[(size_t) k];
                if (! st.on) continue;
                any = true;
                const auto& b = bounces[(size_t) std::min (k, std::max (0, numBounces - 1))];
                if (st.n >= tailLen) { st.on = false; st.over = true; continue; }
                const double t = b.t + (st.n + 1) * h;
                const double d = distanceAt (t, st.seg);
                const double vr = (d - st.dPrev) * sr; st.dPrev = d;
                const double rho = std::min (4.0, std::max (0.25, 1.0 / (1.0 + kappa * vr / BounceDelay::kSpeedOfSound)));
                if (st.q >= hit.len - 2)
                {
                    if (hit.final) { st.on = false; st.over = true; }
                    ++st.n;
                    continue;                                 // the hit has not got that far yet: wait for it
                }
                float bl = cubicAt (hit.l, hit.len, st.q), br = cubicAt (hit.r, hit.len, st.q);
                st.q += rho;
                const float mid = 0.5f * (bl + br), side = 0.5f * (bl - br) * b.width;
                bl = mid + side; br = mid - side;
                const float fc = std::min (0.45f * sr, std::max (1500.0f, 20000.0f / (1.0f + (std::max (b.dist, 1.0f) - 1.0f) / 12.0f)));
                const float air = b.dist > 1.0f ? 1.0f - std::exp (-6.2831853f * fc / sr) : 1.0f;
                st.lpL += air * (bl - st.lpL); st.lpR += air * (br - st.lpR);
                const float env = st.n > tailLen - fadeLen ? (float) (tailLen - st.n) / (float) fadeLen : 1.0f;
                const float g = b.gain * p.wet * env;
                l += g * st.lpL * std::sqrt (std::max (0.0f, 0.5f * (1.0f - b.pan))) * 1.41421356f;
                r += g * st.lpR * std::sqrt (std::max (0.0f, 0.5f * (1.0f + b.pan))) * 1.41421356f;
                ++st.n;
            }
            outL[i] = l; outR[i] = r;
            if (inputLen >= 0 && nextBounce >= numBounces && ! any) done = true;
            if (s > bounceEnd + (int64_t) (4.0 * sr) && inputLen >= 0) done = true;
        }
    }

    // ================================================================= state
    float sr = 48000.0f;
    int hop = 256;
    LiveFxPools* pools = nullptr;
    LiveHistPool* histPool = nullptr;
    float* hist = nullptr;
    int histFrames = 64;
    LiveSpec spec;
    bool spectral = false, active = false, done = false;
    double bpm = 120.0, hitTime = 0.0;
    int64_t inCount = 0, inputLen = -1, tailN = 4800, fadeN = 1440;
    double ein = 0.0, eout = 0.0;
    float gain = 1.0f, gainTarget = 1.0f, gainCoef = 0.001f, prior = 1.0f, rawPeak = 1.0e-9f, learned = 0.0f;
    bool priorExact = false;
    std::array<Chan, 2> ch;
    std::vector<float> eRing, magA, freqA, magO, freqO, re, im;
    std::vector<int> owner, peaks, idx;
    std::vector<uint8_t> keep;
    // time domain
    float* delayMem = nullptr; int ringSize = 4096;
    std::array<float, 24> lp {};
    std::array<std::array<float, 5>, 32> bank {}; int bankCount = 0;
    std::array<std::array<float, 8>, 32> bq {};
    std::array<Grain, kGrains> grains {}; int numGrains = 0;
    std::array<Stream, kStreams> streams {}; int numStreams = 0;
    std::mt19937 tdRng { 1u };
    double nextCapture = 0.0, nextCloud = 0.0;
    // Bounce Delay
    std::array<Bounce, kMaxBounces> bounces {}; int numBounces = 0, nextBounce = 0;
    std::array<Pt, kMaxBounces + 1> path {};
    std::array<BState, kMaxBounces> bstate {};
    int64_t bounceEnd = 0;
};
} // namespace nb
