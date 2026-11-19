// Batchcut's name for the shared Neosystem look. The drawing and the widgets now live in one place,
// Source/neobeat/shared/Ui.h, which NeoBeat and MidiJaQ draw with too; this header is what keeps every
// bcui:: call site in Batchcut working unchanged.
#pragma once

#include "../neobeat/shared/Ui.h"

namespace bcui = nsui;
