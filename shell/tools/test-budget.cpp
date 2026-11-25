// The step budget without JUCE (tools/test-budget.sh; docs/midi-tools.md, "The step budget";
// decision 46). The module is compiled as the plugin compiles it, platform/logic_native.cpp
// with platform/logic_native.flags, and run through pangea::logic::Guard as the shell runs it.
//
//   test-budget faults      with tools/fixtures/runaway: an endless loop, an endless recursion
//                           and a prepare that never ends are each stopped, for the right reason
//                           and quickly; honest heavy work is not; and a guard that stopped a
//                           call runs the next one as if nothing had happened
//   test-budget calibrate   with any module: the most steps one block takes, over the defaults,
//                           every control at its minimum and maximum, every value of a small
//                           menu, three block sizes and two tempos; none may come near the budget
#include "logic_api.h"
#include "PangeaLogicRun.h"
#include "PangeaLogicGuard.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pangea::logic;

namespace
{

int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

const char* name (Stop why)
{
    switch (why)
    {
        case Stop::none: return "not stopped";
        case Stop::tooLong: return "stopped for running too long";
        case Stop::tooDeep: return "stopped for going too deep";
        case Stop::unguarded: return "not run, unguarded";
    }
    return "?";
}

double msSince (std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
}

/** One module and its guard, driven block by block from bar one. */
struct Runner
{
    Guard guard;
    Module* module = nullptr;
    std::vector<float> params;
    std::vector<LogicEvent> events = std::vector<LogicEvent> (1024);
    long long mostSteps = 0;
    double slowestMs = 0.0;
    int stoppedAtBlock = -1;
    Stop why = Stop::none;

    Runner()
    {
        for (int i = 0; i < numParams(); ++i)
            params.push_back (pangea::logic::params()[i].init);
    }

    Stop start (double sampleRate)
    {
        module = nullptr;
        const auto t0 = std::chrono::steady_clock::now();
        why = guard.run (setupLimits, [&] { module = make(); prepare (*module, sampleRate); });
        slowestMs = msSince (t0);
        return why;
    }

    /** Plays `bars` bars of 4/4 at `bpm` in blocks of `blockSize`; stops early when the budget does. */
    Stop play (double bars, double bpm, int blockSize, double sampleRate = 48000.0)
    {
        mostSteps = 0;
        slowestMs = 0.0;
        stoppedAtBlock = -1;
        const double samplesPerBeat = sampleRate * 60.0 / bpm;
        const long long total = (long long) (bars * 4.0 * samplesPerBeat);
        int index = 0;
        for (long long at = 0; at < total; at += blockSize, ++index)
        {
            LogicBlock block { sampleRate, bpm, (double) at / samplesPerBeat, blockSize, true, params.data(), 4.0 };
            int count = 0;
            const auto t0 = std::chrono::steady_clock::now();
            why = guard.run (processLimits, [&] { count = process (*module, block, events.data(), (int) events.size()); });
            const double ms = msSince (t0);
            if (ms > slowestMs)
                slowestMs = ms;
            if (guard.stepsUsed() > mostSteps)
                mostSteps = guard.stepsUsed();
            if (why != Stop::none)
            {
                stoppedAtBlock = index;
                return why;
            }
        }
        return why;
    }
};

std::string percentOfBudget (long long steps)
{
    char text[64];
    std::snprintf (text, sizeof text, "%.4f%%", 100.0 * (double) steps / (double) processLimits.steps);
    return text;
}

int faults()
{
    if (numParams() < 2 || std::strcmp (params()[0].name, "Fault") != 0)
    {
        std::printf ("FAIL  faults needs tools/fixtures/runaway\n");
        return 1;
    }
    check (Guard::setUp(), "the budget is set up on this machine");

    // The bar the fault starts in is bar 2: a quarter of a beat into beat 5, at sample 102000.
    const int expectedBlock = 102000 / 512;
    for (const auto& [fault, want] : { std::pair { 1.0f, Stop::tooLong }, std::pair { 2.0f, Stop::tooDeep } })
    {
        Runner r;
        r.params[0] = fault;
        check (r.start (48000.0) == Stop::none, "made and prepared");
        const auto why = r.play (4.0, 120.0, 512);
        check (why == want, std::string (fault == 1.0f ? "endless loop: " : "endless recursion: ") + name (why) + " in block "
                                + std::to_string (r.stoppedAtBlock) + " (the fault starts in block " + std::to_string (expectedBlock)
                                + "), after " + std::to_string (r.slowestMs).substr (0, 5) + " ms");
        check (r.stoppedAtBlock == expectedBlock, "stopped in the block where the fault starts, not before");
        check (r.slowestMs < 250.0, "the stopped block took under 250 ms");

        // The same guard, a new module, no fault: it plays as if nothing had happened.
        Runner& again = r;
        again.params[0] = 0.0f;
        check (again.start (48000.0) == Stop::none && again.play (4.0, 120.0, 512) == Stop::none,
               "the same guard then runs a new module for 4 bars without a stop");
    }

    {
        Runner r;
        r.params[0] = 3.0f;
        r.start (48000.0);
        const auto why = r.play (8.0, 120.0, 512);
        check (why == Stop::none, std::string ("heavy work for 7 bars: ") + name (why) + ", at most " + std::to_string (r.mostSteps)
                                      + " steps a block (" + percentOfBudget (r.mostSteps) + " of the budget), the slowest block "
                                      + std::to_string (r.slowestMs).substr (0, 5) + " ms");
    }

    {
        Runner r;
        const auto why = r.start (1.0);
        check (why == Stop::tooLong, std::string ("a prepare that never ends: ") + name (why) + " after "
                                         + std::to_string (r.slowestMs).substr (0, 5) + " ms");
    }

    // The module's own functions outside any guard count nothing and stop nothing.
    check (numParams() == 2 && std::strcmp (controlName (0), "") == 0, "the parameters and controls read outside a guard");

    std::printf ("%s\n", failures == 0 ? "all budget checks passed" : (std::to_string (failures) + " budget checks failed").c_str());
    return failures == 0 ? 0 : 1;
}

int calibrate()
{
    check (Guard::setUp(), "the budget is set up on this machine");
    std::vector<std::pair<std::string, std::vector<float>>> settings;
    Runner defaults;
    settings.push_back ({ "the defaults", defaults.params });
    for (int i = 0; i < numParams(); ++i)
    {
        const auto& p = params()[i];
        const bool menu = p.step >= 1.0f && p.max - p.min <= 16.0f;
        for (float v = p.min; v <= p.max; v += menu ? p.step : (p.max - p.min))
        {
            auto values = defaults.params;
            values[(size_t) i] = v;
            settings.push_back ({ std::string (p.name) + " at " + std::to_string (v).substr (0, 6), values });
            if (! menu && p.max == p.min)
                break;
        }
    }

    long long most = 0;
    std::string where;
    int runs = 0, stopped = 0;
    for (const auto& [label, values] : settings)
        for (const int blockSize : { 64, 512, 1024 })
            for (const double bpm : { 90.0, 180.0 })
            {
                Runner r;
                r.params = values;
                if (r.start (48000.0) != Stop::none || r.play (8.0, bpm, blockSize) != Stop::none)
                {
                    ++stopped;
                    std::printf ("FAIL  stopped with %s, block %d, %d BPM\n", label.c_str(), blockSize, (int) bpm);
                }
                ++runs;
                if (r.mostSteps > most)
                {
                    most = r.mostSteps;
                    where = label + ", block " + std::to_string (blockSize) + ", " + std::to_string ((int) bpm) + " BPM";
                }
            }
    check (stopped == 0, std::to_string (runs) + " runs of 8 bars (" + std::to_string (settings.size()) + " settings), none stopped");
    check (most * 100 < processLimits.steps, "at most " + std::to_string (most) + " steps in one block, " + percentOfBudget (most)
                                                 + " of the budget, with " + where);
    std::printf ("%s\n", failures == 0 ? "calibration passed" : (std::to_string (failures) + " calibration checks failed").c_str());
    return failures == 0 ? 0 : 1;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "faults")
        return faults();
    if (mode == "calibrate")
        return calibrate();
    std::printf ("usage: test-budget faults|calibrate\n");
    return 2;
}
