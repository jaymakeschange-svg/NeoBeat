// Shared by NeoBeat and Batchcut — the local learner: a small neural network, kept on this computer,
// that learns two things from you:
//   which part of a kit a sound is   (every time you move a sound to another lane)
//   which sounds you like            (every time you give one 1 to 5 stars)
// It starts from rules (SoundAnalysis.h); what you teach it is added on top. (NeoBeat's own kits are
// not used to teach it: they are found objects, rubber and metal, not the kick, snare and hat the
// lanes are named after, and would teach it wrong.) Everything it knows is a list of examples in one
// text file, shared by both apps; the networks are trained again from that list whenever it changes
// (a moment's work), so nothing is hidden in weights.
#pragma once

#include "SoundAnalysis.h"
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace nbk
{
// One hidden layer, tanh. Small enough to train in milliseconds.
class TinyNet
{
public:
    void init (int inputs, int hidden, int outputs, uint32_t seed)
    {
        nIn = inputs; nHid = hidden; nOut = outputs;
        w1.assign ((size_t) (nHid * (nIn + 1)), 0.0f); w2.assign ((size_t) (nOut * (nHid + 1)), 0.0f);
        std::mt19937 rng (seed);
        auto uni = [&] { return (float) (rng() >> 8) * (2.0f / 16777216.0f) - 1.0f; };
        const float s = 1.0f / std::sqrt ((float) nIn);
        for (auto& w : w1) w = uni() * s;
        // the output layer starts at zero: an untrained net adds nothing to the rules
        h.assign ((size_t) nHid, 0.0f); out.assign ((size_t) nOut, 0.0f);
        m1.assign (w1.size(), 0.0f); m2.assign (w2.size(), 0.0f);
    }
    const std::vector<float>& run (const float* x)
    {
        for (int j = 0; j < nHid; ++j)
        {
            const float* w = &w1[(size_t) (j * (nIn + 1))];
            float a = w[nIn]; for (int i = 0; i < nIn; ++i) a += w[i] * x[i];
            h[(size_t) j] = std::tanh (a);
        }
        for (int k = 0; k < nOut; ++k)
        {
            const float* w = &w2[(size_t) (k * (nHid + 1))];
            float a = w[nHid]; for (int j = 0; j < nHid; ++j) a += w[j] * h[(size_t) j];
            out[(size_t) k] = a;
        }
        return out;
    }
    // one step down the gradient: dOut is dLoss/dOutput for the x last run
    void learn (const float* x, const float* dOut, float rate, float decay = 1.0e-4f)
    {
        std::vector<float> dh ((size_t) nHid, 0.0f);
        for (int k = 0; k < nOut; ++k)
        {
            float* w = &w2[(size_t) (k * (nHid + 1))]; float* m = &m2[(size_t) (k * (nHid + 1))];
            for (int j = 0; j < nHid; ++j) { dh[(size_t) j] += dOut[k] * w[j]; m[j] = 0.9f * m[j] + dOut[k] * h[(size_t) j] + decay * w[j]; w[j] -= rate * m[j]; }
            m[nHid] = 0.9f * m[nHid] + dOut[k]; w[nHid] -= rate * m[nHid];
        }
        for (int j = 0; j < nHid; ++j)
        {
            const float g = dh[(size_t) j] * (1.0f - h[(size_t) j] * h[(size_t) j]);
            float* w = &w1[(size_t) (j * (nIn + 1))]; float* m = &m1[(size_t) (j * (nIn + 1))];
            for (int i = 0; i < nIn; ++i) { m[i] = 0.9f * m[i] + g * x[i] + decay * w[i]; w[i] -= rate * m[i]; }
            m[nIn] = 0.9f * m[nIn] + g; w[nIn] -= rate * m[nIn];
        }
    }
private:
    int nIn = 0, nHid = 0, nOut = 0;
    std::vector<float> w1, w2, m1, m2, h, out;
};

struct Example
{
    std::array<float, kDim> x {};
    int role = -1;          // the lane it belongs on (-1: not said)
    float stars = 0.0f;     // 1..5 (0: not rated)
    int weight = 1;         // yours count for more than the built-in ones
    std::string name;       // for you, in the list
};

class Learner
{
public:
    Learner() { reset(); }

    // ---- what it knows
    void addSeed (const std::array<float, kDim>& x, int role) { Example e; e.x = x; e.role = role; e.weight = 1; e.name = "(NeoBeat kit)"; seeds.push_back (e); dirty = true; }
    // you moved a sound to a lane: that is what it is
    void teachRole (const std::array<float, kDim>& x, int role, const std::string& name)
    {
        if (role < 0 || role >= kRoles) return;
        Example* e = find (x);
        if (e == nullptr) { examples.push_back ({}); e = &examples.back(); e->x = x; }
        e->role = role; e->weight = 4; e->name = name; dirty = true; trim();
    }
    // you rated a sound (1..5; 0 takes the rating back)
    void rate (const std::array<float, kDim>& x, int role, float stars, const std::string& name)
    {
        Example* e = find (x);
        if (e == nullptr) { examples.push_back ({}); e = &examples.back(); e->x = x; e->role = role; }
        e->stars = std::max (0.0f, std::min (5.0f, stars)); e->weight = 4; e->name = name; dirty = true; trim();
    }
    float ratingOf (const std::array<float, kDim>& x) const { const Example* e = const_cast<Learner*> (this)->find (x); return e != nullptr ? e->stars : 0.0f; }
    int numRatings() const { int n = 0; for (const auto& e : examples) n += e.stars > 0.0f ? 1 : 0; return n; }
    int numTaught() const { int n = 0; for (const auto& e : examples) n += e.role >= 0 ? 1 : 0; return n; }
    void forget() { examples.clear(); dirty = true; }

    // ---- what it says
    // how well a sound fits each lane: the rules, moved by what it has learned (sums to 1)
    std::array<float, kRoles> roles (const Features& f)
    {
        train();
        const auto prior = rolePrior (f);
        const auto x = vectorOf (f);
        const auto& o = roleNet.run (x.data());
        std::array<float, kRoles> p {}; float mx = -1.0e30f;
        for (int k = 0; k < kRoles; ++k) { p[(size_t) k] = std::log (prior[(size_t) k]) + o[(size_t) k]; mx = std::max (mx, p[(size_t) k]); }
        float total = 0.0f; for (auto& v : p) { v = std::exp (v - mx); total += v; }
        for (auto& v : p) v /= total;
        return p;
    }
    // 0..1: how much you are likely to like it (0.5 until you have rated a few)
    float preference (const Features& f, int role)
    {
        train();
        const int n = numRatings();
        if (n < 3) return 0.5f;
        const auto x = vectorOf (f);
        // a memory of the sounds you rated: the nearer, the more they say
        float wsum = 0.0f, vsum = 0.0f;
        for (const auto& e : examples)
        {
            if (e.stars <= 0.0f) continue;
            const float d = distance (x, e.x);
            const float w = std::exp (-(d * d) / (2.0f * 0.25f * 0.25f)) * (e.role == role ? 1.0f : 0.6f);
            wsum += w; vsum += w * (e.stars - 1.0f) / 4.0f;
        }
        const float memory = wsum > 1.0e-4f ? vsum / wsum : 0.5f;
        const float near = std::min (1.0f, wsum);                         // how much the memory has to say here
        float in[kDim + kRoles] {};
        for (int i = 0; i < kDim; ++i) in[i] = x[(size_t) i];
        if (role >= 0 && role < kRoles) in[kDim + role] = 1.0f;
        const float net = std::max (0.0f, std::min (1.0f, 0.5f + rateNet.run (in)[0]));
        const float guess = near * memory + (1.0f - near) * net;
        const float trust = std::min (1.0f, (float) n / 12.0f);          // a few ratings move it a little, a dozen all the way
        return std::max (0.0f, std::min (1.0f, 0.5f + trust * (guess - 0.5f)));
    }
    // one number for choosing: fit to the lane, how usable, how much you'd like it
    float pickScore (const Features& f, int role)
    {
        const auto r = roles (f);
        const float fit = std::min (1.0f, r[(size_t) role] * 3.0f);
        return (0.25f + 0.75f * fit) * (0.3f + 0.7f * musicalScore (f)) * (0.5f + preference (f, role));
    }

    // ---- the file (text: one example per line)
    bool load (const std::string& path)
    {
        std::ifstream in (path);
        if (! in) return false;
        std::string line; std::vector<Example> got;
        if (! std::getline (in, line) || line.rfind ("NBK-LEARNER 1", 0) != 0) return false;
        while (std::getline (in, line))
        {
            std::istringstream ss (line);
            Example e; int dim = 0;
            if (! (ss >> e.role >> e.stars >> e.weight >> dim) || dim != kDim) continue;
            bool ok = true; for (int i = 0; i < kDim; ++i) if (! (ss >> e.x[(size_t) i])) ok = false;
            if (! ok) continue;
            std::getline (ss, e.name); if (! e.name.empty() && e.name[0] == ' ') e.name.erase (0, 1);
            got.push_back (e);
        }
        examples = std::move (got); dirty = true; trim();
        return true;
    }
    bool save (const std::string& path) const
    {
        std::ofstream out (path, std::ios::trunc);
        if (! out) return false;
        out << "NBK-LEARNER 1\n";
        for (const auto& e : examples)
        {
            out << e.role << ' ' << e.stars << ' ' << e.weight << ' ' << kDim;
            for (float v : e.x) out << ' ' << v;
            std::string n = e.name; for (auto& c : n) if (c == '\n' || c == '\r') c = ' ';
            out << ' ' << n << '\n';
        }
        return (bool) out;
    }
    // what another copy of the app saved meanwhile: keep both (the same sound once)
    void merge (const Learner& other)
    {
        for (const auto& e : other.examples)
            if (Example* mine = find (e.x)) { if (e.role >= 0 && mine->role < 0) mine->role = e.role; if (e.stars > 0.0f && mine->stars <= 0.0f) mine->stars = e.stars; }
            else examples.push_back (e);
        dirty = true; trim();
    }
    const std::vector<Example>& all() const { return examples; }

private:
    void reset() { roleNet.init (kDim, 20, kRoles, 1234u); rateNet.init (kDim + kRoles, 14, 1, 4321u); dirty = true; }
    Example* find (const std::array<float, kDim>& x)
    {
        for (auto& e : examples) if (distance (e.x, x) < 1.0e-4f) return &e;
        return nullptr;
    }
    void trim() { if (examples.size() > 4000) examples.erase (examples.begin(), examples.begin() + (long) (examples.size() - 4000)); }

    void train()
    {
        if (! dirty) return;
        dirty = false;
        roleNet.init (kDim, 20, kRoles, 1234u); rateNet.init (kDim + kRoles, 14, 1, 4321u);
        // roles: the rules are the starting point; the net learns what is left over
        std::vector<const Example*> set;
        for (const auto& e : seeds) set.push_back (&e);
        for (const auto& e : examples) if (e.role >= 0) set.push_back (&e);
        std::mt19937 rng (99u);
        if (! set.empty())
        {
            const int epochs = (int) std::min<size_t> (160, 4000 / set.size() + 30);
            for (int ep = 0; ep < epochs; ++ep)
            {
                const float rate = 0.02f / (1.0f + 0.02f * (float) ep);
                for (size_t n = 0; n < set.size(); ++n)
                {
                    const Example& e = *set[(size_t) (rng() % set.size())];
                    const auto& o = roleNet.run (e.x.data());
                    // the prior is not known here (only the vector is kept): learn the lane directly, softly
                    float p[kRoles]; float mx = -1.0e30f; for (int k = 0; k < kRoles; ++k) mx = std::max (mx, o[(size_t) k]);
                    float total = 0.0f; for (int k = 0; k < kRoles; ++k) { p[k] = std::exp (o[(size_t) k] - mx); total += p[k]; }
                    float d[kRoles]; for (int k = 0; k < kRoles; ++k) d[k] = (p[k] / total - (k == e.role ? 1.0f : 0.0f)) * (float) e.weight * 0.5f;
                    roleNet.learn (e.x.data(), d, rate);
                }
            }
        }
        // ratings
        std::vector<const Example*> rated;
        for (const auto& e : examples) if (e.stars > 0.0f) rated.push_back (&e);
        if (rated.size() >= 3)
        {
            const int epochs = (int) std::min<size_t> (300, 6000 / rated.size() + 40);
            for (int ep = 0; ep < epochs; ++ep)
            {
                const float rate = 0.03f / (1.0f + 0.02f * (float) ep);
                for (size_t n = 0; n < rated.size(); ++n)
                {
                    const Example& e = *rated[(size_t) (rng() % rated.size())];
                    float in[kDim + kRoles] {};
                    for (int i = 0; i < kDim; ++i) in[i] = e.x[(size_t) i];
                    if (e.role >= 0 && e.role < kRoles) in[kDim + e.role] = 1.0f;
                    const float y = (e.stars - 1.0f) / 4.0f - 0.5f;
                    const float d = rateNet.run (in)[0] - y;
                    rateNet.learn (in, &d, rate, 1.0e-3f);
                }
            }
        }
    }

    std::vector<Example> seeds, examples;
    TinyNet roleNet, rateNet;
    bool dirty = true;
};

// ------------------------------------------------------------------ choosing a kit from a pile of sounds
struct Candidate { Features f; std::array<float, kDim> x {}; std::array<float, kRoles> roles {}; float quality = 0.0f; };

// One sound per lane (index into `pool`, or -1 when nothing fits): the best fit first for the lanes a
// kit cannot do without, each sound used once, and no two picks alike.
// `skip`: lanes that are left alone (they get no pick, and take no sound away from the others).
inline std::array<int, kRoles> chooseKit (Learner& learner, const std::vector<Candidate>& pool, bool fillEveryLane, const std::array<bool, kRoles>* skip = nullptr)
{
    std::array<int, kRoles> pick; pick.fill (-1);
    std::vector<char> used (pool.size(), 0);
    // what a kit cannot do without is settled first when scores are close
    static const float need[kRoles] = { 1.3f, 1.0f, 1.3f, 1.0f, 1.25f, 1.15f, 1.0f, 1.0f, 1.0f, 1.0f, 0.9f, 0.9f, 0.95f, 0.95f, 1.0f, 0.85f };
    std::vector<float> like (pool.size() * kRoles, 1.0f);
    for (size_t i = 0; i < pool.size(); ++i) for (int r = 0; r < kRoles; ++r) like[i * kRoles + (size_t) r] = 0.5f + learner.preference (pool[i].f, r);
    for (int pass = 0; pass < (fillEveryLane ? 2 : 1); ++pass)
        while (true)
        {
            float best = pass == 0 ? 0.12f : -1.0f; int bestI = -1, bestR = -1;      // first: only sounds that really fit
            for (int role = 0; role < kRoles; ++role)
            {
                if (pick[(size_t) role] >= 0 || (skip != nullptr && (*skip)[(size_t) role])) continue;
                for (size_t i = 0; i < pool.size(); ++i)
                {
                    if (used[i] || ! pool[i].f.valid) continue;
                    const float fit = std::min (1.0f, pool[i].roles[(size_t) role] * 3.0f);
                    float s = (pass == 0 ? fit : 0.25f + 0.75f * fit) * (0.3f + 0.7f * pool[i].quality) * like[i * kRoles + (size_t) role] * need[role];
                    // variety: less for a sound much like one already chosen
                    for (int r = 0; r < kRoles; ++r) if (pick[(size_t) r] >= 0) s *= 0.55f + 0.45f * std::min (1.0f, distance (pool[i].x, pool[(size_t) pick[(size_t) r]].x) / 0.25f);
                    if (s > best) { best = s; bestI = (int) i; bestR = role; }
                }
            }
            if (bestI < 0) break;
            pick[(size_t) bestR] = bestI; used[(size_t) bestI] = 1;
        }
    return pick;
}
} // namespace nbk
