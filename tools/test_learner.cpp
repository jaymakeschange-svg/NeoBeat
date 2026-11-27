// The MaterialLab learner's port, checked stage by stage against the learner itself.
//   test_learner GOLDEN_DIR      (a directory written by the reference scripts: start/, fitted/, x.f32)
// Stage 2: the body renders the same samples as engine2.render_physical from the same parameters
// and the same noise. The learner computes in float32 and this port in double, so "the same" is a
// tolerance, stated with each check.
#include "../Source/neobeat/shared/learner/Body2.h"
#include "../Source/neobeat/shared/learner/Loss2.h"
#include "../Source/neobeat/shared/learner/Analysis2.h"
#include "../Source/neobeat/shared/learner/Fit2.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
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
    // a body with glides runs its phases through long single-precision sums in the learner; where the
    // learner's render in double precision is on hand (render64.py), that is the exact reference
    std::ifstream y64f (dir + "/y64.f64");
    if (y64f)
    {
        const auto r64 = readRaw<double> (dir + "/y64.f64");
        double e2 = 0, s2 = 0;
        for (int s = 0; s < n; ++s) { const double d = (double) y[(size_t) s] - r64[(size_t) s]; e2 += d * d; s2 += r64[(size_t) s] * r64[(size_t) s]; }
        const double rel64 = std::sqrt (e2 / (s2 + 1e-30));
        std::printf ("    against the learner in double precision: relative rms difference %.2e\n", rel64);
        CHECK (rel64 < 1e-6, "%s: the port renders the learner's double-precision samples (output stored as float)", tag);
    }
    else
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

static double relDiff (const std::vector<double>& a, const std::vector<double>& b)
{
    double e = 0, s = 0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { e += (a[i] - b[i]) * (a[i] - b[i]); s += b[i] * b[i]; }
    return std::sqrt (e / (s + 1e-300));
}

static void stage4a (const std::string& g)
{
    const auto xf = readRaw<float> (g + "/x.f32");
    const std::vector<double> x (xf.begin(), xf.end());
    const auto A = ml::an::analyze (x, 48000);
    // the learner's tracks
    std::ifstream tf (g + "/tracks.txt"); std::string line; std::vector<ml::an::Track> ref;
    while (std::getline (tf, line)) { std::istringstream is (line); ml::an::Track t; int len; is >> t.start >> len; t.f.resize ((size_t) len); t.a.resize ((size_t) len); for (auto& v : t.f) is >> v; for (auto& v : t.a) is >> v; ref.push_back (t); }
    bool same = A.tracks.size() == ref.size();
    double worstF = 0, worstA = 0;
    for (size_t i = 0; same && i < ref.size(); ++i)
    {
        same = A.tracks[i].start == ref[i].start && A.tracks[i].length() == ref[i].length();
        for (int j = 0; same && j < ref[i].length(); ++j)
        {
            worstF = std::max (worstF, std::fabs (A.tracks[i].f[(size_t) j] - ref[i].f[(size_t) j]) / ref[i].f[(size_t) j]);
            worstA = std::max (worstA, std::fabs (A.tracks[i].a[(size_t) j] - ref[i].a[(size_t) j]) / (ref[i].a[(size_t) j] + 1e-12));
        }
    }
    std::printf ("  %zu tracks here, %zu in the learner; worst frequency difference %.1e, amplitude %.1e (relative)\n", A.tracks.size(), ref.size(), worstF, worstA);
    CHECK (same && worstF < 1e-6 && worstA < 1e-4, "the partial tracks are the learner's: same count, births and lengths, same paths");
    // each partial's behaviour
    std::ifstream df (g + "/desc.txt"); int bad = 0, n = 0;
    for (size_t i = 0; i < A.tracks.size() && std::getline (df, line); ++i)
    {
        std::istringstream is (line); int valid; is >> valid;
        const auto D = ml::an::describeTrack (A.tracks[i], 128, 48000);
        if (! valid) { bad += D.valid ? 1 : 0; continue; }
        int kind; double fq, born, peak, rise, glide, bh, bd, en; std::string dec;
        is >> kind >> fq >> born >> peak >> rise >> dec >> glide >> bh >> bd >> en;
        ++n;
        const bool ok = D.valid && (int) D.kind == kind && std::fabs (D.freqHz - fq) < 0.11 && std::fabs (D.bornMs - born) < 0.11
                        && std::fabs (D.riseDb - rise) < 0.11 && std::fabs (D.glideCents - glide) < 1.1 && std::fabs (D.beatHz - bh) < 0.011
                        && std::fabs (D.energy - en) < 1e-6 * std::max (1.0, en)
                        && (dec == "nan" ? ! D.decayDbPerS : (D.decayDbPerS && std::fabs (*D.decayDbPerS - std::atof (dec.c_str())) < 0.11));
        if (! ok) ++bad;
    }
    CHECK (bad == 0, "each partial's behaviour (kind, frequency, birth, rise, decay, glide, beating, energy) matches the learner's (%d described, %d differ)", n, bad);
    // the resynthesised partials, the residual and its noise
    const auto det = ml::an::synthesizePartials (A.tracks, 48000, 128, (int) x.size());
    const auto detRef = readRaw<float> (g + "/an_det.f32");
    const double dd = relDiff (det, std::vector<double> (detRef.begin(), detRef.end()));
    CHECK (dd < 1e-5, "the resynthesised partials, the learner's random phases included (relative rms %.1e)", dd);
    const auto resRef = readRaw<double> (g + "/an_res.f64");
    const double rd = relDiff (A.residual, resRef);
    CHECK ((int) resRef.size() == A.resBins * A.resFrames && rd < 1e-5, "the residual the partials leave, as scipy's stft sees it (relative rms %.1e)", rd);
    const auto nz = ml::an::synthesizeNoise (A, (int) x.size());
    const auto nzRef = readRaw<float> (g + "/an_noise.f32");
    const double nd = relDiff (nz, std::vector<double> (nzRef.begin(), nzRef.end()));
    CHECK (nd < 1e-5, "the residual noise, through scipy's istft with the learner's phases (relative rms %.1e)", nd);
}

static void stage4b (const std::string& g)
{
    const auto xf = readRaw<float> (g + "/x.f32");
    const std::vector<double> x (xf.begin(), xf.end());
    const auto A = ml::an::analyze (x, 48000);
    std::array<double, ml::kBands> gains {}, rates {};
    ml::an::bodyNoiseBands (x, 48000, A, gains, rates);
    std::ifstream bf (g + "/bandsfit.txt"); std::vector<double> rg (ml::kBands), rr (ml::kBands);
    for (auto& v : rg) bf >> v;
    for (auto& v : rr) bf >> v;
    double wg = 0, wr = 0;
    for (int b = 0; b < ml::kBands; ++b) { wg = std::max (wg, std::fabs (gains[(size_t) b] / rg[(size_t) b] - 1)); wr = std::max (wr, std::fabs (rates[(size_t) b] / rr[(size_t) b] - 1)); }
    // 1e-5, not 1e-6: the noise these are measured on already differs from the learner's by ~1e-7
    // (float32 rounding through istft), the decay is a fitted slope, and the band-pass is the same
    // filter factored into different sections from scipy's - so rounding, not a different result.
    CHECK (wg < 1e-5 && wr < 1e-5, "the noise layer by band: level and decay, through a Butterworth band-pass and zero-phase filtering (worst %.1e, %.1e; to 1e-5)", wg, wr);
    const auto sw = ml::an::findSweeps (x, 48000);
    const auto st = ml::an::findContactStop (x, 48000);
    std::printf ("  sweeps found: %zu; contact stop: %s\n", sw.size(), st ? "yes" : "no");
    // the whole starting body
    const auto p = ml::an::initialBody (x, 48000);
    const auto r = readBody (g + "/init_body.txt");
    bool same = p.modes() == r.modes();
    double worst = 0; const char* where = "";
    auto cmp = [&] (const char* name, const std::vector<double>& a, const std::vector<double>& b)
    { for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) { const double d = std::fabs (a[i] - b[i]) / std::max (1e-9, std::fabs (b[i])); if (d > worst) { worst = d; where = name; } } };
    if (same)
    {
        cmp ("f", p.f, r.f); cmp ("a", p.a, r.a); cmp ("rho", p.rho, r.rho); cmp ("delta", p.delta, r.delta); cmp ("beta", p.beta, r.beta);
        cmp ("eps", p.eps, r.eps); cmp ("glide", p.glide, r.glide); cmp ("tau_g", p.tau_g, r.tau_g);
        cmp ("laws", { p.b1, p.b3, p.lb_m, p.lb_rate, p.cx_amt, p.cx_t, p.cx_f }, { r.b1, r.b3, r.lb_m, r.lb_rate, r.cx_amt, r.cx_t, r.cx_f });
        cmp ("dn", std::vector<double> (p.dn.begin(), p.dn.end()), std::vector<double> (r.dn.begin(), r.dn.end()));
        cmp ("dn_rho", std::vector<double> (p.dn_rho.begin(), p.dn_rho.end()), std::vector<double> (r.dn_rho.begin(), r.dn_rho.end()));
    }
    std::printf ("  %d modes here, %d in the learner; worst relative difference %.1e (%s)\n", p.modes(), r.modes(), worst, where);
    CHECK (same && worst < 1e-5, "the starting body is the learner's: every mode and every law (to 1e-5, for the reason above)");
}

// cosine similarity and relative norm difference of two gradients
static void compareGrad (const std::vector<double>& a, const std::vector<double>& b, double& cosv, double& rel)
{
    double ab = 0, aa = 0, bb = 0, dd = 0;
    for (size_t i = 0; i < std::min (a.size(), b.size()); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; dd += (a[i] - b[i]) * (a[i] - b[i]); }
    cosv = ab / std::sqrt (aa * bb + 1e-300); rel = std::sqrt (dd / (bb + 1e-300));
}

static void stage5a (const std::string& g, const char* tag)
{
    const auto x = readRaw<float> (g + "/x.f32");
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    bool has = false; double floor = 0.0;
    if (at != std::string::npos) { const auto v = meta.substr (at + 17); if (v.find ("null") > 2) { has = true; floor = std::atof (v.c_str()); } }
    const ml::Loss2 loss (x.data(), (int) x.size(), 48000.0, has, floor);
    const auto yf = readRaw<float> (g + "/" + tag + "/y.f32");
    const std::vector<double> y (yf.begin(), yf.end());

    // each term against JAX's gradient of the same term in double precision: the derivation, exactly
    const char* names[] = { "spec", "env", "wass", "att" };
    std::vector<double> sum (y.size(), 0.0);
    for (int t = 0; t < 4; ++t)
    {
        std::vector<double> gt;
        const double vt = loss.valueAndGrad (y, gt, 1 << t);
        const auto ref = readRaw<double> (g + "/" + tag + "/gy_" + names[t] + "_x64.f64");
        double ct, rt; compareGrad (gt, ref, ct, rt);
        for (size_t i = 0; i < y.size(); ++i) sum[i] += ref[i];
        std::printf ("    %-4s %.9f  relative difference %.1e\n", names[t], vt, rt);
        CHECK (rt < 1e-6, "%s: %s term's gradient is JAX's (double precision)", tag, names[t]);
    }
    std::vector<double> gy;
    const double v = loss.valueAndGrad (y, gy);
    double c, r; compareGrad (gy, sum, c, r);
    CHECK (r < 1e-6, "%s: the whole loss's gradient is the sum of the terms'", tag);
    // and against the learner as it runs (single precision): close, not equal - its float32 STFT
    // noise flips the sign of |log a - log b| in bins at the noise level
    compareGrad (gy, readRaw<double> (g + "/" + tag + "/gy.f64"), c, r);
    std::printf ("  %s: loss %.6f; against the learner's single-precision gradient: cosine %.6f, relative difference %.1e\n", tag, v, c, r);
    CHECK (c > 0.9995, "%s: points the same way as the learner's own gradient", tag);
}

static std::map<std::string, std::vector<double>> readKeyed (const std::string& path)
{
    std::map<std::string, std::vector<double>> kv;
    std::ifstream f (path); std::string line;
    while (std::getline (f, line))
    {
        std::istringstream is (line); std::string key; size_t n = 0; is >> key >> n;
        std::vector<double> v (n); for (auto& x : v) is >> x;
        kv[key] = v;
    }
    return kv;
}

// a keyed file (u.txt, gu.txt, gu64.txt) as flat Params
static ml::Params readParams (const std::string& path)
{
    auto kv = readKeyed (path);
    ml::Params u ((int) kv["logf"].size());
    for (int key = 0; key < ml::kModeKeys; ++key) for (int m = 0; m < u.k; ++m) u.mode (key)[m] = kv[ml::modeKeyName (key)].at ((size_t) m);
    for (int key = 0; key < ml::kGlobalKeys; ++key) u.global (key) = kv[ml::globalKeyName (key)].at (0);
    for (int key = 0; key < ml::kBandKeys; ++key) for (int b = 0; b < ml::kBands; ++b) u.band (key)[b] = kv[ml::bandKeyName (key)].at ((size_t) b);
    return u;
}

static void stage5b (const std::string& g, const char* tag)
{
    const std::string dir = g + "/" + tag;
    const auto x = readRaw<float> (g + "/x.f32");
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    bool has = false; double floor = 0.0;
    if (at != std::string::npos) { const auto v = meta.substr (at + 17); if (v.find ("null") > 2) { has = true; floor = std::atof (v.c_str()); } }
    const ml::Loss2 loss (x.data(), (int) x.size(), 48000.0, has, floor);
    const auto bands = readRaw<float> (dir + "/bands.f32"), bodyb = readRaw<float> (dir + "/bodyb.f32");
    const auto phases = readRaw<double> (dir + "/phases.f64");
    ml::Scene sc; sc.n = (int) x.size(); sc.bands = bands.data(); sc.body = bodyb.data(); sc.phases = phases.data();
    const auto u = readParams (dir + "/u.txt");
    ml::Params gu;
    const auto t0 = std::chrono::steady_clock::now();
    const double v = ml::objective (u, loss, sc, &gu);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double want = std::ifstream (dir + "/loss64.txt") ? readNumber (dir + "/loss64.txt") : v;
    std::printf ("  %s: objective %.9f here, %.9f in the learner (double); value and gradient in %.2f s\n", tag, v, want, secs);
    CHECK (std::fabs (v - want) < 1e-7 * std::fabs (want), "%s: the objective at u is the learner's", tag);

    auto ref = readParams (dir + "/gu64.txt");
    double worst = 0.0; std::string worstKey;
    auto cmp = [&] (const char* name, const double* a, const double* b, int len)
    {
        double d2 = 0, b2 = 0;
        for (int i = 0; i < len; ++i) { d2 += (a[i] - b[i]) * (a[i] - b[i]); b2 += b[i] * b[i]; }
        const double r = std::sqrt (d2 / (b2 + 1e-300));
        if (b2 > 0 && r > worst) { worst = r; worstKey = name; }
        if (std::getenv ("LEARNER_VERBOSE")) std::printf ("    %-7s rel %.2e  |ref| %.3e\n", name, r, std::sqrt (b2));
    };
    for (int key = 0; key < ml::kModeKeys; ++key) cmp (ml::modeKeyName (key), gu.mode (key), ref.mode (key), u.k);
    for (int key = 0; key < ml::kGlobalKeys; ++key) cmp (ml::globalKeyName (key), &gu.global (key), &ref.global (key), 1);
    for (int key = 0; key < ml::kBandKeys; ++key) cmp (ml::bandKeyName (key), gu.band (key), ref.band (key), ml::kBands);
    std::printf ("    every parameter's gradient against JAX's in double precision: worst relative difference %.1e (%s)\n", worst, worstKey.c_str());
    CHECK (worst < 1e-5, "%s: the gradient with respect to all %zu parameters is JAX's", tag, u.size());
    if (std::ifstream (dir + "/gu.txt"))
    {
        double c, r; compareGrad (gu.v, readParams (dir + "/gu.txt").v, c, r);
        std::printf ("    against the learner's own float32 gradient: cosine %.6f\n", c);
    }
}

static void stage6 (const std::string& g)
{
    const std::string dir = g + "/start";
    const auto xf = readRaw<float> (g + "/x.f32");
    const std::vector<double> x (xf.begin(), xf.end());
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    bool has = false; double floor = 0.0;
    if (at != std::string::npos) { const auto v = meta.substr (at + 17); if (v.find ("null") > 2) { has = true; floor = std::atof (v.c_str()); } }
    const ml::Loss2 loss (xf.data(), (int) xf.size(), 48000.0, has, floor);
    const auto bands = readRaw<float> (dir + "/bands.f32"), bodyb = readRaw<float> (dir + "/bodyb.f32");
    const auto phases = readRaw<double> (dir + "/phases.f64");
    ml::Scene sc; sc.n = (int) xf.size(); sc.bands = bands.data(); sc.body = bodyb.data(); sc.phases = phases.data();

    // the whole pipeline: this port's analysis, starting body and freeze mask, then the fit
    ml::FitOptions opt;
    const auto p0 = ml::an::initialBody (x, 48000, &opt.keepGlide);
    std::vector<double> hist;
    { std::ifstream hf (g + "/fit_history.txt"); double v; while (hf >> v) hist.push_back (v); }
    if (const char* pe = std::getenv ("LEARNER_PERTURB"))
    {
        // a nudge of the starting point at float32's resolution (1e-7), different for each index:
        // how far the outcome moves with it is how path-dependent the fit is
        const int idx = std::atoi (pe);
        opt.adjustStart = [idx] (ml::Params& u)
        {
            std::mt19937 rng ((unsigned) idx);
            for (auto& v : u.v) v += idx == 0 ? 0.0 : 1e-7 * (rng() % 2 ? 1.0 : -1.0);
        };
        opt.progress = nullptr;
    }
    opt.everyStepUntil = std::getenv ("LEARNER_EVERY") ? std::atoi (std::getenv ("LEARNER_EVERY")) : 0;
    opt.steps = std::getenv ("LEARNER_STEPS") ? std::atoi (std::getenv ("LEARNER_STEPS")) : 300;
    if (! opt.adjustStart) opt.progress = [&] (int i, int steps, double v)
    { std::printf ("    step %3d/%d  %.4f   learner %.4f\n", i, steps, v, i - 1 < (int) hist.size() ? hist[(size_t) i - 1] : 0.0); std::fflush (stdout); };
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = ml::fit (xf.data(), loss, sc, p0, opt);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (std::getenv ("LEARNER_DUMP"))
    {
        // the fit's best point, keyed like u.txt, for checking the gradient there against JAX
        if (std::system (("mkdir -p '" + g + "/cppfit'").c_str()) != 0) std::printf ("  (could not make cppfit)\n");
        std::ofstream o (g + "/cppfit/u.txt");
        o << std::setprecision (17);
        auto line = [&] (const char* name, const double* v, int len) { o << name << " " << len; for (int i = 0; i < len; ++i) o << " " << v[i]; o << "\n"; };
        for (int key = 0; key < ml::kModeKeys; ++key) line (ml::modeKeyName (key), r.best.mode (key), r.best.k);
        for (int key = 0; key < ml::kGlobalKeys; ++key) { const double v = r.best.global (key); line (ml::globalKeyName (key), &v, 1); }
        for (int key = 0; key < ml::kBandKeys; ++key) line (ml::bandKeyName (key), r.best.band (key), ml::kBands);
    }
    if (std::getenv ("LEARNER_DUMP")) { std::ofstream o (g + "/cpp_fit_history.txt"); for (double v : r.history) o << std::setprecision (17) << v << " "; }
    const double lStart = hist.front(), lBest = *std::min_element (hist.begin(), hist.end());
    std::printf ("  %d modes, 300 steps in %.0f s: start %.6f (learner %.6f), best %.6f (learner %.6f)\n",
                 p0.modes(), secs, r.startLoss, lStart, r.loss, lBest);
    CHECK (std::fabs (r.startLoss - lStart) < 2e-3 * lStart, "the fit starts where the learner's does (float32 against double: 2e-3)");
    // No pass mark on where one run ends: the learner's fit is path-dependent. Nudging the start by
    // float32's resolution moves the final loss by up to 40%% in the port and in the learner alike
    // (docs/PROPOSED-FEATURES.md, stage 6); stage 6a checks the steps themselves.
    CHECK (r.loss < 0.5 * r.startLoss, "the fit learns (best under half the start)");
}

// stage 6a: the optimiser step for step. Six steps of the learner's fit2.fit in double precision with
// the port's two choices (window and starting point rounded to float32: fit64b.py, AS_PORT=1 STEPS=6)
// against six steps here. The paths agree until roundoff-level gradients on parameters that barely
// matter flip Adam's sign (Adam moves every parameter by about its rate whatever the gradient's
// size, so a gradient of 1e-16 either way is a full step either way). That shows from step 4 or 5;
// the first three (two updates, bias correction and schedule included) are held to 1e-6.
static void stage6a (const std::string& g)
{
    const std::string dir = g + "/start";
    const auto xf = readRaw<float> (g + "/x.f32");
    const std::vector<double> x (xf.begin(), xf.end());
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    bool has = false; double floor = 0.0;
    if (at != std::string::npos) { const auto v = meta.substr (at + 17); if (v.find ("null") > 2) { has = true; floor = std::atof (v.c_str()); } }
    const ml::Loss2 loss (xf.data(), (int) xf.size(), 48000.0, has, floor);
    const auto bands = readRaw<float> (dir + "/bands.f32"), bodyb = readRaw<float> (dir + "/bodyb.f32");
    const auto phases = readRaw<double> (dir + "/phases.f64");
    ml::Scene sc; sc.n = (int) xf.size(); sc.bands = bands.data(); sc.body = bodyb.data(); sc.phases = phases.data();
    ml::FitOptions opt;
    opt.steps = 6;
    const auto r = ml::fit (xf.data(), loss, sc, ml::an::initialBody (x, 48000, &opt.keepGlide), opt);
    std::vector<double> ref;
    { std::ifstream hf (g + "/fit64port6_history.txt"); double v; while (hf >> v) ref.push_back (v); }
    double worst4 = 0.0;
    std::printf ("  step:");
    for (size_t i = 0; i < std::min (r.history.size(), ref.size()); ++i)
    {
        const double d = std::fabs (r.history[i] - ref[i]) / ref[i];
        if (i < 3) worst4 = std::max (worst4, d);
        std::printf ("  %zu %.1e", i + 1, d);
    }
    std::printf ("  (relative difference from the learner)\n");
    CHECK (r.history.size() == 6 && ref.size() == 6 && worst4 < 1e-6, "the first three steps are the learner's (Adam, rates, schedule, regularisation, freeze, level match)");
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
    std::printf ("stage 4a: the analysis, against partials.py\n");
    stage4a (g);
    stage4b (g);
    std::printf ("stage 5a: the loss's gradient, against jax.grad (make_loss)\n");
    if (std::ifstream (g + "/start/gy_spec_x64.f64")) { stage5a (g, "start"); stage5a (g, "fitted"); }
    std::printf ("stage 5b: back through the body to the parameters, against jax.grad (objective)\n");
    for (const char* tag : { "start", "fitted" })
        if (std::ifstream (g + "/" + tag + "/gu64.txt")) stage5b (g, tag);
    if (const char* at = std::getenv ("LEARNER_AT")) stage5b (g, at);      // any other point (tests)
    if (std::ifstream (g + "/fit64port6_history.txt"))
    {
        std::printf ("stage 6a: the optimiser, step for step, against fit2.fit in double precision\n");
        stage6a (g);
    }
    if (std::getenv ("LEARNER_FIT") && std::ifstream (g + "/fit_history.txt"))
    {
        std::printf ("stage 6b: a whole fit, beside fit2.fit from scratch\n");
        stage6 (g);
    }
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
