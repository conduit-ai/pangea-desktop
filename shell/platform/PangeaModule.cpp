// One version of a project as a module for the Pangea plugin (PangeaModuleApi.h,
// docs/pangea-plugin.md). build-module.sh compiles this file with the project's FaustDsp.h and,
// when HAS_LOGIC is 1, links it with platform/logic_native.cpp compiled with the step budget's
// flags. This file is not instrumented: like HybridShell.cpp, it holds the guard and the callback
// that counts the module's steps (PangeaLogicGuard.h), so they belong to this module alone.
// When HAS_EFFECT is 1 the instrument has an effect section, and tools/build-effect.sh wrote its
// class, mydsp_effect, into FaustEffect.h beside FaustDsp.h (decision 73).
#if defined (__clang__)
 #pragma clang fp contract(off)
#endif
#include "logic_api.h"
#if defined (__clang__)
 #pragma clang fp contract(on)
#endif
#include "PangeaModuleApi.h"

#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <faust/dsp/dsp.h>
#include "FaustDsp.h"

#ifndef HAS_LOGIC
 #define HAS_LOGIC 0
#endif
#ifndef HAS_EFFECT
 #define HAS_EFFECT 0
#endif
#if HAS_EFFECT
 #include "FaustEffect.h"
#endif
#if HAS_LOGIC
 #include "PangeaLogicRun.h"
 #include "PangeaLogicGuard.h"
#endif

#define PANGEA_EXPORT extern "C" __attribute__((visibility ("default")))

PANGEA_EXPORT int pangea_module_abi (void) { return PANGEA_MODULE_ABI; }

PANGEA_EXPORT void* pangea_create_dsp (void) { return static_cast<dsp*> (new mydsp()); }

PANGEA_EXPORT void* pangea_create_effect (void)
{
   #if HAS_EFFECT
    return static_cast<dsp*> (new mydsp_effect());
   #else
    return nullptr;
   #endif
}

#if HAS_LOGIC

namespace
{
    pangea::logic::Guard& guardOf (void* g) { return *static_cast<pangea::logic::Guard*> (g); }
}

PANGEA_EXPORT int pangea_logic_present (void) { return 1; }
PANGEA_EXPORT int pangea_logic_num_params (void) { return pangea::logic::numParams(); }
PANGEA_EXPORT const LogicParam* pangea_logic_params (void) { return pangea::logic::params(); }
PANGEA_EXPORT int pangea_logic_num_controls (void) { return pangea::logic::numControls(); }
PANGEA_EXPORT const char* pangea_logic_control_name (int index) { return pangea::logic::controlName (index); }

PANGEA_EXPORT int pangea_logic_setup (void) { return pangea::logic::Guard::setUp() ? 1 : 0; }
PANGEA_EXPORT void* pangea_logic_guard_new (void) { return new pangea::logic::Guard(); }
PANGEA_EXPORT void pangea_logic_guard_free (void* guard) { delete static_cast<pangea::logic::Guard*> (guard); }

PANGEA_EXPORT int pangea_logic_make (void* guard, double sampleRate, void** moduleOut)
{
    pangea::logic::Module* made = nullptr;
    const auto why = guardOf (guard).run (pangea::logic::setupLimits, [&made, sampleRate]
    {
        made = pangea::logic::make();
        pangea::logic::prepare (*made, sampleRate);
    });
    // Stopped while it was made: left as it is and never touched again, as the plugin leaves it.
    *moduleOut = why == pangea::logic::Stop::none ? made : nullptr;
    return (int) why;
}

PANGEA_EXPORT int pangea_logic_unmake (void* guard, void* module)
{
    auto* m = static_cast<pangea::logic::Module*> (module);
    return (int) guardOf (guard).run (pangea::logic::setupLimits, [m] { pangea::logic::unmake (m); });
}

PANGEA_EXPORT int pangea_logic_process (void* guard, void* module, const LogicBlock* block,
                                        LogicEvent* out, int maxEvents, int* countOut)
{
    auto* m = static_cast<pangea::logic::Module*> (module);
    int count = 0;
    const auto why = guardOf (guard).run (pangea::logic::processLimits, [m, block, out, maxEvents, &count]
    {
        count = pangea::logic::process (*m, *block, out, maxEvents);
    });
    *countOut = why == pangea::logic::Stop::none ? count : 0;
    return (int) why;
}

PANGEA_EXPORT long long pangea_logic_steps_used (void* guard) { return guardOf (guard).stepsUsed(); }

#else

PANGEA_EXPORT int pangea_logic_present (void) { return 0; }

#endif
