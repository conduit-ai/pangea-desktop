import("stdfaust.lib");
declare name "Bar Gate";

smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

division = nentry("h:[1]Clock/[1]Division [style:menu{'1/32':0;'1/16 T':1;'1/16':2;'1/8 T':3;'1/8':4;'1/4':5}]", 2, 0, 5, 1);
clockMode = nentry("h:[1]Clock/[2]Clock [style:menu{'Song':0;'Restart':1;'Free':2}]", 0, 0, 2, 1);
rate = hslider("h:[1]Clock/[3]Free Rate [style:knob][unit:Hz][scale:log]", 8, 0.5, 32, 0.01) : smoo : max(0.5);
level(i) = hslider("h:[2]Pattern/[%2j]Step %j [style:knob][playhead:0]", 1, 0, 1, 0.01) : smoo with { j = i + 1; };
length = hslider("h:[3]Gate/[1]Length [style:knob]", 0.6, 0.05, 1, 0.01) : smoo;
att = hslider("h:[3]Gate/[2]Attack [style:knob][unit:ms][scale:log]", 2, 0.1, 50, 0.01) : smoo : max(0.1);
rel = hslider("h:[3]Gate/[3]Release [style:knob][unit:ms][scale:log]", 30, 0.1, 500, 0.01) : smoo : max(0.1);
depth = hslider("h:[3]Gate/[4]Depth [style:knob]", 1, 0, 1, 0.01) : smoo;

beats = waveform{0.125, 0.1667, 0.25, 0.3333, 0.5, 1}, int(division) : rdtable;
clock = custom.step_clock(beats, 16, clockMode, rate);
open = clock : \(step, phase, running).(select2(running, 1, par(i, 16, level(i) * (step == i)) :> *(phase < length)));
env = (\(e).(e + max(0 - 1000 / (rel * ma.SR), min(1000 / (att * ma.SR), open - e)))) ~ _;
gain = 1 - depth * (1 - env);
light = clock : \(step, phase, running).(select2(running, -1, step)) : vbargraph("h:[2]Pattern/Step Playing [playhead:0]", -1, 15);

process = (attach(_, light), _) : par(i, 2, *(gain) : max(-1) : min(1));

// ---- Custom components, filled in by Pangea: step_clock (from 01a6bc5, check f43160344c2d6625). Change your own code, not this block.
custom = environment {
clk = environment {
bpm = hslider("BPM [pangea:bpm]", 120, 20, 300, 0.01);
playing = hslider("Song Playing [pangea:playing][hidden:1]", 1, 0, 1, 1);
beat = hslider("Song Beat [pangea:beat][hidden:1]", 0, -100000, 1000000, 0.001);
fraction = hslider("Song Beat Fraction [pangea:beatfrac][hidden:1]", -1, -1, 1, 0.000001);

clock(beats, steps, mode, hz) = step, phase, run with {
  Q = 960;
  L = max(1, rint(max(0, min(64, beats)) * Q));
  n = max(1, min(64, int(steps)));
  P = int(L) * n;
  wrap(x) = y + P * (y < 0) - P * (y >= P) with { y = x - P * floor(x / P); };
  free = mode > 1.5;
  restart = (mode > 0.5) * (1 - free);
  on = playing > 0.5;
  run = free | on;
  exact = fraction >= 0;
  whole = select2(exact, floor(beat), rint(beat - fraction));
  part = select2(exact, beat - floor(beat), fraction);
  rise = on * (1 - on');
  w = whole - restart * ba.sAndH(rise, whole);
  p = part - restart * ba.sAndH(rise, part);
  ticks(m) = (((m * 60) % P) * 16) % P;
  anchor = select2(free, wrap(ticks(int(w) % P) + p * Q), 0);
  inc = select2(free, on * max(1, bpm) * Q / (60 * ma.SR), max(0, hz) * L / ma.SR);
  heard = (beat != beat') | (fraction != fraction') | (playing != playing');
  fresh = (1 - 1') | (heard * (1 - free));
  gone(jp, rp) = j, r with {
    again = (jp >= 65536) | (inc != inc') | (P != P');
    next = rp + (jp + 1) * inc';
    j = select2(fresh, select2(again, jp + 1, 0), 0);
    r = select2(fresh, select2(again, rp, select2(free | (jp >= 65536), next, wrap(next))), 0);
  };
  pos = (gone ~ (_, _)) : (\(j, r).(wrap(anchor + r + j * inc + 0.002))) : *(run);
  idx = max(0, min(n - 1, floor(pos / L)));
  step = int(idx);
  phase = min(1, max(0, (pos - idx * L) / L));
};
};
step_clock(beats, steps, mode, hz) = clk.clock(beats, steps, mode, hz);
};
// ---- End of custom components
