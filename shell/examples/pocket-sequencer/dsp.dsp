import("stdfaust.lib");
declare name "Pocket Sequencer";
declare options "[nvoices:8]";

smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

process = 0, 0;

division = nentry("h:[1]Sequencer/[1]Division [style:menu{'1/16':0;'1/8 T':1;'1/8':2}]", 0, 0, 2, 1);
root = nentry("h:[1]Sequencer/[2]Root [style:menu{'C':0;'D':1;'E':2;'F':3;'G':4;'A':5}]", 5, 0, 5, 1);
rootNote = waveform{36, 38, 40, 41, 43, 45}, int(root) : rdtable;
cutoff = hslider("h:[1]Sequencer/[3]Cutoff [style:knob][unit:Hz][scale:log]", 900, 100, 8000, 1) : smoo : max(100);
res = hslider("h:[1]Sequencer/[4]Resonance [style:knob]", 0.5, 0, 0.9, 0.01) : smoo;
decay = hslider("h:[1]Sequencer/[5]Decay [style:knob]", 0.6, 0.05, 1, 0.01) : smoo : max(0.05);
note(i) = nentry("h:[2]Notes/[%2j]Step %j [style:knob][playhead:0]", ba.take(i + 1, (0, 0, 12, 0, 7, 0, 10, 12, 0, 0, 12, 0, 5, 7, 3, 12)), -12, 24, 1) with { j = i + 1; };
level = hslider("h:[3]Output/[1]Level [style:knob]", 0.6, 0, 1, 0.01) : smoo;

beats = waveform{0.25, 0.3333, 0.5}, int(division) : rdtable;
clock = custom.step_clock(beats, 16, 0, 0);
line(step, phase, running) = os.sawtooth(ba.midikey2hz(rootNote - 12 + (par(i, 16, note(i) * (step == i)) :> _)))
  : ve.moog_vcf_2bn(res, min(12000, cutoff * (1 + 3 * amp))) : *(amp * 0.5 * level)
  with { amp = pow(max(0, 1 - phase / decay), 2) * (phase < 0.95) * running : si.smooth(ba.tau2pole(0.003)); };
bass = clock : line;
light = clock : \(step, phase, running).(select2(running, -1, step)) : vbargraph("h:[2]Notes/Step Playing [playhead:0]", -1, 15);

effect = (attach(_, light), _) : par(i, 2, +(bass) : max(-1) : min(1));

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
