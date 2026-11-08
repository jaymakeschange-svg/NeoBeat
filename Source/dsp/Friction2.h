// NeoBeat — MaterialLab friction v2 with the stretched strip and the snap, in real time.
//
// A port of materiallab/friction2.py (simulate, _simulate, mix) by Marbelite,
// playing the learned rubber materials (friction_v2_stretch_materials):
//   - the LuGre contact (Canudas de Wit et al. 1995): bristles that bend
//     before they let go, a Stribeck curve, viscous drag; run at 4x the sample
//     rate and filtered down (the learner's resample_poly filter), so no grit
//     folds back;
//   - per contact a tab (a mass on a spring) pulled by the hand; three
//     contacts along the peeling edge at slightly different rates and
//     pressures, with a rough surface wobbling the pressing force;
//   - the learned body: its motion at each contact feeds back into the
//     sliding speed (two-way coupling), so slips can lock onto a mode and
//     squeal; the rest of the rubber rubs between slips as fine contacts;
//   - the stretched strip: its singing modes follow the pull,
//     f = f0 sqrt(1 - sag d) / (1 + grow d); at the release the tab slaps
//     back, the strip goes slack, the object rings free until it is caught.
// The pull itself (hand speed, pressure, the hand's loudness contour) comes
// from the learner's drive for the recording; it can be played faster,
// slower, or from part-way through, which is what the sequencer uses.
#pragma once

#include "Butterworth.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace nb
{
struct FrictionMaterial
{
    std::string name;
    // Contact2
    float f_tab = 300, zeta_tab = 0.08f, mu_ratio = 0.55f, v_s = 0.05f, f_bristle = 12000, zeta_bristle = 0.6f;
    float viscous = 0.05f, rough = 0.15f, rough_hz = 60, body_mass = 30, spread = 0.12f, second_level = 0.35f;
    float tab_sound = 0.15f, rub_level = 0.01f, rub_hz = 4000, strip_sigma = 40, strip_mass = 5, strip_level = 1;
    float snap_level = 20, snap_ms = 1.5f, slack_ms = 15, free_ring = 1, catch_ms = 1000;
    int contacts = 3;
    // Body2 and Strip2
    std::vector<float> bf, bsigma, ba;
    std::vector<std::vector<float>> phi;          // [8][K]
    std::vector<float> sf0, sgrow, ssag, son, soff, sa;
    // the pull
    float pull_length = 60, release_s = -1, tab_norm = 1, v_median = 1, out_gain = 1;
    int hop = 240, samples = 0;
    std::vector<float> V, env, gain;
};

inline std::string parseFrictionMaterial (const std::string& text, FrictionMaterial& m)
{
    std::istringstream in (text);
    std::string line;
    int lineNo = 0;
    auto readList = [] (std::istringstream& ls, std::vector<float>& v) { float x; while (ls >> x) v.push_back (x); };
    while (std::getline (in, line))
    {
        ++lineNo;
        const auto hash = line.find ('#');
        if (hash != std::string::npos) line = line.substr (0, hash);
        std::istringstream ls (line);
        std::string key;
        if (! (ls >> key)) continue;
        if (key == "body.f") { readList (ls, m.bf); continue; }
        if (key == "body.sigma") { readList (ls, m.bsigma); continue; }
        if (key == "body.a") { readList (ls, m.ba); continue; }
        if (key == "body.phi") { m.phi.emplace_back(); readList (ls, m.phi.back()); continue; }
        if (key == "strip.f0") { readList (ls, m.sf0); continue; }
        if (key == "strip.grow") { readList (ls, m.sgrow); continue; }
        if (key == "strip.sag") { readList (ls, m.ssag); continue; }
        if (key == "strip.d_on") { readList (ls, m.son); continue; }
        if (key == "strip.d_off") { readList (ls, m.soff); continue; }
        if (key == "strip.a") { readList (ls, m.sa); continue; }
        if (key == "drive.V") { readList (ls, m.V); continue; }
        if (key == "drive.env") { readList (ls, m.env); continue; }
        if (key == "drive.gain") { readList (ls, m.gain); continue; }
        std::string eq, value;
        if (! (ls >> eq) || eq != "=") return "line " + std::to_string (lineNo) + ": expected 'key = value'";
        std::getline (ls, value);
        const auto a0 = value.find_first_not_of (' ');
        value = a0 == std::string::npos ? "" : value.substr (a0);
        const float v = (float) std::atof (value.c_str());
        if (key == "name") m.name = value;
        else if (key == "pull_length") m.pull_length = v; else if (key == "release_s") m.release_s = v;
        else if (key == "tab_norm") m.tab_norm = v; else if (key == "v_median") m.v_median = v; else if (key == "out_gain") m.out_gain = v;
        else if (key == "hop") m.hop = (int) v; else if (key == "samples") m.samples = (int) v;
        else if (key.rfind ("contact.", 0) == 0)
        {
            const std::string k = key.substr (8);
            float* dst = nullptr;
            if (k == "f_tab") dst = &m.f_tab; else if (k == "zeta_tab") dst = &m.zeta_tab; else if (k == "mu_ratio") dst = &m.mu_ratio;
            else if (k == "v_s") dst = &m.v_s; else if (k == "f_bristle") dst = &m.f_bristle; else if (k == "zeta_bristle") dst = &m.zeta_bristle;
            else if (k == "viscous") dst = &m.viscous; else if (k == "rough") dst = &m.rough; else if (k == "rough_hz") dst = &m.rough_hz;
            else if (k == "body_mass") dst = &m.body_mass; else if (k == "spread") dst = &m.spread; else if (k == "second_level") dst = &m.second_level;
            else if (k == "tab_sound") dst = &m.tab_sound; else if (k == "rub_level") dst = &m.rub_level; else if (k == "rub_hz") dst = &m.rub_hz;
            else if (k == "strip_sigma") dst = &m.strip_sigma; else if (k == "strip_mass") dst = &m.strip_mass; else if (k == "strip_level") dst = &m.strip_level;
            else if (k == "snap_level") dst = &m.snap_level; else if (k == "snap_ms") dst = &m.snap_ms; else if (k == "slack_ms") dst = &m.slack_ms;
            else if (k == "free_ring") dst = &m.free_ring; else if (k == "catch_ms") dst = &m.catch_ms;
            else if (k == "contacts") { m.contacts = (int) v; continue; }
            if (! dst) return "line " + std::to_string (lineNo) + ": unknown contact key '" + k + "'";
            *dst = v;
        }
        else return "line " + std::to_string (lineNo) + ": unknown key '" + key + "'";
    }
    if (m.bf.empty() || m.V.empty()) return "no body or no drive";
    return {};
}

// The learner's variations (tools/render_rubber2.py) and NeoBeat's own controls for the sequencer.
struct StretchPlay
{
    float speed = 1.0f;        // the same pull done faster (> 1) or slower; more speed = more slips per second
    float from = 0.0f;         // start part-way through the pull (0..0.95 of the pull already done)
    bool tight = false;        // tension holds: only the gentle fall
    bool slack = false;        // deeper bends
    bool climbs = false;       // impossible: pulling makes it tighter, the pitch climbs
    bool oneContact = false;
    bool lightBody = false;    // the body pushes back: it squeals
    bool metalBody = false;    // impossible: rubber on a body that rings like metal
    bool snap = true;          // false: the tab never lets go
    bool handGain = true;      // follow the hand's loudness contour
    float width = 1.0f;        // two listening points (0 = mono)
    float fd = 12000.0f, q = 1.2f;
    float pitch = 1.0f;        // body and strip frequencies scaled together (the pad's Tune)
    float level = 1.0f;
    uint32_t seed = 3;
};

// Seconds from the note-on to the snap for this play (negative: it never lets go).
inline float stretchSnapSeconds (const FrictionMaterial& m, const StretchPlay& play, float fs = 48000.0f)
{
    if (m.release_s < 0.0f || ! play.snap || m.V.empty()) return -1.0f;
    const double hopS = (double) m.hop / fs;
    auto Vat = [&] (double t) { const double x = t / hopS; const int i = (int) x; if (i >= (int) m.V.size() - 1) return (double) m.V.back();
                                const double f = x - i; return m.V[(size_t) i] + (m.V[(size_t) i + 1] - m.V[(size_t) i]) * f; };
    const double total = (double) m.V.size() * hopS;
    double t = 0, d = 0;
    const double step = 1.0 / fs;
    const double target0 = std::min (0.95, (double) play.from) * m.pull_length;
    while (d < target0 && t < total) { d += std::fabs (Vat (t)) * step; t += step; }
    const double t0 = t;
    while (d < m.pull_length && t < total + 2.0) { d += std::fabs (Vat (t)) * step; t += step; }
    return (float) ((t - t0) / std::max (play.speed, 0.05f));
}

// What a rubber needs at a sample rate that does not depend on how it is played: the noise filters
// and their gains, and checkpoints of the pull's distance (to start part-way through without walking
// the whole drive). Built once off the audio thread; StretchVoice::start then only copies.
struct StretchPrep
{
    float fs = 0.0f;
    std::vector<Biquad2> rough, fine;
    float roughNorm = 1.0f, fineNorm = 1.0f;
    std::vector<double> cpT, cpDist;   // every kCp samples along the drive: tDrive and the distance before it
    static constexpr int kCp = 512;
};

class StretchVoice
{
public:
    static constexpr int kOS = 4;
    static constexpr int kMaxK = 128;
    static constexpr int kMaxC = 6;
    static constexpr int kMaxM = 12;
    static constexpr int kTaps = 81;

    void prepare (float sampleRate)
    {
        fs = sampleRate; fsOs = fs * (float) kOS; dt = 1.0f / fsOs;
        // resample_poly(x, 1, 4): firwin(81, 1/4, window=("kaiser", 5.0))
        const double beta = 5.0;
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        double sum = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const double m = n - (kTaps - 1) / 2.0;
            const double sinc = m == 0 ? 1.0 : std::sin (3.14159265358979 * m / 4.0) / (3.14159265358979 * m / 4.0);
            const double r = 2.0 * n / (kTaps - 1) - 1.0;
            const double w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0 (beta);
            fir[(size_t) n] = (float) (0.25 * sinc * w);
            sum += fir[(size_t) n];
        }
        for (auto& h : fir) h = (float) (h / sum);
        for (auto& f : roughF) { f.s.reserve (8); f.z1.reserve (8); f.z2.reserve (8); }
        fineF.s.reserve (8); fineF.z1.reserve (8); fineF.z2.reserve (8);
        active = false;
    }

    static StretchPrep makePrep (const FrictionMaterial& mat, float sampleRate)
    {
        StretchPrep r;
        r.fs = sampleRate;
        r.rough = butterLowHigh (2, mat.rough_hz, sampleRate, false);
        r.roughNorm = 1.0f / (float) std::sqrt (noiseGain (SosCascade (r.rough)));
        r.fine = butterLowHigh (2, std::min (mat.rub_hz, 15000.0f), sampleRate, true);
        auto lp = butterLowHigh (4, std::min (16000.0, 0.45 * sampleRate), sampleRate, false);
        r.fine.insert (r.fine.end(), lp.begin(), lp.end());
        r.fineNorm = 1.0f / (float) std::sqrt (noiseGain (SosCascade (r.fine)));
        // the same walk as start() does, remembered every kCp samples
        if (! mat.V.empty())
        {
            const double step = 1.0 / sampleRate;
            const double secs = (double) ((float) mat.V.size() * (float) mat.hop / sampleRate);
            double t = 0.0, d = 0.0;
            for (int64_t n = 0; t < secs; ++n)
            {
                if (n % StretchPrep::kCp == 0) { r.cpT.push_back (t); r.cpDist.push_back (d); }
                const double x = t * sampleRate / (double) mat.hop;
                const int i = (int) x;
                const float v = i >= (int) mat.V.size() - 1 ? mat.V.back()
                                                           : mat.V[(size_t) i] + (mat.V[(size_t) i + 1] - mat.V[(size_t) i]) * (float) (x - i);
                d += std::fabs (v) * step;
                t += step;
            }
        }
        return r;
    }

    bool isActive() const noexcept { return active; }
    void stop() noexcept { active = false; }

    // seconds from the note-on to the release (the snap), or a negative value if it never lets go
    float snapTime() const noexcept { return releaseOs >= 0 ? (float) releaseOs / fsOs : -1.0f; }

    void start (const FrictionMaterial& mat, const StretchPlay& play, const StretchPrep* prep = nullptr)
    {
        M = &mat; P = play;
        const float spd = std::max (play.speed, 0.05f);
        C = play.oneContact ? 1 : std::min (std::max (mat.contacts, 1), kMaxC);
        K = std::min ((int) mat.bf.size(), kMaxK);
        Ms = std::min ((int) mat.sf0.size(), kMaxM);
        // ---- constants of _simulate ----
        for (int c = 0; c < C; ++c)
        {
            const float wt = 6.28318530718f * mat.f_tab * (1.0f + (C > 1 ? 0.25f * (float) c / (float) (C - 1) : 0.0f));
            mt[c] = 1.0f / (wt * wt);
            ct[c] = 2.0f * mat.zeta_tab * std::sqrt (mt[c]);
            const float wb = 6.28318530718f * std::min (mat.f_bristle, 0.19f / dt);
            s0[c] = mt[c] * wb * wb;
            s1[c] = 2.0f * mat.zeta_bristle * mt[c] * wb;
        }
        const float wt0 = 6.28318530718f * mat.f_tab;
        const float vSlip = wt0 * (1.0f - mat.mu_ratio);
        s2 = mat.viscous / vSlip;
        vs = mat.v_s * vSlip;
        Fc = mat.mu_ratio;
        const float bodyMass = play.lightBody ? mat.body_mass * 0.08f : mat.body_mass;
        mk = bodyMass * mt[0];
        for (int k = 0; k < K; ++k)
        {
            const float w = 6.28318530718f * mat.bf[(size_t) k] * play.pitch;
            const float wp = (2.0f / dt) * std::sin (std::min (w * dt / 2.0f, 1.2f));
            wk2[k] = wp * wp;
            sg[k] = mat.bsigma[(size_t) k] * (play.metalBody ? 0.06f : 1.0f);
            for (int c = 0; c < C; ++c) phi[c][k] = mat.phi[(size_t) c][(size_t) k];
            float meanAbs = 0;
            for (const auto& row : mat.phi) meanAbs += std::fabs (row[(size_t) k]);
            meanAbs /= (float) mat.phi.size();
            const float sgn = std::sin ((float) k * 2.39996f);
            phiRub[k] = meanAbs * (sgn > 0 ? 1.0f : (sgn < 0 ? -1.0f : 0.0f));
            q[k] = p[k] = 0.0f;
        }
        ms = mat.strip_mass * mt[0];
        // strip laws (the variations change them)
        for (int i = 0; i < Ms; ++i)
        {
            float f0 = mat.sf0[(size_t) i], grow = mat.sgrow[(size_t) i], sag = mat.ssag[(size_t) i];
            if (play.tight) sag = 0.0f;
            if (play.slack) { sag = std::min (sag * 1.5f + 0.2f, 0.95f); grow *= 1.5f; }
            if (play.climbs)
            {
                const float R = std::sqrt (std::max (1.0f - sag, 0.01f)) / (1.0f + grow);
                f0 *= R; grow = R - 1.0f; sag = 0.0f;
            }
            sF0[i] = f0 * play.pitch; sGrow[i] = grow; sSag[i] = sag;
            sOn[i] = mat.son[(size_t) i]; sOff[i] = mat.soff[(size_t) i];
            qs[i] = ps[i] = 0.0f;
        }
        // contacts: rates and pressures (rng as in simulate)
        std::mt19937 rng (play.seed * 7919u + 3u);
        std::uniform_real_distribution<float> uni (0.0f, 1.0f);
        for (int c = 0; c < C; ++c)
        {
            scale[c] = c == 0 ? 1.0f : 1.0f + mat.spread * (uni (rng) - 0.5f) * 2.0f;
            lev[c] = c == 0 ? 1.0f : mat.second_level;
            phiS[c] = c == 0 ? 1.0f : 0.3f;
            sst[c] = 0.0f; vt[c] = 0.0f; z[c] = 0.0f;
        }
        // noise filters at the oversampled rate
        if (prep != nullptr && prep->fs == fs)
        {   // prepared off the audio thread: copy into the reserved filters, no allocation, no measuring
            auto load = [] (SosCascade& f, const std::vector<Biquad2>& secs)
            {
                f.s.assign (secs.begin(), secs.end());
                f.z1.assign (secs.size(), 0.0); f.z2.assign (secs.size(), 0.0);
            };
            for (int c = 0; c < C; ++c) { load (roughF[c], prep->rough); roughNorm[c] = prep->roughNorm; }
            load (fineF, prep->fine); fineNorm = prep->fineNorm;
        }
        else
        {
            for (int c = 0; c < C; ++c)
            {
                roughF[c] = SosCascade (butterLowHigh (2, mat.rough_hz, fs, false));
                roughNorm[c] = 1.0f / (float) std::sqrt (noiseGain (roughF[c]));
            }
            std::vector<Biquad2> s = butterLowHigh (2, std::min (mat.rub_hz, 15000.0f), fs, true);
            auto lp = butterLowHigh (4, std::min (16000.0, 0.45 * fs), fs, false);
            s.insert (s.end(), lp.begin(), lp.end());
            fineF = SosCascade (s);
            fineNorm = 1.0f / (float) std::sqrt (noiseGain (fineF));
        }
        nrng.seed (play.seed * 2654435761u + 99u);
        // listening points
        std::mt19937 wrng (21u + play.seed);
        std::normal_distribution<float> nd;
        for (int k = 0; k < K; ++k) { const float s = pickupSide (mat.bf[(size_t) k], nd (wrng)); wl[k] = mat.ba[(size_t) k] * (1 + s); wr[k] = mat.ba[(size_t) k] * (1 - s); }
        std::mt19937 srng (33u + play.seed);
        for (int i = 0; i < Ms; ++i) { const float s = pickupSide (sF0[i], nd (srng)); swl[i] = mat.strip_level * mat.sa[(size_t) i] * (1 + s); swr[i] = mat.strip_level * mat.sa[(size_t) i] * (1 - s); }
        // the pull, possibly faster and from part-way through
        speed = spd;
        tDrive = 0.0;
        dist = 0.0;
        if (play.from > 0.0f)
        {   // skip ahead along the drive until that much of the pull is done
            const double target = std::min (0.95, (double) play.from) * mat.pull_length;
            const double step = 1.0 / fs;
            if (prep != nullptr && prep->fs == fs && ! prep->cpDist.empty())
            {   // jump to the last checkpoint still short of the target, then walk the rest as before
                const auto it = std::lower_bound (prep->cpDist.begin(), prep->cpDist.end(), target);
                const size_t k = (size_t) std::max<ptrdiff_t> (0, (it - prep->cpDist.begin()) - 1);
                tDrive = prep->cpT[k]; dist = prep->cpDist[k];
            }
            while (dist < target && tDrive < driveSeconds())
            {
                dist += std::fabs (driveAt (mat.V, tDrive)) * step;   // distance in V units (V already scaled by speed below)
                tDrive += step;
            }
        }
        // the initial spring stretch (init state: N[0] * 0.5)
        const float n0 = driveAt (mat.env, tDrive);
        for (int c = 0; c < C; ++c) sst[c] = n0 * 0.5f * lev[c];
        released = false; releaseOs = -1; osIndex = 0; caughtAt = -1;
        snapTotal = std::max (4, (int) (mat.snap_ms / 1000.0f * fsOs));
        firPos = 0; for (auto& h : histL) h = 0; for (auto& h : histR) h = 0;
        sub = 0; outCount = 0;
        tabAcc = 0;
        maxOut = (int64_t) (((driveSeconds() - (float) tDrive) / spd + 0.8) * fs);
        quietRun = 0;
        active = true;
    }

    void process (float& outL, float& outR) noexcept
    {
        outL = outR = 0.0f;
        if (! active) return;
        // the noises that drive the contact change slowly (rough: < 100 Hz) or are band-limited
        // below 16 kHz (rubbing): draw them once per output sample
        for (int c = 0; c < C; ++c) roughNow[c] = (float) roughF[c].process (gauss()) * roughNorm[c];
        fineNow = (float) fineF.process (gauss()) * fineNorm;
        // four oversampled steps, filtered down
        for (int j = 0; j < kOS; ++j)
        {
            float yl, yr;
            step (yl, yr);
            histL[(size_t) firPos] = yl; histR[(size_t) firPos] = yr;
            firPos = (firPos + 1) % kTaps;
        }
        float l = 0, r = 0;
        for (int n = 0; n < kTaps; ++n)
        {
            const int i = (firPos + n) % kTaps;   // oldest first
            l += fir[(size_t) n] * histL[(size_t) i];
            r += fir[(size_t) n] * histR[(size_t) i];
        }
        // the hand's loudness contour
        const float g = P.handGain ? driveAt (M->gain, tNow()) : 1.0f;
        outL = l * g * P.level * M->out_gain;
        outR = r * g * P.level * M->out_gain;
        if (++outCount > maxOut) active = false;
        // after the snap (and the catch) the object dies away: stop once it has been silent for 50 ms
        if (released && osIndex > caughtAt)
        {
            const float a = std::fabs (outL) + std::fabs (outR);
            quietRun = a < 1.0e-5f ? quietRun + 1 : 0;
            if (quietRun > (int) (0.05f * fs)) active = false;
        }
    }

private:
    float driveSeconds() const noexcept { return (float) M->V.size() * (float) M->hop / fs; }
    // the drive in the learner's time, at the voice's own time (faster/slower pulls)
    double tNow() const noexcept { return tDrive; }
    float driveAt (const std::vector<float>& v, double t) const noexcept
    {
        const double x = t * fs / (double) M->hop;
        const int i = (int) x;
        if (i >= (int) v.size() - 1) return v.back();
        const float fr = (float) (x - i);
        return v[(size_t) i] + (v[(size_t) i + 1] - v[(size_t) i]) * fr;
    }

    float pickupSide (float f, float normal) const noexcept
    {
        const float r = std::min (0.999f, std::max (-0.999f, 1.0f / (1.0f + std::pow (f / P.fd, P.q))));
        return normal * std::sqrt ((1.0f - r) / (1.0f + r)) * P.width;
    }

    static double noiseGain (SosCascade f)
    {
        double e = 0;
        for (int i = 0; i < 1 << 16; ++i) { const double y = f.process (i == 0 ? 1.0 : 0.0); e += y * y; }
        return std::max (e, 1e-30);
    }

    float gauss() noexcept
    {
        // Box-Muller from a fast generator
        if (haveSpare) { haveSpare = false; return spare; }
        float u, v, s;
        do { u = uni01() * 2 - 1; v = uni01() * 2 - 1; s = u * u + v * v; } while (s >= 1.0f || s == 0.0f);
        const float m = std::sqrt (-2.0f * std::log (s) / s);
        spare = v * m; haveSpare = true;
        return u * m;
    }
    float uni01() noexcept { return (float) (nrng() >> 8) * (1.0f / 16777216.0f); }

    void step (float& outL, float& outR) noexcept
    {
        const auto& m = *M;
        // drive at this oversampled instant (the learner's time runs `speed` times faster)
        const float Vd = driveAt (m.V, tDrive) * speed;
        const float Nd = driveAt (m.env, tDrive);
        tDrive += (double) speed / (double) fsOs;
        // how much of the pull is done, and the release
        if (! released) dist += std::fabs (Vd) / fsOs;
        const float d = (float) std::min (1.0, dist / m.pull_length);
        if (! released && P.snap && m.release_s >= 0.0f && dist >= m.pull_length) { released = true; releaseOs = osIndex; caughtAt = osIndex + (int64_t) (m.catch_ms / 1000.0f * fsOs); }
        float off = 1.0f, snapIn = 0.0f, damp = 1.0f, slackF = 1.0f, stripFade = 1.0f;
        if (released)
        {
            const int64_t since = osIndex - releaseOs;
            off = std::max (0.0f, 1.0f - (float) since / (0.002f * fsOs));
            if (since < snapTotal) { const float s = std::sin (3.14159265f * (float) since / (float) snapTotal); snapIn = m.snap_level * s * s; }
            if (osIndex < caughtAt) damp = m.free_ring;
            const float tt = (float) since / fsOs;
            slackF = 0.35f + 0.65f * std::exp (-tt / (m.slack_ms / 1000.0f));
            stripFade = std::exp (-tt / 0.006f);
        }
        // the rubbing: many tiny contacts, growing with pressure and pull speed
        const float spdRel = std::fabs (Vd) / (m.v_median * speed + 1e-12f);
        const float rub = m.rub_level * Nd * std::sqrt (spdRel) * fineNow * off;
        // strip: pitch and activity follow the pull
        for (int i = 0; i < Ms; ++i)
        {
            float a;
            if (! released || releaseActivity[i] < 0)
            {
                const float up = std::min (1.0f, std::max (0.0f, (d - (sOn[i] - 0.05f)) / 0.05f));
                const float dn = std::min (1.0f, std::max (0.0f, ((sOff[i] + 0.05f) - d) / 0.05f));
                a = (up * dn) * (up * dn);
                if (released) releaseActivity[i] = a;
            }
            if (released) a = releaseActivity[i] * stripFade;
            else releaseActivity[i] = -1.0f;
            const float f = sF0[i] * std::sqrt (std::max (1.0f - sSag[i] * d, 0.01f)) / (1.0f + sGrow[i] * d) * slackF;
            const float w = (2.0f / dt) * std::sin (std::min (6.28318530718f * f * dt / 2.0f, 1.2f));
            act[i] = a; w2[i] = w * w;
        }
        // ---- one step of the coupled system ----
        float Fsum[kMaxC] = {};
        const bool contactOn = ! (released && off <= 0.0f);   // after the release nothing presses on the object
        for (int c = 0; c < (contactOn ? C : 0); ++c)
        {
            const float vIn = Vd * scale[c] * lev[c] * off;
            const float nIn = Nd * lev[c] * off;
            const float rIn = roughNow[c];
            const float nEff = nIn * (1.0f + m.rough * rIn);
            const float* ph = phi[c].data();
            float u4[4] = { 0, 0, 0, 0 };
            int k = 0;
            for (; k + 4 <= K; k += 4) { u4[0] += ph[k] * p[k]; u4[1] += ph[k + 1] * p[k + 1]; u4[2] += ph[k + 2] * p[k + 2]; u4[3] += ph[k + 3] * p[k + 3]; }
            float u = (u4[0] + u4[1]) + (u4[2] + u4[3]);
            for (; k < K; ++k) u += ph[k] * p[k];
            float us = 0.0f;
            for (int i = 0; i < Ms; ++i) us += act[i] * ps[i];
            u += phiS[c] * us;
            const float vr = vt[c] - u;
            const float x = vr / vs;
            const float g = nEff * (Fc + (1.0f - Fc) * std::exp (-x * x)) + 1e-9f;
            const float a = s0[c] * std::fabs (vr) / g;
            const float zss = g * (vr > 0 ? 1.0f : (vr < 0 ? -1.0f : 0.0f)) / s0[c];
            const float zNew = zss + (z[c] - zss) * std::exp (-a * dt);
            const float zd = (zNew - z[c]) / dt;
            const float F = s0[c] * zNew + s1[c] * zd + s2 * vr * nEff;
            z[c] = zNew;
            const float acc = (sst[c] - ct[c] * vt[c] - F) / mt[c];
            vt[c] += dt * acc;
            sst[c] += dt * (vIn - vt[c]);
            Fsum[c] = F;
        }
        float l = 0.0f, r = 0.0f;
        const float inv = 1.0f / mk;
        const float a0 = Fsum[0] * inv + snapIn * inv, a1 = C > 1 ? Fsum[1] * inv : 0.0f, a2 = C > 2 ? Fsum[2] * inv : 0.0f;
        const float rubI = rub * inv, dmp = 2.0f * damp;
        const float* p0 = phi[0].data(); const float* p1 = phi[1].data(); const float* p2 = phi[2].data();
        float l4[4] = { 0, 0, 0, 0 }, r4[4] = { 0, 0, 0, 0 };
        for (int k = 0; k < K; ++k)
        {
            float f = phiRub[k] * rubI + p0[k] * a0 + p1[k] * a1 + p2[k] * a2;
            for (int c = 3; c < C; ++c) f += phi[c][k] * Fsum[c] * inv;
            const float pk = p[k] + dt * (f - dmp * sg[k] * p[k] - wk2[k] * q[k]);
            p[k] = pk;
            q[k] += dt * pk;
            l4[k & 3] += pk * wl[k]; r4[k & 3] += pk * wr[k];
        }
        l += (l4[0] + l4[1]) + (l4[2] + l4[3]);
        r += (r4[0] + r4[1]) + (r4[2] + r4[3]);
        float fsSum = snapIn;
        for (int c = 0; c < C; ++c) fsSum += phiS[c] * Fsum[c];
        const bool stripOn = ! (released && stripFade < 1.0e-4f);   // the strip has gone slack and silent
        for (int i = 0; i < (stripOn ? Ms : 0); ++i)
        {
            const float fsm = act[i] * fsSum / ms;
            ps[i] += dt * (fsm - 2.0f * m.strip_sigma * ps[i] - w2[i] * qs[i]);
            qs[i] += dt * ps[i];
            const float rad = act[i] * ps[i];
            l += rad * swl[i]; r += rad * swr[i];
        }
        // the tab itself radiates a little (scaled as the learner's render measured it)
        float tab = 0.0f;
        for (int c = 0; c < (contactOn ? C : 0); ++c) tab += vt[c] * lev[c] / (float) C;
        const float t = m.tab_sound * m.tab_norm * tab;
        outL = l + t; outR = r + t;
        ++osIndex;
    }

    const FrictionMaterial* M = nullptr;
    StretchPlay P;
    int C = 3, K = 0, Ms = 0;
    float fs = 48000, fsOs = 192000, dt = 1.0f / 192000, s2 = 0, vs = 1, Fc = 0.5f, mk = 1, ms = 1, speed = 1;
    std::array<float, kMaxC> mt {}, ct {}, s0 {}, s1 {}, scale {}, lev {}, phiS {}, sst {}, vt {}, z {}, roughNorm {};
    std::array<std::array<float, kMaxK>, kMaxC> phi {};
    std::array<float, kMaxK> wk2 {}, sg {}, phiRub {}, q {}, p {}, wl {}, wr {};
    std::array<float, kMaxM> sF0 {}, sGrow {}, sSag {}, sOn {}, sOff {}, qs {}, ps {}, act {}, w2 {}, swl {}, swr {}, releaseActivity {};
    std::array<SosCascade, kMaxC> roughF { SosCascade ({}), SosCascade ({}), SosCascade ({}), SosCascade ({}), SosCascade ({}), SosCascade ({}) };
    SosCascade fineF { {} };
    float fineNorm = 1, fineNow = 0;
    std::array<float, kMaxC> roughNow {};
    std::mt19937 nrng { 1u };
    float spare = 0; bool haveSpare = false;
    std::array<float, kTaps> fir {}, histL {}, histR {};
    int firPos = 0, sub = 0, snapTotal = 4;
    double tDrive = 0, dist = 0;
    int64_t osIndex = 0, releaseOs = -1, caughtAt = -1, outCount = 0, maxOut = 0;
    int quietRun = 0;
    float tabAcc = 0;
    bool released = false, active = false;
};
} // namespace nb
