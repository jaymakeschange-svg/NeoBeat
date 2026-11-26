// Renders the BANDS room to PNG, in the states worth looking at.
//
// The engine can be tested by measuring what comes out of it. A room cannot: a plate the wrong
// height, a readout under a knob, a stage drawn upside down - none of that shows up in a number.
// So this builds the component off-screen and writes what it draws, and the check is a look.
//
// Nothing here needs a plugin, a host or a session: BandsRoom takes a BandsState and a callback,
// which is the whole reason it was given that contract.

#include <juce_gui_basics/juce_gui_basics.h>
#include "neobeat/shared/rooms/BandsRoom.h"

namespace
{

void shoot (bc::BandsRoom& room, const juce::File& out)
{
    juce::Image img (juce::Image::ARGB, room.getWidth(), room.getHeight(), true);
    {
        juce::Graphics g (img);
        room.paintEntireComponent (g, true);
    }
    juce::PNGImageFormat png;
    out.deleteFile();
    std::unique_ptr<juce::FileOutputStream> s (out.createOutputStream());
    if (s != nullptr && png.writeImageToStream (img, *s))
        std::printf ("  wrote %s (%d x %d)\n", out.getFileName().toRawUTF8(), img.getWidth(), img.getHeight());
    else
        std::printf ("  FAILED to write %s\n", out.getFullPathName().toRawUTF8());
}

// A room laid out and ready to draw. Kept in one place so every shot is the same size.
struct Rig
{
    bc::BandsState state;
    double beats = 0.0;
    std::unique_ptr<bc::BandsRoom> room;

    Rig()
    {
        room = std::make_unique<bc::BandsRoom> (state, [] {}, [this] { return beats; });
        room->setSize (1280, 736);
        room->setBounds (0, 0, 1280, 736);
    }
};

} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory().getChildFile ("bands_shots");
    dir.createDirectory();
    std::printf ("BANDS room, into %s\n", dir.getFullPathName().toRawUTF8());

    // 1. straight out of the constructor: the default fan, nothing moving
    {
        Rig r;
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("01-default.png"));
    }

    // 2. a band selected near the top, so the detail panel is showing something with a long ring
    {
        Rig r;
        r.room->select (14);
        shoot (*r.room, dir.getChildFile ("02-band-15-selected.png"));
    }

    // 3. every band circling, caught part way round, so the paths and the live positions both show
    {
        Rig r;
        r.state.travelAll (nsr::Travel::Circle, 4);
        r.beats = 5.5;
        r.room->select (11);
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("03-circling.png"));
    }

    // 4. the neighbour feed up and the feedback high, which is where the trim bites: the readout
    //    should show a feedback figure below what the knob was set to
    {
        Rig r;
        r.state.dsp.startFeedback = r.state.dsp.finishFeedback = 0.9f;
        r.state.dsp.sideOffset = 2;
        r.state.dsp.sideFeedback = 0.5f;
        r.state.dsp.startTimeMs = 20.0f;
        r.state.dsp.finishTimeMs = 600.0f;
        r.room->select (6);
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("04-trimmed-feedback.png"));
    }

    // 5. placement off: the stage should say so rather than draw an empty room
    {
        Rig r;
        r.state.placed = false;
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("05-placement-off.png"));
    }

    // 6. the interaural split on across the whole bank, and a wide fan
    {
        Rig r;
        r.state.dsp.startInterMs = 1.0f;
        r.state.dsp.finishInterMs = 9.0f;
        r.state.fan (1.0f, true);
        r.room->select (0);
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("06-split-and-fan.png"));
    }

    // 7. short delays everywhere, which collapses the time axis: a check that the grid and the
    //    axis label follow rather than staying on a one-second grid for a 90 ms bank
    {
        Rig r;
        r.state.dsp.startTimeMs = 8.0f;
        r.state.dsp.finishTimeMs = 45.0f;
        r.state.dsp.startFeedback = r.state.dsp.finishFeedback = 0.2f;
        r.room->refresh();
        shoot (*r.room, dir.getChildFile ("07-short-times.png"));
    }

    std::printf ("done\n");
    return 0;
}
