// BatchCut - the preview player. A worker renders what is about to play (the layers and edits applied)
// in half-second chunks a few seconds ahead of the playhead; the audio thread only reads finished chunks.
// The audio thread never locks, allocates or frees: chunks and jobs are freed by the other threads a
// while after the audio thread can last have seen them.
#pragma once

#include "Engine.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace bc
{
class Preview
{
public:
    Preview() = default;
    ~Preview() { stopWorker(); }

    void startWorker()
    {
        if (worker.joinable()) return;
        { std::lock_guard<std::mutex> lock (mutex); quit = false; }
        worker = std::thread ([this] { run(); });
    }
    void stopWorker()
    {
        { std::lock_guard<std::mutex> lock (mutex); quit = true; }
        cv.notify_all();
        if (worker.joinable()) worker.join();
    }

    // ---- message thread
    // Plays [from, to) of a recording with its layers and edits (rendered ahead by the worker).
    void play (std::shared_ptr<const Audio> audio, std::shared_ptr<const Analysis> an, const LayerState& layers, int64_t from, int64_t to, bool loop = false)
    {
        if (! audio || ! an) return;
        from = std::max<int64_t> (0, std::min (from, audio->size()));
        to = std::max (from, std::min (to, audio->size()));
        auto job = std::make_shared<Job>();
        job->audio = std::move (audio); job->an = std::move (an); job->layers = layers;
        job->from = from; job->to = to; job->sr = job->audio->sr;
        job->loop = loop && (to - from) < (int64_t) (120.0 * job->sr);   // a loop keeps all its chunks: two minutes at most
        job->chunk = std::max<int64_t> (256, (int64_t) (0.25 * job->sr));
        job->count = (int) ((to - from + job->chunk - 1) / job->chunk);
        job->slots.reset (new std::atomic<Chunk*>[(size_t) std::max (1, job->count)]);
        for (int i = 0; i < std::max (1, job->count); ++i) job->slots[(size_t) i].store (nullptr);
        swapIn (std::move (job));
    }

    // Plays a clip that is already rendered (an event as it will be exported).
    void playClip (std::vector<float> l, std::vector<float> r, double sr)
    {
        auto job = std::make_shared<Job>();
        job->sr = sr; job->from = 0; job->to = (int64_t) l.size();
        job->chunk = std::max<int64_t> (1, (int64_t) l.size()); job->count = l.empty() ? 0 : 1;
        job->slots.reset (new std::atomic<Chunk*>[1]);
        auto c = std::make_unique<Chunk>(); c->l = std::move (l); c->r = r.size() == c->l.size() ? std::move (r) : c->l;
        job->slots[0].store (c.get());
        job->owned.push_back (std::move (c));
        job->rendered = 1;
        swapIn (std::move (job));
    }

    void stopPlaying() { swapIn (nullptr); }

    bool playing() const noexcept { const Job* j = current.load(); return j != nullptr && ! j->finished.load(); }
    // the playhead, in samples of the recording (-1: not playing)
    int64_t position() const noexcept { const Job* j = current.load(); return j == nullptr || j->finished.load() ? -1 : j->from + (int64_t) j->pos.load(); }
    bool waiting() const noexcept { const Job* j = current.load(); return j != nullptr && j->starved.load(); }

    // frees what the audio thread can no longer be using
    void collect()
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock (mutex);
        while (! retired.empty() && now - retired.front().first > std::chrono::milliseconds (1500)) retired.pop_front();
    }

    // ---- audio thread: adds into L and R
    void process (float* L, float* R, int n, double outSr, float gain = 1.0f) noexcept
    {
        Job* j = current.load();
        if (j == nullptr || j->finished.load() || n <= 0) return;
        const double step = j->sr / std::max (1.0, outSr);
        double pos = j->pos.load();
        const double len = (double) (j->to - j->from);
        bool starved = false;
        for (int i = 0; i < n; ++i)
        {
            if (pos >= len - 1.0)
            {
                if (j->loop && len > 2.0) pos = 0.0;
                else { j->finished.store (true); break; }
            }
            const int64_t i0 = (int64_t) pos;
            const float f = (float) (pos - (double) i0);
            float l0, r0, l1, r1;
            if (! sampleAt (*j, i0, l0, r0) || ! sampleAt (*j, i0 + 1, l1, r1)) { starved = true; break; }   // not rendered yet: wait
            L[i] += gain * (l0 + (l1 - l0) * f);
            R[i] += gain * (r0 + (r1 - r0) * f);
            pos += step;
        }
        j->starved.store (starved);
        j->pos.store (pos);
    }

private:
    struct Chunk { std::vector<float> l, r; };
    struct Job
    {
        std::shared_ptr<const Audio> audio;
        std::shared_ptr<const Analysis> an;
        LayerState layers;
        int64_t from = 0, to = 0, chunk = 1;
        double sr = 48000.0;
        int count = 0;
        bool loop = false;
        std::unique_ptr<std::atomic<Chunk*>[]> slots;
        std::vector<std::unique_ptr<Chunk>> owned;                                         // the worker's
        std::deque<std::pair<std::chrono::steady_clock::time_point, std::unique_ptr<Chunk>>> dropped;   // behind the playhead, freed later
        int rendered = 0;                                                                   // the worker's: chunks done in order
        std::atomic<double> pos { 0.0 };
        std::atomic<bool> finished { false }, starved { false };
    };

    static bool sampleAt (const Job& j, int64_t i, float& l, float& r) noexcept
    {
        if (i < 0 || i >= j.to - j.from) { l = r = 0.0f; return true; }
        const int k = (int) (i / j.chunk);
        const Chunk* c = j.slots[(size_t) k].load();
        if (c == nullptr) return false;
        const size_t o = (size_t) (i - (int64_t) k * j.chunk);
        if (o >= c->l.size()) { l = r = 0.0f; return true; }
        l = c->l[o]; r = c->r[o];
        return true;
    }

    void swapIn (std::shared_ptr<Job> job)
    {
        Job* raw = job.get();
        std::lock_guard<std::mutex> lock (mutex);
        Job* old = current.exchange (raw);
        (void) old;
        if (active) retired.push_back ({ std::chrono::steady_clock::now(), std::move (active) });
        active = std::move (job);
        cv.notify_all();
    }

    void run()
    {
        while (true)
        {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock (mutex);
                cv.wait_for (lock, std::chrono::milliseconds (20));
                if (quit) return;
                job = active;
            }
            if (! job || ! job->audio) continue;
            // render ahead of the playhead, at most 6 s, in order
            const int64_t playAt = (int64_t) job->pos.load();
            const int ahead = std::max (2, (int) (6.0 * job->sr / (double) job->chunk));
            const int playing = (int) (playAt / job->chunk);
            while (job->rendered < job->count && job->rendered <= playing + ahead)
            {
                {
                    std::lock_guard<std::mutex> lock (mutex);
                    if (quit || active != job) break;
                }
                const int k = job->rendered;
                const int64_t s0 = job->from + (int64_t) k * job->chunk, s1 = std::min (job->to, s0 + job->chunk);
                auto c = std::make_unique<Chunk>();
                render (*job->audio, *job->an, job->layers, s0, s1, c->l, c->r);
                job->slots[(size_t) k].store (c.get());
                job->owned.push_back (std::move (c));
                ++job->rendered;
            }
            // chunks well behind the playhead go (not in a loop: it comes round again). Chunk k is slot k.
            if (! job->loop)
                for (int k = 0; k < std::min (playing - 1, (int) job->owned.size()); ++k)
                    if (job->owned[(size_t) k])
                    {
                        job->slots[(size_t) k].store (nullptr);
                        job->dropped.push_back ({ std::chrono::steady_clock::now(), std::move (job->owned[(size_t) k]) });
                    }
            const auto now = std::chrono::steady_clock::now();
            while (! job->dropped.empty() && now - job->dropped.front().first > std::chrono::milliseconds (1500)) job->dropped.pop_front();
        }
    }

    std::atomic<Job*> current { nullptr };
    std::shared_ptr<Job> active;   // the message thread's and the worker's (under mutex)
    std::deque<std::pair<std::chrono::steady_clock::time_point, std::shared_ptr<Job>>> retired;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable cv;
    bool quit = false;
};
} // namespace bc
