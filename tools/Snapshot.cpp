// Renders the NeoBeat editor to a PNG while a preset plays (Linux UI check, no
// display server needed beyond Xvfb). Usage: NeoBeatSnapshot out.png [preset] [kit] [pad]
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/plugin/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto proc = std::make_unique<NeoBeatProcessor>();
    proc->setPlayConfigDetails (0, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    if (argc > 3) proc->loadKit (std::atoi (argv[3]));
    proc->loadPreset (argc > 2 ? std::atoi (argv[2]) : 0);
    if (auto* run = proc->apvts.getParameter ("run")) run->setValueNotifyingHost (1.0f);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    if (argc > 4) proc->selectedPad.store (std::atoi (argv[4]));
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    for (int f = 0; f < 75; ++f)
    {
        for (int b = 0; b < 3; ++b) { proc->processBlock (buf, midi); midi.clear(); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (34);
    }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat().writeImageToStream (img, os);
    std::printf ("wrote %s (%d x %d)\n", out.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    ed.reset();
    return 0;
}
