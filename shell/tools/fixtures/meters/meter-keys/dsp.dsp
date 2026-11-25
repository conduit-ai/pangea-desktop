// A test instrument for the meters (decision 76), written by hand. The shell's copy declares eight
// voices, as the shell's own example instruments do, so that a MIDI note plays it (decision 91). It declares two bar
// graphs. "Voice Level" is in the voice: the browser reads a voice's bar graph from one voice
// only, so it is no real meter and no panel draws it. "Level" is in the effect, after the
// voices are mixed: it is the instrument's meter, a plain level in decibels from -60 to 0.
import("stdfaust.lib");
declare name "Meter Keys";
declare options "[nvoices:8] [midi:on]";

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
tone = hslider("h:Keys/Tone [style:knob][unit:Hz][scale:log]", 2000, 200, 8000, 1) : si.smoo;

inDb = abs : si.smooth(ba.tau2pole(0.05)) : max(0.000001) : ba.linear2db : max(-60) : min(0);
voiceLevel = inDb : hbargraph("h:Keys/Voice Level [unit:dB]", -60, 0);
voice = os.sawtooth(freq) * gain * (gate : en.adsr(0.01, 0.1, 0.8, 0.2)) : fi.lowpass(2, tone);
process = voice <: (_ <: attach(_, voiceLevel)), _;

level = hslider("h:Output/Level dB [style:knob][unit:dB]", -6, -60, 6, 0.1) : ba.db2linear : si.smoo;
outLevel = inDb : hbargraph("h:Output/Level [meter:level][unit:dB]", -60, 0);
effect = par(i, 2, *(level) : max(-1) : min(1)) : (_ <: attach(_, outLevel)), _;
