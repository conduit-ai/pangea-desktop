declare name "Poly Saw";
declare version "0.1.0";
declare author "Pangea";
declare description "Eight voice sawtooth synth with a resonant low-pass filter.";
declare options "[nvoices:8] [midi:on]";
import("stdfaust.lib");

// The standard voice controls. The shell sets these per voice from MIDI.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// Filter
cutoff = hslider("h:[0] Filter/[0] Cutoff [unit:Hz] [scale:log] [style:knob]", 1800, 40, 16000, 1) : si.smoo;
reso   = hslider("h:[0] Filter/[1] Resonance [style:knob]", 0.35, 0, 0.95, 0.01) : si.smoo;
envAmt = hslider("h:[0] Filter/[2] Env Amount [style:knob]", 0.5, 0, 1, 0.01) : si.smoo;

// Envelope
attack  = hslider("h:[1] Envelope/[0] Attack [unit:s] [scale:log] [style:knob]", 0.005, 0.001, 2, 0.001);
decay   = hslider("h:[1] Envelope/[1] Decay [unit:s] [scale:log] [style:knob]", 0.25, 0.01, 4, 0.01);
sustain = hslider("h:[1] Envelope/[2] Sustain [style:knob]", 0.7, 0, 1, 0.01);
release = hslider("h:[1] Envelope/[3] Release [unit:s] [scale:log] [style:knob]", 0.3, 0.01, 4, 0.01);

// Output
detune = hslider("h:[2] Output/[0] Detune [unit:cent] [style:knob]", 8, 0, 50, 0.1) : si.smoo;
volume = hslider("h:[2] Output/[1] Volume [unit:dB] [style:knob]", -12, -40, 0, 0.1) : ba.db2linear : si.smoo;

env = en.adsr(attack, decay, sustain, release, gate);
spread = pow(2, detune / 1200);
osc = (os.sawtooth(freq) + os.sawtooth(freq * spread) + os.sawtooth(freq / spread)) / 3;
cut = cutoff * pow(2, envAmt * 4 * env) : min(18000);
voice = osc : ve.moog_vcf_2bn(reso, cut) : *(env * gain * volume);

process = vgroup("Poly Saw", voice <: _, _);
