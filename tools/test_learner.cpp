// The MaterialLab learner's port, checked stage by stage against the learner itself.
//   test_learner GOLDEN_DIR      (a directory written by the reference scripts: start/, fitted/, x.f32)
// Stage 2: the body renders the same samples as engine2.render_physical from the same parameters
// and the same noise. The learner computes in float32 and this port in double, so "the same" is a
// tolerance, stated with each check.
#include "../Source/neobeat/shared/learner/Body2.h"
#include "../Source/neobeat/shared/learner/Loss2.h"
#include "../Source/neobeat/shared/learner/Analysis2.h"
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
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
