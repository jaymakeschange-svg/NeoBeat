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

    // the multi: four voices, laid across the keys, two of them overlapping
    ed->setBounds (0, 0, 1120, 700);
    for (int t = 0; t < mjq::kSessionTracks; ++t)
        for (int p = 0; p < mjq::kSessionSlots; ++p)
            proc->session.slots[(size_t) p].tracks[(size_t) t].enabled = true;
    proc->changeTrackRange (1, 24, 47);
    proc->changeTrackRange (2, 48, 79);
    proc->changeTrackRange (3, 72, 108);
    for (int p = 0; p < mjq::kSessionSlots; ++p)
    {
        proc->session.slots[(size_t) p].tracks[0].instrumentFile = "City horns.njqi";
        proc->session.slots[(size_t) p].tracks[1].instrumentFile = "Rubber bass.njqi";
        proc->session.slots[(size_t) p].tracks[2].instrumentFile = "Struck glass.njqi";
    }
    proc->chooseTrack (2);
    ed->showSideTab (1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    shot (*ed, dir, "4_voices");

    // the Orb: four corners filled, the joystick between two of them, effects stacked
    {
        proc->chooseTrack (0);
        for (int c = 0; c < 4; ++c)
        {
            proc->line.dialect = 0.15f + 0.25f * (float) c;
            proc->line.traffic = 0.9f - 0.15f * (float) c;
            proc->line.gate = 0.4f + 0.4f * (float) c;
            mjq::generate (proc->line, 300u + 77u * (uint32_t) c);
            proc->lineChanged();
            proc->putOrbCorner (0, c * 2);
        }
        for (int p = 0; p < mjq::kSessionSlots; ++p)
        {
            auto& orb = proc->session.slots[(size_t) p].tracks[0].orb;
            orb.on = true;
            orb.corners[0].stack[0].fx = (int) nb::MatrixFx::Freezing; orb.corners[0].stack[0].mix = 0.8f;
            orb.corners[0].stack[1].fx = (int) nb::MatrixFx::Shimmer;  orb.corners[0].stack[1].mix = 0.5f;
            orb.corners[2].stack[0].fx = (int) nb::MatrixFx::Freezing; orb.corners[2].stack[0].mix = 0.3f;
            orb.corners[2].stack[1].fx = (int) nb::MatrixFx::Granulation; orb.corners[2].stack[1].mix = 1.0f;
        }
        proc->moveOrb (0.42f, -0.62f);
        ed->setBounds (0, 0, 1120, 780);
        ed->showSideTab (2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        shot (*ed, dir, "5_orb");
    }

    ed.reset();
    proc->releaseResources();
    return 0;
}
