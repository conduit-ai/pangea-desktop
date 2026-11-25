// MIDI Steps: the smallest MIDI tool, the contract fixture for the MIDI tools experiment
// (docs/midi-tools.md). A four-step C minor arpeggio on channel 1, synced to the host, with
// a rate menu, four accent bars that show the step playing, a note length and a variation
// number that nudges the accents. Every layer tests against this module.
#include "logic_api.h"

// A MIDI tool drives no sound controls.
static const char* const kLogicControls[1] = { "" };
static const int kNumLogicControls = 0;

static const LogicParam kLogicParams[] = {
    { "Rate",      0.0f, 2.0f,   0.0f,   1.0f, "[style:menu{1/16;1/8;1/4}]" },
    { "Accent 1",  0.0f, 127.0f, 100.0f, 1.0f, "[style:bars][group:Accents][playhead:0]" },
    { "Accent 2",  0.0f, 127.0f, 50.0f,  1.0f, "[style:bars][group:Accents][playhead:0]" },
    { "Accent 3",  0.0f, 127.0f, 70.0f,  1.0f, "[style:bars][group:Accents][playhead:0]" },
    { "Accent 4",  0.0f, 127.0f, 40.0f,  1.0f, "[style:bars][group:Accents][playhead:0]" },
    { "Length",    0.1f, 1.0f,   0.5f,   0.01f, "[style:knob]" },
    { "Variation", 0.0f, 999.0f, 0.0f,   1.0f, "[style:generate]" },
};
static const int kNumLogicParams = 7;

static const int kNotes[4] = { 60, 63, 67, 70 };   // C4 Eb4 G4 Bb4
static const int kChannel = 1;

struct Logic
{
    bool sounding = false;
    int soundingNote = 0;
    long long offAt = 0;          // absolute sample of the pending note off
    long long nextBlockStart = 0; // where the next block should start if the transport runs on

    void prepare (double) { sounding = false; offAt = 0; nextBlockStart = 0; }

    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        int count = 0;
        auto emit = [&] (int offset, int control, float value, int note)
        {
            if (count < maxEvents)
                out[count++] = { offset, control, value, note, kChannel };
        };

        if (! b.playing)
        {
            if (sounding)
                emit (0, LOGIC_NOTE_OFF, 0.0f, soundingNote);
            sounding = false;
            return count;
        }

        const double samplesPerBeat = b.sampleRate * 60.0 / b.bpm;
        const long long blockStart = logic_round (b.ppqStart * samplesPerBeat);
        const long long blockEnd = blockStart + b.numSamples;

        // A jump in the transport (a loop, a seek) ends the note now.
        if (sounding && (blockStart != nextBlockStart || offAt < blockStart))
        {
            emit (0, LOGIC_NOTE_OFF, 0.0f, soundingNote);
            sounding = false;
        }
        nextBlockStart = blockEnd;

        const int rate = logic_clamp ((int) b.params[0], 0, 2);
        const double stepBeats = rate == 0 ? 0.25 : (rate == 1 ? 0.5 : 1.0);
        const double length = logic_clamp ((double) b.params[5], 0.1, 1.0);
        const int variation = (int) b.params[6];

        const long long firstStep = logic_floor (b.ppqStart / stepBeats) - 1;
        const long long lastStep = logic_floor (((double) blockEnd / samplesPerBeat) / stepBeats) + 1;
        for (long long k = firstStep; k <= lastStep; ++k)
        {
            const long long on = logic_round ((double) k * stepBeats * samplesPerBeat);
            if (on < blockStart || on >= blockEnd)
                continue;
            const int step = (int) logic_mod (k, 4);

            // The note before ends here at the latest.
            if (sounding && offAt <= on)
            {
                emit ((int) (logic_max (offAt, blockStart) - blockStart), LOGIC_NOTE_OFF, 0.0f, soundingNote);
                sounding = false;
            }
            if (sounding)
            {
                emit ((int) (on - blockStart), LOGIC_NOTE_OFF, 0.0f, soundingNote);
                sounding = false;
            }

            // The variation nudges each accent by a fixed amount for that seed and step.
            int velocity = (int) b.params[1 + step];
            if (variation > 0)
            {
                LogicRandom random ((unsigned long long) variation * 4ull + (unsigned long long) step + 1ull);
                velocity += random.below (31) - 15;
            }
            velocity = logic_clamp (velocity, 1, 127);

            emit ((int) (on - blockStart), LOGIC_DISPLAY, (float) step, 0);
            emit ((int) (on - blockStart), LOGIC_NOTE_ON, (float) velocity, kNotes[step]);
            sounding = true;
            soundingNote = kNotes[step];
            offAt = on + logic_max ((long long) 1, logic_round (length * stepBeats * samplesPerBeat));
        }

        if (sounding && offAt >= blockStart && offAt < blockEnd)
        {
            emit ((int) (offAt - blockStart), LOGIC_NOTE_OFF, 0.0f, soundingNote);
            sounding = false;
        }

        // Sorted by offset, offs before ons at the same sample (a short insertion sort).
        for (int i = 1; i < count; ++i)
        {
            LogicEvent e = out[i];
            int j = i - 1;
            while (j >= 0 && (out[j].offset > e.offset
                              || (out[j].offset == e.offset && out[j].control == LOGIC_NOTE_ON && e.control == LOGIC_NOTE_OFF)))
            {
                out[j + 1] = out[j];
                --j;
            }
            out[j + 1] = e;
        }
        return count;
    }
};
