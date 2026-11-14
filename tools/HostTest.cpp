// NeoBeat 2.0 — a stress host: the whole plugin (processor and editor) driven like a DAW would,
// with random edits everywhere, MIDI, transport jumps, sample-rate and block-size changes, state
// round trips and the editor opened, used and closed while it plays. Usage: NeoBeatHostTest [seconds]
#include <cstdlib>
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
    // the tests keep their settings, presets and what the learner knows away from the real ones
    const auto sharedDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neobeat_hosttest_shared");
    sharedDir.deleteRecursively(); sharedDir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8());
   #else
    setenv ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8(), 1);
   #endif

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
    std::printf ("7. rapid sample-rate and block-size changes while the renderer works (pluginval's audio test, 150 times)\n");
    {
        const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const int sizes[] = { 32, 64, 128, 256, 512, 1024, 2048 };
        juce::Random r (99);
        host.playing = false;
        for (int it = 0; it < 150; ++it)
        {
            if (r.nextBool()) proc->releaseResources();
            host.prepare (rates[r.nextInt (5)], sizes[r.nextInt (7)]);
            if (it % 3 == 0) randomEdit (*proc, r);   // new inputs, so the worker is busy at the next change
            const int blocks = 2 + r.nextInt (12);
            const int ch = 1 + r.nextInt (16), note = r.nextInt (128);
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buf (2, host.block);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < host.block; ++i) buf.setSample (c, i, r.nextFloat() * 2.0f - 1.0f);   // hosts may hand over junk
                juce::MidiBuffer m;
                if (b == 0) m.addEvent (juce::MidiMessage::noteOn (ch, note, (juce::uint8) 100), juce::jmin (10, host.block - 1));
                if (b == blocks - 1) m.addEvent (juce::MidiMessage::noteOff (ch, note), 0);
                proc->processBlock (buf, m);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < host.block; ++i) if (! std::isfinite (buf.getSample (c, i))) host.bad = true;
            }
            juce::MessageManager::getInstance()->runDispatchLoopUntil (r.nextInt (3) == 0 ? 160 : 1);   // sometimes long enough for the timer to submit
        }
        host.playing = true;
        host.prepare (48000.0, 512); host.run (0.5);
        CHECK (! host.bad, "finite through 150 rate and block changes");
    }

    std::printf ("8. scenes: four in the state, a 2.0 state becomes scene A\n");
    {
        auto p3 = std::make_unique<NeoBeatProcessor>();
        auto pat = std::make_unique<nb::Pattern>(); pat->length = 512; pat->steps[3][511].vel = 99; pat->steps[3][511].roll = 3;
        p3->pattern.loadScene (2, *pat);
        p3->setScene (2);
        p3->project.song.chainOn = true; p3->project.song.numLinks = 2; p3->project.song.chain[0] = { 2, 3 }; p3->project.song.chain[1] = { 0, 1 };
        nb::OneShotCell c; c.scene = 2; c.pad = 3; c.step = 511; c.fx = nb::CellFx::library (nb::MatrixFx::Freezing); p3->project.matrix.oneShot.cells = { c };
        p3->project.midi.mode = 1; p3->project.midi.note[5] = 60; p3->project.grid.follow[7].on = true; p3->project.grid.follow[7].lane = 3; p3->project.grid.follow[7].mode = 2;
        p3->projectChanged();
        juce::MemoryBlock st; p3->getStateInformation (st);
        auto p4 = std::make_unique<NeoBeatProcessor>();
        p4->setStateInformation (st.getData(), (int) st.getSize());
        const auto got = p4->pattern.getIn (2, 3, 511);
        CHECK (p4->pattern.lengthOf (2) == 512 && got.vel == 99 && got.roll == 3 && p4->scene() == 2, "a 32-bar scene survives");
        CHECK (p4->project.song.chainOn && p4->project.song.numLinks == 2 && p4->project.song.chain[0].repeats == 3, "the chain survives");
        CHECK (p4->project.matrix.oneShot.cells.size() == 1 && p4->project.matrix.oneShot.cells[0].scene == 2 && p4->project.matrix.oneShot.cells[0].step == 511, "a node's scene and step survive");
        CHECK (p4->project.midi.mode == 1 && p4->project.midi.note[5] == 60 && p4->project.grid.follow[7].on && p4->project.grid.follow[7].mode == 2, "MIDI out and the Layermax follows survive");
        // a 2.0 state: one PATTERN element
        auto x = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
        while (auto* ch = x->getChildByName ("SCENES")) x->removeChildElement (ch, true);
        auto* old2 = x->createNewChildElement ("PATTERN"); old2->setAttribute ("length", 16);
        old2->setAttribute ("t0", "60 0 0 0 60 0 0 0 60 0 0 0 60 0 0 0");
        juce::MemoryBlock ob; juce::AudioProcessor::copyXmlToBinary (*x, ob);
        p4->setStateInformation (ob.getData(), (int) ob.getSize());
        CHECK (p4->pattern.lengthOf (0) == 16 && p4->pattern.getIn (0, 0, 4).vel == 0x60 && p4->pattern.getIn (1, 0, 4).vel == 0, "a 2.0 project's pattern loads as scene A");
    }

    std::printf ("9. undo: each room keeps its own history\n");
    {
        auto p3 = std::make_unique<NeoBeatProcessor>();
        p3->undoCommit (0);                                   // the starting point
        nb::Step hit; hit.vel = 111;
        p3->pattern.set (9, 3, hit); p3->undoCommit (0);       // the sequencer's room: a hit
        p3->project.grid.patterns[0].cells[5] = nb::GridPattern::pack (200, 128, 128, nb::GridPattern::kOn); p3->projectChanged(); p3->undoCommit (2);   // Layermax: a box
        p3->setParam (p3->paramId (4, "width"), 0.0f); p3->undoCommit (0);   // the sequencer's room again: a knob
        CHECK (p3->canUndo (0) && p3->canUndo (2) && ! p3->canUndo (1) && ! p3->canUndo (3), "two rooms have something to undo, two have nothing");
        std::printf ("     sequencer: \"%s\" | Layermax: \"%s\"\n", p3->undoLabel (0).toRawUTF8(), p3->undoLabel (2).toRawUTF8());
        p3->undo (2);
        CHECK (p3->project.grid.patterns[0].cells[5] == 0 && p3->pattern.get (9, 3).vel == 111 && p3->getParam (p3->paramId (4, "width")) < 0.01f, "undo in Layermax takes back the box and nothing else");
        p3->undo (0);
        CHECK (p3->getParam (p3->paramId (4, "width")) > 0.99f && p3->pattern.get (9, 3).vel == 111, "undo in the sequencer's room takes back the knob");
        p3->undo (0);
        CHECK (p3->pattern.get (9, 3).vel == 0, "and again: the hit");
        p3->redo (0); p3->redo (0); p3->redo (2);
        CHECK (p3->pattern.get (9, 3).vel == 111 && p3->getParam (p3->paramId (4, "width")) < 0.01f && p3->project.grid.patterns[0].cells[5] != 0, "redo brings all three back");
        // a node sent to an effect, and back
        nb::OneShotCell c; c.pad = 9; c.step = 3; c.fx = nb::CellFx::library (nb::MatrixFx::Shimmer); p3->project.matrix.oneShot.cells.push_back (c); p3->projectChanged(); p3->undoCommit (1);
        p3->undo (1);
        CHECK (p3->project.matrix.oneShot.cells.empty(), "sending a node to an effect is one step in the Matrixxxer's history");
        for (int i = 0; i < 130; ++i) { hit.vel = (uint8_t) (1 + i % 120); p3->pattern.set (1, i % 64, hit); p3->undoCommit (0); }
        int steps = 0; while (p3->canUndo (0) && steps < 500) { p3->undo (0); ++steps; }
        CHECK (steps == 100, "the history keeps the last 100 steps");
    }

    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("neobeat_hosttest_" + juce::String (juce::Random::getSystemRandom().nextInt (1000000)));
    tmp.createDirectory();
    auto writeWav = [&] (const juce::File& f, int kind, float gain)
    {
        const double sr = 44100.0; const int n = (int) (sr * (kind == 3 ? 0.7 : 0.3));
        juce::AudioBuffer<float> b (2, n); juce::Random r (kind * 77 + 5); float ph = 0.0f, lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i / sr); float v = 0.0f; const float w = r.nextFloat() * 2.0f - 1.0f;
            if (kind == 0) { ph += 6.2831853f * (50.0f + 140.0f * std::exp (-t * 35.0f)) / (float) sr; v = std::sin (ph) * std::exp (-t * 11.0f); }
            else if (kind == 1) { ph += 6.2831853f * 190.0f / (float) sr; v = 0.5f * std::sin (ph) * std::exp (-t * 22.0f) + 0.6f * w * std::exp (-t * 16.0f); }
            else { const float hp = w - lp; lp += 0.35f * (w - lp); v = hp * std::exp (-t * (kind == 2 ? 90.0f : 7.0f)); }
            b.setSample (0, i, v * gain); b.setSample (1, i, v * gain * 0.8f);
        }
        juce::WavAudioFormat wav; f.deleteFile();
        std::unique_ptr<juce::AudioFormatWriter> wr (wav.createWriterFor (new juce::FileOutputStream (f), sr, 2, 24, {}, 0));
        if (wr) wr->writeFromAudioSampleBuffer (b, 0, n);
    };

    std::printf ("10. a folder of samples into the lanes\n");
    {
        const auto folder = tmp.getChildFile ("samples"); folder.createDirectory();
        writeWav (folder.getChildFile ("thing_a.wav"), 0, 0.05f);     // a kick, recorded very quietly
        writeWav (folder.getChildFile ("thing_b.wav"), 1, 0.5f);      // a snare
        writeWav (folder.getChildFile ("thing_c.wav"), 2, 0.5f);      // a closed hat
        writeWav (folder.getChildFile ("thing_d.wav"), 3, 0.5f);      // an open hat
        auto p3 = std::make_unique<NeoBeatProcessor>();
        p3->undoCommit (0);
        const auto rep = p3->importFolder (folder, true);
        for (const auto& l : rep.lines) std::printf ("     %s\n", l.toRawUTF8());
        CHECK (rep.found == 4 && rep.lanes == 4, "four files found, four lanes filled");
        CHECK (p3->padName (0).containsIgnoreCase ("thing_a") && p3->padName (2).containsIgnoreCase ("thing_b") && p3->padName (4).containsIgnoreCase ("thing_c") && p3->padName (5).containsIgnoreCase ("thing_d"),
               "kick, snare, closed hat and open hat each landed on their lane");
        CHECK (p3->getParam (p3->paramId (0, "level")) > p3->getParam (p3->paramId (2, "level")) + 12.0f, "the quiet recording was brought up to the others (more than 12 dB more)");
        CHECK (p3->padUsesSample (0) && ! p3->padUsesSample (1), "the other lanes keep their stock sounds");
        p3->undoCommit (0); p3->undo (0);
        CHECK (! p3->padUsesSample (0) && ! p3->padUsesSample (4), "one undo takes the whole import back");
        const auto all = p3->importFolder (folder, false);
        CHECK (all.rows == 4 && p3->project.grid.files.size() == 4 && ! p3->project.grid.useKit, "import all: every file is also a Layermax row");
    }

    std::printf ("11. .NJQ: a kit and its sequence in one file, there and back\n");
    {
        juce::String err;
        auto p3 = std::make_unique<NeoBeatProcessor>();
        writeWav (tmp.getChildFile ("mine.wav"), 1, 0.4f);
        CHECK (p3->loadSample (6, tmp.getChildFile ("mine.wav")), "a sample on lane 7");
        p3->setParam (p3->paramId (6, "level"), 7.5f); p3->setParam (p3->paramId (6, "width"), 1.6f); p3->setParam (p3->paramId (2, "tune"), -5.0f);
        p3->setParam ("tempo", 97.0f); p3->setParam ("swing", 0.58f);
        nb::Step hit; hit.vel = 120; hit.roll = 2; p3->pattern.setIn (3, 6, 200, hit); p3->pattern.setLengthIn (3, 256);
        p3->project.song.chainOn = true; p3->project.song.numLinks = 1; p3->project.song.chain[0] = { 3, 2 };
        const auto file = tmp.getChildFile ("Test kit.NJQ");
        CHECK (p3->saveNjq (file, "Test kit", err) && file.getSize() > 1000, "saved");
        std::printf ("     %s: %d bytes\n", file.getFileName().toRawUTF8(), (int) file.getSize());
        auto p4 = std::make_unique<NeoBeatProcessor>();
        CHECK (p4->loadNjq (file, err), "loaded into a new NeoBeat");
        CHECK (p4->padUsesSample (6) && std::fabs (p4->getParam (p4->paramId (6, "level")) - 7.5f) < 0.06f && std::fabs (p4->getParam (p4->paramId (6, "width")) - 1.6f) < 0.01f, "the lane's sample and its settings");
        CHECK (! p4->padUsesSample (2) && std::fabs (p4->getParam (p4->paramId (2, "tune")) + 5.0f) < 0.02f && p4->padName (2) == p3->padName (2), "a stock lane, by name, with its settings");
        CHECK (p4->pattern.lengthOf (3) == 256 && p4->pattern.getIn (3, 6, 200).vel == 120 && p4->pattern.getIn (3, 6, 200).roll == 2, "the scenes");
        CHECK (p4->project.song.chainOn && p4->project.song.chain[0].scene == 3 && std::fabs (p4->getParam ("tempo") - 97.0f) < 0.06f && std::fabs (p4->getParam ("swing") - 0.58f) < 0.002f, "the chain, the tempo and the swing");
        // the sound itself came through the FLAC intact
        Host h4 (*p4); h4.prepare (48000.0, 512); h4.playing = false;
        p4->audition (6, 1.0f); h4.run (0.3, false);
        CHECK (! h4.bad && h4.maxAbs > 0.02f, "the lane plays its sample");
        // the project remembers where the sample came from
        juce::MemoryBlock st; p4->getStateInformation (st);
        auto p5 = std::make_unique<NeoBeatProcessor>();
        p5->setStateInformation (st.getData(), (int) st.getSize());
        CHECK (p5->padUsesSample (6), "a saved project finds the sample inside the .NJQ again");
        CHECK (! p5->loadNjq (tmp.getChildFile ("mine.wav"), err) && err.isNotEmpty(), "a file that is not a .NJQ is refused, with a reason");
        // "make this a default sequence"
        juce::File::getCurrentWorkingDirectory();
        auto st0 = shared::Settings::load(); st0.presetFolder = tmp.getChildFile ("presets").getFullPathName(); st0.startupNjq.clear(); st0.save();
        CHECK (p4->makeDefault (file, true, err), "made a default sequence");
        const auto st1 = shared::Settings::load();
        CHECK (p4->njqFiles().size() == 1 && juce::File (st1.startupNjq).existsAsFile() && juce::File (st1.startupNjq).isAChildOf (st1.presets()), "it is in the preset folder's list, and the one to start with");
        auto p6 = std::make_unique<NeoBeatProcessor>();
        for (int i = 0; i < 40 && ! p6->padUsesSample (6); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        CHECK (p6->padUsesSample (6) && p6->pattern.lengthOf (3) == 256, "a new NeoBeat opens with it");
        auto p7 = std::make_unique<NeoBeatProcessor>();
        p7->setStateInformation (st.getData(), (int) st.getSize());
        p7->setParam (p7->paramId (6, "level"), -3.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (700);
        CHECK (std::fabs (p7->getParam (p7->paramId (6, "level")) + 3.0f) < 0.06f, "but a saved project is left as it was saved");
        auto st2 = shared::Settings::load(); st2.startupNjq.clear(); st2.save();
    }

    // a kit made in Batchcut's Kit room (NJQ_FROM_BATCHCUT names its file; skipped without it)
    {
        const auto from = juce::SystemStats::getEnvironmentVariable ("NJQ_FROM_BATCHCUT", {});
        if (from.isNotEmpty())
        {
            std::printf ("11b. a .NJQ from Batchcut's Kit room\n");
            juce::String err;
            auto pb = std::make_unique<NeoBeatProcessor>();
            const bool ok = pb->loadNjq (juce::File (from), err);
            CHECK (ok, "loaded");
            int withSample = 0; for (int l = 0; l < 16; ++l) withSample += pb->padUsesSample (l) ? 1 : 0;
            int hits = 0; for (int l = 0; l < 16; ++l) for (int st = 0; st < pb->pattern.lengthOf (0); ++st) hits += pb->pattern.getIn (0, l, st).vel > 0;
            std::printf ("     %d lanes with a sample, %d hits in scene A, tempo %.1f, swing %.2f\n", withSample, hits, pb->getParam ("tempo"), pb->getParam ("swing"));
            CHECK (withSample == 16 && hits > 20, "16 lanes play Batchcut's sounds, scene A has its pattern");
            CHECK (std::fabs (pb->getParam ("tempo") - 93.0f) < 0.06f && std::fabs (pb->getParam ("swing") - 0.58f) < 0.002f, "at its tempo and swing");
            Host hb (*pb); hb.prepare (48000.0, 512); hb.bpm = 93.0; hb.playing = true;
            hb.run (5.0, false);
            std::printf ("     peak %.2f\n", hb.maxAbs);
            CHECK (! hb.bad && hb.maxAbs > 0.05f && hb.maxAbs <= 1.0f, "and it plays in NeoBeat");
        }
    }

    std::printf ("12. export: WAV, stems and MIDI into a folder\n");
    {
        auto p3 = std::make_unique<NeoBeatProcessor>();
        Host h3 (*p3); h3.prepare (48000.0, 512); h3.playing = false; h3.run (0.1, false);
        p3->project.padFx[2].inserts[0] = nb::CellFx::library (nb::MatrixFx::Freezing); p3->projectChanged();
        NeoBeatProcessor::ExportOptions o; o.folder = tmp.getChildFile ("export"); o.name = "Host test"; o.stems = true; o.range.mode = 2; o.range.barFrom = 0; o.range.barTo = 1;
        CHECK (p3->startExport (o), "started");
        // (seconds in a release build; a debug build with sanitizers is about 18 times slower)
        for (int i = 0; i < 12000 && p3->exporting(); ++i) juce::Thread::sleep (50);
        const auto files = p3->exportedFiles();
        std::printf ("     %s\n", p3->exportResult().toRawUTF8());
        int wavs = 0, mids = 0; juce::int64 mixSize = 0;
        for (const auto& f : files) { const juce::File ff (f); if (ff.hasFileExtension ("wav")) { ++wavs; if (ff.getFileName() == "Host test.wav") mixSize = ff.getSize(); } if (ff.hasFileExtension ("mid")) ++mids; }
        CHECK (! p3->exporting() && wavs >= 4 && mids == 1, "the mix, a stem for each lane that plays, and a MIDI file");
        CHECK (mixSize > (juce::int64) (2.0 * 48000 * 6), "the mix is at least the bar (24 bit stereo), plus its tail");
    }
    tmp.deleteRecursively();

    proc2.reset();
    proc.reset();
    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
