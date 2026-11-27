// NeoBeat 3.0 — the JACK room: IDM Jack and the global randomiser, on one plain panel.
//
// Spec: docs/PROPOSED-FEATURES.md, "IDM Jack"; the randomiser and the decisions behind this panel:
// docs/DECISIONS.md. Built to be tested, not to be looked at: the user asked for "no fancy UI,
// just usable for a test". What it holds:
//   ON      held: chaos while on, the written pattern and the stored kit back the moment it is off.
//           It is the host parameter "IDM Jack", so a controller or an automation lane can hold it.
//   ROLL    the global randomiser: one press, every parameter of every included lane once, and the
//           parameters of the effects on those lanes. It writes values - a destination, not a
//           visit - and each press is one undo step in this room.
//   LANES   which lanes are in, for both. Choosing a lane puts all of its parameters in.
//   the rest: Jack's settings, the seed of the pass, and what it is doing.
#include "UI.h"

namespace ui
{
namespace
{
const int kDivisions[] = { 4, 6, 8, 12 };
int divisionIndex (int d) { for (int i = 0; i < 4; ++i) if (kDivisions[i] == d) return i; return 1; }

// The axes NeoBeat can act on. EFFECT is left out: NeoBeat's lane effects are prepared ahead of
// the hit, so there is nothing per hit for it to move (docs/DECISIONS.md) - no dead switches.
const nsr::ChaosAxis kAxes[] = { nsr::ChaosAxis::Pitch, nsr::ChaosAxis::Parameter, nsr::ChaosAxis::Retrigger, nsr::ChaosAxis::Timing };
const char* kAxisTips[] = {
    "Pitch: each hit it seizes is retuned, up to an octave either way at full extremity.",
    "Parameters: decay, tone, drive, hollow, evolve, pan, width and level, per hit. On a lane whose "
    "effects need its sound prepared ahead, only pan, level and pitch reach it.",
    "Retrigger: a burst subdivides its lattice step into rapid repeats, each with its own offsets.",
    "Micro-timing: each hit lands up to the timing amount late, 1 to 100 ms.",
};
} // namespace

class JackRoom final : public juce::Component, public Part
{
public:
    explicit JackRoom (Ctx& c)
        : ctx (c),
          kDensity (c, "DENSITY", 0.0f, 1.0f, 0.45f), kExtremity (c, "EXTREMITY", 0.0f, 1.0f, 0.6f),
          kRepeat (c, "REPEAT", 0.0f, 0.95f, 0.45f), kWindow (c, "WINDOW", 2.0f, 32.0f, 12.0f),
          kRecall (c, "RECALL", 0.0f, 0.95f, 0.4f), kMemory (c, "MEMORY", 1.0f, 16.0f, 4.0f),
          kSwing (c, "SWING", 0.5f, 0.75f, 0.58f), kRetrig (c, "RETRIG", 0.0f, 8.0f, 4.0f),
          kTiming (c, "TIMING", 1.0f, 100.0f, 100.0f), kSelf (c, "SELF", 0.0f, 1.0f, 0.0f),
          kRecovery (c, "RECOVERY", 0.0f, 4.0f, 0.0f),
          mode ({ "OVERLAY", "REPLACE" }), lattice ({ "1/4", "1/6", "1/8", "1/12" })
    {
        addAndMakeVisible (on);
        on.setClickingTogglesState (false);
        on.setTooltip ("Held: chaos while it is on, the original back the moment it is off. Nothing is "
                       "written: the pattern and the kit are exactly as they were. Also the host "
                       "parameter \"IDM Jack\" - map it to a pad or a key to play it.");
        on.onClick = [this] { ctx.proc.jackSetHeld (! ctx.proc.jackHeldParam()); tick(); };

        addAndMakeVisible (roll);
        roll.setTooltip ("The randomiser: one press, every parameter of every lane marked below, and "
                         "the effects on those lanes, once. It writes them - press, listen, press "
                         "again - and each press is one undo step in this room.");
        roll.onClick = [this] { ctx.proc.roll(); ctx.changed(); };

        addAndMakeVisible (mode);
        mode.setTooltip ("Overlay keeps the written pattern and seizes over it. Replace silences the "
                         "written pattern while held and runs the kit on Jack alone.");
        mode.onSelect = [this] (int) { push(); };
        addAndMakeVisible (lattice);
        lattice.setTooltip ("The lattice, per beat. Hits can only land on it: randomness decides "
                            "whether and how much, never where. 1/6 is sixteenth triplets.");
        lattice.onSelect = [this] (int) { push(); };

        struct K { Knob* k; const char* tip; };
        const K knobs[] = {
            { &kDensity, "How often a lattice step fires a burst at all." },
            { &kExtremity, "How far each seizure goes when it fires: pitch, parameters, timing." },
            { &kRepeat, "Whether the next window reuses this window's seed. High holds a figure long "
                        "enough to hear it as an idea." },
            { &kWindow, "Lattice steps per window. A window is what shares a seed." },
            { &kRecall, "How often a burst replays a figure it played before, instead of a new one. "
                        "This is what makes motifs: shapes that come back." },
            { &kMemory, "How many figures it remembers. Few: the same shapes recur. Many: texture." },
            { &kSwing, "The lattice shuffle. 0.5 is straight." },
            { &kRetrig, "The most extra hits a burst can split its step into." },
            { &kTiming, "The latest a hit may land, for the micro-timing axis." },
            { &kSelf, "Roll Jack's own settings at each switch-on, at this depth. It never reaches "
                      "this control, the lanes, or the seed." },
            { &kRecovery, "How it lands back when released: zero is a hard cut, above that the "
                          "seizures fade out over this many bars (no new bursts start)." },
        };
        for (const auto& k : knobs)
        {
            k.k->setTooltip (k.tip);
            k.k->onChange = [this] (float) { push(); };
            addAndMakeVisible (*k.k);
        }
        kWindow.toText = [] (float v) { return juce::String ((int) std::lround (v)); };
        kMemory.toText = [] (float v) { return juce::String ((int) std::lround (v)); };
        kRetrig.toText = [] (float v) { return juce::String ((int) std::lround (v)); };
        kTiming.toText = [] (float v) { return juce::String ((int) std::lround (v)) + " ms"; };
        kRecovery.toText = [] (float v) { return v < 0.01f ? juce::String ("CUT") : juce::String (v, 2) + " bar"; };
        for (auto* k : { &kDensity, &kExtremity, &kRepeat, &kRecall, &kSelf })
            k->toText = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };
        kSwing.toText = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };

        for (int i = 0; i < 4; ++i)
        {
            auto* g = axes.add (new Pill (nsr::chaosAxisName (kAxes[i]), true));
            g->setTooltip (kAxisTips[i]);
            g->onClick = [this] { push(); };
            addAndMakeVisible (g);
        }

        addAndMakeVisible (lock);
        lock.setTooltip ("Off: every switch-on is a new pass. On: the same pass again - the seed shown - "
                         "so a take worth keeping can be played again.");
        lock.onClick = [this] { ctx.proc.jackLockSeed = lock.getToggleState(); };
        addAndMakeVisible (newSeed);
        newSeed.setTooltip ("A new seed now, for the next pass (or this one, from its next window).");
        newSeed.onClick = [this]
        {
            ctx.proc.jack.seed = gen::Rng::mix ((uint64_t) juce::Time::currentTimeMillis(), (uint64_t) ++presses) | 1u;
            ctx.proc.jackApply(); tick();
        };
        addAndMakeVisible (seedLcd);

        for (int p = 0; p < NeoBeatProcessor::kPads; ++p)
        {
            auto* b = lanes.add (new Pill ({}, true));
            b->onClick = [this, p, b]
            {
                auto& list = ctx.proc.chaos.allTargets();
                if (p < (int) list.size()) list[(size_t) p].on = b->getToggleState();
                ctx.proc.jackApply();
            };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (all); addAndMakeVisible (none);
        all.onClick = [this] { ctx.proc.includeAll (true); refresh(); };
        none.onClick = [this] { ctx.proc.includeAll (false); refresh(); };
        all.setTooltip ("Every lane in, for Jack and for ROLL.");
        none.setTooltip ("No lane in. Overlay with nothing in plays the plain kit.");

        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        paintPearl (g, getLocalBounds().toFloat(), 18.0f);
        g.setFont (caps (13.0f)); g.setColour (C::text());
        g.drawText ("IDM JACK", 16, 12, 200, 20, juce::Justification::centredLeft);
        paintLabel (g, "HELD | ROLL", { 16, 34, 200, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "MODE", { 300, 34, 200, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "LATTICE", { 524, 34, 200, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "WHAT IT MAY SEIZE", { 16, yAxes - 16, 300, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "PASS", { 470, yAxes - 16, 200, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
        paintLabel (g, "LANES IT MAY GRAB - AND THAT ROLL ROLLS. A LANE IN IS ALL OF ITS PARAMETERS IN.",
                    { 16, yLanes - 16, 640, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());

        // what it is doing, in words: a panel this plain has nothing else to say it with
        const int st = ctx.proc.jackState();
        juce::String s = "IDLE";
        juce::Colour col = C::dim();
        if (st == 1)      { s = "HELD - SEIZING"; col = C::amber(); }
        else if (st == 2) { s = "RECOVERING"; col = C::teal(); }
        g.setFont (caps (10.0f)); g.setColour (col);
        g.drawText (s, getWidth() - 236, 12, 220, 20, juce::Justification::centredRight);
        paintLabel (g, juce::String (ctx.proc.jackBursts()) + " BURSTS | " + juce::String (ctx.proc.jackSeized()) + " HITS SEIZED THIS PASS"
                       + (ctx.proc.jack.selfRandomise > 0.001f ? "   |   SELF-ROLLED: DENSITY " + juce::String (juce::roundToInt (ctx.proc.jackPlaying.density * 100.0f))
                            + "%  EXTREMITY " + juce::String (juce::roundToInt (ctx.proc.jackPlaying.extremity * 100.0f))
                            + "%  LATTICE 1/" + juce::String (ctx.proc.jackPlaying.division) : juce::String()),
                    { 16, getHeight() - 22, getWidth() - 32, 12 }, juce::Justification::centredLeft, 7.0f, C::dim());
    }

    void resized() override
    {
        on.setBounds (16, 48, 96, 34);
        roll.setBounds (120, 48, 96, 34);
        mode.setBounds (300, 52, 200, 26);
        lattice.setBounds (524, 52, 240, 26);

        int x = 16, y = 102;
        const int kw = 98;
        for (auto* k : { &kDensity, &kExtremity, &kRepeat, &kWindow, &kRecall, &kMemory, &kSwing, &kRetrig })
        { k->setBounds (x, y, kw - 8, 82); x += kw; }
        x = 16; y += 96;
        for (auto* k : { &kTiming, &kSelf, &kRecovery })
        { k->setBounds (x, y, kw - 8, 82); x += kw; }

        yAxes = y + 108;
        for (int i = 0; i < axes.size(); ++i) axes[i]->setBounds (16 + i * 106, yAxes, 98, 28);
        lock.setBounds (470, yAxes, 84, 28);
        newSeed.setBounds (560, yAxes, 84, 28);
        seedLcd.setBounds (650, yAxes - 6, 154, 40);

        yLanes = yAxes + 62;
        const int cols = 4, tw = 190, th = 36;
        for (int p = 0; p < lanes.size(); ++p)
            lanes[p]->setBounds (16 + (p % cols) * (tw + 6), yLanes + (p / cols) * (th + 6), tw, th);
        const int yb = yLanes + 4 * (th + 6) + 4;
        all.setBounds (16, yb, 70, 26);
        none.setBounds (92, yb, 70, 26);
    }

    void refresh() override
    {
        const auto& j = ctx.proc.jack;
        kDensity.setValue (j.density); kExtremity.setValue (j.extremity); kRepeat.setValue (j.repeatProb);
        kWindow.setValue ((float) j.windowSteps); kRecall.setValue (j.recallProb); kMemory.setValue ((float) j.memory);
        kSwing.setValue (j.swing); kRetrig.setValue ((float) j.maxRetriggers); kTiming.setValue (j.timingMaxMs);
        kSelf.setValue (j.selfRandomise); kRecovery.setValue (j.recoveryBars);
        mode.setSelected (j.replace ? 1 : 0);
        lattice.setSelected (divisionIndex (j.division));
        for (int i = 0; i < axes.size(); ++i) axes[i]->setToggleState (j.axis[(int) kAxes[i]], juce::dontSendNotification);
        lock.setToggleState (ctx.proc.jackLockSeed, juce::dontSendNotification);
        ctx.proc.refreshPadTargetNames();
        for (int p = 0; p < lanes.size(); ++p)
        {
            lanes[p]->setButtonText (juce::String (p + 1).paddedLeft ('0', 2) + "  " + ctx.proc.padName (p));
            lanes[p]->setToggleState (ctx.proc.padIncluded (p), juce::dontSendNotification);
        }
        tick();
        repaint();
    }

    void tick() override
    {
        // the host can hold it too: show what the parameter says, not what was last clicked here
        const bool held = ctx.proc.jackHeldParam();
        on.setToggleState (held, juce::dontSendNotification);
        on.setButtonText (held ? "ON" : "OFF");
        seedLcd.set ("SEED " + juce::String::toHexString ((juce::int64) ctx.proc.jack.seed).substring (0, 8).toUpperCase(),
                     ctx.proc.jackLockSeed ? "LOCKED: SAME PASS" : "NEXT ON: NEW PASS");
        const int b = ctx.proc.jackBursts(), st = ctx.proc.jackState();
        if (b != lastBursts || st != lastState) { lastBursts = b; lastState = st; repaint(); }
    }

private:
    void push()
    {
        auto& j = ctx.proc.jack;
        j.density = kDensity.getValue(); j.extremity = kExtremity.getValue(); j.repeatProb = kRepeat.getValue();
        j.windowSteps = (int) std::lround (kWindow.getValue()); j.recallProb = kRecall.getValue();
        j.memory = (int) std::lround (kMemory.getValue()); j.swing = kSwing.getValue();
        j.maxRetriggers = (int) std::lround (kRetrig.getValue()); j.timingMaxMs = kTiming.getValue();
        j.selfRandomise = kSelf.getValue(); j.recoveryBars = kRecovery.getValue();
        j.replace = mode.getSelected() == 1;
        j.division = kDivisions[juce::jlimit (0, 3, lattice.getSelected())];
        for (int i = 0; i < axes.size(); ++i) j.axis[(int) kAxes[i]] = axes[i]->getToggleState();
        ctx.proc.jackApply();
        repaint();
    }

    Ctx& ctx;
    Pill on { "OFF" }, roll { "ROLL" }, lock { "LOCK", true }, newSeed { "NEW SEED" }, all { "ALL" }, none { "NONE" };
    Knob kDensity, kExtremity, kRepeat, kWindow, kRecall, kMemory, kSwing, kRetrig, kTiming, kSelf, kRecovery;
    Segmented mode, lattice;
    juce::OwnedArray<Pill> axes, lanes;
    LcdBox seedLcd;
    int yAxes = 0, yLanes = 0, lastBursts = -1, lastState = -1;
    uint32_t presses = 0;
};

std::unique_ptr<juce::Component> makeJackRoom (Ctx& c) { return std::make_unique<JackRoom> (c); }
} // namespace ui
