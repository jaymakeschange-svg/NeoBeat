// The IDM Jack panel, rendered. Plain by design, so this is here to catch overlap and unreadable
// text rather than to judge a look.
#include <juce_gui_basics/juce_gui_basics.h>
#include "neobeat/shared/rooms/JackRoom.h"

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir = argc > 1 ? juce::File (juce::String (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory();
    dir.createDirectory();

    float a = 0.5f, b = 0.3f, c = 0.7f, mono = 1.0f, own = 0.25f;
    nsr::Chaos chaos;
    chaos.addTarget ({ "KIT",   "drive",      &a,    0.0f, 1.0f, 1.00f, true,  false, 0.0f });
    chaos.addTarget ({ "KIT",   "tone",       &b,    0.0f, 1.0f, 0.60f, true,  false, 0.0f });
    chaos.addTarget ({ "BANDS", "length",     &c,    0.0f, 1.0f, 0.55f, true,  false, 0.0f });
    chaos.addTarget ({ "BANDS", "feedback",   &b,    0.2f, 0.93f,0.45f, false, false, 0.0f });
    chaos.addTarget ({ "ROOM",  "mono locks", &mono, 0.0f, 1.0f, 0.00f, false, true,  0.0f });
    chaos.addTarget ({ "JACK",  "own rate",   &own,  0.0f, 1.0f, 0.00f, false, true,  0.0f });

    double beats = 0.0;
    bc::JackRoom room (chaos, [&beats] { return beats; });
    room.setSize (1280, 736);
    room.setBounds (0, 0, 1280, 736);

    auto shoot = [&] (const char* name)
    {
        juce::Image img (juce::Image::ARGB, 1280, 736, true);
        { juce::Graphics g (img); room.paintEntireComponent (g, true); }
        juce::PNGImageFormat png;
        auto f = dir.getChildFile (name);
        f.deleteFile();
        std::unique_ptr<juce::FileOutputStream> s (f.createOutputStream());
        if (s != nullptr && png.writeImageToStream (img, *s))
            std::printf ("  wrote %s\n", name);
    };

    shoot ("jack-idle.png");
    chaos.engage (0.0);
    beats = 2.5;
    chaos.advance (0.0, 4.0);
    room.refresh();
    shoot ("jack-held.png");
    return 0;
}
