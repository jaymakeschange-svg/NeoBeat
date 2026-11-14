// Shared by NeoBeat and Batchcut — the one learner of this process, and its file
// (%APPDATA%/Marbelite/Shared/learner.txt). Both apps read it when they start and write it whenever you
// teach it something; what the other app taught it meanwhile is merged in, never overwritten.
#pragma once

#include "Learner.h"
#include "Njq.h"

namespace nbkstore
{
inline juce::CriticalSection& lock() { static juce::CriticalSection l; return l; }

inline nbk::Learner& learner()
{
    static nbk::Learner l;
    static bool loaded = false;
    const juce::ScopedLock sl (lock());
    if (! loaded) { loaded = true; l.load (shared::learnerFile().getFullPathName().toStdString()); }
    return l;
}

inline void save()
{
    auto& l = learner();
    const juce::ScopedLock sl (lock());
    const auto path = shared::learnerFile().getFullPathName().toStdString();
    nbk::Learner other;
    if (other.load (path)) l.merge (other);
    l.save (path);
}
} // namespace nbkstore
