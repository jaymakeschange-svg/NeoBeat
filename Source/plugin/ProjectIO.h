// NeoBeat 2.0 — the project as XML (host state, user presets). Each struct is described once by a
// visit() that both the writer and the reader walk, so the two can never drift apart.
#pragma once

#include <juce_core/juce_core.h>
#include "../engine/Project.h"
#include "../shared/PatternXml.h"

namespace io
{
struct Writer
{
    juce::XmlElement& e;
    void operator() (const char* n, float& v) { e.setAttribute (n, (double) v); }
    void operator() (const char* n, double& v) { e.setAttribute (n, v); }
    void operator() (const char* n, int& v) { e.setAttribute (n, v); }
    void operator() (const char* n, bool& v) { e.setAttribute (n, v ? 1 : 0); }
    void operator() (const char* n, uint32_t& v) { e.setAttribute (n, juce::String::toHexString ((int) v)); }
    void operator() (const char* n, std::string& v) { e.setAttribute (n, juce::String (v)); }
};
struct Reader
{
    const juce::XmlElement& e;
    void operator() (const char* n, float& v) { if (e.hasAttribute (n)) v = (float) e.getDoubleAttribute (n, v); }
    void operator() (const char* n, double& v) { if (e.hasAttribute (n)) v = e.getDoubleAttribute (n, v); }
    void operator() (const char* n, int& v) { if (e.hasAttribute (n)) v = e.getIntAttribute (n, v); }
    void operator() (const char* n, bool& v) { if (e.hasAttribute (n)) v = e.getIntAttribute (n, v ? 1 : 0) != 0; }
    void operator() (const char* n, uint32_t& v) { if (e.hasAttribute (n)) v = (uint32_t) e.getStringAttribute (n).getHexValue32(); }
    void operator() (const char* n, std::string& v) { if (e.hasAttribute (n)) v = e.getStringAttribute (n).toStdString(); }
};

template <class V> void visit (nb::RateSpec& r, V& v, const char* pre)
{
    const juce::String p (pre);
    v ((p + "tempo").toRawUTF8(), r.tempo); v ((p + "beats").toRawUTF8(), r.beats); v ((p + "ms").toRawUTF8(), r.ms); v ((p + "swing").toRawUTF8(), r.swing);
}
template <class V> void visit (nb::StretchParams& p, V& v)
{
    visit (p.rate, v, "rate_");
    v ("stretch", p.stretch); v ("creep", p.creep); v ("holdFade", p.holdFade); v ("snap", p.snap); v ("slipMs", p.slipMs); v ("grit", p.grit);
    v ("tape", p.tape); v ("attackMs", p.attackMs); v ("wet", p.wet); v ("seed", p.seed); v ("peel", p.peel); v ("freeze", p.freeze);
    v ("screechHz", p.screechHz); v ("adhesive", p.adhesive); v ("grainMs", p.grainMs); v ("rasp", p.rasp); v ("roll", p.roll);
    v ("rollBody", p.rollBody); v ("rollSize", p.rollSize); v ("strip", p.strip); v ("width", p.width); v ("gateBeats", p.gateBeats);
    v ("gateMs", p.gateMs); v ("decayMs", p.decayMs);
}
template <class V> void visit (nb::StretchLine& l, V& v)
{
    v ("on", l.on); v ("fLo", l.fLo); v ("fHi", l.fHi); v ("edge", l.edge); v ("offset", l.offset); v ("tilt", l.tilt); v ("spray", l.spray); v ("level", l.level); v ("pan", l.pan);
}
template <class V> void visit (nb::BounceParams& p, V& v)
{
    visit (p.rate, v, "rate_");
    v ("gridBeats", p.gridBeats); v ("gridMs", p.gridMs); v ("bounciness", p.bounciness); v ("maxBounces", p.maxBounces); v ("distStart", p.distStart);
    v ("distEnd", p.distEnd); v ("speed", p.speed); v ("panRandom", p.panRandom); v ("panCenter", p.panCenter); v ("width", p.width);
    v ("room", p.room); v ("wet", p.wet); v ("tail", p.tail); v ("seed", p.seed);
}

inline juce::XmlElement* writeFx (juce::XmlElement& parent, const char* tag, const nb::CellFx& c0)
{
    nb::CellFx c = c0;
    auto* e = parent.createNewChildElement (tag);
    e->setAttribute ("kind", (int) c.kind);
    e->setAttribute ("fx", (int) c.fx);
    if (c.kind == nb::CellFx::Library)
    {
        juce::String s; for (float x : c.settings.p) s << juce::String (x, 5) << " ";
        e->setAttribute ("p", s.trim());
        e->setAttribute ("seed", (int) c.settings.seed);
    }
    if (c.kind == nb::CellFx::FrictionStretchFx)
    {
        auto* f = e->createNewChildElement ("FS"); Writer w { *f }; visit (c.stretch, w);
        for (auto& l : c.stretch.lines) { auto* le = f->createNewChildElement ("LINE"); Writer lw { *le }; visit (l, lw); }
    }
    if (c.kind == nb::CellFx::BounceDelayFx) { auto* b = e->createNewChildElement ("BD"); Writer w { *b }; visit (c.bounce, w); }
    return e;
}

inline nb::CellFx readFx (const juce::XmlElement* e)
{
    nb::CellFx c;
    if (e == nullptr) return c;
    c.kind = (nb::CellFx::Kind) juce::jlimit (0, 3, e->getIntAttribute ("kind", 0));
    c.fx = (nb::MatrixFx) juce::jlimit (0, (int) nb::MatrixFx::Count - 1, e->getIntAttribute ("fx", 0));
    if (c.kind == nb::CellFx::Library)
    {
        c.settings = nb::FxSettings::defaults (c.fx);
        auto toks = juce::StringArray::fromTokens (e->getStringAttribute ("p"), " ", "");
        const auto& info = nb::matrixFxInfo()[(size_t) c.fx].params;
        for (int i = 0; i < toks.size() && i < (int) c.settings.p.size(); ++i)
            c.settings.p[(size_t) i] = juce::jlimit (info[(size_t) i].lo, info[(size_t) i].hi, toks[i].getFloatValue());
        c.settings.seed = (uint32_t) e->getIntAttribute ("seed", 1);
    }
    if (auto* f = e->getChildByName ("FS"))
    {
        Reader r { *f }; visit (c.stretch, r);
        c.stretch.stretch = juce::jlimit (1.0f, 64.0f, c.stretch.stretch);
        c.stretch.lines.clear();
        for (auto* le : f->getChildWithTagNameIterator ("LINE")) { nb::StretchLine l; Reader lr { *le }; visit (l, lr); if (c.stretch.lines.size() < 6) c.stretch.lines.push_back (l); }
    }
    if (auto* b = e->getChildByName ("BD")) { Reader r { *b }; visit (c.bounce, r); c.bounce.maxBounces = juce::jlimit (1, 64, c.bounce.maxBounces); }
    return c;
}

template <class V> void visit (nb::Zone& z, V& v)
{
    v ("name", z.name); v ("layer", z.layer); v ("loKey", z.loKey); v ("hiKey", z.hiKey); v ("loVel", z.loVel); v ("hiVel", z.hiVel); v ("root", z.root);
    int src = (int) z.src; v ("src", src); z.src = (nb::ZoneSource) juce::jlimit (0, 4, src);
    v ("index", z.index); v ("file", z.file); v ("tune", z.tune); v ("gainDb", z.gainDb); v ("pan", z.pan); v ("keytrack", z.keytrack);
    v ("rrGroup", z.rrGroup); v ("choke", z.choke);
}
template <class V> void visit (nb::Mark& m, V& v)
{
    int t = (int) m.type; v ("type", t); m.type = (nb::MarkType) juce::jlimit (0, 4, t);
    v ("start", m.start); v ("length", m.length); v ("row", m.row); v ("intensity", m.intensity); v ("inertia", m.inertia);
    v ("level", m.level); v ("depth", m.depth); v ("rate", m.rate); v ("pan", m.pan);
}
template <class V> void visitSeq (nb::SeqPreset& p, V& v)
{
    v ("name", p.name); int e = (int) p.engine; v ("engine", e); p.engine = (nb::MarkEngine) juce::jlimit (0, 3, e);
    v ("root", p.root); v ("scale", p.scale); v ("grid", p.grid); v ("bars", p.bars); v ("mirror", p.mirror); v ("baseOctave", p.baseOctave);
    v ("grainMs", p.grainMs); v ("spray", p.spray); v ("samplePad", p.samplePad); v ("level", p.level);
}
template <class V> void visit (nb::LfoSpec& l, V& v, const char* pre)
{
    const juce::String p (pre);
    v ((p + "shape").toRawUTF8(), l.shape); v ((p + "sync").toRawUTF8(), l.sync); v ((p + "beats").toRawUTF8(), l.beats); v ((p + "hz").toRawUTF8(), l.hz);
    int s = (int) l.scope; v ((p + "scope").toRawUTF8(), s); l.scope = (nb::Scope) juce::jlimit (0, 2, s);
}

// ---------------------------------------------------------------- sections (also used by user presets)
inline void writeOneShot (juce::XmlElement& e, const nb::OneShotPreset& p)
{
    e.setAttribute ("name", juce::String (p.name));
    for (const auto& c : p.cells)
    {
        auto* ce = e.createNewChildElement ("CELL");
        ce->setAttribute ("pad", c.pad); ce->setAttribute ("step", c.step); ce->setAttribute ("span", c.span); ce->setAttribute ("height", (double) c.height);
        if (c.scene != 0) ce->setAttribute ("scene", c.scene);
        writeFx (*ce, "FX", c.fx);
    }
}
inline void readOneShot (const juce::XmlElement& e, nb::OneShotPreset& p)
{
    p = {};
    p.name = e.getStringAttribute ("name", "INIT").toStdString();
    for (auto* ce : e.getChildWithTagNameIterator ("CELL"))
    {
        nb::OneShotCell c;
        c.pad = juce::jlimit (0, 15, ce->getIntAttribute ("pad")); c.step = juce::jlimit (0, nb::Pattern::kMaxSteps - 1, ce->getIntAttribute ("step"));
        c.scene = juce::jlimit (0, nb::kNumScenes - 1, ce->getIntAttribute ("scene", 0));
        c.span = juce::jlimit (1, 64, ce->getIntAttribute ("span", 2)); c.height = (float) juce::jlimit (0.0, 1.0, ce->getDoubleAttribute ("height", 0.5));
        c.fx = readFx (ce->getChildByName ("FX"));
        if (p.cells.size() < 2048) p.cells.push_back (c);
    }
}
inline void writeSample (juce::XmlElement& e, const nb::SamplePreset& p)
{
    e.setAttribute ("name", juce::String (p.name)); e.setAttribute ("pad", p.pad); e.setAttribute ("file", juce::String (p.file)); e.setAttribute ("seconds", (double) p.seconds);
    for (const auto& o : p.ops)
    {
        auto* oe = e.createNewChildElement ("OP");
        oe->setAttribute ("c0", o.c0); oe->setAttribute ("c1", o.c1); oe->setAttribute ("fLo", (double) o.fLo); oe->setAttribute ("fHi", (double) o.fHi); oe->setAttribute ("mix", (double) o.mix);
        writeFx (*oe, "FX", o.fx);
    }
}
inline void readSample (const juce::XmlElement& e, nb::SamplePreset& p)
{
    p = {};
    p.name = e.getStringAttribute ("name", "INIT").toStdString(); p.pad = juce::jlimit (0, 15, e.getIntAttribute ("pad", 15));
    p.file = e.getStringAttribute ("file").toStdString(); p.seconds = (float) e.getDoubleAttribute ("seconds", 2.0);
    for (auto* oe : e.getChildWithTagNameIterator ("OP"))
    {
        nb::SampleOp o;
        o.c0 = juce::jlimit (0, 32, oe->getIntAttribute ("c0")); o.c1 = juce::jlimit (0, 32, oe->getIntAttribute ("c1", 4));
        o.fLo = (float) oe->getDoubleAttribute ("fLo", 200.0); o.fHi = (float) oe->getDoubleAttribute ("fHi", 4000.0); o.mix = (float) oe->getDoubleAttribute ("mix", 1.0);
        o.fx = readFx (oe->getChildByName ("FX"));
        if (p.ops.size() < 64) p.ops.push_back (o);
    }
}
inline void writeSeq (juce::XmlElement& e, const nb::SeqPreset& p0)
{
    nb::SeqPreset p = p0; Writer w { e }; visitSeq (p, w);
    for (auto m : p.marks) { auto* me = e.createNewChildElement ("MARK"); Writer mw { *me }; visit (m, mw); }
}
inline void readSeq (const juce::XmlElement& e, nb::SeqPreset& p)
{
    p = {};
    Reader r { e }; visitSeq (p, r);
    p.scale = juce::jlimit (0, (int) nb::scaleNames().size() - 1, p.scale); p.grid = juce::jlimit (0, 3, p.grid); p.bars = juce::jlimit (1, 32, p.bars);
    for (auto* me : e.getChildWithTagNameIterator ("MARK"))
    {
        nb::Mark m; Reader mr { *me }; visit (m, mr);
        m.row = juce::jlimit (0, nb::SeqPreset::kRows - 1, m.row);
        if (p.marks.size() < 512) p.marks.push_back (m);
    }
}
inline void writeKeyMap (juce::XmlElement& e, const nb::KeyMapState& k)
{
    for (const auto& l : k.layers) { auto* le = e.createNewChildElement ("LAYER"); le->setAttribute ("name", juce::String (l.name)); le->setAttribute ("colour", juce::String::toHexString ((int) l.colour)); le->setAttribute ("on", l.on ? 1 : 0); }
    for (auto z : k.zones) { auto* ze = e.createNewChildElement ("ZONE"); Writer w { *ze }; visit (z, w); writeFx (*ze, "FX", z.fx); }
}
inline void readKeyMap (const juce::XmlElement& e, nb::KeyMapState& k)
{
    k.layers.clear(); k.zones.clear();
    for (auto* le : e.getChildWithTagNameIterator ("LAYER"))
        k.layers.push_back ({ le->getStringAttribute ("name").toStdString(), (uint32_t) le->getStringAttribute ("colour", "ff7fe0d0").getHexValue32(), le->getIntAttribute ("on", 1) != 0 });
    for (auto* ze : e.getChildWithTagNameIterator ("ZONE"))
    {
        nb::Zone z; Reader r { *ze }; visit (z, r);
        z.loKey = juce::jlimit (0, 127, z.loKey); z.hiKey = juce::jlimit (z.loKey, 127, z.hiKey);
        z.loVel = juce::jlimit (1, 127, z.loVel); z.hiVel = juce::jlimit (z.loVel, 127, z.hiVel);
        z.fx = readFx (ze->getChildByName ("FX"));
        if (k.zones.size() < 256) k.zones.push_back (z);
    }
    if (k.layers.empty()) k.layers.push_back ({ "Layer", 0xff7fe0d0, true });
}
inline void writeGridPattern (juce::XmlElement& e, const nb::GridPattern& g)
{
    e.setAttribute ("length", g.length); e.setAttribute ("rate", g.rate); e.setAttribute ("swing", (double) g.swing);
    juce::MemoryBlock mb (g.cells.data(), g.cells.size() * sizeof (uint32_t));
    e.setAttribute ("cells", mb.toBase64Encoding());
}
inline void readGridPattern (const juce::XmlElement& e, nb::GridPattern& g)
{
    g.length = juce::jlimit (1, 64, e.getIntAttribute ("length", 64)); g.rate = juce::jlimit (0, 3, e.getIntAttribute ("rate", 1));
    g.swing = (float) e.getDoubleAttribute ("swing", -1.0);
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (e.getStringAttribute ("cells")) && mb.getSize() == g.cells.size() * sizeof (uint32_t))
        std::memcpy (g.cells.data(), mb.getData(), mb.getSize());
}
inline void writeGrid (juce::XmlElement& e, const nb::GridState& g, bool withPatterns = true)
{
    e.setAttribute ("folder", juce::String (g.folder)); e.setAttribute ("current", g.current); e.setAttribute ("play", g.play ? 1 : 0);
    e.setAttribute ("level", (double) g.level); e.setAttribute ("useKit", g.useKit ? 1 : 0);
    for (const auto& f : g.files) e.createNewChildElement ("FILE")->setAttribute ("path", juce::String (f));
    if (withPatterns) for (const auto& p : g.patterns) writeGridPattern (*e.createNewChildElement ("PATTERN"), p);
    for (int row = 0; row < (int) g.follow.size(); ++row)
    {
        const auto& f = g.follow[(size_t) row];
        if (! f.on) continue;
        auto* fe = e.createNewChildElement ("FOLLOW");
        fe->setAttribute ("row", row); fe->setAttribute ("lane", f.lane); fe->setAttribute ("mode", f.mode); fe->setAttribute ("nth", f.nth);
        fe->setAttribute ("offsetMs", (double) f.offsetMs); fe->setAttribute ("velocity", f.velocity ? 1 : 0); fe->setAttribute ("level", (double) f.level);
        fe->setAttribute ("pan", (double) f.pan); fe->setAttribute ("semis", (double) f.semis);
    }
}
inline void readGrid (const juce::XmlElement& e, nb::GridState& g)
{
    g = {};
    g.folder = e.getStringAttribute ("folder").toStdString(); g.current = juce::jlimit (0, 15, e.getIntAttribute ("current"));
    g.play = e.getIntAttribute ("play") != 0; g.level = (float) e.getDoubleAttribute ("level", 0.8); g.useKit = e.getIntAttribute ("useKit", 1) != 0;
    for (auto* fe : e.getChildWithTagNameIterator ("FILE")) if (g.files.size() < 64) g.files.push_back (fe->getStringAttribute ("path").toStdString());
    int i = 0;
    for (auto* pe : e.getChildWithTagNameIterator ("PATTERN")) if (i < 16) readGridPattern (*pe, g.patterns[(size_t) i++]);
    for (auto* fe : e.getChildWithTagNameIterator ("FOLLOW"))
    {
        const int row = fe->getIntAttribute ("row", -1);
        if (row < 0 || row >= (int) g.follow.size()) continue;
        auto& f = g.follow[(size_t) row];
        f.on = true; f.lane = juce::jlimit (0, nb::kNumPads - 1, fe->getIntAttribute ("lane")); f.mode = juce::jlimit (0, 2, fe->getIntAttribute ("mode"));
        f.nth = juce::jlimit (1, 16, fe->getIntAttribute ("nth", 2)); f.offsetMs = (float) juce::jlimit (-200.0, 200.0, fe->getDoubleAttribute ("offsetMs"));
        f.velocity = fe->getIntAttribute ("velocity", 1) != 0; f.level = (float) juce::jlimit (0.0, 2.0, fe->getDoubleAttribute ("level", 0.8));
        f.pan = (float) juce::jlimit (-1.0, 1.0, fe->getDoubleAttribute ("pan")); f.semis = (float) juce::jlimit (-24.0, 24.0, fe->getDoubleAttribute ("semis"));
    }
}

inline void writeMidi (juce::XmlElement& e, const nb::MidiSettings& m)
{
    e.setAttribute ("mode", m.mode); e.setAttribute ("lengthSteps", (double) m.lengthSteps); e.setAttribute ("rolls", m.rolls ? 1 : 0); e.setAttribute ("transpose", m.transpose ? 1 : 0);
    juce::String n, c; for (int i = 0; i < nb::kNumPads; ++i) { n << (int) m.note[(size_t) i] << " "; c << (int) m.channel[(size_t) i] << " "; }
    e.setAttribute ("notes", n.trim()); e.setAttribute ("channels", c.trim());
}
inline void readMidi (const juce::XmlElement& e, nb::MidiSettings& m)
{
    m = {};
    m.mode = juce::jlimit (0, 2, e.getIntAttribute ("mode")); m.lengthSteps = (float) juce::jlimit (0.05, 16.0, e.getDoubleAttribute ("lengthSteps", 0.5));
    m.rolls = e.getIntAttribute ("rolls", 1) != 0; m.transpose = e.getIntAttribute ("transpose") != 0;
    const auto n = juce::StringArray::fromTokens (e.getStringAttribute ("notes"), " ", ""), c = juce::StringArray::fromTokens (e.getStringAttribute ("channels"), " ", "");
    for (int i = 0; i < nb::kNumPads; ++i)
    {
        if (i < n.size()) m.note[(size_t) i] = (uint8_t) juce::jlimit (0, 127, n[i].getIntValue());
        if (i < c.size()) m.channel[(size_t) i] = (uint8_t) juce::jlimit (1, 16, c[i].getIntValue());
    }
}
inline void writeMod (juce::XmlElement& e, const nb::ModState& m0)
{
    nb::ModState m = m0;
    Writer w { e };
    visit (m.lfo1, w, "lfo1_"); visit (m.lfo2, w, "lfo2_");
    w ("envA", m.envA); w ("envD", m.envD); w ("randBeats", m.randBeats); int rs = (int) m.randScope; w ("randScope", rs); w ("xyX", m.xyX); w ("xyY", m.xyY);
    juce::String t; for (float x : m.table) t << juce::String (x, 4) << " ";
    e.setAttribute ("table", t.trim());
    for (const auto& r : m.routes)
    {
        auto* re = e.createNewChildElement ("ROUTE");
        re->setAttribute ("src", (int) r.src); re->setAttribute ("dest", juce::String (r.dest)); re->setAttribute ("depth", (double) r.depth);
    }
    for (int p = 0; p < nb::kNumPads; ++p)
    {
        const auto& a = m.algos[(size_t) p];
        if (a.type == 0) continue;
        auto* ae = e.createNewChildElement ("ALGO");
        ae->setAttribute ("pad", p); ae->setAttribute ("type", a.type); ae->setAttribute ("amount", (double) a.amount); ae->setAttribute ("mix", (double) a.mix);
        ae->setAttribute ("src", (int) a.src); ae->setAttribute ("srcDepth", (double) a.srcDepth);
    }
}
inline void readMod (const juce::XmlElement& e, nb::ModState& m)
{
    m = {};
    Reader r { e };
    visit (m.lfo1, r, "lfo1_"); visit (m.lfo2, r, "lfo2_");
    r ("envA", m.envA); r ("envD", m.envD); r ("randBeats", m.randBeats); int rs = (int) m.randScope; r ("randScope", rs); m.randScope = (nb::Scope) juce::jlimit (0, 2, rs);
    r ("xyX", m.xyX); r ("xyY", m.xyY);
    auto toks = juce::StringArray::fromTokens (e.getStringAttribute ("table"), " ", "");
    for (int i = 0; i < toks.size() && i < 64; ++i) m.table[(size_t) i] = juce::jlimit (-1.0f, 1.0f, toks[i].getFloatValue());
    for (auto* re : e.getChildWithTagNameIterator ("ROUTE"))
        if (m.routes.size() < 256)
            m.routes.push_back ({ (nb::ModSrc) juce::jlimit (0, (int) nb::ModSrc::Count - 1, re->getIntAttribute ("src")), re->getStringAttribute ("dest").toStdString(),
                                  (float) juce::jlimit (-1.0, 1.0, re->getDoubleAttribute ("depth", 0.3)) });
    for (auto* ae : e.getChildWithTagNameIterator ("ALGO"))
    {
        const int p = ae->getIntAttribute ("pad", -1);
        if (p < 0 || p >= nb::kNumPads) continue;
        auto& a = m.algos[(size_t) p];
        a.type = juce::jlimit (0, 5, ae->getIntAttribute ("type")); a.amount = (float) ae->getDoubleAttribute ("amount", 0.5); a.mix = (float) ae->getDoubleAttribute ("mix", 0.5);
        a.src = (nb::ModSrc) juce::jlimit (0, (int) nb::ModSrc::Count - 1, ae->getIntAttribute ("src")); a.srcDepth = (float) ae->getDoubleAttribute ("srcDepth", 0.0);
    }
}

// ---------------------------------------------------------------- the whole project
inline void writePadFx (juce::XmlElement& root, const nb::Project& P)
{
    auto* pads = root.createNewChildElement ("PADFX");
    for (int p = 0; p < nb::kNumPads; ++p)
    {
        auto* pe = pads->createNewChildElement ("PAD"); pe->setAttribute ("index", p);
        for (int i = 0; i < nb::kNumInserts; ++i) writeFx (*pe, "INSERT", P.padFx[(size_t) p].inserts[(size_t) i]);
    }
}
inline void writeWindows (juce::XmlElement& root, const nb::Project& P)
{
    auto* win = root.createNewChildElement ("WINDOWS");
    for (int s = 0; s < nb::kNumSlots; ++s)
    {
        const auto& f = P.fs[(size_t) s];
        auto* fe = win->createNewChildElement ("FS");
        fe->setAttribute ("slot", s); fe->setAttribute ("on", f.on ? 1 : 0); fe->setAttribute ("srcKind", (int) f.src.kind); fe->setAttribute ("srcIndex", f.src.index);
        fe->setAttribute ("level", (double) f.level); fe->setAttribute ("replace", f.replace ? 1 : 0);
        writeFx (*fe, "FX", nb::CellFx::friction (f.p));
        const auto& b = P.bd[(size_t) s];
        auto* be = win->createNewChildElement ("BD");
        be->setAttribute ("slot", s); be->setAttribute ("on", b.on ? 1 : 0); be->setAttribute ("srcKind", (int) b.src.kind); be->setAttribute ("srcIndex", b.src.index);
        be->setAttribute ("level", (double) b.level); be->setAttribute ("replace", b.replace ? 1 : 0);
        writeFx (*be, "FX", nb::CellFx::bouncing (b.p));
    }
}
inline void writeMatrix (juce::XmlElement& root, const nb::Project& P)
{
    auto* mx = root.createNewChildElement ("MATRIX");
    mx->setAttribute ("view", P.matrix.view); mx->setAttribute ("seqOn", P.matrix.seqOn ? 1 : 0); mx->setAttribute ("oneShotOn", P.matrix.oneShotOn ? 1 : 0);
    mx->setAttribute ("sampleOn", P.matrix.sampleOn ? 1 : 0); mx->setAttribute ("seqIndex", P.matrix.seqIndex); mx->setAttribute ("oneShotIndex", P.matrix.oneShotIndex);
    mx->setAttribute ("sampleIndex", P.matrix.sampleIndex);
    writeSeq (*mx->createNewChildElement ("SEQ"), P.matrix.seq);
    writeOneShot (*mx->createNewChildElement ("ONESHOT"), P.matrix.oneShot);
    writeSample (*mx->createNewChildElement ("SAMPLE"), P.matrix.sample);
}

inline std::unique_ptr<juce::XmlElement> toXml (const nb::Project& P)
{
    auto root = std::make_unique<juce::XmlElement> ("PROJECT");
    root->setAttribute ("version", 3);
    root->setAttribute ("glue", (double) P.glue);
    writeMidi (*root->createNewChildElement ("MIDI"), P.midi);
    writeSong (*root->createNewChildElement ("SONG"), P.song);
    writePadFx (*root, P);
    writeWindows (*root, P);
    writeKeyMap (*root->createNewChildElement ("KEYMAP"), P.keymap);
    writeMatrix (*root, P);
    writeGrid (*root->createNewChildElement ("GRID"), P.grid);
    writeMod (*root->createNewChildElement ("MOD"), P.mod);
    return root;
}

// only the sections that are in root (undo restores one section at a time)
inline void readInto (const juce::XmlElement& root, nb::Project& P)
{
    if (root.hasAttribute ("glue")) P.glue = (float) juce::jlimit (0.0, 1.0, root.getDoubleAttribute ("glue", 0.4));
    if (auto* pads = root.getChildByName ("PADFX"))
        for (auto* pe : pads->getChildWithTagNameIterator ("PAD"))
        {
            const int p = pe->getIntAttribute ("index", -1);
            if (p < 0 || p >= nb::kNumPads) continue;
            int i = 0;
            for (auto* ie : pe->getChildWithTagNameIterator ("INSERT")) if (i < nb::kNumInserts) P.padFx[(size_t) p].inserts[(size_t) i++] = readFx (ie);
        }
    if (auto* win = root.getChildByName ("WINDOWS"))
    {
        for (auto* fe : win->getChildWithTagNameIterator ("FS"))
        {
            const int s = fe->getIntAttribute ("slot", -1); if (s < 0 || s >= nb::kNumSlots) continue;
            auto& f = P.fs[(size_t) s];
            f.on = fe->getIntAttribute ("on") != 0; f.src.kind = (nb::SourceKind) juce::jlimit (0, 3, fe->getIntAttribute ("srcKind")); f.src.index = fe->getIntAttribute ("srcIndex");
            f.level = (float) fe->getDoubleAttribute ("level", 1.0); f.replace = fe->getIntAttribute ("replace", 1) != 0;
            const auto fx = readFx (fe->getChildByName ("FX")); if (fx.kind == nb::CellFx::FrictionStretchFx) f.p = fx.stretch;
        }
        for (auto* be : win->getChildWithTagNameIterator ("BD"))
        {
            const int s = be->getIntAttribute ("slot", -1); if (s < 0 || s >= nb::kNumSlots) continue;
            auto& b = P.bd[(size_t) s];
            b.on = be->getIntAttribute ("on") != 0; b.src.kind = (nb::SourceKind) juce::jlimit (0, 3, be->getIntAttribute ("srcKind")); b.src.index = be->getIntAttribute ("srcIndex");
            b.level = (float) be->getDoubleAttribute ("level", 0.7); b.replace = be->getIntAttribute ("replace", 0) != 0;
            const auto fx = readFx (be->getChildByName ("FX")); if (fx.kind == nb::CellFx::BounceDelayFx) b.p = fx.bounce;
        }
    }
    if (auto* k = root.getChildByName ("KEYMAP")) readKeyMap (*k, P.keymap);
    if (auto* mx = root.getChildByName ("MATRIX"))
    {
        P.matrix.view = juce::jlimit (0, 2, mx->getIntAttribute ("view", 1));
        P.matrix.seqOn = mx->getIntAttribute ("seqOn", 1) != 0; P.matrix.oneShotOn = mx->getIntAttribute ("oneShotOn", 1) != 0; P.matrix.sampleOn = mx->getIntAttribute ("sampleOn", 1) != 0;
        P.matrix.seqIndex = mx->getIntAttribute ("seqIndex"); P.matrix.oneShotIndex = mx->getIntAttribute ("oneShotIndex"); P.matrix.sampleIndex = mx->getIntAttribute ("sampleIndex");
        if (auto* e = mx->getChildByName ("SEQ")) readSeq (*e, P.matrix.seq);
        if (auto* e = mx->getChildByName ("ONESHOT")) readOneShot (*e, P.matrix.oneShot);
        if (auto* e = mx->getChildByName ("SAMPLE")) readSample (*e, P.matrix.sample);
    }
    if (auto* g = root.getChildByName ("GRID")) readGrid (*g, P.grid);
    if (auto* m = root.getChildByName ("MOD")) readMod (*m, P.mod);
    if (auto* m = root.getChildByName ("MIDI")) readMidi (*m, P.midi);
    if (auto* sg = root.getChildByName ("SONG")) readSong (*sg, P.song);
}
inline void fromXml (const juce::XmlElement& root, nb::Project& P)
{
    P = nb::Project();
    readInto (root, P);
}

} // namespace io
