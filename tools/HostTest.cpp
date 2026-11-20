// BatchCut host test: the plugin driven as a DAW would drive it, with its editor open: files in, events,
// edits with the real mouse handlers, playback, recording the input, export with matched loudness, the
// saved state, and a long random session (the "monkey"). Run under Xvfb. Usage: BatchcutHostTest [seed]
#include <juce_audio_utils/juce_audio_utils.h>
#include "app/Processor.h"
#include "app/Editor.h"
#include "TestAudio.h"
#include "neobeat/shared/NjqInstrument.h"

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
    // (the learner and the preset folder of this test are its own, not yours)
    const auto sharedDir = dir.getSiblingFile ("batchcut_hosttest_shared");
    sharedDir.deleteRecursively(); sharedDir.createDirectory();
   #if JUCE_WINDOWS
    _putenv_s ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8());
   #else
    setenv ("MARBELITE_SHARED_DIR", sharedDir.getFullPathName().toRawUTF8(), 1);
   #endif
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

    std::printf ("10. the kit room\n");
    {
        proc->open (loopIdx, 0); waitIdle (*proc); pump (*proc, 100);
        proc->kitGather (false);
        CHECK (waitIdle (*proc), "the open file's one-shots gathered");
        pump (*proc, 150);
        const int fromLoop = (int) proc->kit.pool.size();
        CHECK (fromLoop >= 20, "the drum loop gave the pool %d sounds", fromLoop);
        proc->kitGather (false); waitIdle (*proc); pump (*proc, 150);
        CHECK ((int) proc->kit.pool.size() == fromLoop, "gathering the same file again adds nothing (%d)", (int) proc->kit.pool.size());
        proc->kitGather (true); waitIdle (*proc, 180000); pump (*proc, 150);
        const int fromAll = (int) proc->kit.pool.size();
        CHECK (fromAll > fromLoop, "every file: %d sounds", fromAll);
        const int fromFolder = proc->kitAddFolder (out);
        CHECK (fromFolder > 0 && (int) proc->kit.pool.size() == fromAll + fromFolder, "a folder of one-shots: %d more", fromFolder);
        bool finite = true; for (const auto& it : proc->kit.pool) { finite = finite && it->f.valid && std::isfinite (it->quality) && std::isfinite (it->lufs); for (float r : it->roles) finite = finite && std::isfinite (r); }
        CHECK (finite, "every pool sound was listened to (role, quality, loudness)");

        proc->kitChoose();
        int filled = 0; std::vector<int> picked;
        for (const auto& ln : proc->kit.lanes) if (! ln.empty()) { ++filled; picked.push_back (ln.layers[0].item); }
        std::sort (picked.begin(), picked.end());
        CHECK (filled == 16 && std::adjacent_find (picked.begin(), picked.end()) == picked.end(), "auto-fill: %d lanes, no sound twice", filled);
        const auto* kick = proc->kit.item (proc->kit.lanes[0].layers[0].item);
        CHECK (kick != nullptr && kick->f.centroidHz < 1500.0f, "the kick lane got a low sound (centre %.0f Hz)", kick ? kick->f.centroidHz : 0.0f);

        // heard in a pattern
        proc->kitSetPattern (3);
        proc->kitPlayer.playing.store (true);
        pump (*proc, 60000, 0.0f, 300);
        const float playing = lastOut; const int stepNow = proc->kitPlayer.step.load();
        proc->kitPlayer.playing.store (false);
        pump (*proc, 60000, 0.0f, 1400);
        CHECK (playing > 0.02f && playing <= 0.9f && stepNow >= 0, "the kit plays in pattern 4, under the ceiling (peak %.2f, step %d)", playing, stepNow);
        pump (*proc, 60000, 0.0f, 30);
        CHECK (lastOut < 1.0e-4f && proc->kitPlayer.step.load() == -1, "and stops (%.5f)", lastOut);
        proc->kitPlayLane (0); pump (*proc, 60000, 0.0f, 20);
        CHECK (lastOut > 0.01f, "a click on a lane is heard (%.2f)", lastOut);

        // layers, lock, shuffle, undo
        bc::KitSound one, two;
        proc->kit.flatten (2, one);
        const int extra = proc->kit.lanes[4].layers[0].item;
        proc->kitAssign (2, 1, extra, false);
        proc->kit.lanes[2].layers[1].offsetMs = 30.0f; proc->kit.lanes[2].layers[1].semis = -5.0f; proc->kitChanged (2); proc->kitMark();
        proc->kit.flatten (2, two);
        float pk = 0.0f; for (size_t i = 0; i < two.l.size(); ++i) pk = std::max (pk, std::max (std::fabs (two.l[i]), std::fabs (two.r[i])));
        bool differs = two.l.size() != one.l.size(); for (size_t i = 0; ! differs && i < one.l.size(); ++i) differs = std::fabs (one.l[i] - two.l[i]) > 1.0e-4f;
        CHECK (differs && pk <= 0.99f && pk > 0.01f, "a second layer, 30 ms late and 5 st down, is mixed into the lane (peak %.2f)", pk);
        proc->kitUndo(); proc->kitUndo();
        CHECK (proc->kit.lanes[2].layers[1].item < 0, "undo takes the layer away");
        proc->kitRedo(); proc->kitRedo();
        CHECK (proc->kit.lanes[2].layers[1].item == extra && proc->kit.lanes[2].layers[1].semis == -5.0f, "redo brings it back");
        proc->kit.lanes[0].locked = true; proc->kitChanged (0); proc->kitMark();
        const int lockedItem = proc->kit.lanes[0].layers[0].item;
        bool moved = false, kept = true, twice = false;
        for (int k = 0; k < 6; ++k)
        {
            const auto before = proc->kit.lanes;
            proc->kitShuffle();
            kept = kept && proc->kit.lanes[0].layers[0].item == lockedItem;
            for (int l = 1; l < 16; ++l) moved = moved || proc->kit.lanes[(size_t) l].layers[0].item != before[(size_t) l].layers[0].item;
            std::vector<int> u; for (const auto& ln : proc->kit.lanes) if (ln.layers[0].item >= 0) u.push_back (ln.layers[0].item);
            std::sort (u.begin(), u.end()); twice = twice || std::adjacent_find (u.begin(), u.end()) != u.end();
        }
        CHECK (moved && kept && ! twice, "shuffle changes the open lanes, keeps the locked one, uses no sound twice");
        proc->kitNext (5, 1);
        const int rated = proc->kit.lanes[5].layers[0].item;
        proc->kitRate (rated, 5.0f);
        CHECK (proc->kitRating (rated) == 5.0f && shared::learnerFile().existsAsFile(), "five stars are remembered, in the learner both apps share");
        const int odd = proc->kitCandidates (9).back();
        const float fitBefore = proc->kit.pool[(size_t) odd]->roles[9];
        proc->kitAssign (9, 0, odd, true);
        const float fitAfter = proc->kit.pool[(size_t) odd]->roles[9];
        CHECK (fitAfter > fitBefore && fitAfter > 0.3f, "a sound you put on a lane is learned as that lane's kind (fit %.2f, was %.2f)", fitAfter, fitBefore);

        // the file NeoBeat loads
        proc->kit.name = "Yard kit"; proc->kit.bpm = 93.0; proc->kit.swing = 0.58f; proc->kitChanged();
        const auto njqFile = dir.getChildFile ("Yard kit.njq");
        juce::String err;
        const bool savedOk = proc->kitSaveNjq (njqFile, err);
        // (for the round trip into NeoBeat: BATCHCUT_KEEP_NJQ names where a copy goes)
        { const auto keep = juce::SystemStats::getEnvironmentVariable ("BATCHCUT_KEEP_NJQ", {}); if (keep.isNotEmpty()) njqFile.copyFileTo (juce::File (keep)); }
        CHECK (savedOk && njqFile.getSize() > 2000, "saved as .NJQ (%d kB) %s", (int) (njqFile.getSize() / 1024), err.toRawUTF8());
        {
            auto back = std::make_unique<njq::Kit>();
            const bool ok = njq::load (njqFile, *back, err);
            int lanes = 0; float top = 0.0f; bool rates = true;
            for (const auto& ln : back->lanes) if (ln.set && ln.hasSample) { ++lanes; rates = rates && ln.sampleRate > 8000.0 && ln.l.size() == ln.r.size() && ln.l.size() > 16; for (float x : ln.l) top = std::max (top, std::fabs (x)); }
            int hits = 0, want = 0; const auto preset = nb::patternPreset (3);
            for (int l = 0; l < 16; ++l) for (int st = 0; st < preset.pattern.length; ++st) { want += preset.pattern.steps[(size_t) l][(size_t) st].vel > 0; hits += back->scenes[0].steps[(size_t) l][(size_t) st].vel > 0; }
            CHECK (ok && lanes == 16 && rates && top > 0.05f && top <= 1.0f, "read back: %d lanes with their sounds (peak %.2f)", lanes, top);
            CHECK (std::fabs (back->bpm - 93.0) < 0.01 && std::fabs (back->swing - 0.58f) < 0.001f && back->patternIndex == 3 && hits == want && want > 0 && back->name == "Yard kit",
                   "with its tempo (%.0f), swing, name and pattern (%d hits)", back->bpm, hits);
        }

        // the kit in the project
        juce::MemoryBlock state; proc->getStateInformation (state);
        auto other = std::make_unique<BcProcessor>();
        other->setPlayConfigDetails (2, 2, 48000.0, 512); other->prepareToPlay (48000.0, 512);
        other->setStateInformation (state.getData(), (int) state.getSize());
        waitIdle (*other); pump (*other, 100);
        bool same = other->kit.name == "Yard kit" && std::fabs (other->kit.bpm - 93.0) < 0.01 && other->kit.pattern == 3 && other->kit.lanes[0].locked;
        for (int l = 0; l < 16 && same; ++l) for (int y = 0; y < 3 && same; ++y)
        {
            const auto *a = proc->kit.item (proc->kit.lanes[(size_t) l].layers[(size_t) y].item), *b = other->kit.item (other->kit.lanes[(size_t) l].layers[(size_t) y].item);
            same = (a == nullptr) == (b == nullptr) && (a == nullptr || (a->name == b->name && a->l.size() == b->l.size()));
        }
        bc::KitSound sa, sb; proc->kit.flatten (2, sa); other->kit.flatten (2, sb);
        float worst = 0.0f; if (sa.l.size() == sb.l.size()) for (size_t i = 0; i < sa.l.size(); ++i) worst = std::max (worst, std::fabs (sa.l[i] - sb.l[i])); else worst = 1.0f;
        CHECK (same && worst < 2.0e-3f, "the project keeps the kit: lanes, layers and their sounds (%d kB, worst difference %.5f)", (int) (state.getSize() / 1024), worst);
        other->releaseResources(); other.reset();

        // the room itself
        ed->showRoom (1); pump (*proc, 80);
        auto& room = ed->getKitRoom();
        CHECK (room.isVisible() && ! ed->getSpec().isVisible(), "KIT shows the kit room");
        { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 0.5f); juce::ignoreUnused (img); }
        ed->keyPressed (juce::KeyPress (juce::KeyPress::downKey)); ed->keyPressed (juce::KeyPress (juce::KeyPress::downKey));
        CHECK (ed->getView().kitLane == 2, "arrow keys walk the lanes");
        const int was = proc->kit.lanes[2].layers[0].item;
        ed->keyPressed (juce::KeyPress (juce::KeyPress::rightKey));
        CHECK (proc->kit.lanes[2].layers[0].item != was, "right: the next best sound for the lane");
        ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        CHECK (proc->kit.lanes[2].layers[0].item == was, "Ctrl+Z in the kit room undoes it");
        ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)); pump (*proc, 60000, 0.0f, 120);
        CHECK (proc->kitPlayer.playing.load() && lastOut > 0.02f, "Space plays the kit");
        ed->showRoom (0); pump (*proc, 60);
        CHECK (! proc->kitPlayer.playing.load() && ed->getSpec().isVisible() && ! room.isVisible(), "CUT goes back, and the pattern stops");
        proc->kitClearPool();
        int still = 0; for (const auto& ln : proc->kit.lanes) still += ln.empty() ? 0 : 1;
        CHECK (still == 16 && (int) proc->kit.pool.size() <= 17, "clearing the pool keeps the lanes' sounds (%d left in it)", (int) proc->kit.pool.size());
    }

    std::printf ("10b. RANDOM, Clear lanes, Clear all\n");
    const juce::StringArray threeFiles { field.file.getFullPathName(), loop.file.getFullPathName(), take.file.getFullPathName() };
    {
        proc->kitAddFolder (out); pump (*proc, 50);
        const int poolSize = (int) proc->kit.pool.size();
        ed->showRoom (1); pump (*proc, 60);
        proc->kit.lanes[0].locked = true; proc->kitChanged (0); proc->kitMark();
        const int lockedItem = proc->kit.lanes[0].layers[0].item;
        const int deck = [&] { int n = 0; for (const auto& it : proc->kit.pool) n += bc::KitModel::fits (*it, 2) ? 1 : 0; return n; }();
        const int presses = std::min (20, deck);
        std::vector<int> snares; bool kept = true, twice = false;
        for (int k = 0; k < presses; ++k)
        {
            ed->keyPressed (juce::KeyPress ('r', {}, 'r')); pump (*proc, 10);
            kept = kept && proc->kit.lanes[0].layers[0].item == lockedItem;
            snares.push_back (proc->kit.lanes[2].layers[0].item);
            std::vector<int> u; for (const auto& ln : proc->kit.lanes) if (ln.layers[0].item >= 0) u.push_back (ln.layers[0].item);
            std::sort (u.begin(), u.end()); twice = twice || std::adjacent_find (u.begin(), u.end()) != u.end();
        }
        std::sort (snares.begin(), snares.end());
        const bool distinct = std::adjacent_find (snares.begin(), snares.end()) == snares.end() && snares.front() >= 0;
        CHECK (poolSize > 100 && distinct && kept && ! twice, "R (Random) %d times on a pool of %d: %d different snare-lane sounds of %d that fit, the locked lane kept, no sound twice", presses, poolSize, presses, deck);
        CHECK (proc->kitMessage.contains ("still to come") || proc->kitMessage.contains ("have played"), "it says how many are still to come: \"%s\"", proc->kitMessage.toRawUTF8());
        proc->kitAnyLane = true; proc->kitRandom (2);
        CHECK (proc->kitMessage.contains ("Any lane"), "Any lane deals from the whole pool: \"%s\"", proc->kitMessage.toRawUTF8());
        proc->kitAnyLane = false;
        const auto before = proc->kit.lanes;
        proc->kitClearLanes();
        int left = 0; for (const auto& ln : proc->kit.lanes) left += ln.empty() ? 0 : 1;
        CHECK (left == 1 && proc->kit.lanes[0].layers[0].item == lockedItem, "Clear lanes empties every lane but the locked one");
        proc->kitUndo();
        CHECK (proc->kit.lanes == before, "and Undo brings them all back");
        proc->kit.lanes[0].locked = false; proc->kitChanged (0); proc->kitMark();
        ed->showRoom (0); pump (*proc, 60);

        // the file list: several off at once, then Clear all
        proc->open (1, 0); waitIdle (*proc);
        proc->removeFiles ({ 0, 1 });
        waitIdle (*proc); pump (*proc, 60);
        CHECK (proc->session.files.size() == 1 && proc->session.doc() && proc->session.doc()->file == 0, "two files off the list at once; the one left is opened");
        proc->addPaths (threeFiles); waitIdle (*proc); pump (*proc, 60);
        CHECK (proc->session.files.size() == 3, "and back on the list (%d)", (int) proc->session.files.size());
        bool asked = false;
        ed->getFiles().ask = [&] (std::function<void (bool)> answer) { asked = true; answer (false); };
        ed->getFiles().confirmClearAll(); pump (*proc, 30);
        CHECK (asked && proc->session.files.size() == 3, "Clear all asks first, and No keeps the list");
        const auto poolBefore = proc->kit.pool.size();
        ed->getFiles().ask = [&] (std::function<void (bool)> answer) { answer (true); };
        proc->session.exportFiles (true);   // an export under way while the list is cleared
        ed->getFiles().confirmClearAll(); pump (*proc, 60);
        CHECK (proc->session.files.empty() && ! proc->session.doc() && ed->getView().doc == nullptr && proc->kit.pool.size() == poolBefore, "Yes: every file off the list, nothing open, the pool untouched");
        waitIdle (*proc, 600000);
        ed->getFiles().ask = nullptr;
        proc->addPaths (threeFiles); waitIdle (*proc); pump (*proc, 100);
        CHECK (proc->session.files.size() == 3 && proc->session.doc(), "the three recordings added again, and one open");
    }

    std::printf ("10c. LOOPS\n");
    {
        ed->showRoom (2); pump (*proc, 80);
        CHECK (ed->getLoopsRoom().isVisible() && ! ed->getKitRoom().isVisible() && ! ed->getSpec().isVisible(), "LOOPS shows its own room");
        auto& lp = proc->loops;
        ed->keyPressed (juce::KeyPress ('r', {}, 'r')); pump (*proc, 30);
        CHECK (! lp.layers[0].hits.empty() && proc->loopsMessage.contains ("chops"), "R rolls the layer: %d chops (\"%s\")", (int) lp.layers[0].hits.size(), proc->loopsMessage.toRawUTF8());
        const auto before = lp.layers[0].hits;
        ed->keyPressed (juce::KeyPress ('r', {}, 'r')); pump (*proc, 30);
        CHECK (! (lp.layers[0].hits == before), "again: another roll");
        ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0));
        CHECK (lp.layers[0].hits == before, "Ctrl+Z brings the last one back");
        const int l1 = proc->loopsAddLayer(), l2 = proc->loopsAddLayer(), l3 = proc->loopsAddLayer();
        lp.layers[(size_t) l2].source.kind = bc::LoopSource::From; lp.layers[(size_t) l2].source.from = proc->loopsSources().front();
        proc->loopsRoll (l2, false);
        lp.layers[(size_t) l3].mode = bc::LoopMode::Melody; lp.layers[(size_t) l3].bars = 4; proc->loopsRoll (l3, false);
        bool fromIt = ! lp.layers[(size_t) l2].hits.empty(); for (const auto& h : lp.layers[(size_t) l2].hits) fromIt = fromIt && h.sound->source == lp.layers[(size_t) l2].source.from;
        CHECK (lp.numLayers == 4 && l1 == 1 && fromIt && lp.bars() == 4, "four layers; one takes only %s's sounds; the loop is 4 bars", proc->loopsSources().front().c_str());
        lp.layers[0].hits[1].locked = true; const auto lockedHit = lp.layers[0].hits[1];
        ed->keyPressed (juce::KeyPress ('r', juce::ModifierKeys::shiftModifier, 'R')); pump (*proc, 30);
        bool stays = false; for (const auto& h : lp.layers[0].hits) stays = stays || (h.locked && h.step == lockedHit.step && h.sound == lockedHit.sound);
        CHECK (stays, "Shift+R rolls them all; the locked chop stays");
        // the grid's own mouse handlers: a chop clicked is chosen and heard, dragged it moves
        auto& g = ed->getLoopsRoom().grid();
        const auto& L0 = lp.layers[0];
        // (rolls are fresh each time, so the chop to click is one no other chop overlaps where it starts)
        auto firstAt = [&] (double b)
        {
            for (int h = 0; h < (int) L0.hits.size(); ++h)
            {
                const auto& hh = L0.hits[(size_t) h];
                if (b >= gen::swung (hh.step, L0.perBar, lp.swing) - 0.02 && b < gen::swung (hh.step + std::max (1, hh.len), L0.perBar, lp.swing)) return h;
            }
            return -1;
        };
        const float span = (float) (g.getWidth() - 132 - 6);
        int target = 0;
        for (int k = 1; k < (int) L0.hits.size(); ++k)
        {
            const double bk = std::fmod (gen::swung (L0.hits[(size_t) k].step, L0.perBar, lp.swing) + 2.0 / span * lp.beats(), 4.0 * L0.bars);
            if (firstAt (bk) == k) { target = k; break; }
        }
        const float x = (float) (132 + span * gen::swung (L0.hits[(size_t) target].step, L0.perBar, lp.swing) / lp.beats()) + 2.0f;
        click (g, { x, 18.0f + 26.0f });
        CHECK (ed->getView().loopLayer == 0 && ed->getView().loopHit == target, "a click on a chop chooses it (%d)", ed->getView().loopHit);
        // MIDI: a key plays the loop (Trigger) without PLAY
        proc->loopPlayer.playing.store (false); pump (*proc, 600);
        {
            juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; float top = 0.0f;
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 10);
            for (int b = 0; b < 200; ++b) { buf.clear(); proc->processBlock (buf, midi); midi.clear(); top = std::max (top, buf.getMagnitude (0, 512)); }
            midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0); buf.clear(); proc->processBlock (buf, midi);
            CHECK (top > 0.01f && top <= 0.9f, "a MIDI key plays the loop, transposed (peak %.2f)", top);
        }
        proc->loopPlayer.playing.store (true); pump (*proc, 60000, 0.0f, 200);
        const float playing = lastOut; const double beat = proc->loopPlayer.beatNow.load();
        CHECK (playing > 0.01f && beat >= 0.0, "PLAY plays it (peak %.2f, beat %.2f)", playing, beat);
        proc->loopPlayer.playing.store (false); pump (*proc, 60000, 0.0f, 400);
        // export: the mix and the stems, the same length, the stems adding up to the mix
        const auto ldir = dir.getChildFile ("loops");
        juce::String err;
        CHECK (proc->loopsExportAudio (ldir, true, err), "WAV and stems written %s", err.toRawUTF8());
        auto wavs = ldir.findChildFiles (juce::File::findFiles, false, "*.wav"); wavs.sort();
        // the loop first, then its stems
        for (int i = 0; i < wavs.size(); ++i) if (! wavs[i].getFileName().contains (" - L")) { wavs.move (i, 0); break; }
        std::vector<std::vector<float>> got; int len = -1; bool same = true;
        for (const auto& f : wavs)
        {
            std::unique_ptr<juce::AudioFormatReader> r (proc->formats.createReaderFor (f));
            if (r == nullptr) { same = false; continue; }
            juce::AudioBuffer<float> b (2, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            same = same && (len < 0 || len == b.getNumSamples()); len = b.getNumSamples();
            got.emplace_back (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples());
        }
        const int want = (int) std::lround (lp.beats() * 60.0 / lp.bpm * 48000.0);
        double sumErr = 0.0;
        if (got.size() == 5) for (size_t i = 0; i < got[0].size(); ++i) { double s2 = 0.0; for (size_t k = 1; k < 5; ++k) s2 += got[k][i]; sumErr = std::max (sumErr, std::fabs (s2 - got[0][i])); }
        CHECK (wavs.size() == 5 && same && std::abs (len - want) <= 1, "5 files (the loop and 4 stems), all %d samples (%.2f s)", len, len / 48000.0);
        CHECK (got.size() == 5 && sumErr < 2.0e-4, "the stems add up to the loop (%.5f)", sumErr);
        const auto mid = dir.getChildFile ("loop.mid");
        CHECK (proc->loopsExportMidi (mid, err), "MIDI written");
        {
            juce::MidiFile mf; juce::FileInputStream in (mid); const bool ok = in.openedOk() && mf.readFrom (in);
            int notes = 0, wantNotes = 0; for (int t = 0; ok && t < mf.getNumTracks(); ++t) for (const auto* e : *mf.getTrack (t)) notes += e->message.isNoteOn() ? 1 : 0;
            for (int i = 0; i < lp.numLayers; ++i) for (const auto& h : lp.layers[(size_t) i].hits) wantNotes += std::max (1, h.ratchet);
            CHECK (ok && notes == wantNotes && mf.getNumTracks() == 5, "read back: %d notes on %d tracks", notes, mf.getNumTracks());
        }
        const auto nj = dir.getChildFile ("Yard loop.njq");
        CHECK (proc->loopsSaveNjq (nj, err), ".NJQ saved %s", err.toRawUTF8());
        {
            auto back = std::make_unique<njq::Kit>(); const bool ok = njq::load (nj, *back, err);
            int lanes = 0, hits = 0; for (const auto& ln : back->lanes) lanes += ln.set && ln.hasSample && ln.l.size() > 16 ? 1 : 0;
            for (int l = 0; l < 16; ++l) for (int s2 = 0; s2 < back->scenes[0].length; ++s2) hits += back->scenes[0].steps[(size_t) l][(size_t) s2].vel > 0;
            CHECK (ok && back->kind == "loop" && lanes >= 4 && lanes <= 16 && hits > 8 && back->scenes[0].length == 64 && back->extra ("LOOP") != nullptr,
                   "read back as NeoBeat reads it: %d lanes, %d hits in 4 bars, kind \"%s\", with the layers", lanes, hits, back->kind.toRawUTF8());
        }
        const auto poolBefore = proc->kit.pool.size();
        CHECK (proc->loopsBounce (err) && proc->kit.pool.size() == poolBefore + 1 && proc->kit.pool.back()->source == "LOOPS", "bounced into the pool");
        // the loop in the project
        juce::MemoryBlock state; proc->getStateInformation (state);
        auto other = std::make_unique<BcProcessor>();
        other->setPlayConfigDetails (2, 2, 48000.0, 512); other->prepareToPlay (48000.0, 512);
        other->setStateInformation (state.getData(), (int) state.getSize());
        waitIdle (*other); pump (*other, 50);
        bool kept = other->loops.numLayers == lp.numLayers && other->loops.name == lp.name;
        for (int i = 0; kept && i < lp.numLayers; ++i)
        {
            const auto &a = lp.layers[(size_t) i], &b = other->loops.layers[(size_t) i];
            kept = a.hits.size() == b.hits.size() && a.mode == b.mode && a.seed == b.seed && a.source == b.source;
            for (size_t h = 0; kept && h < a.hits.size(); ++h) kept = a.hits[h].step == b.hits[h].step && a.hits[h].sound->name == b.hits[h].sound->name && a.hits[h].locked == b.hits[h].locked;
        }
        std::vector<float> m1, r1, m2, r2; bc::renderLoop (lp, 48000.0, m1, r1); bc::renderLoop (other->loops, 48000.0, m2, r2);
        double worst = m1.size() == m2.size() ? 0.0 : 1.0; for (size_t i = 0; worst < 1.0 && i < m1.size(); ++i) worst = std::max (worst, (double) std::fabs (m1[i] - m2[i]));
        CHECK (kept && worst < 2.0e-3, "the project keeps the loop: layers, chops, seeds, sounds (%d kB, worst %.5f)", (int) (state.getSize() / 1024), worst);
        other->releaseResources(); other.reset();
        ed->showRoom (0); pump (*proc, 60);
        CHECK (! proc->loopPlayer.playing.load() && ! ed->getLoopsRoom().isVisible(), "CUT: LOOPS stops and hides");
    }

    std::printf ("10d. KEYS\n");
    {
        const auto hornDir = testaudio::horns (dir);
        const int got = proc->kitAddFolder (hornDir);
        ed->showRoom (3); pump (*proc, 80);
        CHECK (got == 8 && ed->getKeysRoom().isVisible() && proc->midiTarget.load() == 3, "KEYS shows its room and takes the keys; 8 horns in the pool");
        proc->keysSource.kind = bc::LoopSource::From; proc->keysSource.from = "horns";
        proc->keys.mode = bc::KeysMode::Tuned; proc->keys.sustain = bc::KeysSustain::Loop; proc->keys.releaseMs = 150.0f;
        proc->keysBuild();
        int rr = 0, layers = 0; for (const auto& z : proc->keys.zones) { rr = std::max (rr, z.rrCount); layers += z.lowVel > 1 ? 1 : 0; }
        CHECK (proc->keys.zones.size() == 8 && rr == 2 && layers >= 1, "built: 8 zones, a soft layer and round robins at C3 (\"%s\")", proc->keysMessage.toRawUTF8());
        // played from MIDI, in tune
        auto playKey = [&] (int key, int blocks, std::vector<float>* keep)
        {
            juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi; float top = 0.0f;
            midi.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 110), 0);
            for (int b = 0; b < blocks; ++b) { buf.clear(); proc->processBlock (buf, midi); midi.clear(); top = std::max (top, buf.getMagnitude (0, 512)); if (keep) keep->insert (keep->end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
            midi.addEvent (juce::MidiMessage::noteOff (1, key), 0); buf.clear(); proc->processBlock (buf, midi);
            for (int b = 0; b < 40; ++b) { buf.clear(); juce::MidiBuffer none; proc->processBlock (buf, none); }
            return top;
        };
        std::vector<float> a4; const float top = playKey (69, 80, &a4);
        float strength = 0.0f; const float rough = nbk::detail::pitchOf (a4, 9600, 38400, 48000.0f, 50.0f, 2000.0f, strength);
        const double hz = bc::keysdetail::preciseHz (a4.data() + 9600, 28800, 48000.0, rough);
        CHECK (top > 0.02f && std::fabs (1200.0 * std::log2 (hz / 440.0)) < 3.0, "a MIDI A3 plays 440 Hz (%.2f Hz, peak %.2f)", hz, top);
        proc->keysChord.store (2);   // minor
        std::vector<float> chord; playKey (60, 40, &chord);
        CHECK (proc->keysPlayer.active.load() == 0, "a minor chord on C3 plays, and lets go");
        proc->keysChord.store (0);
        // the computer keyboard plays the on-screen keys; the map's mouse handlers choose a sound
        auto& map = ed->getKeysRoom().map();
        click (map, { map.getWidth() * 0.45f, map.getHeight() * 0.5f });
        CHECK (ed->getView().keysZone >= 0, "a click on the map chooses a sound (%d)", ed->getView().keysZone);
        pump (*proc, 600);
        // the .NJQ instrument, and opening it again
        const auto insFile = dir.getChildFile ("Horns.njq");
        proc->keys.name = "Horns";
        juce::String err;
        const bool saved = proc->keysSaveNjq (insFile, err);
        CHECK (saved, "saved as a .NJQ instrument (%d kB) %s", (int) (insFile.getSize() / 1024), err.toRawUTF8());
        {
            njq::InstrumentFile back; const bool ok = njq::loadInstrument (insFile, back, err);
            bool same = ok && back.zones.size() == proc->keys.zones.size() && back.mode == 0 && back.sustain == 1 && back.name == "Horns";
            for (size_t i = 0; same && i < back.zones.size(); ++i)
            {
                const auto& x = back.zones[i]; const auto& y = proc->keys.zones[i];
                same = std::fabs (x.root - y.root) < 1e-4f && x.lowKey == y.lowKey && x.highKey == y.highKey && x.lowVel == y.lowVel && x.rr == y.rr && x.loopStart == y.loopStart && x.l.size() == y.sound->l.size();
            }
            CHECK (same, "read back: every zone, its pitch, keys, velocities, round robin, loop and sound %s", err.toRawUTF8());
            njq::Kit asKit; njq::load (insFile, asKit, err);
            int lanes = 0; for (const auto& ln : asKit.lanes) lanes += ln.set ? 1 : 0;
            CHECK (asKit.kind == "instrument" && lanes == 0, "to NeoBeat's reader it is no kit (kind \"%s\", no lanes)", asKit.kind.toRawUTF8());
            // A .NJQ that holds sounds now opens as an instrument with those sounds laid out on the
            // keys, one to a key from C1 up, rather than being turned away for being the wrong kind
            // of file. A sequence's chops are still sounds, and a set of sounds on the keys is an
            // instrument. What is still refused is a file with nothing in it to play.
            njq::InstrumentFile asRack; juce::String e2;
            const bool opened = njq::loadInstrument (dir.getChildFile ("Yard loop.njq"), asRack, e2);
            int lowest = 128, highest = -1;
            for (const auto& z : asRack.zones) { lowest = std::min (lowest, z.lowKey); highest = std::max (highest, z.highKey); }
            CHECK (opened && ! asRack.zones.empty(),
                   "a sequence opens as a rack: %d sounds over keys %d..%d%s",
                   (int) asRack.zones.size(), lowest, highest, e2.isEmpty() ? "" : (" " + e2).toRawUTF8());

            njq::Kit empty; empty.kind = "kit"; empty.name = "nothing";
            const auto emptyFile = dir.getChildFile ("Nothing.njq");
            juce::String e3;
            njq::save (emptyFile, empty, e3);
            njq::InstrumentFile notOne; juce::String e4;
            const bool refused = ! njq::loadInstrument (emptyFile, notOne, e4);
            CHECK (refused && e4.contains ("no sounds"), "one with nothing in it is still refused: \"%s\"", e4.toRawUTF8());
        }
        const auto before = proc->keys;
        proc->keys.zones.clear(); proc->keysChanged();
        CHECK (proc->keysOpenNjq (insFile, err) && proc->keys.zones.size() == before.zones.size(), "opened again in KEYS");
        // KEYS in the project
        juce::MemoryBlock state; proc->getStateInformation (state);
        auto other = std::make_unique<BcProcessor>();
        other->setPlayConfigDetails (2, 2, 48000.0, 512); other->prepareToPlay (48000.0, 512);
        other->setStateInformation (state.getData(), (int) state.getSize());
        waitIdle (*other); pump (*other, 50);
        bool kept = other->keys.zones.size() == proc->keys.zones.size() && other->keys.sustain == proc->keys.sustain && other->keysSource == proc->keysSource && other->midiTarget.load() == 3;
        for (size_t i = 0; kept && i < proc->keys.zones.size(); ++i) kept = std::fabs (other->keys.zones[i].root - proc->keys.zones[i].root) < 1e-4f && other->keys.zones[i].sound->name == proc->keys.zones[i].sound->name;
        CHECK (kept, "the project keeps the instrument (%d kB in all)", (int) (state.getSize() / 1024));
        other->releaseResources(); other.reset();
        // the other ways to play
        proc->keys.mode = bc::KeysMode::Resonator; proc->keysSource = bc::LoopSource(); proc->keysBuild();
        std::vector<float> res; const float rtop = playKey (57, 60, &res);
        float rs = 0.0f; const float rhz = res.size() > 20000 ? nbk::detail::pitchOf (res, 4800, 19200, 48000.0f, 50.0f, 2000.0f, rs) : 0.0f;
        CHECK (proc->keys.zones.size() >= 2 && proc->keys.zones.size() <= 16 && rtop > 0.01f && std::fabs (1200.0 * std::log2 (std::max (1.0f, rhz) / 220.0)) < 30.0,
               "Resonator: the pool struck into a resonator on A2 (%d exciters, %.1f Hz, peak %.2f)", (int) proc->keys.zones.size(), rhz, rtop);
        proc->keys.mode = bc::KeysMode::Chops; proc->keysBuild();
        CHECK (! proc->keys.zones.empty() && proc->keys.zones[0].lowKey == 48, "Chops: a sound a key from C2");
        // a key held (looping) while leaving the room is let go
        proc->keys.mode = bc::KeysMode::Tuned; proc->keys.sustain = bc::KeysSustain::Loop; proc->keysSource.kind = bc::LoopSource::From; proc->keysSource.from = "horns"; proc->keysBuild();
        {
            juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100), 0);
            proc->processBlock (buf, mb);
            for (int b = 0; b < 200; ++b) { buf.clear(); juce::MidiBuffer none; proc->processBlock (buf, none); }
            const int during = proc->keysPlayer.active.load();
            ed->showRoom (2); pump (*proc, 30);
            for (int b = 0; b < 200; ++b) { buf.clear(); juce::MidiBuffer none; proc->processBlock (buf, none); }
            CHECK (during == 1 && proc->keysPlayer.active.load() == 0 && proc->midiTarget.load() == 2, "a key held in KEYS is let go on leaving; back in LOOPS, the keys go to LOOPS");
        }
        ed->showRoom (0); pump (*proc, 30);
    }

    std::printf ("11. a long random session (seed %d)\n", seed);
    {
        juce::Random r (seed);
        int actions = 0;
        for (int it = 0; it < 700; ++it)
        {
            auto& w = ed->getView();   // the editor may have been closed and opened again
            const int a = r.nextInt (39);
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
                case 29: ed->showRoom (r.nextInt (2)); break;
                case 30: if (r.nextInt (8) == 0) proc->kitGather (r.nextBool()); else if (r.nextInt (30) == 0) proc->kitClearPool(); else if (r.nextInt (10) == 0) proc->kitClearLanes(); break;
                case 31:
                    switch (r.nextInt (4))
                    {
                        case 0: proc->kitChoose(); break;
                        case 1: proc->kitShuffle(); break;
                        case 2: proc->kitAnyLane = r.nextInt (3) == 0; proc->kitRandom (r.nextInt (16)); break;
                        default: if (r.nextInt (20) == 0) { proc->clearFiles(); pump (*proc, r.nextInt (30)); proc->addPaths (threeFiles); }
                                 else if (r.nextInt (12) == 0 && proc->session.files.size() > 1) { proc->removeFiles ({ r.nextInt ((int) proc->session.files.size()) }); proc->addPaths (threeFiles); }
                                 break;
                    }
                    break;
                case 32: proc->kitAssign (r.nextInt (16), r.nextInt (3), r.nextInt ((int) proc->kit.pool.size() + 2) - 1, r.nextBool()); break;
                case 33:
                {
                    auto& ly = proc->kit.lanes[(size_t) r.nextInt (16)].layers[(size_t) r.nextInt (3)];
                    ly.levelDb = r.nextFloat() * 60.0f - 36.0f; ly.offsetMs = r.nextFloat() * 250.0f - 50.0f; ly.semis = r.nextFloat() * 48.0f - 24.0f;
                    ly.start = r.nextFloat(); ly.end = r.nextFloat(); ly.fadeMs = r.nextFloat() * 300.0f; ly.reverse = r.nextBool();
                    proc->kitChanged (r.nextInt (17) - 1); proc->kitMark();
                    if (r.nextInt (3) == 0) proc->kitSetPattern (r.nextInt (128), r.nextBool());
                    break;
                }
                case 34:
                    switch (r.nextInt (6))
                    {
                        case 0: proc->kitUndo(); break;
                        case 1: proc->kitRedo(); break;
                        case 2: proc->kitPlayer.playing.store (r.nextBool()); break;
                        case 3: proc->kitNext (r.nextInt (16), r.nextBool() ? 1 : -1); break;
                        case 4: proc->kitRate (r.nextInt ((int) proc->kit.pool.size() + 1), (float) r.nextInt (6)); break;
                        default: { static const int keys[] = { juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::spaceKey, juce::KeyPress::returnKey };
                                   ed->keyPressed (juce::KeyPress (keys[r.nextInt (6)])); break; }
                    }
                    break;
                case 36:
                    switch (r.nextInt (8))
                    {
                        case 0: proc->loopsRoll (r.nextInt (proc->loops.numLayers + 1) - 1, r.nextBool()); break;
                        case 1: if (r.nextInt (3) == 0) proc->loopsAddLayer(); else if (proc->loops.numLayers > 1 && r.nextInt (4) == 0) proc->loopsRemoveLayer (r.nextInt (proc->loops.numLayers)); break;
                        case 2: { auto& L = proc->loops.layers[(size_t) r.nextInt (proc->loops.numLayers)]; L.mode = (bc::LoopMode) r.nextInt (4); L.density = r.nextFloat(); L.dejavu = r.nextFloat();
                                  static const int g[4] = { 8, 12, 16, 24 }; L.perBar = g[r.nextInt (4)]; L.bars = 1 << r.nextInt (4); L.random = r.nextBool(); L.rhythm = r.nextInt (3) == 0 ? r.nextInt (128) : -1;
                                  proc->loopsRoll (r.nextInt (proc->loops.numLayers), false); break; }
                        case 3: proc->loopPlayer.playing.store (r.nextBool()); break;
                        case 4: if (r.nextBool()) proc->loopsUndo(); else proc->loopsRedo(); break;
                        case 5: { const int li = r.nextInt (proc->loops.numLayers); auto& H = proc->loops.layers[(size_t) li].hits; if (! H.empty()) { const int h = r.nextInt ((int) H.size()); if (r.nextBool()) proc->loopsRerollHit (li, h); else { H[(size_t) h].semis = r.nextFloat() * 24.0f - 12.0f; H[(size_t) h].reverse = r.nextBool(); proc->loopsChanged(); } } break; }
                        case 6: { const auto& lg = ed->getLoopsRoom().grid(); auto& gg = const_cast<juce::Component&> (lg); if (gg.isShowing()) drag (gg, { rp (gg), rp (gg) }); break; }
                        default: { juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 48 + r.nextInt (40), (juce::uint8) 90), r.nextInt (512));
                                   if (r.nextBool()) mb.addEvent (juce::MidiMessage::noteOff (1, 48 + r.nextInt (40)), 511); proc->processBlock (buf, mb); break; }
                    }
                    break;
                case 37: ed->showRoom (r.nextInt (4)); break;
                case 38:
                    switch (r.nextInt (6))
                    {
                        case 0: proc->keys.mode = (bc::KeysMode) r.nextInt (3); proc->keysSource.kind = (bc::LoopSource::Kind) r.nextInt (2); proc->keysSource.from = "horns"; proc->keysBuild(); break;
                        case 1: proc->keys.sustain = (bc::KeysSustain) r.nextInt (3); proc->keys.mono = r.nextBool(); proc->keys.glideMs = r.nextFloat() * 200.0f; proc->keys.attackMs = 1.0f + r.nextFloat() * 300.0f; proc->keys.releaseMs = 10.0f + r.nextFloat() * 2000.0f; proc->keysChanged(); break;
                        case 2: if (r.nextBool()) proc->keysUndo(); else proc->keysRedo(); break;
                        case 3: proc->keysChord.store (r.nextInt (BcProcessor::numChords())); break;
                        case 4: { const auto& km = ed->getKeysRoom().map(); auto& mm = const_cast<juce::Component&> (km); if (mm.isShowing()) click (mm, rp (mm)); break; }
                        default: { proc->midiTarget.store (r.nextBool() ? 3 : 2); juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
                                   for (int k = 0; k < 1 + r.nextInt (4); ++k) mb.addEvent (juce::MidiMessage::noteOn (1, 30 + r.nextInt (70), (juce::uint8) (1 + r.nextInt (127))), r.nextInt (512));
                                   if (r.nextBool()) mb.addEvent (juce::MidiMessage::allNotesOff (1), 511); proc->processBlock (buf, mb); break; }
                    }
                    break;
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
            for (const auto& ln : proc->kit.lanes) for (const auto& ly : ln.layers) if (ly.item >= (int) proc->kit.pool.size()) { std::printf ("  FAIL: a lane points outside the pool at step %d\n", it); ++failures; }
            for (int li = 0; li < proc->loops.numLayers; ++li) for (const auto& h : proc->loops.layers[(size_t) li].hits) if (! h.sound || h.step < 0 || h.step >= proc->loops.layers[(size_t) li].steps()) { std::printf ("  FAIL: a chop out of its layer at step %d\n", it); ++failures; break; }
            if (! std::isfinite (lastOut) || lastOut > 8.0f) { std::printf ("  FAIL: the output broke at step %d (%f)\n", it, lastOut); ++failures; }
        }
        waitIdle (*proc, 120000);
        CHECK (true, "%d random actions, nothing broke", actions);
    }
    ed.reset();
    proc->releaseResources();
    proc.reset();
    dir.deleteRecursively(); sharedDir.deleteRecursively();
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
