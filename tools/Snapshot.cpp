// Pictures of MidiJaQ's window, for looking over the layout. Run under Xvfb:
//   xvfb-run -a MidiJaQSnapshot <folder>
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "app/Editor.h"

static void shot (juce::Component& c, const juce::File& dir, const juce::String& name)
{
    juce::Image img (juce::Image::ARGB, c.getWidth(), c.getHeight(), true);
    { juce::Graphics g (img); c.paintEntireComponent (g, true); }
    juce::PNGImageFormat png;
    auto f = dir.getChildFile (name + ".png");
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
    if (out != nullptr) png.writeImageToStream (img, *out);
    std::printf ("  %s\n", f.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const auto dir = juce::File (argc > 1 ? juce::String (argv[1]) : juce::String ("."));
    dir.createDirectory();

    auto proc = std::make_unique<MjqProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->line.numSteps = 16; proc->line.traffic = 0.55f; proc->line.dialect = 0.35f;
    mjq::generate (proc->line, 2026);
    proc->lineChanged();

    std::unique_ptr<MjqEditor> ed (dynamic_cast<MjqEditor*> (proc->createEditor()));
    ed->setBounds (0, 0, 1120, 480);
    ed->refresh();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    shot (*ed, dir, "1_midijaq");

    proc->line.traffic = 1.0f; proc->line.dialect = 1.0f; proc->line.numSteps = 32; proc->line.locked[(size_t) mjq::Lane::Rhythm] = true;
    proc->lineChanged(); ed->refresh();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    shot (*ed, dir, "2_full_acid_32");

    ed->setBounds (0, 0, 920, 430);
    ed->refresh();
    juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
    shot (*ed, dir, "3_small");

    ed.reset();
    proc->releaseResources();
    return 0;
}
