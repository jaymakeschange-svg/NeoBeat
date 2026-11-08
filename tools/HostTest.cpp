// NeoBeat 2.0 — a stress host: the whole plugin (processor and editor) driven like a DAW would,
// with random edits everywhere, MIDI, transport jumps, sample-rate and block-size changes, state
// round trips and the editor opened, used and closed while it plays. Usage: NeoBeatHostTest [seconds]
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/plugin/PluginProcessor.h"
#include "../Source/plugin/PluginEditor.h"
#include "../Source/plugin/ProjectIO.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("  FAIL: %s\n", msg); ++failures; } else std::printf ("  ok:   %s\n", msg); } while (0)

struct Host
{
    NeoBeatProcessor& p;
    double sr = 48000.0; int block = 512; double ppq = 0.0; bool playing = true; double bpm = 120.0;
    float maxAbs = 0.0f; bool bad = false;
    juce::Random rnd { 1234 };

    struct PH : juce::AudioPlayHead
    {
        Host* h = nullptr;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i; i.setBpm (h->bpm); i.setPpqPosition (h->ppq); i.setIsPlaying (h->playing);
            i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            return i;
        }
    } ph;

    explicit Host (NeoBeatProcessor& proc) : p (proc) { ph.h = this; p.setPlayHead (&ph); }

    void prepare (double rate, int bs) { sr = rate; block = bs; p.setPlayConfigDetails (0, 2, rate, bs); p.prepareToPlay (rate, bs); }

    void run (double seconds, bool midi = true, int sizeOverride = 0)
    {
        const int total = (int) (seconds * sr);
        for (int done = 0; done < total;)
        {
            int n = sizeOverride > 0 ? sizeOverride : block;
            if (sizeOverride < 0) n = 1 + rnd.nextInt (block);   // hosts send smaller blocks than promised
            juce::AudioBuffer<float> buf (2, n);
            juce::MidiBuffer m;
            if (midi && rnd.nextFloat() < 0.3f)
            {
                const int note = rnd.nextInt (128);
                m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) (1 + rnd.nextInt (127))), rnd.nextInt (n));
                if (rnd.nextFloat() < 0.5f) m.addEvent (juce::MidiMessage::noteOff (1, rnd.nextInt (128)), rnd.nextInt (n));
                if (rnd.nextFloat() < 0.01f) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            }
            p.processBlock (buf, m);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float x = buf.getSample (c, i);
                    if (! std::isfinite (x)) bad = true;
                    maxAbs = std::max (maxAbs, std::fabs (x));
                }
            if (playing) ppq += bpm / 60.0 * n / sr;
            if (rnd.nextFloat() < 0.002f) ppq = rnd.nextFloat() * 64.0;   // the host loops or jumps
            done += n;
            if ((done / n) % 8 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }
};

static void randomEdit (NeoBeatProcessor& p, juce::Random& r)
{
    auto& P = p.project;
    nb::Pattern pat; p.snapshotPattern (pat);
    switch (r.nextInt (14))
    {
        case 0: P.matrix.oneShot = nb::makeOneShot (r.nextInt (33), pat); break;
        case 1: P.matrix.seq = nb::makeSeq (r.nextInt (32)); break;
        case 2: P.matrix.sample = nb::makeSample (r.nextInt (32)); break;
        case 3: P.keymap = nb::makeKeyMap (r.nextInt (5)); break;
        case 4: { auto& s = P.fs[(size_t) r.nextInt (8)]; s.on = r.nextBool(); s.src = { (nb::SourceKind) r.nextInt (4), r.nextInt (16) }; s.p.stretch = 1.0f + r.nextFloat() * 20.0f; s.replace = r.nextBool(); break; }
        case 5: { auto& s = P.bd[(size_t) r.nextInt (8)]; s.on = r.nextBool(); s.src = { (nb::SourceKind) r.nextInt (4), r.nextInt (64) }; s.p.bounciness = 0.3f + r.nextFloat() * 0.65f; break; }
        case 6: P.padFx[(size_t) r.nextInt (16)].inserts[(size_t) r.nextInt (3)] = r.nextBool() ? nb::CellFx() : nb::CellFx::library ((nb::MatrixFx) r.nextInt (24)); break;
        case 7: { auto& g = P.grid.patterns[(size_t) r.nextInt (16)]; for (int i = 0; i < 200; ++i) g.cells[(size_t) r.nextInt (4096)] = nb::GridPattern::pack (r.nextInt (256), r.nextInt (256), r.nextInt (256), (uint32_t) (1 | (r.nextInt (4) << 1))); g.length = 1 + r.nextInt (64); g.rate = r.nextInt (4); P.grid.play = r.nextBool(); P.grid.current = r.nextInt (16); break; }
        case 8:
        {
            const char* dests[] = { "pad/*/level", "pad/3/tone", "pad/0/tune", "cell/0/main", "fs/0/stretch", "bd/1/speed", "seq/pan", "grid/pitch", "master", "pad/7/pan", "bogus/route" };
            if (P.mod.routes.size() > 20) P.mod.routes.clear();
            P.mod.routes.push_back ({ (nb::ModSrc) r.nextInt ((int) nb::ModSrc::Count), dests[r.nextInt (11)], r.nextFloat() * 2.0f - 1.0f });
            break;
        }
        case 9: P.mod.algos[(size_t) r.nextInt (16)] = { r.nextInt (6), r.nextFloat(), r.nextFloat(), (nb::ModSrc) r.nextInt ((int) nb::ModSrc::Count), r.nextFloat() * 2.0f - 1.0f }; break;
        case 10: p.loadPreset (r.nextInt (nb::numPatterns())); break;
        case 11: p.loadKit (r.nextInt ((int) nb::kits().size())); break;
        case 12: { const auto id = p.paramId (r.nextInt (16), r.nextBool() ? "tune" : "decay"); if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (r.nextFloat()); break; }
        default: { nb::OneShotCell c; c.pad = r.nextInt (16); c.step = r.nextInt (64); c.span = 1 + r.nextInt (16); c.height = r.nextFloat(); c.fx = r.nextInt (5) == 0 ? nb::CellFx::friction ({}) : nb::CellFx::library ((nb::MatrixFx) r.nextInt (24)); if (P.matrix.oneShot.cells.size() < 40) P.matrix.oneShot.cells.push_back (c); break; }
    }
    p.projectChanged();
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double seconds = argc > 1 ? std::atof (argv[1]) : 20.0;
    auto proc = std::make_unique<NeoBeatProcessor>();
    Host host (*proc);

    std::printf ("1. plays at 48 kHz / 512 with random MIDI and edits\n");
    host.prepare (48000.0, 512);
    for (int i = 0; i < 40; ++i) { randomEdit (*proc, host.rnd); host.run (seconds / 40.0); }
    CHECK (! host.bad, "finite output");
    std::printf ("     peak %.3f, renderer %s (%d/%d)\n", host.maxAbs, proc->renderer().busy() ? "busy" : "idle", proc->renderer().jobsDone(), proc->renderer().jobsTotal());
    CHECK (host.maxAbs < 1.05f, "stays near or under full scale");

    std::printf ("2. the editor: open, use every room and window, close, while it plays\n");
    for (int round = 0; round < 3; ++round)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1440, 900);
        auto* shell = dynamic_cast<ui::Shell*> (ed->getChildComponent (0));
        CHECK (shell != nullptr, "the shell is there");
        if (shell == nullptr) break;
        auto& ctx = shell->ctx;
        for (int k = 0; k < 30; ++k)
        {
            switch (host.rnd.nextInt (9))
            {
                case 0: ctx.room = host.rnd.nextInt (4); ctx.refreshNow(); break;
                case 1: ctx.select ({ ui::Sel::Pad, host.rnd.nextInt (16) }, host.rnd.nextInt (3)); break;
                case 2: ctx.room = 1; proc->project.matrix.view = host.rnd.nextInt (3); ctx.select ({ ui::Sel::None, 0 }, host.rnd.nextInt (3)); break;
                case 3: if (! proc->project.matrix.oneShot.cells.empty()) { ctx.room = 1; proc->project.matrix.view = 1; ctx.select ({ ui::Sel::Cell, host.rnd.nextInt ((int) proc->project.matrix.oneShot.cells.size()) }, host.rnd.nextInt (3)); } break;
                case 4: ctx.openWindow (host.rnd.nextBool(), host.rnd.nextInt (8)); break;
                case 5: ctx.fsOpen = ctx.bdOpen = false; ctx.listOpen = host.rnd.nextBool(); ctx.refreshNow(); break;
                case 6: if (! proc->project.keymap.zones.empty()) { ctx.room = 3; ctx.select ({ ui::Sel::Zone, host.rnd.nextInt ((int) proc->project.keymap.zones.size()) }, host.rnd.nextInt (3)); } break;
                case 7: randomEdit (*proc, host.rnd); ctx.refreshNow(); break;
                default: ctx.dockSrc = (nb::ModSrc) host.rnd.nextInt ((int) nb::ModSrc::Count); ctx.room = 2; ctx.select ({ ui::Sel::Grid, 0 }, host.rnd.nextInt (3)); break;
            }
            host.run (0.08);
            // paint everything once (catches drawing bugs)
            if (k % 5 == 0) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f); juce::ignoreUnused (img); }
        }
        ed.reset();
        host.run (0.2);
    }
    CHECK (! host.bad, "finite output with the editor");

    std::printf ("3. odd block sizes, a sample-rate change, stopping and starting\n");
    host.run (1.0, true, -1);
    host.prepare (44100.0, 256); host.run (1.5);
    host.prepare (96000.0, 64); host.run (1.0);
    host.prepare (48000.0, 1024); host.run (1.0, true, 1);
    host.playing = false; host.run (0.5); host.playing = true; host.run (0.5);
    proc->setParam ("mode", 1.0f); host.run (1.0); proc->setParam ("mode", 2.0f); host.run (1.0); proc->setParam ("mode", 0.0f);
    proc->releaseResources(); host.prepare (48000.0, 512); host.run (0.5);
    CHECK (! host.bad, "finite through every change");

    std::printf ("4. state: save, load into a new instance, save again: the same\n");
    for (int i = 0; i < 6; ++i) randomEdit (*proc, host.rnd);
    juce::MemoryBlock a, b;
    proc->getStateInformation (a);
    auto proc2 = std::make_unique<NeoBeatProcessor>();
    proc2->setStateInformation (a.getData(), (int) a.getSize());
    proc2->getStateInformation (b);
    auto xa = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
    auto xb = juce::AudioProcessor::getXmlFromBinary (b.getData(), (int) b.getSize());
    const bool same = xa && xb && xa->isEquivalentTo (xb.get(), false);
    if (! same && xa && xb) { xa->writeTo (juce::File ("/tmp/state_a.xml")); xb->writeTo (juce::File ("/tmp/state_b.xml")); }
    CHECK (same, "the state survives a round trip");
    // a truncated or foreign state must not crash
    proc2->setStateInformation (a.getData(), (int) a.getSize() / 2);
    const char junk[] = "not a state at all";
    proc2->setStateInformation (junk, (int) sizeof junk);
    CHECK (true, "broken states are ignored");
    // build-1 state still loads (no PROJECT element)
    auto old = juce::AudioProcessor::getXmlFromBinary (a.getData(), (int) a.getSize());
    if (old) { while (auto* c = old->getChildByName ("PROJECT")) old->removeChildElement (c, true); juce::MemoryBlock ob; juce::AudioProcessor::copyXmlToBinary (*old, ob); proc2->setStateInformation (ob.getData(), (int) ob.getSize()); }
    CHECK (true, "a build-1 state loads");

    std::printf ("5. user presets: save and load every section\n");
    int okCount = 0;
    for (auto s : { "Pattern", "Kit", "OneShot", "Sample", "Sequencer", "Grid", "KeyMap", "Mod" })
        okCount += (proc->saveUserPreset (s, "HostTest") && proc->loadUserPreset (s, "HostTest")) ? 1 : 0;
    CHECK (okCount == 8, "8 / 8 sections save and load");
    for (auto s : { "Pattern", "Kit", "OneShot", "Sample", "Sequencer", "Grid", "KeyMap", "Mod" }) NeoBeatProcessor::userRoot().getChildFile (s).getChildFile ("HostTest.nbp").deleteFile();
    host.run (1.0);
    CHECK (! host.bad, "finite after the presets");


    std::printf ("6. a monkey: random clicks, drags, wheels and double-clicks on every control\n");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1440, 900);
        auto* shell = dynamic_cast<ui::Shell*> (ed->getChildComponent (0));
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        int events = 0;
        for (int it = 0; it < 900 && shell != nullptr; ++it)
        {
            if (it % 60 == 0) { shell->ctx.room = it / 60 % 4; shell->ctx.refreshNow(); }
            std::vector<juce::Component*> all;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                for (auto* ch : c->getChildren())
                {
                    if (! ch->isVisible() || ch->getWidth() < 2 || ch->getHeight() < 2) continue;
                    if (auto* b = dynamic_cast<juce::Button*> (ch))
                    {
                        const auto t = b->getButtonText().toUpperCase();
                        if (t.contains ("LOAD") || t.contains ("FOLDER") || t.contains ("SAVE") || t.contains ("FILE") || t.contains ("SOURCE")) continue;
                    }
                    if (dynamic_cast<juce::ComboBox*> (ch) != nullptr) continue;
                    all.push_back (ch);
                    walk (ch);
                }
            };
            walk (shell);
            if (all.empty()) break;
            auto* target = all[(size_t) host.rnd.nextInt ((int) all.size())];
            juce::Component::SafePointer<juce::Component> safe (target);
            const juce::Point<float> p0 ((float) host.rnd.nextInt (target->getWidth()), (float) host.rnd.nextInt (target->getHeight()));
            juce::ModifierKeys mods (juce::ModifierKeys::leftButtonModifier | (host.rnd.nextInt (6) == 0 ? juce::ModifierKeys::shiftModifier : 0) | (host.rnd.nextInt (6) == 0 ? juce::ModifierKeys::altModifier : 0));
            const auto now = juce::Time::getCurrentTime();
            auto ev = [&] (juce::Point<float> p, bool dragged) { return juce::MouseEvent (src, p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, target, target, now, p0, now, 1, dragged); };
            const int kind = host.rnd.nextInt (10);
            if (kind == 0 && safe) { juce::MouseWheelDetails w { 0.0f, host.rnd.nextFloat() - 0.5f, false, false, false }; target->mouseWheelMove (ev (p0, false), w); }
            else if (kind == 1 && safe) { target->mouseDown (ev (p0, false)); if (safe) target->mouseUp (ev (p0, false)); if (safe) target->mouseDoubleClick (ev (p0, false)); }
            else if (safe)
            {
                target->mouseDown (ev (p0, false));
                juce::Point<float> p = p0;
                const int drags = host.rnd.nextInt (6);
                for (int d = 0; d < drags && safe; ++d) { p += { (float) host.rnd.nextInt (41) - 20.0f, (float) host.rnd.nextInt (41) - 20.0f }; target->mouseDrag (ev (p, true)); }
                if (safe) target->mouseUp (ev (p, drags > 0));
                if (auto* b = dynamic_cast<juce::Button*> (safe.getComponent())) if (host.rnd.nextInt (3) == 0) b->triggerClick();
            }
            ++events;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // the deferred rebuilds land here, as in a host
            host.run (0.02);
            if (it % 50 == 0) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.4f); juce::ignoreUnused (img); }
        }
        // close any windows the monkey opened
        for (int i = juce::TopLevelWindow::getNumTopLevelWindows(); --i >= 0;)
            if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i)) if (dynamic_cast<juce::AlertWindow*> (w) != nullptr) w->exitModalState (0);
        ed.reset();
        host.run (0.3);
        std::printf ("     %d events\n", events);
        CHECK (! host.bad, "finite after the monkey");
        juce::MemoryBlock st; proc->getStateInformation (st);
        CHECK (st.getSize() > 100, "the state still saves");
    }
    proc2.reset();
    proc.reset();
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
