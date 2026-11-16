// The no-repeat dealer: how RANDOM chooses. Every card in the deck is dealt once, in a shuffled order,
// before any card comes round again, the way Tetris deals all seven pieces before it repeats one. Nothing
// is ranked: a sound you have rated five stars is as likely as one you have never heard.
//
//   reset     a new deck (and a new round)
//   add       cards that join the round under way, shuffled in among those still to come
//   deal      the next card a test accepts; cards it turns down (a sound already on another lane) wait
//             for later in the round. When every card left is turned down, those cards are in use, so
//             heard: the round ends there.
//
// Framework-free; shared by Batchcut's Kit room and LOOPS, and by MidiJaQ.
#pragma once

#include "Rng.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gen
{
class Dealer
{
public:
    void reset (std::vector<int> deck, uint32_t seed)
    {
        cards = std::move (deck);
        std::sort (cards.begin(), cards.end());
        cards.erase (std::unique (cards.begin(), cards.end()), cards.end());
        rng.reseed (seed);
        toCome.clear(); last = -1; rounds = 0;
    }

    void add (const std::vector<int>& fresh)
    {
        for (int c : fresh)
        {
            if (contains (c)) continue;
            cards.insert (std::upper_bound (cards.begin(), cards.end(), c), c);
            // somewhere among the cards still to come (or the start of the next round, if none are left)
            if (! toCome.empty())
            {
                toCome.insert (toCome.begin() + (std::ptrdiff_t) rng.below ((uint32_t) toCome.size() + 1u), c);
            }
        }
    }

    // -1: nothing in the deck is accepted
    template <typename Accept>
    int deal (Accept&& ok)
    {
        if (cards.empty()) return -1;
        if (toCome.empty()) newRound();
        for (size_t k = toCome.size(); k-- > 0;)
        {
            const int c = toCome[k];
            if (! ok (c)) continue;
            toCome.erase (toCome.begin() + (std::ptrdiff_t) k);
            last = c;
            return c;
        }
        // Every card left in this round is turned down: each is in use elsewhere (on another lane), so it is
        // being heard already. The round is over and the next one starts.
        newRound();
        for (size_t k = toCome.size(); k-- > 0;)
        {
            const int c = toCome[k];
            if (! ok (c)) continue;
            toCome.erase (toCome.begin() + (std::ptrdiff_t) k);
            last = c;
            return c;
        }
        toCome.clear();
        return -1;
    }
    int deal() { return deal ([] (int) { return true; }); }

    bool contains (int c) const { return std::binary_search (cards.begin(), cards.end(), c); }
    int size() const noexcept { return (int) cards.size(); }
    // still to come in this round (0 once a round is dealt out: the next card opens a new round)
    int remaining() const noexcept { return rounds == 0 ? (int) cards.size() : (int) toCome.size(); }
    int round() const noexcept { return rounds; }
    const std::vector<int>& deck() const noexcept { return cards; }

private:
    void newRound()
    {
        toCome = cards;
        rng.shuffle (toCome);
        // the card dealt last does not open the next round as well
        if (toCome.size() > 1 && toCome.back() == last) std::swap (toCome.back(), toCome.front());
        ++rounds;
    }

    std::vector<int> cards, toCome;   // toCome is dealt from the back
    Rng rng { 1u };
    int last = -1, rounds = 0;
};
} // namespace gen
