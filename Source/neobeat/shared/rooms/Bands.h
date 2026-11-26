#pragma once

// BANDS: the spectral delay, standing in the room.
//
// The engine in SpectralDelay.h makes sixteen band signals and deliberately does not know where
// any of them is. This is the part that decides. Each band gets a Place of its own and goes out
// through its own Strip, which means a band can take any Travel the rest of the family can - it
// can sit still, sway, circle, drift - and it feeds the same shared room everything else does.
//
// Sixteen bands each walking their own path is the thing a broadband delay cannot do. A single
// stereo delay can only move the whole repeat; here the top of the spectrum can circle while the
// bottom stays locked in the middle, so a sound comes apart across the image as it repeats
// instead of travelling as one object.
//
// The two sixteens are not a coincidence worth relying on, so it is checked: a kit has sixteen
// lanes, the Stereoroom holds sixteen places, and the bank has sixteen bands.

#include "../SpectralDelay.h"
#include "../StereoRoom.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace bc
{

static_assert (nsr::SpectralDelay::kBands <= nsr::kMaxPlaces,
               "a band needs a place to stand in, and there are not enough of them");

struct BandsState
{
    static constexpr int kBands = nsr::SpectralDelay::kBands;

    nsr::SpectralDelayParams dsp;
    nsr::Place place[kBands];

    // Off, and every band goes out as the engine made it - which is still stereo, because the
    // interaural split happens inside the bank. Placement is an addition to that, not a
    // replacement for it.
    bool placed = true;

    // Lay the bands out by frequency, low in the middle and high toward the edges.
    //
    // Fanning them left to right in band order is the obvious default and the wrong one: it puts
    // the bass hard left, where it will fold the moment anything sums to mono. Low frequencies
    // carry almost no directional information anyway - the ear reads them from the room, not from
    // the image - so the low bands stay centred and narrow and the spread is spent on the top,
    // where it is audible.
    void fan (float amount = 1.0f, bool alternate = true)
    {
        for (int b = 0; b < kBands; ++b)
        {
            const float t = kBands > 1 ? (float) b / (float) (kBands - 1) : 0.0f;
            auto& pl = place[(size_t) b];
            pl.on = true;
            pl.travel = nsr::Travel::Still;
            pl.locked = t < 0.18f;                         // the bottom two bands stay mono
            pl.width = std::min (1.0f, 0.15f + t * 0.85f * amount);
            // x runs -1 to +1 with 0 in the middle, not 0 to 1. Centring on 0.5 puts the whole
            // bank three quarters of the way right, which reads as a mix that is simply off.
            const float reach = t * t * amount;            // squared: the fan opens late
            pl.x = alternate && (b & 1) ? -reach : reach;
            // y is 0 at the front of the stage and 1 at the back, so this sets the top of the
            // spectrum a little further off. Only a little: distance costs high frequencies, and
            // pushing the bright bands back is the one move that would take away what it is for.
            pl.y = std::min (1.0f, 0.18f + t * 0.28f * amount);
            pl.bars = 2 + (b % 3);
            pl.depth = 0.2f + t * 0.5f;
            pl.phase = t;
            pl.seed = 1 + b * 7;
        }
    }

    // Give every band the same movement, keeping the spread each already has. Phases are left
    // alone so they do not all swing together, which is the difference between a room breathing
    // and a chorus.
    void travelAll (nsr::Travel t, int bars = 4)
    {
        for (int b = 0; b < kBands; ++b)
        {
            place[(size_t) b].travel = t;
            place[(size_t) b].bars = std::max (1, bars + (b % 3) - 1);
        }
    }

    BandsState() { fan(); }
};

class BandsEngine
{
public:
    static constexpr int kBands = nsr::SpectralDelay::kBands;

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        block = std::max (1, maxBlock);
        bank.prepare (sampleRate, block);
        for (auto& s : strip) s.prepare (sampleRate, block);
    }

    void setState (const BandsState& s)
    {
        state = s;
        bank.setParams (s.dsp);
    }

    const BandsState& getState() const noexcept { return state; }

    // Where the placed bands' depth goes. Null is allowed and simply means no room: a band still
    // plays, it just has nothing behind it.
    void setSendBus (float* l, float* r) noexcept { sendL = l; sendR = r; }

    void reset()
    {
        bank.reset();
        for (auto& s : strip) s.prepare (sr, block);
    }

    // Adds into outL/outR. The input is read, never written, so a caller can hand the same
    // buffers it is accumulating into without the dry path eating its own tail.
    void process (float* outL, float* outR, const float* inL, const float* inR,
                  int n, double beats)
    {
        if (outL == nullptr || outR == nullptr || inL == nullptr || inR == nullptr || n <= 0)
            return;

        const float dry = state.dsp.dry, wet = state.dsp.wet;

        if (dry != 0.0f)
            for (int i = 0; i < n; ++i) { outL[i] += inL[i] * dry; outR[i] += inR[i] * dry; }

        if (wet == 0.0f) return;

        bank.processSplit (inL, inR, n);

        if (! state.placed)
        {
            for (int b = 0; b < kBands; ++b)
            {
                const float* bl = bank.bandOut (b, 0);
                const float* br = bank.bandOut (b, 1);
                if (bl == nullptr || br == nullptr) continue;
                for (int i = 0; i < n; ++i) { outL[i] += bl[i] * wet; outR[i] += br[i] * wet; }
            }
            return;
        }

        if ((int) mixL.size() < n) { mixL.assign ((size_t) n, 0.0f); mixR.assign ((size_t) n, 0.0f); }
        std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + n, 0.0f);

        if (sendL != nullptr)
        {
            if ((int) sendScratchL.size() < n)
            { sendScratchL.assign ((size_t) n, 0.0f); sendScratchR.assign ((size_t) n, 0.0f); }
            std::fill (sendScratchL.begin(), sendScratchL.begin() + n, 0.0f);
            std::fill (sendScratchR.begin(), sendScratchR.begin() + n, 0.0f);
        }

        for (int b = 0; b < kBands; ++b)
        {
            const auto& pl = state.place[(size_t) b];
            const float* bl = bank.bandOut (b, 0);
            const float* br = bank.bandOut (b, 1);
            if (! pl.on || bl == nullptr || br == nullptr) continue;

            float px = pl.x, py = pl.y;
            nsr::placeAt (pl, beats, px, py);
            strip[(size_t) b].setBlock (n);
            strip[(size_t) b].aim (nsr::placementFor (px, py, pl.width, pl.locked));
            strip[(size_t) b].process (bl, br, mixL.data(), mixR.data(),
                                       sendL != nullptr ? sendScratchL.data() : nullptr,
                                       sendL != nullptr ? sendScratchR.data() : nullptr,
                                       n);
        }

        for (int i = 0; i < n; ++i) { outL[i] += mixL[(size_t) i] * wet; outR[i] += mixR[(size_t) i] * wet; }

        // The send carries the same wet trim, so turning the band mix down takes its room with it
        // rather than leaving a tail hanging behind a signal that is no longer there.
        if (sendL != nullptr)
            for (int i = 0; i < n; ++i)
            { sendL[i] += sendScratchL[(size_t) i] * wet; sendR[i] += sendScratchR[(size_t) i] * wet; }
    }

    float bandHz (int b) const noexcept { return bank.bandHz (b); }

private:
    nsr::SpectralDelay bank;
    nsr::Strip strip[kBands];
    BandsState state;

    std::vector<float> mixL, mixR, sendScratchL, sendScratchR;
    float* sendL = nullptr;
    float* sendR = nullptr;
    double sr = 48000.0;
    int block = 512;
};

} // namespace bc
