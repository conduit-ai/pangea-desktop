declare name "Stomp Fuzz";
declare version "0.1.0";
declare author "Pangea";
declare description "A fuzz pedal with a tone control and a boost footswitch.";
import("stdfaust.lib");

fuzz  = hslider("[0] Fuzz [style:knob]", 0.6, 0, 1, 0.01) : si.smoo;
tone  = hslider("[1] Tone [unit:Hz] [scale:log] [style:knob]", 2400, 400, 8000, 1) : si.smoo;
level = hslider("[2] Level [unit:dB] [style:knob]", -6, -30, 6, 0.1) : ba.db2linear : si.smoo;
boost = checkbox("[3] Boost") : si.smoo;

// More drive into the clipper when the footswitch is down, a tone filter after it.
drive = 1 + fuzz * 60 * (1 + boost);
channel = fi.dcblocker : *(drive) : ma.tanh : fi.lowpass(2, tone) : *(level) : max(-1) : min(1);

process = hgroup("Stomp Fuzz", channel, channel);
