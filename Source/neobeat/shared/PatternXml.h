// Shared by NeoBeat and Batchcut — scenes and the chain as XML (the host state, user presets, .NJQ files).
#pragma once

#include <juce_core/juce_core.h>
#include "../engine/Sequencer.h"

namespace io
{
// a scene as text: per lane, the steps that hold a hit, as "step:word" (word: velocity, roll, nudge, in hex)
inline void writePattern (juce::XmlElement& e, const nb::Pattern& p)
{
    e.setAttribute ("length", p.length);
    e.setAttribute ("sparse", 1);
    for (int t = 0; t < nb::Pattern::kTracks; ++t)
    {
        juce::String row;
        for (int s = 0; s < nb::Pattern::kMaxSteps; ++s)
        {
            const auto& st = p.steps[(size_t) t][(size_t) s];
            if (st.vel == 0) continue;
            const uint32_t v = (uint32_t) st.vel | ((uint32_t) st.roll << 8) | ((uint32_t) (uint8_t) st.nudge << 16);
            row << s << ":" << juce::String::toHexString ((int) v) << " ";
        }
        if (row.isNotEmpty()) e.setAttribute ("t" + juce::String (t), row.trim());
    }
}
inline void readPattern (const juce::XmlElement& e, nb::Pattern& p)
{
    p = {};
    p.length = juce::jlimit (1, nb::Pattern::kMaxSteps, e.getIntAttribute ("length", 32));
    const bool sparse = e.getIntAttribute ("sparse") != 0;    // (2.0 wrote all 64 steps in a row)
    for (int t = 0; t < nb::Pattern::kTracks; ++t)
    {
        auto cells = juce::StringArray::fromTokens (e.getStringAttribute ("t" + juce::String (t)), " ", "");
        for (int i = 0; i < cells.size(); ++i)
        {
            const int s = sparse ? cells[i].upToFirstOccurrenceOf (":", false, false).getIntValue() : i;
            if (s < 0 || s >= nb::Pattern::kMaxSteps) continue;
            const uint32_t v = (uint32_t) (sparse ? cells[i].fromFirstOccurrenceOf (":", false, false) : cells[i]).getHexValue32();
            auto& st = p.steps[(size_t) t][(size_t) s];
            st.vel = (uint8_t) juce::jmin (127u, v & 0xffu); st.roll = (uint8_t) ((v >> 8) & 0xff); st.nudge = (int8_t) (uint8_t) ((v >> 16) & 0xff);
            if (st.roll != 0 && st.roll != 2 && st.roll != 3 && st.roll != 4 && st.roll != 5) st.roll = 0;
            st.nudge = (int8_t) juce::jlimit (-50, 50, (int) st.nudge);
        }
    }
}
inline void writeSong (juce::XmlElement& e, const nb::SongSettings& s)
{
    e.setAttribute ("chainOn", s.chainOn ? 1 : 0);
    juce::String t; for (int i = 0; i < s.numLinks; ++i) t << (int) s.chain[(size_t) i].scene << "x" << (int) s.chain[(size_t) i].repeats << " ";
    e.setAttribute ("chain", t.trim());
}
inline void readSong (const juce::XmlElement& e, nb::SongSettings& s)
{
    s = {};
    s.chainOn = e.getIntAttribute ("chainOn") != 0;
    for (const auto& tok : juce::StringArray::fromTokens (e.getStringAttribute ("chain"), " ", ""))
    {
        if (s.numLinks >= nb::SongSettings::kMaxLinks) break;
        s.chain[(size_t) s.numLinks++] = { (uint8_t) juce::jlimit (0, nb::kNumScenes - 1, tok.upToFirstOccurrenceOf ("x", false, false).getIntValue()),
                                           (uint8_t) juce::jlimit (1, 32, tok.fromFirstOccurrenceOf ("x", false, false).getIntValue()) };
    }
}
} // namespace io
