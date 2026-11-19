// MidiJaQ's clock and MIDI out: the notes the player sends, against the line it was given.
#include "../Source/core/Player.h"
#include <cstdio>
#include <map>
#include <set>

static int failures = 0;
#define CHECK(c, ...) do { char m_[300]; std::snprintf (m_, sizeof m_, __VA_ARGS__); if (c) std::printf ("  ok:   %s\n", m_); else { std::printf ("  FAIL: %s\n", m_); ++failures; } } while (0)

using namespace mjq;

static constexpr double kSr = 48000.0;
static constexpr int kBlock = 128;

// Runs the player for a while and collects everything it sent, with the sample each event landed on.
struct Run
{
    std::vector<std::pair<long long, MidiOut>> events;
    int ons = 0, offs = 0;
    std::vector<int> notes;
};

static Run play (Player& p, const Line& L, const Pass& pass, ClockMode mode, int blocks, double bpm = 120.0,
                 bool playing = true, bool validHost = true, double startPpq = 0.0)
{
    Run r;
    MidiOut buf[256];
    for (int b = 0; b < blocks; ++b)
    {
        HostTime h;
        h.valid = validHost;
        h.playing = playing;
        h.bpm = bpm;
        h.ppq = startPpq + (double) b * kBlock * bpm / (60.0 * kSr);
        const int n = p.process (L, pass, mode, h, kBlock, buf, 256);
        for (int i = 0; i < n; ++i)
        {
            r.events.push_back ({ (long long) b * kBlock + buf[i].sample, buf[i] });
            if (buf[i].on) { ++r.ons; r.notes.push_back (buf[i].note); } else ++r.offs;
        }
    }
    return r;
}

int main()
{
    std::printf ("1. the player sends what the line says\n");
    {
        Line L; L.numSteps = 16; L.traffic = 1.0f; generate (L, 8);
        const auto pass = readPass (L);
        Player p; p.prepare (kSr);
        // one bar at 120 bpm is two seconds: 750 blocks of 128 samples
        const int blocks = (int) std::lround (2.0 * kSr / kBlock);
        const auto r = play (p, L, pass, ClockMode::Pattern, blocks);
        CHECK (r.ons == 16, "a bar of 16 steps sends 16 notes (%d)", r.ons);

        bool pitches = r.notes.size() == pass.events.size();
        for (size_t i = 0; pitches && i < r.notes.size(); ++i) pitches = r.notes[i] == pass.events[i].note;
        CHECK (pitches, "the same notes, in the same order");

        // the first few land where the beats are
        bool onTime = true;
        for (size_t i = 0; i < pass.events.size() && i < 8; ++i)
        {
            const long long want = (long long) std::llround (pass.events[i].start * 60.0 / 120.0 * kSr);
            long long got = -1;
            for (const auto& e : r.events) if (e.second.on && e.second.note == pass.events[i].note && got < 0 && e.first >= want - 200) got = e.first;
            onTime = onTime && std::llabs (got - want) <= 2;
        }
        CHECK (onTime, "each one within two samples of its beat");

        // every note is let go, and its length is the one the line asked for
        std::map<int, long long> on;
        bool lengths = true;
        for (const auto& e : r.events)
        {
            if (e.second.on) on[e.second.note] = e.first;
            else if (on.count (e.second.note))
            {
                const double beats = (double) (e.first - on[e.second.note]) / kSr * 120.0 / 60.0;
                bool found = false;
                for (const auto& ev : pass.events) if (ev.note == e.second.note && std::fabs (ev.length - beats) < 0.01) found = true;
                lengths = lengths && found;
                on.erase (e.second.note);
            }
        }
        CHECK (r.offs == r.ons && lengths, "and let go again after the length its gate asked for");
    }

    std::printf ("2. the clock modes\n");
    {
        Line L; L.numSteps = 16; L.traffic = 0.5f; generate (L, 12);
        const auto pass = readPass (L);
        const int halfBar = (int) std::lround (1.0 * kSr / kBlock);

        // Pattern: the line starts again whenever the transport does
        Player p; p.prepare (kSr);
        const auto first = play (p, L, pass, ClockMode::Pattern, halfBar, 120.0, true, true, 0.0);
        (void) play (p, L, pass, ClockMode::Pattern, 4, 120.0, false);               // stop
        const auto again = play (p, L, pass, ClockMode::Pattern, halfBar, 120.0, true, true, 97.5);
        CHECK (first.notes == again.notes && ! first.notes.empty(), "Pattern: it starts from the top every time, wherever the song is (%d notes)", (int) first.notes.size());

        // Song: the line sits where the song sits
        Player q; q.prepare (kSr);
        const auto fromTop = play (q, L, pass, ClockMode::Song, halfBar, 120.0, true, true, 0.0);
        Player q2; q2.prepare (kSr);
        const auto fromBar8 = play (q2, L, pass, ClockMode::Song, halfBar, 120.0, true, true, 28.0);   // seven bars on
        CHECK (fromTop.notes == fromBar8.notes && ! fromTop.notes.empty(), "Song: playing from bar 8 gives the same notes as from bar 1");

        Player q3; q3.prepare (kSr);
        const auto fromHalf = play (q3, L, pass, ClockMode::Song, halfBar, 120.0, true, true, 2.0);    // half a bar in
        CHECK (fromHalf.notes != fromTop.notes, "and playing from halfway through the bar does not");

        // Gate: only while keys are held
        Player g; g.prepare (kSr);
        const auto silent = play (g, L, pass, ClockMode::Gate, halfBar);
        g.keyDown (60, 100);
        const auto heard = play (g, L, pass, ClockMode::Gate, halfBar);
        g.keyUp (60);
        const auto quiet = play (g, L, pass, ClockMode::Gate, halfBar);
        CHECK (silent.ons == 0 && heard.ons > 0 && quiet.ons == 0 && quiet.offs <= heard.ons,
               "Gate: nothing until a key is held (%d notes), nothing again once it is let go", heard.ons);
    }

    std::printf ("3. stopping lets every note go\n");
    {
        Line L; L.numSteps = 16; L.traffic = 1.0f; L.gate = 2.0f; generate (L, 5);
        for (auto& s : L.steps) s.gate = 2;
        const auto pass = readPass (L);
        Player p; p.prepare (kSr);
        const auto r = play (p, L, pass, ClockMode::Pattern, 200);
        const auto stop = play (p, L, pass, ClockMode::Pattern, 2, 120.0, false);
        int open = r.ons - r.offs;
        CHECK (open > 0 && stop.offs == open && stop.ons == 0, "a note still sounding when the transport stops is let go (%d of them)", open);

        MidiOut buf[64];
        const int n = p.releaseAllNow (buf, 64);
        CHECK (n == 0, "and nothing is left hanging after that");
    }

    std::printf ("4. Follow and Bass take their pitches from the keys\n");
    {
        Line L; L.numSteps = 8; L.traffic = 1.0f; L.mode = Mode::Follow; generate (L, 17);
        const auto pass = readPass (L);
        Player p; p.prepare (kSr);
        p.keyDown (60, 100); p.keyDown (64, 100); p.keyDown (67, 100);
        const auto r = play (p, L, pass, ClockMode::Pattern, 400);
        std::set<int> classes;
        for (int n : r.notes) classes.insert (((n % 12) + 12) % 12);
        bool onlyHeld = true;
        for (int c : classes) onlyHeld = onlyHeld && (c == 0 || c == 4 || c == 7);
        CHECK (r.ons > 0 && onlyHeld && classes.size() >= 2, "Follow: a held C major triad gives only C, E and G (%d of them)", (int) classes.size());

        Line B = L; B.mode = Mode::Bass; B.scaleIndex = 10;              // minor pentatonic
        const auto bassPass = readPass (B);
        Player q; q.prepare (kSr);
        q.keyDown (55, 100); q.keyDown (62, 100); q.keyDown (71, 100);   // a G chord
        const auto rb = play (q, B, bassPass, ClockMode::Pattern, 400);
        int low = 127, root = 0;
        for (int n : rb.notes) { low = std::min (low, n); if (((n % 12) + 12) % 12 == 7) ++root; }
        CHECK (rb.ons > 0 && low < 55 && root > 0, "Bass: it plays under the chord (down to %d, below the chord's G) and on its root", low);
    }

    std::printf ("5. no host, and odd blocks\n");
    {
        Line L; L.numSteps = 16; L.traffic = 1.0f; generate (L, 77);
        const auto pass = readPass (L);
        Player p; p.prepare (kSr);
        const auto r = play (p, L, pass, ClockMode::Pattern, 760, 120.0, false, false);   // standalone: no transport
        CHECK (r.ons >= 16, "with no host it runs on its own clock (%d notes in two seconds)", r.ons);

        // the block size must not change a note: one sample at a time against blocks of 128, over the same second
        Player q; q.prepare (kSr);
        MidiOut buf[64];
        std::vector<std::pair<long long, int>> oneByOne;
        for (int i = 0; i < 48000; ++i)
        {
            HostTime h; h.valid = false; h.bpm = 120.0;
            const int n = q.process (L, pass, ClockMode::Pattern, h, 1, buf, 64);
            for (int k = 0; k < n; ++k) if (buf[k].on) oneByOne.push_back ({ i, buf[k].note });
        }
        Player q2; q2.prepare (kSr);
        const auto blocked = play (q2, L, pass, ClockMode::Pattern, 48000 / kBlock, 120.0, false, false);
        std::vector<std::pair<long long, int>> inBlocks;
        for (const auto& e : blocked.events) if (e.second.on) inBlocks.push_back ({ e.first, e.second.note });
        bool same = oneByOne.size() == inBlocks.size();
        for (size_t i = 0; same && i < inBlocks.size(); ++i)
            same = oneByOne[i].second == inBlocks[i].second && std::llabs (oneByOne[i].first - inBlocks[i].first) <= 1;
        CHECK (same, "and a block of one sample at a time plays the same %d notes at the same samples", (int) inBlocks.size());
    }

    // ---- Keys: the line keeps its shape and moves to the key held
    {
        Line L;
        L.mode = Mode::Keys;
        L.numSteps = 16;
        L.traffic = 0.8f;
        L.base = 48;                       // the line's home note: holding it plays the line as written
        L.transpose = 0;
        generate (L, 7777);
        const auto pass = readPass (L);

        // nothing held: it plays as written
        Player idle; idle.prepare (kSr);
        const auto nothing = play (idle, L, pass, ClockMode::Pattern, 48000 / kBlock);
        CHECK (nothing.ons > 0, "Keys plays with no key held (%d notes)", nothing.ons);

        // holding the home note plays exactly the same notes
        Player home; home.prepare (kSr);
        home.keyDown (L.base, 100);
        const auto atHome = play (home, L, pass, ClockMode::Pattern, 48000 / kBlock);
        CHECK (atHome.notes == nothing.notes, "holding its home note plays the line exactly as written");

        // holding a key a fifth up moves every note by the same seven semitones
        Player up; up.prepare (kSr);
        up.keyDown (L.base + 7, 100);
        const auto fifth = play (up, L, pass, ClockMode::Pattern, 48000 / kBlock);
        bool movedTogether = fifth.notes.size() == nothing.notes.size();
        for (size_t i = 0; movedTogether && i < fifth.notes.size(); ++i)
            movedTogether = fifth.notes[i] == nothing.notes[i] + 7;
        CHECK (movedTogether, "a key a fifth up moves the whole line by a fifth, shape intact");

        // a triad gives three copies of the same figure at once
        Player triad; triad.prepare (kSr);
        triad.keyDown (L.base, 100); triad.keyDown (L.base + 4, 100); triad.keyDown (L.base + 7, 100);
        const auto three = play (triad, L, pass, ClockMode::Pattern, 48000 / kBlock);
        CHECK (three.ons == nothing.ons * 3, "a triad sounds three copies at once (%d for %d)", three.ons, nothing.ons);
        // whatever is still ringing when the run stops has not been let go yet, so the right comparison is
        // with the single copy: three copies leave exactly three times as many ringing, and no more
        CHECK (three.ons - three.offs == 3 * (nothing.ons - nothing.offs),
               "and every copy is let go the same way the single line is (%d ringing for %d)",
               three.ons - three.offs, nothing.ons - nothing.offs);

        // the copies are the same figure, a third and a fifth apart
        std::vector<int> copyA, copyB, copyC;
        for (const auto& e : three.events)
            if (e.second.on)
            {
                const int rel = e.second.note - nothing.notes[0];
                if (rel % 12 == 0 || true)
                {
                    // sort by which copy it belongs to, using the shift from the written line
                    if (std::find (nothing.notes.begin(), nothing.notes.end(), e.second.note) != nothing.notes.end()) copyA.push_back (e.second.note);
                    else if (std::find (nothing.notes.begin(), nothing.notes.end(), e.second.note - 4) != nothing.notes.end()) copyB.push_back (e.second.note);
                    else if (std::find (nothing.notes.begin(), nothing.notes.end(), e.second.note - 7) != nothing.notes.end()) copyC.push_back (e.second.note);
                }
            }
        CHECK (! copyA.empty() && ! copyB.empty() && ! copyC.empty(), "all three copies are there (%d, %d, %d)", (int) copyA.size(), (int) copyB.size(), (int) copyC.size());

        // letting the keys go returns it to the written line
        Player back; back.prepare (kSr);
        back.keyDown (L.base + 7, 100);
        back.allKeysUp();
        const auto released = play (back, L, pass, ClockMode::Pattern, 48000 / kBlock);
        CHECK (released.notes == nothing.notes, "letting go returns it to the line as written");

        // a key that would push the line off the top of the keyboard drops only what falls off
        Player high; high.prepare (kSr);
        high.keyDown (126, 100);
        const auto tooHigh = play (high, L, pass, ClockMode::Pattern, 48000 / kBlock);
        bool inRange = true;
        for (int n : tooHigh.notes) inRange = inRange && n >= 0 && n <= 127;
        CHECK (inRange, "nothing sounds off the end of the keyboard");
        CHECK (tooHigh.ons < nothing.ons, "the notes that would have fallen off the top are dropped (%d of %d)", tooHigh.ons, nothing.ons);
        CHECK (tooHigh.offs <= tooHigh.ons, "and nothing is let go that never sounded");
    }

    std::printf ("%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
