// BatchCut - the processor: the session (files, edits, export), the preview player, recording the input,
// and the state saved with the project. The same code runs as the standalone app and as the VST3.
#pragma once

#include <set>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "neobeat/shared/rooms/Player.h"
#include "neobeat/shared/rooms/Session.h"
#include "neobeat/shared/rooms/Rooms.h"
#include "neobeat/shared/Material.h"
#include "neobeat/shared/rooms/Kit.h"
#include "neobeat/shared/rooms/Loops.h"
#include "neobeat/shared/rooms/Keys.h"
#include "../neobeat/shared/LearnerStore.h"
#include <deque>

// The input recorder: the audio thread writes into chunks the message thread allocates ahead of it.
class Recorder
{
public:
    static constexpr int kChunks = 180;          // 30 minutes in 10 s chunks
    void prepare (double sampleRate) { stop(); sr = sampleRate; }
    void start()
    {
        stop();
        chunkLen = std::max (1024, (int) (10.0 * sr));
        for (int i = 0; i < 2; ++i) ensure (i);
        written.store (0); overflow.store (false);
        recording.store (true);
    }
    // message thread: stops, and hands back what was recorded
    void stop (std::vector<float>* l = nullptr, std::vector<float>* r = nullptr)
    {
        recording.store (false);
        const int64_t n = written.load();
        if (l != nullptr && r != nullptr)
        {
            l->resize ((size_t) n); r->resize ((size_t) n);
            for (int64_t i = 0; i < n; ++i)
            {
                const auto* c = chunks[(size_t) (i / chunkLen)].get();
                const size_t o = (size_t) (i % chunkLen);
                (*l)[(size_t) i] = c != nullptr ? c->l[o] : 0.0f; (*r)[(size_t) i] = c != nullptr ? c->r[o] : 0.0f;
            }
        }
        written.store (0);
        // the audio thread may still be in its block: freed a moment later
        stoppedAt = juce::Time::getMillisecondCounter(); pendingFree = true;
    }
    // message thread, on a timer: chunks ahead of the write position, and freeing after a stop
    void service()
    {
        if (recording.load())
        {
            const int k = (int) (written.load() / std::max (1, chunkLen));
            for (int i = k; i < std::min (kChunks, k + 2); ++i) ensure (i);
        }
        else if (pendingFree && juce::Time::getMillisecondCounter() - stoppedAt > 500)
        {
            for (int i = 0; i < kChunks; ++i) { ptrs[(size_t) i].store (nullptr); chunks[(size_t) i].reset(); }
            pendingFree = false;
        }
    }
    // audio thread
    void push (const float* l, const float* r, int n) noexcept
    {
        if (! recording.load()) return;
        int64_t w = written.load();
        for (int i = 0; i < n; ++i, ++w)
        {
            const int k = (int) (w / chunkLen);
            if (k >= kChunks) { recording.store (false); overflow.store (true); break; }
            Chunk* c = ptrs[(size_t) k].load();
            if (c == nullptr) { overflow.store (true); break; }   // the timer fell behind: what follows is dropped
            const size_t o = (size_t) (w % chunkLen);
            c->l[o] = l[i]; c->r[o] = r != nullptr ? r[i] : l[i];
        }
        written.store (w);
    }
    bool isRecording() const noexcept { return recording.load(); }
    double seconds() const noexcept { return (double) written.load() / std::max (1.0, sr); }
    double rate() const noexcept { return sr; }
    bool overflowed() const noexcept { return overflow.load(); }

private:
    struct Chunk { std::vector<float> l, r; };
    void ensure (int i)
    {
        if (i < 0 || i >= kChunks || chunks[(size_t) i]) return;
        auto c = std::make_unique<Chunk>(); c->l.assign ((size_t) chunkLen, 0.0f); c->r.assign ((size_t) chunkLen, 0.0f);
        ptrs[(size_t) i].store (c.get());
        chunks[(size_t) i] = std::move (c);
    }
    double sr = 48000.0;
    int chunkLen = 480000;
    std::array<std::unique_ptr<Chunk>, kChunks> chunks;
    std::array<std::atomic<Chunk*>, kChunks> ptrs {};
    std::atomic<int64_t> written { 0 };
    std::atomic<bool> recording { false }, overflow { false };
    juce::uint32 stoppedAt = 0;
    bool pendingFree = false;
};

class BcProcessor final : public juce::AudioProcessor, public bc::Rooms, public juce::ChangeBroadcaster, private juce::AsyncUpdater, private juce::Timer
{
public:
    // MaterialLab: a learned object, played. `material` is the learner's fitted model; the voice
    // is that model as a bank of resonators, so it can be struck or have sound run through it.
    nsr::Material material;
    nsr::MaterialVoice materialVoice;
    std::atomic<bool> materialLive { false };     // true while the MATERIAL room is showing
    std::atomic<float> materialPending { 0.0f };  // a strike waiting for the audio thread

    bool materialOpen (const juce::File& file, juce::String& error);
    bool materialThrough (int poolIndex, float mix, juce::String& error);
    int  materialRender (int hits, juce::String& error);

    BcProcessor();
    ~BcProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Batchcut"; }
    bool acceptsMidi() const override { return true; }          // LOOPS and KEYS play from a keyboard
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    Recorder recorder;


    // playback of the open part. What plays and from where, for the playhead.
    void playEvent (const bc::Event& e);                         // as it will be exported (fades, level)
    void playRange (int64_t from, int64_t to, bool loop);        // the part with its layers and edits
    void stopPlayback() override { preview.stopPlaying(); playDoc = nullptr; }
    void roomsNotify() override { sendChangeMessage(); }
    bool isPlaying() const noexcept { return preview.playing(); }
    // the playhead in samples of the open part (-1: not playing it)
    int64_t playhead() const;
    bool playingEvent = false;

    bool startRecording();
    juce::File stopRecording();                                  // the recording's file (added and opened), or none
    juce::File renderToTemp (const bc::Event& e);                // one event as a WAV, for dragging out to a DAW

    // view state kept with the project


private:

private:
    void handleAsyncUpdate() override;
    void timerCallback() override;
    void refreshStateCache();
    void applyState (const juce::XmlElement& root);
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();


    std::atomic<float>* level = nullptr;
    std::atomic<float>* thru = nullptr;
    std::vector<float> scratch;                                  // a second channel for a mono output
    const bc::Doc* playDoc = nullptr;
    int64_t playOffset = 0;
    juce::CriticalSection stateLock;
    juce::MemoryBlock stateCache;
    std::unique_ptr<juce::XmlElement> pendingState;
    int ticks = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BcProcessor)
};
