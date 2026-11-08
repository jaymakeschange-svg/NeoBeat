// NeoBeat — MaterialLab engine v2/v3 "living modal body", ported to real time.
//
// A line-by-line port of materiallab/engine2.py (render_physical) by Marbelite,
// playing the learned material files (engine_v3_materials). Per mode: a
// beating doublet split by delta Hz; damping law sigma(f) = b1 + b3 (f/1k)^2
// times a per-mode residual; contact damping that switches on at cx_t below
// cx_f; an upward energy cascade (the struck body's energy feeds receiving
// modes after a delay); a strike ramp; a shared slow rocking of the low body;
// per-mode tension glide plus a global elastic term from the body's energy.
// Under the modes: the contact click (band noise with a near-instant onset)
// and the dense body layer (band noise decaying by the same damping law).
//
// Envelopes run at the learner's control rate (one frame per 64 samples) and
// are interpolated per sample exactly as the Python does; phases run at the
// sample rate. Each mode is heard from two listening points (MaterialLab
// spatial.py): left = 1 + s, right = 1 - s, with a side weight s whose spread
// grows with frequency, so the mono sum stays the fitted sound.
//
// The learner's own variation laws (tools/variations_v2.py) and the strike
// law (gesture.py: faster impacts shorten the contact, which brings out the
// higher modes) are applied to the parameters at note-on.
#pragma once

#include "Butterworth.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace nb
{
// ---------------------------------------------------------------------------
// Material data (one learned body)
// ---------------------------------------------------------------------------
struct Engine2Material
{
    static constexpr int kBands = 16;
    std::string name;
    int samples = 48000;    // length of the learned hit
    // per mode
    std::vector<float> f, a, rho, delta, beta, eps, glide, tau_g;
    // global laws
    float b1 = 30, b3 = 0.1f, C = 1, gamma = 0, tau_c = 0.05f, G = 0, tau_a = 0.002f;
    float cx_amt = 0, cx_t = 1, cx_f = 1000, lb_m = 0, lb_rate = 2, lb_f = 500, tau_n = 0.02f;
    std::array<float, kBands> noise {}, dn {}, dn_rho {};
    int modes() const noexcept { return (int) f.size(); }
};

// Text form written by tools/materiallab/export_nbm.py:
//   name = clank / samples = 41246 / b1 = ... (one line per global law)
//   noise v0 .. v15 / dn ... / dn_rho ...
//   mode f a rho delta beta eps glide tau_g
inline std::string parseEngine2Material (const std::string& text, Engine2Material& m)
{
    std::istringstream in (text);
    std::string line;
    int lineNo = 0;
    while (std::getline (in, line))
    {
        ++lineNo;
        const auto hash = line.find ('#');
        if (hash != std::string::npos) line = line.substr (0, hash);
        std::istringstream ls (line);
        std::string key;
        if (! (ls >> key)) continue;
        auto err = [&] (const std::string& s) { return "line " + std::to_string (lineNo) + ": " + s; };
        if (key == "mode")
        {
            float v[8];
            for (float& x : v) if (! (ls >> x)) return err ("mode needs 8 numbers");
            m.f.push_back (v[0]); m.a.push_back (v[1]); m.rho.push_back (v[2]); m.delta.push_back (v[3]);
            m.beta.push_back (v[4]); m.eps.push_back (v[5]); m.glide.push_back (v[6]); m.tau_g.push_back (v[7]);
            continue;
        }
        if (key == "noise" || key == "dn" || key == "dn_rho")
        {
            auto& arr = key == "noise" ? m.noise : (key == "dn" ? m.dn : m.dn_rho);
            for (float& x : arr) if (! (ls >> x)) return err (key + " needs 16 numbers");
            continue;
        }
        std::string eq, value;
        if (! (ls >> eq) || eq != "=") return err ("expected 'key = value'");
        std::getline (ls, value);
        const auto a0 = value.find_first_not_of (' ');
        value = a0 == std::string::npos ? "" : value.substr (a0);
        const float v = (float) std::atof (value.c_str());
        if (key == "name") m.name = value;
        else if (key == "samples") m.samples = (int) v;
        else if (key == "b1") m.b1 = v; else if (key == "b3") m.b3 = v; else if (key == "C") m.C = v;
        else if (key == "gamma") m.gamma = v; else if (key == "tau_c") m.tau_c = v; else if (key == "G") m.G = v;
        else if (key == "tau_a") m.tau_a = v; else if (key == "cx_amt") m.cx_amt = v; else if (key == "cx_t") m.cx_t = v;
        else if (key == "cx_f") m.cx_f = v; else if (key == "lb_m") m.lb_m = v; else if (key == "lb_rate") m.lb_rate = v;
        else if (key == "lb_f") m.lb_f = v; else if (key == "tau_n") m.tau_n = v;
        else return err ("unknown key '" + key + "'");
    }
    if (m.f.empty()) return "no modes";
    return {};
}

// ---------------------------------------------------------------------------
// The learner's laws, applied to a copy of the material at note-on
// ---------------------------------------------------------------------------
enum class Variation { AsLearned, Higher, BiggerBody, LongerRing, MoreWarble, Frozen, Bloom, SoftStriker,
                       BuoyantRise, ElasticDrop, StruckElsewhere, Count };

inline const char* variationName (Variation v)
{
    static const char* names[] = { "as learned", "higher", "bigger body", "longer ring", "more warble", "frozen",
                                   "bloom", "soft striker", "buoyant rise", "elastic drop", "struck elsewhere" };
    return names[(int) v];
}

// tools/variations_v2.py vary()
inline void applyVariation (Engine2Material& q, Variation kind)
{
    const int k = q.modes();
    switch (kind)
    {
        case Variation::Higher:      for (auto& f : q.f) f *= std::exp2 (5.0f / 12.0f); break;
        case Variation::BiggerBody:  for (auto& f : q.f) f *= std::exp2 (-7.0f / 12.0f); q.b1 *= 0.7f; q.b3 *= 0.7f; break;
        case Variation::LongerRing:  q.b1 *= 0.4f; q.b3 *= 0.4f; break;
        case Variation::MoreWarble:
            for (int i = 0; i < k; ++i) { q.delta[(size_t) i] = std::min (39.0f, std::max (0.0f, q.delta[(size_t) i] * 2.0f + 1.0f)); q.beta[(size_t) i] = std::max (q.beta[(size_t) i], 0.35f); }
            q.lb_m = std::min (1.0f, q.lb_m * 2.0f + 0.2f);
            break;
        case Variation::Frozen:
            for (int i = 0; i < k; ++i) { q.beta[(size_t) i] = 0; q.eps[(size_t) i] = -6.0f; q.glide[(size_t) i] = 0; }
            q.lb_m = 0;
            break;
        case Variation::Bloom:       q.C = std::min (q.C * 4.0f, 2900.0f); q.tau_c = std::min (q.tau_c * 2.0f, 0.45f); break;
        case Variation::SoftStriker:
            q.tau_a = std::min (q.tau_a * 8.0f, 0.025f);
            for (int i = 0; i < k; ++i) { const float r = q.f[(size_t) i] / 2500.0f; q.a[(size_t) i] /= 1.0f + r * r; }
            break;
        case Variation::BuoyantRise: for (int i = 0; i < k; ++i) { q.glide[(size_t) i] = -0.7f; q.tau_g[(size_t) i] = 0.35f; } break;
        case Variation::ElasticDrop: q.G = 2.0f; break;
        case Variation::StruckElsewhere:
        {
            // a smooth random reshaping of the mode levels (the learner used rng seed 9; any smooth field will do)
            std::mt19937 rng (9u);
            std::normal_distribution<float> nd;
            std::vector<float> w ((size_t) k + 8);
            for (auto& v : w) v = nd (rng);
            for (int i = 0; i < k; ++i)
            {
                float s = 0;
                for (int j = 0; j < 5; ++j) s += w[(size_t) (i + 2 + j)];
                q.a[(size_t) i] *= std::exp (0.9f * s / 5.0f);
            }
            break;
        }
        default: break;
    }
}

// gesture.py strike(): the same material struck at another speed and tuned.
inline void applyStrike (Engine2Material& q, float speed, float pitchFactor)
{
    float fmin = 1.0e9f;
    for (auto& f : q.f) { f *= pitchFactor; fmin = std::min (fmin, f); }
    const float s = std::max (speed, 1.0e-3f);
    const float e = 0.12f * std::log (s);
    for (int i = 0; i < q.modes(); ++i)
        q.a[(size_t) i] *= s * std::pow (q.f[(size_t) i] / std::max (fmin, 1.0f), e);
    q.tau_a = std::min (0.004f, std::max (5.0e-5f, q.tau_a * std::pow (s, -0.2f)));
    for (auto& v : q.noise) v *= s;
}

// ---------------------------------------------------------------------------
// Shared noise: the learner's band noises, pre-filtered once (unit RMS each)
// ---------------------------------------------------------------------------
class MaterialNoise
{
public:
    static constexpr int kBands = Engine2Material::kBands;
    static constexpr int kLen = 1 << 17;   // 2.7 s at 48 kHz, read with independent offsets

    void prepare (double fs, uint32_t seed = 11)
    {
        if (fs == rate && ! table.empty()) return;
        rate = fs;
        table.assign ((size_t) kBands * kLen, 0.0f);
        const double top = std::min (20000.0, 0.45 * fs);
        for (int b = 0; b <= kBands; ++b) edges[(size_t) b] = 60.0 * std::pow (top / 60.0, (double) b / kBands);
        std::mt19937_64 rng (seed);
        std::normal_distribution<double> nd;
        for (int b = 0; b < kBands; ++b)
        {
            SosCascade f (butterBandpass (4, edges[(size_t) b], edges[(size_t) b + 1], fs));
            for (int i = 0; i < 4096; ++i) f.process (nd (rng));          // settle (the learner skips 2048)
            double e = 0;
            float* row = &table[(size_t) b * kLen];
            for (int i = 0; i < kLen; ++i) { row[i] = (float) f.process (nd (rng)); e += (double) row[i] * row[i]; }
            const float g = (float) (1.0 / std::sqrt (e / kLen + 1e-30));
            for (int i = 0; i < kLen; ++i) row[i] *= g;
            centres[(size_t) b] = (float) std::sqrt (edges[(size_t) b] * edges[(size_t) b + 1]);
        }
    }
    const float* band (int b) const noexcept { return &table[(size_t) b * kLen]; }
    float centre (int b) const noexcept { return centres[(size_t) b]; }
    double sampleRate() const noexcept { return rate; }

private:
    std::vector<float> table;
    std::array<double, kBands + 1> edges {};
    std::array<float, kBands> centres {};
    double rate = 0.0;
};

// ---------------------------------------------------------------------------
// The voice
// ---------------------------------------------------------------------------
struct BodyPlay
{
    float speed = 1.0f;        // impact speed relative to the learned strike (velocity)
    float pitch = 1.0f;        // frequency ratio
    float decay = 1.0f;        // > 1 rings longer (damping law scaled by 1/decay)
    Variation variation = Variation::AsLearned;
    float width = 1.0f;        // spread of the two listening points (0 = mono)
    float fd = 12000.0f, q = 1.2f;   // spatial.CALIBRATED: where the points share half their signal
    float level = 1.0f;
    uint32_t seed = 1;
};

class MaterialBody
{
public:
    static constexpr int kHop = 64;
    static constexpr int kMax = 160;
    static constexpr int kBands = Engine2Material::kBands;

    void prepare (float sampleRate, std::shared_ptr<const MaterialNoise> sharedNoise)
    {
        fs = sampleRate;
        noise = std::move (sharedNoise);
        active = false;
    }

    bool isActive() const noexcept { return active; }
    void stop() noexcept { active = false; }
    // pitch bend while ringing (semitones), e.g. a gesture lane
    void setBend (float semis) noexcept { bendRatio = std::exp2 (semis / 12.0f); }

    void start (const Engine2Material& learned, const BodyPlay& play)
    {
        m = learned;                                     // the copy the laws act on (no allocation once warmed up)
        applyVariation (m, play.variation);
        applyStrike (m, play.speed, play.pitch);
        if (play.decay != 1.0f) { const float d = 1.0f / std::max (play.decay, 0.05f); m.b1 *= d; m.b3 *= d; }
        k = std::min (m.modes(), kMax);
        dt = (float) kHop / fs;
        level = play.level;
        // per-mode constants
        for (int i = 0; i < k; ++i)
        {
            const float f0 = m.f[(size_t) i];
            sigma[i] = (m.b1 + m.b3 * (f0 / 1000.0f) * (f0 / 1000.0f)) * std::exp (m.rho[(size_t) i]);
            cxGain[i] = m.cx_amt * sigmoid ((m.cx_f - f0) / (0.25f * m.cx_f));
            gain[i] = m.C * std::pow (f0 / 1000.0f, m.gamma) * std::exp (m.eps[(size_t) i]);
            low[i] = sigmoid ((m.lb_f - f0) / (0.2f * m.lb_f));
            logf0[i] = std::log (f0);
            decayAcc[i] = 0.0f; casc[i] = 0.0f;
            // the two oscillators of the doublet start in cosine phase (strike_phases)
            z1r[i] = 0.0f; z1i[i] = 1.0f; z2r[i] = 0.0f; z2i[i] = 1.0f;
        }
        // listening points (spatial.pickup_weights; any seeded normal draw)
        std::mt19937 rng (21u + play.seed * 7919u);
        std::normal_distribution<float> nd;
        for (int i = 0; i < k; ++i)
        {
            const float r = std::min (0.999f, std::max (-0.999f, 1.0f / (1.0f + std::pow (m.f[(size_t) i] / play.fd, play.q))));
            const float s = nd (rng) * std::sqrt ((1.0f - r) / (1.0f + r)) * play.width;
            wl[i] = 1.0f + s; wr[i] = 1.0f - s;
        }
        // body noise pair per band (spatial.body_noise_pair): left = a + sig b, right = a - sig b
        for (int b = 0; b < kBands; ++b)
        {
            const float c = noise->centre (b);
            const float r = std::min (1.0f, std::max (-1.0f, 1.0f / (1.0f + std::pow (c / play.fd, play.q))));
            bodySide[(size_t) b] = std::sqrt ((1.0f - r) / (1.0f + r)) * play.width;
            sigB[(size_t) b] = (m.b1 + m.b3 * (c / 1000.0f) * (c / 1000.0f)) * std::exp (m.dn_rho[(size_t) b]);
            cxB[(size_t) b] = m.cx_amt * sigmoid ((m.cx_f - c) / (0.25f * m.cx_f));
        }
        // independent offsets into the shared noise tables
        std::mt19937 orng (play.seed * 2654435761u + 5u);
        offClick = (int) (orng() % MaterialNoise::kLen);
        offA = (int) (orng() % MaterialNoise::kLen);
        offB = (int) (orng() % MaterialNoise::kLen);
        // energy normalisation of the elastic term: energy at frame 1
        {
            const float t1 = dt;
            double e1 = 0;
            for (int i = 0; i < k; ++i)
            {
                const float st = sigma[i] + cxGain[i] * sigmoid ((t1 - m.cx_t) / 0.004f);
                const float d = m.a[(size_t) i] * std::exp (-st * dt);
                e1 += (double) d * d;
            }
            energy1 = (float) std::max (e1, 1.0e-20);
        }
        frame = 0; n = 0; pos = 0;
        frameState (0, ampA, fA);       // frame 0
        frameState (1, ampB, fB);       // frame 1 (interpolation target)
        frame = 1;
        clickA = 1.0f; clickD = 1.0f; attB = 1.0f;
        clickAttMul = std::exp (-1.0f / (fs * 0.0002f));
        clickDecMul = std::exp (-1.0f / (fs * std::max (m.tau_n, 1.0e-4f)));
        bodyAttMul = std::exp (-1.0f / (fs * 0.0003f));
        computeBodyEnv (0.0f, envB0); computeBodyEnv (dt, envB1);
        maxSamples = (int64_t) (fs * 12.0f);
        setRotations();
        active = true;
    }

    // Renders one stereo frame (the object, dry; the place is applied after).
    void process (float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        if (! active) return;
        if (pos == kHop) advance();
        const float fr = (float) pos / (float) kHop;
        float l = 0.0f, r = 0.0f;
        for (int i = 0; i < k; ++i)
        {
            // rotate both oscillators of the doublet; the rotation itself turns a little each sample,
            // because the learner's frequency ramps linearly through the block
            const float a1 = z1r[i], b1 = z1i[i];
            z1r[i] = a1 * c1[i] - b1 * s1[i]; z1i[i] = a1 * s1[i] + b1 * c1[i];
            const float a2 = z2r[i], b2 = z2i[i];
            z2r[i] = a2 * c2[i] - b2 * s2[i]; z2i[i] = a2 * s2[i] + b2 * c2[i];
            const float cc = c1[i], ss = s1[i];
            c1[i] = cc * dc[i] - ss * ds[i]; s1[i] = cc * ds[i] + ss * dc[i];
            const float cc2 = c2[i], ss2 = s2[i];
            c2[i] = cc2 * dc[i] - ss2 * ds[i]; s2[i] = cc2 * ds[i] + ss2 * dc[i];
            const float amp = ampA[i] + (ampB[i] - ampA[i]) * fr;
            const float w = amp * ((1.0f - betaK[i]) * z1i[i] + betaK[i] * z2i[i]);
            l += w * wl[i]; r += w * wr[i];
        }
        // contact click: shared by both points; near-instant onset
        // envelopes at t = n / fs, then step them on
        const float envN = (1.0f - clickA) * clickD;
        const float att = 1.0f - attB;
        clickA *= clickAttMul; clickD *= clickDecMul; attB *= bodyAttMul;
        const int ic = (offClick + (int) n) & (MaterialNoise::kLen - 1);
        const int ia = (offA + (int) n) & (MaterialNoise::kLen - 1);
        const int ib = (offB + (int) n) & (MaterialNoise::kLen - 1);
        float click = 0.0f, bl = 0.0f, bs = 0.0f;
        const bool clickOn = envN > 1.0e-7f;
        for (int b = 0; b < kBands; ++b)
        {
            const float* row = noise->band (b);
            if (clickOn) click += m.noise[(size_t) b] * row[ic];
            const float eb = (envB0[(size_t) b] + (envB1[(size_t) b] - envB0[(size_t) b]) * fr) * att * m.dn[(size_t) b];
            if (eb != 0.0f)
            {
                const float av = row[ia], bv = row[ib];
                bl += eb * av;
                bs += eb * bodySide[(size_t) b] * bv;
            }
        }
        click *= envN;
        outL = (l + click + bl + bs) * level;
        outR = (r + click + bl - bs) * level;
        ++pos; ++n;
        if (n > maxSamples) active = false;
    }

    int64_t samplesPlayed() const noexcept { return n; }

private:
    static float sigmoid (float x) noexcept { return 1.0f / (1.0f + std::exp (-x)); }
    static float softplus (float x) noexcept { return x > 20.0f ? x : std::log1p (std::exp (x)); }

    // the learner's control-rate state for frame index fi (t = fi * dt); advances the scans by one frame
    void frameState (int fi, std::array<float, kMax>& amp, std::array<float, kMax>& fpath) noexcept
    {
        const float t = (float) fi * dt;
        const float cxs = sigmoid ((t - m.cx_t) / 0.004f);
        double sumD2 = 0.0;
        for (int i = 0; i < k; ++i)
        {
            const float sigT = sigma[i] + cxGain[i] * cxs;
            sigNow[i] = sigT;
            if (fi > 0) decayAcc[i] += sigT * dt;      // cumsum(sig_t) dt - sig_t[0] dt
            direct[i] = m.a[(size_t) i] * std::exp (-decayAcc[i]);
            sumD2 += (double) direct[i] * direct[i];
        }
        const float source = (float) std::sqrt (sumD2 + 1.0e-20) * (1.0f - std::exp (-t / m.tau_c));
        const float energy = (float) sumD2 / (energy1 + 1.0e-20f);
        const float elastic = 0.5f * std::log1p (std::min (3.0f, std::max (-0.9f, m.G)) * energy);
        const float strike = 1.0f - std::exp (-t / m.tau_a);
        const float rockPhase = std::cos (6.28318530718f * m.lb_rate * t);
        bool alive = false;
        for (int i = 0; i < k; ++i)
        {
            casc[i] = casc[i] * std::exp (-sigNow[i] * dt) + gain[i] * source * dt;
            float a = (direct[i] + casc[i]) * strike;
            a *= (1.0f + m.lb_m * low[i] * rockPhase) / (1.0f + m.lb_m * low[i]);
            const float lf = logf0[i] + m.glide[(size_t) i] * std::exp (-t / m.tau_g[(size_t) i]) + elastic;
            const float f = std::exp (lf) * bendRatio;
            a *= sigmoid ((0.46f * fs - f) / (0.005f * fs));
            amp[i] = a;
            fpath[i] = f;
            if (a > 1.0e-6f || t < 0.05f) alive = true;
        }
        aliveModes = alive;
    }

    void computeBodyEnv (float t, std::array<float, kBands>& env) noexcept
    {
        const float held = softplus ((t - m.cx_t) / 0.004f) * 0.004f;
        bool any = false;
        for (int b = 0; b < kBands; ++b)
        {
            const float e = std::exp (-sigB[(size_t) b] * t - cxB[(size_t) b] * held);
            env[(size_t) b] = e < 1.0e-7f ? 0.0f : e;
            any = any || (e * m.dn[(size_t) b] > 1.0e-7f);
        }
        aliveBody = any;
    }

    // called every kHop samples: the previous target becomes the start, compute the next frame
    void advance() noexcept
    {
        pos = 0;
        ampA = ampB; fA = fB;
        ++frame;
        frameState (frame, ampB, fB);
        envB0 = envB1;
        computeBodyEnv ((float) frame * dt, envB1);
        setRotations();
        if (! aliveModes && ! aliveBody && n > (int64_t) (0.1f * fs)) active = false;
    }

    void setRotations() noexcept
    {
        const float w = 6.28318530718f / fs;
        for (int i = 0; i < k; ++i)
        {
            // the learner's frequency at sample j of the block is fA + (fB - fA) j / 64
            const float f = fA[i];
            const float half = 0.5f * m.delta[(size_t) i];
            c1[i] = (float) std::cos ((double) w * (f - half)); s1[i] = (float) std::sin ((double) w * (f - half));
            c2[i] = (float) std::cos ((double) w * (f + half)); s2[i] = (float) std::sin ((double) w * (f + half));
            const double dw = (double) w * (fB[i] - fA[i]) / (double) kHop;
            dc[i] = (float) std::cos (dw); ds[i] = (float) std::sin (dw);
            betaK[i] = m.beta[(size_t) i];
            // keep both phasors on the unit circle
            const float g1 = 1.5f - 0.5f * (z1r[i] * z1r[i] + z1i[i] * z1i[i]);
            const float g2 = 1.5f - 0.5f * (z2r[i] * z2r[i] + z2i[i] * z2i[i]);
            z1r[i] *= g1; z1i[i] *= g1; z2r[i] *= g2; z2i[i] *= g2;
        }
    }

    using Arr = std::array<float, kMax>;
    Engine2Material m;
    std::shared_ptr<const MaterialNoise> noise;
    Arr sigma {}, cxGain {}, gain {}, low {}, logf0 {}, decayAcc {}, casc {}, direct {}, sigNow {};
    Arr ampA {}, ampB {}, fA {}, fB {}, wl {}, wr {}, betaK {};
    Arr z1r {}, z1i {}, z2r {}, z2i {}, c1 {}, s1 {}, c2 {}, s2 {}, dc {}, ds {};
    std::array<float, kBands> bodySide {}, sigB {}, cxB {}, envB0 {}, envB1 {};
    int k = 0, frame = 0, pos = 0, offClick = 0, offA = 0, offB = 0;
    int64_t n = 0, maxSamples = 0;
    float fs = 48000.0f, dt = 64.0f / 48000.0f, level = 1.0f, energy1 = 1.0f, bendRatio = 1.0f;
    float clickA = 1, clickD = 1, attB = 1, clickAttMul = 0, clickDecMul = 0, bodyAttMul = 0;
    bool active = false, aliveModes = true, aliveBody = true;
};
} // namespace nb
