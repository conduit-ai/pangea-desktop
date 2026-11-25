import("stdfaust.lib");
declare name "Step Drive Filter";

smoo = smooth_for(0.02);
smooth_for(t, x) = x - ((x - x') * 1' : (+ : *(ba.tau2pole(t)) : tiny) ~ _) with { tiny(e) = e * (abs(e) > 1e-30); };

bpm = hslider("BPM [pangea:bpm]", 120, 20, 300, 0.01);

inTrim = hslider("h:[1]Input/[1]Input Trim dB [style:knob][unit:dB]", 0, -24, 24, 0.1) : smoo : ba.db2linear;

driveBy = checkbox("h:[2]Drive/Drive Bypass [place:header]");
drive = hslider("h:[2]Drive/[1]Drive dB [style:knob][size:l][unit:dB]", 12, 0, 36, 0.1) : smoo;
bias = hslider("h:[2]Drive/[2]Drive Bias [style:knob][size:s]", 0.15, 0, 0.5, 0.01) : smoo;
dtone = hslider("h:[2]Drive/[3]Drive Tone [style:knob][unit:Hz][scale:log]", 7000, 1000, 16000, 1) : smoo : max(1000);
dpos = nentry("h:[2]Drive/[4]Drive Position [style:menu{'Pre Filter':0;'Post Filter':1}]", 0, 0, 1, 1);

cutoff = hslider("h:[3]Filter/[1]Filter Cutoff [style:knob][size:l][unit:Hz][scale:log]", 400, 40, 12000, 1) : smoo : max(40);
res = hslider("h:[3]Filter/[2]Filter Resonance [style:knob]", 0.5, 0, 0.95, 0.01) : smoo;
fmode = nentry("h:[3]Filter/[3]Filter Mode [style:menu{'Low Pass':0;'Band Pass':1;'High Pass':2}]", 0, 0, 2, 1);

seqBy = checkbox("v:[4]Sequencer/Seq Bypass [place:header]");
st1 = vslider("v:[4]Sequencer/h:[1]Pattern/[1]Step 1 [size:s][playhead:0]", 0.2, 0, 1, 0.01) : smoo;
st2 = vslider("v:[4]Sequencer/h:[1]Pattern/[2]Step 2 [size:s][playhead:0]", 0.8, 0, 1, 0.01) : smoo;
st3 = vslider("v:[4]Sequencer/h:[1]Pattern/[3]Step 3 [size:s][playhead:0]", 0.4, 0, 1, 0.01) : smoo;
st4 = vslider("v:[4]Sequencer/h:[1]Pattern/[4]Step 4 [size:s][playhead:0]", 1, 0, 1, 0.01) : smoo;
st5 = vslider("v:[4]Sequencer/h:[1]Pattern/[5]Step 5 [size:s][playhead:0]", 0.1, 0, 1, 0.01) : smoo;
st6 = vslider("v:[4]Sequencer/h:[1]Pattern/[6]Step 6 [size:s][playhead:0]", 0.6, 0, 1, 0.01) : smoo;
st7 = vslider("v:[4]Sequencer/h:[1]Pattern/[7]Step 7 [size:s][playhead:0]", 0.3, 0, 1, 0.01) : smoo;
st8 = vslider("v:[4]Sequencer/h:[1]Pattern/[8]Step 8 [size:s][playhead:0]", 0.9, 0, 1, 0.01) : smoo;
sync = checkbox("v:[4]Sequencer/h:[2]Clock/[1]Seq Sync");
division = nentry("v:[4]Sequencer/h:[2]Clock/[2]Seq Division [style:menu{'1/32':0;'1/16':1;'1/8 T':2;'1/8':3;'1/4':4}]", 1, 0, 4, 1);
rate = hslider("v:[4]Sequencer/h:[2]Clock/[3]Seq Rate [style:knob][unit:Hz][scale:log]", 8, 0.5, 30, 0.01) : smoo : max(0.5);
depth = hslider("v:[4]Sequencer/h:[2]Clock/[4]Seq Depth [style:knob][size:l][unit:oct]", 3, 0, 6, 0.01) : smoo;
glide = hslider("v:[4]Sequencer/h:[2]Clock/[5]Seq Glide [style:knob][size:s][unit:ms][scale:log]", 20, 1, 500, 0.1) : smoo : max(1);

cin = hslider("h:[5]Comp/v:[1]Gain/[1]Comp Input [style:knob][unit:dB]", -26, -60, 0, 0.1) : smoo;
cout = hslider("h:[5]Comp/v:[1]Gain/[2]Comp Output [style:knob][unit:dB]", -24, -60, 0, 0.1) : smoo;
catt = hslider("h:[5]Comp/v:[2]Timing/[1]Comp Attack [style:knob][size:s]", 3, 0, 7, 0.01) : smoo;
crel = hslider("h:[5]Comp/v:[2]Timing/[2]Comp Release [style:knob][size:s]", 5, 1, 7, 0.01) : smoo;
cratio = nentry("h:[5]Comp/[3]Comp Ratio [style:menu{'4:1':0;'8:1':1;'12:1':2;'20:1':3;'All':4}]", 0, 0, 4, 1);
cmix = hslider("h:[5]Comp/[4]Comp Mix [style:knob][size:l]", 0.7, 0, 1, 0.01) : smoo;
compBy = checkbox("h:[5]Comp/Comp Bypass [place:header]");
grMeter = hbargraph("h:[5]Comp/[5]Gain Reduction [meter:gr][unit:dB]", 0, 20);

outGain = hslider("h:[6]Output/[1]Output dB [style:knob][unit:dB]", 0, -24, 12, 0.1) : smoo : ba.db2linear;

lvl = an.amp_follower_ar(0.002, 0.3) : max(0.000001) : ba.linear2db : max(-60);
inMeter = (_ <: attach(_, lvl : vbargraph("h:[1]Input/[2]In L [unit:dB]", -60, 6))), (_ <: attach(_, lvl : vbargraph("h:[1]Input/[3]In R [unit:dB]", -60, 6)));
outMeter = (_ <: attach(_, lvl : vbargraph("h:[6]Output/[2]Out L [unit:dB]", -60, 6))), (_ <: attach(_, lvl : vbargraph("h:[6]Output/[3]Out R [unit:dB]", -60, 6)));

beats = waveform{0.125, 0.25, 0.3333, 0.5, 1}, int(division) : rdtable;
stepHz = select2(sync, rate, bpm / 60 / beats) : smooth_for(0.05) : max(0.1);
ph = os.lf_sawpos(stepHz / 8);
idx = min(7, int(ph * 8));
seqRaw = st1 * (idx == 0) + st2 * (idx == 1) + st3 * (idx == 2) + st4 * (idx == 3) + st5 * (idx == 4) + st6 * (idx == 5) + st7 * (idx == 6) + st8 * (idx == 7);
seqv = seqRaw : smooth_for(glide / 1000);
steps = select2(seqBy, idx, -1) : vbargraph("v:[4]Sequencer/h:[1]Pattern/[9]Step Playing [playhead:0]", -1, 7);
fc = cutoff * pow(2, depth * (1 - seqBy) * seqv) : min(18000);

lp = ve.moog_vcf_2bn(res, fc);
bp = fi.resonbp(fc, 0.7 + res * 12, 1);
hp = fi.resonhp(fc, 0.7 + res * 8, 1);
filt = _ <: lp, bp, hp : select3(fmode);

g = ba.db2linear(drive);
sat = fi.high_shelf(3, 2500) : *(g) : +(bias) : aa.tanh1 : -(ma.tanh(bias)) : fi.dcblocker : *(1 / sqrt(g)) : fi.high_shelf(-3, 2500) : fi.peak_eq(1.5, 120, 100) : fi.lowpass(1, dtone);
drv = _ <: sat, _ : select2(driveBy);
chain = _ <: (drv : filt), (filt : drv) : select2(dpos);

m = cmix * (1 - compBy);
comp = _,_ <: (custom.fet76_stereo(cin, cout, catt, crel, cratio) : \(l, r, gr, vu).(attach(l, gr : grMeter), r)), _, _ : \(wl, wr, dl, dr).(wl * m + dl * (1 - m), wr * m + dr * (1 - m));

process = par(i, 2, *(inTrim)) : (attach(_, steps), _) : inMeter : par(i, 2, chain) : comp : par(i, 2, *(outGain) : max(-1) : min(1)) : outMeter;

// ---- Custom components, filled in by Pangea: fet76_stereo (from 01a6bc5, check b47afa38cf7b401c). Change your own code, not this block.
custom = environment {
tune = environment {
KNEE_DB = 9.6293;
CELL_DEPTH_DB = 45.0;
REST_DROP_DB = 1.0;
LN_RESIDUAL = 0.0;
TAP_GAIN = 1.0;
STAGE_H2 = 0.0828;
STAGE_H2_PER_DB = 0.00947;
STAGE_H3 = 1.8669;
STAGE_H3_PER_DB = 0.05261;
STAGE_H3_FLOOR = 0.25;
OUTPUT_HP_HZ = 7.14;
DETECTOR_SKEW_DB = 0.9187;
SIDECHAIN_4 = 4.9358;
SIDECHAIN_8 = 9.6281;
SIDECHAIN_12 = 16.2792;
SIDECHAIN_20 = 29.9474;
THRESHOLD_4_DB = -37.2297;
THRESHOLD_8_STEP_DB = 3.302;
THRESHOLD_12_STEP_DB = 5.0695;
THRESHOLD_20_STEP_DB = 6.9853;
ATTACK_TC_1_US = 2391.0;
ATTACK_TC_2P5_US = 1245.1;
ATTACK_TC_4_US = 812.1;
ATTACK_TC_5P5_US = 490.7;
ATTACK_TC_7_US = 132.9;
RELEASE_TC_1_MS = 958.03;
RELEASE_TC_2P5_MS = 815.73;
RELEASE_TC_4_MS = 527.34;
RELEASE_TC_5P5_MS = 207.24;
RELEASE_TC_7_MS = 68.02;
RELEASE_SCALE_4 = 1.1333;
RELEASE_SCALE_8 = 1.0568;
RELEASE_SCALE_12 = 1.0;
MEMORY_SHARE = 1.2149;
MEMORY_MS = 2213.7;
MEMORY_SENSE = 0.2488;
ALL_SIDECHAIN = 13.9826;
ALL_THRESHOLD_DB = -39.1079;
ALL_DEAD_ZONE = 72.02;
ALL_REST_GAIN_DB = 1.2;
ALL_RELEASE_SCALE = 0.5105;
ALL_ATTACK_SCALE = 0.6761;
METER_MAX_DB = 20.0;
OS_FACTOR = 4;
all = KNEE_DB, CELL_DEPTH_DB, REST_DROP_DB, LN_RESIDUAL, TAP_GAIN,
      STAGE_H2, STAGE_H2_PER_DB, STAGE_H3, STAGE_H3_PER_DB, STAGE_H3_FLOOR, OUTPUT_HP_HZ, DETECTOR_SKEW_DB,
      SIDECHAIN_4, SIDECHAIN_8, SIDECHAIN_12, SIDECHAIN_20, THRESHOLD_4_DB,
      THRESHOLD_8_STEP_DB, THRESHOLD_12_STEP_DB, THRESHOLD_20_STEP_DB, ATTACK_TC_1_US, ATTACK_TC_2P5_US,
      ATTACK_TC_4_US, ATTACK_TC_5P5_US, ATTACK_TC_7_US, RELEASE_TC_1_MS, RELEASE_TC_2P5_MS,
      RELEASE_TC_4_MS, RELEASE_TC_5P5_MS, RELEASE_TC_7_MS, RELEASE_SCALE_4, RELEASE_SCALE_8,
      RELEASE_SCALE_12, MEMORY_SHARE, MEMORY_MS, MEMORY_SENSE, ALL_SIDECHAIN, ALL_THRESHOLD_DB, ALL_DEAD_ZONE, ALL_REST_GAIN_DB, ALL_RELEASE_SCALE, ALL_ATTACK_SCALE,
      METER_MAX_DB, OS_FACTOR;
sidechain(sel) = ba.selectn(4, sel, SIDECHAIN_4, SIDECHAIN_8, SIDECHAIN_12, SIDECHAIN_20);
threshold_db(sel) = ba.selectn(4, sel, THRESHOLD_4_DB, THRESHOLD_4_DB + THRESHOLD_8_STEP_DB,
                               THRESHOLD_4_DB + THRESHOLD_12_STEP_DB, THRESHOLD_4_DB + THRESHOLD_20_STEP_DB);
release_scale(sel) = ba.selectn(4, sel, RELEASE_SCALE_4, RELEASE_SCALE_8, RELEASE_SCALE_12, 1.0);
mark_tc(v1, v2, v3, v4, v5, mark) = exp(log(v1) + log(v2 / v1) * seg(0) + log(v3 / v2) * seg(1) + log(v4 / v3) * seg(2) + log(v5 / v4) * seg(3))
with {
    u = (max(1.0, min(7.0, mark)) - 1.0) / 1.5;
    seg(i) = max(0.0, min(1.0, u - i));
};
attack_us(mark) = mark_tc(ATTACK_TC_1_US, ATTACK_TC_2P5_US, ATTACK_TC_4_US, ATTACK_TC_5P5_US, ATTACK_TC_7_US, mark);
release_ms(mark) = mark_tc(RELEASE_TC_1_MS, RELEASE_TC_2P5_MS, RELEASE_TC_4_MS, RELEASE_TC_5P5_MS, RELEASE_TC_7_MS, mark);
INPUT_TAPER_DB = waveform{-28.535, -28.023, -27.507, -26.973, -26.19, -25.386, -24.589, -23.7, -22.79, -21.908, -19.555, -17.066, -14.19, -12.806, -11.361, -9.82, -5.755, -2.515, 0.0, 2.6, 4.369, 5.37, 6.438, 7.586, 8.84, 11.615, 13.08, 13.3, 13.34, 13.37, 13.39};
OUTPUT_TAPER_DB = waveform{-68.054, -65.476, -62.882, -60.197, -51.932, -43.368, -34.892, -31.425, -28.318, -25.306, -22.369, -19.498, -16.18, -10.758, -5.862, -1.36, 2.651, 6.405, 10.0, 13.326, 16.269, 18.91, 21.369, 23.629, 25.75, 27.47, 28.966, 30.28, 31.207, 31.71, 31.81};
THRESHOLD_TRIM_4_DB = waveform{0.391, 0.356, 0.568, 0.815, 1.102, 0.534, 0.492, 0.703, 0.951, 1.158, 0.39, 0.326, 0.495, 0.772, 0.882, -0.178, -0.197, 0.022, 0.336, 0.56, -1.003, -0.971, -0.616, -0.205, -0.03};
THRESHOLD_TRIM_8_DB = waveform{0.308, 0.076, 0.276, 0.667, 0.825, 0.409, 0.351, 0.478, 0.78, 0.98, 0.274, 0.23, 0.36, 0.609, 0.84, -0.202, -0.214, -0.012, 0.22, 0.505, -0.913, -0.889, -0.572, -0.188, 0.014};
THRESHOLD_TRIM_12_DB = waveform{0.184, 0.147, 0.25, 0.476, 0.729, 0.29, 0.251, 0.373, 0.587, 0.867, 0.172, 0.148, 0.271, 0.431, 0.717, -0.196, -0.194, -0.018, 0.205, 0.394, -0.824, -0.795, -0.52, -0.18, 0.03};
THRESHOLD_TRIM_20_DB = waveform{0.078, 0.056, 0.144, 0.257, 0.513, 0.164, 0.144, 0.241, 0.362, 0.609, 0.055, 0.049, 0.138, 0.267, 0.464, -0.244, -0.245, -0.095, 0.107, 0.222, -0.711, -0.688, -0.466, -0.183, 0.01};
THRESHOLD_TRIM_ALL_DB = waveform{-0.37, -0.531, -0.118, 0.417, 0.644, -0.007, -0.212, 0.222, 0.845, 1.312, -0.188, -0.339, -0.034, 0.572, 0.995, -0.99, -1.036, -0.585, -0.029, 0.41, -2.109, -2.105, -1.516, -0.718, -0.258};
taper(tbl, mark) = a + (b - a) * (u - i)
with {
    u = (max(-60.0, min(0.0, mark)) + 60.0) / 2.0;
    i = min(29, int(floor(u)));
    a = tbl, i : rdtable;
    b = tbl, i + 1 : rdtable;
};
input_gain_db(mark) = taper(INPUT_TAPER_DB, mark);
output_gain_db(mark) = taper(OUTPUT_TAPER_DB, mark);
trim_at(tbl, am, rm) = lerp(lerp(v(i, j), v(i, j + 1), ur - j), lerp(v(i + 1, j), v(i + 1, j + 1), ur - j), ua - i)
with {
    ua = (max(1.0, min(7.0, am)) - 1.0) / 1.5;
    ur = (max(1.0, min(7.0, rm)) - 1.0) / 1.5;
    i = min(3, int(floor(ua)));
    j = min(3, int(floor(ur)));
    v(a, b) = tbl, a * 5 + b : rdtable;
    lerp(p, q, f) = p + (q - p) * f;
};
threshold_trim_db(sel, all, am, rm) = select2(all > 0.5,
    ba.selectn(4, sel, trim_at(THRESHOLD_TRIM_4_DB, am, rm), trim_at(THRESHOLD_TRIM_8_DB, am, rm), trim_at(THRESHOLD_TRIM_12_DB, am, rm), trim_at(THRESHOLD_TRIM_20_DB, am, rm)),
    trim_at(THRESHOLD_TRIM_ALL_DB, am, rm));
};
f76s = environment {
limiter(N, input, output, attack, release, ratio) = f76.limiter2(N, input, output, attack, release, ratio);
};
f76 = environment {
meter_db(c) = min(tune.METER_MAX_DB, fet.limiting(tune.KNEE_DB, fet.control_max(tune.KNEE_DB, tune.CELL_DEPTH_DB), c));
vu(y) = 10.0 * log10(max(2.0 * (y * y : pg.smooth_ms(300.0)), 1.0e-12)) + 18.0;
limiter(N, input, output, attack, release, ratio) = *(pg.db2lin(tune.input_gain_db(input))) : loop : finish
with {
    all = ratio > 3.5;
    sel = min(3, ratio);
    on = attack >= 0.5;
    att = tune.attack_us(max(1.0, attack)) * 1.0e-6;
    rel = tune.release_ms(release) * 1.0e-3;
    loop = ab.loop(N, sel, all, att, rel, on, tune.threshold_trim_db(sel, all, max(1.0, attack), release));
    finish(f, v, m) = y, meter_db(f + m), vu(y)
    with {
        y = v : st.output_stage(tune.OUTPUT_HP_HZ, tune.output_gain_db(output));
    };
};
vu2(a, b) = 10.0 * log10(max((a * a + b * b) : pg.smooth_ms(300.0), 1.0e-12)) + 18.0;
limiter2(N, input, output, attack, release, ratio) = par(i, 2, *(pg.db2lin(tune.input_gain_db(input)))) : loop : finish
with {
    all = ratio > 3.5;
    sel = min(3, ratio);
    on = attack >= 0.5;
    att = tune.attack_us(max(1.0, attack)) * 1.0e-6;
    rel = tune.release_ms(release) * 1.0e-3;
    loop = ab.loop2(N, sel, all, att, rel, on, tune.threshold_trim_db(sel, all, max(1.0, attack), release));
    finish(f, v1, v2, m) = y1, y2, meter_db(f + m), vu2(y1, y2)
    with {
        y1 = v1 : st.output_stage(tune.OUTPUT_HP_HZ, tune.output_gain_db(output));
        y2 = v2 : st.output_stage(tune.OUTPUT_HP_HZ, tune.output_gain_db(output));
    };
};
};
pg = environment {
MAX_SR = 192000;
first = 1 - 1';
clamp(lo, hi) = max(lo) : min(hi);
safe = \(x).(select2(abs(x) < 1.0e20, 0.0, x));
flush = \(x).(select2(abs(x) > 1.0e-30, 0.0, x));
db2lin(d) = pow(10.0, d * 0.05);
lin2db(x) = 20.0 * log10(max(abs(x), 1.0e-12));
map_lin(lo, hi) = *(hi - lo) : +(lo);
map_log(lo, hi) = \(n).(lo * pow(hi / lo, n));
tau2coef(tau) = select2(tau > 0.0, 0.0, exp(-1.0 / (max(tau, 1.0e-9) * ma.SR)));
tau2coef_sr(tau, sr) = select2(tau > 0.0, 0.0, exp(-1.0 / (max(tau, 1.0e-9) * sr)));
smooth_coef(c) = \(x).(x + ((\(e).(flush(c * (e + x' - x)))) ~ _));
smooth_ms(ms) = smooth_coef(tau2coef(ms * 0.001) * (1 - first));
smooth_ms_sr(ms, sr) = smooth_coef(tau2coef_sr(ms * 0.001, sr) * (1 - first));
smooth_log_ms(ms) = max(1.0e-12) : log : smooth_ms(ms) : exp;
param(ms, lo, hi) = smooth_ms(ms) : clamp(lo, hi);
rect_full = abs;
rect_half = max(0.0);
env_ar(att, rel) = abs : \(x).(x + ((\(e).(flush(select2(x > (x' + e), cr, ca) * (e + x' - x)))) ~ _))
with {
    ca = tau2coef(att);
    cr = tau2coef(rel);
};
env_rms(tau) = \(x).(x * x) : smooth_coef(tau2coef(tau)) : max(0.0) : sqrt;
reduce(op, 1) = _;
reduce(op, 2) = op;
reduce(op, n) = (op, si.bus(n - 2)) : reduce(op, n - 1);
parallel_max(n) = reduce(max, n);
parallel_min(n) = reduce(min, n);
parallel_sum(n) = reduce(+, n);
sliding_reduce(op, ident, K, w) = levels : par(i, K + 1, part(i)) : reduce(op, K + 1)
with {
    stage(i) = _ <: _, (_ <: _, @(int(2 ^ i)) : op);
    levels = stage(0) : seq(i, K - 1, (si.bus(i + 1), stage(i + 1)));
    wi = int(max(1, min(w, 2 ^ K)));
    bit(i) = (wi >> i) & 1;
    off(i) = (wi >> (i + 1)) << (i + 1);
    part(i) = \(l).(select2(bit(i), ident, l @ max(0, min(int(2 ^ K), off(i)))));
};
sliding_max(K, w) = sliding_reduce(max, -1.0e30, K, w);
sliding_min(K, w) = sliding_reduce(min, 1.0e30, K, w);
sliding_sum(K, w) = sliding_reduce(+, 0.0, K, w);
sliding_mean(K, w) = sliding_sum(K, w) / max(1, min(w, 2 ^ K));
sliding_rms(K, w) = \(x).(x * x) : sliding_mean(K, w) : max(0.0) : sqrt;
lcg(seed) = (\(s).(s * 1103515245 + 12345 + int(seed) * first)) ~ _;
noise(seed) = lcg(seed) : float : /(2147483648.0);
lane_trigger = \(c).(c != c');
ap1(a) = \(x).((\(ym).(flush(a * (x - ym) + x'))) ~ _);
hb_path0 = ap1(0.035832788431062107) : ap1(0.2720401433964576) : ap1(0.57205719723570025) : ap1(0.82712476199732399);
hb_path1 = ap1(0.1340901419430669) : ap1(0.42432487127186852) : ap1(0.70629214213863944) : ap1(0.94150309417375511);
up2 = _ <: hb_path0, hb_path1;
down2 = \(s0, s1).(0.5 * (hb_path0(s1) + hb_path1(s0)));
os2(f) = up2 : (f, f) : down2;
serial(S, 1, step) = step;
serial(S, N, step) = (step, si.bus(N - 1))
    : (si.bus(S), route(N, N, (1, N), par(i, N - 1, (i + 2, i + 1))))
    : (serial(S, N - 1, step), _)
    : (si.bus(S), route(N, N, (N, 1), par(i, N - 1, (i + 1, i + 2))));
serialk(S, K, 1, step) = step;
serialk(S, K, N, step) = (step, si.bus((N - 1) * K))
    : (si.bus(S), route(N * K, N * K, par(i, K, (i + 1, (N - 1) * K + i + 1)), par(i, (N - 1) * K, (K + i + 1, i + 1))))
    : (serialk(S, K, N - 1, step), si.bus(K))
    : (si.bus(S), route(N * K, N * K, par(i, K, ((N - 1) * K + i + 1, i + 1)), par(i, (N - 1) * K, (i + 1, K + i + 1))));
};
fet = environment {
control_max(gk, max_db) = select2(max_db < gk, max_db + gk, sqrt(4.0 * gk * max(0.0, max_db)));
limiting(gk, cmax, c) = select2(cc < 2.0 * gk, cc - gk, cc * cc / (4.0 * gk))
with {
    cc = max(0.0, min(cmax, c));
};
control_for(gk, lim_db) = select2(l < gk, l + gk, sqrt(4.0 * gk * l))
with {
    l = max(0.0, lim_db);
};
factor(rest_db, lim_db) = pg.db2lin(rest_db + lim_db);
cell_f(r, F, x) = x / (1.0 + (F - 1.0) * max(0.0, 1.0 + r * (x / F)));
cell(gk, max_db, rest_db, r, c, x) = cell_f(r, factor(rest_db, limiting(gk, control_max(gk, max_db), c)), x);
};
st = environment {
k2_at(k2, k2db, lim) = k2 - k2db * lim;
k3_at(k3, k3db, k3min, lim) = max(k3min, k3 - k3db * lim);
stage(k2, k2db, k3, k3db, k3min, lim, v) = sv + k2_at(k2, k2db, lim) * sv * sv
with {
    sv = v / sqrt(1.0 + 2.0 * k3_at(k3, k3db, k3min, lim) * v * v);
};
stage_level_db(k2, k2db, k3, k3db, k3min, lim, v) = 20.0 * log10(max((1.0 + k2_at(k2, k2db, lim) * v / rt) / rt, 1.0e-6))
with {
    rt = sqrt(1.0 + 2.0 * k3_at(k3, k3db, k3min, lim) * v * v);
};
output_stage(hp_hz, gain_db) = svf.hp(hp_hz, 0.70710678) : *(pg.db2lin(gain_db));
};
svf = environment {
svf(fc, q) = tick ~ (_, _) : (!, !, _, _, _)
with {
    fcc = max(5.0, min(fc, 0.45 * ma.SR));
    g = tan(ma.PI * fcc / ma.SR);
    k = 1.0 / max(q, 0.01);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    tick(ic1, ic2, x) = pg.flush(2.0 * v1 - ic1), pg.flush(2.0 * v2 - ic2), v2, v1, x - k * v1 - v2
    with {
        v3 = x - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
    };
};
lp(fc, q) = svf(fc, q) : (_, !, !);
bp(fc, q) = svf(fc, q) : (!, _, !);
hp(fc, q) = svf(fc, q) : (!, !, _);
bp_norm(fc, q) = bp(fc, q) / max(q, 0.01);
notch(fc, q) = svf(fc, q) : (_, !, _) :> _;
allpass(fc, q) = _ <: _, bp(fc, q) : \(x, b).(x - 2.0 * b / max(q, 0.01));
peak(fc, q) = svf(fc, q) : (_, !, _) : -;
lp_bp_hp(fc, q) = svf(fc, q) : (_, /(max(q, 0.01)), _);
};
ab = environment {
sidechain(sel, all) = select2(all > 0.5, tune.sidechain(sel), tune.ALL_SIDECHAIN);
threshold_db(sel, all) = select2(all > 0.5, tune.threshold_db(sel), tune.ALL_THRESHOLD_DB);
dead_zone(all) = select2(all > 0.5, 0.0, tune.ALL_DEAD_ZONE);
rest_db(all) = select2(all > 0.5, tune.REST_DROP_DB, tune.REST_DROP_DB - tune.ALL_REST_GAIN_DB);
attack_scale(all) = select2(all > 0.5, 1.0, tune.ALL_ATTACK_SCALE);
release_scale(sel, all) = select2(all > 0.5, tune.release_scale(sel), tune.ALL_RELEASE_SCALE);
loop(N, sel, all, att, rel, on, trim) =
    pdr.loop(N, tune.KNEE_DB, tune.CELL_DEPTH_DB, rest_db(all), tune.LN_RESIDUAL, tune.TAP_GAIN,
             det.detector_gain(sidechain(sel, all), tune.MEMORY_SHARE, tune.MEMORY_SENSE), threshold_db(sel, all) - trim, tune.MEMORY_SENSE,
             att * attack_scale(all), rel * release_scale(sel, all), on,
             tune.STAGE_H2, tune.STAGE_H2_PER_DB, tune.STAGE_H3, tune.STAGE_H3_PER_DB, tune.STAGE_H3_FLOOR, tune.DETECTOR_SKEW_DB,
             dead_zone(all), tune.MEMORY_SHARE, tune.MEMORY_MS * 1.0e-3);
loop2(N, sel, all, att, rel, on, trim) =
    pdr.loop2(N, tune.KNEE_DB, tune.CELL_DEPTH_DB, rest_db(all), tune.LN_RESIDUAL, tune.TAP_GAIN,
              det.detector_gain(sidechain(sel, all), tune.MEMORY_SHARE, tune.MEMORY_SENSE), threshold_db(sel, all) - trim, tune.MEMORY_SENSE,
              att * attack_scale(all), rel * release_scale(sel, all), on,
              tune.STAGE_H2, tune.STAGE_H2_PER_DB, tune.STAGE_H3, tune.STAGE_H3_PER_DB, tune.STAGE_H3_FLOOR, tune.DETECTOR_SKEW_DB,
              dead_zone(all), tune.MEMORY_SHARE, tune.MEMORY_MS * 1.0e-3);
};
det = environment {
charge_coef(tau, fs) = select2(z < 0.02, exp(z) - 1.0, z * (1.0 + z * (0.5 + z * 0.16666667)))
with {
    z = 1.0 / (max(tau, 1.0e-7) * fs);
};
release_coef(tau, fs) = z / (1.0 + z)
with {
    z = 1.0 / (max(tau, 1.0e-7) * fs);
};
detector_gain(K, mu, lam) = K * (1.0 + lam * mu) / (1.0 + mu);
drive_db(rest_db, G, T, k2, k2db, k3, k3db, k3min, skew, gp, lp, x) =
    20.0 * log10(max(Gg * abs(xb), 1.0e-30)) - T - skew * (xb > 0.0) + st.stage_level_db(k2, k2db, k3, k3db, k3min, lp, Gg * xb * gp)
with {
    xb = max(-1.0e15, min(1.0e15, x));
    Gg = G * pg.db2lin(0.0 - rest_db);
};
charge(gk, cmax, kd, lam, al, kr, on, dead, m, fp, D) = f, g, l
with {
    be = kr / (1.0 - kr);
    b = 1.0 + al + be;
    foff = fp - max(0.0, kr * fp);
    charging = (on > 0.5) & (D > 0.0) & (kd * (D - fet.limiting(gk, cmax, foff + m - dead)) > foff + lam * m);
    aK = al * kd;
    q = fp + (b - al * lam) * m + aK * max(D, 0.0);
    qd = max(0.0, q - b * dead);
    wq = 2.0 * qd / (b + sqrt(b * b + aK / gk * qd));
    wl = (qd + aK * gk) / (b + aK);
    w = select2(wq > 2.0 * gk, wq, wl) : min(cmax);
    u = select2(q > b * dead, q / b, w + dead);
    f = select2(charging, foff, max(foff, u - m)) : max(0.0);
    l = fet.limiting(gk, cmax, f + m - dead);
    g = pg.db2lin(0.0 - l);
};
tap(rest_db, r, G, k2, k2db, k3, k3db, k3min, g, l, x) = st.stage(k2, k2db, k3, k3db, k3min, l, G * fet.cell_f(r, pg.db2lin(rest_db) / g, x));
step(gk, cmax, rest_db, r, G, kd, T, lam, al, kr, on, k2, k2db, k3, k3db, k3min, skew, dead, m, fp, gp, lp, x) = f, g, l, v
with {
    fgl = charge(gk, cmax, kd, lam, al, kr, on, dead, m, fp, drive_db(rest_db, G, T, k2, k2db, k3, k3db, k3min, skew, gp, lp, x));
    f = fgl : (_, !, !);
    g = fgl : (!, _, !);
    l = fgl : (!, !, _);
    v = tap(rest_db, r, G, k2, k2db, k3, k3db, k3min, g, l, x);
};
step2(gk, cmax, rest_db, r, G, kd, T, lam, al, kr, on, k2, k2db, k3, k3db, k3min, skew, dead, m, fp, gp, lp, x1, x2) = f, g, l, v1, v2
with {
    D = max(drive_db(rest_db, G, T, k2, k2db, k3, k3db, k3min, skew, gp, lp, x1), drive_db(rest_db, G, T, k2, k2db, k3, k3db, k3min, skew, gp, lp, x2));
    fgl = charge(gk, cmax, kd, lam, al, kr, on, dead, m, fp, D);
    f = fgl : (_, !, !);
    g = fgl : (!, _, !);
    l = fgl : (!, !, _);
    v1 = tap(rest_db, r, G, k2, k2db, k3, k3db, k3min, g, l, x1);
    v2 = tap(rest_db, r, G, k2, k2db, k3, k3db, k3min, g, l, x2);
};
loop_m(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead) = \(m, x).(x : ovs.ups(N) : core(m) : (_, !, !, ovs.downs(N)))
with {
    fs = ma.SR * N;
    al = charge_coef(att, fs);
    kr = release_coef(rel, fs);
    cmax = fet.control_max(gk, max_db);
    one(m, fp, gp, lp, x) = step(gk, cmax, rest_db, r, G, kd, T, lam, al, kr, on, k2, k2db, k3, k3db, k3min, skew, dead, m, fp, gp, lp, x);
    core(m) = pg.serial(3, N, one(m)) ~ (pg.flush, pg.flush, pg.flush);
};
loop(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead) = (0.0, _) : loop_m(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead);
loop_m2(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead) =
    \(m, x1, x2).((x1 : ovs.ups(N)), (x2 : ovs.ups(N)) : weave : core(m) : (_, !, !, unweave) : (_, ovs.downs(N), ovs.downs(N)))
with {
    fs = ma.SR * N;
    al = charge_coef(att, fs);
    kr = release_coef(rel, fs);
    cmax = fet.control_max(gk, max_db);
    one(m, fp, gp, lp, x1, x2) = step2(gk, cmax, rest_db, r, G, kd, T, lam, al, kr, on, k2, k2db, k3, k3db, k3min, skew, dead, m, fp, gp, lp, x1, x2);
    core(m) = pg.serialk(3, 2, N, one(m)) ~ (pg.flush, pg.flush, pg.flush);
    weave = route(2 * N, 2 * N, par(i, N, (i + 1, 2 * i + 1)), par(i, N, (N + i + 1, 2 * i + 2)));
    unweave = route(2 * N, 2 * N, par(i, N, (2 * i + 1, i + 1)), par(i, N, (2 * i + 2, N + i + 1)));
};
};
ovs = environment {
step(a) = \(xp, yp, x).(x, allpass(xp, yp, x), allpass(xp, yp, x))
with { allpass(xp, yp, x) = a * (x - yp) + xp; };
chain(a, 1) = step(a);
chain(a, M) = (step(a), si.bus(M - 1))
    : route(M + 2, M + 2, (1, 1), (2, 2), (3, M + 2), par(i, M - 1, (i + 4, i + 3)))
    : (chain(a, M - 1), _)
    : route(M + 2, M + 2, (1, 1), (2, 2), (M + 2, 3), par(i, M - 1, (i + 3, i + 4)));
section(a, M) = (chain(a, M) ~ (pg.flush, pg.flush)) : (!, !, si.bus(M));
path0(M) = section(0.035832788431062107, M) : section(0.2720401433964576, M)
         : section(0.57205719723570025, M) : section(0.82712476199732399, M);
path1(M) = section(0.1340901419430669, M) : section(0.42432487127186852, M)
         : section(0.70629214213863944, M) : section(0.94150309417375511, M);
interleave(M) = route(2 * M, 2 * M, par(i, M, (i + 1, 2 * i + 1)), par(i, M, (M + i + 1, 2 * i + 2)));
up_stage(M) = (si.bus(M) <: (path0(M), path1(M))) : interleave(M);
down_stage(M) = route(2 * M, 2 * M, par(i, M, (2 * i + 1, M + i + 1)), par(i, M, (2 * i + 2, i + 1)))
    : (path0(M), path1(M))
    : interleave(M) : par(i, M, +) : par(i, M, *(0.5));
ups(1) = _;
ups(2) = up_stage(1);
ups(4) = up_stage(1) : up_stage(2);
ups(8) = up_stage(1) : up_stage(2) : up_stage(4);
downs(1) = _;
downs(2) = down_stage(1);
downs(4) = down_stage(2) : down_stage(1);
downs(8) = down_stage(4) : down_stage(2) : down_stage(1);
oversample(N, f) = ups(N) : par(i, N, f) : downs(N);
tanh_shape = ma.tanh;
clip_shape = max(-1.0) : min(1.0);
shape_fn(shape) = \(x).(select2(shape > 0.5, tanh_shape(x), clip_shape(x)));
drive_gain(drive_db) = drive_db : pg.smooth_ms(10) : pg.clamp(0.0, 36.0) : pg.db2lin;
shaper(N, drive_db, shape) = *(drive_gain(drive_db)) : oversample(N, shape_fn(shape));
};
pdr = environment {
EVERY = 32;
memory(mu, tau_m) = follow
with {
    tick = ((+(1) : %(EVERY)) ~ _) == 0;
    z = float(EVERY) / (max(tau_m, 1.0e-3) * ma.SR);
    g = z * (1.0 - z * (0.5 - z * 0.16666667));
    follow(f) = (\(mp).(pg.flush(mp + tick * g * (mu * fbar - mp)))) ~ _
    with {
        fbar = f : pg.smooth_coef(1.0 - 1.0 / EVERY);
    };
};
loop(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead, mu, tau_m) = core ~ memory(mu, tau_m)
with {
    core(m, x) = det.loop_m(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead, m, x), m;
};
loop2(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead, mu, tau_m) = core ~ memory(mu, tau_m)
with {
    core(m, x1, x2) = det.loop_m2(N, gk, max_db, rest_db, r, G, kd, T, lam, att, rel, on, k2, k2db, k3, k3db, k3min, skew, dead, m, x1, x2), m;
};
};
fet76_stereo(input, output, attack, release, ratio) = f76s.limiter(tune.OS_FACTOR, input, output, attack, release, ratio);
};
// ---- End of custom components

