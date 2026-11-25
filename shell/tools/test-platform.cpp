// Tests for the parts of the native shell that need no JUCE: the note safety net
// (platform/PangeaNotes.h) and the ring of notes a MIDI tool's panel reads (notes::Heard), the
// display hints (platform/PangeaLogicMeta.h), the bypass's fader (platform/PangeaBypass.h) and what
// feeds an effect's sidechain inputs (platform/PangeaSidechain.h). Run by tools/test-platform.sh,
// which compiles this with a bare clang++.
#include "PangeaBypass.h"
#include "PangeaLogicMeta.h"
#include "PangeaNotes.h"
#include "PangeaSidechain.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace
{

int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL  %s\n", what.c_str());
    }
}

std::string joined (const std::vector<std::string>& v)
{
    std::string s;
    for (const auto& x : v)
        s += (s.empty() ? "" : "|") + x;
    return s;
}

using pangea::notes::Note;
using pangea::notes::Tracker;

LogicEvent on (int offset, int note, float velocity, int channel = 1) { return { offset, LOGIC_NOTE_ON, velocity, note, channel }; }
LogicEvent off (int offset, int note, int channel = 1) { return { offset, LOGIC_NOTE_OFF, 0.0f, note, channel }; }

std::vector<Note> route (const std::vector<LogicEvent>& events, Tracker& tracker, bool playing = true, bool endAll = false, int numSamples = 512)
{
    std::vector<Note> out (4096);
    const int n = pangea::notes::route (events.data(), (int) events.size(), numSamples, playing, endAll, tracker, out.data(), (int) out.size());
    out.resize ((size_t) n);
    return out;
}

std::string shown (const std::vector<Note>& notes)
{
    std::string s;
    for (const auto& n : notes)
        s += (s.empty() ? "" : " ") + std::to_string (n.offset) + (n.on ? ":on:" : ":off:") + std::to_string (n.channel) + ":"
           + std::to_string (n.note) + (n.on ? ":" + std::to_string (n.velocity) : "");
    return s;
}

void testHints()
{
    using namespace pangea::logicmeta;

    // The pairs, as parseMeta reads them (src/lib/logic/compile.test.ts).
    const auto p = pairs ("[style:bars][group:Left hand][playhead:0]");
    check (p.size() == 3 && p[0].key == "style" && p[0].value == "bars" && p[1].key == "group" && p[1].value == "Left hand"
               && p[2].key == "playhead" && p[2].value == "0",
           "pairs: bars, group, playhead");
    check (pairs ("no pairs here [9:x] [:y] [a b:c] [ok : fine ]").size() == 1 && pairs ("[ ok : fine ]")[0].value == "fine",
           "pairs: what is not a pair is ignored, values are trimmed");
    check (pairs ("[style:menu{a;b}]broken[unit:ms").size() == 1, "pairs: an unclosed pair is ignored");
    check (pairs (nullptr).empty() && pairs ("").empty(), "pairs: no meta, no pairs");

    std::vector<std::string> labels;
    check (menuLabels ("menu{1/16;1/8;dotted 1/8}", labels) && joined (labels) == "1/16|1/8|dotted 1/8", "menu labels");
    check (menuLabels (" menu { a ; ;b } ", labels) && joined (labels) == "a|b", "menu labels: trimmed, empty ones dropped");
    check (! menuLabels ("knob", labels) && ! menuLabels ("menu{a}b}", labels) && ! menuLabels ("menu{a", labels), "not menus");

    // The fixture's controls (shell/examples/midi-steps/logic.cpp).
    auto rate = parse ("[style:menu{1/16;1/8;1/4}]");
    check (rate.style == Style::menu && joined (rate.labels) == "1/16|1/8|1/4" && rate.group.empty(), "Rate is a menu in no group");
    auto accent = parse ("[style:bars][group:Accents][playhead:0]");
    check (accent.style == Style::bars && accent.group == "Accents" && accent.playhead == 0, "Accent 1 is a bar in Accents, playhead 0");
    check (key (accent.group, "Accent 1") == "Accents/Accent 1" && key (rate.group, "Rate") == "Rate", "keys as controlKey makes them");
    check (parse ("[style:knob]").style == Style::knob && parse (nullptr).style == Style::knob && parse ("").style == Style::knob,
           "a knob by default");
    check (parse ("[style:generate]").style == Style::generate, "Variation is a generate control");

    // The rules logicTree and itemFor follow.
    check (parse ("[style:bars][style:knob]").style == Style::knob, "the last style wins");
    check (parse ("[style:slider]").style == Style::knob, "an unknown style is a knob");
    check (parse ("[group: Left hand ][group:Right]").group == "Left hand", "the first group wins, trimmed");
    check (parse ("[group:  ][group:Right]").group.empty(), "an empty first group is no group");
    check (parse ("[unit:ms][unit:s]").unit == "ms", "the first unit wins");
    check (parse ("[playhead:7]").playhead == 7 && parse ("[playhead:8]").playhead == -1 && parse ("[playhead:x]").playhead == -1,
           "a playhead is a display slot from 0 to 7");
    check (parse ("[style:menu{}]").style == Style::menu && parse ("[style:menu{}]").labels.empty(), "an empty menu is still a menu");
    // The XY pad's pair (docs/felt-look.md), as the site's libraryControl reads it.
    check (parse ("[style:knob][xy:x]").xy == "x" && parse ("[xy: Y ]").xy == "y", "[xy:x] and [xy:y], trimmed, in either case");
    check (parse ("[xy:z]").xy.empty() && parse ("[style:knob]").xy.empty() && parse ("[xy:y][xy:x]").xy == "y",
           "anything else is neither, and the first wins");
}

void testNotes()
{
    {
        Tracker t;
        const auto out = route ({ on (10, 60, 100), off (300, 60) }, t);
        check (shown (out) == "10:on:1:60:100 300:off:1:60" && t.count() == 0, "a note on and its off pass through: " + shown (out));
    }
    {
        Tracker t;
        const auto out = route ({ off (5, 60), on (100, 61, 90, 2) }, t);
        check (shown (out) == "100:on:2:61:90" && t.isOn (2, 61), "a stray off is dropped: " + shown (out));
    }
    {
        Tracker t;
        const auto out = route ({ on (100, 60, 90), on (50, 62, 80), off (100, 62), on (100, 64, 70) }, t);
        check (shown (out) == "50:on:1:62:80 100:off:1:62 100:on:1:60:90 100:on:1:64:70",
               "sorted by offset, offs first, otherwise in order: " + shown (out));
    }
    {
        Tracker t;
        const auto out = route ({ on (0, 60, 90), on (20, 60, 100) }, t);
        check (shown (out) == "0:on:1:60:90 20:off:1:60 20:on:1:60:100" && t.count() == 1, "a second on ends the first: " + shown (out));
    }
    {
        Tracker t;
        const auto out = route ({ on (-5, 200, 300.0f, 0), on (9999, -3, 0.2f, 40), on (3, 10, std::nanf (""), 1) }, t, true, false, 64);
        check (shown (out) == "0:on:1:127:127 63:on:16:0:1", "offsets, notes, channels and velocities are clamped, NaN dropped: " + shown (out));
    }
    {
        Tracker t;
        route ({ on (0, 60, 90), on (0, 64, 90, 3) }, t);
        const auto stopped = route ({ on (0, 67, 90) }, t, false, true);
        check (shown (stopped) == "0:off:1:60 0:off:3:64" && t.count() == 0, "stopped: everything ends, no new note starts: " + shown (stopped));
    }
    {
        Tracker t;
        route ({ on (0, 60, 90) }, t);
        const auto jumped = route ({ off (0, 60), on (0, 60, 100) }, t, true, true);
        check (shown (jumped) == "0:off:1:60 0:on:1:60:100" && t.count() == 1,
               "a jump ends what sounds first, then the module plays on: " + shown (jumped));
    }
    {
        Tracker t;
        std::vector<LogicEvent> many;
        for (int i = 0; i < 1500; ++i)
            many.push_back (on (i % 512, i % 128, 90, 1 + i % 16));
        const auto out = route (many, t);
        check (out.size() <= 2048 && t.count() <= 1024, "more than the platform asks for is cut, not overrun");
    }
    {
        Tracker t;
        std::vector<LogicEvent> display = { { 5, LOGIC_DISPLAY, 2.0f, 0, 0 }, { 6, 3, 0.5f, 0, 0 } };
        check (route (display, t).empty(), "display and sound control events are not notes");
        pangea::notes::Display d;
        display.push_back ({ 7, LOGIC_DISPLAY, 9.0f, 8, 0 });
        display.push_back ({ 8, LOGIC_DISPLAY, std::nanf (""), 1, 0 });
        d.take (display.data(), (int) display.size());
        check (d.position (0) == 2 && d.position (1) == -1 && d.position (7) == -1 && d.position (8) == -1,
               "display slots 0 to 7 take finite values, anything else is ignored");
    }
}

void testTransport()
{
    const double sr = 48000.0, bpm = 120.0;   // 24000 samples a beat
    auto beats = [&] (double samples) { return samples / 24000.0; };
    pangea::notes::TransportFollower f;
    bool any = false;
    for (int b = 0; b < 100; ++b)
        any = f.jumped (beats (b * 512.0), bpm, 512, sr, true) || any;
    check (! any, "a running transport never jumps");
    check (f.jumped (0.0, bpm, 512, sr, true), "back to the start is a jump");
    check (! f.jumped (beats (512.0 + 20.0), bpm, 512, sr, true), "20 samples off is host rounding, not a jump");
    check (f.jumped (beats (512.0 * 2 + 20.0 + 4800.0), bpm, 512, sr, true), "a tenth of a second forward is a jump");
    check (! f.jumped (beats (1.0e6), bpm, 512, sr, false), "stopping is not a jump");
    check (! f.jumped (beats (5.0e6), bpm, 512, sr, true), "starting somewhere else is not a jump");
    // A tempo ramp: the block was planned at 120 BPM but played at 121 for 4096 samples.
    pangea::notes::TransportFollower ramp;
    ramp.jumped (0.0, 120.0, 4096, sr, true);
    check (! ramp.jumped (4096.0 * 121.0 / (60.0 * sr), 121.0, 4096, sr, true), "a tempo ramp across a long block is not a jump");
}

// The bypass every plugin has (decision 75): its parameter, and the fader both processors use.
void testBypass()
{
    using pangea::bypass::Fader;
    using pangea::bypass::blend;

    // The parameter. A Faust control's id is its label in lower-case letters and digits, so one
    // with an underscore is no project's; its hint is above a control's 1 and an effect section's 2.
    const std::string id = pangea::bypass::parameterId;
    bool plain = true;
    for (char c : id)
        plain = plain && ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
    check (id == "pangea_bypass" && ! plain && std::string (pangea::bypass::parameterName) == "Bypass",
           "the bypass parameter is pangea_bypass, an id no Faust label can make, named Bypass");
    check (pangea::bypass::versionHint > 2, "its version hint is above every control's, so it is last in an Audio Unit's list");

    // Every gain a run of blocks gives, sample by sample, with the largest step between two.
    struct Walk { std::vector<float> gains; float largestStep = 0.0f; bool monotonic = true; };
    auto walk = [] (Fader& f, int blocks, int blockSize)
    {
        Walk w;
        float last = f.gain();
        float direction = 0.0f;
        for (int b = 0; b < blocks; ++b)
        {
            const auto ramp = f.advance (blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                const float g = ramp.at (i);
                const float step = g - last;
                w.largestStep = std::max (w.largestStep, std::abs (step));
                if (step != 0.0f)
                {
                    if (direction != 0.0f && (step > 0.0f) != (direction > 0.0f))
                        w.monotonic = false;
                    direction = step;
                }
                w.gains.push_back (g);
                last = g;
            }
        }
        return w;
    };
    auto firstAt = [] (const std::vector<float>& gains, float value)
    {
        for (size_t i = 0; i < gains.size(); ++i)
            if (gains[i] == value)
                return (int) i;
        return -1;
    };

    {
        Fader f;
        f.prepare (48000.0);
        f.setTarget (false);
        check (f.lengthInSamples() == 1440 && f.fullyIn() && ! f.fullyBypassed() && f.gain() == 1.0f,
               "at 48 kHz a fade is 1440 samples, 30 ms, and the plugin starts in");
        const auto rest = f.advance (512);
        check (rest.start == 1.0f && rest.end == 1.0f && rest.moving() == 0 && rest.at (0) == 1.0f && rest.at (511) == 1.0f,
               "with nothing asked for a block's ramp rests at 1");

        f.setTarget (true);
        check (f.bypassAsked() && ! f.fullyBypassed() && ! f.fullyIn(), "a bypass asked for is not there at once");
        const auto w = walk (f, 4, 512);
        check (firstAt (w.gains, 0.0f) == 1439 && w.gains[1438] > 0.0f && w.gains.back() == 0.0f && f.fullyBypassed(),
               "it arrives at 0 on the 1440th sample, and not before: " + std::to_string (firstAt (w.gains, 0.0f) + 1));
        check (w.monotonic && w.largestStep <= 1.0f / 1440.0f + 1.0e-6f,
               "on the way no sample is further from the last than one step of 1/1440: " + std::to_string (w.largestStep));

        // The third block arrives part of the way through: 416 samples move, the rest stay at 0.
        Fader g;
        g.prepare (48000.0);
        g.setTarget (false);
        g.setTarget (true);
        const auto first = g.advance (512), second = g.advance (512), third = g.advance (512), fourth = g.advance (512);
        check (first.start == 1.0f && first.moving() == 512 && first.end == second.start && second.end == third.start
                   && third.moving() == 416 && third.end == 0.0f && third.at (415) == 0.0f && third.at (414) > 0.0f
                   && fourth.moving() == 0 && fourth.start == 0.0f && fourth.end == 0.0f,
               "each block starts where the last ended, and the block the fade ends in says how far it moves");

        // Taken off again in the middle of a fade: back up from where it stands, with no jump.
        Fader h;
        h.prepare (48000.0);
        h.setTarget (false);
        h.setTarget (true);
        auto down = walk (h, 1, 720);
        h.setTarget (false);
        check (! h.fullyBypassed() && ! h.fullyIn() && std::abs (h.gain() - 0.5f) < 1.0e-6f, "half way down it is neither in nor bypassed");
        auto up = walk (h, 2, 512);
        check (std::abs (up.gains.front() - down.gains.back()) <= 1.0f / 1440.0f + 1.0e-6f && up.monotonic
                   && firstAt (up.gains, 1.0f) == 719 && h.fullyIn(),
               "taken off half way, it turns round with no jump and is back in after as many samples");
    }
    {
        // Nothing sounds across a prepare, so what is asked for first is taken at once.
        Fader f;
        f.prepare (44100.0);
        f.setTarget (true);
        const auto ramp = f.advance (256);
        check (f.lengthInSamples() == 1323 && f.fullyBypassed() && ramp.start == 0.0f && ramp.end == 0.0f && ramp.moving() == 0,
               "a session that opens bypassed starts bypassed, with no fade: 1323 samples at 44.1 kHz");
        f.setTarget (false);
        const auto w = walk (f, 1, 1323);
        check (w.gains.back() == 1.0f && w.gains[1321] < 1.0f && f.fullyIn(), "and from there the plugin comes in over a whole fade");
        f.setTarget (true);
        f.advance (100);
        f.prepare (96000.0);
        check (f.lengthInSamples() == 2880 && f.fullyBypassed(), "a new rate sets a new length and ends a fade that was running");
    }
    {
        Fader f;
        f.prepare (0.0);
        const int none = f.lengthInSamples();
        f.prepare (std::numeric_limits<double>::quiet_NaN());
        const int nan = f.lengthInSamples();
        f.prepare (10.0);
        const int tiny = f.lengthInSamples();
        f.setTarget (false);
        f.setTarget (true);
        const auto ramp = f.advance (std::numeric_limits<int>::max());
        check (none == 1440 && nan == 1440 && tiny == 1 && f.fullyBypassed() && ramp.end == 0.0f && ramp.moving() == 1,
               "a rate that is no rate gives 30 ms at 48 kHz, a fade is never shorter than a sample, and a huge block cannot overrun");
        const auto still = f.advance (0);
        const auto negative = f.advance (-5);
        check (still.moving() == 0 && negative.moving() == 0 && f.fullyBypassed(), "a block of no samples moves nothing");
    }

    // What a bypassed effect gives on each channel of its output, from the inputs of the host's track.
    {
        using pangea::bypass::keepDry;
        const std::vector<float> left { 0.5f, -0.25f, 0.125f, 1.0f }, right { 0.1f, 0.2f, -0.3f, -1.0f };
        const float* stereo[] = { left.data(), right.data() };
        const float* mono[] = { left.data() };
        std::vector<float> a (4, 9.0f), b (4, 9.0f);
        keepDry (a.data(), 0, 2, stereo, 2, 4);
        keepDry (b.data(), 1, 2, stereo, 2, 4);
        check (a == left && b == right, "a stereo input on a stereo output: each side is its own input");
        keepDry (a.data(), 0, 2, mono, 1, 4);
        keepDry (b.data(), 1, 2, mono, 1, 4);
        check (a == left && b == left, "a mono input on a stereo output goes to both sides");
        keepDry (a.data(), 0, 1, mono, 1, 4);
        check (a == left, "a mono input on a mono output is itself");
        keepDry (a.data(), 0, 1, stereo, 2, 4);
        check (a == std::vector<float> ({ 0.3f, (-0.25f + 0.2f) * 0.5f, (0.125f - 0.3f) * 0.5f, 0.0f }),
               "a stereo input on a mono output is the mean of its sides");
        keepDry (a.data(), 0, 2, nullptr, 0, 4);
        check (a == std::vector<float> (4, 0.0f), "with no input there is nothing to give");
    }

    // The blend the processors use: wet is the plugin, dry what a bypassed plugin gives.
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        std::vector<float> dry (512), wet (512);
        for (size_t i = 0; i < dry.size(); ++i)
            dry[i] = 0.25f * std::sin (0.07f * (float) i);

        Fader f;
        f.prepare (48000.0);
        f.setTarget (true);   // taken at once: bypassed
        for (auto& x : wet) x = nan;
        blend (f.advance (512), wet.data(), dry.data(), 512);
        bool same = true;
        for (size_t i = 0; i < dry.size(); ++i)
            same = same && wet[i] == dry[i];
        check (same, "fully bypassed, an effect gives back its input to the bit, whatever the plugin made");
        for (auto& x : wet) x = nan;
        blend (f.advance (512), wet.data(), nullptr, 512);
        bool silent = true;
        for (float x : wet)
            silent = silent && x == 0.0f;
        check (silent, "fully bypassed with no dry sound, an instrument is silent");

        Fader in;
        in.prepare (48000.0);
        in.setTarget (false);
        for (size_t i = 0; i < wet.size(); ++i) wet[i] = 0.5f - dry[i];
        const auto before = wet;
        blend (in.advance (512), wet.data(), dry.data(), 512);
        check (wet == before, "with the plugin in, its sound is left as it is");

        // A whole fade of a steady 1 against silence: the output is the gain itself, with no step
        // larger than the fade allows; then of two equal sounds, which stay equal.
        in.setTarget (true);
        float last = 1.0f, largest = 0.0f;
        bool equalStay = true;
        for (int b = 0; b < 4; ++b)
        {
            std::vector<float> ones (512, 1.0f), copy (dry);
            const auto ramp = in.advance (512);
            blend (ramp, ones.data(), nullptr, 512);
            blend (ramp, copy.data(), dry.data(), 512);
            for (size_t i = 0; i < ones.size(); ++i)
            {
                largest = std::max (largest, std::abs (ones[i] - last));
                last = ones[i];
                equalStay = equalStay && std::abs (copy[i] - dry[i]) < 1.0e-6f;
            }
        }
        check (last == 0.0f && largest <= 1.0f / 1440.0f + 1.0e-6f,
               "a steady sound fades out with no step larger than 1/1440 of it: " + std::to_string (largest));
        check (equalStay, "while the fade runs, a plugin that changes nothing still gives its input");

        // The block a fade ends in: mixed up to the sample it ends on, the dry sound alone after.
        Fader end;
        end.prepare (48000.0);
        end.setTarget (false);
        end.setTarget (true);
        end.advance (1440 - 100);
        std::vector<float> tail (512, nan);
        tail[98] = 1.0f;
        blend (end.advance (512), tail.data(), dry.data(), 512);
        bool clean = true;
        for (size_t i = 99; i < tail.size(); ++i)
            clean = clean && tail[i] == dry[i];
        check (clean && std::isnan (tail[0]) && std::abs (tail[98] - (1.0f / 1440.0f + dry[98] * (1439.0f / 1440.0f))) < 1.0e-6f,
               "in the block a fade ends in, every sample from the end on is the dry sound alone");
    }
}

// The notes a MIDI tool sends out, as its web panel reads them (docs/felt-look.md).
void testHeard()
{
    using pangea::notes::Heard;
    std::vector<int> read;
    const auto into = [&read] (int on, int note, int velocity, int channel)
    {
        read.insert (read.end(), { on, note, velocity, channel });
    };

    Heard heard;
    heard.push ({ 0, true, 1, 60, 100 });
    check (heard.end() == 0, "with no panel listening, no note is kept");

    heard.listeners.fetch_add (1);
    std::uint64_t from = heard.end();
    heard.push ({ 0, true, 1, 60, 100 });
    heard.push ({ 64, true, 2, 67, 90 });
    heard.push ({ 128, false, 1, 60, 0 });
    from = heard.read (from, into);
    check (read == std::vector<int> ({ 1, 60, 100, 1, 1, 67, 90, 2, 0, 60, 0, 1 }),
           "a listening panel reads each on and off as on, note, velocity, channel, in order");
    read.clear();
    from = heard.read (from, into);
    check (read.empty() && from == 3, "a second read finds nothing new");

    heard.push ({ 0, true, 16, 200, 300 });
    heard.read (from, into);
    check (read == std::vector<int> ({ 1, 127, 127, 16 }), "a note out of range is read clamped, as it was sent");

    read.clear();
    from = heard.end();
    for (int i = 0; i < Heard::capacity + 10; ++i)
        heard.push ({ 0, i % 2 == 0, 1, i % 128, 1 + i % 127 });
    from = heard.read (from, into);
    check (read.size() == (size_t) Heard::capacity * 4, "a reader that fell behind reads the ring's worth, the newest");
    check (read[1] == 10 % 128 && read[read.size() - 3] == (Heard::capacity + 9) % 128,
           "and goes on from the oldest note the ring still holds to the newest");

    heard.listeners.fetch_sub (1);
    const auto before = heard.end();
    heard.push ({ 0, true, 1, 61, 100 });
    check (heard.end() == before, "when the last panel closes, notes are no longer kept");
}

// The sidechain (docs/sidechain.md): what the declaration says, when the code's inputs match it,
// and what each of its inputs is fed from a bus that is off, mono or stereo.
void testSidechain()
{
    using pangea::fillSidechain;
    using pangea::sidechainChannelsFor;
    using pangea::sidechainInputsOf;
    check (sidechainChannelsFor ("stereo") == 2 && sidechainChannelsFor ("mono") == 1 && sidechainChannelsFor (" stereo ") == 2,
           "declare sidechain says stereo (2 inputs) or mono (1)");
    check (sidechainChannelsFor ("") == 0 && sidechainChannelsFor ("2") == 0 && sidechainChannelsFor ("Stereo") == 0,
           "anything else declares no sidechain");
    check (sidechainInputsOf (2, 4) == 2 && sidechainInputsOf (1, 3) == 1, "the code's inputs are the sound's two and then the sidechain's");
    check (sidechainInputsOf (2, 3) == 0 && sidechainInputsOf (1, 4) == 0 && sidechainInputsOf (0, 4) == 0 && sidechainInputsOf (2, 2) == 0,
           "inputs that do not match the declaration are no sidechain");

    constexpr int n = 4;
    float left[n] = { 1, 2, 3, 4 }, right[n] = { 3, 2, 1, 0 };
    const float* stereoBus[2] = { left, right };
    const float* monoBus[1] = { left };
    float a[n], b[n];
    float* two[2] = { a, b };
    float* one[1] = { a };
    auto equal = [] (const float* x, std::vector<float> y)
    {
        for (size_t i = 0; i < y.size(); ++i)
            if (x[i] != y[i])
                return false;
        return true;
    };
    std::fill (a, a + n, 9.0f);
    std::fill (b, b + n, 9.0f);
    fillSidechain (two, 2, nullptr, 0, n);
    check (equal (a, { 0, 0, 0, 0 }) && equal (b, { 0, 0, 0, 0 }), "a bus that is off feeds silence");
    fillSidechain (two, 2, stereoBus, 2, n);
    check (equal (a, { 1, 2, 3, 4 }) && equal (b, { 3, 2, 1, 0 }), "a stereo bus feeds a stereo sidechain side for side");
    fillSidechain (two, 2, monoBus, 1, n);
    check (equal (a, { 1, 2, 3, 4 }) && equal (b, { 1, 2, 3, 4 }), "a mono bus feeds both sides of a stereo sidechain");
    fillSidechain (one, 1, stereoBus, 2, n);
    check (equal (a, { 2, 2, 2, 2 }), "a stereo bus feeds a mono sidechain the mean of its two sides");
    fillSidechain (one, 1, monoBus, 1, n);
    check (equal (a, { 1, 2, 3, 4 }), "a mono bus feeds a mono sidechain as it is");
}

} // namespace

int main()
{
    testSidechain();
    testHints();
    testNotes();
    testHeard();
    testTransport();
    testBypass();
    if (failures == 0)
        std::printf ("all %d platform checks passed\n", checks);
    else
        std::printf ("%d of %d platform checks failed\n", failures, checks);
    return failures == 0 ? 0 : 1;
}
