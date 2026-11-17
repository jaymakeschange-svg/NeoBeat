// Generator memory is repeatable, but changes from round to round when CHANGE permits it.
// The plugin prepares these variants outside the audio callback and reads them by index.
#include "../Source/core/Session.h"
#ifdef NDEBUG
#undef NDEBUG // keep the test checks active in Release builds
#endif
#include <cassert>
#include <cstdio>

static bool samePass(const mjq::Pass& a, const mjq::Pass& b)
{
    if (a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.events.size(); ++i)
    {
        if (a.events[i].step != b.events[i].step || a.events[i].note != b.events[i].note ||
            a.events[i].start != b.events[i].start) return false;
    }
    return true;
}

int main()
{
    mjq::Track tr;
    mjq::generate(tr.line, 329);
    tr.line.numSteps = 16;
    tr.line.traffic = 1.0f;
    tr.generator.seed = 414;
    tr.generator.kind = mjq::Generator::DRIFT;
    tr.generator.amount = 0.0f;
    bool differs = false;
    for (uint64_t r = 0; r < 16; ++r)
    {
        assert(samePass(mjq::generatedPass(tr,r),mjq::generatedPass(tr,r)));
        differs |= !samePass(mjq::generatedPass(tr,r),mjq::generatedPass(tr,r+1));
    }
    assert(differs);
    std::puts("DRIFT advances by round, using the same seed reproducibly.");

    tr.generator.amount = 1.0f;
    assert(samePass(mjq::generatedPass(tr,0),mjq::generatedPass(tr,1)));
    std::puts("At full DRIFT memory, the line remains locked.");

    tr.generator.kind = mjq::Generator::HABIT;
    tr.generator.amount = 0.5f;
    differs = false;
    for (uint64_t r=0;r<15;++r)
        differs |= !samePass(mjq::generatedPass(tr,r),mjq::generatedPass(tr,r+1));
    assert(differs);
    std::puts("HABIT mutates its rhythm between rounds.");

    tr.generator.amount = 0.0f;
    assert(samePass(mjq::generatedPass(tr,0),mjq::generatedPass(tr,1)));
    std::puts("At zero HABIT change, the shift-register rhythm repeats.");
    assert(samePass(mjq::generatedPass(tr,1),mjq::generatedPass(tr,17)));
    std::puts("Round memory stays inside a deterministic sixteen-round cycle.");
}
