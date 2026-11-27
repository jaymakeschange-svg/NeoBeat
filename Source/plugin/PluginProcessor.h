// NeoBeat 3.0 — the plugin processor: host parameters, the scenes, the project, files, presets, undo,
// import, export and state. The sound is the framework-free Workstation; this class feeds it and keeps
// it in sync.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/Export.h"
#include "../engine/Factory.h"
#include "../engine/Kits.h"
#include "../engine/Workstation.h"
#include "../shared/Learner.h"
#include "../shared/Njq.h"
#include "../shared/Chaos.h"

// The four scenes the editor edits and the audio thread plays, shared without locks.
struct PatternStore
{
    static constexpr int kPerScene = nb::Pattern::kTracks * nb::Pattern::kMaxSteps;
    std::vector<std::atomic<uint32_t>> cells = std::vector<std::atomic<uint32_t>> ((size_t) (nb::kNumScenes * kPerScene));
    std::array<std::atomic<int>, nb::kNumScenes> lengths;
    std::array<std::atomic<uint32_t>, nb::kNumScenes> sceneVersion;
    std::atomic<uint32_t> version { 1 };
    std::atomic<int> edit { 0 };      // the scene the editor shows and edits

    PatternStore() { for (auto& l : lengths) l.store (32); for (auto& v : sceneVersion) v.store (1); }

    static uint32_t pack (nb::Step s) noexcept { return (uint32_t) s.vel | ((uint32_t) s.roll << 8) | ((uint32_t) (uint8_t) s.nudge << 16); }
    static nb::Step unpack (uint32_t v) noexcept
    {
        nb::Step s; s.vel = (uint8_t) (v & 0xff); s.roll = (uint8_t) ((v >> 8) & 0xff); s.nudge = (int8_t) (uint8_t) ((v >> 16) & 0xff);
        return s;
    }
    static size_t at (int scene, int track, int step) noexcept { return (size_t) (scene * kPerScene + track * nb::Pattern::kMaxSteps + step); }
    void touch (int scene) noexcept { sceneVersion[(size_t) scene].fetch_add (1, std::memory_order_release); version.fetch_add (1, std::memory_order_release); }

    // ---- the scene being edited
    int scene() const noexcept { return edit.load (std::memory_order_relaxed); }
    int length() const noexcept { return lengths[(size_t) scene()].load(); }
    nb::Step get (int track, int step) const noexcept { return getIn (scene(), track, step); }
    void set (int track, int step, nb::Step s) noexcept { setIn (scene(), track, step, s); }
    void setLength (int len) noexcept { setLengthIn (scene(), len); }
    void load (const nb::Pattern& p) noexcept { loadScene (scene(), p); }
    void snapshot (nb::Pattern& p) const noexcept { snapshotScene (scene(), p); }

    // ---- any scene
    nb::Step getIn (int scene, int track, int step) const noexcept { return unpack (cells[at (scene, track, step)].load (std::memory_order_relaxed)); }
    void setIn (int scene, int track, int step, nb::Step s) noexcept { cells[at (scene, track, step)].store (pack (s), std::memory_order_relaxed); touch (scene); }
    int lengthOf (int scene) const noexcept { return lengths[(size_t) scene].load(); }
    void setLengthIn (int scene, int len) noexcept { lengths[(size_t) scene].store (juce::jlimit (1, nb::Pattern::kMaxSteps, len)); touch (scene); }
    void loadScene (int scene, const nb::Pattern& p) noexcept
    {
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
            for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
                cells[at (scene, t, s)].store (pack (p.steps[(size_t) t][(size_t) s]), std::memory_order_relaxed);
        lengths[(size_t) scene].store (juce::jlimit (1, nb::Pattern::kMaxSteps, p.length));
        touch (scene);
    }
    void snapshotScene (int scene, nb::Pattern& p) const noexcept
    {
        p.length = lengths[(size_t) scene].load();
        for (int t = 0; t < nb::Pattern::kTracks; ++t)
            for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
                p.steps[(size_t) t][(size_t) s] = getIn (scene, t, s);
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
    static constexpr int kRooms = 5;      // SEQUENCER, MATRIXXXER, LAYERMAX, KEY MAP, JACK
    static constexpr int kJackRoom = 4;

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
    void loadPreset (int index);          // one of the 128 patterns, into the scene being edited
    void loadKit (int index);
    void audition (int pad, float velocity) noexcept { ws->audition (pad, velocity); }
    void auditionNote (int note, float velocity) noexcept { ws->auditionNote (note, velocity); }
    void midiTest (int lane) noexcept { ws->midiTest (lane); }
    bool loadSample (int pad, const juce::File& file);
    void clearSample (int pad);
    juce::String padName (int pad) const;
    juce::String padGroup (int pad) const;
    bool padUsesSample (int pad) const noexcept { return useSample[(size_t) pad].load(); }
    juce::String paramId (int pad, const char* name) const { return "p" + juce::String (pad + 1) + "_" + name; }
    void setParam (const juce::String& id, float value);
    float getParam (const juce::String& id) const;

    // scenes: the one shown is the one played (unless the chain runs)
    int scene() const noexcept { return pattern.scene(); }
    void setScene (int s);
    void copyScene (int from, int to);
    void clearScene (int s);

    // the project: edit it, then call projectChanged()
    nb::Project project;
    void projectChanged();
    std::map<std::string, std::shared_ptr<const nb::SampleData>> files;
    std::shared_ptr<const nb::SampleData> loadFile (const juce::File& f);
    int loadGridFolder (const juce::File& folder);   // returns how many samples
    nb::RenderService& renderer() noexcept { return ws->render(); }
    nb::Workstation& workstation() noexcept { return *ws; }
    void snapshotPattern (nb::Pattern& p) const noexcept { pattern.snapshot (p); }
    std::unique_ptr<nb::RenderInputs> renderInputs() const;

    // ---- undo: one history per room. Call undoCommit after every gesture; what changed since the last
    // call becomes one step in that room's history.
    void undoCommit (int room);
    bool canUndo (int room) const { return room >= 0 && room < kRooms && ! history[(size_t) room].undo.empty(); }
    bool canRedo (int room) const { return room >= 0 && room < kRooms && ! history[(size_t) room].redo.empty(); }
    juce::String undoLabel (int room) const { return canUndo (room) ? history[(size_t) room].undo.back().label : juce::String(); }
    juce::String redoLabel (int room) const { return canRedo (room) ? history[(size_t) room].redo.back().label : juce::String(); }
    void undo (int room);
    void redo (int room);

    // ---- the global randomiser. Spec: docs/DECISIONS.md, "B - the global randomiser".
    //
    // One press rolls every included pad once: all of its own parameters, and the parameters of
    // the effects on its lane. It writes values, deliberately - the randomiser is a destination,
    // for auditioning: press, listen, press again. Each press is one undo step in the JACK room, so
    // rolling past something good costs nothing.
    //
    // Its target list is sixteen pads, one action each, and the same list is IDM Jack's include
    // list: choosing a pad puts all of its parameters in. Jack does NOT use roll() or write
    // anything - the spec requires its parameter chaos to be a per-hit overlay at playback.
    nsr::Chaos chaos;
    void roll();
    void refreshPadTargetNames();                  // pad names follow the sounds loaded on them
    bool padIncluded (int pad) const noexcept;
    void includeAll (bool on);

    // ---- BANDS, the spectral delay on a send. Decisions: docs/DECISIONS.md, "BANDS in NeoBeat"
    // and "Next in the same build". Each lane's send is the host parameter p<N>_bands; ALL is
    // "bands_all". The bank's own settings live here, set by the BANDS window.
    bc::BandsState bandsState;
    void bandsApply() { ws->bandsSettings.write (bandsState); }
    bool bandsRunning() const noexcept { return ws->bandsRunning.load (std::memory_order_relaxed); }

    // ---- IDM Jack. Spec: docs/PROPOSED-FEATURES.md, "IDM Jack"; decisions: docs/DECISIONS.md.
    //
    // The engine is in the workstation, on the audio thread. Here: the panel's settings, and the
    // pass that will play next. ON is the host parameter "jack", so a controller or automation can
    // hold it; the audio thread reads it every block, so engaging is never late by a timer tick.
    // The next pass - its seed, and its settings after self-randomise - is prepared ahead, when the
    // previous one is let go, so it is already waiting when ON arrives.
    nsr::JackSettings jack;                         // what the panel shows
    nsr::JackSettings jackPlaying;                  // what plays: `jack`, self-randomised for this pass
    bool jackLockSeed = false;                      // off: each switch-on a new pass; on: the same again
    void jackApply();                               // settings and include list to the audio thread
    void jackSetHeld (bool on);                     // the panel's ON (sets the host parameter)
    bool jackHeldParam() const noexcept { return jackParam != nullptr && jackParam->load() > 0.5f; }
    int jackState() const noexcept { return ws->jackState.load (std::memory_order_relaxed); }
    int jackBursts() const noexcept { return ws->jackBursts.load (std::memory_order_relaxed); }
    int jackSeized() const noexcept { return ws->jackSeized.load (std::memory_order_relaxed); }
    uint32_t jackMask() const noexcept;

    // ---- .NJQ: a kit and its sequence in one file
    bool saveNjq (const juce::File& file, const juce::String& name, juce::String& error);
    bool loadNjq (const juce::File& file, juce::String& error);
    juce::Array<juce::File> njqFiles() const;         // the preset folder's
    juce::String currentNjq;                           // the last one saved or loaded (for "make this a default?")
    std::atomic<double> suggestedBpm { 0.0 };          // a loaded kit's tempo, when the host keeps its own
    // "make this a default sequence": into the preset folder (so it is listed), and optionally what a new NeoBeat opens with
    bool makeDefault (const juce::File& njq, bool startWithIt, juce::String& error);

    // ---- a folder of samples into the lanes
    struct ImportReport { int found = 0, lanes = 0, rows = 0; juce::StringArray lines; };
    ImportReport importFolder (const juce::File& folder, bool pickMostMusical);
    void matchLevel (int pad);                         // bring a lane's sample to the stock sounds' loudness
    void rateLane (int pad, float stars);              // teach the learner
    void teachLane (int pad);                          // "this sound belongs on this lane" (you put it there)
    float laneRating (int pad);
    static nbk::Learner& learner();                    // shared with Batchcut through one file
    static void saveLearner();

    // ---- export the sequencer's pattern
    struct ExportOptions
    {
        juce::File folder; juce::String name = "NeoBeat";
        nb::ExportRange range;
        bool wav = true, stems = false, midi = true, straightMidi = false;
    };
    bool startExport (const ExportOptions&);
    bool exporting() const noexcept { return exportBusy.load(); }
    float exportProgress() const noexcept { return exportDone.load(); }
    juce::String exportResult() const { const juce::ScopedLock l (exportLock); return exportMessage; }
    juce::StringArray exportedFiles() const { const juce::ScopedLock l (exportLock); return exportFiles; }

    // user presets ("keep their samples": referenced files are copied next to the preset)
    static juce::File userRoot();
    juce::StringArray userPresets (const juce::String& section) const;
    bool saveUserPreset (const juce::String& section, const juce::String& name);
    bool loadUserPreset (const juce::String& section, const juce::String& name);

    juce::AudioProcessorValueTreeState apvts;
    PatternStore pattern;
    const nb::Library& library() const noexcept { return ws->kit().lib(); }
    std::atomic<int> currentPreset { 0 }, currentKit { 0 }, playStep { -1 }, playScene { 0 }, queuedScene { -1 }, playLink { -1 }, selectedPad { 0 };
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> auditionRun { false };           // the preset bar's audition: NeoBeat's own transport, briefly
    std::atomic<bool> hostSynced { false };
    uint32_t hitCount (int pad) const noexcept { return ws->kit().hitCounter[(size_t) pad].load (std::memory_order_relaxed); }
    uint32_t lateHits() const noexcept { return ws->lateHits(); }

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
    void setPadSample (int pad, std::shared_ptr<const nb::SampleData> ptr);
    std::shared_ptr<const nb::SampleData> resolveSample (const juce::String& key);
    void publishLive();
    void registerPadTargets();
    void rollPad (int pad, gen::Rng& rng);

    // undo
    using Sections = std::map<juce::String, juce::MemoryBlock>;
    struct Entry { juce::String label; Sections before, after; };
    struct History { std::vector<Entry> undo, redo; };
    Sections captureSections();
    void applySections (const Sections&);
    std::array<History, kRooms> history;
    Sections baseline;
    bool baselineValid = false;

    std::vector<nb::Pattern> sceneCopy = std::vector<nb::Pattern> ((size_t) nb::kNumScenes);
    std::array<uint32_t, nb::kNumScenes> seenScene {};
    std::vector<nb::MidiIn> midiIn;
    std::vector<nb::MidiOut> midiOut;
    std::vector<float> tapScratch;
    double sampleRate = 48000.0;
    int maxBlock = 512;
    bool prepared = false;

    std::atomic<uint64_t> projectVersion { 1 };
    uint64_t publishedVersion = 0, submittedHash = 0;
    juce::uint32 lastSubmit = 0;
    int ticks = 0;
    std::atomic<bool> stateRestored { false };
    bool startupDone = false;

    std::array<std::atomic<bool>, kPads> useSample {};
    std::array<std::shared_ptr<const nb::SampleData>, kPads> samples {};    // message-thread owner
    std::array<std::shared_ptr<const nb::SampleData>, kPads> liveSamples {}; // read with atomic_load on the audio thread
    std::array<std::atomic<float>, kPads> sampleRms {};
    std::vector<std::shared_ptr<const nb::SampleData>> keepAlive;           // never freed on the audio thread
    juce::AudioFormatManager formats;

    struct PadParams { std::atomic<float>* sound, * tune, * decay, * tone, * drive, * hollow, * evolve, * pan, * level, * choke, * mute, * width, * bands; };
    std::array<PadParams, kPads> padParams {};
    std::atomic<float>* masterParam = nullptr, * swingParam = nullptr, * tempoParam = nullptr, * modeParam = nullptr, * runParam = nullptr, * sceneParam = nullptr, * jackParam = nullptr, * bandsAllParam = nullptr;
    int jackSeenState = 0;
    uint64_t jackPasses = 0;
    void jackNextPass();

    // export (its own thread)
    struct ExportThread;
    std::unique_ptr<ExportThread> exportThread;
    std::atomic<bool> exportBusy { false };
    std::atomic<float> exportDone { 0.0f };
    juce::CriticalSection exportLock;
    juce::String exportMessage;
    juce::StringArray exportFiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NeoBeatProcessor)
};
