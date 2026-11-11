// The stand-alone effect plugins: one processor, compiled twice (NB_FX_KIND 0 = Friction Stretch,
// 1 = Bounce Delay). Every knob is a host parameter; the lines of Friction Stretch and the loaded hit
// travel in the state.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "FxCore.h"

#ifndef NB_FX_KIND
 #define NB_FX_KIND 0
#endif

namespace nbfx
{
// how a parameter reads on its knob
enum class Unit { Plain, Percent, Ms, Times, Hz, Metres, Seconds, Count, Division, Body, GateBeats, GateMs, Db, Pan, Place };
// when it is shown: always, only with the Tempo switch on, only with it off
enum class When { Always, Tempo, Millis };

struct ParamDef
{
    const char* id; const char* label;
    float lo, hi, def;
    bool log;
    int page;          // the window page it sits on; -1: placed by hand (the side column)
    Unit unit;
    When when = When::Always;
    bool main = false;
};

const std::vector<std::pair<const char*, double>>& divisions();
int divisionIndex (double beats);
const std::vector<ParamDef>& paramDefs();        // this plugin's knobs
juce::StringArray pageNames();
juce::String formatValue (Unit u, float v);
}

class FxProcessor final : public juce::AudioProcessor, private juce::Timer
{
public:
    FxProcessor();
    ~FxProcessor() override;

    static constexpr nbfx::Kind kKind = NB_FX_KIND == 0 ? nbfx::Kind::Friction : nbfx::Kind::Bounce;
    static bool isFriction() { return kKind == nbfx::Kind::Friction; }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return isFriction() ? "Friction Stretch" : "Bounce Delay"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 24.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the editor (message thread)
    juce::AudioProcessorValueTreeState apvts;
    nbfx::Core& engine() { return *core; }
    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    void setValue (const char* id, float v);
    bool loadFile (const juce::File&);
    void useDefaultHit();
    bool keepLastCapture();              // the last caught hit becomes the loaded hit
    bool saveShown (const juce::File&);  // what the display shows, as a WAV
    juce::String hitName() const { return hit ? juce::String (hit->path) : juce::String ("no hit"); }
    nbfx::HitPtr currentHit() const { return hit; }
    nbfx::Settings settingsNow() const;
    std::vector<nb::StretchLine> lines;  // Friction Stretch's lines
    void linesChanged() { ++lineVersion; }
    std::atomic<double> displayBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();

    std::unique_ptr<nbfx::Core> core;
    nbfx::HitPtr hit;
    bool hitIsBuiltIn = true;
    int program = 0;
    uint64_t lineVersion = 0, pendingHash = 0, submittedHash = 0;
    int stableTicks = 0;
    std::atomic<double> stepBeats { 0.25 };
    std::vector<nbfx::Midi> midiIn;
    struct Raw { std::atomic<float>* mode, *dry, *thru, *level, *threshold, *capture, *retrig, *keytrack, *choke, *loop; } raw {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxProcessor)
};
