import("stdfaust.lib");
declare name "Kick Duck";
declare sidechain "stereo";

smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

lows = checkbox("Lows Only");
thresh = hslider("Threshold dB [style:knob][unit:dB]", -30, -60, 0, 0.1) : smoo;
depth = hslider("Depth dB [style:knob][unit:dB]", 18, 0, 40, 0.1) : smoo;
ratio = hslider("Ratio [style:knob]", 6, 1, 20, 0.1) : smoo : max(1);
att = hslider("Attack ms [style:knob][unit:ms][scale:log]", 2, 0.1, 50, 0.1) : smoo : max(0.1);
rel = hslider("Release ms [style:knob][unit:ms][scale:log]", 250, 20, 2000, 1) : smoo : max(20);
outgain = hslider("Output dB [style:knob][unit:dB]", 0, -24, 12, 0.1) : smoo : ba.db2linear;

key(kl, kr) = hgroup("[1]Sidechain", (kl + kr) * 0.5 <: select2(lows, _, fi.lowpass(2, 150)) : abs : /(ba.db2linear(thresh)));
reduction = hgroup("[2]Duck", an.amp_follower_ar(att / 1000, rel / 1000) : max(0.000001) : ba.linear2db
  : max(0) : *(1 - 1 / ratio) : min(depth) : vbargraph("Gain Reduction [unit:dB][meter:gr]", 0, 20));
duck(l, r, kl, kr) = l * g, r * g with { g = key(kl, kr) : reduction : *(-1) : ba.db2linear; };

process = duck : hgroup("[3]Output", par(i, 2, *(outgain) : max(-1) : min(1)));
