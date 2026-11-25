// Platform wrapper that compiles a logic module to WebAssembly, for the server's check and the
// browser's test bench (docs/midi-tools.md, "WebAssembly"). Grown from the bench prototype in
// shell/bench-origin/platform/logic_wasm.cpp for version 2 of logic_api.h.
//
//   clang++ --target=wasm32 -std=c++17 -O2 -nostdlib -nostdinc -fno-exceptions -fno-rtti
//           -Werror=vla -Wframe-larger-than=16384 -Werror=frame-larger-than
//           -Werror=global-constructors -Werror=exit-time-destructors
//           -I shell/platform -DLOGIC_FILE='"logic.cpp"' shell/platform/logic_wasm.cpp
//           -Wl,--no-entry -Wl,--strip-all -Wl,--initial-memory=1048576 -Wl,--max-memory=4194304
//           -Wl,-z,stack-size=262144 -o logic.wasm
//
// No imports: the module can reach nothing outside its own memory. Everything it shares with
// the host is through the exports below; the layouts are fixed by static_asserts, because the
// JavaScript side (src/lib/logic/abi.ts) reads them by byte offset.
#include "logic_api.h"
#include LOGIC_FILE

extern "C" {
void* memcpy (void* d, const void* s, unsigned long n) { auto* a = (char*) d; auto* b = (const char*) s; while (n--) *a++ = *b++; return d; }
void* memset (void* d, int v, unsigned long n) { auto* a = (char*) d; while (n--) *a++ = (char) v; return d; }
void* memmove (void* d, const void* s, unsigned long n)
{
    auto* a = (char*) d; auto* b = (const char*) s;
    if (a < b) while (n--) *a++ = *b++;
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
}

static_assert (sizeof (LogicEvent) == 20, "LogicEvent is read as 5 x 4 bytes");
static_assert (sizeof (float) == 4 && sizeof (int) == 4, "wasm32");

static const int kMaxParams = 64;
static const int kMaxEvents = 1024;

// The module is made in place by prepare, never as a global: a global Logic could need code to
// run before anything else, which the refusals above do not allow (the native plugin makes it
// within the step budget instead, decision 46).
inline void* operator new (decltype (sizeof (0)), void* where) noexcept { return where; }
alignas (Logic) static unsigned char gStorage[sizeof (Logic)];
static Logic* gLogic = nullptr;
static float gParams[kMaxParams];
static LogicEvent gEvents[kMaxEvents];

#define EXPORT(name) extern "C" __attribute__((export_name(#name)))

static int clampedParams() { return kNumLogicParams < kMaxParams ? kNumLogicParams : kMaxParams; }
static const LogicParam* paramAt (int i) { return (i >= 0 && i < clampedParams()) ? &kLogicParams[i] : nullptr; }

EXPORT(api_version) int api_version() { return PANGEA_LOGIC_API; }
EXPORT(params_ptr) float* params_ptr() { return gParams; }
EXPORT(events_ptr) LogicEvent* events_ptr() { return gEvents; }
EXPORT(max_events) int max_events() { return kMaxEvents; }
EXPORT(event_size) int event_size() { return (int) sizeof (LogicEvent); }
EXPORT(num_params) int num_params() { return clampedParams(); }
EXPORT(param_name) const char* param_name (int i) { auto* p = paramAt (i); return p ? p->name : ""; }
EXPORT(param_min) float param_min (int i) { auto* p = paramAt (i); return p ? p->min : 0.0f; }
EXPORT(param_max) float param_max (int i) { auto* p = paramAt (i); return p ? p->max : 0.0f; }
EXPORT(param_init) float param_init (int i) { auto* p = paramAt (i); return p ? p->init : 0.0f; }
EXPORT(param_step) float param_step (int i) { auto* p = paramAt (i); return p ? p->step : 0.0f; }
EXPORT(param_meta) const char* param_meta (int i) { auto* p = paramAt (i); return (p && p->meta) ? p->meta : ""; }
EXPORT(num_controls) int num_controls() { return kNumLogicControls; }
EXPORT(control_name) const char* control_name (int i) { return (i >= 0 && i < kNumLogicControls) ? kLogicControls[i] : ""; }

EXPORT(prepare) void prepare (double sampleRate)
{
    if (gLogic != nullptr)
        gLogic->~Logic();
    gLogic = new (gStorage) Logic();
    for (int i = 0; i < clampedParams(); ++i)
        gParams[i] = kLogicParams[i].init;
    gLogic->prepare (sampleRate);
}

EXPORT(process) int process (double sampleRate, double bpm, double ppq, int numSamples, int playing, double beatsPerBar)
{
    if (gLogic == nullptr)
        return 0;
    LogicBlock b { sampleRate, bpm, ppq, numSamples, playing != 0, gParams, beatsPerBar > 0.0 ? beatsPerBar : 4.0 };
    const int n = gLogic->process (b, gEvents, kMaxEvents);
    return n < 0 ? 0 : (n > kMaxEvents ? kMaxEvents : n);
}
