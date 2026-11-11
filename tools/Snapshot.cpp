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

    ed.reset();
    proc.reset();
    dir.deleteRecursively();
    return 0;
}
