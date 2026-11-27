// The MaterialLab learner's port, checked stage by stage against the learner itself.
//   test_learner GOLDEN_DIR      (a directory written by the reference scripts: start/, fitted/, x.f32)
// Stage 2: the body renders the same samples as engine2.render_physical from the same parameters
// and the same noise. The learner computes in float32 and this port in double, so "the same" is a
// tolerance, stated with each check.
#include "../Source/neobeat/shared/learner/Body2.h"
#include "../Source/neobeat/shared/learner/Loss2.h"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <cstdlib>
#include <iterator>

static int failures = 0;
#define CHECK(cond, ...) do { std::printf ((cond) ? "  ok:   " : "  FAIL: "); std::printf (__VA_ARGS__); std::printf ("\n"); if (! (cond)) ++failures; } while (0)

template <typename T> static std::vector<T> readRaw (const std::string& path)
{
    std::ifstream f (path, std::ios::binary | std::ios::ate);
    const auto bytes = (size_t) f.tellg(); f.seekg (0);
    std::vector<T> v (bytes / sizeof (T));
    f.read ((char*) v.data(), (std::streamsize) bytes);
    return v;
}

static ml::Body readBody (const std::string& path)
{
    std::map<std::string, std::vector<double>> kv;
    std::ifstream f (path); std::string line;
    while (std::getline (f, line))
    {
        std::istringstream is (line); std::string key; size_t n = 0; is >> key >> n;
        std::vector<double> v (n); for (auto& x : v) is >> x;
        kv[key] = v;
    }
    ml::Body b;
    b.f = kv["f"]; b.a = kv["a"]; b.rho = kv["rho"]; b.delta = kv["delta"]; b.beta = kv["beta"];
    b.eps = kv["eps"]; b.glide = kv["glide"]; b.tau_g = kv["tau_g"];
    auto one = [&] (const char* k) { return kv[k].at (0); };
    b.b1 = one ("b1"); b.b3 = one ("b3"); b.C = one ("C"); b.gamma = one ("gamma"); b.tau_c = one ("tau_c");
    b.G = one ("G"); b.tau_a = one ("tau_a"); b.cx_amt = one ("cx_amt"); b.cx_t = one ("cx_t"); b.cx_f = one ("cx_f");
    b.lb_m = one ("lb_m"); b.lb_rate = one ("lb_rate"); b.lb_f = one ("lb_f"); b.tau_n = one ("tau_n");
    for (int i = 0; i < ml::kBands; ++i) { b.noise[(size_t) i] = kv["noise"].at ((size_t) i); b.dn[(size_t) i] = kv["dn"].at ((size_t) i); b.dn_rho[(size_t) i] = kv["dn_rho"].at ((size_t) i); }
    return b;
}

static void stage2 (const std::string& dir, const char* tag)
{
    const auto body = readBody (dir + "/body.txt");
    const auto bands = readRaw<float> (dir + "/bands.f32"), bodyb = readRaw<float> (dir + "/bodyb.f32"), ref = readRaw<float> (dir + "/y.f32");
    const auto phases = readRaw<double> (dir + "/phases.f64");
    const int n = (int) ref.size();
    std::vector<float> y ((size_t) n);
    ml::render (body, n, 48000.0, bands.data(), bodyb.data(), phases.data(), y.data());
    double err = 0, sig = 0, peakErr = 0;
    for (int s = 0; s < n; ++s) { const double d = (double) y[(size_t) s] - ref[(size_t) s]; err += d * d; sig += (double) ref[(size_t) s] * ref[(size_t) s]; peakErr = std::max (peakErr, std::fabs (d)); }
    const double rel = std::sqrt (err / (sig + 1e-30));
    std::printf ("  %s: %d modes, %d samples; relative rms difference %.2e, largest sample difference %.2e\n", tag, body.modes(), n, rel, peakErr);
    CHECK (rel < 1e-3, "%s: the port renders the learner's samples (relative rms < 1e-3; float32 against double)", tag);
}

// the learner's loss at a golden point, as JAX reported it (written by golden.py into the npz,
// copied here by export.py into loss.txt)
static double readNumber (const std::string& path) { std::ifstream f (path); double v = 0; f >> v; return v; }

static void stage3 (const std::string& g, const std::string& dir, const char* tag)
{
    const auto x = readRaw<float> (g + "/x.f32");
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    bool has = false; double floor = 0.0;
    if (at != std::string::npos) { const auto v = meta.substr (at + 17); if (v.find ("null") > 2) { has = true; floor = std::atof (v.c_str()); } }
    const ml::Loss2 loss (x.data(), (int) x.size(), 48000.0, has, floor);
    const auto ref = readRaw<float> (dir + "/y.f32");
    const double want = readNumber (dir + "/loss.txt");
    const double got = loss.value (ref.data());
    std::printf ("  %s: the learner's render scores %.6f here, %.6f in the learner\n", tag, got, want);
    CHECK (std::fabs (got - want) < 1e-4 * std::max (1.0, want), "%s: the loss matches the learner's (to 1e-4)", tag);
    // and the port's own render through the port's own loss
    const auto body = readBody (dir + "/body.txt");
    const auto bands = readRaw<float> (dir + "/bands.f32"), bodyb = readRaw<float> (dir + "/bodyb.f32");
    const auto phases = readRaw<double> (dir + "/phases.f64");
    std::vector<float> y (ref.size());
    ml::render (body, (int) y.size(), 48000.0, bands.data(), bodyb.data(), phases.data(), y.data());
    const double own = loss.value (y.data());
    CHECK (std::fabs (own - want) < 1e-3 * std::max (1.0, want), "%s: the port's render through the port's loss: %.6f (to 1e-3)", tag, own);
}

int main (int argc, char** argv)
{
    const std::string g = argc > 1 ? argv[1] : "/tmp/mlref/pvc";
    std::printf ("stage 2: the body, against engine2.render_physical\n");
    stage2 (g + "/start", "the analysis starting point");
    stage2 (g + "/fitted", "the user's fitted material");
    std::printf ("stage 3: the loss, against fit2.make_loss\n");
    stage3 (g, g + "/start", "the analysis starting point");
    stage3 (g, g + "/fitted", "the user's fitted material");
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
