// Batchcut 3.0 - LOOPS on the processor's side: the layers, rolling them, undo, the player, export (WAV,
// stems, MIDI, .NJQ for NeoBeat), bouncing to the pool, and the loop in the project.
#include "Processor.h"
#include <set>
#ifndef BATCHCUT_VERSION
 #define BATCHCUT_VERSION "3.0.0"
#endif

namespace
{
uint64_t freshSeed()
{
    auto& r = juce::Random::getSystemRandom();
    return ((uint64_t) (uint32_t) r.nextInt() << 32) ^ (uint64_t) (uint32_t) r.nextInt() ^ (uint64_t) juce::Time::getHighResolutionTicks();
}
juce::String layerLabel (const bc::LoopLayer& L, int i) { return "L" + juce::String (i + 1) + " " + bc::loopModeName (L.mode); }

// The loop's chops as NeoBeat lanes: each sound at each pitch, forwards or backwards, is one lane (16 at most;
// the rarest share the nearest lane of the same sound, or the busiest lane)
struct LaneCombo { const bc::PoolItem* sound = nullptr; float semis = 0.0f; bool reverse = false; int uses = 0; std::shared_ptr<const bc::PoolItem> keep; };
struct LaneMap
{
    std::vector<LaneCombo> lanes;
    std::vector<std::vector<int>> hitLane;      // [layer][hit] -> lane
};
LaneMap laneMap (const bc::LoopSet& set)
{
    std::vector<LaneCombo> all;
    auto find = [&] (const bc::LoopHit& h) { for (int i = 0; i < (int) all.size(); ++i) if (all[(size_t) i].sound == h.sound.get() && all[(size_t) i].semis == h.semis && all[(size_t) i].reverse == h.reverse) return i; return -1; };
    for (int l = 0; l < set.numLayers; ++l)
        for (const auto& h : set.layers[(size_t) l].hits)
        {
            if (! h.sound) continue;
            int i = find (h);
            if (i < 0) { all.push_back ({ h.sound.get(), h.semis, h.reverse, 0, h.sound }); i = (int) all.size() - 1; }
            all[(size_t) i].uses += std::max (1, h.ratchet);
        }
    std::vector<int> order ((size_t) all.size()); for (int i = 0; i < (int) order.size(); ++i) order[(size_t) i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (int a, int b) { return all[(size_t) a].uses > all[(size_t) b].uses; });
    LaneMap m;
    std::vector<int> laneOf (all.size(), -1);
    for (int k = 0; k < (int) order.size() && k < 16; ++k) { laneOf[(size_t) order[(size_t) k]] = k; m.lanes.push_back (all[(size_t) order[(size_t) k]]); }
    for (int k = 16; k < (int) order.size(); ++k)
    {
        const auto& c = all[(size_t) order[(size_t) k]];
        int best = 0; float d = 1e9f;
        for (int j = 0; j < (int) m.lanes.size(); ++j)
        {
            const float dj = m.lanes[(size_t) j].sound == c.sound ? std::fabs (m.lanes[(size_t) j].semis - c.semis) : 100.0f + (float) j;
            if (dj < d) { d = dj; best = j; }
        }
        laneOf[(size_t) order[(size_t) k]] = best;
    }
    for (int l = 0; l < set.numLayers; ++l)
    {
        m.hitLane.emplace_back();
        for (const auto& h : set.layers[(size_t) l].hits) m.hitLane.back().push_back (h.sound ? laneOf[(size_t) find (h)] : -1);
    }
    return m;
}

// a chop as one lane's sample: tuned and turned as it plays, at its own rate
void laneSample (const LaneCombo& c, std::vector<float>& l, std::vector<float>& r)
{
    const auto& s = *c.sound; const int len = (int) s.l.size();
    const double inc = std::exp2 ((double) c.semis / 12.0);
    const int n = std::max (2, (int) ((double) (len - 1) / inc));
    l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f);
    for (int j = 0; j < n; ++j)
    {
        const double pos = c.reverse ? (double) len - 2.0 - j * inc : j * inc;
        const int i0 = std::max (0, std::min (len - 2, (int) pos)); const float f = (float) (pos - i0);
        l[(size_t) j] = s.l[(size_t) i0] + f * (s.l[(size_t) i0 + 1] - s.l[(size_t) i0]);
        r[(size_t) j] = s.r.size() == s.l.size() ? s.r[(size_t) i0] + f * (s.r[(size_t) i0 + 1] - s.r[(size_t) i0]) : l[(size_t) j];
    }
}
} // namespace

// ---------------------------------------------------------------- layers and rolls
int BcProcessor::loopsAddLayer()
{
    if (loops.numLayers >= bc::kLoopLayers) { loopsMessage = "8 layers is the most a loop has"; return -1; }
    const int i = loops.numLayers++;
    auto& L = loops.layers[(size_t) i];
    L = bc::LoopLayer();
    // a new layer fills differently from the one before: Pool, Stutter, Slice, Melody in turn
    static const bc::LoopMode turn[4] = { bc::LoopMode::Pool, bc::LoopMode::Stutter, bc::LoopMode::Slice, bc::LoopMode::Melody };
    L.mode = turn[i % 4];
    if (i > 0) L.bars = loops.layers[0].bars;
    L.seed = freshSeed();
    if (! kit.pool.empty()) bc::rollLayer (L, kit.pool, &nbkstore::learner(), loops.root, loops.scale, loops.bpm);
    loopsMessage = layerLabel (L, i) + ": " + juce::String ((int) L.hits.size()) + " chops from " + juce::String (L.source.label());
    loopsChanged(); loopsMark();
    return i;
}

void BcProcessor::loopsRemoveLayer (int layer)
{
    if (layer < 0 || layer >= loops.numLayers) return;
    for (int i = layer; i + 1 < loops.numLayers; ++i) loops.layers[(size_t) i] = loops.layers[(size_t) i + 1];
    loops.layers[(size_t) loops.numLayers - 1] = bc::LoopLayer();
    loops.numLayers = std::max (1, loops.numLayers - 1);
    loopsMessage = "Layer " + juce::String (layer + 1) + " taken out (Undo brings it back)";
    loopsChanged(); loopsMark();
}

void BcProcessor::loopsRoll (int layer, bool newSeed)
{
    if (kit.pool.empty()) { loopsMessage = "The pool is empty: THIS FILE, ALL FILES or FOLDER brings sounds in"; loopsChanged(); return; }
    int rolled = 0, chops = 0; juce::String empty;
    for (int i = 0; i < loops.numLayers; ++i)
    {
        if (layer >= 0 && i != layer) continue;
        auto& L = loops.layers[(size_t) i];
        if (newSeed) L.seed = freshSeed();
        if (bc::rollLayer (L, kit.pool, &nbkstore::learner(), loops.root, loops.scale, loops.bpm)) { ++rolled; chops += (int) L.hits.size(); }
        else empty = juce::String (L.source.label());
    }
    if (layer >= 0)
    {
        const auto& L = loops.layers[(size_t) layer];
        loopsMessage = empty.isNotEmpty() ? "No sounds from " + empty + " in the pool"
                                          : layerLabel (L, layer) + ": " + juce::String ((int) L.hits.size()) + " chops from " + juce::String (L.source.label())
                                            + (L.random ? ", dealt at random" : "") + "  (seed " + juce::String::toHexString ((juce::int64) L.seed).getLastCharacters (6) + ")";
    }
    else loopsMessage = "Rolled " + juce::String (rolled) + (rolled == 1 ? " layer: " : " layers: ") + juce::String (chops) + " chops" + (empty.isNotEmpty() ? " (nothing from " + empty + ")" : juce::String());
    loopsChanged(); loopsMark();
}

bool BcProcessor::loopsRerollHit (int layer, int hit)
{
    if (layer < 0 || layer >= loops.numLayers) return false;
    const bool ok = bc::rerollHit (loops.layers[(size_t) layer], hit, kit.pool, &nbkstore::learner(), freshSeed());
    if (ok) { loopsChanged(); loopsMark(); loopsPlayHit (layer, hit); }
    return ok;
}

void BcProcessor::loopsChanged()
{
    for (int i = 0; i < loops.numLayers; ++i) bc::tidyLayer (loops.layers[(size_t) i]);
    auto snap = bc::makeSnapshot (loops);
    loopPlayer.setSnapshot (snap);
    loopKeep.push_back ({ juce::Time::getMillisecondCounter(), std::move (snap) });
    ++loopsVersion; stateDirty = true;
}

// Clear all: every layer empties and the room goes back to one. What the loop is in - its tempo, its swing,
// its key and its name - is not content, so it stays. One Undo brings the hits back.
void BcProcessor::loopsClearAll()
{
    for (auto& layer : loops.layers)
    {
        const auto wasMode = layer.mode;
        const auto wasSource = layer.source;
        const int wasBars = layer.bars, wasPerBar = layer.perBar;
        layer = bc::LoopLayer {};
        layer.mode = wasMode;
        layer.source = wasSource;
        layer.bars = wasBars;
        layer.perBar = wasPerBar;
    }
    loops.numLayers = 1;
    loopsChanged();
    loopsMark();
}

void BcProcessor::loopsMark()
{
    if (loops == loopsSaved) return;
    loopsUndoStack.push_back (loopsSaved); loopsRedoStack.clear(); loopsSaved = loops;
    if (loopsUndoStack.size() > 100) loopsUndoStack.erase (loopsUndoStack.begin());
}
void BcProcessor::loopsUndo()
{
    loopsMark();
    if (loopsUndoStack.empty()) return;
    loopsRedoStack.push_back (loops); loops = loopsSaved = loopsUndoStack.back(); loopsUndoStack.pop_back();
    loopsChanged();
}
void BcProcessor::loopsRedo()
{
    if (loopsRedoStack.empty()) return;
    loopsUndoStack.push_back (loops); loops = loopsSaved = loopsRedoStack.back(); loopsRedoStack.pop_back();
    loopsChanged();
}

void BcProcessor::loopsPlayHit (int layer, int hit)
{
    if (layer < 0 || layer >= loops.numLayers) return;
    const auto& H = loops.layers[(size_t) layer].hits;
    if (hit < 0 || hit >= (int) H.size() || ! H[(size_t) hit].sound) return;
    const auto& h = H[(size_t) hit];
    loopKeep.push_back ({ juce::Time::getMillisecondCounter(), bc::makeSnapshot (loops) });   // (keeps the sound alive while it plays)
    loopPlayer.audition (h.sound.get(), h.semis, bc::hitGain (h) * std::pow (10.0f, loops.layers[(size_t) layer].levelDb / 20.0f));
}

std::vector<std::string> BcProcessor::loopsSources() const
{
    std::set<std::string> s;
    for (const auto& it : kit.pool) if (it && ! it->source.empty()) s.insert (it->source);
    return { s.begin(), s.end() };
}

// ---------------------------------------------------------------- export
bool BcProcessor::loopsExportAudio (const juce::File& dir, bool stems, juce::String& error)
{
    bool any = false; for (int i = 0; i < loops.numLayers; ++i) any = any || ! loops.layers[(size_t) i].hits.empty();
    if (! any) { error = "The loop has no chops yet: ROLL a layer first."; return false; }
    if (! dir.createDirectory()) { error = "Could not make " + dir.getFullPathName(); return false; }
    const double sr = loopsRate();
    std::vector<float> l, r; bc::renderLoop (loops, sr, l, r);
    float pk = 0.0f; for (size_t i = 0; i < l.size(); ++i) pk = std::max (pk, std::max (std::fabs (l[i]), std::fabs (r[i])));
    // layers that add up past full scale: the whole loop (and its stems alike) comes down, nothing is limited
    const float k = pk > 0.98f ? 0.98f / pk : 1.0f;
    for (auto* ch : { &l, &r }) for (auto& x : *ch) x *= k;
    auto base = juce::File::createLegalFileName (juce::String (loops.name).trim()); if (base.isEmpty()) base = "Loop";
    base << " " << juce::String ((int) std::lround (loops.bpm)) << " BPM";
    std::string err;
    const auto mixFile = dir.getNonexistentChildFile (base, ".wav", false);
    if (! writeWav (mixFile, l, r, sr, 24, err)) { error = juce::String (err); return false; }
    int written = 0;
    if (stems)
        for (int i = 0; i < loops.numLayers; ++i)
        {
            const auto& L = loops.layers[(size_t) i];
            if (L.hits.empty()) continue;
            std::vector<float> a, b; bc::renderLoop (loops, sr, a, b, i);
            for (auto* ch : { &a, &b }) for (auto& x : *ch) x *= k;
            if (! writeWav (dir.getNonexistentChildFile (base + " - " + layerLabel (L, i), ".wav", false), a, b, sr, 24, err)) { error = juce::String (err); return false; }
            ++written;
        }
    loopsMessage = "Wrote " + mixFile.getFileName() + (written > 0 ? " and " + juce::String (written) + (written == 1 ? " stem" : " stems") : juce::String())
                 + (k < 1.0f ? " (all " + juce::String (20.0f * std::log10 (k), 1) + " dB, so nothing clips)" : juce::String()) + " to " + dir.getFileName();
    return true;
}

bool BcProcessor::loopsExportMidi (const juce::File& file, juce::String& error)
{
    const auto m = laneMap (loops);
    if (m.lanes.empty()) { error = "The loop has no chops yet."; return false; }
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    {
        juce::MidiMessageSequence meta;
        meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / std::max (30.0, loops.bpm))), 0.0);
        meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        meta.addEvent (juce::MidiMessage::textMetaEvent (3, juce::String (loops.name)), 0.0);
        mf.addTrack (meta);
    }
    // each chop on the note of its NeoBeat lane (C1 = lane 1, as NeoBeat's MIDI in): the file plays the .NJQ
    for (int i = 0; i < loops.numLayers; ++i)
    {
        const auto& L = loops.layers[(size_t) i];
        if (L.hits.empty()) continue;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, layerLabel (L, i)), 0.0);
        for (int h = 0; h < (int) L.hits.size(); ++h)
        {
            const auto& hit = L.hits[(size_t) h];
            const int lane = m.hitLane[(size_t) i][(size_t) h];
            if (lane < 0) continue;
            const double a = gen::swung (hit.step, L.perBar, loops.swing), next = gen::swung (hit.step + 1, L.perBar, loops.swing);
            const int r = std::max (1, std::min (hit.ratchet, 4));
            for (int k = 0; k < r; ++k)
            {
                const double on = (a + (next - a) * k / r) * 960.0;
                const double len = r > 1 ? (next - a) / r * 960.0 * 0.9 : std::max (60.0, (gen::swung (hit.step + hit.len, L.perBar, loops.swing) - a) * 960.0 * 0.95);
                const int vel = juce::jlimit (1, 127, (int) std::lround (hit.vel * 127.0f * (r > 1 ? 0.55f + 0.45f * (float) (k + 1) / (float) r : 1.0f)));
                seq.addEvent (juce::MidiMessage::noteOn (10, 36 + lane, (juce::uint8) vel), on);
                seq.addEvent (juce::MidiMessage::noteOff (10, 36 + lane), on + len);
            }
        }
        seq.updateMatchedPairs();
        mf.addTrack (seq);
    }
    const auto tmp = file.getSiblingFile (file.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream os (tmp);
        if (! os.openedOk() || ! mf.writeTo (os)) { tmp.deleteFile(); error = "Could not write " + file.getFullPathName(); return false; }
    }
    if (! tmp.moveFileTo (file)) { tmp.deleteFile(); error = "Could not write " + file.getFullPathName(); return false; }
    loopsMessage = "Wrote " + file.getFileName() + ": " + juce::String ((int) m.lanes.size()) + " chops on NeoBeat's lane notes (C1 up)";
    return true;
}

bool BcProcessor::loopsSaveNjq (const juce::File& file, juce::String& error)
{
    const auto m = laneMap (loops);
    if (m.lanes.empty()) { error = "The loop has no chops yet: ROLL a layer first."; return false; }
    auto k = std::make_unique<njq::Kit>();
    k->name = juce::String (loops.name); k->madeWith = "Batchcut " BATCHCUT_VERSION; k->kind = "loop";
    k->bpm = loops.bpm; k->swing = loops.swing; k->patternIndex = -1;
    for (int i = 0; i < (int) m.lanes.size(); ++i)
    {
        const auto& c = m.lanes[(size_t) i];
        auto& ln = k->lanes[(size_t) i];
        ln.set = true; ln.hasSample = true; ln.role = i; ln.sampleRate = c.sound->sr;
        ln.name = juce::String (c.sound->name) + (c.semis != 0.0f ? juce::String (" ") + (c.semis > 0 ? "+" : "") + juce::String (c.semis, 1) : juce::String()) + (c.reverse ? " rev" : "");
        laneSample (c, ln.l, ln.r);
        ln.levelDb = juce::jlimit (-30.0f, 24.0f, nbk::matchGainDb ({ c.sound->lufs, c.sound->peakDb }));   // as loud as it is in the loop
        ln.decay = 1.0f; ln.evolve = 0.5f; ln.width = 1.0f; ln.choke = 0;
    }
    // the loop as scene A: NeoBeat's own 16ths, its swing applied by NeoBeat (so the steps are written straight)
    auto& P = k->scenes[0];
    const int bars = std::min (loops.bars(), nb::kMaxBars);
    P.length = bars * 16;
    const float top = [&] { float t = 0.0f; for (int i = 0; i < loops.numLayers; ++i) t = std::max (t, loops.layers[(size_t) i].levelDb); return t; }();
    for (int i = 0; i < loops.numLayers; ++i)
    {
        const auto& L = loops.layers[(size_t) i];
        if (! loops.audible (i)) continue;
        const float layerGain = std::pow (10.0f, (L.levelDb - top) / 20.0f);
        for (int rep = 0; rep * L.bars < bars; ++rep)
            for (int h = 0; h < (int) L.hits.size(); ++h)
            {
                const auto& hit = L.hits[(size_t) h];
                const int lane = m.hitLane[(size_t) i][(size_t) h];
                if (lane < 0) continue;
                const double s16 = (double) hit.step * 16.0 / (double) L.perBar + (double) rep * L.bars * 16.0;
                const int s = (int) std::lround (s16);
                if (s < 0 || s >= P.length) continue;
                auto& st = P.steps[(size_t) lane][(size_t) s];
                st.vel = (uint8_t) juce::jlimit (1, 127, std::max ((int) st.vel, (int) std::lround (hit.vel * layerGain * 127.0f)));
                st.nudge = (int8_t) juce::jlimit (-50, 50, (int) std::lround ((s16 - s) * 100.0));
                st.roll = (uint8_t) (hit.ratchet >= 2 && hit.ratchet <= 4 ? hit.ratchet : 0);
            }
    }
    for (int sc = 1; sc < nb::kNumScenes; ++sc) { k->scenes[(size_t) sc] = nb::Pattern(); k->scenes[(size_t) sc].length = P.length; }
    // the layers themselves (version 2): for whatever plays loops as loops (JaqRaq's LOOPS module)
    auto loop = std::make_shared<juce::XmlElement> ("LOOP");
    loop->setAttribute ("bars", loops.bars()); loop->setAttribute ("root", loops.root); loop->setAttribute ("scale", loops.scale);
    for (int i = 0; i < loops.numLayers; ++i)
    {
        const auto& L = loops.layers[(size_t) i];
        auto* le = loop->createNewChildElement ("LAYER");
        le->setAttribute ("index", i); le->setAttribute ("mode", (int) L.mode); le->setAttribute ("bars", L.bars); le->setAttribute ("perBar", L.perBar);
        le->setAttribute ("level", (double) L.levelDb); le->setAttribute ("pan", (double) L.pan); le->setAttribute ("mute", L.mute ? 1 : 0); le->setAttribute ("solo", L.solo ? 1 : 0);
        for (int h = 0; h < (int) L.hits.size(); ++h)
        {
            const auto& hit = L.hits[(size_t) h];
            auto* he = le->createNewChildElement ("HIT");
            he->setAttribute ("step", hit.step); he->setAttribute ("len", hit.len); he->setAttribute ("ratchet", hit.ratchet);
            he->setAttribute ("lane", m.hitLane[(size_t) i][(size_t) h]); he->setAttribute ("level", (double) hit.levelDb); he->setAttribute ("vel", (double) hit.vel);
        }
    }
    k->extras.push_back (loop);
    if (! njq::save (file, *k, error)) return false;
    loopsMessage = "Saved " + file.getFileName() + ": " + juce::String ((int) m.lanes.size()) + " chops as lanes, " + juce::String (bars) + (bars == 1 ? " bar" : " bars") + " as scene A. NeoBeat: KIT / FILE > My sequences.";
    return true;
}

bool BcProcessor::loopsBounce (juce::String& error)
{
    bool any = false; for (int i = 0; i < loops.numLayers; ++i) any = any || ! loops.layers[(size_t) i].hits.empty();
    if (! any) { error = "The loop has no chops yet."; return false; }
    if (kit.pool.size() >= 2000) { error = "The pool is full (2,000 sounds): CLEAR ALL some first."; return false; }
    auto it = std::make_shared<bc::PoolItem>();
    it->sr = 48000.0;
    bc::renderLoop (loops, it->sr, it->l, it->r);
    float pk = 0.0f; for (size_t i = 0; i < it->l.size(); ++i) pk = std::max (pk, std::max (std::fabs (it->l[i]), std::fabs (it->r[i])));
    if (pk > 0.98f) { const float k = 0.98f / pk; for (auto* ch : { &it->l, &it->r }) for (auto& x : *ch) x *= k; }
    int n = 1; for (const auto& p : kit.pool) n += p->source == "LOOPS" ? 1 : 0;
    it->name = loops.name + " " + std::to_string (n); it->source = "LOOPS";
    bc::analyseItem (*it, nbkstore::learner());
    it->f.valid = true;                       // (a loop is not a one-shot, but it is a sound to use)
    kit.pool.push_back (it);
    ++kitVersion; stateDirty = true;
    loopsMessage = "Bounced into the pool as \"" + juce::String (it->name) + "\": use it in a layer (source LOOPS) or on a kit lane";
    return true;
}

// ---------------------------------------------------------------- the loop in the project
std::unique_ptr<juce::XmlElement> BcProcessor::loopsToXml()
{
    auto root = std::make_unique<juce::XmlElement> ("LOOPS");
    root->setAttribute ("name", juce::String (loops.name)); root->setAttribute ("bpm", loops.bpm); root->setAttribute ("swing", (double) loops.swing);
    root->setAttribute ("root", loops.root); root->setAttribute ("scale", loops.scale); root->setAttribute ("layers", loops.numLayers);
    std::vector<const bc::PoolItem*> used; std::vector<std::shared_ptr<const bc::PoolItem>> keep;
    auto ref = [&] (const std::shared_ptr<const bc::PoolItem>& s) { for (int i = 0; i < (int) used.size(); ++i) if (used[(size_t) i] == s.get()) return i; used.push_back (s.get()); keep.push_back (s); return (int) used.size() - 1; };
    for (int i = 0; i < loops.numLayers; ++i)
    {
        const auto& L = loops.layers[(size_t) i];
        auto* le = root->createNewChildElement ("LAYER");
        le->setAttribute ("index", i); le->setAttribute ("mode", (int) L.mode); le->setAttribute ("bars", L.bars); le->setAttribute ("perBar", L.perBar);
        le->setAttribute ("density", (double) L.density); le->setAttribute ("dejavu", (double) L.dejavu); le->setAttribute ("level", (double) L.levelDb); le->setAttribute ("pan", (double) L.pan);
        le->setAttribute ("mute", L.mute ? 1 : 0); le->setAttribute ("solo", L.solo ? 1 : 0); le->setAttribute ("random", L.random ? 1 : 0); le->setAttribute ("midi", L.midiMode);
        le->setAttribute ("rhythm", L.rhythm); le->setAttribute ("rhythmLane", L.rhythmLane); le->setAttribute ("seed", juce::String::toHexString ((juce::int64) L.seed));
        le->setAttribute ("sourceKind", (int) L.source.kind); le->setAttribute ("sourceFrom", juce::String (L.source.from)); le->setAttribute ("sourceFamily", L.source.family);
        for (const auto& h : L.hits)
        {
            if (! h.sound) continue;
            auto* he = le->createNewChildElement ("HIT");
            he->setAttribute ("step", h.step); he->setAttribute ("len", h.len); he->setAttribute ("ratchet", h.ratchet); he->setAttribute ("sound", ref (h.sound));
            he->setAttribute ("semis", (double) h.semis); he->setAttribute ("level", (double) h.levelDb); he->setAttribute ("vel", (double) h.vel);
            he->setAttribute ("reverse", h.reverse ? 1 : 0); he->setAttribute ("locked", h.locked ? 1 : 0);
        }
    }
    for (const auto* s : used) root->addChildElement (bcxml::sound (*s).release());
    return root;
}

void BcProcessor::loopsFromXml (const juce::XmlElement& root)
{
    loops = bc::LoopSet();
    loops.name = root.getStringAttribute ("name", "My loop").toStdString();
    loops.bpm = juce::jlimit (30.0, 300.0, root.getDoubleAttribute ("bpm", 100.0)); loops.swing = (float) juce::jlimit (0.5, 0.75, root.getDoubleAttribute ("swing", 0.62));
    loops.root = juce::jlimit (0, 11, root.getIntAttribute ("root")); loops.scale = juce::jlimit (0, gen::numScales() - 1, root.getIntAttribute ("scale", 3));
    loops.numLayers = juce::jlimit (1, bc::kLoopLayers, root.getIntAttribute ("layers", 1));
    std::vector<std::shared_ptr<const bc::PoolItem>> sounds;
    for (auto* se : root.getChildWithTagNameIterator ("SOUND")) sounds.push_back (bcxml::sound (*se, 30.0));
    for (auto* le : root.getChildWithTagNameIterator ("LAYER"))
    {
        const int i = le->getIntAttribute ("index", -1);
        if (i < 0 || i >= loops.numLayers) continue;
        auto& L = loops.layers[(size_t) i];
        L.mode = (bc::LoopMode) juce::jlimit (0, 3, le->getIntAttribute ("mode", 1));
        L.bars = juce::jlimit (1, bc::kLoopMaxBars, le->getIntAttribute ("bars", 2));
        const int pb = le->getIntAttribute ("perBar", 16); L.perBar = (pb == 8 || pb == 12 || pb == 16 || pb == 24) ? pb : 16;
        L.density = (float) juce::jlimit (0.0, 1.0, le->getDoubleAttribute ("density", 0.5)); L.dejavu = (float) juce::jlimit (0.0, 1.0, le->getDoubleAttribute ("dejavu", 0.6));
        L.levelDb = (float) juce::jlimit (-60.0, 12.0, le->getDoubleAttribute ("level")); L.pan = (float) juce::jlimit (-1.0, 1.0, le->getDoubleAttribute ("pan"));
        L.mute = le->getIntAttribute ("mute") != 0; L.solo = le->getIntAttribute ("solo") != 0; L.random = le->getIntAttribute ("random") != 0;
        L.midiMode = juce::jlimit (0, 1, le->getIntAttribute ("midi")); L.rhythm = juce::jlimit (-1, nb::numPatterns() - 1, le->getIntAttribute ("rhythm", -1)); L.rhythmLane = juce::jlimit (0, 15, le->getIntAttribute ("rhythmLane", 4));
        L.seed = (uint64_t) le->getStringAttribute ("seed", "1").getHexValue64();
        L.source.kind = (bc::LoopSource::Kind) juce::jlimit (0, 2, le->getIntAttribute ("sourceKind")); L.source.from = le->getStringAttribute ("sourceFrom").toStdString(); L.source.family = juce::jlimit (0, 11, le->getIntAttribute ("sourceFamily"));
        for (auto* he : le->getChildWithTagNameIterator ("HIT"))
        {
            const int si = he->getIntAttribute ("sound", -1);
            if (si < 0 || si >= (int) sounds.size() || sounds[(size_t) si] == nullptr) continue;
            bc::LoopHit h; h.sound = sounds[(size_t) si];
            h.step = juce::jlimit (0, L.steps() - 1, he->getIntAttribute ("step")); h.len = juce::jlimit (1, 64, he->getIntAttribute ("len", 1)); h.ratchet = juce::jlimit (1, 4, he->getIntAttribute ("ratchet", 1));
            h.semis = (float) juce::jlimit (-36.0, 36.0, he->getDoubleAttribute ("semis")); h.levelDb = (float) juce::jlimit (-60.0, 24.0, he->getDoubleAttribute ("level")); h.vel = (float) juce::jlimit (0.0, 1.0, he->getDoubleAttribute ("vel", 1.0));
            h.reverse = he->getIntAttribute ("reverse") != 0; h.locked = he->getIntAttribute ("locked") != 0;
            L.hits.push_back (h);
        }
        std::sort (L.hits.begin(), L.hits.end(), [] (const bc::LoopHit& a, const bc::LoopHit& b) { return a.step < b.step; });
    }
    loopsUndoStack.clear(); loopsRedoStack.clear(); loopsSaved = loops;
    loopsChanged();
}
