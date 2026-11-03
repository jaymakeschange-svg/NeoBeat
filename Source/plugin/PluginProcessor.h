// NeoBeat — the plugin processor: parameters, host sync, MIDI in/out and state.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Engine.h"
#include "../engine/Sequencer.h"
#include "../engine/Presets.h"
#include "../engine/Kits.h"

// The pattern the editor edits and the audio thread plays, shared without locks.
struct PatternStore
{
    std::array<std::atomic<uint32_t>, nb::Pattern::kTracks * nb::Pattern::kMaxSteps> cells {};
    std::atomic<int> length { 32 };
    std::atomic<uint32_t> version { 1 };

    static uint32_t pack (nb::Step s) noexcept { return (uint32_t) s.vel | ((uint32_t) s.roll << 8) | ((uint32_t) (uint8_t) s.nudge << 16); }
    static nb::Step unpack (uint32_t v) noexcept
    {
        nb::Step s; s.vel = (uint8_t) (v & 0xff); s.roll = (uint8_t) ((v >> 8) & 0xff); s.nudge = (int8_t) (uint8_t) ((v >> 16) & 0xff);
        return s;
    }
    nb::Step get (int track, int step) const noexcept { return unpack (cells[(size_t) (track * nb::Pattern::kMaxSteps + step)].load (std::memory_order_relaxed)); }
    void set (int track, int step, nb::Step s) noexcept
    {
        cells[(size_t) (track * nb::Pattern::kMaxSteps + step)].store (pack (s), std::memory_order_relaxed);
        version.fetch_add (1, std::memory_order_release);
    }
    void load (const nb::Pattern& p) noexcept
    {
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
            for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
                cells[(size_t) (t * nb::Pattern::kMaxSteps + s)].store (pack (p.steps[(size_t) t][(size_t) s]), std::memory_order_relaxed);
        length.store (p.length);
        version.fetch_add (1, std::memory_order_release);
    }
    void snapshot (nb::Pattern& p) const noexcept
    {
        p.length = length.load();
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
            for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
                p.steps[(size_t) t][(size_t) s] = get (t, s);
    }
};

class NeoBeatProcessor final : public juce::AudioProcessor
{
    // Declared first: the parameter layout reads the sound library it builds.
    std::unique_ptr<nb::Engine> engine;

public:
    static constexpr int kPads = nb::Engine::kPads;
    static constexpr int kPadNoteBase = 36;   // C1 (Ableton naming) .. D#2 play pads 1-16
    static constexpr int kKeyRoot = 60;        // C3: in Key mode, keys from here play the pattern transposed

    NeoBeatProcessor();
    ~NeoBeatProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "NeoBeat"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return nb::kNumPresets; }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override { loadPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- used by the editor (message thread) ---
    void loadPreset (int index);
    void loadKit (int index);
    void audition (int pad, float velocity) noexcept { pendingAudition[(size_t) pad].store (velocity); }
    bool loadSample (int pad, const juce::File& file);
    void clearSample (int pad);
    juce::String padName (int pad) const;
    juce::String padGroup (int pad) const;
    bool padUsesSample (int pad) const noexcept { return useSample[(size_t) pad].load(); }
    juce::String paramId (int pad, const char* name) const { return "p" + juce::String (pad + 1) + "_" + name; }

    juce::AudioProcessorValueTreeState apvts;
    PatternStore pattern;
    const nb::Library& library() const noexcept { return engine->lib(); }
    std::atomic<int> currentPreset { 0 }, currentKit { 0 }, playStep { -1 }, selectedPad { 0 };
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> hostSynced { false };
    uint32_t hitCount (int pad) const noexcept { return engine->hitCounter[(size_t) pad].load (std::memory_order_relaxed); }

    // Mono tap of the selected pad, for the editor's spectrum view.
    juce::AbstractFifo tapFifo { 16384 };
    std::vector<float> tapBuffer = std::vector<float> (16384, 0.0f);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const nb::Library& lib);
    void readParameters() noexcept;
    void pushTap (const float* data, int n) noexcept;

    nb::Sequencer sequencer;
    nb::Pattern patternCopy;
    uint32_t seenVersion = 0;
    std::vector<nb::SeqEvent> events;
    std::vector<float> tapScratch;
    double sampleRate = 48000.0;
    double internalPpq = 0.0;
    bool wasRunning = false;

    // Key mode
    int heldKeys = 0, keySemis = 0;
    double keyPpq = 0.0;

    // MIDI out note-offs
    struct PendingOff { int note = -1; int samplesLeft = 0; };
    std::array<PendingOff, 128> pendingOffs {};

    std::array<std::atomic<float>, kPads> pendingAudition {};
    std::array<std::atomic<bool>, kPads> useSample {};
    std::array<std::shared_ptr<const nb::SampleData>, kPads> samples {};   // message-thread owner
    std::array<std::shared_ptr<const nb::SampleData>, kPads> liveSamples {}; // read with atomic_load on the audio thread
    std::array<float, kPads> sampleRms {};
    std::vector<std::shared_ptr<const nb::SampleData>> keepAlive;          // never freed on the audio thread
    juce::AudioFormatManager formats;

    // cached parameter pointers
    struct PadParams { std::atomic<float>* sound, * tune, * decay, * tone, * drive, * hollow, * evolve, * pan, * level, * choke, * mute; };
    std::array<PadParams, kPads> padParams {};
    std::atomic<float>* masterParam = nullptr, * swingParam = nullptr, * tempoParam = nullptr, * modeParam = nullptr, * runParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeoBeatProcessor)
};
