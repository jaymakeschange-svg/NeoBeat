// BatchCut - the session: the files, what the user changed in each, the one that is open, and the batch
// export. Framework-free: reading and writing audio files come in as callbacks (the app uses JUCE's).
//
// Long recordings are opened in parts of at most 8 minutes (2 s overlapping, so nothing is cut at a seam);
// an event belongs to the part its start falls in. Everything slow (reading, analysing, exporting) runs
// on one worker thread; the message thread only ever swaps in finished results.
#pragma once

#include "Engine.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>

namespace bc
{
struct FileInfo
{
    std::string path, name;   // name: without folder or extension
    double sr = 0.0;
    int64_t length = 0;
    int channels = 0;
};

// what the user changed in one part of one file
struct PartEdits
{
    std::vector<Event> keep;          // placed or adjusted by hand
    std::vector<int64_t> deleted;     // starts the user removed
    std::vector<int64_t> off;         // starts the user switched off for export
    std::vector<std::array<float, kFeat>> groupsOff;   // groups switched off (by their centre: groups are found again)
    LayerState layers;
    bool touched() const noexcept { return ! keep.empty() || ! deleted.empty() || ! off.empty() || ! groupsOff.empty() || ! layers.ops.empty() || layers.layers.size() > (size_t) kAuto; }
};

struct FileState
{
    FileInfo info;
    std::vector<PartEdits> parts;
    std::vector<int> counts;          // events per part when last analysed (-1: not yet)
    std::string error;
};

struct Settings
{
    DetectParams detect;
    PickParams pick;                  // ALL / BEST / LIKE
    ExportParams exp;
    std::string outDir;
    bool folderPerFile = true;
    // FIND MORE: what to search for, in every file (set from an event in any file)
    Template like;
    std::string likeName;
};

// The part that is open: its sound, its analysis, its events and groups (an immutable snapshot)
struct Doc
{
    int file = -1, part = 0, parts = 1;
    int64_t offset = 0;               // where the part starts in the file
    std::shared_ptr<const Audio> audio;
    std::shared_ptr<const Analysis> an;
    std::vector<Event> events;
    std::vector<Group> groups;
    LayerState layers;
};

using Prober = std::function<bool (const std::string& path, FileInfo& info, std::string& error)>;
using Loader = std::function<bool (const std::string& path, int64_t start, int64_t length, Audio& out, std::string& error)>;
using Writer = std::function<bool (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double sr, int bits, std::string& error)>;

inline int64_t partLength (double sr) noexcept { return (int64_t) (8.0 * 60.0 * sr); }
inline int64_t partOverlap (double sr) noexcept { return (int64_t) (2.0 * sr); }
inline int partCount (const FileInfo& f) noexcept
{
    if (f.length <= 0 || f.sr <= 0) return 1;
    const int64_t L = partLength (f.sr), step = L - partOverlap (f.sr);
    return f.length <= L ? 1 : (int) ((f.length - L + step - 1) / step) + 1;
}
inline int64_t partStart (const FileInfo& f, int p) noexcept { return (int64_t) p * (partLength (f.sr) - partOverlap (f.sr)); }
// the starts that belong to part p (relative to the part): from the middle of the overlap with the part
// before, up to the middle of the overlap with the part after
inline int64_t partOwnStart (const FileInfo& f, int p) noexcept { return p > 0 ? partOverlap (f.sr) / 2 : 0; }
inline int64_t partOwnEnd (const FileInfo& f, int p) noexcept
{
    const int n = partCount (f);
    if (p >= n - 1) return INT64_MAX;
    return partLength (f.sr) - partOverlap (f.sr) / 2;
}

inline std::string groupLetter (int g)
{
    std::string s;
    do { s.insert (s.begin(), (char) ('A' + g % 26)); g = g / 26 - 1; } while (g >= 0);
    return s;
}

inline std::string threeDigits (int n) { char b[16]; std::snprintf (b, sizeof b, "%03d", n); return b; }

// The events of a part, with the user's changes and the FIND MORE search applied, grouped.
inline void detectPart (const Audio& a, const Analysis& an, const Settings& st, const PartEdits& ed, int64_t ownStart, int64_t ownEnd, std::vector<Event>& ev, std::vector<Group>& groups)
{
    ev = detect (a, an, st.detect, ed.keep, ed.deleted, st.like.valid ? &st.like : nullptr, st.pick.similarity);
    // starts in the overlaps belong to the neighbouring part (they still cut this part's events)
    ev.erase (std::remove_if (ev.begin(), ev.end(), [&] (const Event& e) { return (e.onset >= ownEnd || e.onset < ownStart) && ! e.manual; }), ev.end());
    const int64_t gap = std::max<int64_t> (1, (int64_t) (st.detect.minGapMs * 0.001 * a.sr));
    for (auto& e : ev)
        for (auto o : ed.off) if (std::llabs (o - e.onset) < gap) e.enabled = false;
    group (ev, groups, st.detect.grouping);
    for (auto& g : groups)
        for (const auto& c : ed.groupsOff) if (featDist (g.centroid, c) < 0.5f) g.enabled = false;
}

// file names for an event: stem_A001.wav (group letter, number in the group), stem_like_001.wav, with
// _p2 for later parts of long files and _tone / _noise ... for separate layers
struct Namer
{
    std::map<std::string, int> next;
    std::string name (const std::string& stem, int part, const Event& e, const std::vector<Group>& groups, bool like, const std::string& layer)
    {
        std::string key = like ? std::string ("like") : (e.group >= 0 && e.group < (int) groups.size() ? groupLetter (e.group) : std::string ("X"));
        if (part > 0) key = "p" + std::to_string (part + 1) + "_" + key;
        const int n = ++next[key + "|" + layer];
        std::string s = stem + "_" + key + (like ? "_" : "") + threeDigits (n);
        if (! layer.empty()) s += "_" + layer;
        return s + ".wav";
    }
};

inline std::string lowerName (std::string s) { for (auto& c : s) { if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a'); if (c == ' ') c = '_'; } return s; }

inline std::string joinPath (const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    const char last = dir.back();
    return (last == '/' || last == '\\') ? dir + name : dir + "/" + name;
}

// Everything slow happens here, one task at a time. Results come back through `onDone` (called on the
// worker thread: the app hands them to its message thread).
class Session
{
public:
    Prober prober;
    Loader loader;
    Writer writer;
    std::function<bool (const std::string& dir)> makeDir;
    std::function<void()> onChange;   // something finished (any thread)

    Session() = default;
    ~Session() { stop(); }

    void start() { if (! worker.joinable()) { quit = false; worker = std::thread ([this] { run(); }); } }
    void stop()
    {
        { std::lock_guard<std::mutex> lock (mutex); quit = true; cancel = true; }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    // ---------------------------------------------------------------- message thread
    std::vector<FileState> files;
    Settings settings;

    // the open part (null while nothing is open)
    std::shared_ptr<const Doc> doc() const { std::lock_guard<std::mutex> lock (docMutex); return current; }

    // adds files (probed on the worker); duplicates are skipped
    void addFiles (const std::vector<std::string>& paths)
    {
        std::vector<std::string> fresh;
        for (const auto& p : paths)
        {
            bool dup = false; for (const auto& f : files) dup = dup || f.info.path == p;
            for (const auto& q : fresh) dup = dup || q == p;
            if (! dup) fresh.push_back (p);
        }
        if (fresh.empty()) return;
        for (const auto& p : fresh) { FileState f; f.info.path = p; f.info.name = stemOf (p); f.parts.resize (1); f.counts.assign (1, -1); files.push_back (f); }
        Task t; t.kind = Task::Probe; t.paths = fresh;
        post (std::move (t));
    }

    // reads every file's details again (after a saved project was loaded: the files may have changed or moved)
    void refresh()
    {
        if (files.empty()) return;
        Task t; t.kind = Task::Probe;
        for (const auto& f : files) t.paths.push_back (f.info.path);
        post (std::move (t));
    }

    // nothing open (the file list was replaced)
    void close() { setDoc (nullptr); }

    void removeFile (int i)
    {
        if (i < 0 || i >= (int) files.size()) return;
        files.erase (files.begin() + i);
        auto d = doc();
        if (d && d->file == i) setDoc (nullptr);
        else if (d && d->file > i) { auto c = std::make_shared<Doc> (*d); --c->file; setDoc (c); }
    }

    // opens part `part` of file i (read and analysed on the worker)
    void open (int i, int part = 0)
    {
        if (i < 0 || i >= (int) files.size()) return;
        Task t; t.kind = Task::Open; t.file = i; t.part = part; t.path = files[(size_t) i].info.path; t.info = files[(size_t) i].info;
        t.edits = edits (i, part); t.settings = settings;
        post (std::move (t));
    }

    // the detection or the user's events changed: find the events again (fast, on the worker)
    void redetect()
    {
        auto d = doc();
        if (! d || d->file < 0) return;
        Task t; t.kind = Task::Detect; t.file = d->file; t.part = d->part; t.base = d; t.info = files[(size_t) d->file].info;
        t.edits = edits (d->file, d->part); t.settings = settings;
        post (std::move (t));
    }

    // the layers or edits changed: only the snapshot changes (rendering reads them live)
    void layersChanged()
    {
        auto d = doc();
        if (! d || d->file < 0) return;
        auto c = std::make_shared<Doc> (*d);
        c->layers = edits (d->file, d->part).layers;
        setDoc (c);
    }

    PartEdits& edits (int file, int part)
    {
        auto& f = files[(size_t) file];
        if ((int) f.parts.size() <= part) f.parts.resize ((size_t) part + 1);
        return f.parts[(size_t) part];
    }

    // export: the open file only, or every file. Progress through status().
    void exportFiles (bool allFiles)
    {
        Task t; t.kind = Task::Export; t.settings = settings;
        auto d = doc();
        for (int i = 0; i < (int) files.size(); ++i)
            if (allFiles || (d && d->file == i)) { t.exportFiles.push_back ((int) i); t.exportStates.push_back (files[(size_t) i]); }
        if (t.exportStates.empty()) return;
        post (std::move (t));
    }

    void cancelTask() { std::lock_guard<std::mutex> lock (mutex); cancel = true; }

    struct Status
    {
        bool busy = false;
        std::string what;      // "Analysing kitchen.wav", "Exporting 12 of 40"
        float progress = 0.0f;
        int exported = 0, failed = 0;
        std::string lastError, lastExportDir;
        std::string note;      // what the last export did about levels ("all 40 at -18.2 LUFS ...")
    };
    // busy while anything is queued or running (reading files included), not only while a task reports progress
    Status status() const
    {
        Status s; { std::lock_guard<std::mutex> lock (statusMutex); s = stat; }
        bool pending = false; { std::lock_guard<std::mutex> lock (mutex); pending = ! tasks.empty() || runningKind >= 0; }
        if (pending && ! s.busy) { s.busy = true; if (s.what.empty()) s.what = "Reading files"; }
        return s;
    }

    // results the worker finished, applied on the message thread (call it when onChange fires)
    void applyResults()
    {
        std::vector<Result> rs;
        { std::lock_guard<std::mutex> lock (resultMutex); rs.swap (results); }
        for (auto& r : rs)
        {
            if (r.kind == Result::Probed)
            {
                for (auto& f : files)
                    if (f.info.path == r.info.path)
                    {
                        const int parts = r.error.empty() ? partCount (r.info) : 1;
                        f.info = r.info; f.error = r.error;
                        if ((int) f.parts.size() < parts) f.parts.resize ((size_t) parts);
                        f.counts.resize ((size_t) parts, -1);
                    }
            }
            else if (r.kind == Result::Opened)
            {
                if (r.file < 0 || r.file >= (int) files.size() || files[(size_t) r.file].info.path != r.info.path) continue;   // removed meanwhile
                auto& f = files[(size_t) r.file];
                if (! r.error.empty()) { f.error = r.error; continue; }
                if (f.info.sr <= 0 && r.info.sr > 0) { f.info = r.info; f.counts.resize ((size_t) partCount (f.info), -1); }
                if ((int) f.counts.size() > r.part) f.counts[(size_t) r.part] = (int) r.doc->events.size();
                auto c = std::make_shared<Doc> (*r.doc);
                c->layers = edits (r.file, r.part).layers;   // the newest edits, not the ones the task started with
                setDoc (c);
            }
        }
    }

private:
    struct Task
    {
        enum Kind { Probe, Open, Detect, Export } kind = Probe;
        std::vector<std::string> paths;
        std::string path;
        FileInfo info;
        int file = -1, part = 0;
        PartEdits edits;
        Settings settings;
        std::shared_ptr<const Doc> base;
        std::vector<int> exportFiles;
        std::vector<FileState> exportStates;
    };
    struct Result
    {
        enum Kind { Probed, Opened } kind = Probed;
        FileInfo info;
        std::string error;
        int file = -1, part = 0;
        std::shared_ptr<Doc> doc;
    };

    static std::string stemOf (const std::string& p)
    {
        const size_t s = p.find_last_of ("/\\");
        std::string n = s == std::string::npos ? p : p.substr (s + 1);
        const size_t d = n.find_last_of ('.');
        return d == std::string::npos || d == 0 ? n : n.substr (0, d);
    }

    void setDoc (std::shared_ptr<const Doc> d) { std::lock_guard<std::mutex> lock (docMutex); current = std::move (d); }

    void post (Task t)
    {
        {
            std::lock_guard<std::mutex> lock (mutex);
            // a newer open or detect makes an older one pointless; probes and exports always run
            if (t.kind == Task::Open || t.kind == Task::Detect)
            {
                tasks.erase (std::remove_if (tasks.begin(), tasks.end(), [] (const Task& x) { return x.kind == Task::Open || x.kind == Task::Detect; }), tasks.end());
                if (runningKind == Task::Open || runningKind == Task::Detect) cancel = true;
            }
            tasks.push_back (std::move (t));
        }
        cv.notify_all();
    }

    void setStatus (bool busy, const std::string& what, float progress)
    {
        std::lock_guard<std::mutex> lock (statusMutex);
        stat.busy = busy; stat.what = what; stat.progress = progress;
    }

    void finished (Result r)
    {
        { std::lock_guard<std::mutex> lock (resultMutex); results.push_back (std::move (r)); }
        if (onChange) onChange();
    }

    bool cancelled() { std::lock_guard<std::mutex> lock (mutex); return cancel || quit; }

    void run()
    {
        while (true)
        {
            Task t;
            {
                std::unique_lock<std::mutex> lock (mutex);
                cv.wait (lock, [this] { return quit || ! tasks.empty(); });
                if (quit) return;
                t = std::move (tasks.front()); tasks.pop_front();
                cancel = false; runningKind = t.kind;
            }
            try
            {
                if (t.kind == Task::Probe) doProbe (t);
                else if (t.kind == Task::Open) doOpen (t);
                else if (t.kind == Task::Detect) doDetect (t);
                else doExport (t);
            }
            catch (const std::exception& e) { std::lock_guard<std::mutex> lock (statusMutex); stat.lastError = e.what(); }
            catch (...) { std::lock_guard<std::mutex> lock (statusMutex); stat.lastError = "unexpected error"; }
            { std::lock_guard<std::mutex> lock (mutex); runningKind = -1; }
            setStatus (false, "", 1.0f);
            if (onChange) onChange();
        }
    }

    void doProbe (const Task& t)
    {
        for (size_t k = 0; k < t.paths.size(); ++k)
        {
            const auto& p = t.paths[k];
            setStatus (true, "Reading " + stemOf (p), (float) k / (float) t.paths.size());
            Result r; r.kind = Result::Probed; r.info.path = p; r.info.name = stemOf (p);
            std::string err;
            if (! prober || ! prober (p, r.info, err)) r.error = err.empty() ? "could not read this file" : err;
            r.info.path = p; if (r.info.name.empty()) r.info.name = stemOf (p);
            finished (std::move (r));
        }
    }

    bool loadPart (const FileInfo& info, int part, Audio& a, std::string& err)
    {
        const int64_t s0 = partStart (info, part), len = std::min (partLength (info.sr), info.length - s0);
        if (! loader || len <= 0) { err = "nothing to read"; return false; }
        if (! loader (info.path, s0, len, a, err)) return false;
        a.name = info.name;
        return true;
    }

    void doOpen (const Task& t)
    {
        Result r; r.kind = Result::Opened; r.file = t.file; r.part = t.part; r.info = t.info;
        setStatus (true, "Reading " + t.info.name, 0.0f);
        auto audio = std::make_shared<Audio>();
        std::string err;
        if (r.info.sr <= 0 && prober) { r.info.path = t.path; prober (t.path, r.info, err); r.info.path = t.path; if (r.info.name.empty()) r.info.name = stemOf (t.path); }
        if (r.info.sr <= 0) { r.error = err.empty() ? "could not read this file" : err; finished (std::move (r)); return; }
        const FileInfo info = r.info;
        const int part = std::max (0, std::min (t.part, partCount (info) - 1));
        if (! loadPart (info, part, *audio, err)) { r.error = err.empty() ? "could not read this file" : err; finished (std::move (r)); return; }
        if (cancelled()) return;
        setStatus (true, "Analysing " + info.name, 0.0f);
        auto an = analyze (*audio, [this, &info] (float p) { setStatus (true, "Analysing " + info.name, p); return ! cancelled(); });
        if (! an) return;   // cancelled: a newer task replaces it
        auto d = std::make_shared<Doc>();
        d->file = t.file; d->part = part; d->parts = partCount (info); d->offset = partStart (info, part);
        d->audio = audio; d->an = an;
        detectPart (*audio, *an, t.settings, t.edits, partOwnStart (info, part), partOwnEnd (info, part), d->events, d->groups);
        d->layers = t.edits.layers;
        r.part = part; r.doc = d;
        finished (std::move (r));
    }

    void doDetect (const Task& t)
    {
        if (! t.base || ! t.base->audio || ! t.base->an) return;
        auto d = std::make_shared<Doc> (*t.base);
        detectPart (*d->audio, *d->an, t.settings, t.edits, partOwnStart (t.info, d->part), partOwnEnd (t.info, d->part), d->events, d->groups);
        if (cancelled()) return;
        Result r; r.kind = Result::Opened; r.file = t.file; r.part = d->part; r.info = t.info; r.doc = d;
        finished (std::move (r));
    }

    // one one-shot's files and the loudness they were written at (for matching them all afterwards)
    struct Written { std::vector<std::string> paths; std::vector<int64_t> lengths; float lufs = -120.0f, peakDb = -120.0f; double sr = 0; };

    void doExport (const Task& t)
    {
        const auto& st = t.settings;
        int written = 0, failed = 0, held = 0;
        std::string lastErr, lastDir;
        std::vector<Written> loud;   // loudness mode: what was written, at what level
        const bool like = st.pick.mode == Pick::Like;
        PickParams pk = st.pick;
        if (like) { pk.ref = st.like.feat; pk.hasRef = st.like.valid; pk.refRise = st.like.w; pk.hasRise = st.like.valid; }
        ExportParams raw = st.exp; raw.normMode = 0;   // rendered as is; the level is decided per one-shot below
        const int total = (int) t.exportStates.size();
        for (int fi = 0; fi < total; ++fi)
        {
            const auto& fs = t.exportStates[(size_t) fi];
            if (fs.info.sr <= 0) { ++failed; lastErr = fs.info.name + ": could not read it"; continue; }
            std::string dir = st.outDir;
            if (st.folderPerFile) dir = joinPath (dir, fs.info.name);
            if (makeDir && ! makeDir (dir)) { ++failed; lastErr = "could not make the folder " + dir; continue; }
            lastDir = dir;
            Namer namer;
            const int parts = partCount (fs.info);
            for (int p = 0; p < parts; ++p)
            {
                if (cancelled()) { std::lock_guard<std::mutex> lock (statusMutex); stat.exported = written; stat.failed = failed; stat.lastError = "export stopped"; stat.note.clear(); return; }
                const std::string what = "Exporting " + fs.info.name + (parts > 1 ? " (part " + std::to_string (p + 1) + " of " + std::to_string (parts) + ")" : std::string()) + "  |  file " + std::to_string (fi + 1) + " of " + std::to_string (total);
                setStatus (true, what, (float) fi / (float) std::max (1, total));
                Audio a; std::string err;
                if (! loadPart (fs.info, p, a, err)) { ++failed; lastErr = fs.info.name + ": " + err; break; }
                auto an = analyze (a, [this, fi, total, &what] (float pr) { setStatus (true, what, ((float) fi + 0.5f * pr) / (float) std::max (1, total)); return ! cancelled(); });
                if (! an) continue;
                const PartEdits ed = p < (int) fs.parts.size() ? fs.parts[(size_t) p] : PartEdits();
                std::vector<Event> ev; std::vector<Group> groups;
                detectPart (a, *an, st, ed, partOwnStart (fs.info, p), partOwnEnd (fs.info, p), ev, groups);
                const auto chosen = pick (ev, groups, pk);
                for (size_t k = 0; k < chosen.size(); ++k)
                {
                    if (cancelled()) break;
                    const auto& e = ev[(size_t) chosen[k]];
                    // what is written for this one-shot: the mix, or each audible layer on its own
                    struct Out { std::vector<float> l, r; std::string layer; };
                    std::vector<Out> outs;
                    std::vector<float> ml, mr;
                    renderEvent (a, *an, ed.layers, e, raw, -1, ml, mr);
                    const Level lv = measureLevel (ml, mr, a.sr);   // the level is the mix's, also for layer files
                    float peakDb = lv.peakDb;
                    if (st.exp.separate)
                    {
                        for (int L = 0; L < (int) ed.layers.layers.size(); ++L)
                        {
                            if (! ed.layers.audible (L)) continue;   // a muted layer is not written on its own
                            Out o; o.layer = lowerName (ed.layers.layers[(size_t) L].name);
                            renderEvent (a, *an, ed.layers, e, raw, L, o.l, o.r);
                            peakDb = std::max (peakDb, peakDbOf (o.l, o.r));
                            outs.push_back (std::move (o));
                        }
                    }
                    else { Out o; o.l = std::move (ml); o.r = std::move (mr); outs.push_back (std::move (o)); }
                    bool wasHeld = false;
                    const float g = normGainDb (lv, peakDb, st.exp, &wasHeld);
                    held += wasHeld ? 1 : 0;
                    Written w; w.lufs = lv.lufs + g; w.peakDb = peakDb + g; w.sr = a.sr;
                    for (auto& o : outs)
                    {
                        applyGainDb (o.l, o.r, g);
                        const std::string path = joinPath (dir, namer.name (fs.info.name, p, e, groups, like, o.layer));
                        std::string werr;
                        if (writer && writer (path, o.l, o.r, a.sr, st.exp.bits, werr)) { ++written; w.paths.push_back (path); w.lengths.push_back ((int64_t) o.l.size()); }
                        else { ++failed; lastErr = werr.empty() ? "could not write " + path : werr; }
                    }
                    if (st.exp.normMode == 2 && ! w.paths.empty() && lv.lufs > -119.0f) loud.push_back (std::move (w));
                    setStatus (true, what, ((float) fi + 0.5f + 0.5f * (float) (k + 1) / (float) std::max<size_t> (1, chosen.size())) / (float) std::max (1, total));
                }
            }
        }
        std::string note;
        if (st.exp.normMode == 1 && written > 0) note = "every peak at " + dbText (st.exp.normDb) + " dBFS";
        if (st.exp.normMode == 2 && ! loud.empty())
        {
            // loudness: the ones the ceiling held back are quieter than the rest. To make them all alike, the others
            // come down to the quietest (a gain down only: nothing new can clip)
            size_t qi = 0; for (size_t i = 0; i < loud.size(); ++i) if (loud[i].lufs < loud[qi].lufs) qi = i;
            const float level = loud[qi].lufs;
            if (held > 0 && st.exp.matchAll && ! cancelled())
            {
                int done = 0, todo = 0; for (const auto& w : loud) todo += w.lufs - level > 0.05f ? 1 : 0;
                for (auto& w : loud)
                {
                    if (cancelled()) break;
                    const float d = level - w.lufs;
                    if (d > -0.05f) continue;
                    setStatus (true, "Matching loudness  |  " + std::to_string (++done) + " of " + std::to_string (todo), (float) done / (float) std::max (1, todo));
                    for (size_t j = 0; j < w.paths.size(); ++j)
                    {
                        Audio f; std::string err;
                        if (! loader || ! loader (w.paths[j], 0, w.lengths[j], f, err)) { ++failed; lastErr = "could not read back " + w.paths[j] + (err.empty() ? std::string() : ": " + err); continue; }
                        if (f.r.size() != f.l.size()) f.r = f.l;
                        applyGainDb (f.l, f.r, d);
                        std::string werr;
                        if (! writer || ! writer (w.paths[j], f.l, f.r, f.sr > 0 ? f.sr : w.sr, st.exp.bits, werr)) { ++failed; lastErr = werr.empty() ? "could not write " + w.paths[j] : werr; }
                    }
                    w.lufs = level; w.peakDb += d;
                }
                const std::string who = loud[qi].paths.empty() ? std::string() : loud[qi].paths[0].substr (loud[qi].paths[0].find_last_of ("/\\") + 1);
                note = "all " + std::to_string (loud.size()) + " at " + dbText (level) + " LUFS: the target " + dbText (st.exp.loudDb)
                     + " would make " + std::to_string (held) + " clip; the one sharpest for its loudness (" + who + ") set the level";
            }
            else if (held > 0)
                note = std::to_string (held) + " of " + std::to_string (loud.size()) + " are under " + dbText (st.exp.loudDb) + " LUFS (louder would clip; the quietest is at " + dbText (level) + ")";
            else note = "all " + std::to_string (loud.size()) + " at " + dbText (st.exp.loudDb) + " LUFS";
        }
        std::lock_guard<std::mutex> lock (statusMutex);
        stat.exported = written; stat.failed = failed; stat.lastError = lastErr; stat.lastExportDir = lastDir; stat.note = note;
    }

    static std::string dbText (float v) { char b[32]; std::snprintf (b, sizeof b, "%.1f", (double) v); return b; }

    std::thread worker;
    mutable std::mutex mutex, docMutex, statusMutex, resultMutex;
    std::condition_variable cv;
    std::deque<Task> tasks;
    bool quit = false, cancel = false;
    int runningKind = -1;
    std::shared_ptr<const Doc> current;
    Status stat;
    std::vector<Result> results;
};
} // namespace bc
