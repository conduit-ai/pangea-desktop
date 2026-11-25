declare name "Acid Line";
declare version "0.1";
import("stdfaust.lib");

// Driven by the sequencer logic, hidden from the user.
note   = nentry("note [logic:1]", 33, 0, 127, 1);
gate   = nentry("gate [logic:1]", 0, 0, 1, 1);
accent = nentry("accent [logic:1]", 0, 0, 1, 1);
slide  = nentry("slide [logic:1]", 0, 0, 1, 1);

cutoff = hslider("Cutoff [unit:Hz] [scale:log]", 400, 60, 6000, 1) : si.smoo;
reso   = hslider("Resonance", 0.75, 0, 0.97, 0.01) : si.smoo;
envMod = hslider("Env Mod", 0.6, 0, 1, 0.01) : si.smoo;
decay  = hslider("Decay [unit:s] [scale:log]", 0.3, 0.05, 2, 0.01);
accAmt = hslider("Accent", 0.6, 0, 1, 0.01);
shape  = hslider("Saw/Square", 0, 0, 1, 0.01) : si.smoo;
drive  = hslider("Drive", 0.4, 0, 1, 0.01) : si.smoo;
volume = hslider("Volume", 0.6, 0, 1, 0.01) : si.smoo;

acc = accent * accAmt;
glide = ba.if(slide > 0.5, 0.06, 0.002);
freq = ba.midikey2hz(note) : si.smooth(ba.tau2pole(glide));

osc = os.sawtooth(freq) * (1 - shape) + os.square(freq) * shape;
filterEnv = en.adsr(0.002, decay * (1 - 0.5 * acc), 0, 0.01, gate);
cut = cutoff * pow(2, envMod * 5 * filterEnv * (1 + acc)) : min(16000);
ampEnv = en.adsr(0.002, 0.08, 0.9, 0.01, gate) * (1 + acc * 0.6);

voice = osc : ve.moog_vcf_2bn(reso, cut) : *(ampEnv) : *(1 + drive * 6) : ma.tanh : *(volume * 0.5);

process = voice <: _, _;
