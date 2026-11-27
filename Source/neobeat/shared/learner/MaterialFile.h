// A fitted body written as the learner writes a material file (tools/fit_v2.py in MaterialLab: name,
// loss, start_loss, seconds, info, n_samples, and params with every key of engine2), so the learner
// opens one fitted in Batchcut as one of its own, and Batchcut's MATERIAL room reads either.
//
// Numbers are written with 17 significant digits, which read back to the same double. No JUCE: the
// plugin writes with it and the container's tests check the files against the learner.
#pragma once

#include "Body2.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace ml
{
struct MaterialInfo
{
    std::string name;                // shown in the room and the pool, e.g. "pvc pipe fit 1"
    std::string source;              // the file or pool sound it was fitted from
    std::string madeBy;              // e.g. "Batchcut 3.0.0"
    int rank = 1, of = 1;            // its place among the fits of one sound, closest first
    double loss = 0.0, startLoss = 0.0, seconds = 0.0;
    double noiseFloor = -70.0;       // dB, as the fit measured it
    int nSamples = 0;                // the length of the clip it was fitted to, at 48 kHz
};

// a string as JSON: quotes, backslashes and control characters escaped; UTF-8 passes through
inline std::string jsonString (const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s)
    {
        if (c == '"' || c == '\\') { o += '\\'; o += (char) c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char) c;
    }
    return o + "\"";
}

inline std::string jsonNumber (double v)
{
    char b[40];
    std::snprintf (b, sizeof b, "%.17g", std::isfinite (v) ? v : 0.0);
    return b;
}

inline std::string materialJson (const Body& p, const MaterialInfo& m)
{
    auto list = [] (const double* v, size_t n) { std::string t = "["; for (size_t i = 0; i < n; ++i) t += (i ? ", " : "") + jsonNumber (v[i]); return t + "]"; };
    std::string o = "{\n";
    o += "  \"name\": " + jsonString (m.name) + ",\n";
    o += "  \"loss\": " + jsonNumber (m.loss) + ",\n";
    o += "  \"start_loss\": " + jsonNumber (m.startLoss) + ",\n";
    o += "  \"seconds\": " + jsonNumber (std::round (m.seconds * 10.0) / 10.0) + ",\n";
    o += "  \"info\": {\"source\": " + jsonString (m.source) + ", \"fit\": " + std::to_string (m.rank) + ", \"of\": " + std::to_string (m.of)
       + ", \"noise_floor_db\": " + jsonNumber (m.noiseFloor) + ", \"made_by\": " + jsonString (m.madeBy) + "},\n";
    o += "  \"n_samples\": " + std::to_string (m.nSamples) + ",\n";
    o += "  \"params\": {\n";
    const std::pair<const char*, const std::vector<double>*> modes[] = { { "f", &p.f }, { "a", &p.a }, { "rho", &p.rho }, { "delta", &p.delta },
                                                                         { "beta", &p.beta }, { "eps", &p.eps }, { "glide", &p.glide }, { "tau_g", &p.tau_g } };
    for (const auto& k : modes) o += std::string ("    \"") + k.first + "\": " + list (k.second->data(), k.second->size()) + ",\n";
    const std::pair<const char*, double> laws[] = { { "b1", p.b1 }, { "b3", p.b3 }, { "C", p.C }, { "gamma", p.gamma }, { "tau_c", p.tau_c },
                                                    { "G", p.G }, { "tau_a", p.tau_a }, { "cx_amt", p.cx_amt }, { "cx_t", p.cx_t }, { "cx_f", p.cx_f },
                                                    { "lb_m", p.lb_m }, { "lb_rate", p.lb_rate }, { "lb_f", p.lb_f }, { "tau_n", p.tau_n } };
    for (const auto& l : laws) o += std::string ("    \"") + l.first + "\": " + jsonNumber (l.second) + ",\n";
    o += "    \"noise\": " + list (p.noise.data(), p.noise.size()) + ",\n";
    o += "    \"dn\": " + list (p.dn.data(), p.dn.size()) + ",\n";
    o += "    \"dn_rho\": " + list (p.dn_rho.data(), p.dn_rho.size()) + "\n";
    o += "  }\n}\n";
    return o;
}

} // namespace ml
