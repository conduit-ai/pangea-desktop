declare name "Tape Drift";
declare version "0.1.0";
declare author "Pangea";
declare description "Stereo tape delay with wow and flutter.";
import("stdfaust.lib");

// Controls. Every control is smoothed so moves never click.
time     = hslider("[0] Time [unit:ms] [scale:log] [style:knob]", 380, 20, 1500, 1) : si.smoo;
feedback = hslider("[1] Feedback [style:knob]", 0.45, 0, 0.95, 0.01) : si.smoo;
tone     = hslider("[2] Tone [unit:Hz] [scale:log] [style:knob]", 4500, 500, 12000, 1) : si.smoo;
wow      = hslider("[3] Wow [style:knob]", 0.3, 0, 1, 0.01) : si.smoo;
flutter  = hslider("[4] Flutter [style:knob]", 0.2, 0, 1, 0.01) : si.smoo;
mix      = hslider("[5] Mix [style:knob]", 0.35, 0, 1, 0.01) : si.smoo;

maxDelay = 131072;   // samples, about 2.7 seconds at 48 kHz

// Wow is a slow drift (0.5 Hz, up to 8 ms), flutter a faster wobble (6.3 Hz, up to 1.5 ms).
// Each channel gets its own phase so the stereo image breathes.
drift(ph) = wow * 8 * os.oscp(0.5, ph) + flutter * 1.5 * os.oscp(6.3, ph + 1);
delaySamples(ph) = (time + drift(ph)) : max(1) : *(ma.SR / 1000) : min(maxDelay - 2);

// One tape loop: a fractional delay with a filtered, gently saturated feedback path.
echo(ph) = (+ : de.fdelay(maxDelay, delaySamples(ph))) ~ (fi.lowpass(1, tone) : fi.dcblocker : ma.tanh : *(feedback));

safe = max(-1) : min(1);

process = hgroup("Tape Drift", _, _ <: (*(1 - mix), *(1 - mix)), (echo(0), echo(0.9) : *(mix), *(mix)) :> safe, safe);
