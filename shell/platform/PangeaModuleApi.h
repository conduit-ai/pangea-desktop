// The C entry points of a Pangea module: one version of a project, compiled on its own into a
// small library that the Pangea plugin loads while the DAW runs (docs/pangea-plugin.md).
//
// A module holds the Faust class and, if the project has one, the logic module, compiled as it is
// into a plugin (its own unit, the step budget's flags) with its own guard (PangeaLogicGuard.h,
// decision 46): every call into the logic module's code goes through a function here that runs it
// inside that guard, so a call that runs too long or too deep is stopped inside the module, as in a
// built plugin. An instrument whose code defines `effect`, the stage its voices share, carries that
// effect section as a second Faust class (decision 73). Everything else in a module is hidden, so
// two versions of one project can be loaded side by side while one replaces the other.
//
// Both sides are built from the same Faust headers and this repository's logic_api.h; the dsp the
// module makes is used through Faust's dsp class. PANGEA_MODULE_ABI changes whenever anything here
// or in those headers changes in a way an older module would not match; the plugin refuses a module
// with another number and asks the desktop app to build it again.
//
// 1: the first modules. 2: pangea_create_effect, an instrument's effect section (decision 73).
#pragma once

#include "logic_api.h"

#define PANGEA_MODULE_ABI 2

extern "C"
{
/** PANGEA_MODULE_ABI, as the module was built. */
typedef int (*PangeaModuleAbiFn) (void);
/** A new instance of the project's Faust class, as a `dsp*`; the caller deletes it. */
typedef void* (*PangeaCreateDspFn) (void);
/** A new instance of an instrument's effect section, as a `dsp*` that takes what the Faust class
    gives (the voices' mix), or null when the project has none; the caller deletes it. */
typedef void* (*PangeaCreateEffectFn) (void);

/** 1 when the module carries a logic module; the functions below exist only then. */
typedef int (*PangeaLogicPresentFn) (void);
typedef int (*PangeaLogicNumParamsFn) (void);
typedef const LogicParam* (*PangeaLogicParamsFn) (void);
typedef int (*PangeaLogicNumControlsFn) (void);
typedef const char* (*PangeaLogicControlNameFn) (int index);

/** Sets up what the module's guards share; call off the audio thread. 1 when it worked. */
typedef int (*PangeaLogicSetupFn) (void);
/** A guard of the module's own, for one running module; freed with the next function. */
typedef void* (*PangeaLogicGuardNewFn) (void);
typedef void (*PangeaLogicGuardFreeFn) (void* guard);

// The calls into the module's code. Each runs inside `guard` and returns why it was stopped:
// 0 not stopped, 1 too long, 2 too deep, 3 the budget could not be set up (pangea::logic::Stop).
// A module that was stopped is never called again.
typedef int (*PangeaLogicMakeFn) (void* guard, double sampleRate, void** moduleOut);
typedef int (*PangeaLogicUnmakeFn) (void* guard, void* module);
typedef int (*PangeaLogicProcessFn) (void* guard, void* module, const LogicBlock* block,
                                     LogicEvent* out, int maxEvents, int* countOut);
typedef long long (*PangeaLogicStepsUsedFn) (void* guard);
}
