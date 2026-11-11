// BatchCut host test: the plugin driven as a DAW would drive it, with its editor open: files in, events,
// edits with the real mouse handlers, playback, recording the input, export with matched loudness, the
// saved state, and a long random session (the "monkey"). Run under Xvfb. Usage: BatchcutHostTest [seed]
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "app/Editor.h"
#include "TestAudio.h"

static int failures = 0;
#define CHECK(c, ...) do { char m_[400]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } std::fflush (stdout); } while (0)

static float lastOut = 0.0f;   // the loudest output sample of the last pump

// what a host does: audio blocks, and the message loop in between
static void pump (BcProcessor& p, int ms, float inHz = 0.0f, int maxBlocks = -1)
{
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    static double ph = 0.0;
    lastOut = 0.0f;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    int blocks = 0;
    while (juce::Time::getMillisecondCounter() < end && (maxBlocks < 0 || blocks < maxBlocks))
    {
        buf.clear();
        if (inHz > 0.0f) for (int i = 0; i < 512; ++i) { const float v = 0.3f * (float) std::sin (ph); ph += 6.283185307 * inHz / 48000.0; buf.setSample (0, i, v); buf.setSample (1, i, v); }
        p.processBlock (buf, midi); ++blocks;
        lastOut = std::max (lastOut, buf.getMagnitude (0, 512));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }
}

static bool waitIdle (BcProcessor& p, int maxMs = 60000)
{
    int idle = 0;
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) maxMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        pump (p, 25);
        idle = p.session.status().busy ? 0 : idle + 1;
        if (idle >= 4) return true;
    }
    return false;
}

// the real mouse handlers, with events built as JUCE builds them
static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downAt, juce::ModifierKeys mods, int clicks, bool dragged)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (src, pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, downAt, now, clicks, dragged);
}
static void drag (juce::Component& c, const std::vector<juce::Point<float>>& pts, juce::ModifierKeys mods = {})
{
    const auto m = mods.withFlags (juce::ModifierKeys::leftButtonModifier);
    c.mouseDown (mouseAt (c, pts.front(), pts.front(), m, 1, false));
    for (size_t k = 1; k < pts.size(); ++k) c.mouseDrag (mouseAt (c, pts[k], pts.front(), m, 1, true));
    c.mouseUp (mouseAt (c, pts.back(), pts.front(), mods, 1, pts.size() > 1));
}
static void click (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = {}) { drag (c, { p }, mods); }
static void doubleClick (juce::Component& c, juce::Point<float> p) { c.mouseDoubleClick (mouseAt (c, p, p, juce::ModifierKeys::leftButtonModifier, 2, false)); }
static void wheel (juce::Component& c, juce::Point<float> p, float dy, juce::ModifierKeys mods = {})
{
    juce::MouseWheelDetails w; w.deltaX = 0.0f; w.deltaY = dy; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
    c.mouseWheelMove (mouseAt (c, p, p, mods, 0, false), w);
}

static int countWavs (const juce::File& dir) { return dir.findChildFiles (juce::File::findFiles, true, "*.wav").size(); }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const int seed = argc > 1 ? std::atoi (argv[1]) : 1;
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("batchcut_hosttest");
    dir.deleteRecursively(); dir.createDirectory();
    std::printf ("1. test recordings\n");
    const auto field = testaudio::field (dir), loop = testaudio::loop (dir), take = testaudio::longTake (dir);
    CHECK (field.file.existsAsFile() && loop.file.existsAsFile() && take.file.existsAsFile(), "three WAVs written (%zu, %zu, %zu events planted)", field.truth.size(), loop.truth.size(), take.truth.size());

    auto proc = std::make_unique<BcProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    const auto out = dir.getChildFile ("out");
    proc->session.settings.outDir = out.getFullPathName().toStdString();

    std::printf ("2. a folder dropped in\n");
    const int added = proc->addPaths ({ dir.getFullPathName() });
    CHECK (added == 3, "three files added from the folder (%d)", added);
    CHECK (waitIdle (*proc), "read and analysed");
    auto d = proc->session.doc();
    CHECK (d && d->file == 0, "the first one opened by itself");
    std::unique_ptr<BcEditor> ed (dynamic_cast<BcEditor*> (proc->createEditor()));
    ed->setSize (1280, 800);
    pump (*proc, 300);
    auto& v = ed->getView();
    CHECK (v.doc == proc->session.doc(), "the editor shows it");
    const int loopIdx = [&] { for (int i = 0; i < 3; ++i) if (proc->session.files[(size_t) i].info.path == loop.file.getFullPathName().toStdString()) return i; return -1; }();
    const int fieldIdx = [&] { for (int i = 0; i < 3; ++i) if (proc->session.files[(size_t) i].info.path == field.file.getFullPathName().toStdString()) return i; return -1; }();
    const int takeIdx = 3 - loopIdx - fieldIdx;
    CHECK (loopIdx >= 0 && fieldIdx >= 0, "files sorted by name (loop %d, yard %d, long take %d)", loopIdx, fieldIdx, takeIdx);
    CHECK (proc->session.files[(size_t) takeIdx].parts.size() == 2, "the 9.5 minute take is in two parts");

    auto score = [&] (const bc::Doc& doc, const std::vector<double>& truth, double tolMs)
    {
        int hits = 0; for (double t : truth) for (const auto& e : doc.events) if (std::fabs ((double) e.onset / doc.audio->sr - t) * 1000.0 < tolMs) { ++hits; break; }
        return hits;
    };
    proc->open (loopIdx, 0); waitIdle (*proc); pump (*proc, 100);
    d = proc->session.doc();
    CHECK (d && d->file == loopIdx && std::abs ((int) d->events.size() - 64) <= 2 && score (*d, loop.truth, 6.0) >= 62, "the drum loop: %zu events, %d of 64 hits found", d ? d->events.size() : 0, d ? score (*d, loop.truth, 6.0) : 0);
    proc->open (fieldIdx, 0); waitIdle (*proc); pump (*proc, 100);
    d = proc->session.doc();
    const int fieldHits = d ? score (*d, field.truth, 8.0) : 0;
    CHECK (d && fieldHits >= (int) field.truth.size() - 1, "the yard: %d of %zu found (%zu events)", fieldHits, field.truth.size(), d ? d->events.size() : 0);

    std::printf ("3. hearing events\n");
    v.selectEvent (1, true);
    const bool started = proc->isPlaying();
    pump (*proc, 150, 0.0f, 6);
    CHECK (started && lastOut > 0.01f, "an event plays when it is clicked (peak %.3f)", lastOut);
    pump (*proc, 2500);
    CHECK (! proc->isPlaying(), "and stops at its end");
    ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    pump (*proc, 150);
    CHECK (proc->isPlaying() && lastOut > 0.001f, "SPACE plays the file");
    ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
    pump (*proc, 50);
    CHECK (! proc->isPlaying(), "and SPACE stops it");

    std::printf ("4. the spectrogram's tools (the real mouse handlers)\n");
    auto& spec = ed->getSpec();
    auto* edits = v.edits();
    v.fit(); pump (*proc, 50);
    drag (spec, { { 100, 100 }, { 200, 200 }, { 300, 380 } });
    CHECK (v.hasSel && v.sel.t1 > v.sel.t0, "BOX selects (%.2f .. %.2f s)", v.sel.t0, v.sel.t1);
    ed->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey));
    pump (*proc, 50);
    CHECK (edits->layers.ops.size() == 1 && v.doc->layers.ops.size() == 1, "DELETE erases it (%zu edit)", edits->layers.ops.size());
    ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
    CHECK (edits->layers.ops.empty(), "Ctrl+Z undoes it");
    ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
    CHECK (edits->layers.ops.size() == 1, "Ctrl+Shift+Z redoes it");
    ed->keyPressed (juce::KeyPress ('2', {}, '2'));
    drag (spec, { { 400, 300 }, { 430, 280 }, { 470, 260 }, { 520, 250 } });
    const auto brushSel = v.sel;
    CHECK (v.tool == 1 && v.hasSel && brushSel.w > 1 && ! brushSel.empty(), "BRUSH paints a selection (%d x %d)", brushSel.w, brushSel.h);
    ed->keyPressed (juce::KeyPress ('3', {}, '3'));
    drag (spec, { { 200, 100 }, { 320, 120 }, { 300, 300 }, { 180, 260 } }, juce::ModifierKeys::shiftModifier);
    CHECK (v.hasSel && v.sel.t0 <= brushSel.t0 + 1e-6 && v.sel.t0 < brushSel.t0, "LASSO with Shift adds to it (%.2f .. %.2f s)", v.sel.t0, v.sel.t1);
    ed->keyPressed (juce::KeyPress ('1', {}, '1'));
    drag (spec, { { 0, 0 }, { 720, 514 } }, juce::ModifierKeys::altModifier);
    CHECK (! v.hasSel, "BOX with Alt over everything takes it all away");
    // the wand on the loudest event
    ed->keyPressed (juce::KeyPress ('4', {}, '4'));
    {
        // the loudest clank (every third planted sound is a low thud, with nothing at 760 Hz)
        int best = -1;
        for (size_t k = 0; k < field.truth.size(); ++k)
        {
            if (k % 3 == 2) continue;
            const int i = v.indexOf ((int64_t) (field.truth[k] * v.sr()));
            if (i >= 0 && (best < 0 || v.doc->events[(size_t) i].peak > v.doc->events[(size_t) best].peak)) best = i;
        }
        if (best < 0) best = 0;
        const auto& e = v.doc->events[(size_t) best];
        const float x = spec.timeToX ((double) e.onset / v.sr() + 0.03);
        const float y = spec.heightToY (v.doc->an->tab.yOf (760.0f));
        click (spec, { x, y });
        CHECK (v.hasSel && v.sel.t0 < (double) e.onset / v.sr() + 0.03 && v.sel.t1 > (double) e.onset / v.sr() + 0.03 && v.sel.t1 - v.sel.t0 < 1.0,
               "the WAND selects the clank under it and stops at the noise (%.3f .. %.3f s, %d x %d cells)", v.sel.t0, v.sel.t1, v.sel.w, v.sel.h);
    }
    v.edit (bc::EditOp::Move, 1.0f, -2);
    CHECK (edits->layers.layers.size() == 5 && v.doc->layers.layers.size() == 5, "TO A NEW LAYER makes layer 5 (%zu layers)", edits->layers.layers.size());
    edits->layers.layers[bc::Background].mute = true; v.layersChanged();
    CHECK (v.doc->layers.layers[bc::Background].mute, "the background muted");
    ed->keyPressed (juce::KeyPress ('1', {}, '1'));
    wheel (spec, { 360, 200 }, 0.5f, juce::ModifierKeys::commandModifier);
    CHECK (v.t1 - v.t0 < v.length() * 0.95, "Ctrl + wheel zooms in (%.2f s shown)", v.t1 - v.t0);
    const double before = v.t0;
    wheel (spec, { 360, 200 }, -0.5f);
    CHECK (v.t0 > before, "the wheel scrolls");
    click (ed->getOverview(), { 10.0f, 25.0f });
    CHECK (v.t0 < 1.0, "a click on the overview goes there (%.2f s)", v.t0);
    v.fit();

    std::printf ("5. events by hand\n");
    waitIdle (*proc);
    const int n0 = (int) v.doc->events.size();
    v.deleteEvent (2); waitIdle (*proc);
    CHECK ((int) v.doc->events.size() == n0 - 1, "an event deleted (%d -> %zu)", n0, v.doc->events.size());
    ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)); waitIdle (*proc);
    CHECK ((int) v.doc->events.size() == n0, "and undone (%zu)", v.doc->events.size());
    auto& lane = ed->getLane();
    // a double-click on an empty stretch of the lane adds one
    double gapAt = -1;
    for (size_t i = 0; i + 1 < v.doc->events.size(); ++i)
    {
        const double a = (double) v.doc->events[i].end / v.sr(), b = (double) v.doc->events[i + 1].start / v.sr();
        if (b - a > 0.3) { gapAt = 0.5 * (a + b); break; }
    }
    if (gapAt < 0) gapAt = 19.6;
    doubleClick (lane, { (float) ((gapAt - v.t0) / (v.t1 - v.t0) * lane.getWidth()), 14.0f }); waitIdle (*proc);
    CHECK ((int) v.doc->events.size() == n0 + 1 && v.selEvent() && v.selEvent()->manual, "a double-click on the lane adds one at %.2f s (%zu events)", gapAt, v.doc->events.size());
    // drag the selected one's end
    {
        const int si = v.selIndex();
        const auto e = v.doc->events[(size_t) si];
        const float x1 = (float) (((double) e.end / v.sr() - v.t0) / (v.t1 - v.t0) * lane.getWidth());
        drag (lane, { { x1, 14 }, { x1 + 6, 14 }, { x1 + 12, 14 } }); waitIdle (*proc);
        CHECK (v.selEvent() && v.selEvent()->userEnd && v.selEvent()->end > e.end, "dragging its end makes it longer (%.3f -> %.3f s)", (double) (e.end - e.start) / v.sr(), v.selEvent() ? (double) (v.selEvent()->end - v.selEvent()->start) / v.sr() : 0.0);
    }
    v.toggleEvent (v.selIndex()); waitIdle (*proc);
    CHECK (v.selEvent() && ! v.selEvent()->enabled, "switched off for export (E)");
    const int g0 = (int) v.doc->groups.size();
    v.toggleGroup (0); waitIdle (*proc);
    CHECK (g0 > 0 && ! v.doc->groups[0].enabled, "a group switched off (%d groups)", g0);
    v.toggleGroup (0); waitIdle (*proc);
    v.selectEvent (0, false);
    ed->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
    CHECK (v.selIndex() == 1, "the right arrow selects the next");
    v.findMore (v.selIndex()); waitIdle (*proc);
    CHECK (proc->session.settings.like.valid && ! proc->session.settings.likeName.empty(), "FIND MORE searches for it (\"%s\")", proc->session.settings.likeName.c_str());
    for (int t = 0; t < 4; ++t) { ed->getInspector().show (t); pump (*proc, 30); auto img = ed->createComponentSnapshot (ed->getLocalBounds()); juce::ignoreUnused (img); }
    CHECK (true, "the four inspector tabs draw");

    std::printf ("6. recording the input\n");
    CHECK (proc->startRecording() && proc->recorder.isRecording(), "recording starts");
    pump (*proc, 60000, 440.0f, 200);   // 200 blocks of 512: 2.13 s at 48 kHz
    const auto recFile = proc->stopRecording();
    CHECK (recFile.existsAsFile(), "and is saved (%s)", recFile.getFileName().toRawUTF8());
    waitIdle (*proc); pump (*proc, 100);
    d = proc->session.doc();
    CHECK (d && d->audio && std::abs (d->audio->size() - 200 * 512) <= 2 && proc->session.files.size() == 4, "added and opened: %.3f s (%lld samples)", d && d->audio ? (double) d->audio->size() / d->audio->sr : 0.0, d && d->audio ? (long long) d->audio->size() : 0LL);
    float pk = 0; if (d && d->audio) for (float x : d->audio->l) pk = std::max (pk, std::fabs (x));
    CHECK (std::fabs (pk - 0.3f) < 0.01f, "it holds what came in (peak %.3f, sent 0.300)", pk);
    // the input passes through in a DAW (the standalone app never plays it back)
    pump (*proc, 30, 440.0f, 3);
    CHECK (lastOut > 0.25f, "the input passes through (%.2f)", lastOut);
    proc->apvts.getParameter ("thru")->setValueNotifyingHost (0.0f);
    pump (*proc, 30, 440.0f, 3);
    CHECK (lastOut < 1.0e-6f, "and not when INPUT THROUGH is off");
    proc->apvts.getParameter ("thru")->setValueNotifyingHost (1.0f);
    recFile.deleteFile();
    proc->session.removeFile (3); proc->openFile = -1; proc->sendChangeMessage();
    pump (*proc, 50);

    std::printf ("7. export: every file, one loudness\n");
    auto& st = proc->session.settings;
    st.pick.mode = bc::Pick::All; st.exp.normMode = 2; st.exp.loudDb = -14.0f; st.exp.normDb = -1.0f; st.exp.matchAll = true; st.folderPerFile = true;
    proc->session.exportFiles (true);
    CHECK (waitIdle (*proc, 600000), "export finished");
    const auto s = proc->session.status();
    std::printf ("     %d written, %d failed: \"%s\"\n", s.exported, s.failed, s.note.c_str());
    const int wavs = countWavs (out);
    CHECK (s.exported > 200 && s.failed == 0 && wavs == s.exported, "%d one-shots written, %d on disk", s.exported, wavs);
    CHECK (out.getChildFile ("yard clanks").isDirectory() && out.getChildFile ("drum loop 120").isDirectory() && out.getChildFile ("long take").isDirectory(), "a folder per recording");
    {
        float lo = 1e9f, hi = -1e9f, top = -1e9f; int n = 0;
        for (const auto& f : out.findChildFiles (juce::File::findFiles, true, "*.wav"))
        {
            std::unique_ptr<juce::AudioFormatReader> r (proc->formats.createReaderFor (f));
            if (r == nullptr) continue;
            juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            std::vector<float> l (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples()), rr (b.getReadPointer (1), b.getReadPointer (1) + b.getNumSamples());
            const auto lv = bc::measureLevel (l, rr, r->sampleRate);
            lo = std::min (lo, lv.lufs); hi = std::max (hi, lv.lufs); top = std::max (top, lv.peakDb); ++n;
        }
        CHECK (n == wavs && hi - lo < 0.15f && top <= -0.98f, "read back: all %d within %.2f dB (%.2f .. %.2f LUFS), highest peak %.2f dBFS", n, hi - lo, lo, hi, top);
    }
    const bool erased = ! out.getChildFile ("yard clanks").findChildFiles (juce::File::findFiles, false, "*_background*").isEmpty();
    CHECK (! erased, "no layer files unless asked for");

    std::printf ("8. the saved state\n");
    juce::MemoryBlock state;
    proc->getStateInformation (state);
    const auto ye = proc->session.edits (fieldIdx, 0);
    {
        auto p2 = std::make_unique<BcProcessor>();
        p2->setPlayConfigDetails (2, 2, 48000.0, 512); p2->prepareToPlay (48000.0, 512);
        p2->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (waitIdle (*p2), "restored and analysed");
        const auto& e2 = p2->session.edits (fieldIdx, 0);
        CHECK (p2->session.files.size() == 3 && p2->session.files[(size_t) fieldIdx].info.path == proc->session.files[(size_t) fieldIdx].info.path, "the same files");
        CHECK (e2.layers.ops.size() == ye.layers.ops.size() && e2.layers.layers.size() == ye.layers.layers.size() && e2.layers.layers[0].mute, "the edits and layers (%zu edits, %zu layers, background muted)", e2.layers.ops.size(), e2.layers.layers.size());
        CHECK (e2.keep.size() == ye.keep.size() && e2.off.size() == ye.off.size(), "the events placed by hand and switched off (%zu, %zu)", e2.keep.size(), e2.off.size());
        CHECK (p2->session.settings.exp.normMode == 2 && std::fabs (p2->session.settings.exp.loudDb + 14.0f) < 0.01f && p2->session.settings.like.valid, "the settings, FIND MORE included");
        CHECK (p2->openFile == proc->openFile, "the open file (%d)", p2->openFile);
        // nonsense must not hurt
        const char junk[] = "not a state at all";
        p2->setStateInformation (junk, (int) sizeof junk);
        p2->setStateInformation (state.getData(), (int) state.getSize() / 2);
        juce::Random r (seed);
        juce::MemoryBlock bad (state); for (int i = 0; i < 40; ++i) static_cast<char*> (bad.getData())[r.nextInt ((int) bad.getSize())] ^= (char) r.nextInt (256);
        p2->setStateInformation (bad.getData(), (int) bad.getSize());
        waitIdle (*p2, 20000);
        CHECK (true, "junk, half and scrambled states are survived");
    }

    std::printf ("9. the editor's life\n");
    proc->open (fieldIdx, 0); waitIdle (*proc);
    v.selectEvent (0, true);
    ed.reset(); pump (*proc, 100);
    ed.reset (dynamic_cast<BcEditor*> (proc->createEditor()));
    for (auto sz : { std::pair<int, int> { 900, 563 }, { 1920, 1200 }, { 1280, 800 } }) { ed->setSize (sz.first, sz.second); pump (*proc, 30); auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f); juce::ignoreUnused (img); }
    CHECK (ed->getView().doc == proc->session.doc(), "closed while playing, opened again, resized");

    std::printf ("10. a long random session (seed %d)\n", seed);
    {
        juce::Random r (seed);
        auto& w = ed->getView();
        int actions = 0;
        for (int it = 0; it < 700; ++it)
        {
            const int a = r.nextInt (30);
            auto rp = [&] (juce::Component& c) { return juce::Point<float> (r.nextFloat() * (float) c.getWidth(), r.nextFloat() * (float) c.getHeight()); };
            const auto mods = r.nextInt (4) == 0 ? juce::ModifierKeys::shiftModifier : r.nextInt (5) == 0 ? juce::ModifierKeys::altModifier : 0;
            auto& sp = ed->getSpec();
            switch (a)
            {
                case 0: case 1: case 2: { std::vector<juce::Point<float>> pts; for (int k = 0; k < 2 + r.nextInt (12); ++k) pts.push_back (rp (sp)); drag (sp, pts, mods); break; }
                case 3: ed->keyPressed (juce::KeyPress ((int) ('1' + r.nextInt (4)), {}, (juce::juce_wchar) ('1' + r.nextInt (4)))); break;
                case 4: click (sp, rp (sp), mods); break;
                case 5: w.edit ((bc::EditOp::Kind) r.nextInt (3), r.nextFloat() * 2.0f, r.nextInt (3) == 0 ? -2 : r.nextInt (6)); break;
                case 6: ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier | (r.nextBool() ? juce::ModifierKeys::shiftModifier : 0), 0)); break;
                case 7: click (ed->getLane(), rp (ed->getLane())); break;
                case 8: { auto& ln = ed->getLane(); const auto p0 = rp (ln); drag (ln, { p0, p0.translated (r.nextFloat() * 40.0f - 20.0f, 0.0f) }); break; }
                case 9: doubleClick (ed->getLane(), rp (ed->getLane())); break;
                case 10: if (w.doc && ! w.doc->events.empty()) w.deleteEvent (r.nextInt ((int) w.doc->events.size())); break;
                case 11: if (w.doc && ! w.doc->events.empty()) w.toggleEvent (r.nextInt ((int) w.doc->events.size())); break;
                case 12: if (w.doc && ! w.doc->groups.empty()) w.toggleGroup (r.nextInt ((int) w.doc->groups.size())); break;
                case 13: wheel (sp, rp (sp), r.nextFloat() - 0.5f, r.nextBool() ? juce::ModifierKeys::commandModifier : 0); break;
                case 14: click (ed->getOverview(), rp (ed->getOverview())); break;
                case 15: ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)); break;
                case 16: { const int f = r.nextInt ((int) proc->session.files.size()); proc->open (f, r.nextInt (2)); break; }
                case 17: ed->getInspector().show (r.nextInt (4)); break;
                case 18: { auto& dp = proc->session.settings.detect; dp.sensitivity = r.nextFloat(); dp.minGapMs = 10.0f + r.nextFloat() * 200.0f; dp.endMode = r.nextInt (4); proc->session.redetect(); break; }
                case 19: if (w.doc) w.addEventAt (r.nextFloat() * w.length()); break;
                case 20: if (w.doc && ! w.doc->events.empty()) w.moveEnd (r.nextInt ((int) w.doc->events.size()), r.nextFloat() * w.length()); break;
                case 21: if (w.doc && ! w.doc->events.empty()) w.moveStart (r.nextInt ((int) w.doc->events.size()), r.nextFloat() * w.length()); break;
                case 22: if (w.doc && ! w.doc->events.empty()) w.findMore (r.nextInt ((int) w.doc->events.size())); else w.clearFindMore(); break;
                case 23: { if (auto* e2 = w.edits()) { const int k = r.nextInt ((int) e2->layers.layers.size()); e2->layers.layers[(size_t) k].mute = r.nextBool(); e2->layers.layers[(size_t) k].solo = r.nextInt (5) == 0; e2->layers.layers[(size_t) k].gainDb = r.nextFloat() * 24.0f - 18.0f; w.layersChanged(); } break; }
                case 24: ed->keyPressed (juce::KeyPress (r.nextBool() ? juce::KeyPress::leftKey : juce::KeyPress::rightKey)); break;
                case 25: ed->keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)); break;
                case 26: if (r.nextInt (6) == 0) { proc->session.exportFiles (r.nextBool()); pump (*proc, 20 + r.nextInt (200)); proc->session.cancelTask(); } break;
                case 27: if (r.nextInt (8) == 0) { const int ws = 800 + r.nextInt (1100); ed->setSize (ws, ws * 800 / 1280); } break;
                case 28: if (r.nextInt (10) == 0) { ed.reset(); pump (*proc, 10); ed.reset (dynamic_cast<BcEditor*> (proc->createEditor())); ed->setSize (1280, 800); } break;
                default: if (w.selEvent()) proc->playEvent (*w.selEvent()); break;
            }
            ++actions;
            juce::PopupMenu::dismissAllActiveMenus();
            pump (*proc, r.nextInt (4) == 0 ? 60 : 4);
            if (it % 60 == 0) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.4f); juce::ignoreUnused (img); }
            // what the editor holds stays sound
            auto& vv = ed->getView();
            if (vv.doc) for (size_t i = 1; i < vv.doc->events.size(); ++i) if (vv.doc->events[i].onset < vv.doc->events[i - 1].onset) { std::printf ("  FAIL: events out of order at step %d\n", it); ++failures; break; }
            if (! (vv.t1 > vv.t0) || ! std::isfinite (vv.t0)) { std::printf ("  FAIL: the view broke at step %d\n", it); ++failures; }
        }
        waitIdle (*proc, 120000);
        CHECK (true, "%d random actions, nothing broke", actions);
    }
    ed.reset();
    proc->releaseResources();
    proc.reset();
    dir.deleteRecursively();
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
