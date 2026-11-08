// NeoBeat 2.0 — real-time plumbing: rendered audio, a lock-free publisher for the state the audio
// thread reads, and a content hasher for the render cache.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nb
{
// A rendered sound at the engine's sample rate.
struct Rendered
{
    std::vector<float> l, r;
    float sampleRate = 48000.0f;
    int size() const noexcept { return (int) l.size(); }
};
using RenderedPtr = std::shared_ptr<const Rendered>;

// One writer (any non-audio thread, serialised by a mutex) publishes immutable objects; the audio
// thread reads the newest between beginBlock and endBlock. Replaced objects are kept until the
// audio thread can no longer be holding them, then freed by the writer side (never on the audio thread).
template <typename T>
class RtPublisher
{
public:
    ~RtPublisher()
    {
        delete current.load();
        for (auto& r : retired) delete r.ptr;
    }

    // ---- audio thread
    const T* beginBlock() noexcept
    {
        epoch.fetch_add (1, std::memory_order_seq_cst);   // odd: inside a block
        return current.load (std::memory_order_seq_cst);
    }
    void endBlock() noexcept { epoch.fetch_add (1, std::memory_order_seq_cst); }

    // ---- writer threads
    void publish (std::unique_ptr<T> next)
    {
        std::lock_guard<std::mutex> lock (mutex);
        T* old = current.exchange (next.release(), std::memory_order_seq_cst);
        const uint64_t e = epoch.load (std::memory_order_seq_cst);
        if (old != nullptr) retired.push_back ({ old, e });
        collectLocked();
    }
    void collect() { std::lock_guard<std::mutex> lock (mutex); collectLocked(); }
    size_t pending() { std::lock_guard<std::mutex> lock (mutex); return retired.size(); }
    // the newest object, for writer-side reads (never freed while the caller holds the lock it returns)
    const T* peek() const noexcept { return current.load(); }

private:
    struct Retired { T* ptr; uint64_t epoch; };
    void collectLocked()
    {
        const uint64_t now = epoch.load (std::memory_order_seq_cst);
        for (size_t i = 0; i < retired.size();)
        {
            const auto& r = retired[i];
            // retired outside a block (even epoch): the audio thread will load the new pointer next.
            // retired inside a block (odd): wait for that block to end.
            const bool safe = (r.epoch % 2 == 0) || now > r.epoch;
            if (safe) { delete r.ptr; retired[i] = retired.back(); retired.pop_back(); }
            else ++i;
        }
    }
    std::atomic<T*> current { nullptr };
    std::atomic<uint64_t> epoch { 0 };
    std::mutex mutex;
    std::vector<Retired> retired;
};

// FNV-1a over the fields that decide a render.
struct Hasher
{
    uint64_t h = 1469598103934665603ull;
    Hasher& bytes (const void* p, size_t n) noexcept
    {
        const auto* b = (const uint8_t*) p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        return *this;
    }
    Hasher& f (float v) noexcept { return bytes (&v, sizeof v); }
    Hasher& d (double v) noexcept { return bytes (&v, sizeof v); }
    Hasher& i (int64_t v) noexcept { return bytes (&v, sizeof v); }
    Hasher& u (uint64_t v) noexcept { return bytes (&v, sizeof v); }
    Hasher& s (const std::string& v) noexcept { i ((int64_t) v.size()); return bytes (v.data(), v.size()); }
    Hasher& p (const void* ptr) noexcept { const auto v = (uint64_t) (uintptr_t) ptr; return u (v); }
};
} // namespace nb
