#pragma once

// MaterialLab: the learned object, played.
//
// The learner fits a physical model to a recording and writes it out as a material file - 128
// modes, each with a frequency, a level, a damping residual, a doublet split and a pitch glide,
// plus the laws that govern all of them together. What it captures is not a sound. It is how a
// particular piece of metal, plastic or rubber behaves when something hits it.
//
// This plays those files. Two ways in:
//
//   strike(velocity)   excite the body and let it ring
//   process(audio)     send a sound through the body and hear it in that material
//
// The second one is why this is a resonator bank rather than a copy of the learner's renderer.
// The learner is additive: it builds each mode as a pair of sines with an amplitude envelope,
// which is exact and cannot be driven by anything. A mode with frequency f and damping sigma is
// also a two-pole resonator with pole radius exp(-sigma/fs), and in that form the same object can
// be struck OR have audio poured through it. The difference matters musically, not just
// technically: a resonator has state, so striking a body that is still ringing adds to it rather
// than starting again. Sixteen hits in a bar are one object being beaten, which is the thing a
// sampler cannot do at all.
//
// What is faithful to the learner: mode frequencies and levels, the damping law
// (b1 + b3*(f/1000)^2) * exp(rho), the doublet split that makes partials beat, the contact damping
// that switches on when the striker stays against the body, the per-mode pitch glide, and the two
// listening points either side of the strike.
//
// What is not yet carried over, and is honest to say so: the energy cascade between modes, the
// low-body rock, the global tension term, and the 16-band dense-noise layer. Each shapes the sound
// further; none of them is what makes a clank a clank. They belong in a second pass.

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace nsr
{

struct Material
{
    juce::String name;
    int numModes = 0;

    // per mode, straight from the file
    std::vector<float> f;        // Hz
    std::vector<float> a;        // level
    std::vector<float> rho;      // damping residual, exp()'d against the law
    std::vector<float> delta;    // doublet split in Hz: two partials this far apart, so they beat
    std::vector<float> beta;     // how much of the second partial, 0..0.5
    std::vector<float> glide;    // pitch glide in log-f, decaying with tau_g
    std::vector<float> tauG;

    // the laws that bind them
    float b1 = 20.0f, b3 = 0.05f;          // damping: sigma = (b1 + b3*(f/1000)^2) * exp(rho)
    float tauA = 0.001f;                   // attack ramp
    float cxAmt = 0.0f, cxT = 9.0f, cxF = 600.0f;   // contact damping, after cxT, below cxF

    bool valid() const noexcept { return numModes > 0; }

    // Reads a material file written by the learner. Returns false and leaves this untouched if the
    // file is not one.
    bool load (const juce::File& file, juce::String& error)
    {
        auto json = juce::JSON::parse (file);
        if (! json.isObject()) { error = file.getFileName() + " is not a material file."; return false; }
        auto* o = json.getDynamicObject();
        if (o == nullptr) { error = file.getFileName() + " is not a material file."; return false; }

        auto* params = o->getProperty ("params").getDynamicObject();
        if (params == nullptr) { error = file.getFileName() + " has no params - is it a material file?"; return false; }

        auto arr = [params] (const char* key, std::vector<float>& out)
        {
            out.clear();
            if (auto* v = params->getProperty (key).getArray())
                for (const auto& x : *v) out.push_back ((float) (double) x);
        };
        auto num = [params] (const char* key, float fallback)
        {
            const auto v = params->getProperty (key);
            return v.isVoid() ? fallback : (float) (double) v;
        };

        Material m;
        arr ("f", m.f); arr ("a", m.a); arr ("rho", m.rho);
        arr ("delta", m.delta); arr ("beta", m.beta);
        arr ("glide", m.glide); arr ("tau_g", m.tauG);

        if (m.f.empty() || m.a.size() != m.f.size())
        { error = file.getFileName() + " has no modes in it."; return false; }

        m.numModes = (int) m.f.size();
        auto fill = [&m] (std::vector<float>& v, float with)
        { if ((int) v.size() != m.numModes) v.assign ((size_t) m.numModes, with); };
        fill (m.rho, 0.0f); fill (m.delta, 0.0f); fill (m.beta, 0.0f);
        fill (m.glide, 0.0f); fill (m.tauG, 0.05f);

        m.b1 = num ("b1", 20.0f);  m.b3 = num ("b3", 0.05f);
        m.tauA = num ("tau_a", 0.001f);
        m.cxAmt = num ("cx_amt", 0.0f);
        m.cxT = num ("cx_t", 9.0f);
        m.cxF = num ("cx_f", 600.0f);

        m.name = o->getProperty ("name").toString();
        if (m.name.isEmpty()) m.name = file.getFileNameWithoutExtension();

        *this = std::move (m);
        return true;
    }

    // The damping of one mode, before contact. This is the law the learner fitted, verbatim.
    float sigmaOf (int i) const noexcept
    {
        const float hz = f[(size_t) i];
        return (b1 + b3 * (hz / 1000.0f) * (hz / 1000.0f)) * std::exp (rho[(size_t) i]);
    }

    // How long the loudest modes take to fall 60 dB. Useful for sizing a render.
    float ringSeconds() const noexcept
    {
        float slowest = 0.0f;
        for (int i = 0; i < numModes; ++i)
        {
            const float s = sigmaOf (i);
            if (s > 1.0e-4f && a[(size_t) i] > 1.0e-3f) slowest = std::max (slowest, 6.9078f / s);
        }
        return juce::jlimit (0.05f, 20.0f, slowest);
    }
};

// ------------------------------------------------------------------ playing one

// How the object is heard. The learner's space model listens from two points, one each side of
// the strike: each hears the modes at slightly different levels, so the pair is wide at equal
// loudness rather than panned. Chassis's frappe arrives at the same answer independently, which
// is a fair sign it is the right one.
struct Pickups
{
    float width = 0.35f;     // 0 = both points hear the same thing (mono), 1 = very different
    float fd = 900.0f;       // modes above this differ more between the points
    float q = 1.6f;
};

class MaterialVoice
{
public:
    void prepare (double sampleRate, int maxModes = 256)
    {
        sr = (float) std::max (8000.0, sampleRate);
        m.assign ((size_t) std::max (1, maxModes), Mode());
        active = 0;
        reset();
    }

    void setMaterial (const Material& mat, const Pickups& pk = {})
    {
        active = std::min ((int) m.size(), mat.numModes);
        src = &mat;

        for (int i = 0; i < active; ++i)
        {
            auto& md = m[(size_t) i];
            md.f0 = mat.f[(size_t) i];
            md.amp = mat.a[(size_t) i];
            md.sigma = mat.sigmaOf (i);
            md.half = 0.5f * mat.delta[(size_t) i];
            md.beta = mat.beta[(size_t) i];
            md.glide = mat.glide[(size_t) i];
            md.tauG = std::max (1.0e-4f, mat.tauG[(size_t) i]);

            // Contact damping reaches the modes below cxF. Same shape the learner used.
            md.contact = mat.cxAmt / (1.0f + std::exp (-(mat.cxF - md.f0) / (0.25f * mat.cxF)));

            // The two listening points. Deterministic rather than random, so the same material
            // always sounds the same: the sign alternates and the depth follows the learner's
            // law, which makes high modes differ more between the points than low ones.
            const float r = 1.0f / (1.0f + std::pow (md.f0 / 900.0f, 1.6f));
            const float spread = std::sqrt (std::max (0.0f, (1.0f - r) / (1.0f + r))) * pk.width;
            const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            const float wobble = 0.6f + 0.4f * std::sin ((float) i * 2.39996f);   // golden angle
            md.wl = 1.0f + sign * spread * wobble;
            md.wr = 1.0f - sign * spread * wobble;
        }
        retune (0.0f);
        reset();
    }

    void reset() noexcept
    {
        for (auto& md : m) { md.y1a = md.y2a = md.y1b = md.y2b = 0.0f; }
        age = 0.0f;
        ringing = false;
    }

    // Hit it. The body keeps whatever it was already doing, which is the point: strike a ringing
    // object again and the energy adds.
    void strike (float velocity) noexcept
    {
        const float v = juce::jlimit (0.0f, 1.0f, velocity);
        age = 0.0f;
        pending = v;
        ringing = true;
    }

    bool isRinging() const noexcept { return ringing; }

    // Render `n` samples, adding into l/r. `in` is optional: when given, that audio excites the
    // body instead of (or as well as) a strike, which is how a sound is "run through" a material.
    void process (float* l, float* r, int n, const float* in = nullptr, float inGain = 1.0f) noexcept
    {
        if (active <= 0 || src == nullptr) return;

        for (int s = 0; s < n; ++s)
        {
            // control-rate work: retune for glide and switch contact damping in
            if (ctr-- <= 0) { ctr = kHop; retune (age); }

            float x = 0.0f;
            if (pending > 0.0f) { x += pending; pending = 0.0f; }
            if (in != nullptr) x += in[s] * inGain;

            float oL = 0.0f, oR = 0.0f;
            for (int i = 0; i < active; ++i)
            {
                auto& md = m[(size_t) i];
                // two resonators a doublet apart, so the partials beat the way the body does
                const float ya = x * md.ga + md.ca * md.y1a - md.ra * md.y2a;
                md.y2a = md.y1a; md.y1a = ya;
                float v = (1.0f - md.beta) * ya;
                if (md.beta > 1.0e-4f)
                {
                    const float yb = x * md.gb + md.cb * md.y1b - md.rb * md.y2b;
                    md.y2b = md.y1b; md.y1b = yb;
                    v += md.beta * yb;
                }
                oL += v * md.wl;
                oR += v * md.wr;
            }

            l[s] += oL * out;
            r[s] += oR * out;
            age += 1.0f / sr;
        }

        // let it go once everything has died away
        if (in == nullptr)
        {
            float peak = 0.0f;
            for (int i = 0; i < active; ++i) peak = std::max (peak, std::fabs (m[(size_t) i].y1a));
            if (peak < 1.0e-6f) ringing = false;
        }
    }

    float level = 1.0f;

private:
    static constexpr int kHop = 64;      // the learner's control rate

    struct Mode
    {
        float f0 = 0.0f, amp = 0.0f, sigma = 0.0f, half = 0.0f, beta = 0.0f;
        float glide = 0.0f, tauG = 0.05f, contact = 0.0f;
        float wl = 1.0f, wr = 1.0f;
        float ga = 0.0f, ca = 0.0f, ra = 0.0f, y1a = 0.0f, y2a = 0.0f;
        float gb = 0.0f, cb = 0.0f, rb = 0.0f, y1b = 0.0f, y2b = 0.0f;
    };

    // Set each resonator's coefficients for the pitch and damping at time `t`.
    //
    // A two-pole resonator y = x + 2r cos(w) y1 - r^2 y2 has impulse response
    // r^n sin((n+1)w)/sin(w), so it rings at w and decays at r^n. Matching the learner:
    // r = exp(-sigma/fs), and the input is scaled by amp*sin(w) so the mode rings at the level
    // the file says.
    void retune (float t) noexcept
    {
        if (src == nullptr) return;
        const bool held = t >= src->cxT;

        for (int i = 0; i < active; ++i)
        {
            auto& md = m[(size_t) i];
            const float sigma = md.sigma + (held ? md.contact : 0.0f);
            const float gl = md.glide * std::exp (-t / md.tauG);
            const float hz = md.f0 * std::exp (gl);

            auto set = [&] (float f, float& g, float& c, float& rr)
            {
                const float w = juce::jlimit (1.0e-4f, 3.1f, 6.283185307f * f / sr);
                const float rad = juce::jlimit (0.0f, 0.99999f, std::exp (-sigma / sr));
                c = 2.0f * rad * std::cos (w);
                rr = rad * rad;
                g = md.amp * std::sin (w);
            };
            set (std::max (10.0f, hz - md.half), md.ga, md.ca, md.ra);
            if (md.beta > 1.0e-4f) set (hz + md.half, md.gb, md.cb, md.rb);
        }
    }

    const Material* src = nullptr;
    std::vector<Mode> m;
    int active = 0, ctr = 0;
    float sr = 48000.0f, age = 0.0f, pending = 0.0f, out = 1.0f;
    bool ringing = false;
};

} // namespace nsr
