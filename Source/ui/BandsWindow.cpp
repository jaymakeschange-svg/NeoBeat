// NeoBeat 3.0 — the BANDS window: the spectral delay, on a send.
//
// Decisions: docs/DECISIONS.md, "BANDS in NeoBeat - per-pad send" and "Next in the same build".
// A window, opened from the header next to FRICTION and BOUNCE, because that is how NeoBeat's
// effects already open (the user's choice, 27 Nov). The panel inside is the shared BandsRoom -
// the same one the family uses - so nothing about the bank is drawn twice.
//
// What reaches it: each lane's BANDS knob (in the lane's SOUND panel), and ALL here, which sends
// every lane at least that much. A lane at full send sounds the way the bank was set by ear: its
// own sound at the Dry level, the bank over it at Wet.
#include "UI.h"
#include "../shared/BandsRoom.h"

namespace ui
{
class BandsWindow final : public juce::Component, public Part
{
public:
    explicit BandsWindow (Ctx& c)
        : ctx (c),
          room (c.proc.bandsState, [this] { ctx.proc.bandsApply(); },
                [this] { return ctx.proc.workstation().bandsBeat.load (std::memory_order_relaxed); }),
          all (c, "ALL", 0.0f, 1.0f, 0.0f)
    {
        addAndMakeVisible (room);
        addAndMakeVisible (all);
        all.attach (ctx.proc.apvts.getParameter ("bands_all"));
        all.toText = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        all.setTooltip ("Every lane through BANDS at least this much, whatever its own send. "
                        "At 0 only the lanes whose BANDS knob is up go through.");
        addAndMakeVisible (close);
        close.onClick = [this] { ctx.bandsOpen = false; if (ctx.refreshAll) ctx.refreshAll(); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0x55000000));
        paintPlate (g, getLocalBounds().toFloat(), 20.0f);
        paintLabel (g, "BANDS | THE SPECTRAL DELAY, ON A SEND", { 22, 18, 420, 18 }, juce::Justification::centredLeft, 11.0f);
        // which lanes go through, in words: the knobs that decide it live in another panel
        juce::String lanes;
        const float allV = ctx.proc.apvts.getRawParameterValue ("bands_all")->load();
        if (allV > 0.0f) lanes = "EVERY LANE (ALL " + juce::String (juce::roundToInt (allV * 100.0f)) + "%)";
        else
            for (int p = 0; p < NeoBeatProcessor::kPads; ++p)
                if (ctx.proc.apvts.getRawParameterValue (ctx.proc.paramId (p, "bands"))->load() > 0.0f)
                    lanes << (lanes.isEmpty() ? "" : " ") << juce::String (p + 1);
        paintLabel (g, lanes.isEmpty() ? juce::String ("NO LANE SENDS YET - TURN UP BANDS IN A LANE'S SOUND PANEL, OR ALL")
                                       : "SENDING: " + lanes + (ctx.proc.bandsRunning() ? "" : "  (IDLE)"),
                    { 22, 42, 760, 14 }, juce::Justification::centredLeft, 7.5f, C::dim());
    }

    void resized() override
    {
        close.setBounds (getWidth() - 100, 20, 80, 30);
        all.setBounds (getWidth() - 196, 4, 84, 66);
        room.setBounds (10, 72, getWidth() - 20, getHeight() - 80);   // the panel needs 1280 x 736
    }

    void refresh() override { room.refresh(); repaint(); }
    void tick() override
    {
        all.syncFromParam();          // the host or automation can move ALL too
        const bool r = ctx.proc.bandsRunning();
        if (r != lastRunning) { lastRunning = r; repaint(); }
    }
    void mouseDown (const juce::MouseEvent&) override {}   // swallow clicks so the shell under it stays put

private:
    Ctx& ctx;
    bc::BandsRoom room;
    Knob all;
    Pill close { "CLOSE" };
    bool lastRunning = false;
};

std::unique_ptr<juce::Component> makeBandsWindow (Ctx& c) { return std::make_unique<BandsWindow> (c); }
} // namespace ui
