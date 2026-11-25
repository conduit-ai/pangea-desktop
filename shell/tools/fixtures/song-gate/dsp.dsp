import("stdfaust.lib");
declare name "Song Gate";

smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

bpm = hslider("BPM [pangea:bpm]", 120, 20, 300, 0.01);
playing = hslider("Playing [pangea:playing]", 1, 0, 1, 1);
songBeat = hslider("Beat [pangea:beat]", 0, -100000, 1000000, 0.001);
songPhase(len) = wrap(wrap(songBeat) + k * playing * bpm / (60 * ma.SR)) with {
  wrap(x) = x - len * floor(x / len);
  fresh = (songBeat != songBeat') | (playing != playing');
  k = (+(1) : min(16777216) : *(1 - fresh)) ~ _;
};

division = nentry("h:[1]Clock/[1]Division [style:menu{'1/32':0;'1/16 T':1;'1/16':2;'1/8 T':3;'1/8':4;'1/4':5}]", 2, 0, 5, 1);
steps = nentry("h:[1]Clock/[2]Steps", 16, 1, 16, 1);
level(i) = hslider("h:[2]Pattern/[%2j]Step %j [style:knob][playhead:0]", 1, 0, 1, 0.01) : smoo with { j = i + 1; };
length = hslider("h:[3]Gate/[1]Length [style:knob]", 0.5, 0.05, 1, 0.01) : smoo;
att = hslider("h:[3]Gate/[2]Attack ms [style:knob][unit:ms][scale:log]", 1, 0.1, 50, 0.01) : smoo : max(0.1);
rel = hslider("h:[3]Gate/[3]Release ms [style:knob][unit:ms][scale:log]", 20, 0.1, 500, 0.01) : smoo : max(0.1);
depth = hslider("h:[3]Gate/[4]Depth [style:knob]", 1, 0, 1, 0.01) : smoo;

beats = waveform{0.125, 0.1667, 0.25, 0.3333, 0.5, 1}, int(division) : rdtable;
pos = songPhase(steps * beats) / beats;
idx = int(pos);
open = par(i, 16, level(i) * (idx == i)) :> _ : *(pos - idx < length);
target = select2(playing, 1, open);
env = (\(e).(e + max(0 - 1000 / (rel * ma.SR), min(1000 / (att * ma.SR), target - e)))) ~ _;
gain = 1 - depth * (1 - env);
light = select2(playing, -1, idx) : vbargraph("h:[2]Pattern/Step Playing [playhead:0]", -1, 15);

process = (attach(_, light), _) : par(i, 2, *(gain) : max(-1) : min(1));
