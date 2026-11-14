// Batchcut 2.0 - the Kit room's side of the processor: the pool, the lanes, the audition, the .NJQ file.
#include "Processor.h"

namespace
{
std::shared_ptr<bc::PoolItem> makeItem (std::vector<float> l, std::vector<float> r, double sr, const std::string& name, const std::string& source)
{
    auto it = std::make_shared<bc::PoolItem>();
    it->l = std::move (l); it->r = std::move (r); it->sr = sr; it->name = name; it->source = source;
    // a kit's sound is a one-shot: no more than 12 s of it
    const size_t cap = (size_t) (sr * 12.0);
    if (it->l.size() > cap) { it->l.resize (cap); if (it->r.size() > cap) it->r.resize (cap); }
    bc::analyseItem (*it, nbkstore::learner());
    return it;
}
}

void BcProcessor::kitGather (bool allFiles)
{
    kitMessage = "Gathering the one-shots...";
    session.gather (allFiles);
}

void BcProcessor::kitTakeGathered()
{
    auto clips = session.takeGathered();
    if (clips.empty()) return;
    int added = 0;
    for (auto& c : clips)
    {
        if (kit.pool.size() >= 2000) break;
        bool dup = false;
        for (const auto& p : kit.pool) dup = dup || (p->name == c.name && p->source == c.source && p->l.size() == c.l.size());
        if (dup) continue;
        auto it = makeItem (std::move (c.l), std::move (c.r), c.sr, c.name, c.source);
        if (it->f.valid) { kit.pool.push_back (it); ++added; }
    }
    kitMessage = juce::String (added) + (added == 1 ? " one-shot" : " one-shots") + " added to the pool (" + juce::String ((int) kit.pool.size()) + " in all)";
    ++kitVersion; stateDirty = true;
}

int BcProcessor::kitAddFolder (const juce::File& folder)
{
    if (! folder.isDirectory()) return 0;
    auto found = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    found.sort();
    int added = 0;
    for (const auto& f : found)
    {
        if (kit.pool.size() >= 2000) break;
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples < 16) continue;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 12.0));
        juce::AudioBuffer<float> buf ((int) std::max (1u, r->numChannels), n);
        r->read (&buf, 0, n, 0, true, true);
        std::vector<float> l (buf.getReadPointer (0), buf.getReadPointer (0) + n);
        const float* rp = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : buf.getReadPointer (0);
        std::vector<float> rr (rp, rp + n);
        for (auto* ch : { &l, &rr }) for (auto& x : *ch) if (! std::isfinite (x)) x = 0.0f;
        const auto name = f.getFileNameWithoutExtension().toStdString(), src = folder.getFileName().toStdString();
        bool dup = false;
        for (const auto& p : kit.pool) dup = dup || (p->name == name && p->source == src && p->l.size() == l.size());
        if (dup) continue;
        auto it = makeItem (std::move (l), std::move (rr), r->sampleRate, name, src);
        if (it->f.valid) { kit.pool.push_back (it); ++added; }
    }
    kitMessage = juce::String (added) + " sounds from " + folder.getFileName() + " (" + juce::String ((int) kit.pool.size()) + " in the pool)";
    ++kitVersion; stateDirty = true;
    return added;
}

void BcProcessor::kitClearPool()
{
    // what the lanes use stays; the rest goes
    std::vector<std::shared_ptr<bc::PoolItem>> keep; std::vector<int> map (kit.pool.size(), -1);
    for (auto& ln : kit.lanes) for (auto& ly : ln.layers)
        if (ly.item >= 0 && ly.item < (int) kit.pool.size())
        {
            if (map[(size_t) ly.item] < 0) { map[(size_t) ly.item] = (int) keep.size(); keep.push_back (kit.pool[(size_t) ly.item]); }
            ly.item = map[(size_t) ly.item];
        }
    kit.pool = std::move (keep);
    kitMessage = "The pool is cleared (the lanes keep their sounds)";
    kitUndoStack.clear(); kitRedoStack.clear(); kitSaved = kit.lanes;     // (the sounds' numbers changed)
    kitChanged();
}

void BcProcessor::kitChoose()
{
    kit.choose (nbkstore::learner());
    int n = 0; for (const auto& ln : kit.lanes) n += ln.empty() ? 0 : 1;
    kitMessage = kit.pool.empty() ? juce::String ("The pool is empty: gather one-shots or add a folder first") : juce::String (n) + " of 16 lanes have a sound";
    kitChanged(); kitMark();
}

void BcProcessor::kitShuffle()
{
    kit.shuffle (nbkstore::learner(), (uint32_t) juce::Random::getSystemRandom().nextInt());
    kitMessage = kit.pool.empty() ? juce::String ("The pool is empty: gather one-shots or add a folder first") : juce::String ("Shuffled: locked lanes kept theirs");
    kitChanged(); kitMark();
}

void BcProcessor::kitAssign (int lane, int layer, int item, bool teach)
{
    if (lane < 0 || lane >= bc::kLanes || layer < 0 || layer >= bc::kLayers) return;
    auto& ly = kit.lanes[(size_t) lane].layers[(size_t) layer];
    const bool fresh = ly.item < 0;
    ly.item = item >= 0 && item < (int) kit.pool.size() ? item : -1;
    if (fresh || ly.item < 0) { const int keep = ly.item; ly = bc::LaneLayer(); ly.item = keep; }   // a new sound starts untrimmed
    if (teach && ly.item >= 0 && layer == 0)
    {
        auto& it = *kit.pool[(size_t) ly.item];
        nbkstore::learner().teachRole (it.x, lane, it.name);
        nbkstore::save();
        for (auto& p : kit.pool) { p->roles = nbkstore::learner().roles (p->f); p->bestRole = (int) (std::max_element (p->roles.begin(), p->roles.end()) - p->roles.begin()); }
    }
    kitChanged (lane); kitMark();
}

void BcProcessor::kitNext (int lane, int dir)
{
    if (lane < 0 || lane >= bc::kLanes) return;
    const auto c = kitCandidates (lane);
    if (c.empty()) return;
    const int cur = kit.lanes[(size_t) lane].layers[0].item;
    int at = -1; for (int i = 0; i < (int) c.size(); ++i) if (c[(size_t) i] == cur) at = i;
    at = at < 0 ? 0 : (at + dir + (int) c.size()) % (int) c.size();
    kitAssign (lane, 0, c[(size_t) at], false);
    kitPlayLane (lane);
}

void BcProcessor::kitRate (int item, float stars)
{
    if (item < 0 || item >= (int) kit.pool.size()) return;
    auto& it = *kit.pool[(size_t) item];
    nbkstore::learner().rate (it.x, it.bestRole, stars, it.name);
    nbkstore::save();
    ++kitVersion;
}
float BcProcessor::kitRating (int item)
{
    return item >= 0 && item < (int) kit.pool.size() ? nbkstore::learner().ratingOf (kit.pool[(size_t) item]->x) : 0.0f;
}

void BcProcessor::kitChanged (int lane)
{
    for (int l = 0; l < bc::kLanes; ++l)
    {
        if (lane >= 0 && l != lane) continue;
        auto s = std::make_shared<bc::KitSound>();
        std::shared_ptr<const bc::KitSound> out;
        if (kit.flatten (l, *s)) out = s;
        laneSounds[(size_t) l] = out;
        if (out) kitKeep.push_back (out);
        kitPlayer.setSound (l, out);
    }
    if (kitKeep.size() > 400) kitKeep.erase (kitKeep.begin(), kitKeep.begin() + 200);   // (those are long past playing)
    kitPlayer.bpm.store ((float) kit.bpm); kitPlayer.swing.store (kit.swing);
    ++kitVersion; stateDirty = true;
}

void BcProcessor::kitMark()
{
    if (kit.lanes == kitSaved) return;
    kitUndoStack.push_back (kitSaved); kitRedoStack.clear(); kitSaved = kit.lanes;
    if (kitUndoStack.size() > 100) kitUndoStack.erase (kitUndoStack.begin());
}
void BcProcessor::kitUndo()
{
    kitMark();
    if (kitUndoStack.empty()) return;
    kitRedoStack.push_back (kit.lanes); kit.lanes = kitSaved = kitUndoStack.back(); kitUndoStack.pop_back();
    kitChanged();
}
void BcProcessor::kitRedo()
{
    if (kitRedoStack.empty()) return;
    kitUndoStack.push_back (kit.lanes); kit.lanes = kitSaved = kitRedoStack.back(); kitRedoStack.pop_back();
    kitChanged();
}

void BcProcessor::kitSetPattern (int index, bool takeTempo)
{
    kit.pattern = juce::jlimit (0, nb::numPatterns() - 1, index);
    const auto p = nb::patternPreset (kit.pattern);
    kitPlayer.setPattern (p.pattern);
    if (takeTempo) { kit.bpm = p.tempo; kit.swing = p.swing; }
    kitPlayer.bpm.store ((float) kit.bpm); kitPlayer.swing.store (kit.swing);
    ++kitVersion; stateDirty = true;
}

void BcProcessor::kitPlayItem (int item)
{
    if (item < 0 || item >= (int) kit.pool.size()) return;
    const auto& it = *kit.pool[(size_t) item];
    preview.playClip (it.l, it.r, it.sr);
}

bool BcProcessor::kitSaveNjq (const juce::File& file, juce::String& error)
{
    auto k = std::make_unique<njq::Kit>();
    k->name = juce::String (kit.name); k->madeWith = "Batchcut 2.0";
    k->bpm = kit.bpm; k->swing = kit.swing; k->patternIndex = kit.pattern;
    k->scenes[0] = nb::patternPreset (kit.pattern).pattern;
    for (int s = 1; s < nb::kNumScenes; ++s) { k->scenes[(size_t) s] = nb::Pattern(); k->scenes[(size_t) s].length = k->scenes[0].length; }
    int lanes = 0;
    for (int l = 0; l < bc::kLanes; ++l)
    {
        bc::KitSound s;
        if (! kit.flatten (l, s)) continue;                    // an empty lane keeps NeoBeat's own sound
        auto& ln = k->lanes[(size_t) l];
        ln.set = true; ln.role = l; ln.hasSample = true;
        const auto* first = kit.item (kit.lanes[(size_t) l].layers[0].item);
        ln.name = first != nullptr ? juce::String (first->name) : juce::String (nb::roleName (l));
        ln.l = std::move (s.l); ln.r = std::move (s.r); ln.sampleRate = s.sr;
        ln.levelDb = juce::jlimit (-30.0f, 24.0f, 20.0f * std::log10 (std::max (1.0e-4f, s.gain)));
        ln.pan = s.pan; ln.width = 1.0f; ln.decay = 1.0f; ln.evolve = 0.5f;
        // hats cut each other, as on a real kit
        ln.choke = (l == nbk::ClosedHat || l == nbk::OpenHat) ? 1 : 0;
        ++lanes;
    }
    if (lanes == 0) { error = "No lane has a sound yet."; return false; }
    if (! njq::save (file, *k, error)) return false;
    kitMessage = "Saved " + file.getFileName() + " (" + juce::String (lanes) + " lanes). NeoBeat: KIT / FILE > My sequences.";
    return true;
}

// ---------------------------------------------------------------- the kit in the project
// The lanes' sounds travel with the project (lossless, a few megabytes at most); the rest of the pool is
// gathered again with one click.
std::unique_ptr<juce::XmlElement> BcProcessor::kitToXml()
{
    auto root = std::make_unique<juce::XmlElement> ("KIT");
    root->setAttribute ("name", juce::String (kit.name)); root->setAttribute ("bpm", kit.bpm); root->setAttribute ("swing", (double) kit.swing); root->setAttribute ("pattern", kit.pattern);
    std::vector<int> used; auto ref = [&] (int item) { for (int i = 0; i < (int) used.size(); ++i) if (used[(size_t) i] == item) return i; used.push_back (item); return (int) used.size() - 1; };
    for (int l = 0; l < bc::kLanes; ++l)
    {
        const auto& ln = kit.lanes[(size_t) l];
        auto* le = root->createNewChildElement ("LANE");
        le->setAttribute ("index", l); le->setAttribute ("level", (double) ln.levelDb); le->setAttribute ("pan", (double) ln.pan); le->setAttribute ("width", (double) ln.width); le->setAttribute ("locked", ln.locked ? 1 : 0);
        for (int y = 0; y < bc::kLayers; ++y)
        {
            const auto& ly = ln.layers[(size_t) y];
            if (ly.item < 0 || ly.item >= (int) kit.pool.size()) continue;
            auto* ye = le->createNewChildElement ("LAYER");
            ye->setAttribute ("index", y); ye->setAttribute ("item", ref (ly.item)); ye->setAttribute ("level", (double) ly.levelDb); ye->setAttribute ("offset", (double) ly.offsetMs);
            ye->setAttribute ("semis", (double) ly.semis); ye->setAttribute ("start", (double) ly.start); ye->setAttribute ("end", (double) ly.end); ye->setAttribute ("fade", (double) ly.fadeMs); ye->setAttribute ("reverse", ly.reverse ? 1 : 0);
        }
    }
    juce::FlacAudioFormat flac;
    for (int item : used)
    {
        const auto& it = *kit.pool[(size_t) item];
        auto* ie = root->createNewChildElement ("SOUND");
        ie->setAttribute ("name", juce::String (it.name)); ie->setAttribute ("source", juce::String (it.source));
        juce::MemoryBlock mb;
        {
            auto* os = new juce::MemoryOutputStream (mb, false);
            std::unique_ptr<juce::AudioFormatWriter> w (flac.createWriterFor (os, it.sr, 2, 24, {}, 5));
            if (w == nullptr) { delete os; continue; }
            const int n = (int) it.l.size();
            juce::AudioBuffer<float> buf (2, n);
            buf.copyFrom (0, 0, it.l.data(), n); buf.copyFrom (1, 0, it.r.data(), n);
            const float pk = buf.getMagnitude (0, n);
            if (pk > 0.999f) { buf.applyGain (0.999f / pk); ie->setAttribute ("gain", (double) (pk / 0.999f)); }
            w->writeFromAudioSampleBuffer (buf, 0, n);
        }
        ie->addTextElement (mb.toBase64Encoding());
    }
    return root;
}

void BcProcessor::kitFromXml (const juce::XmlElement& root)
{
    kit = bc::KitModel();
    kit.name = root.getStringAttribute ("name", "My kit").toStdString();
    kit.bpm = juce::jlimit (30.0, 300.0, root.getDoubleAttribute ("bpm", 100.0)); kit.swing = (float) juce::jlimit (0.5, 0.75, root.getDoubleAttribute ("swing", 0.62));
    kit.pattern = juce::jlimit (0, nb::numPatterns() - 1, root.getIntAttribute ("pattern"));
    juce::FlacAudioFormat flac;
    std::vector<int> index;    // the file's sound number -> the pool's (or -1 when it could not be read)
    for (auto* ie : root.getChildWithTagNameIterator ("SOUND"))
    {
        index.push_back (-1);
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (ie->getAllSubText().trim())) continue;
        std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (mb, false), true));
        if (r == nullptr || r->lengthInSamples < 2) continue;
        const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 12.0));
        juce::AudioBuffer<float> buf (2, n);
        r->read (&buf, 0, n, 0, true, true);
        buf.applyGain ((float) ie->getDoubleAttribute ("gain", 1.0));
        std::vector<float> l (buf.getReadPointer (0), buf.getReadPointer (0) + n), rr (buf.getReadPointer (1), buf.getReadPointer (1) + n);
        kit.pool.push_back (makeItem (std::move (l), std::move (rr), r->sampleRate, ie->getStringAttribute ("name").toStdString(), ie->getStringAttribute ("source").toStdString()));
        index.back() = (int) kit.pool.size() - 1;
    }
    for (auto* le : root.getChildWithTagNameIterator ("LANE"))
    {
        const int l = le->getIntAttribute ("index", -1);
        if (l < 0 || l >= bc::kLanes) continue;
        auto& ln = kit.lanes[(size_t) l];
        ln.levelDb = (float) juce::jlimit (-30.0, 24.0, le->getDoubleAttribute ("level")); ln.pan = (float) juce::jlimit (-1.0, 1.0, le->getDoubleAttribute ("pan"));
        ln.width = (float) juce::jlimit (0.0, 1.0, le->getDoubleAttribute ("width", 1.0)); ln.locked = le->getIntAttribute ("locked") != 0;
        for (auto* ye : le->getChildWithTagNameIterator ("LAYER"))
        {
            const int y = ye->getIntAttribute ("index", -1), it = ye->getIntAttribute ("item", -1);
            if (y < 0 || y >= bc::kLayers || it < 0 || it >= (int) index.size() || index[(size_t) it] < 0) continue;
            auto& ly = ln.layers[(size_t) y];
            ly.item = index[(size_t) it]; ly.levelDb = (float) juce::jlimit (-36.0, 24.0, ye->getDoubleAttribute ("level")); ly.offsetMs = (float) juce::jlimit (-200.0, 500.0, ye->getDoubleAttribute ("offset"));
            ly.semis = (float) juce::jlimit (-24.0, 24.0, ye->getDoubleAttribute ("semis")); ly.start = (float) juce::jlimit (0.0, 1.0, ye->getDoubleAttribute ("start")); ly.end = (float) juce::jlimit (0.0, 1.0, ye->getDoubleAttribute ("end", 1.0));
            ly.fadeMs = (float) juce::jlimit (0.0, 500.0, ye->getDoubleAttribute ("fade", 4.0)); ly.reverse = ye->getIntAttribute ("reverse") != 0;
        }
    }
    kitSetPattern (kit.pattern);
    kitUndoStack.clear(); kitRedoStack.clear(); kitSaved = kit.lanes;
    kitChanged();
}
