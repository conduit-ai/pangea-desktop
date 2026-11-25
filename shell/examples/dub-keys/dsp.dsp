declare name "Dub Keys";
declare version "0.1.0";
declare author "Pangea";
declare description "Eight voice keys into an echo that all the voices share: an instrument with an effect section.";
declare options "[nvoices:8] [midi:on]";
import("stdfaust.lib");

// The house smoother (docs/decisions.md, 54): it starts at the control's own value.
smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

// The standard voice controls. The shell sets these per voice from MIDI.
freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");
// The host's tempo (decision 55). Only the echo reads it, so it lives in the effect section.
bpm = hslider("BPM [pangea:bpm]", 120, 20, 300, 0.01);

// Keys: one voice. Tone is read by the voice and by the echo: one control with two homes.
attack  = hslider("h:[0] Keys/[0] Attack [unit:s] [scale:log] [style:knob]", 0.005, 0.001, 1, 0.001);
release = hslider("h:[0] Keys/[1] Release [unit:s] [scale:log] [style:knob]", 0.4, 0.01, 4, 0.01);
tone    = hslider("h:[0] Keys/[2] Tone [unit:Hz] [scale:log] [style:knob]", 2400, 200, 12000, 1);

// Echo and Level: the effect section, one stage for all the voices.
sync     = checkbox("h:[1] Echo/[0] Sync");
time     = hslider("h:[1] Echo/[1] Time [unit:ms] [scale:log] [style:knob]", 320, 20, 1500, 1) : smoo : max(20);
feedback = hslider("h:[1] Echo/[2] Feedback [style:knob]", 0.45, 0, 0.9, 0.01) : smoo;
mix      = hslider("h:[1] Echo/[3] Mix [style:knob]", 0.3, 0, 1, 0.01) : smoo;
level    = hslider("[2] Level [unit:dB] [style:knob]", -6, -40, 6, 0.1) : smoo : ba.db2linear;

env = en.adsr(attack, 0.25, 0.6, release, gate);
osc = os.triangle(freq) * 0.7 + os.osc(freq * 2) * 0.2 * env;
voice = osc : fi.lowpass(2, tone : si.polySmooth(gate, 0.999, 32) : max(200)) : *(env * gain * 0.5);

process = voice <: _, _;

// With Sync on the echo is a dotted eighth of the host's tempo: three quarters of a beat.
echoMs = select2(sync, time, 45000 / bpm) : smooth_for(0.05) : max(1);
echoSamples = echoMs * ma.SR / 1000 : min(262143);
echoes = (+ : de.fdelay(262144, echoSamples) : fi.lowpass(1, tone : smoo : max(200))) ~ *(feedback);
echo = par(i, 2, _ <: _, (echoes : *(mix)) :> _);

effect = echo : par(i, 2, *(level) : max(-1) : min(1));
