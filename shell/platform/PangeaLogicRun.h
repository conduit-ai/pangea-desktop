// The way into a logic module from the native shell (docs/midi-tools.md, "The step budget";
// decision 46). The module is compiled on its own, in platform/logic_native.cpp, with the
// flags in platform/logic_native.flags, so that only its code counts steps. The shell sees
// these functions and nothing of the module's own types.
//
// make, unmake, prepare and process run the module's code. Call them only inside a
// pangea::logic::Guard (PangeaLogicGuard.h), which stops a call that runs too long or too
// deep. The parameters and the control names are data and may be read anywhere.
#pragma once

#include "logic_api.h"

namespace pangea::logic
{

/** One module: a value-initialised Logic, as `Logic()` makes it. Opaque to the shell. */
struct Module;

int numParams();
const LogicParam* params();
int numControls();
const char* controlName (int index);

/** A new module (the module's constructor runs). */
Module* make();
/** Ends a module (its destructor runs). */
void unmake (Module* module);
void prepare (Module& module, double sampleRate);
int process (Module& module, const LogicBlock& block, LogicEvent* out, int maxEvents);

} // namespace pangea::logic
