// MidiJaQ's name for the shared Neosystem look. The drawing and the widgets now live in one place,
// Source/neobeat/shared/Ui.h, which Batchcut and NeoBeat draw with too; this header is what keeps every
// mjqui:: call site in MidiJaQ working unchanged.
#pragma once

#include "../neobeat/shared/Ui.h"

namespace mjqui = nsui;
