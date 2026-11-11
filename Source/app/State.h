// BatchCut - the session's settings and the user's edits as XML (saved with the project).
#pragma once

#include <juce_core/juce_core.h>
#include "../core/Session.h"

namespace bcstate
{
inline juce::String floats (const float* v, int n) { juce::StringArray a; for (int i = 0; i < n; ++i) a.add (juce::String (v[i], 6)); return a.joinIntoString (" "); }
inline void readFloats (const juce::String& s, float* v, int n)
{
    auto a = juce::StringArray::fromTokens (s, " ", ""); a.removeEmptyStrings();
    for (int i = 0; i < n; ++i) { const float x = i < a.size() ? a[i].getFloatValue() : 0.0f; v[i] = std::isfinite (x) ? x : 0.0f; }
}
inline float clampF (double v, float lo, float hi) { return std::isfinite (v) ? juce::jlimit (lo, hi, (float) v) : lo; }

inline void writeEvent (juce::XmlElement& x, const bc::Event& e)
{
    x.setAttribute ("start", juce::String ((juce::int64) e.start)); x.setAttribute ("end", juce::String ((juce::int64) e.end)); x.setAttribute ("onset", juce::String ((juce::int64) e.onset));
    x.setAttribute ("frame", e.frame); x.setAttribute ("salience", e.salience); x.setAttribute ("snr", e.snrDb); x.setAttribute ("peak", e.peak);
    x.setAttribute ("enabled", e.enabled ? 1 : 0); x.setAttribute ("manual", e.manual ? 1 : 0); x.setAttribute ("userEnd", e.userEnd ? 1 : 0); x.setAttribute ("found", e.found ? 1 : 0);
    x.setAttribute ("feat", floats (e.feat.data(), bc::kFeat));
}
inline bc::Event readEvent (const juce::XmlElement& x)
{
    bc::Event e;
    e.start = std::max<juce::int64> (0, x.getStringAttribute ("start").getLargeIntValue());
    e.end = std::max<juce::int64> (e.start + 1, x.getStringAttribute ("end").getLargeIntValue());
    e.onset = juce::jlimit<juce::int64> (e.start, e.end, x.getStringAttribute ("onset").getLargeIntValue());
    e.frame = std::max (0, x.getIntAttribute ("frame"));
    e.salience = clampF (x.getDoubleAttribute ("salience"), -1e6f, 1e6f); e.snrDb = clampF (x.getDoubleAttribute ("snr"), -200, 200); e.peak = clampF (x.getDoubleAttribute ("peak"), 0, 100);
    e.enabled = x.getIntAttribute ("enabled", 1) != 0; e.manual = x.getIntAttribute ("manual") != 0; e.userEnd = x.getIntAttribute ("userEnd") != 0; e.found = x.getIntAttribute ("found") != 0;
    readFloats (x.getStringAttribute ("feat"), e.feat.data(), bc::kFeat);
    return e;
}

inline void writeShape (juce::XmlElement& x, const bc::Shape& s)
{
    x.setAttribute ("t0", s.t0); x.setAttribute ("t1", s.t1); x.setAttribute ("y0", s.y0); x.setAttribute ("y1", s.y1);
    x.setAttribute ("w", s.w); x.setAttribute ("h", s.h);
    juce::MemoryBlock mb (s.a.data(), s.a.size());
    x.setAttribute ("a", mb.toBase64Encoding());
}
inline bool readShape (const juce::XmlElement& x, bc::Shape& s)
{
    s.t0 = std::max (0.0, x.getDoubleAttribute ("t0")); s.t1 = std::max (s.t0, x.getDoubleAttribute ("t1"));
    s.y0 = clampF (x.getDoubleAttribute ("y0"), 0, 1); s.y1 = clampF (x.getDoubleAttribute ("y1"), 0, 1);
    s.w = juce::jlimit (1, 4096, x.getIntAttribute ("w", 1)); s.h = juce::jlimit (1, 4096, x.getIntAttribute ("h", 1));
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (x.getStringAttribute ("a")) || (int) mb.getSize() != s.w * s.h) return false;
    s.a.assign ((const uint8_t*) mb.getData(), (const uint8_t*) mb.getData() + mb.getSize());
    return true;
}

inline void writeLayers (juce::XmlElement& x, const bc::LayerState& st)
{
    for (const auto& l : st.layers)
    {
        auto* e = x.createNewChildElement ("LAYER");
        e->setAttribute ("name", juce::String (l.name)); e->setAttribute ("gain", l.gainDb); e->setAttribute ("mute", l.mute ? 1 : 0); e->setAttribute ("solo", l.solo ? 1 : 0);
    }
    for (const auto& o : st.ops)
    {
        auto* e = x.createNewChildElement ("OP");
        e->setAttribute ("kind", (int) o.kind); e->setAttribute ("layer", o.layer); e->setAttribute ("target", o.target); e->setAttribute ("gain", o.gain);
        writeShape (*e->createNewChildElement ("SHAPE"), o.shape);
    }
}
inline void readLayers (const juce::XmlElement& x, bc::LayerState& st)
{
    std::vector<bc::Layer> layers;
    for (auto* e : x.getChildWithTagNameIterator ("LAYER"))
    {
        if ((int) layers.size() >= bc::kMaxLayers) break;
        bc::Layer l; l.name = e->getStringAttribute ("name", "Layer").toStdString(); l.gainDb = clampF (e->getDoubleAttribute ("gain"), -60, 12);
        l.mute = e->getIntAttribute ("mute") != 0; l.solo = e->getIntAttribute ("solo") != 0;
        layers.push_back (l);
    }
    if ((int) layers.size() >= bc::kAuto) st.layers = layers;
    st.ops.clear(); st.undone.clear();
    for (auto* e : x.getChildWithTagNameIterator ("OP"))
    {
        bc::EditOp o;
        o.kind = (bc::EditOp::Kind) juce::jlimit (0, 2, e->getIntAttribute ("kind"));
        o.layer = juce::jlimit (-1, (int) st.layers.size() - 1, e->getIntAttribute ("layer", -1));
        o.target = juce::jlimit (-1, (int) st.layers.size() - 1, e->getIntAttribute ("target", -1));
        o.gain = clampF (e->getDoubleAttribute ("gain"), 0, 16);
        if (auto* sh = e->getChildByName ("SHAPE")) { if (! readShape (*sh, o.shape)) continue; } else continue;
        if (o.kind != bc::EditOp::Gain && o.target < 0) continue;
        st.ops.push_back (std::move (o));
    }
}

inline std::unique_ptr<juce::XmlElement> writeSession (const bc::Session& s)
{
    auto root = std::make_unique<juce::XmlElement> ("SESSION");
    const auto& st = s.settings;
    auto* d = root->createNewChildElement ("DETECT");
    d->setAttribute ("sensitivity", st.detect.sensitivity); d->setAttribute ("minGap", st.detect.minGapMs); d->setAttribute ("preRoll", st.detect.preRollMs);
    d->setAttribute ("tail", st.detect.tailMs); d->setAttribute ("maxLen", st.detect.maxLenS); d->setAttribute ("minLen", st.detect.minLenMs);
    d->setAttribute ("cutAtNext", st.detect.cutAtNext ? 1 : 0); d->setAttribute ("grouping", st.detect.grouping); d->setAttribute ("endMode", st.detect.endMode);
    auto* p = root->createNewChildElement ("PICK");
    p->setAttribute ("mode", (int) st.pick.mode); p->setAttribute ("best", st.pick.bestN); p->setAttribute ("similarity", st.pick.similarity);
    auto* e = root->createNewChildElement ("EXPORT");
    e->setAttribute ("fadeIn", st.exp.fadeInMs); e->setAttribute ("fadeOut", st.exp.fadeOutMs); e->setAttribute ("normMode", st.exp.normMode); e->setAttribute ("loudDb", st.exp.loudDb); e->setAttribute ("matchAll", st.exp.matchAll ? 1 : 0);
    e->setAttribute ("normDb", st.exp.normDb); e->setAttribute ("separate", st.exp.separate ? 1 : 0); e->setAttribute ("bits", st.exp.bits);
    e->setAttribute ("dir", juce::String (st.outDir)); e->setAttribute ("folderPerFile", st.folderPerFile ? 1 : 0);
    if (st.like.valid)
    {
        auto* l = root->createNewChildElement ("LIKE");
        l->setAttribute ("name", juce::String (st.likeName)); l->setAttribute ("w", floats (st.like.w.data(), bc::kDet)); l->setAttribute ("feat", floats (st.like.feat.data(), bc::kFeat));
    }
    for (const auto& f : s.files)
    {
        auto* fx = root->createNewChildElement ("FILE");
        fx->setAttribute ("path", juce::String (f.info.path));
        for (int pi = 0; pi < (int) f.parts.size(); ++pi)
        {
            const auto& ed = f.parts[(size_t) pi];
            if (! ed.touched()) continue;
            auto* px = fx->createNewChildElement ("PART");
            px->setAttribute ("index", pi);
            for (const auto& k : ed.keep) writeEvent (*px->createNewChildElement ("KEEP"), k);
            for (auto v : ed.deleted) px->createNewChildElement ("DELETED")->setAttribute ("at", juce::String ((juce::int64) v));
            for (auto v : ed.off) px->createNewChildElement ("OFF")->setAttribute ("at", juce::String ((juce::int64) v));
            for (const auto& c : ed.groupsOff) px->createNewChildElement ("GROUPOFF")->setAttribute ("centre", floats (c.data(), bc::kFeat));
            writeLayers (*px->createNewChildElement ("LAYERS"), ed.layers);
        }
    }
    return root;
}

inline void readSession (const juce::XmlElement& root, bc::Session& s, std::vector<std::string>& paths)
{
    auto& st = s.settings;
    if (auto* d = root.getChildByName ("DETECT"))
    {
        st.detect.sensitivity = clampF (d->getDoubleAttribute ("sensitivity", 0.5), 0, 1); st.detect.minGapMs = clampF (d->getDoubleAttribute ("minGap", 50), 5, 1000);
        st.detect.preRollMs = clampF (d->getDoubleAttribute ("preRoll", 3), 0, 100); st.detect.tailMs = clampF (d->getDoubleAttribute ("tail", 20), 0, 1000);
        st.detect.maxLenS = clampF (d->getDoubleAttribute ("maxLen", 4), 0.05f, 30); st.detect.minLenMs = clampF (d->getDoubleAttribute ("minLen", 15), 1, 1000);
        st.detect.cutAtNext = d->getIntAttribute ("cutAtNext", 1) != 0; st.detect.grouping = clampF (d->getDoubleAttribute ("grouping", 0.5), 0, 1);
        st.detect.endMode = juce::jlimit (0, 3, d->getIntAttribute ("endMode", 0));
    }
    if (auto* p = root.getChildByName ("PICK"))
    {
        st.pick.mode = (bc::Pick) juce::jlimit (0, 2, p->getIntAttribute ("mode", 0)); st.pick.bestN = juce::jlimit (1, 16, p->getIntAttribute ("best", 1));
        st.pick.similarity = clampF (p->getDoubleAttribute ("similarity", 0.5), 0, 1);
    }
    if (auto* e = root.getChildByName ("EXPORT"))
    {
        st.exp.fadeInMs = clampF (e->getDoubleAttribute ("fadeIn", 1), 0, 100); st.exp.fadeOutMs = clampF (e->getDoubleAttribute ("fadeOut", 10), 0, 1000);
        st.exp.normMode = juce::jlimit (0, 2, e->hasAttribute ("normMode") ? e->getIntAttribute ("normMode") : (e->getIntAttribute ("normalize") != 0 ? 1 : 0));
        st.exp.normDb = clampF (e->getDoubleAttribute ("normDb", -1), -30, 0);
        st.exp.loudDb = clampF (e->getDoubleAttribute ("loudDb", -16), -40, -3); st.exp.matchAll = e->getIntAttribute ("matchAll", 1) != 0;
        st.exp.separate = e->getIntAttribute ("separate") != 0;
        const int bits = e->getIntAttribute ("bits", 24); st.exp.bits = bits == 16 || bits == 32 ? bits : 24;
        st.outDir = e->getStringAttribute ("dir").toStdString(); st.folderPerFile = e->getIntAttribute ("folderPerFile", 1) != 0;
    }
    st.like = bc::Template(); st.likeName.clear();
    if (auto* l = root.getChildByName ("LIKE"))
    {
        st.like.valid = true; st.likeName = l->getStringAttribute ("name").toStdString();
        readFloats (l->getStringAttribute ("w"), st.like.w.data(), bc::kDet); readFloats (l->getStringAttribute ("feat"), st.like.feat.data(), bc::kFeat);
        for (auto& w : st.like.w) w = std::max (0.0f, w);
    }
    // the files: added (probed) by the caller; the edits wait in the session's files once they exist
    paths.clear();
    s.files.clear();
    for (auto* fx : root.getChildWithTagNameIterator ("FILE"))
    {
        bc::FileState f; f.info.path = fx->getStringAttribute ("path").toStdString();
        if (f.info.path.empty()) continue;
        const auto slash = f.info.path.find_last_of ("/\\"); std::string n = slash == std::string::npos ? f.info.path : f.info.path.substr (slash + 1);
        const auto dot = n.find_last_of ('.'); f.info.name = dot == std::string::npos || dot == 0 ? n : n.substr (0, dot);
        for (auto* px : fx->getChildWithTagNameIterator ("PART"))
        {
            const int pi = juce::jlimit (0, 1000, px->getIntAttribute ("index"));
            if ((int) f.parts.size() <= pi) f.parts.resize ((size_t) pi + 1);
            auto& ed = f.parts[(size_t) pi];
            for (auto* k : px->getChildWithTagNameIterator ("KEEP")) if (ed.keep.size() < 100000) ed.keep.push_back (readEvent (*k));
            for (auto* k : px->getChildWithTagNameIterator ("DELETED")) ed.deleted.push_back (k->getStringAttribute ("at").getLargeIntValue());
            for (auto* k : px->getChildWithTagNameIterator ("OFF")) ed.off.push_back (k->getStringAttribute ("at").getLargeIntValue());
            for (auto* k : px->getChildWithTagNameIterator ("GROUPOFF")) { std::array<float, bc::kFeat> c {}; readFloats (k->getStringAttribute ("centre"), c.data(), bc::kFeat); ed.groupsOff.push_back (c); }
            if (auto* lx = px->getChildByName ("LAYERS")) readLayers (*lx, ed.layers);
        }
        if (f.parts.empty()) f.parts.resize (1);
        f.counts.assign (f.parts.size(), -1);
        s.files.push_back (f);
        paths.push_back (f.info.path);
    }
}
} // namespace bcstate
