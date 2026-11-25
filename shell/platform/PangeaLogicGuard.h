// The step budget for a logic module in the native plugin (docs/midi-tools.md, "The step
// budget"; decision 46). In the browser and on the server a module is metered WebAssembly
// (decision 41); natively it is machine code on the host's audio thread, where nothing can
// stop a thread that never returns. So:
//
//   - clang instruments every edge of the module's code (logic_native.flags), and each one
//     calls __sanitizer_cov_trace_pc_guard, defined at the end of this file;
//   - a Guard runs each call into the module with a budget of steps and of stack. The
//     callback counts a step, measures the stack, and when either runs out it leaves the
//     module at once, with _longjmp, back to the Guard;
//   - the shell then never calls that module again. It drops what the module wrote in the
//     stopped block and ends the notes it left sounding (PangeaNotes.h).
//
// Leaving by _longjmp skips the rest of the module's call, like a trap in WebAssembly. That is
// safe for a module because the rules leave it nothing to clean up: no allocation, no files, no
// locks (logic_native.flags turns off the locks on function-local statics), no exceptions.
//
// Include this file in exactly one translation unit of a binary: it defines the two callbacks.
#pragma once

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <setjmp.h>
#include <type_traits>

namespace pangea::logic
{

/** Why a call was stopped. */
enum class Stop
{
    none = 0,
    tooLong = 1,     // it used all its steps: an endless loop, or far too much work for one call
    tooDeep = 2,     // it used all its stack: a function calling itself without end
    unguarded = 3    // the budget could not be set up on this machine, so the module is not run
};

struct Limits
{
    long long steps;   // edges of the module's code one call may take
    long stackBytes;   // stack one call may use below the Guard
};

/**
 * A block (`process`) gets 16 million steps, about 40 ms at most on Apple silicon: thousands of
 * times what Two Hands needs (tools/test-budget.sh measures it), and far more than the browser's
 * million units of WebAssembly allow. Making, preparing and ending a module run off the audio
 * thread and get four times as much. The stack is 64 KB, with frames of at most 16 KB
 * (logic_native.flags); WebAssembly gives a module 256 KB, but an audio thread has less to spare.
 */
constexpr Limits processLimits { 16'000'000, 64 * 1024 };
constexpr Limits setupLimits { 64'000'000, 64 * 1024 };

namespace detail
{
    inline pthread_key_t key;
    inline std::atomic<bool> keyReady { false };
    inline pthread_once_t keyOnce = PTHREAD_ONCE_INIT;
    inline void makeKey() { keyReady.store (pthread_key_create (&key, nullptr) == 0, std::memory_order_release); }
}

class Guard
{
public:
    /** Sets up what every Guard shares. Call it off the audio thread, before the first run. */
    static bool setUp() noexcept
    {
        pthread_once (&detail::keyOnce, detail::makeKey);
        return detail::keyReady.load (std::memory_order_acquire);
    }

    /** Runs fn (context) within `limits`; says why it was stopped, or Stop::none. */
    __attribute__((noinline)) Stop run (const Limits& limits, void (*fn) (void*), void* context) noexcept
    {
        if (! detail::keyReady.load (std::memory_order_acquire))
            return Stop::unguarded;
        given = limits.steps;
        steps = limits.steps;
        stackBytes = limits.stackBytes;
        stop = Stop::none;
        base = static_cast<char*> (__builtin_frame_address (0));
        void* const previous = pthread_getspecific (detail::key);
        pthread_setspecific (detail::key, this);
        if (_setjmp (jump) == 0)
            fn (context);
        pthread_setspecific (detail::key, previous);
        used = given - (steps < 0 ? 0 : steps);
        return stop;
    }

    /** The same with any callable, such as a lambda. */
    template <typename F>
    Stop run (const Limits& limits, F&& f) noexcept
    {
        using Fn = std::remove_reference_t<F>;
        return run (limits, [] (void* c) { (*static_cast<Fn*> (c)) (); }, static_cast<void*> (&f));
    }

    /** The steps the last run took. */
    long long stepsUsed() const noexcept { return used; }

    // For the callback below.
    void count() noexcept
    {
        if (--steps < 0)
            leave (Stop::tooLong);
        if (base - static_cast<char*> (__builtin_frame_address (0)) > stackBytes)
            leave (Stop::tooDeep);
    }

private:
    jmp_buf jump;
    char* base = nullptr;
    long long given = 0, steps = 0, used = 0;
    long stackBytes = 0;
    Stop stop = Stop::none;

    [[noreturn]] void leave (Stop why) noexcept
    {
        stop = why;
        _longjmp (jump, 1);
    }
};

} // namespace pangea::logic

// The callbacks clang's edge instrumentation calls (-fsanitize-coverage=trace-pc-guard). Only the
// module's translation unit is instrumented; this one is not, so the callback counts nothing of
// its own. Hidden, so each plugin binary uses its own.
extern "C" __attribute__((visibility ("hidden"))) void __sanitizer_cov_trace_pc_guard_init (uint32_t*, uint32_t*) {}

extern "C" __attribute__((visibility ("hidden"))) void __sanitizer_cov_trace_pc_guard (uint32_t*)
{
    if (! pangea::logic::detail::keyReady.load (std::memory_order_relaxed))
        return;
    if (auto* guard = static_cast<pangea::logic::Guard*> (pthread_getspecific (pangea::logic::detail::key)))
        guard->count();
}
