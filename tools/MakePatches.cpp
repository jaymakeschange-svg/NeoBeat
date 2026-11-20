// Make a real .NJQ and a real .NJQi, the way Batchcut makes them, so MidiJaQ can be tested
// against the files it will actually be given rather than against something a test invented.
//
// Everything here goes through the processor's own paths: sounds are gathered into the pool,
// the kit auto-fills its lanes, the instrument is built across the keys, and both are saved
// with kitSaveNjq and keysSaveNjq. Nothing writes a file by hand.
//
//   MakePatches <output folder>
//
// leaves kit.NJQ and keys.NJQi there, and prints what went into them.

#include "app/Processor.h"
#include "TestAudio.h"
#include <cstdio>
#include <memory>

static void pump (BcProcessor& p, int ms)
{
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    const int blocks = std::max (1, ms * 48000 / (512 * 1000));
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        p.processBlock (buf, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
}

static bool waitIdle (BcProcessor& p, int maxMs = 180000)
{
    int idle = 0;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) maxMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        pump (p, 25);
        idle = p.session.status().busy ? 0 : idle + 1;
        if (idle >= 4) return true;       // quiet for four turns running, not merely this instant
    }
    return false;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File outDir (argc > 1 ? juce::String (argv[1])
                                      : juce::File::getCurrentWorkingDirectory().getChildFile ("patches").getFullPathName());
    outDir.createDirectory();

    const auto work = outDir.getChildFile ("source");
    work.createDirectory();

    // the recordings Batchcut would be pointed at
    const auto field = testaudio::field (work), loop = testaudio::loop (work), take = testaudio::longTake (work);
    if (! (field.file.existsAsFile() && loop.file.existsAsFile() && take.file.existsAsFile()))
    {
        std::printf ("could not write the test recordings\n");
        return 1;
    }

    auto proc = std::make_unique<BcProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->session.settings.outDir = outDir.getChildFile ("out").getFullPathName().toStdString();

    if (proc->addPaths ({ work.getFullPathName() }) < 1) { std::printf ("nothing was added\n"); return 1; }
    if (! waitIdle (*proc)) { std::printf ("analysing never finished\n"); return 1; }

    // the pool, from every file that was brought in
    proc->kitGather (true);
    if (! waitIdle (*proc)) { std::printf ("gathering never finished\n"); return 1; }
    pump (*proc, 200);
    if (proc->kit.pool.empty()) { std::printf ("the pool is empty\n"); return 1; }

    // ---------------------------------------------------------------- the kit, as a .NJQ
    proc->kitChoose();
    proc->kitSetPattern (3);
    int lanes = 0;
    for (const auto& ln : proc->kit.lanes) if (! ln.empty()) ++lanes;

    const auto njq = outDir.getChildFile ("kit.NJQ");
    juce::String err;
    if (! proc->kitSaveNjq (njq, err)) { std::printf ("the kit would not save: %s\n", err.toRawUTF8()); return 1; }

    // ---------------------------------------------------------------- the instrument, as a .NJQi
    proc->keysBuild();
    if (proc->keys.zones.empty())
    {
        // whatever was gathered had no clear pitch; the resonator tunes anything to the keys
        proc->keys.mode = bc::KeysMode::Resonator;
        proc->keysBuild();
    }
    if (proc->keys.zones.empty()) { std::printf ("the instrument came out empty\n"); return 1; }

    // --compact keeps a handful of zones, for a fixture small enough to live in a repository and
    // be opened by CI. The sounds and the layout are Batchcut's own either way.
    const bool compact = argc > 2 && juce::String (argv[2]) == "--compact";
    if (compact && proc->keys.zones.size() > 6)
    {
        std::vector<njq::ZoneT<bc::PoolItem>> kept;
        const size_t step = proc->keys.zones.size() / 6;
        for (size_t i = 0; i < proc->keys.zones.size() && kept.size() < 6; i += std::max<size_t> (1, step))
            kept.push_back (proc->keys.zones[i]);
        // the kept zones stretch to cover the keyboard between them, so every key still answers
        for (size_t i = 0; i < kept.size(); ++i)
        {
            kept[i].lowKey = i == 0 ? 0 : kept[i - 1].highKey + 1;
            kept[i].highKey = (i + 1 == kept.size()) ? 127
                            : juce::jlimit (kept[i].lowKey, 127, (int) std::lround (kept[i].root) + 6);
        }
        proc->keys.zones = std::move (kept);
        proc->keysChanged();
    }

    const auto njqi = outDir.getChildFile ("keys.NJQi");
    if (! proc->keysSaveNjq (njqi, err)) { std::printf ("the instrument would not save: %s\n", err.toRawUTF8()); return 1; }

    int lowest = 128, highest = -1;
    for (const auto& z : proc->keys.zones) { lowest = std::min (lowest, z.lowKey); highest = std::max (highest, z.highKey); }

    std::printf ("pool            %d sounds\n", (int) proc->kit.pool.size());
    std::printf ("%-15s %d lanes, %.0f bytes\n", njq.getFileName().toRawUTF8(), lanes, (double) njq.getSize());
    std::printf ("%-15s %d zones over keys %d..%d, %.0f bytes\n", njqi.getFileName().toRawUTF8(),
                 (int) proc->keys.zones.size(), lowest, highest, (double) njqi.getSize());
    std::printf ("ok\n");
    return 0;
}
