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
        {
            auto& orb = proc->session.orbs[0];
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

    // the stereo room: four voices placed, the chosen one swinging left and right
    {
        {
            auto& places = proc->session.places;
            places[0].on = true; places[0].x = -0.15f; places[0].y = 0.18f; places[0].width = 0.35f;
            places[1].on = true; places[1].x = 0.62f;  places[1].y = 0.55f; places[1].width = 0.2f;
            places[2].on = true; places[2].x = -0.7f;  places[2].y = 0.8f;  places[2].width = 0.8f;
            places[3].on = true; places[3].x = 0.25f;  places[3].y = 0.95f; places[3].width = 0.6f;
            auto& chosen = places[0];
            chosen.move = mjq::RoomMove::LeftRight;
            chosen.depth = 0.7f;
            chosen.bars = 2;
            for (int i = 0; i < mjq::kRoomCurve; ++i)
            {
                const float t = (float) i / (float) mjq::kRoomCurve;
                chosen.curve[(size_t) i] = std::sin (6.28318531f * t) * 0.8f + 0.2f * std::sin (18.84955592f * t);
            }
        }
        proc->chooseTrack (0);
        ed->setBounds (0, 0, 1120, 820);
        ed->showSideTab (3);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        shot (*ed, dir, "6_room");
    }

    ed.reset();
    proc->releaseResources();
    return 0;
}
