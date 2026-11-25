// A scripted session for the Two Hands tests: a host that plays a logic module block by
// block, with parameter changes, transport jumps, tempo changes and stops at given samples.
// test/wasm-compare.mjs plays the same scripts through the WebAssembly build with the same
// arithmetic, so the two can be compared to the sample. This is test code: the standard
// library is fine here, it never reaches the module.
#pragma once
#include "logic_api.h"
#include "logic.cpp"
#include <cstdio>
#include <string>
#include <vector>

namespace runner
{

// What happens before the first block that starts at or after `at` (in played samples).
struct Step
{
    long long at;
    char kind;      // P: parameter `index` = value; J: jump to song position `value` (beats);
                    // T: tempo `value` BPM from here; H: `value` stopped blocks, then play on
    int index;
    double value;
};

struct Script
{
    std::string name;
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    int block = 128;
    double startPpq = 0.0;
    long long length = 0;   // samples played
    std::vector<Step> steps;

    Script& param (long long at, int index, double value) { steps.push_back ({ at, 'P', index, value }); return *this; }
    Script& jump (long long at, double ppq) { steps.push_back ({ at, 'J', 0, ppq }); return *this; }
    Script& tempo (long long at, double newBpm) { steps.push_back ({ at, 'T', 0, newBpm }); return *this; }
    Script& halt (long long at, int blocks) { steps.push_back ({ at, 'H', 0, (double) blocks }); return *this; }
};

// One event, at its time in played samples.
struct Ev
{
    long long time;
    int control;
    int note;
    int channel;
    float value;
    int offset;     // within its block
    int blockSize;  // that block's size
    bool playing;   // whether that block was playing
};

inline bool sameEvent (const Ev& a, const Ev& b)
{
    return a.time == b.time && a.control == b.control && a.note == b.note && a.channel == b.channel && a.value == b.value;
}

// Plays a script through a host with setParam (index, value) and
// process (sampleRate, bpm, ppq, numSamples, playing, beatsPerBar, pos, events).
template <typename Host>
std::vector<Ev> runWith (const Script& s, Host& host)
{
    std::vector<Ev> events;
    std::vector<bool> applied (s.steps.size(), false);
    double bpm = s.bpm;
    double spb = s.sampleRate * 60.0 / bpm;
    double ppqBase = s.startPpq;
    long long posBase = 0, pos = 0;

    auto call = [&] (int n, bool playing)
    {
        const double ppq = ppqBase + (double) (pos - posBase) / spb;
        host.process (s.sampleRate, bpm, ppq, n, playing, s.beatsPerBar, pos, events);
    };
    auto applySteps = [&]
    {
        for (size_t i = 0; i < s.steps.size(); ++i)
        {
            const Step& st = s.steps[i];
            if (applied[i] || st.at > pos) continue;
            applied[i] = true;
            if (st.kind == 'P') host.setParam (st.index, st.value);
            else if (st.kind == 'J') { ppqBase = st.value; posBase = pos; }
            else if (st.kind == 'T')
            {
                ppqBase = ppqBase + (double) (pos - posBase) / spb;
                posBase = pos;
                bpm = st.value;
                spb = s.sampleRate * 60.0 / bpm;
            }
            else if (st.kind == 'H')
            {
                for (int k = 0; k < (int) st.value; ++k) call (s.block, false);
            }
        }
    };

    applySteps();
    call (s.block, false);   // stopped first: nothing may start
    while (pos < s.length)
    {
        applySteps();
        const int n = (int) (s.length - pos < s.block ? s.length - pos : s.block);
        call (n, true);
        pos += n;
    }
    applySteps();
    call (s.block, false);   // the stop
    return events;
}

// Two Hands as a host sees it.
struct TwoHandsHost
{
    Logic logic;
    float params[64] {};
    LogicEvent out[1024] {};

    explicit TwoHandsHost (double sampleRate)
    {
        logic.prepare (sampleRate);
        for (int i = 0; i < kNumLogicParams; ++i) params[i] = kLogicParams[i].init;
    }
    void setParam (int index, double value) { params[index] = (float) value; }
    void process (double sr, double bpm, double ppq, int n, bool playing, double bpb, long long pos, std::vector<Ev>& events)
    {
        const LogicBlock b { sr, bpm, ppq, n, playing, params, bpb };
        const int count = logic.process (b, out, 1024);
        for (int i = 0; i < count; ++i)
            events.push_back ({ pos + out[i].offset, out[i].control, out[i].note, out[i].channel, out[i].value, out[i].offset, n, playing });
    }
};

inline std::vector<Ev> run (const Script& s)
{
    static TwoHandsHost* host = nullptr;   // large: kept off the stack, made once
    if (! host) host = new TwoHandsHost (s.sampleRate);
    *host = TwoHandsHost (s.sampleRate);
    return runWith (s, *host);
}

inline std::vector<Ev> notesOnly (const std::vector<Ev>& events)
{
    std::vector<Ev> notes;
    for (const Ev& e : events)
        if (e.control == LOGIC_NOTE_ON || e.control == LOGIC_NOTE_OFF) notes.push_back (e);
    return notes;
}

inline std::vector<Ev> onsOnly (const std::vector<Ev>& events)
{
    std::vector<Ev> ons;
    for (const Ev& e : events)
        if (e.control == LOGIC_NOTE_ON) ons.push_back (e);
    return ons;
}

// The dump that test/wasm-compare.mjs reads: the script, then the events it gave.
inline void dump (std::FILE* f, const Script& s, const std::vector<Ev>& events)
{
    std::fprintf (f, "S %s %.17g %.17g %.17g %d %.17g %lld\n", s.name.c_str(), s.sampleRate, s.bpm, s.beatsPerBar, s.block, s.startPpq, s.length);
    for (const Step& st : s.steps) std::fprintf (f, "%c %lld %d %.17g\n", st.kind, st.at, st.index, st.value);
    for (const Ev& e : events) std::fprintf (f, "E %lld %d %d %d %.9g\n", e.time, e.control, e.note, e.channel, (double) e.value);
    std::fprintf (f, "X\n");
}

}   // namespace runner
