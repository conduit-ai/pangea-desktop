// transport-check: plays Faust code, compiled to C++ by the native Faust as build.sh compiles it,
// under a host's transport, block by block, and writes every output sample. The song's controls
// are set before each block by PangeaSong.h, the code the built plugin and the Pangea plugin use
// (decisions 123 and 124). scripts/transport-agreement.ts plays the same code in WebAssembly
// under the same transport and compares the two (docs/decisions.md, decision 124).
//
//   transport-check <plan> <out.f32>
//
// The plan is text: "rate <Hz>", "bpm <BPM>", "outputs <n>", "inputs <n>", then one line a block,
// "<frames> <playing 0|1> <beat|none|nobeats|nohost>": the play head at the block's start ("none"
// is a host with no transport, "nobeats" one that gives no position in beats, "nohost" sets nothing). Every input is fed 0.5. The output is the outputs'
// samples, interleaved per sample, as 32-bit floats.
#include <faust/dsp/dsp.h>
#include <faust/gui/UI.h>
#include <faust/gui/meta.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "FaustDsp.h"
#include "../../platform/PangeaSong.h"

// Every zone marked [pangea:...], by what it holds.
struct HostZones : public UI
{
    std::multimap<std::string, FAUSTFLOAT*> zones;
    std::map<FAUSTFLOAT*, std::string> pending;

    void declare (FAUSTFLOAT* zone, const char* key, const char* value) override
    {
        if (zone != nullptr && std::string (key) == "pangea")
            pending[zone] = value;
    }
    void control (FAUSTFLOAT* zone)
    {
        auto it = pending.find (zone);
        if (it != pending.end())
            zones.insert ({ it->second, zone });
    }
    void openTabBox (const char*) override {}
    void openHorizontalBox (const char*) override {}
    void openVerticalBox (const char*) override {}
    void closeBox() override {}
    void addButton (const char*, FAUSTFLOAT* z) override { control (z); }
    void addCheckButton (const char*, FAUSTFLOAT* z) override { control (z); }
    void addVerticalSlider (const char*, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { control (z); }
    void addHorizontalSlider (const char*, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { control (z); }
    void addNumEntry (const char*, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) override { control (z); }
    void addHorizontalBargraph (const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addVerticalBargraph (const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) override {}
    void addSoundfile (const char*, const char*, Soundfile**) override {}

    void set (const std::string& kind, double value)
    {
        auto range = zones.equal_range (kind);
        for (auto it = range.first; it != range.second; ++it)
            *it->second = (FAUSTFLOAT) value;
    }
};

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: transport-check <plan> <out.f32>" << std::endl;
        return 2;
    }
    std::ifstream plan (argv[1]);
    double rate = 48000.0, bpm = 120.0;
    int outputs = 0, inputs = 0;
    struct Block { int frames; bool playing; std::string beat; };
    std::vector<Block> blocks;
    for (std::string line; std::getline (plan, line);)
    {
        std::istringstream in (line);
        std::string first;
        in >> first;
        if (first == "rate") in >> rate;
        else if (first == "bpm") in >> bpm;
        else if (first == "outputs") in >> outputs;
        else if (first == "inputs") in >> inputs;
        else if (! first.empty())
        {
            Block b { std::stoi (first), false, "" };
            int playing = 0;
            in >> playing >> b.beat;
            b.playing = playing != 0;
            blocks.push_back (b);
        }
    }

    mydsp dsp;
    dsp.init ((int) rate);
    if (dsp.getNumOutputs() != outputs || dsp.getNumInputs() != inputs)
    {
        std::cerr << "the code has " << dsp.getNumInputs() << " in and " << dsp.getNumOutputs() << " out" << std::endl;
        return 1;
    }
    HostZones host;
    dsp.buildUserInterface (&host);

    std::FILE* out = std::fopen (argv[2], "wb");
    if (out == nullptr)
        return 1;
    std::int64_t counted = 0;
    for (const auto& b : blocks)
    {
        if (b.beat != "nohost")
        {
            // "none": no transport at all. "nobeats": a position that says playing or stopped but
            // gives no beats, as JUCE's wrappers give for a host without a transport.
            const bool known = b.beat != "none" && b.beat != "nobeats";
            const bool playing = b.beat == "none" ? true : b.playing;
            const auto song = pangea::songValues (playing, known, known ? std::stod (b.beat) : 0.0,
                                                  counted, bpm, rate);
            host.set ("bpm", bpm);
            host.set ("playing", song.playing);
            host.set ("beat", song.beat);
            host.set ("beatfrac", song.fraction);
        }
        counted += b.frames;
        std::vector<std::vector<FAUSTFLOAT>> ins ((size_t) inputs, std::vector<FAUSTFLOAT> ((size_t) b.frames, 0.5f));
        std::vector<std::vector<FAUSTFLOAT>> outs ((size_t) outputs, std::vector<FAUSTFLOAT> ((size_t) b.frames, 0.0f));
        std::vector<FAUSTFLOAT*> inPtrs, outPtrs;
        for (auto& v : ins) inPtrs.push_back (v.data());
        for (auto& v : outs) outPtrs.push_back (v.data());
        dsp.compute (b.frames, inPtrs.data(), outPtrs.data());
        for (int i = 0; i < b.frames; ++i)
            for (int c = 0; c < outputs; ++c)
                std::fwrite (&outs[(size_t) c][(size_t) i], sizeof (FAUSTFLOAT), 1, out);
    }
    std::fclose (out);
    return 0;
}
