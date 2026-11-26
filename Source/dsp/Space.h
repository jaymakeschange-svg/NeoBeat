// Liquid Swing 4 — a small space around each hit.
//
// The implementation used to live here. It now lives in the shared tree, as
// nsr::EarlyReflections in shared/StereoRoom.h, because every plugin in the family wants it and
// keeping a second copy here is how the two drift apart. This header is the alias that keeps
// `ls::EarlyReflections` meaning what it has always meant, so no call site had to change.
//
// The arithmetic in the shared class is bit-for-bit what was here before, deliberately, so no
// existing preset sounds any different for having moved.
//
// Anything genuinely specific to this plugin - the metal layer in Metal.h, the eight-place
// convolution busses in MaterialSpace.h - stays where it is. Shared means shared; plugin-specific
// effects stay with their plugin.
#pragma once

// Core.h is not needed by anything left in this file, but it was included here before and other
// headers in this tree reach it through this one. Kept so the include graph is unchanged.
#include "Core.h"
#include "../shared/StereoRoom.h"

namespace ls
{
using EarlyReflections = nsr::EarlyReflections;
} // namespace ls
