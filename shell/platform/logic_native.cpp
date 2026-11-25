// A logic module in the native plugin, compiled as its own translation unit with the flags in
// logic_native.flags (docs/midi-tools.md, "The step budget"; decision 46). It is the native
// twin of logic_wasm.cpp: the module, and the functions the shell reaches it through
// (PangeaLogicRun.h).
//
// Every edge of the code in this file counts one step against the budget of the Guard that
// is running it (PangeaLogicGuard.h), so nothing else belongs here: no system header, and
// nothing of the shell. A function here called with no guard running counts nothing.
#include "logic_api.h"
#include LOGIC_FILE
#include "PangeaLogicRun.h"

namespace pangea::logic
{

struct Module
{
    Logic logic;
};

int numParams() { return kNumLogicParams; }
const LogicParam* params() { return kLogicParams; }
int numControls() { return kNumLogicControls; }
const char* controlName (int index) { return (index >= 0 && index < kNumLogicControls) ? kLogicControls[index] : ""; }

// The global operator new and delete need no header. A module may not use them; the platform may.
Module* make() { return new Module(); }
void unmake (Module* module) { delete module; }
void prepare (Module& module, double sampleRate) { module.logic.prepare (sampleRate); }
int process (Module& module, const LogicBlock& block, LogicEvent* out, int maxEvents)
{
    return module.logic.process (block, out, maxEvents);
}

} // namespace pangea::logic
