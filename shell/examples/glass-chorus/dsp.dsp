declare name "Glass Chorus";
declare version "0.1.0";
declare author "Pangea";
declare description "A stereo chorus with up to three voices, a wide switch and a wobble button.";
import("stdfaust.lib");

rate   = hslider("h:[0] Motion/[0] Rate [unit:Hz] [scale:log] [style:knob]", 0.6, 0.05, 8, 0.01) : si.smoo;
depth  = hslider("h:[0] Motion/[1] Depth [unit:ms] [style:knob]", 4, 0, 12, 0.1) : si.smoo;
wobble = button("h:[0] Motion/[2] Wobble") : si.smoo;
mix    = hslider("h:[1] Output/[0] Mix [style:knob]", 0.5, 0, 1, 0.01) : si.smoo;
wide   = checkbox("h:[1] Output/[1] Wide") : si.smoo;
voices = nentry("h:[1] Output/[2] Voices", 2, 1, 3, 1);

// Each voice is a delay swept by its own phase of one sine; Wobble speeds and deepens the sweep.
sweep(ph) = os.oscp(rate * (1 + wobble * 3), ph) * 0.5 + 0.5;
tap(ph) = de.fdelay(4096, (2 + depth * (1 + wobble) * sweep(ph)) * ma.SR / 1000);
voice(n, ph) = tap(ph) * (voices >= n);
chorus(off) = _ <: voice(1, off), voice(2, 2.1 + off), voice(3, 4.2 + off) :> /(max(1, voices));
side(off) = _ <: *(1 - mix), (chorus(off) : *(mix)) :> max(-1) : min(1);

process = hgroup("Glass Chorus", side(0), side(wide * 1.3));
