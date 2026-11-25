// An instrument whose effect section holds the awkward cases, for tools/test-effect.sh.
// It declares no name, on purpose: Faust then names a program's root group after its file, and the
// voice and the effect are two programs. Bright below is one control only if both roots agree.
declare version "0.1.0";
declare author "Pangea";
declare options "[nvoices:4]";
import("stdfaust.lib");

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// 1. One definition that the voice and the effect both read: one parameter with two homes.
bright = hslider("h:Tone/Bright", 0.5, 0, 1, 0.01);
// 2. Two definitions under one key, Trim: the voice's from 0 to 1, the effect's in decibels.
//    They stay two parameters, each with its own range and its own start.
voiceTrim = hslider("Trim", 0.8, 0, 1, 0.01);
effectTrim = hslider("Trim [unit:dB]", -12, -40, 0, 0.1);
// 3. A group the voice and the effect both have, Tone: one group, Damp beside Bright.
damp = hslider("h:Tone/Damp [unit:Hz]", 4000, 200, 12000, 1);
// 4. A label that two controls of the voice have, Level, so the second takes its path as its id.
//    The effect has a Level at that same path, with another range: a third parameter, whose id
//    may be neither of theirs.
oscLevel = hslider("h:Osc/Level", 0.7, 0, 1, 0.01);
subLevel = hslider("h:Sub/Level", 0.3, 0, 1, 0.01);
effectSubLevel = hslider("h:Sub/Level [unit:dB]", -3, -24, 0, 0.1);

envelope = en.adsr(0.005, 0.1, 0.7, 0.2, gate);
tone = os.sawtooth(freq) * oscLevel + os.osc(freq / 2) * subLevel;
voice = tone : fi.lowpass(1, 500 + bright * 6000) : *(envelope * gain * voiceTrim * 0.4);

process = voice <: _, _;
effect = par(i, 2, fi.lowpass(1, damp * (0.5 + bright)) : *(ba.db2linear(effectTrim + effectSubLevel)) : max(-1) : min(1));
