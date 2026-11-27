// The MaterialLab learner's port, checked stage by stage against the learner itself.
//   test_learner GOLDEN_DIR [CLIP.wav]   (a directory written by the reference scripts: start/, fitted/,
//                                         x.f32; the clip it was made from, for stage 7a)
// Stage 2: the body renders the same samples as engine2.render_physical from the same parameters
// and the same noise. The learner computes in float32 and this port in double, so "the same" is a
// tolerance, stated with each check.
#include "../Source/neobeat/shared/learner/Body2.h"
#include "../Source/neobeat/shared/learner/Loss2.h"
#include "../Source/neobeat/shared/learner/Analysis2.h"
#include "../Source/neobeat/shared/learner/Fit2.h"
#include "../Source/neobeat/shared/learner/FitJob.h"
#include "../Source/neobeat/shared/learner/MaterialFile.h"
#include "../Source/neobeat/shared/learner/Breed.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <thread>

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

// ------------------------------------------------------------------ stage 7a: the fit job

// A WAV file as soundfile reads it (integer PCM scaled by 2^-(bits-1), or float), channels averaged.
static std::vector<float> readWav (const std::string& path, double& sr)
{
    std::ifstream f (path, std::ios::binary);
    std::vector<unsigned char> d ((std::istreambuf_iterator<char> (f)), {});
    auto u16 = [&] (size_t i) { return (unsigned) d[i] | ((unsigned) d[i + 1] << 8); };
    auto u32 = [&] (size_t i) { return u16 (i) | (u16 (i + 2) << 16); };
    std::vector<float> x;
    if (d.size() < 12 || std::string (d.begin(), d.begin() + 4) != "RIFF") return x;
    int fmt = 0, ch = 1, bits = 16;
    for (size_t p = 12; p + 8 <= d.size();)
    {
        const std::string id (d.begin() + (long) p, d.begin() + (long) p + 4);
        const size_t len = u32 (p + 4), at = p + 8;
        if (id == "fmt ") { fmt = (int) u16 (at); ch = (int) u16 (at + 2); sr = u32 (at + 4); bits = (int) u16 (at + 14); if (fmt == 0xFFFE) fmt = (int) u16 (at + 24); }
        if (id == "data")
        {
            const int bytes = bits / 8;
            const size_t frames = std::min (len, d.size() - at) / (size_t) (bytes * ch);
            x.assign (frames, 0.0f);
            for (size_t i = 0; i < frames; ++i)
            {
                double s = 0.0;
                for (int c = 0; c < ch; ++c)
                {
                    const size_t q = at + (i * (size_t) ch + (size_t) c) * (size_t) bytes;
                    if (fmt == 3 && bits == 32) { float v; std::memcpy (&v, &d[q], 4); s += v; continue; }
                    int32_t v = 0;
                    for (int b = 0; b < bytes; ++b) v |= (int32_t) d[q + (size_t) b] << (8 * b + 32 - bits);
                    s += (double) ((float) v / 2147483648.0f);
                }
                x[i] = (float) (s / ch);
            }
        }
        p = at + len + (len & 1);
    }
    return x;
}

// Where the learner cuts the clip, how loud it hears the noise, and a whole job of fits.
static void stage7a (const std::string& g, const std::string& wav)
{
    double sr = 0.0;
    const auto raw = readWav (wav, sr);
    const auto ref = readRaw<float> (g + "/x.f32");                  // the learner's clip, from its cut
    const auto c = ml::prepareClip (raw.data(), (int) raw.size(), sr);
    const int cut = (int) std::lround (c.sourceStart * ml::kFitRate);
    const int learnerCut = (int) raw.size() - (int) ref.size();      // trim_to_strike keeps everything after
    size_t same = 0;
    for (size_t i = 0; i < c.x.size() && i < ref.size(); ++i) same += c.x[i] == ref[i] ? 1 : 0;
    std::printf ("  %.0f Hz, %zu samples: cut at sample %d here, %d in the learner; %zu of %zu samples identical\n",
                 sr, raw.size(), cut, learnerCut, same, ref.size());
    CHECK (cut == learnerCut && same == ref.size() && c.x.size() == ref.size(), "the clip is the one the learner fits, sample for sample (trim_to_strike)");

    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    const auto at = meta.find ("\"noise_floor_db\":");
    const double target = at == std::string::npos ? 0.0 : std::atof (meta.substr (at + 17).c_str());
    std::printf ("  noise floor %.1f dB here, %.1f dB in the training target\n", c.noiseFloor, target);
    // The training set measured its floor on the whole source recording, which a dropped file does
    // not come with; the clip's quietest 5% stands in for it.
    CHECK (std::fabs (c.noiseFloor - target) < 1.5, "the clip's own noise floor stands in for the training set's (within 1.5 dB)");

    // Another rate, inside a long quiet recording: only the stretch around the strike is resampled,
    // so the cost does not grow with the file, and the strike is found where it is.
    {
        auto at44 = ml::resample (c.x.data(), (int) c.x.size(), ml::kFitRate, 44100.0);
        std::vector<float> longer ((size_t) (60 * 44100), 0.0f);
        ml::NumpyRandom rng (7);
        for (auto& v : longer) v = (float) (1e-4 * (rng.uniform() - 0.5));
        const size_t put = (size_t) (20 * 44100);
        for (size_t i = 0; i < at44.size(); ++i) longer[put + i] += at44[i];
        const auto t1 = std::chrono::steady_clock::now();
        const auto c2 = ml::prepareClip (longer.data(), (int) longer.size(), 44100.0);
        const double ms = 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t1).count();
        const double off = (c2.sourceStart - 20.0) * 1000.0;
        double e1 = 0, e2 = 0;
        for (size_t i = 0; i < std::min (c.x.size(), c2.x.size()); ++i) { e1 += (double) c.x[i] * c.x[i]; e2 += (double) c2.x[i] * c2.x[i]; }
        std::printf ("  the clip at 44.1 kHz, 20 s into 60 s of quiet: prepared in %.0f ms, strike found %.2f ms from where it is, level %.2f dB from the 48 kHz clip's\n",
                     ms, off, 10.0 * std::log10 (e2 / e1));
        // within one of trim_to_strike's 1 ms frames: the frames fall differently at another rate
        CHECK (std::fabs (off) < 1.0 && std::fabs (10.0 * std::log10 (e2 / e1)) < 0.1 && ms < 1000.0, "any rate and any length: the strike found (within its 1 ms frame), the level kept, in under a second");
    }

    if (! std::getenv ("LEARNER_JOB")) return;

    // One whole job: two fits on two threads, the first at the learner's own start and the second
    // nudged, reported best first. Progress is polled the way the plugin's message thread will.
    const int steps = std::getenv ("LEARNER_STEPS") ? std::atoi (std::getenv ("LEARNER_STEPS")) : 300;
    const auto t0 = std::chrono::steady_clock::now();
    ml::FitJob job (c, 2, 2, steps);
    double last = -1.0; bool monotone = true; int polls = 0;
    while (! job.finished())
    {
        const double p = job.progress();
        monotone = monotone && p >= last; last = p; ++polls;
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const auto r = job.results();
    std::printf ("  2 fits x %d steps on 2 threads in %.0f s (%d progress readings)\n", steps, secs, polls);
    for (const auto& f : r) std::printf ("    fit %d: %d modes, start %.4f, best %.4f, %.0f s\n", f.index, f.params.modes(), f.startLoss, f.loss, f.seconds);
    CHECK (job.error().empty() && r.size() == 2 && r[0].loss <= r[1].loss && r[0].index != r[1].index,
           "both fits come back, best first");
    CHECK (monotone && job.progress() == 1.0, "progress only climbs, and ends at 1");
    CHECK (r.size() == 2 && r[0].loss < 0.5 * r[0].startLoss && r[1].loss < 0.5 * r[1].startLoss, "each fit learns (best under half its start)");

    // fit 0 is the plain fit from the learner's start: the job adds nothing to it
    if (r.size() == 2)
    {
        const std::vector<double> xd (c.x.begin(), c.x.end());
        ml::FitOptions opt; opt.steps = steps;
        const auto p0 = ml::an::initialBody (xd, (int) ml::kFitRate, &opt.keepGlide);
        const ml::Loss2 loss (c.x.data(), (int) c.x.size(), ml::kFitRate, true, c.noiseFloor);
        const auto bands = ml::noiseBands ((int) c.x.size(), ml::kFitRate, 11), body = ml::noiseBands ((int) c.x.size(), ml::kFitRate, 101);
        const auto ph = ml::strikePhases (p0.modes());
        ml::Scene sc; sc.n = (int) c.x.size(); sc.bands = bands.data(); sc.body = body.data(); sc.phases = ph.data();
        const auto direct = ml::fit (c.x.data(), loss, sc, p0, opt);
        // LEARNER_WRITE=FILE: the best fit as a material file, for the learner to open
        // (tools/learner_ref/open_material.py scores it against the same clip)
        if (const char* w = std::getenv ("LEARNER_WRITE"))
        {
            ml::MaterialInfo info;
            info.name = "test fit 1"; info.source = wav; info.madeBy = "test_learner"; info.of = 2;
            info.loss = r[0].loss; info.startLoss = r[0].startLoss; info.seconds = r[0].seconds;
            info.noiseFloor = c.noiseFloor; info.nSamples = (int) c.x.size();
            std::ofstream (w) << ml::materialJson (r[0].params, info);
        }
        const auto& f0 = r[0].index == 0 ? r[0] : r[1];
        std::printf ("  the same fit run directly: best %.6f; in the job: %.6f\n", direct.loss, f0.loss);
        CHECK (direct.loss == f0.loss, "a fit in the job is the same fit, to the last bit (nothing shared between threads)");

        // what goes into the pool: the best fit struck, as long as it rings
        const double ring = ml::ringSeconds (r[0].params);
        const auto y = ml::renderStrike (r[0].params, std::min (6.0, ring + 0.1));
        double peak = 0.0, e = 0.0, ex = 0.0;
        for (float v : y) peak = std::max (peak, (double) std::fabs (v));
        for (size_t i = 0; i < std::min (y.size(), c.x.size()); ++i) { e += (double) y[i] * y[i]; ex += (double) c.x[i] * c.x[i]; }
        std::printf ("  the best fit struck: rings %.2f s, %zu samples, peak %.3f, level %.1f dB from the clip's\n",
                     ring, y.size(), peak, 10.0 * std::log10 ((e + 1e-30) / (ex + 1e-30)));
        CHECK (peak > 1e-4 && std::fabs (10.0 * std::log10 ((e + 1e-30) / (ex + 1e-30))) < 6.0, "the struck fit sounds, at about the clip's level");
    }

    // Cancel: stops within a step of being asked, and leaves nothing half-done in the results
    {
        ml::FitJob j2 (c, 2, 2, steps);
        std::this_thread::sleep_for (std::chrono::milliseconds (800));
        const auto tc = std::chrono::steady_clock::now();
        j2.cancel();
        while (! j2.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (10));
        const double wait = std::chrono::duration<double> (std::chrono::steady_clock::now() - tc).count();
        std::printf ("  cancelled after 0.8 s: stopped %.2f s later, %zu results\n", wait, j2.results().size());
        CHECK (j2.wasCancelled() && j2.results().empty() && wait < 2.0, "Cancel stops the job promptly and keeps no partial fits");
    }

    // a silent clip is refused with a sentence, not a crash
    {
        std::vector<float> quiet (48000, 0.0f);
        ml::FitJob j3 (ml::prepareClip (quiet.data(), (int) quiet.size(), 48000.0), 2, 2, steps);
        while (! j3.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (10));
        std::printf ("  a silent clip: \"%s\"\n", j3.error().c_str());
        CHECK (! j3.error().empty() && j3.results().empty(), "a silent clip is refused with a reason");
    }
}

// ------------------------------------------------------------------ stage 7b: breeding

// the strongest frequency in a stretch of sound (Hann, zero-padded FFT, parabolic peak)
static double peakHz (const std::vector<float>& x, int from, int len, double fs)
{
    int N = 1; while (N < 4 * len) N <<= 1;
    std::vector<std::complex<double>> b ((size_t) N);
    for (int i = 0; i < len && from + i < (int) x.size(); ++i) b[(size_t) i] = x[(size_t) (from + i)] * (0.5 - 0.5 * std::cos (2.0 * ml::an::kPi * i / len));
    ml::fftPlan (N).run (b.data());
    int k = 1; for (int i = 1; i < N / 2; ++i) if (std::abs (b[(size_t) i]) > std::abs (b[(size_t) k])) k = i;
    const double a = std::abs (b[(size_t) k - 1]), c = std::abs (b[(size_t) k]), d = std::abs (b[(size_t) k + 1]);
    return (k + 0.5 * (a - d) / (a - 2 * c + d)) * fs / N;
}

static void stage7b (const std::string& g, const std::string& other)
{
    using namespace ml;
    const Body pvc = readBody (g + "/fitted/body.txt"), can = readBody (other + "/fitted/body.txt");
    const auto xf = readRaw<float> (g + "/x.f32");
    std::ifstream mf (g + "/meta.json"); std::string meta ((std::istreambuf_iterator<char> (mf)), {});
    Clip target; target.x = xf; target.noiseFloor = std::atof (meta.substr (meta.find ("\"noise_floor_db\":") + 17).c_str());

    // the larger moves do what they say, on the numbers themselves
    {
        Params u = unconstrained (pvc), t = u;
        applyMove (t, Move::Transpose, 5.0 / 12.0);
        const Body b = physical (t);
        double worst = 0; for (int m = 0; m < pvc.modes(); ++m) worst = std::max (worst, std::fabs (b.f[(size_t) m] / pvc.f[(size_t) m] / std::pow (2.0, 5.0 / 12.0) - 1.0));
        Params s2 = u; applyMove (s2, Move::Stretch, 0.4);            // spread +10%
        const Body bs = physical (s2);
        double lo = 1e300; int top = 0; for (int m = 0; m < pvc.modes(); ++m) { lo = std::min (lo, pvc.f[(size_t) m]); if (pvc.f[(size_t) m] > pvc.f[(size_t) top]) top = m; }
        const double spread = std::log (bs.f[(size_t) top] / lo) / std::log (pvc.f[(size_t) top] / lo);
        Params r2 = u; applyMove (r2, Move::Ring, 0.5);
        CHECK (worst < 1e-12 && std::fabs (spread - 1.1) < 1e-9 && physical (r2).b1 < pvc.b1,
               "transpose +5 st moves every mode by 2^(5/12) (to %.0e), a 10%% spread is 10%% (%.6f), rings longer lowers the damping", worst, spread);
    }

    // REACH: how far children go. 24 body children of the PVC pipe at three settings, each measured
    // against the parent's own strike with the learner's loss
    {
        Clip parentClip; parentClip.x = renderStrike (pvc, 0.78, kShowSeed + 7);
        parentClip.noiseFloor = noiseFloorDb (parentClip.x.data(), (int) parentClip.x.size(), kFitRate);
        const Scorer sp (parentClip);
        double means[3] = {}; int playable = 0, made = 0;
        const double reaches[3] = { 0.1, 0.5, 1.0 };
        std::vector<const Source*> bodies; Source ps; ps.isBody = true; ps.body = pvc; ps.name = "pvc"; bodies.push_back (&ps);
        for (int k = 0; k < 3; ++k)
        {
            for (int i = 0; i < 24; ++i)
            {
                Child c; ++made;
                if (! makeChild (ChildKind::Body, bodies, {}, reaches[k], childSeed (7, k, i), c)) continue;
                ++playable;
                means[k] += sp.body (c.body, 1) / 24.0;
            }
        }
        const double self = sp.body (pvc, 1);
        std::printf ("  body children of this clip's material, distance from it (the parent itself: %.3f): REACH 0.1 %.3f, 0.5 %.3f, 1.0 %.3f; %d of %d playable\n",
                     self, means[0], means[1], means[2], playable, made);
        CHECK (playable == made && means[0] < means[1] && means[1] < means[2], "every body child plays, and the further the REACH the further they go");
    }

    // a cross takes the modes below its split from one parent and above it from the other
    {
        BreedRandom r (3); std::string how; double split = 0;
        const Body c = crossBodies (pvc, can, 0.5, r, how, &split);
        // (to 1e-12: a frequency goes through log and exp on its way through the unconstrained numbers)
        auto has = [] (const Body& p, double f) { for (double q : p.f) if (std::fabs (q / f - 1.0) < 1e-12) return true; return false; };
        bool right = c.modes() > 0;
        int below = 0;
        for (double f : c.f) { right = right && (f < split ? has (pvc, f) : has (can, f)); below += f < split ? 1 : 0; }
        std::printf ("  this clip's material x the other's, %s: %d modes below from the one, %d above from the other\n", how.c_str(), below, c.modes() - below);
        CHECK (right && below > 0 && below < c.modes(), "a cross: every mode below the split from one parent, every one above from the other");
    }

    // the time stretch keeps the pitch
    {
        std::vector<float> tone (48000);
        for (int i = 0; i < 48000; ++i) { const double t = i / 48000.0; tone[(size_t) i] = (float) (std::exp (-2.0 * t) * (0.6 * std::sin (2 * ml::an::kPi * 440 * t) + 0.3 * std::sin (2 * ml::an::kPi * 1320 * t))); }
        bool ok = true; std::string say;
        for (double f : { 2.0, 0.5 })
        {
            const auto y = stretchKeepPitch (tone, f);
            const double hz = peakHz (y, (int) (0.1 * y.size()), 8192, 48000.0);
            const double lvl = 20.0 * std::log10 (rmsOf (y.data(), (int) y.size() / 4) / rmsOf (tone.data(), (int) tone.size() / 4));
            char b[120]; std::snprintf (b, sizeof b, " %.1fx: %.3f s long, strongest %.1f Hz, level %+.1f dB;", f, y.size() / 48000.0, hz, lvl); say += b;
            ok = ok && std::fabs (y.size() / 48000.0 - f) < 0.01 * f && std::fabs (hz - 440.0) < 2.0 && std::fabs (lvl) < 1.5;
        }
        std::printf ("  a 440 Hz tone stretched%s\n", say.c_str());
        CHECK (ok, "stretched to twice and half its length, a tone keeps its pitch and its level");
    }

    // sound through a material rings at the material's modes
    {
        std::vector<float> burst (4800, 0.0f); NumpyRandom r (5);
        for (int i = 0; i < 480; ++i) burst[(size_t) i] = (float) (r.uniform() - 0.5);
        const auto y = throughBody (burst, pvc);
        // a 1 s window: the low pairs of a long body (the tube's) sit about 3 Hz apart
        const double hz = peakHz (y, 4800, std::min (48000, (int) y.size() - 4800), kFitRate);
        // each mode rings as its pair, half its beating either side of its frequency
        double nearest = 1e9;
        for (int m = 0; m < pvc.modes(); ++m) for (double side : { -0.5, 0.5 }) nearest = std::min (nearest, std::fabs ((pvc.f[(size_t) m] + side * pvc.delta[(size_t) m]) / hz - 1.0));
        std::printf ("  a noise burst through this clip's material rings at %.1f Hz, %.2f%% from one of its modes' pairs\n", hz, 100.0 * nearest);
        CHECK (nearest < 0.01 || nearest * hz < 1.0, "Through: a burst rings at the material's own modes (within 1%% or 1 Hz)");
    }

    // a Wander generation: from the pipe and the clip, eight children, closest first; the same children
    // on one thread or two
    {
        BreedRequest q; q.mode = BreedRequest::Wander; q.reach = 0.4; q.seed = 11; q.hasTarget = true; q.target = target;
        Source a; a.name = "pvc"; a.isBody = true; a.body = pvc; strikeOf (pvc, 3, a.sound);
        Source b; b.name = "clip"; b.sound = target.x;
        q.parents = { a, b };
        std::vector<Child> runs[2]; double secs[2] = {};
        for (int t = 0; t < 2; ++t)
        {
            q.threads = t + 1;
            const auto t0 = std::chrono::steady_clock::now();
            BreedJob job (q);
            while (! job.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (10));
            secs[t] = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            runs[t] = job.results();
        }
        bool sorted = true, same = runs[0].size() == runs[1].size(), finite = true;
        int kinds[5] = {};
        for (size_t i = 0; i < runs[0].size(); ++i)
        {
            const auto& c = runs[0][i];
            if (i > 0) sorted = sorted && c.loss >= runs[0][i - 1].loss;
            finite = finite && std::isfinite (c.loss) && ! c.sound.empty();
            ++kinds[(int) c.kind];
            if (same) same = c.loss == runs[1][i].loss && c.sound == runs[1][i].sound;
            std::printf ("    %-7s %.3f  %s\n", kindName (c.kind), c.loss, c.how.c_str());
        }
        std::printf ("  one generation: %zu children in %.1f s on one thread, %.1f s on two (body %d, file %d, through %d)\n",
                     runs[0].size(), secs[0], secs[1], kinds[0], kinds[2], kinds[3]);
        CHECK (runs[0].size() == 8 && kinds[0] == 5 && kinds[2] == 2 && kinds[3] == 1 && finite && sorted,
               "Wander from a material and a sound: five body children, two file, one through, closest first");
        CHECK (same, "the same children on one thread or two, to the last sample");
        // two materials kept: half the generation are crosses
        Source c2; c2.name = "can"; c2.isBody = true; c2.body = can; strikeOf (can, 4, c2.sound);
        q.parents = { a, c2, b }; q.threads = 2;
        BreedJob j2 (q);
        while (! j2.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (10));
        int crosses = 0; for (const auto& c : j2.results()) crosses += c.kind == ChildKind::Cross ? 1 : 0;
        CHECK (j2.results().size() == 8 && crosses == 4, "two materials kept: four of the eight are crosses (%d)", crosses);
    }

    // From scratch: every shape plays at every WILD, keeps its own partials when not wild, and a Scratch
    // generation is one of each shape, closest first, the same on any number of threads
    {
        int playable = 0, made = 0;
        for (int sh = 0; sh < (int) Shape::Count; ++sh)
            for (double wild : { 0.0, 0.5, 1.0 })
                for (int k = 0; k < 4; ++k)
                {
                    BreedRandom r (childSeed (31, sh * 10 + (int) (wild * 4), k)); std::string how;
                    const Body b = scratchBody (r, wild, how, sh);
                    std::vector<float> y; ++made;
                    playable += strikeOf (b, 9, y) && b.modes() > 0 ? 1 : 0;
                }
        auto second = [] (int sh) { BreedRandom r (childSeed (5, 0, sh)); std::string how; const Body b = scratchBody (r, 0.0, how, sh); return b.f[1] / b.f[0]; };
        const double bar = second ((int) Shape::Bar), skin = second ((int) Shape::Membrane), closed = second ((int) Shape::ClosedTube);
        std::printf ("  from scratch: %d of %d playable (8 shapes x WILD 0, 0.5, 1 x 4); second partial over the first, not wild: bar %.4f, drum skin %.4f, closed tube %.4f\n",
                     playable, made, bar, skin, closed);
        CHECK (playable == made && std::fabs (bar - 2.7565) < 1e-3 && std::fabs (skin - 1.5933) < 1e-3 && std::fabs (closed - 3.0) < 1e-9,
               "every shape plays at every WILD, and keeps its own partials when not wild (beam, Bessel, odd harmonics)");
        BreedRequest q; q.mode = BreedRequest::Scratch; q.reach = 0.5; q.seed = 21; q.hasTarget = true; q.target = target;
        std::vector<Child> runs[2];
        for (int t = 0; t < 2; ++t) { q.threads = t + 1; BreedJob job (q); while (! job.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (10)); runs[t] = job.results(); }
        std::vector<int> shapes ((size_t) Shape::Count, 0);
        bool sorted = true, same = runs[0].size() == runs[1].size();
        for (size_t i = 0; i < runs[0].size(); ++i)
        {
            for (int sh = 0; sh < (int) Shape::Count; ++sh) if (runs[0][i].how.rfind (shapeName ((Shape) sh), 0) == 0) ++shapes[(size_t) sh];
            if (i > 0) sorted = sorted && runs[0][i].loss >= runs[0][i - 1].loss;
            if (same) same = runs[0][i].loss == runs[1][i].loss && runs[0][i].sound == runs[1][i].sound;
        }
        bool each = runs[0].size() == 8; for (int n : shapes) each = each && n == 1;
        CHECK (each && sorted && same, "a Scratch generation: one of each shape, closest first, the same on one thread or two");
    }

    // Grow: generations on their own toward the clip, from somewhere else - the tin can - as Petri's
    // takes climb toward a pinned sound (from the PVC pipe's own fit there is little left to climb)
    {
        BreedRequest q; q.mode = BreedRequest::Grow; q.reach = 0.4; q.seed = 5; q.hasTarget = true; q.target = target;
        q.generations = std::getenv ("LEARNER_GROW_GENS") ? std::atoi (std::getenv ("LEARNER_GROW_GENS")) : 12;
        q.polishSteps = std::getenv ("LEARNER_GROW_POLISH") ? std::atoi (std::getenv ("LEARNER_GROW_POLISH")) : 5;
        q.threads = 2;
        Source a; a.name = std::getenv ("LEARNER_GROW_FROM_SELF") ? "clip's own material" : "other clip's material"; a.isBody = true; a.body = std::getenv ("LEARNER_GROW_FROM_SELF") ? pvc : can;
        q.parents = { a };
        const Scorer sc (target);
        const double parentShown = sc.body (a.body, 1);
        const auto t0 = std::chrono::steady_clock::now();
        BreedJob job (q);
        double last = 0.0; bool climbs = true;
        while (! job.finished()) { const double p = job.progress(); climbs = climbs && p >= last; last = p; std::this_thread::sleep_for (std::chrono::milliseconds (20)); }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const auto h = job.history();
        const auto r = job.results();
        bool down = ! h.empty(); for (size_t i = 1; i < h.size(); ++i) down = down && h[i] <= h[i - 1];
        std::printf ("  Grow from the %s, %d generations (%d fit steps each child) on 2 threads in %.0f s: closest on the choosing draw %.3f to %.3f; on the showing draw the parent %.3f, the closest grown %.3f\n",
                     a.name.c_str(), q.generations, q.polishSteps, secs, h.empty() ? 0.0 : h.front(), h.empty() ? 0.0 : h.back(), parentShown, r.empty() ? 0.0 : r.front().loss);
        if (std::getenv ("LEARNER_VERBOSE")) { std::printf ("   "); for (double v : h) std::printf (" %.3f", v); std::printf ("\n"); for (const auto& c : r) std::printf ("    %.3f %s\n", c.loss, c.how.c_str()); }
        CHECK (down && climbs && r.size() == 8, "Grow never loses its closest, the progress only climbs, and it shows the eight closest it made");
        CHECK (! r.empty() && r.front().loss < parentShown, "what Grow shows is closer than the material it grew from, on a draw it never chose by");
        // Cancel
        q.generations = 200;
        BreedJob j2 (q);
        std::this_thread::sleep_for (std::chrono::milliseconds (500));
        const auto tc = std::chrono::steady_clock::now();
        j2.cancel();
        while (! j2.finished()) std::this_thread::sleep_for (std::chrono::milliseconds (5));
        const double wait = std::chrono::duration<double> (std::chrono::steady_clock::now() - tc).count();
        CHECK (j2.wasCancelled() && j2.results().empty() && wait < 1.0, "Cancel stops Grow in %.2f s and keeps nothing", wait);
    }
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
    if (argc > 2)
    {
        std::printf ("stage 7a: the fit job (FitJob.h), from the clip's own file\n");
        stage7a (g, argv[2]);
    }
    if (const char* other = std::getenv ("LEARNER_BREED"))       // another clip's reference directory, for crosses
    {
        std::printf ("stage 7b: breeding (Breed.h)\n");
        stage7b (g, other);
    }
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
