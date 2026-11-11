// Host stress test and screenshots for the stand-alone effects (Linux, built once per effect).
// FxHostTest [outdir]: plays, fuzzes every parameter, loads a file, catches hits, round-trips the state,
// changes rates, clicks at random on the window, and writes PNGs of it.
#include "fx/FxEditor.h"
#include <cstdio>

static int failures = 0;
#define CHECK(c, what) do { if (c) std::printf ("  ok:   %s\n", what); else { std::printf ("  FAIL: %s\n", what); ++failures; } } while (0)

struct Host : juce::AudioPlayHead
{
    FxProcessor& p; double sr = 48000.0; int block = 512; double ppq = 0.0; bool playing = true, bad = false, feedHits = false; float peak = 0.0f;
    juce::int64 t = 0; juce::Random r { 5 };
    explicit Host (FxProcessor& proc) : p (proc) { p.setPlayHead (this); }
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i; i.setBpm (124.0); i.setPpqPosition (ppq); i.setIsPlaying (playing); i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return i;
    }
    void prepare (double rate, int b) { sr = rate; block = b; p.setRateAndBufferSizeDetails (sr, block); p.prepareToPlay (sr, block); }
    void run (double seconds, bool notes = false)
    {
        juce::AudioBuffer<float> buf (2, block);
        for (int done = 0; done < (int) (seconds * sr); done += block)
        {
            for (int i = 0; i < block; ++i, ++t)
            {
                const int ph = (int) (t % (juce::int64) (sr * 0.4));   // a click every 0.4 s
                const float v = feedHits && ph < 800 ? 0.6f * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) ph / 150.0f) : 0.0f;
                buf.setSample (0, i, v); buf.setSample (1, i, v);
            }
            juce::MidiBuffer m;
            if (notes && r.nextInt (20) == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36 + r.nextInt (48), (juce::uint8) (40 + r.nextInt (80))), r.nextInt (block));
            p.processBlock (buf, m);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) { const float v = buf.getSample (c, i); if (! std::isfinite (v)) bad = true; peak = std::max (peak, std::fabs (v)); }
            ppq += 124.0 / 60.0 * block / sr;
            if ((done / block) % 6 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }
    bool settle (int ms = 30000)
    {
        for (int i = 0; i < ms / 20; ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); if (i > 12 && ! p.engine().renderer.busy()) return true; }
        return false;
    }
};

static void shot (juce::Component& c, const juce::File& f)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    f.deleteFile();
    juce::FileOutputStream out (f);
    juce::PNGImageFormat().writeImageToStream (img, out);
}

static void walk (juce::Component& c, juce::Array<juce::Component*>& out) { for (auto* k : c.getChildren()) { if (k->isVisible()) { out.add (k); walk (*k, out); } } }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "fxshots");
    out.createDirectory();
    const juce::String tag = FxProcessor::isFriction() ? "friction" : "bounce";
    auto proc = std::make_unique<FxProcessor>();
    Host host (*proc);
    host.prepare (48000.0, 512);

    std::printf ("%s\n1. load mode: the built-in hit renders and plays\n", proc->getName().toRawUTF8());
    CHECK (host.settle(), "the first render finishes");
    proc->engine().trigger();
    host.run (1.5);
    CHECK (host.peak > 0.01f && ! host.bad, "PLAY is heard, finite");
    proc->setValue ("loop", 1.0f); host.peak = 0.0f; host.run (4.5);
    CHECK (host.peak > 0.01f, "LOOP plays it on the bar");
    proc->setValue ("loop", 0.0f);

    std::printf ("2. the window\n");
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (FxEditor::kW, FxEditor::kH);
    ed->setVisible (true);   // not on the desktop: Xvfb has no window manager
    host.run (0.5); host.settle();
    shot (*ed, out.getChildFile (tag + "_1_load.png"));
    if (FxProcessor::isFriction())
    {
        proc->setCurrentProgram (1); host.settle (60000);
        nb::StretchLine l; l.fLo = 400; l.fHi = 1600; proc->lines.push_back (l); l.fLo = 3000; l.fHi = 9000; l.offset = 0.1f; proc->lines.push_back (l); proc->linesChanged();
        host.settle (60000);
        shot (*ed, out.getChildFile (tag + "_2_preset_lines.png"));
    }
    else { proc->setCurrentProgram (2); host.settle(); proc->engine().trigger(); host.run (0.7); shot (*ed, out.getChildFile (tag + "_2_pinball.png")); }
    proc->setCurrentProgram (0);

    std::printf ("3. a file, and the state round trip\n");
    {
        const auto wav = out.getChildFile ("hit_test.wav");
        juce::AudioBuffer<float> b (2, 24000);
        for (int i = 0; i < 24000; ++i) { const float v = 0.5f * std::sin (i * 0.05f) * std::exp (-i / 4000.0f); b.setSample (0, i, v); b.setSample (1, i, v); }
        wav.deleteFile();
        { juce::WavAudioFormat f; std::unique_ptr<juce::OutputStream> os (wav.createOutputStream().release()); auto w = f.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (44100.0).withNumChannels (2).withBitsPerSample (16)); if (w) w->writeFromAudioSampleBuffer (b, 0, 24000); }
        CHECK (proc->loadFile (wav), "a WAV loads");
        CHECK (! proc->loadFile (out.getChildFile ("nope.wav")), "a missing file is refused");
        CHECK (proc->hitName() == "hit_test", "its name shows");
        host.settle();
        proc->setValue ("swing", 0.7f);
        juce::MemoryBlock state; proc->getStateInformation (state);
        auto other = std::make_unique<FxProcessor>();
        other->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (other->hitName() == "hit_test" && other->currentHit() && std::abs ((int) other->currentHit()->left.size() - 24000) < 4, "the hit travels in the state");
        CHECK (std::fabs (other->value ("swing") - 0.7f) < 1.0e-4f, "the knobs travel in the state");
        CHECK (other->lines.size() == proc->lines.size(), "the lines travel in the state");
        const char junk[] = "not a state"; other->setStateInformation (junk, sizeof (junk));
        CHECK (other->hitName() == "hit_test", "a broken state is ignored");
        const auto saved = out.getChildFile (tag + "_result.wav");
        CHECK (proc->saveShown (saved) && saved.getSize() > 1000, "SAVE WAV writes the result");
        proc->useDefaultHit();
    }

    std::printf ("4. catch hits\n");
    {
        proc->setValue ("mode", 1.0f); proc->setValue ("threshold", -20.0f);
        host.feedHits = true; host.peak = 0.0f;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        // real time, so the renders can keep up as they would in a host
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 4000.0) { host.run (0.02); juce::Thread::sleep (18); }
        CHECK (proc->engine().hits.load() >= 6, "the clicks on the input are caught");
        CHECK (proc->engine().renderer.captures() >= 3, "and rendered");
        CHECK (! host.bad, "finite");
        shot (*ed, out.getChildFile (tag + "_3_catch.png"));
        CHECK (proc->keepLastCapture() && proc->hitName() == "Caught hit" && proc->value ("mode") < 0.5f, "KEEP makes the last caught hit the loaded hit");
        host.feedHits = false;
    }

    std::printf ("5. every parameter fuzzed while it plays, the window open\n");
    {
        juce::Random r (11);
        for (int round = 0; round < 60; ++round)
        {
            for (auto* p : proc->getParameters()) if (r.nextInt (3) == 0) p->setValueNotifyingHost (r.nextFloat());
            host.feedHits = round % 2 == 0;
            if (round % 7 == 0) proc->setCurrentProgram (r.nextInt (proc->getNumPrograms()));
            if (round % 5 == 0) proc->engine().trigger();
            host.run (0.15, true);
        }
        CHECK (! host.bad, "finite through 60 rounds of random settings");
        for (auto* p : proc->getParameters()) p->setValueNotifyingHost (p->getDefaultValue());
        host.feedHits = false;
    }

    std::printf ("6. sample rates and block sizes\n");
    {
        const double rates[] = { 44100.0, 96000.0, 192000.0, 48000.0 }; const int sizes[] = { 32, 64, 1024, 2048, 480 };
        juce::Random r (3);
        for (int i = 0; i < 40; ++i) { host.prepare (rates[r.nextInt (4)], sizes[r.nextInt (5)]); host.feedHits = i % 2 == 1; proc->setValue ("mode", (float) (i % 2)); if (i % 2 == 0) proc->engine().trigger(); host.run (0.12, true); }
        host.feedHits = false; proc->setValue ("mode", 0.0f);
        host.prepare (48000.0, 512); host.settle();
        CHECK (! host.bad, "finite through 40 rate and block changes");
    }

    std::printf ("7. the monkey: 600 random clicks, drags and wheels on the window\n");
    {
        juce::Random r (2);
        int events = 0;
        for (int i = 0; i < 600; ++i)
        {
            juce::Array<juce::Component*> all; walk (*ed, all);
            if (all.isEmpty()) break;
            auto* c = all[r.nextInt (all.size())];
            if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText().containsIgnoreCase ("FILE") || b->getButtonText().containsIgnoreCase ("SAVE")) continue;   // they open system dialogs
            if (dynamic_cast<juce::ComboBox*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c->getParentComponent()) != nullptr) continue;
            juce::Component::SafePointer<juce::Component> safe (c);
            const auto pos = juce::Point<float> (r.nextFloat() * (float) c->getWidth(), r.nextFloat() * (float) c->getHeight());
            const auto src = juce::Desktop::getInstance().getMainMouseSource();
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent down (src, pos, juce::ModifierKeys::leftButtonModifier, 0.5f, 0, 0, 0, 0, c, c, now, pos, now, 1, false);
            c->mouseDown (down);
            if (safe != nullptr && r.nextBool()) { const auto p2 = pos + juce::Point<float> (r.nextFloat() * 60 - 30, r.nextFloat() * 60 - 30); c->mouseDrag (juce::MouseEvent (src, p2, juce::ModifierKeys::leftButtonModifier, 0.5f, 0, 0, 0, 0, c, c, now, pos, now, 1, true)); }
            if (safe != nullptr) c->mouseUp (juce::MouseEvent (src, pos, juce::ModifierKeys(), 0.0f, 0, 0, 0, 0, c, c, now, pos, now, 1, false));
            if (safe != nullptr && r.nextInt (4) == 0) { juce::MouseWheelDetails w; w.deltaX = 0; w.deltaY = r.nextFloat() - 0.5f; w.isReversed = false; w.isSmooth = false; w.isInertial = false; c->mouseWheelMove (down, w); }
            if (safe != nullptr) if (auto* b = dynamic_cast<juce::Button*> (c)) if (r.nextBool()) b->triggerClick();
            ++events;
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
            host.run (0.02);
        }
        std::printf ("     %d events\n", events);
        CHECK (! host.bad, "finite after the monkey");
        juce::MemoryBlock state; proc->getStateInformation (state);
        CHECK (state.getSize() > 100, "the state still saves");
    }
    ed.reset();
    host.settle (5000);
    proc.reset();
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
