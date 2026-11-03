// Liquid Swing 4 — plain-text patch format.
//
//   # comment
//   hardness = 0.9
//   g0.kind = harmonic        (harmonic | cluster | explicit)
//   g0.ratios = 1 1.39 1.85   (explicit ratios, up to 16)
//
// Unknown keys are reported, never silently ignored, so a typo can't hide.
#pragma once

#include "Voice.h"
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ls
{
inline std::string trim (const std::string& s)
{
    const auto a = s.find_first_not_of (" \t\r\n");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of (" \t\r\n");
    return s.substr (a, b - a + 1);
}

// Returns the list of problems (empty when the text parsed cleanly).
inline std::vector<std::string> parsePatchText (const std::string& text, Patch& p)
{
    std::vector<std::string> problems;
    using Setter = std::function<void (const std::string&)>;
    std::map<std::string, Setter> table;
    auto num = [] (float& f) { return Setter ([&f] (const std::string& v) { f = (float) std::atof (v.c_str()); }); };
    auto integer = [] (int& i) { return Setter ([&i] (const std::string& v) { i = std::atoi (v.c_str()); }); };

    table["numGroups"] = integer (p.numGroups);
    table["lowMonoHz"] = num (p.lowMonoHz);
    table["hardness"] = num (p.hardness);
    table["noiseExcite"] = num (p.noiseExcite);
    table["noiseExciteT60"] = num (p.noiseExciteT60);
    table["haloAmount"] = num (p.haloAmount);
    table["haloT60"] = num (p.haloT60);
    table["rebound"] = num (p.rebound);
    table["reboundMs"] = num (p.reboundMs);
    table["airHz"] = num (p.airHz);
    table["bounces"] = integer (p.bounces);
    table["restitution"] = num (p.restitution);
    table["bobSemis"] = num (p.bobSemis);
    table["bobHz"] = num (p.bobHz);
    table["bobTime"] = num (p.bobTime);
    table["bobLevelDb"] = num (p.bobLevelDb);
    table["bobLiftDb"] = num (p.bobLiftDb);
    table["liftDb"] = num (p.liftDb);
    table["roomLevel"] = num (p.roomLevel);
    table["roomSize"] = num (p.roomSize);
    table["roomDampHz"] = num (p.roomDampHz);
    table["roomLowCutHz"] = num (p.roomLowCutHz);
    table["clickLevel"] = num (p.clickLevel);
    table["clickHz"] = num (p.clickHz);
    table["clickQ"] = num (p.clickQ);
    table["clickT60"] = num (p.clickT60);
    table["subLevel"] = num (p.subLevel);
    table["subHz"] = num (p.subHz);
    table["subT60"] = num (p.subT60);
    table["subDropSemis"] = num (p.subDropSemis);
    table["subDropTime"] = num (p.subDropTime);
    table["subGrowl"] = num (p.subGrowl);
    table["subGrowlTilt"] = num (p.subGrowlTilt);
    table["subGrowlT60"] = num (p.subGrowlT60);
    table["fizzLevel"] = num (p.fizzLevel);
    table["fizzHz"] = num (p.fizzHz);
    table["fizzQ"] = num (p.fizzQ);
    table["fizzT60"] = num (p.fizzT60);
    table["fizzWidth"] = num (p.fizzWidth);
    table["fizzDelay"] = num (p.fizzDelay);
    table["fizzAttack"] = num (p.fizzAttack);
    table["fizzChopHz"] = num (p.fizzChopHz);
    table["fizzChopDuty"] = num (p.fizzChopDuty);
    table["amHz"] = num (p.amHz);
    table["amDepth"] = num (p.amDepth);
    table["sweepSemis"] = num (p.sweepSemis);
    table["sweepTime"] = num (p.sweepTime);
    table["sweepCurve"] = num (p.sweepCurve);
    table["driftSemis"] = num (p.driftSemis);
    table["driftTime"] = num (p.driftTime);
    table["flipSemis"] = num (p.flipSemis);
    table["flipTime"] = num (p.flipTime);
    table["flipSteps"] = integer (p.flipSteps);
    table["flipStepTime"] = num (p.flipStepTime);
    table["tensionSemis"] = num (p.tensionSemis);
    table["recoilSemis"] = num (p.recoilSemis);
    table["recoilHz"] = num (p.recoilHz);
    table["recoilTime"] = num (p.recoilTime);
    table["attack"] = num (p.attack);
    table["gate"] = num (p.gate);
    table["release"] = num (p.release);
    table["maxLength"] = num (p.maxLength);
    table["gainDb"] = num (p.gainDb);
    for (const char* k : { "velCurve", "velBright", "velPitch", "velDecay", "velNoise", "velAm", "velWidth", "velTension", "variation" })
    {
        float* f = std::string (k) == "velCurve" ? &p.velCurve : std::string (k) == "velBright" ? &p.velBright
                 : std::string (k) == "velPitch" ? &p.velPitch : std::string (k) == "velDecay" ? &p.velDecay
                 : std::string (k) == "velNoise" ? &p.velNoise : std::string (k) == "velAm" ? &p.velAm
                 : std::string (k) == "velWidth" ? &p.velWidth : std::string (k) == "velTension" ? &p.velTension : &p.variation;
        table[k] = num (*f);
    }

    // Round 5: coupling and the metal network
    table["couple.a"] = integer (p.couple.a);
    table["couple.b"] = integer (p.couple.b);
    table["couple.rateHz"] = num (p.couple.rateHz);
    table["couple.balance"] = num (p.couple.balance);
    table["couple.posA"] = num (p.couple.posA);
    table["couple.posB"] = num (p.couple.posB);
    table["couple.morphTime"] = num (p.couple.morphTime);
    table["couple.endRateHz"] = num (p.couple.endRateHz);
    table["couple.endBalance"] = num (p.couple.endBalance);
    table["couple.partnerDb"] = num (p.couple.partnerDb);
    table["metal.amount"] = num (p.metal.amount);
    table["metal.lines"] = integer (p.metal.lines);
    table["metal.baseHz"] = num (p.metal.baseHz);
    table["metal.spread"] = num (p.metal.spread);
    table["metal.t60"] = num (p.metal.t60);
    table["metal.brightHz"] = num (p.metal.brightHz);
    table["metal.strike"] = num (p.metal.strike);
    table["metal.noise"] = num (p.metal.noise);
    table["metal.noiseT60"] = num (p.metal.noiseT60);
    table["metal.feed"] = num (p.metal.feed);
    table["metal.follow"] = num (p.metal.follow);
    table["metal.modCents"] = num (p.metal.modCents);
    table["metal.modHz"] = num (p.metal.modHz);
    table["metal.width"] = num (p.metal.width);
    table["metal.duck"] = num (p.metal.duck);
    table["metal.invert"] = num (p.metal.invert);

    for (int gi = 0; gi < Patch::kMaxGroups; ++gi)
    {
        auto& g = p.groups[(size_t) gi];
        const std::string pre = "g" + std::to_string (gi) + ".";
        table[pre + "kind"] = [&g, &problems] (const std::string& v)
        {
            if (v == "harmonic") g.kind = GroupKind::Harmonic;
            else if (v == "cluster") g.kind = GroupKind::Cluster;
            else if (v == "explicit") g.kind = GroupKind::Explicit;
            else problems.push_back ("unknown group kind: " + v);
        };
        table[pre + "count"] = integer (g.count);
        table[pre + "firstHarmonic"] = integer (g.firstHarmonic);
        table[pre + "baseHz"] = num (g.baseHz);
        table[pre + "levelDb"] = num (g.levelDb);
        table[pre + "tilt"] = num (g.tiltDbPerOctave);
        table[pre + "evenGain"] = num (g.evenGain);
        table[pre + "stretch"] = num (g.stretch);
        table[pre + "spacingHz"] = num (g.spacingHz);
        table[pre + "spacingJitter"] = num (g.spacingJitter);
        table[pre + "formantHz"] = num (g.formantHz);
        table[pre + "formantDb"] = num (g.formantDb);
        table[pre + "formantOctaves"] = num (g.formantOctaves);
        table[pre + "t60"] = num (g.t60);
        table[pre + "decayTilt"] = num (g.decayTilt);
        table[pre + "ringHz"] = num (g.ringHz);
        table[pre + "ringOctaves"] = num (g.ringOctaves);
        table[pre + "ringBoost"] = num (g.ringBoost);
        table[pre + "width"] = num (g.width);
        table[pre + "phaseWidth"] = num (g.phaseWidth);
        table[pre + "pitchFollow"] = num (g.pitchFollow);
        table[pre + "pairHz"] = num (g.pairHz);
        table[pre + "pairLevel"] = num (g.pairLevel);
        table[pre + "strike"] = num (g.strike);
        table[pre + "noise"] = num (g.noise);
        table[pre + "bloom"] = num (g.bloom);
        table[pre + "bloomDelay"] = num (g.bloomDelay);
        table[pre + "vibratoSemis"] = num (g.vibratoSemis);
        table[pre + "vibratoHz"] = num (g.vibratoHz);
        table[pre + "jitterCents"] = num (g.jitterCents);
        table[pre + "jitterHz"] = num (g.jitterHz);
        table[pre + "phaseSpread"] = num (g.phaseSpread);
        table[pre + "pan"] = num (g.pan);
        table[pre + "itdMs"] = num (g.itdMs);
        table[pre + "spaceBloom"] = num (g.spaceBloom);
        table[pre + "spaceDrift"] = num (g.spaceDrift);
        table[pre + "spaceDriftHz"] = num (g.spaceDriftHz);
        table[pre + "flutterDb"] = num (g.flutterDb);
        table[pre + "flutterHz"] = num (g.flutterHz);
        table[pre + "am"] = num (g.am);
        table[pre + "peakFocus"] = num (g.peakFocus);
        table[pre + "peakSpan"] = num (g.peakSpan);
        table[pre + "peakFromHz"] = num (g.peakFromHz);
        table[pre + "halo"] = num (g.halo);
        table[pre + "strikePos"] = num (g.strikePos);
        table[pre + "pickupPos"] = num (g.pickupPos);
        table[pre + "positionDepth"] = num (g.positionDepth);
        table[pre + "buoyancy"] = num (g.buoyancy);
        table[pre + "disperseMs"] = num (g.disperseMs);
        table[pre + "lag"] = num (g.lag);
        table[pre + "fan"] = num (g.fan);
        table[pre + "ratios"] = [&g] (const std::string& v)
        {
            std::istringstream in (v);
            int n = 0;
            float x;
            while (n < 16 && in >> x) g.ratios[(size_t) n++] = x;
            g.numRatios = n;
        };
        table[pre + "ratioLevels"] = [&g] (const std::string& v)
        {
            std::istringstream in (v);
            int n = 0;
            float x;
            while (n < 16 && in >> x) g.ratioLevelsDb[(size_t) n++] = x;
        };
    }

    std::istringstream lines (text);
    std::string line;
    while (std::getline (lines, line))
    {
        const auto hash = line.find ('#');
        if (hash != std::string::npos) line = line.substr (0, hash);
        line = trim (line);
        if (line.empty()) continue;
        const auto eq = line.find ('=');
        if (eq == std::string::npos) { problems.push_back ("no '=' in: " + line); continue; }
        const std::string key = trim (line.substr (0, eq)), value = trim (line.substr (eq + 1));
        if (key == "name") continue;
        const auto it = table.find (key);
        if (it == table.end()) { problems.push_back ("unknown key: " + key); continue; }
        it->second (value);
    }
    return problems;
}
} // namespace ls
