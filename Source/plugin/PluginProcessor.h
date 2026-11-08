// NeoBeat 2.0 — the plugin processor: host parameters, the pattern, the project, files, presets and
// state. The sound is the framework-free Workstation; this class feeds it and keeps it in sync.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Factory.h"
#include "../engine/Kits.h"
#include "../engine/Workstation.h"

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
    void setLength (int len) noexcept { length.store (juce::jlimit (1, nb::Pattern::kMaxSteps, len)); version.fetch_add (1, std::memory_order_release); }
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

class NeoBeatProcessor final : public juce::AudioProcessor, private juce::Timer
{
    // Declared first: the parameter layout reads the sound library it builds.
    std::unique_ptr<nb::Workstation> ws;

public:
    static constexpr int kPads = nb::kNumPads;
    static constexpr int kPadNoteBase = 36;
    static constexpr int kKeyRoot = 60;

    NeoBeatProcessor();
    ~NeoBeatProcessor() override;

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
    int getNumPrograms() override { return nb::numPatterns(); }
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override { loadPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the editor (message thread)
    void loadPreset (int index);
    void loadKit (int index);
    void audition (int pad, float velocity) noexcept { ws->audition (pad, velocity); }
    void auditionNote (int note, float velocity) noexcept { ws->auditionNote (note, velocity); }
    bool loadSample (int pad, const juce::File& file);
    void clearSample (int pad);
    juce::String padName (int pad) const;
    juce::String padGroup (int pad) const;
    bool padUsesSample (int pad) const noexcept { return useSample[(size_t) pad].load(); }
    juce::String paramId (int pad, const char* name) const { return "p" + juce::String (pad + 1) + "_" + name; }
    void setParam (const juce::String& id, float value);

    // the project: edit it, then call projectChanged()
    nb::Project project;
    void projectChanged() noexcept { ++projectVersion; }
    std::map<std::string, std::shared_ptr<const nb::SampleData>> files;
    std::shared_ptr<const nb::SampleData> loadFile (const juce::File& f);
    int loadGridFolder (const juce::File& folder);   // returns how many samples
    nb::RenderService& renderer() noexcept { return ws->render(); }
    nb::Workstation& workstation() noexcept { return *ws; }
    void snapshotPattern (nb::Pattern& p) const noexcept { pattern.snapshot (p); }

    // user presets ("keep their samples": referenced files are copied next to the preset)
    static juce::File userRoot();
    juce::StringArray userPresets (const juce::String& section) const;
    bool saveUserPreset (const juce::String& section, const juce::String& name);
    bool loadUserPreset (const juce::String& section, const juce::String& name);

    juce::AudioProcessorValueTreeState apvts;
    PatternStore pattern;
    const nb::Library& library() const noexcept { return ws->kit().lib(); }
    std::atomic<int> currentPreset { 0 }, currentKit { 0 }, playStep { -1 }, selectedPad { 0 };
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> hostSynced { false };
    uint32_t hitCount (int pad) const noexcept { return ws->kit().hitCounter[(size_t) pad].load (std::memory_order_relaxed); }

    // Mono tap of the selected pad, for the editor's displays.
    juce::AbstractFifo tapFifo { 16384 };
    std::vector<float> tapBuffer = std::vector<float> (16384, 0.0f);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const nb::Library& lib);
    void readParameters() noexcept;
    void pushTap (const float* data, int n) noexcept;
    void timerCallback() override;
    std::array<nb::PadSettings, kPads> padSettingsNow() const;
    void reloadProjectFiles();
    std::unique_ptr<juce::XmlElement> sectionXml (const juce::String& section);
    void applySectionXml (const juce::String& section, const juce::XmlElement& e);

    nb::Pattern patternCopy;
    uint32_t seenVersion = 0;
    std::vector<nb::MidiIn> midiIn;
    std::vector<nb::MidiOut> midiOut;
    std::vector<float> tapScratch;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;

    std::atomic<uint64_t> projectVersion { 1 };
    uint64_t pendingHash = 0, submittedHash = 0;
    juce::uint32 pendingSince = 0;

    std::array<std::atomic<bool>, kPads> useSample {};
    std::array<std::shared_ptr<const nb::SampleData>, kPads> samples {};    // message-thread owner
    std::array<std::shared_ptr<const nb::SampleData>, kPads> liveSamples {}; // read with atomic_load on the audio thread
    std::array<std::atomic<float>, kPads> sampleRms {};
    std::vector<std::shared_ptr<const nb::SampleData>> keepAlive;           // never freed on the audio thread
    juce::AudioFormatManager formats;

    struct PadParams { std::atomic<float>* sound, * tune, * decay, * tone, * drive, * hollow, * evolve, * pan, * level, * choke, * mute; };
    std::array<PadParams, kPads> padParams {};
    std::atomic<float>* masterParam = nullptr, * swingParam = nullptr, * tempoParam = nullptr, * modeParam = nullptr, * runParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeoBeatProcessor)
};
