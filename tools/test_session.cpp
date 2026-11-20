// BatchCut session tests: files, parts of long recordings, the user's edits, FIND MORE across files, and
// the batch export, against an in-memory "disk".
#include "neobeat/shared/rooms/Session.h"
#include <chrono>
#include <cstdio>
#include <random>
#include "Portable.h"
#include <set>

using namespace bc;
static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

struct Disk
{
    std::map<std::string, Audio> files;
    std::map<std::string, std::pair<std::vector<float>, std::vector<float>>> written;
    std::map<std::string, double> rates;   // of what was written
    std::set<std::string> dirs;
    std::mutex m;
};

static void clank (std::vector<float>& x, size_t at, float amp, double f0, double sr, PortableRandom& rng)
{
    const double ratios[] = { 1.0, 1.61, 2.32, 3.71 };
    for (size_t i = 0; i < (size_t) (sr * 0.4) && at + i < x.size(); ++i)
    {
        const double t = (double) i / sr; double v = 0;
        for (int k = 0; k < 4; ++k) v += std::exp (-14.0 * (1 + 0.6 * k) * t) * std::sin (6.283185307 * f0 * ratios[k] * t + k) / (1 + 0.5 * k);
        v += 0.5 * rng.gauss() * std::exp (-300.0 * t);
        x[at + i] += (float) (amp * std::min (1.0, t * 4000.0) * v * 0.4);
    }
}
static void thud (std::vector<float>& x, size_t at, float amp, double sr)
{
    double ph = 0;
    for (size_t i = 0; i < (size_t) (sr * 0.3) && at + i < x.size(); ++i) { const double t = (double) i / sr; ph += 6.283185307 * (60 + 90 * std::exp (-t * 30)) / sr; x[at + i] += (float) (amp * std::exp (-t * 12) * std::sin (ph) * std::min (1.0, t * 3000)); }
}

static void pump (Session& s, int ms = 120000)
{
    // wait until the worker is idle and apply its results (what the app's message thread does)
    for (int i = 0; i < ms / 10; ++i)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
        s.applyResults();
        if (! s.status().busy && i > 3) { std::this_thread::sleep_for (std::chrono::milliseconds (30)); s.applyResults(); if (! s.status().busy) return; }
    }
}

int main()
{
    Disk disk;
    PortableRandom rng (7);
    // file 1: 17 minutes at 22.05 kHz (three parts), a clank every 2.3 s, some right on the seams
    std::vector<double> truth1;
    {
        const double sr = 22050; Audio a; a.sr = sr; a.l.assign ((size_t) (sr * 17 * 60), 0.0f);
        for (double t = 1.0; t < 17 * 60 - 1; t += 2.3) truth1.push_back (t);
        const double seam1 = (double) partStart (FileInfo { "", "", sr, (int64_t) a.l.size(), 1 }, 1) / sr;
        for (double t : { seam1 + 0.1, seam1 + 0.95, seam1 + 1.05, seam1 + 1.9 }) truth1.push_back (t);   // in the overlap, both sides of its middle
        std::sort (truth1.begin(), truth1.end());
        for (double t : truth1) clank (a.l, (size_t) (t * sr), 0.4f, 700.0, sr, rng);
        for (auto& v : a.l) v += 0.01f * rng.gauss();
        disk.files["/rec/long take.wav"] = a;
    }
    // file 2: clanks and thuds, 20 s at 48 kHz stereo
    int clanks2 = 0, thuds2 = 0;
    {
        const double sr = 48000; Audio a; a.sr = sr; a.l.assign ((size_t) (sr * 20), 0.0f);
        for (int i = 0; i < 16; ++i)
        {
            const size_t at = (size_t) ((0.5 + i * 1.2) * sr);
            if (i % 3 == 0) { thud (a.l, at, 0.6f, sr); ++thuds2; } else { clank (a.l, at, 0.4f, 700.0, sr, rng); ++clanks2; }
        }
        for (auto& v : a.l) v += 0.003f * rng.gauss();
        a.r = a.l;
        disk.files["/rec/kitchen.wav"] = a;
    }
    // file 3: the same kinds of sound at 44.1 kHz, for FIND MORE across rates
    int clanks3 = 0;
    {
        const double sr = 44100; Audio a; a.sr = sr; a.l.assign ((size_t) (sr * 12), 0.0f);
        for (int i = 0; i < 10; ++i)
        {
            const size_t at = (size_t) ((0.4 + i * 1.1) * sr);
            if (i % 2 == 0) { clank (a.l, at, 0.3f, 700.0, sr, rng); ++clanks3; } else thud (a.l, at, 0.5f, sr);
        }
        for (auto& v : a.l) v += 0.004f * rng.gauss();
        disk.files["/rec/yard.wav"] = a;
    }

    Session s;
    s.prober = [&] (const std::string& p, FileInfo& info, std::string& err)
    {
        std::lock_guard<std::mutex> lock (disk.m);
        auto it = disk.files.find (p);
        if (it == disk.files.end()) { err = "no such file"; return false; }
        info.sr = it->second.sr; info.length = it->second.size(); info.channels = it->second.stereo() ? 2 : 1;
        return true;
    };
    s.loader = [&] (const std::string& p, int64_t start, int64_t len, Audio& out, std::string& err)
    {
        std::lock_guard<std::mutex> lock (disk.m);
        auto it = disk.files.find (p);
        if (it == disk.files.end())
        {
            // an exported one-shot (read back when the loudness is matched)
            auto w = disk.written.find (p);
            if (w == disk.written.end()) { err = "no such file"; return false; }
            const auto& lr = w->second; const int64_t n = (int64_t) lr.first.size();
            out.sr = disk.rates[p];
            out.l.assign (lr.first.begin() + std::min (n, start), lr.first.begin() + std::min (n, start + len));
            out.r.assign (lr.second.begin() + std::min (n, start), lr.second.begin() + std::min (n, start + len));
            return true;
        }
        const auto& a = it->second;
        out.sr = a.sr;
        out.l.assign (a.l.begin() + start, a.l.begin() + std::min (a.size(), start + len));
        if (a.stereo()) out.r.assign (a.r.begin() + start, a.r.begin() + std::min (a.size(), start + len)); else out.r.clear();
        return true;
    };
    s.writer = [&] (const std::string& p, const std::vector<float>& l, const std::vector<float>& r, double sr, int, std::string&)
    {
        std::lock_guard<std::mutex> lock (disk.m);
        disk.written[p] = { l, r }; disk.rates[p] = sr;
        return true;
    };
    s.makeDir = [&] (const std::string& d) { std::lock_guard<std::mutex> lock (disk.m); disk.dirs.insert (d); return true; };
    s.start();

    std::printf ("1. files\n");
    s.addFiles ({ "/rec/long take.wav", "/rec/kitchen.wav", "/rec/missing.wav", "/rec/kitchen.wav", "/rec/yard.wav" });
    pump (s);
    CHECK (s.files.size() == 4, "four files listed (a duplicate is skipped): %zu", s.files.size());
    CHECK (s.files[0].info.sr == 22050 && partCount (s.files[0].info) == 3, "the 17-minute take is opened in 3 parts (%d)", partCount (s.files[0].info));
    CHECK (! s.files[2].error.empty(), "a missing file shows its error (\"%s\")", s.files[2].error.c_str());

    std::printf ("2. opening, and the parts of a long take\n");
    int total1 = 0;
    std::vector<double> found1;
    for (int p = 0; p < 3; ++p)
    {
        const auto t0 = std::chrono::steady_clock::now();
        s.open (0, p); pump (s);
        auto d = s.doc();
        if (! d) { CHECK (false, "part %d opened", p + 1); continue; }
        const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("     part %d: %zu events, %.1f s from the start of the file, read and analysed in %.2f s\n", p + 1, d->events.size(), (double) d->offset / 22050.0, took);
        total1 += (int) d->events.size();
        for (const auto& e : d->events) found1.push_back ((double) (d->offset + e.onset) / 22050.0);
    }
    std::sort (found1.begin(), found1.end());
    int dup = 0; for (size_t i = 1; i < found1.size(); ++i) dup += found1[i] - found1[i - 1] < 0.05 ? 1 : 0;
    int matched = 0; for (double t : truth1) { bool hit = false; for (double f : found1) hit = hit || std::fabs (f - t) < 0.01; matched += hit ? 1 : 0; }
    CHECK (dup == 0, "nothing found twice at the seams (%d)", dup);
    CHECK (matched == (int) truth1.size() && total1 == (int) truth1.size(), "every clank in the take found once: %d of %zu (%d events)", matched, truth1.size(), total1);

    std::printf ("3. your changes, kept per part, and detecting again\n");
    s.open (1, 0); pump (s);
    auto d = s.doc();
    CHECK (d && d->file == 1 && (int) d->events.size() == clanks2 + thuds2, "kitchen: %zu events (%d planted)", d ? d->events.size() : 0, clanks2 + thuds2);
    CHECK (d && d->groups.size() == 2, "two groups, clanks and thuds (%zu)", d ? d->groups.size() : 0);
    if (d && d->events.size() > 3)
    {
        auto& ed = s.edits (1, 0);
        ed.deleted.push_back (d->events[2].onset);
        ed.off.push_back (d->events[3].onset);
        s.redetect(); pump (s);
        auto d2 = s.doc();
        int offCount = 0; for (const auto& e : d2->events) offCount += e.enabled ? 0 : 1;
        CHECK (d2->events.size() == d->events.size() - 1 && offCount == 1, "a deleted event stays gone, a switched-off one stays off (%zu, %d off)", d2->events.size(), offCount);
        // the edits survive opening another file and coming back
        s.open (0, 0); pump (s); s.open (1, 0); pump (s);
        CHECK (s.doc()->events.size() == d2->events.size(), "and they survive opening another file and coming back");
        ed.deleted.clear(); ed.off.clear(); s.redetect(); pump (s);
    }

    std::printf ("4. export: everything, into a folder per file\n");
    s.settings.outDir = "/out";
    s.exportFiles (true); pump (s, 300000);
    auto st = s.status();
    std::printf ("     %d written, %d failed (%s)\n", st.exported, st.failed, st.lastError.c_str());
    int inLong = 0, inKitchen = 0; bool namesOk = true;
    for (const auto& w : disk.written)
    {
        if (w.first.rfind ("/out/long take/long take_", 0) == 0) ++inLong;
        else if (w.first.rfind ("/out/kitchen/kitchen_", 0) == 0) ++inKitchen;
        else if (w.first.rfind ("/out/yard/yard_", 0) != 0) namesOk = false;
    }
    CHECK (inLong == (int) truth1.size() && inKitchen == clanks2 + thuds2, "one file per event, in each file's own folder (%d and %d)", inLong, inKitchen);
    CHECK (namesOk && disk.written.count ("/out/kitchen/kitchen_A001.wav") && disk.written.count ("/out/long take/long take_p2_A001.wav"), "named by group and number, with the part for long takes");
    CHECK (st.failed == 1, "the missing file is reported, the others are written (%d failed)", st.failed);
    bool short1 = true; for (const auto& w : disk.written) short1 = short1 && w.second.first.size() > 100 && w.second.first.size() < (size_t) (4.2 * 48000);
    CHECK (short1, "each one-shot is a plausible length");

    std::printf ("5. FIND MORE in every file: like a clank from the kitchen\n");
    disk.written.clear();
    {
        auto dk = s.doc();
        int clankIdx = -1;
        for (int i = 0; i < (int) dk->events.size() && clankIdx < 0; ++i)
        {
            const double t = (double) dk->events[(size_t) i].onset / 48000.0;
            const int n = (int) std::lround ((t - 0.5) / 1.2);
            if (n % 3 != 0) clankIdx = i;
        }
        s.settings.like = makeTemplate (*dk->an, dk->events[(size_t) clankIdx]);
        s.settings.likeName = "kitchen clank";
        s.settings.pick.mode = Pick::Like;
        s.exportFiles (true); pump (s, 300000);
        int k = 0, lt = 0, yd = 0;
        for (const auto& w : disk.written) { if (w.first.rfind ("/out/kitchen/kitchen_like_", 0) == 0) ++k; if (w.first.rfind ("/out/long take/", 0) == 0) ++lt; if (w.first.rfind ("/out/yard/yard_like_", 0) == 0) ++yd; }
        CHECK (k == clanks2, "in the kitchen: the %d clanks and no thuds (%d)", clanks2, k);
        CHECK (yd == clanks3, "in the yard (another file, 44.1 kHz): its %d clanks and no thuds (%d)", clanks3, yd);
        std::printf ("     in the 22 kHz long take (no sound above 11 kHz, so its clanks differ more): %d of %zu\n", lt, truth1.size());
        CHECK (lt >= (int) truth1.size() * 9 / 10, "most of the long take's clanks too (%d of %zu)", lt, truth1.size());
    }

    std::printf ("6. layers on their own, and BEST of each group\n");
    disk.written.clear();
    s.settings.pick.mode = Pick::Best; s.settings.pick.bestN = 2; s.settings.exp.separate = true;
    s.edits (1, 0).layers.layers[Background].mute = true;
    s.exportFiles (false); pump (s, 300000);
    {
        int n = 0, bg = 0, tone = 0; for (const auto& w : disk.written) { ++n; bg += w.first.find ("_background") != std::string::npos ? 1 : 0; tone += w.first.find ("_tone") != std::string::npos ? 1 : 0; }
        CHECK (n == 2 * 2 * 3 && bg == 0 && tone == 4, "2 best of 2 groups, one file per audible layer, the muted background left out (%d files, %d tone)", n, tone);
    }

    std::printf ("6b. one loudness for every one-shot, across files and rates\n");
    {
        auto levels = [&] (float& lo, float& hi, float& pk, int& n)
        {
            lo = 1e9f; hi = -1e9f; pk = -1e9f; n = 0;
            std::lock_guard<std::mutex> lock (disk.m);
            for (const auto& w : disk.written) { const Level lv = measureLevel (w.second.first, w.second.second, disk.rates[w.first]); lo = std::min (lo, lv.lufs); hi = std::max (hi, lv.lufs); pk = std::max (pk, lv.peakDb); ++n; }
        };
        float lo, hi, pk; int n;
        s.settings.exp.separate = false; s.settings.pick.mode = Pick::All;
        s.edits (1, 0).layers.layers[Background].mute = false;
        // a target too loud for some (they would clip): all come down to the quietest that fits
        disk.written.clear();
        s.settings.exp.normMode = 2; s.settings.exp.loudDb = -6.0f; s.settings.exp.normDb = -1.0f; s.settings.exp.matchAll = true;
        s.exportFiles (true); pump (s, 600000);
        levels (lo, hi, pk, n);
        std::printf ("     %d one-shots: %.2f .. %.2f LUFS, highest peak %.2f dBFS\n     \"%s\"\n", n, lo, hi, pk, s.status().note.c_str());
        CHECK (n > 400 && hi - lo < 0.1f, "too loud a target: all %d matched, within %.2f dB", n, hi - lo);
        CHECK (pk <= -0.99f, "and none passes the -1 dBFS ceiling (%.2f)", pk);
        CHECK (s.status().note.find ("set the level") != std::string::npos, "the export says what set the level");
        // a target they can all reach
        disk.written.clear();
        s.settings.exp.loudDb = -30.0f;
        s.exportFiles (true); pump (s, 600000);
        levels (lo, hi, pk, n);
        CHECK (n > 400 && std::fabs (lo + 30.0f) < 0.1f && std::fabs (hi + 30.0f) < 0.1f, "a target they can all reach: all %d at -30 LUFS (%.2f .. %.2f)", n, lo, hi);
        // separate layers: each one-shot's layer files share one gain, so the layers keep their balance
        disk.written.clear();
        s.settings.exp.loudDb = -6.0f; s.settings.exp.separate = true; s.settings.pick.mode = Pick::Best; s.settings.pick.bestN = 1;
        s.exportFiles (false); pump (s, 300000);
        levels (lo, hi, pk, n);
        CHECK (n > 0 && pk <= -0.99f, "separate layers: %d files, no layer passes the ceiling (%.2f)", n, pk);
        s.settings.exp.normMode = 0; s.settings.exp.separate = false; s.settings.pick.mode = Pick::All;
    }

    std::printf ("6b. gathering for the kit room\n");
    {
        s.settings.exp.normMode = 0; s.settings.exp.separate = false; s.settings.pick.mode = Pick::All;
        disk.written.clear();
        s.exportFiles (false); pump (s, 300000);
        const auto exported = disk.written;
        disk.written.clear();
        s.gather (false); pump (s, 300000);
        const auto clips = s.takeGathered();
        CHECK (! exported.empty() && clips.size() == exported.size() && disk.written.empty(), "the open file's one-shots as sound: %zu, as many as EXPORT writes (%zu), and nothing written", clips.size(), exported.size());
        int same = 0;
        for (const auto& c : clips) for (const auto& w : exported) if (w.first.find (c.name + ".wav") != std::string::npos && w.second.first.size() == c.l.size()) { ++same; break; }
        CHECK (same == (int) clips.size(), "each one is the cut EXPORT makes, under the same name (%d of %zu)", same, clips.size());
        CHECK (s.takeGathered().empty() && s.status().note.find ("gathered") != std::string::npos, "taken once; the status says so");
    }

    std::printf ("7. busy and cancelled\n");
    s.settings.exp.separate = false; s.settings.pick.mode = Pick::All;
    s.exportFiles (true);
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    s.cancelTask(); pump (s);
    CHECK (s.status().lastError == "export stopped" || ! s.status().busy, "an export can be stopped");
    s.removeFile (1);
    CHECK (s.files.size() == 3 && (! s.doc() || s.doc()->file != 1), "removing the open file closes it");

    std::printf ("8. Clear all (2.1)\n");
    {
        std::vector<std::string> paths; for (const auto& f : s.files) paths.push_back (f.info.path);
        s.open (0, 0);
        s.clearFiles();                       // with an open still on its way
        pump (s);
        CHECK (s.files.empty() && ! s.doc(), "every file off the list, and nothing opens afterwards");
        s.addFiles (paths); pump (s);
        bool read = s.files.size() == paths.size(); int yard = -1;
        for (int i = 0; i < (int) s.files.size(); ++i) { const auto& f = s.files[(size_t) i]; read = read && (f.info.sr > 0 || ! f.error.empty()); if (f.info.name == "yard") yard = i; }
        CHECK (read && yard >= 0, "the same files can come straight back (%zu, read again)", s.files.size());
        s.open (yard, 0); pump (s);
        CHECK (s.doc() && s.doc()->file == yard && ! s.doc()->events.empty(), "and open, with their events");
    }
    s.stop();

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
