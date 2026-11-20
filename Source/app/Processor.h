#pragma once
// MidiJaQ's plugin: it holds the line, hands the audio thread a snapshot of it, and turns what the
// player sends into MIDI. The editor only ever touches the line and calls lineChanged().
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>

#include "../neobeat/shared/rooms/Rooms.h"
#include "../neobeat/shared/StereoRoom.h"
#include "../core/Map.h"
#include "../core/Inst.h"
#include "../neobeat/dsp/LiveFx.h"
#include "../core/Player.h"
#include "../core/Session.h"

class MjqProcessor final : public juce::AudioProcessor, public bc::Rooms, public juce::ChangeBroadcaster
{
public:
    MjqProcessor();
    ~MjqProcessor() override = default;

    // ---- the line, and the snapshot the audio thread reads
    mjq::Line line;                                  // the selected line, on the message thread
    mjq::Session song;                             // 128 slots, four lines, chord track
    mjq::Player player;                               // line 1, kept for the original public API
    std::array<mjq::Player, mjq::kSessionTracks - 1> otherPlayers;
    std::array<mjq::InstrumentPlayer, mjq::kSessionTracks - 1> otherVoices;
    void setInstrumentLevel (float level);

    // ---- the Stereoroom: one stage, every chop standing somewhere on it
    nsr::Stage stage;
    std::vector<nsr::Place> places;              // one per node, in the order the room builds them
    void stereoChanged();                        // the places changed: the audio thread takes a new copy
    double stageBeats() const noexcept;          // where the song is, so a travelling thing knows where it is

    // ---- MAP: one grid, two kinds of mark
    mjq::MapPattern map;
    void roomsNotify() override { sendChangeMessage(); }
    void mapChanged();                           // the marks changed: the player takes a new copy
    void mapWrite();                             // a phrase in the key, from whatever is loaded

    // ---- the Orb, for the MOD room
    const mjq::Orb& orbFor (int track) const noexcept
        { return song.orbs[(size_t) juce::jlimit (0, mjq::kSessionTracks - 1, track)]; }
    void scatterOrb() { scatterOrb (chosenTrack()); }
    void clearOrb();
    void setOrbGrip (float grip);

    // ---- RANDOM: one roll that reaches every room
    struct RandomTarget
    {
        const char* room;
        const char* name;
        float reach = 0.4f;
        bool on = false;
        bool never = false;      // a mono lock, the low end, or the randomiser itself
    };
    const std::vector<RandomTarget>& randomTargets() const noexcept { return randTargets; }
    void toggleRandomTarget (int index);
    void rollRandom();
    void holdRandom();
    int heldRolls() const noexcept { return (int) held.size(); }

    bool randomOnKey = false, randomOnGrid = false, randomFree = false;
    int randomEvery = 4;
    float randomAmount = 0.45f;

    struct StereoState
    {
        nsr::Stage stage;
        std::vector<nsr::Place> places;
    };
    std::shared_ptr<const StereoState> stereoState;   // what the audio thread is using
    std::shared_ptr<const mjq::MapPattern> mapState;  // the marks the player is reading
    std::vector<RandomTarget> randTargets;
    std::vector<mjq::MapPattern> held;                // rolls kept with HOLD
    int sinceRoll = 0;
    int chosenSlot() const noexcept { return song.selected; }
    int chosenTrack() const noexcept { return song.selectedTrack; }
    void chooseSlot (int index);
    void chooseTrack (int index);
    void changeWriter (mjq::Generator);
    void changeChord (int bar, const mjq::Chord&);
    void changeChain (int length);
    void changeTrackEnabled (bool enabled);
    bool savePatterns (const juce::File&, juce::String& error);
    bool loadPatterns (const juce::File&, juce::String& error);
    void rebuildSession (bool full = false);
    mjq::ClockMode clockMode = mjq::ClockMode::Pattern;
    std::atomic<int> version { 0 };                  // the editor redraws when this moves
    juce::String message;                            // one line under the window

    // ---- the multi: every track plays an instrument of its own (.NJQi, or a .NJQ from Batchcut's KEYS
    // room), over the stretch of keys that track answers to. Ranges may overlap, so one key can start a
    // piano figure and a bassline at once.
    mjq::InstrumentPlayer voices;
    juce::String instrumentFile;                     // track 0's, kept for the original public API
    bool openInstrument (const juce::File&, juce::String& error);                  // the chosen track
    bool openInstrument (int track, const juce::File&, juce::String& error);
    void closeInstrument();                                                        // the chosen track
    void closeInstrument (int track);
    std::shared_ptr<const mjq::Instrument> instrument() const { return instrument (0); }
    std::shared_ptr<const mjq::Instrument> instrument (int track) const
    {
        return std::atomic_load (&loadedTracks[(size_t) juce::jlimit (0, mjq::kSessionTracks - 1, track)]);
    }
    juce::String instrumentFileFor (int track) const;
    // the voice player of a track (track 0 is `voices`, kept for the original public API)
    mjq::InstrumentPlayer& voicesFor (int track) noexcept
    {
        track = juce::jlimit (0, mjq::kSessionTracks - 1, track);
        return track == 0 ? voices : otherVoices[(size_t) (track - 1)];
    }
    void changeTrackRange (int track, int lowKey, int highKey);
    void changeTrackLevel (int track, float level);
    void setTrackMode (int track, bool layer, int follows);
    void setTrackStack (int track, int semis, float cents, float delayMs);
    void releaseTrack (int track);

    // ---- the Orb. One joystick moves every voice's Orb at once, so a single gesture takes the whole multi
    // somewhere else. Each voice has eight corners of its own.
    void moveOrb (float x, float y);                               // the joystick, in a circle of radius 1
    void setOrbOn (int track, bool on);
    void putOrbCorner (int track, int corner);                     // this voice's line, as it stands, goes there
    void clearOrbCorner (int track, int corner);
    void scatterOrb (int track);          // fill the eight corners with variations of the line as it stands
    void recallOrbCorner (int track, int corner);                  // the joystick travels to that corner
    mjq::OrbBlend orbBlend (int track) const;
    // where the music has got to, for anything on screen that moves in time with it
    double displayBeat() const noexcept { return shownBeat.load (std::memory_order_relaxed); }

    // How many notes the plugin has actually played. The one honest measure of what TRAFFIC and the
    // writers are doing, and what a test should look at rather than guessing from the level.
    std::atomic<uint64_t> notesPlayed { 0 };

    void lineChanged();                              // rebuild the snapshot and bump the version
    void roll();                                     // JAQ: a new family from a new seed
    void rollLane (mjq::Lane lane);                  // one lane again, the rest as they are
    void mark();                                     // remember the line, for undo
    bool undo();
    bool redo();
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    std::shared_ptr<const mjq::Pass> pass() const { return std::atomic_load (&shown); }
    int stepNow() const noexcept { return lastStep.load(); }

    // ---- the plugin
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MidiJaQ"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }   // MidiJaQ is an instrument; a Max for Live device sends MIDI
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::String toXml (const mjq::Line&, mjq::ClockMode, const juce::String& instrumentFile);
    static bool fromXml (const juce::String&, mjq::Line&, mjq::ClockMode&, juce::String& instrumentFile);

private:
    struct Prepared
    {
        mjq::Session state;
        std::array<std::array<mjq::Pass, mjq::kSessionTracks>, mjq::kSessionSlots> passes {};
        std::array<std::array<std::shared_ptr<const std::vector<mjq::Pass>>, mjq::kSessionTracks>, mjq::kSessionSlots> evolving {};
    };
    std::shared_ptr<const Prepared> prepared;
    std::shared_ptr<const mjq::Pass> shown;          // editor's preview of the chosen line
    mutable std::array<std::shared_ptr<const mjq::Instrument>, mjq::kSessionTracks> loadedTracks;

    // The Orb's effect stack. Each voice gets its own chain, so a multi can have one voice dry and another
    // through four effects. These are NeoBeat's own real-time effects, shared rather than written again.
    nb::LiveFxPools fxPools;
    std::array<juce::AudioBuffer<float>, mjq::kSessionTracks> voiceBuf;
    std::array<std::array<nb::LiveFx, mjq::kOrbStack>, mjq::kSessionTracks> orbFx;
    std::array<std::array<mjq::OrbFx, mjq::kOrbStack>, mjq::kSessionTracks> orbFxLast {};
    std::atomic<double> shownBeat { 0.0 };
    // Where the music has got to, counted from when the plugin started. With a host this follows its
    // transport; without one it keeps counting on its own. The player's own position runs from the start of
    // each pass and goes back to nothing every bar, so anything that must not repeat every bar - a cycling
    // knob, a drawn curve in the room - has to be told the time from here instead.
    // A layer can sit a few milliseconds behind the voice it follows, which at a small block
    // size puts its note past the end of this block. Those wait here for the next one. Fixed
    // size on purpose: the audio thread must not allocate.
    static constexpr int kCarry = 256;
    mjq::PlayNote layerCarry[mjq::kSessionTracks][kCarry] {};
    int numCarry[mjq::kSessionTracks] {};

    int directDown = 0;      // keys the player is holding while the song is stopped

    double freeBeats = 0.0;
    double hostBeatsPerSample() const noexcept { return lastBpm / (60.0 * (hostSampleRate > 0.0 ? hostSampleRate : 48000.0)); }
    double lastBpm = 120.0;
    uint64_t orbFxVersion = 0;
    juce::AudioBuffer<float> fxScratch;

    // the stereo room, per voice: the air filter, and where it stood last block so it glides rather than zips
    std::array<mjq::RoomAir, mjq::kSessionTracks> roomAir;
    std::array<mjq::RoomGain, mjq::kSessionTracks> roomWas {};
    std::array<bool, mjq::kSessionTracks> roomReady {};
    std::vector<std::shared_ptr<const mjq::Instrument>> keep;   // the one just replaced, until the audio thread is past it
    double keepSince = 0.0;
    std::atomic<int> lastStep { -1 };
    double hostSampleRate = 48000.0;
    int playingSlot = -1;
    struct History { int slot, track; mjq::Line line; };
    std::vector<History> undoStack, redoStack;
    mjq::Line marked;
    bool hasMark = false;
    juce::Random seeds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MjqProcessor)
};
