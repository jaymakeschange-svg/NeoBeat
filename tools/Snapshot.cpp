// BatchCut snapshots: the editor rendered to PNGs in typical states (a UI check under Xvfb).
// Usage: BatchcutSnapshot outdir
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "app/Editor.h"
#include "TestAudio.h"

static void pump (BcProcessor& p, int ms)
{
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end) { buf.clear(); p.processBlock (buf, midi); juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
}
static void settle (BcProcessor& p)
{
    int idle = 0;
    for (int i = 0; i < 2000 && idle < 4; ++i) { pump (p, 25); idle = p.session.status().busy ? 0 : idle + 1; }
    pump (p, 100);
}
static void shot (juce::Component& c, const juce::File& dir, const juce::String& name)
{
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::FileOutputStream os (dir.getChildFile (name + ".png"));
    os.setPosition (0); os.truncate();
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("  %s.png\n", name.toRawUTF8());
}

int main (int argc, char** argv)
{
    // (the learner and settings of the snapshots are their own)
    const auto sharedDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("batchcut_snapshots_shared");
    sharedDir.deleteRecursively(); sharedDir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8());
   #else
    setenv ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8(), 1);
   #endif
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "shots"));
    outDir.createDirectory();
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("batchcut_snapshots");
    dir.deleteRecursively(); dir.createDirectory();

    auto proc = std::make_unique<BcProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    std::unique_ptr<BcEditor> ed (dynamic_cast<BcEditor*> (proc->createEditor()));
    ed->setSize (1280, 800);
    pump (*proc, 200);
    shot (*ed, outDir, "01_empty");

    const auto field = testaudio::field (dir);
    const auto loop = testaudio::loop (dir);
    testaudio::longTake (dir);
    proc->session.settings.outDir = dir.getChildFile ("out").getFullPathName().toStdString();
    proc->addPaths ({ field.file.getFullPathName(), loop.file.getFullPathName(), dir.getChildFile ("long take.wav").getFullPathName() });
    settle (*proc);
    auto& v = ed->getView();
    shot (*ed, outDir, "02_yard_detect");

    // a selection, the events tab
    v.selectEvent (3, false);
    ed->getInspector().show (1);
    v.findMore (3);
    settle (*proc);
    v.setView (2.0, 9.0);
    v.tool = 0;
    const auto& e = v.doc->events[(size_t) std::min<size_t> (4, v.doc->events.size() - 1)];
    const double t = (double) e.onset / v.sr();
    v.select (bc::Shape::rect (t - 0.02, t + 0.45, v.doc->an->tab.yOf (400.0f), v.doc->an->tab.yOf (3000.0f)), 0);
    pump (*proc, 100);
    shot (*ed, outDir, "03_events_selection");

    // layers: the selection moved to a layer of its own, the background muted
    v.edit (bc::EditOp::Move, 1.0f, -2);
    if (auto* ed2 = v.edits()) ed2->layers.layers[bc::Background].mute = true;
    v.layersChanged();
    v.clearSelection();
    ed->getInspector().show (2);
    v.target = 4;
    v.refreshPanels();
    pump (*proc, 100);
    shot (*ed, outDir, "04_layers");
    v.target = -1;

    // export with matched loudness
    auto& st = proc->session.settings;
    st.exp.normMode = 2; st.exp.loudDb = -16.0f; st.exp.matchAll = true;
    ed->getInspector().show (3);
    proc->session.exportFiles (false);
    settle (*proc);
    v.refreshPanels();
    pump (*proc, 100);
    shot (*ed, outDir, "05_export");

    // the drum loop, zoomed in
    proc->open (1, 0);
    settle (*proc);
    v.setView (0.2, 4.2);
    v.selectEvent (2, false);
    ed->getInspector().show (0);
    pump (*proc, 100);
    shot (*ed, outDir, "06_loop_detect");

    // the long take (two parts), smaller window
    proc->open (2, 1);
    settle (*proc);
    ed->setSize (960, 600);
    pump (*proc, 150);
    shot (*ed, outDir, "07_long_take_part2_small");

    // the kit room: empty, then a kit out of every file's one-shots
    ed->setSize (1280, 800);
    proc->open (1, 0); settle (*proc);
    ed->showRoom (1); pump (*proc, 150);
    shot (*ed, outDir, "08_kit_empty");
    proc->kitGather (true); settle (*proc); settle (*proc);
    proc->kitChoose();
    proc->kit.name = "Yard kit";
    proc->kitAssign (2, 1, proc->kit.lanes[7].layers[0].item, false);
    proc->kit.lanes[2].layers[1].offsetMs = 18.0f; proc->kit.lanes[2].layers[1].semis = -4.0f; proc->kit.lanes[2].layers[1].start = 0.05f; proc->kit.lanes[2].layers[1].end = 0.7f;
    proc->kit.lanes[0].locked = true;
    proc->kitChanged(); proc->kitMark();
    proc->kitRate (proc->kit.lanes[2].layers[1].item, 4.0f); proc->kitRate (proc->kit.lanes[0].layers[0].item, 5.0f);
    ed->getView().kitLane = 2; ed->getView().kitLayer = 1; ed->getView().kitItem = proc->kit.lanes[2].layers[1].item;
    proc->kitSetPattern (7, true);
    proc->kitPlayer.playing.store (true);
    pump (*proc, 900);
    ed->getKitRoom().refresh(); pump (*proc, 60);
    shot (*ed, outDir, "09_kit_room");
    proc->kitPlayer.playing.store (false);
    ed->setSize (960, 600); pump (*proc, 100);
    shot (*ed, outDir, "10_kit_room_small");

    // LOOPS: an empty loop, then four layers rolled from the pool
    ed->setSize (1280, 800);
    ed->showRoom (2); pump (*proc, 150);
    shot (*ed, outDir, "11_loops_empty");
    auto& lp = proc->loops;
    lp.name = "Yard loop"; lp.bpm = 104.0; lp.root = 9; lp.scale = 10;
    lp.layers[0].mode = bc::LoopMode::Pool; lp.layers[0].bars = 2; lp.layers[0].density = 0.55f; lp.layers[0].dejavu = 0.7f; lp.layers[0].seed = 11;
    proc->loopsRoll (0, false);
    const int l1 = proc->loopsAddLayer(); lp.layers[(size_t) l1].mode = bc::LoopMode::Stutter; lp.layers[(size_t) l1].seed = 12; lp.layers[(size_t) l1].levelDb = -4.0f; proc->loopsRoll (l1, false);
    const int l2 = proc->loopsAddLayer(); lp.layers[(size_t) l2].mode = bc::LoopMode::Slice; lp.layers[(size_t) l2].source.kind = bc::LoopSource::From; lp.layers[(size_t) l2].source.from = "yard clanks";
    lp.layers[(size_t) l2].bars = 1; lp.layers[(size_t) l2].perBar = 12; lp.layers[(size_t) l2].seed = 13; lp.layers[(size_t) l2].pan = -0.4f; proc->loopsRoll (l2, false);
    const int l3 = proc->loopsAddLayer(); lp.layers[(size_t) l3].mode = bc::LoopMode::Melody; lp.layers[(size_t) l3].bars = 4; lp.layers[(size_t) l3].seed = 14; lp.layers[(size_t) l3].density = 0.4f; proc->loopsRoll (l3, false);
    proc->loopsMark();
    ed->getView().loopLayer = 0; ed->getView().loopHit = 2;
    proc->loopPlayer.playing.store (true);
    pump (*proc, 1300);
    ed->getLoopsRoom().refresh(); pump (*proc, 60);
    shot (*ed, outDir, "12_loops");
    proc->loopPlayer.playing.store (false);
    ed->setSize (960, 600); pump (*proc, 100);
    shot (*ed, outDir, "13_loops_small");

    // KEYS: empty, then horns tuned across the keys, a chord held
    ed->setSize (1280, 800);
    ed->showRoom (3); pump (*proc, 150);
    shot (*ed, outDir, "14_keys_empty");
    proc->kitAddFolder (testaudio::horns (dir));
    proc->keysSource.kind = bc::LoopSource::From; proc->keysSource.from = "horns";
    proc->keys.name = "City horns"; proc->keys.sustain = bc::KeysSustain::Loop; proc->keys.releaseMs = 600.0f;
    proc->keysBuild();
    ed->getView().keysZone = 3;
    proc->keyboard.noteOn (1, 57, 0.8f); proc->keyboard.noteOn (1, 60, 0.8f); proc->keyboard.noteOn (1, 64, 0.8f);
    pump (*proc, 400);
    ed->getKeysRoom().refresh(); pump (*proc, 60);
    shot (*ed, outDir, "15_keys");
    proc->keyboard.allNotesOff (1);
    ed->setSize (960, 600); pump (*proc, 100);
    shot (*ed, outDir, "16_keys_small");

    ed.reset();
    proc.reset();
    dir.deleteRecursively();
    return 0;
}
