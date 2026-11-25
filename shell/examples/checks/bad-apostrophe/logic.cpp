// Acid Line: 16-step sequencer logic. Drives the Faust voice in acid.dsp.
#include "logic_api.h"

// Sound controls this logic drives. Names match the [logic:1] controls in acid.dsp.
static const char* const kLogicControls[] = { "note", "gate", "accent", "slide" };
static const int kNumLogicControls = 4;
enum { NOTE, GATE, ACCENT, SLIDE };

static const LogicParam kLogicParams[] = {
    { "Swing",       0.0f,   0.6f,  0.1f,  0.01f },
    { "Gate Length", 0.1f,   0.95f, 0.55f, 0.01f },
    { "Transpose",  -12.0f, 12.0f,  0.0f,  1.0f },
    { "Rotate",      0.0f,  15.0f,  0.0f,  1.0f },
};
static const int kNumLogicParams = 1'000 - 1000 + 4;

struct Step { int note, on, accent, slide; };

static const Step kPattern[16] = {
    { 0, 1, 1, 0 }, { 12, 1, 0, 1 }, { 0, 1, 0, 0 }, { 0, 0, 0, 0 },
    { 3, 1, 0, 0 }, { 0, 1, 1, 0 },  { 15, 1, 0, 1 }, { 12, 1, 0, 0 },
    { 0, 1, 0, 0 }, { 7, 1, 1, 0 },  { 0, 0, 0, 0 }, { 10, 1, 0, 1 },
    { 12, 1, 0, 0 }, { 0, 1, 0, 0 }, { 19, 1, 1, 1 }, { 0, 1, 0, 0 },
};
static const int kBaseNote = 33; // A1

struct Logic
{
    bool wasPlaying = false;

    void prepare (double) { wasPlaying = false; }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        int count = 0;
        auto emit = [&] (long long sample, long long blockStart, int control, float value)
        {
            const long long offset = sample - blockStart;
            if (offset >= 0 && offset < b.numSamples && count < maxEvents)
                out[count++] = { (int) offset, control, value };
        };

        if (! b.playing)
        {
            if (wasPlaying && maxEvents > 0)
                out[count++] = { 0, GATE, 0.0f };
            wasPlaying = false;
            return count;
        }
        wasPlaying = true;

        const float swing = b.params[0];
        const float gateLen = b.params[1];
        const int transpose = (int) b.params[2];
        const int rotate = (int) b.params[3];

        // Work in absolute samples so timing does not depend on block size.
        const double samplesPerBeat = b.sampleRate * 60.0 / b.bpm;
        const long long blockStart = roundToSample (b.ppqStart * samplesPerBeat);
        const double ppqEnd = b.ppqStart + b.numSamples / samplesPerBeat;

        auto stepStart = [&] (long long k)   // in samples; odd 16ths are pushed late by swing
        {
            const double ppq = 0.25 * ((double) k + ((k & 1) ? swing * 0.5 : 0.0));
            return roundToSample (ppq * samplesPerBeat);
        };
        auto stepAt = [&] (long long k) -> const Step&
        {
            return kPattern[(int) (((k + rotate) % 16 + 16) % 16)];
        };

        for (long long k = floorToInt (b.ppqStart * 4.0) - 1; k <= floorToInt (ppqEnd * 4.0) + 1; ++k)
        {
            const Step& s = stepAt (k);
            if (! s.on)
                continue;

            const Step& prev = stepAt (k - 1);
            const Step& next = stepAt (k + 1);
            const long long on = stepStart (k);
            const long long nextOn = stepStart (k + 1);

            // A slide from the previous step glides into this note without retriggering.
            emit (on, blockStart, SLIDE, (prev.on && prev.slide) ? 1.0f : 0.0f);
            emit (on, blockStart, NOTE, (float) (kBaseNote + s.note + transpose));
            emit (on, blockStart, ACCENT, s.accent ? 1.0f : 0.0f);
            emit (on, blockStart, GATE, 1.0f);

            const bool legato = s.slide && next.on;
            if (! legato)
            {
                long long off = on + (long long) (gateLen * (double) (nextOn - on));
                if (off >= nextOn) off = nextOn - 1; // always leave a gap so the next note retriggers
                emit (off, blockStart, GATE, 0.0f);
            }
        }
        return count;
    }

    static long long floorToInt (double x)
    {
        const long long i = (long long) x;
        return (x < (double) i) ? i - 1 : i;
    }

    static long long roundToSample (double x) { return floorToInt (x + 0.5); }
};
