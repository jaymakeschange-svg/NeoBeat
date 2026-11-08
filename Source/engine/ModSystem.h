// NeoBeat 2.0 — modulation: the dock's sources, routes resolved for the audio thread, a voice's
// continuous modulation, and the per-pad real-time algorithms.
//
// Scope decides where a source's clock starts:
//   KIT   one clock for everything, locked to the song position
//   PAD   restarts on every hit (so it moves level, pan, tone, drive and pitch through each hit)
//   CELL  restarts on every One Shot cell (the same, for the rendered cells)
// Destinations that are rendered ahead (cell and window parameters) take the source's value at the
// hit's place in the song.
#pragma once

#include "Project.h"
#include <cmath>
#include <cstdlib>

namespace nb
{
enum class Dest : uint8_t
{
    PadTune, PadDecay, PadTone, PadDrive, PadHollow, PadEvolve, PadPan, PadLevel,
    CellMain, FsParam, BdParam, SeqLevel, SeqPan, SeqPitch, GridLevel, GridPan, GridPitch, Master, Count
};

inline const std::vector<std::string>& padMacroNames()
{
    static const std::vector<std::string> n { "tune", "decay", "tone", "drive", "hollow", "evolve", "pan", "level" };
    return n;
}
inline const std::vector<std::string>& fsModParams()
{
    static const std::vector<std::string> n { "stretch", "creep", "peel", "freeze", "screech", "adhesive", "rasp", "roll", "strip", "width", "decay", "wet" };
    return n;
}
inline const std::vector<std::string>& bdModParams()
{
    static const std::vector<std::string> n { "bounciness", "distance start", "distance end", "speed", "pan random", "width", "room", "wet" };
    return n;
}

struct ResolvedRoute
{
    ModSrc src = ModSrc::Lfo1;
    Dest dest = Dest::Master;
    int target = -1;     // pad / cell / slot; -1 = all pads
    int param = 0;
    float depth = 0.0f;
};

inline bool parseDest (const std::string& s, Dest& dest, int& target, int& param)
{
    auto split = [] (const std::string& x)
    {
        std::vector<std::string> v; size_t a = 0;
        while (true) { const size_t b = x.find ('/', a); v.push_back (x.substr (a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
        return v;
    };
    const auto p = split (s);
    target = -1; param = 0;
    if (p.empty()) return false;
    auto num = [] (const std::string& x, int& out) { if (x == "*") { out = -1; return true; } char* e = nullptr; const long v = std::strtol (x.c_str(), &e, 10); if (e == x.c_str()) return false; out = (int) v; return true; };
    if (p[0] == "master") { dest = Dest::Master; return true; }
    if (p.size() < 2) return false;
    if (p[0] == "seq" || p[0] == "grid")
    {
        const bool seq = p[0] == "seq";
        if (p[1] == "level") dest = seq ? Dest::SeqLevel : Dest::GridLevel;
        else if (p[1] == "pan") dest = seq ? Dest::SeqPan : Dest::GridPan;
        else if (p[1] == "pitch") dest = seq ? Dest::SeqPitch : Dest::GridPitch;
        else return false;
        return true;
    }
    if (p.size() < 3) return false;
    if (! num (p[1], target)) return false;
    if (p[0] == "pad")
    {
        const auto& m = padMacroNames();
        for (size_t i = 0; i < m.size(); ++i) if (m[i] == p[2]) { dest = (Dest) i; return true; }
        return false;
    }
    if (p[0] == "cell") { if (p[2] != "main") return false; dest = Dest::CellMain; return target >= 0; }
    if (p[0] == "fs" || p[0] == "bd")
    {
        const auto& m = p[0] == "fs" ? fsModParams() : bdModParams();
        for (size_t i = 0; i < m.size(); ++i) if (m[i] == p[2]) { dest = p[0] == "fs" ? Dest::FsParam : Dest::BdParam; param = (int) i; return target >= 0 && target < kNumSlots; }
        return false;
    }
    return false;
}

inline bool isBipolar (ModSrc s) { return s == ModSrc::Lfo1 || s == ModSrc::Lfo2 || s == ModSrc::Random || s == ModSrc::Key; }

inline float lfoShape (int shape, double phase, const std::array<float, 64>& table) noexcept
{
    double ph = phase - std::floor (phase);
    switch (shape)
    {
        case 0: return (float) std::sin (6.283185307179586 * ph);
        case 1: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
        case 2: return (float) (2.0 * ph - 1.0);
        case 3: return ph < 0.5 ? 1.0f : -1.0f;
        default:
        {
            const double x = ph * 64.0; const int i = (int) x; const float f = (float) (x - i);
            return table[(size_t) (i & 63)] * (1.0f - f) + table[(size_t) ((i + 1) & 63)] * f;
        }
    }
}

inline float hashUnit (uint32_t x) noexcept   // -1..1
{
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return (float) ((double) x / 4294967295.0 * 2.0 - 1.0);
}

// Where a source is read: the song (kit scope) and the hit's own facts.
struct HitContext
{
    double beats = 0.0;      // song position (quarter notes)
    double bpm = 120.0;
    float velocity = 0.8f;
    int key = 60;
    float height = 0.5f;
    float step = 0.0f;       // 0..1 through the main pattern
    uint32_t seed = 1;       // per hit (pad scope random) or per cell
};

// The value of a source at the start of a hit.
inline float sourceAtHit (ModSrc s, const ModParams& m, const HitContext& h) noexcept
{
    auto lfo = [&] (const LfoSpec& sp, int shapeOverride)
    {
        if (sp.scope != Scope::Kit) return lfoShape (shapeOverride, 0.0, m.table);
        const double cyc = sp.sync ? h.beats / std::max (0.01f, sp.beats) : h.beats * 60.0 / std::max (1.0, h.bpm) * sp.hz;
        return lfoShape (shapeOverride, cyc, m.table);
    };
    switch (s)
    {
        case ModSrc::Lfo1: return lfo (m.lfo1, m.lfo1.shape);
        case ModSrc::Lfo2: return lfo (m.lfo2, m.lfo2.shape);
        case ModSrc::Env1: return 1.0f;
        case ModSrc::Random:
            if (m.randScope == Scope::Kit) return hashUnit ((uint32_t) (int64_t) std::floor (h.beats / std::max (0.01f, m.randBeats)) * 2654435761u + 17u);
            return hashUnit (h.seed * 2246822519u + 3u);
        case ModSrc::Velocity: return h.velocity;
        case ModSrc::Key: return std::max (-1.0f, std::min (1.0f, (float) (h.key - 60) / 24.0f));
        case ModSrc::Height: return h.height;
        case ModSrc::Step: return h.step;
        case ModSrc::XyX: return m.xyX;
        case ModSrc::XyY: return m.xyY;
        default: return 0.0f;
    }
}

// The kit-scope values at one moment (the audio thread refreshes this every block).
struct ModFrame
{
    std::array<float, (size_t) ModSrc::Count> v {};
    double beats = 0.0, bpm = 120.0;
};

inline void computeFrame (ModFrame& f, const ModParams& m, double beats, double bpm) noexcept
{
    HitContext h; h.beats = beats; h.bpm = bpm;
    f.beats = beats; f.bpm = bpm;
    for (int i = 0; i < (int) ModSrc::Count; ++i) f.v[(size_t) i] = sourceAtHit ((ModSrc) i, m, h);
}

// ------------------------------------------------------------------ a voice's continuous modulation
enum class VDest : uint8_t { Level, Pan, Tone, Drive, Pitch };

struct VoiceMod
{
    struct Lane { ModSrc src; VDest dest; float depth; };
    std::array<Lane, 8> lanes {};
    int numLanes = 0;
    // voice-local clocks (pad / cell scope)
    double t = 0.0;                 // seconds since the hit
    float velocity = 0.8f, key = 0.0f, height = 0.5f, step = 0.0f, rnd = 0.0f;
    const ModParams* state = nullptr;
    const ModFrame* frame = nullptr;
    // outputs
    float levelMul = 1.0f, pan = 0.0f, tone = 0.0f, drive = 0.0f, pitch = 0.0f;
    bool active() const noexcept { return numLanes > 0 && state != nullptr; }

    float value (ModSrc s) const noexcept
    {
        const ModParams& m = *state;
        auto lfo = [&] (const LfoSpec& sp, int idx)
        {
            if (sp.scope == Scope::Kit) return frame != nullptr ? frame->v[(size_t) idx] : 0.0f;
            const double bpm = frame != nullptr ? frame->bpm : 120.0;
            const double cyc = sp.sync ? t * bpm / 60.0 / std::max (0.01f, sp.beats) : t * sp.hz;
            return lfoShape (sp.shape, cyc, m.table);
        };
        switch (s)
        {
            case ModSrc::Lfo1: return lfo (m.lfo1, 0);
            case ModSrc::Lfo2: return lfo (m.lfo2, 1);
            case ModSrc::Env1:
            {
                const double a = std::max (0.0005f, m.envA), d = std::max (0.005f, m.envD);
                return t < a ? (float) (t / a) : (float) std::exp (-(t - a) / (d / 4.6));
            }
            case ModSrc::Random: return m.randScope == Scope::Kit && frame != nullptr ? frame->v[(size_t) ModSrc::Random] : rnd;
            case ModSrc::Velocity: return velocity;
            case ModSrc::Key: return key;
            case ModSrc::Height: return height;
            case ModSrc::Step: return step;
            case ModSrc::XyX: return m.xyX;
            case ModSrc::XyY: return m.xyY;
            default: return 0.0f;
        }
    }

    // advance by n samples and refresh the outputs
    void update (int n, float sr) noexcept
    {
        if (! active()) return;
        float lv = 0.0f; pan = tone = drive = pitch = 0.0f;
        for (int i = 0; i < numLanes; ++i)
        {
            const auto& L = lanes[(size_t) i];
            const float x = value (L.src) * L.depth;
            switch (L.dest)
            {
                case VDest::Level: lv += x; break;
                case VDest::Pan: pan += x; break;
                case VDest::Tone: tone += x; break;
                case VDest::Drive: drive += x; break;
                case VDest::Pitch: pitch += x * 24.0f; break;
            }
        }
        levelMul = std::pow (10.0f, 24.0f * lv / 20.0f);
        t += n / (double) sr;
    }
};

// ------------------------------------------------------------------ per-pad algorithms (real time)
class AlgoState
{
public:
    void start (const PadAlgo& a, float sr_, float pitchHz, float amountOffset) noexcept
    {
        type = a.type; mix = std::max (0.0f, std::min (1.0f, a.mix)); sr = sr_;
        amount = std::max (0.0f, std::min (1.0f, a.amount + amountOffset));
        ph = 0.0; held[0] = held[1] = 0.0f; holdCount = 0; lp[0] = lp[1] = bp[0] = bp[1] = 0.0f;
        if (type == 1) { ratio = 0.5f + 7.5f * amount * amount; inc = pitchHz * ratio / sr; if (inc > 0.45) inc = 0.45; }
        if (type == 2)
        {
            const float f = 40.0f * std::pow (2.0f, 7.0f * amount);
            combLen = std::max (2, std::min ((int) comb[0].size() - 1, (int) (sr / f)));
            for (auto& c : comb) std::fill (c.begin(), c.end(), 0.0f);
            combPos = 0;
        }
        if (type == 3) { bits = 2.0f + 12.0f * (1.0f - amount); hold = 1 + (int) (amount * amount * 24.0f); }
        if (type == 4) { const float fc = 60.0f * std::pow (2.0f, 8.0f * amount); g = std::tan (3.14159265f * std::min (fc, 0.45f * sr) / sr); }
        if (type == 5) fold = 1.0f + 7.0f * amount;
    }
    bool on() const noexcept { return type > 0 && mix > 0.0f; }
    void process (float& l, float& r) noexcept
    {
        float in[2] = { l, r }, out[2] = { l, r };
        switch (type)
        {
            case 1:
            {
                const float c = (float) std::sin (6.283185307179586 * ph);
                ph += inc; if (ph >= 1.0) ph -= 1.0;
                out[0] = in[0] * c; out[1] = in[1] * c; break;
            }
            case 2:
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto& buf = comb[(size_t) ch];
                    const int rp = (combPos - combLen + (int) buf.size()) % (int) buf.size();
                    const float y = in[ch] + 0.86f * buf[(size_t) rp];
                    buf[(size_t) combPos] = y; out[ch] = y * 0.45f;
                }
                combPos = (combPos + 1) % (int) comb[0].size();
                break;
            case 3:
            {
                if (holdCount-- <= 0)
                {
                    holdCount = hold - 1;
                    const float q = std::pow (2.0f, bits);
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        float x = in[ch] * 1.4f;
                        x = x - 2.0f * std::floor ((x + 1.0f) * 0.5f);   // the Wave's mix-bug wrap
                        held[ch] = std::round (x * q) / q;
                    }
                }
                out[0] = held[0]; out[1] = held[1]; break;
            }
            case 4:
                for (int ch = 0; ch < 2; ++ch)
                {   // a resonant state-variable band/low-pass
                    const float k = 0.25f;
                    const float hp = (in[ch] - (2.0f * k + g) * bp[ch] - lp[ch]) / (1.0f + 2.0f * k * g + g * g);
                    const float b = g * hp + bp[ch]; bp[ch] = g * hp + b;
                    const float lo = g * b + lp[ch]; lp[ch] = g * b + lo;
                    out[ch] = lo + 0.6f * b;
                }
                break;
            case 5:
                for (int ch = 0; ch < 2; ++ch) out[ch] = std::sin (fold * in[ch] * 1.5707963f) / std::max (1.0f, fold * 0.5f);
                break;
            default: break;
        }
        l = in[0] + mix * (out[0] - in[0]);
        r = in[1] + mix * (out[1] - in[1]);
    }
private:
    int type = 0, combLen = 100, combPos = 0, hold = 1, holdCount = 0;
    float mix = 0.0f, amount = 0.5f, ratio = 1.0f, bits = 8.0f, g = 0.1f, fold = 1.0f, sr = 48000.0f;
    double ph = 0.0, inc = 0.0;
    std::array<std::array<float, 2048>, 2> comb {};
    float held[2] {}, lp[2] {}, bp[2] {};
};
} // namespace nb
