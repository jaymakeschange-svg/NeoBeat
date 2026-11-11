// BatchCut tests: random numbers that are the same on every platform. std::mt19937's raw output is fixed by
// the C++ standard; the library's distributions are not (MSVC and libstdc++ give different noise from the
// same seed), so the test recordings are made from the raw output only.
#pragma once

#include <cmath>
#include <cstdint>
#include <random>

struct PortableRandom
{
    explicit PortableRandom (unsigned s = 1) : rng (s) {}
    void seed (unsigned s) { rng.seed (s); have = false; }
    double uniform01() { return ((double) rng() + 0.5) / 4294967296.0; }   // in (0, 1)
    double uni (double a, double b) { return a + (b - a) * uniform01(); }
    float gauss()   // Box-Muller
    {
        if (have) { have = false; return spare; }
        const double u1 = uniform01(), u2 = uniform01(), r = std::sqrt (-2.0 * std::log (u1));
        spare = (float) (r * std::sin (6.283185307179586 * u2)); have = true;
        return (float) (r * std::cos (6.283185307179586 * u2));
    }
private:
    std::mt19937 rng;
    bool have = false;
    float spare = 0.0f;
};
