// Runaway: a MIDI tool that goes wrong on purpose, for the tests of the step budget
// (shell/tools/test-budget.sh; docs/midi-tools.md, "The step budget"). It plays C3 on every
// beat, each note 0.9 of a beat long, on channel 1. A quarter of a beat into the bar "At bar",
// while that bar's first note sounds, Fault decides what it does on every block from then on:
//   None               nothing: it plays on
//   Endless loop       a loop that never ends
//   Endless recursion  a function that calls itself without end
//   Heavy work         200,000 turns of a random number generator a block: honest work, far
//                      inside the budget, which must never be stopped
#include "logic_api.h"

static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;

static const LogicParam kLogicParams[] = {
    { "Fault",  0.0f, 3.0f, 0.0f, 1.0f, "[style:menu{None;Endless loop;Endless recursion;Heavy work}]" },
    { "At bar", 1.0f, 8.0f, 2.0f, 1.0f, "[style:knob]" },
};
static const int kNumLogicParams = 2;

static const int kNote = 48;   // C3
static const int kChannel = 1;

struct Logic
{
    bool sounding = false;
    long long offAt = 0;
    unsigned long long spins = 0;
    unsigned long long seed = 1;

    void prepare (double sampleRate)
    {
        sounding = false;
        offAt = 0;
        // No host asks for 1 Hz: tools/test-budget.cpp does, to make preparing never end.
        if (sampleRate == 1.0)
            for (;;)
                ++spins;
    }

    // Each call hands the next a pointer to its own local, so every frame stays alive and the
    // compiler cannot turn the recursion into a loop.
    int dive (int depth, const int* above)
    {
        int here = depth + *above;
        return dive (depth + 1, &here) + here;
    }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        int count = 0;
        auto emit = [&] (int offset, int control, float value)
        {
            if (count < maxEvents)
                out[count++] = { offset, control, value, kNote, kChannel };
        };

        if (! b.playing)
        {
            if (sounding)
                emit (0, LOGIC_NOTE_OFF, 0.0f);
            sounding = false;
            return count;
        }

        const double samplesPerBeat = b.sampleRate * 60.0 / b.bpm;
        const long long blockStart = logic_round (b.ppqStart * samplesPerBeat);
        const long long blockEnd = blockStart + b.numSamples;

        const int fault = logic_clamp ((int) b.params[0], 0, 3);
        const double atBar = logic_clamp ((double) b.params[1], 1.0, 8.0);
        const long long faultAt = logic_round (((atBar - 1.0) * b.beatsPerBar + 0.25) * samplesPerBeat);
        if (fault != 0 && blockEnd > faultAt)
        {
            if (fault == 1)
                for (;;)
                    ++spins;
            if (fault == 2)
            {
                const int top = 0;
                spins += (unsigned long long) dive (0, &top);
            }
            for (int i = 0; i < 200000; ++i)
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        }

        if (sounding && offAt < blockEnd)
        {
            emit ((int) (logic_max (offAt, blockStart) - blockStart), LOGIC_NOTE_OFF, 0.0f);
            sounding = false;
        }
        const long long firstBeat = logic_floor (b.ppqStart);
        for (long long k = firstBeat; k <= firstBeat + 64; ++k)
        {
            const long long on = logic_round ((double) k * samplesPerBeat);
            if (on >= blockEnd)
                break;
            if (on < blockStart)
                continue;
            if (sounding)
            {
                emit ((int) (on - blockStart), LOGIC_NOTE_OFF, 0.0f);
                sounding = false;
            }
            emit ((int) (on - blockStart), LOGIC_NOTE_ON, (float) (80 + (int) (seed >> 60)));
            sounding = true;
            offAt = on + logic_round (0.9 * samplesPerBeat);
            if (offAt < blockEnd)
            {
                emit ((int) (offAt - blockStart), LOGIC_NOTE_OFF, 0.0f);
                sounding = false;
            }
        }
        return count;
    }
};
