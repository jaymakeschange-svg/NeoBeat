// MidiJaQ's sequencer core, without any framework: what JAQ writes, and what DIALECT and TRAFFIC do to it.
#include "../Source/core/Jaq.h"
#include <cstdio>
#include <map>
#include <set>

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

using namespace mjq;

static std::set<int> onSteps (const Line& L)
{
    std::set<int> s;
    for (int i = 0; i < L.numSteps; ++i) if (stepOn (L, i)) s.insert (i);
    return s;
}
static std::vector<int> pitches (const Line& L)
{
    std::vector<int> v;
    for (int i = 0; i < L.numSteps; ++i) v.push_back (noteOf (L, i));
    return v;
}

int main()
{
    std::printf ("1. JAQ writes a family, and writes it the same every time\n");
    {
        Line a, b, c;
        generate (a, 2026); generate (b, 2026); generate (c, 2027);
        bool same = true, differs = false;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const auto &x = a.steps[(size_t) i], &y = b.steps[(size_t) i], &z = c.steps[(size_t) i];
            same = same && x.weight == y.weight && x.freeDegree == y.freeDegree && x.acidDegree == y.acidDegree
                        && x.dialectAt == y.dialectAt && x.accent == y.accent && x.gate == y.gate && x.octave == y.octave;
            differs = differs || x.freeDegree != z.freeDegree || x.weight != z.weight;
        }
        CHECK (same && differs, "the same seed gives the same line, another seed another");
        // the numbers themselves, so a line saved on Windows rebuilds the same here
        Line k; generate (k, 12345);
        std::printf ("     seed 12345 -> weights %u %u %u, degrees %d %d %d\n", k.steps[0].weight, k.steps[1].weight, k.steps[2].weight,
                     k.steps[0].freeDegree, k.steps[1].freeDegree, k.steps[2].freeDegree);
    }

    std::printf ("2. the lanes are written apart\n");
    {
        Line L; generate (L, 7);
        const auto before = L.steps;
        L.locked[(size_t) Lane::Rhythm] = true;
        L.locked[(size_t) Lane::Accent] = true;
        L.locked[(size_t) Lane::Gate] = true;
        generate (L, 99);
        bool groove = true, notes = false;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            groove = groove && L.steps[(size_t) i].weight == before[(size_t) i].weight
                            && L.steps[(size_t) i].accent == before[(size_t) i].accent
                            && L.steps[(size_t) i].gate == before[(size_t) i].gate;
            notes = notes || L.steps[(size_t) i].freeDegree != before[(size_t) i].freeDegree;
        }
        CHECK (groove && notes, "locking Rhythm, Accent and Gate keeps the groove while the notes change");

        Line M; generate (M, 7);
        for (int i = 0; i < numLanes; ++i) M.locked[(size_t) i] = (i != (int) Lane::Rhythm);
        const auto deg = M.steps;
        generate (M, 1234);
        bool rhythmMoved = false, sameNotes = true;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            rhythmMoved = rhythmMoved || M.steps[(size_t) i].weight != deg[(size_t) i].weight;
            sameNotes = sameNotes && M.steps[(size_t) i].freeDegree == deg[(size_t) i].freeDegree;
        }
        CHECK (rhythmMoved && sameNotes, "and the other way round: a new groove under the same notes");
    }

    std::printf ("3. TRAFFIC: a busier line keeps every note of a sparser one\n");
    {
        Line L; L.numSteps = 16; generate (L, 31);
        std::set<int> last;
        bool nested = true, counts = true;
        int shownFour = -1;
        for (int k = 0; k <= 16; ++k)
        {
            L.traffic = (float) k / 16.0f;
            const auto on = onSteps (L);
            counts = counts && (int) on.size() == k;
            for (int s : last) nested = nested && on.count (s) > 0;
            if (k == 4) shownFour = (int) on.size();
            last = on;
        }
        CHECK (counts && nested, "0 to 16 of 16 steps, each adding to the one before (4 of them: %d)", shownFour);

        // strong beats are favoured, but not every line falls on the same steps
        int onBeat = 0, lines = 0;
        std::set<std::set<int>> shapes;
        for (uint32_t seed = 1; seed <= 40; ++seed)
        {
            Line T; T.numSteps = 16; T.traffic = 4.0f / 16.0f; generate (T, seed);
            const auto on = onSteps (T);
            shapes.insert (on);
            for (int s : on) if (s % 4 == 0) ++onBeat;
            ++lines;
        }
        CHECK (onBeat * 100 / (lines * 4) > 35 && shapes.size() > 30, "the heavy beats come in first (%d%% of the notes on a quarter, where chance is 25%%), yet %d of %d lines differ",
               onBeat * 100 / (lines * 4), (int) shapes.size(), lines);
    }

    std::printf ("4. DIALECT: the pitches move, nothing else\n");
    {
        Line L; L.numSteps = 16; L.traffic = 1.0f; L.scaleIndex = 10; generate (L, 55);   // minor pentatonic
        L.dialect = 0.0f;
        const auto freeNotes = pitches (L);
        const auto freeEvents = readLine (L);
        L.dialect = 1.0f;
        const auto acidNotes = pitches (L);
        const auto acidEvents = readLine (L);

        bool rhythmKept = freeEvents.size() == acidEvents.size();
        for (size_t i = 0; rhythmKept && i < freeEvents.size(); ++i)
            rhythmKept = freeEvents[i].start == acidEvents[i].start && freeEvents[i].velocity == acidEvents[i].velocity
                      && freeEvents[i].step == acidEvents[i].step;
        CHECK (rhythmKept, "the steps, the accents and the lengths are the same at either end of the knob");

        auto rootShare = [&] (const std::vector<int>& v)
        {
            int n = 0; for (int x : v) if (((x - L.root) % 12 + 12) % 12 == 0) ++n;
            return 100 * n / (int) v.size();
        };
        auto classes = [&] (const std::vector<int>& v)
        {
            std::set<int> pc; for (int x : v) pc.insert (((x - L.root) % 12 + 12) % 12);
            return (int) pc.size();
        };
        CHECK (rootShare (acidNotes) > rootShare (freeNotes) + 20 && classes (acidNotes) < classes (freeNotes),
               "acid sits on the root (%d%% of the notes against %d%%) and uses fewer of them (%d against %d)",
               rootShare (acidNotes), rootShare (freeNotes), classes (acidNotes), classes (freeNotes));

        // and it moves a note at a time, not all at once
        int changes = 0, steps = 0;
        auto prev = pitches (L);
        for (int k = 20; k >= 0; --k)
        {
            L.dialect = (float) k / 20.0f;
            const auto now = pitches (L);
            int moved = 0;
            for (size_t i = 0; i < now.size(); ++i) if (now[i] != prev[i]) ++moved;
            if (moved > 0) { ++steps; changes += moved; }
            prev = now;
        }
        CHECK (steps >= 5, "turning it moves a few notes at a time (%d notes over %d of 20 positions)", changes, steps);

        // every note is in the key
        L.dialect = 0.4f;
        bool inKey = true;
        const auto& sc = gen::scale (L.scaleIndex);
        for (int p : pitches (L)) inKey = inKey && sc.has (p - L.root);
        CHECK (inKey, "and every note is in the key");
    }

    std::printf ("5. accent: the three velocities, as the knob opens them\n");
    {
        Line L; generate (L, 3);
        for (int i = 0; i < kMaxSteps; ++i) L.steps[(size_t) i].accent = (uint8_t) (i % 3);
        L.accent = 0.0f;
        bool flat = true;
        for (int i = 0; i < 3; ++i) flat = flat && velocityOf (L, i) == 100;
        L.accent = 0.5f;
        const int lo50 = velocityOf (L, 0), mid50 = velocityOf (L, 1), hi50 = velocityOf (L, 2);
        L.accent = 1.0f;
        const int lo100 = velocityOf (L, 0), hi100 = velocityOf (L, 2);
        CHECK (flat && lo50 == 70 && mid50 == 100 && hi50 == 114 && lo100 == 40 && hi100 == 127,
               "all 100 at zero, then %d/%d/%d at half and %d/%d at full", lo50, mid50, hi50, lo100, hi100);
    }

    std::printf ("6. gate: three lengths, scaled; slides and ties\n");
    {
        Line L; L.numSteps = 4; L.traffic = 1.0f; L.perBar = 16; L.tie = false; generate (L, 11);
        for (int i = 0; i < 4; ++i) { L.steps[(size_t) i].gate = (uint8_t) std::min (2, i); L.steps[(size_t) i].slide = false; }
        auto lenOf = [&] (int step) { for (const auto& e : readLine (L)) if (e.step == step) return e.length; return -1.0; };
        const double step16 = 0.25;
        const bool three = std::fabs (lenOf (0) - 0.25 * step16) < 1e-9 && std::fabs (lenOf (1) - 5.0 / 9.0 * step16) < 1e-9
                        && std::fabs (lenOf (2) - 0.99 * step16) < 1e-9;
        L.gate = 0.25f;
        const bool quartered = std::fabs (lenOf (0) - 0.0625 * step16) < 1e-9;
        L.gate = 2.0f;
        const bool doubled = std::fabs (lenOf (1) - 10.0 / 9.0 * step16) < 1e-9;
        CHECK (three && quartered && doubled, "a quarter of a step, five ninths and the whole of it; GATE scales all three");

        L.gate = 1.0f; L.tie = true;
        L.steps[(size_t) 2].gate = 2; L.steps[(size_t) 2].slide = true;
        L.steps[(size_t) 2].freeDegree = 0; L.steps[(size_t) 3].freeDegree = 2; L.dialect = 0.0f;
        const double slid = lenOf (2);
        L.steps[(size_t) 3].freeDegree = 0;                       // the same note again
        L.steps[(size_t) 3].octave = L.steps[(size_t) 2].octave;
        const double tied = lenOf (2);
        CHECK (std::fabs (slid - 1.089 * step16) < 1e-9 && std::fabs (tied - 1.0 * step16) < 1e-9,
               "a slide leans 9%% into the next note; a tie into the same note holds exactly a step");
    }

    std::printf ("7. steps, offset, swing and direction\n");
    {
        Line L; L.numSteps = 16; L.traffic = 1.0f; generate (L, 21);
        CHECK (readLine (L).size() == 16 && std::fabs (loopBeats (L) - 4.0) < 1e-9, "16 steps make a bar of 16ths");
        L.numSteps = 7;
        const auto seven = readLine (L);
        bool firstSeven = seven.size() == 7;
        for (size_t i = 0; i < seven.size(); ++i) firstSeven = firstSeven && seven[i].step == (int) i;
        CHECK (firstSeven && std::fabs (loopBeats (L) - 1.75) < 1e-9, "7 steps play the first seven and loop in 1.75 beats");

        L.numSteps = 16;
        const auto straight = readLine (L);
        L.offset = 4;
        const auto turned = readLine (L);
        bool rotated = turned.size() == straight.size();
        for (size_t i = 0; rotated && i < turned.size(); ++i) rotated = turned[i].step == (int) ((i + 4) % 16);
        CHECK (rotated, "offset 4 turns the pattern four steps on");

        L.offset = 0; L.swing = 0.66f;
        const auto swung = readLine (L);
        CHECK (std::fabs (swung[1].start - 0.33) < 1e-6 && std::fabs (swung[4].start - 1.0) < 1e-6, "swing moves the off-beats and leaves the beats where they are");

        L.direction = Direction::Pendulum;
        const auto w = walk (L);
        CHECK (w.size() == 30 && w[0] == 0 && w[15] == 15 && w[16] == 14 && w.back() == 1, "the pendulum walks out and back without playing the ends twice");
    }

    std::printf ("8. the knobs leave the line itself alone\n");
    {
        Line L; generate (L, 404);
        const auto before = L.steps;
        for (float d = 0.0f; d <= 1.0f; d += 0.1f) { L.dialect = d; L.traffic = 1.0f - d; (void) readLine (L); }
        bool untouched = true;
        for (int i = 0; i < kMaxSteps; ++i)
            untouched = untouched && L.steps[(size_t) i].freeDegree == before[(size_t) i].freeDegree
                                  && L.steps[(size_t) i].weight == before[(size_t) i].weight;
        CHECK (untouched, "turning DIALECT and TRAFFIC reads the family, it does not rewrite it");
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
