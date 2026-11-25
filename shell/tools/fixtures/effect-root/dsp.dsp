declare name "Effect Root";
declare version "0.1.0";
declare author "Pangea";
declare description "An instrument whose effect section is one group, for tools/test-effect.sh.";
declare options "[nvoices:4]";
import("stdfaust.lib");

freq = hslider("freq", 440, 20, 20000, 0.01);
gain = hslider("gain", 0.5, 0, 1, 0.01);
gate = button("gate");

// The voice has a Level of its own, at the top.
level = hslider("Level", 0.5, 0, 1, 0.01);
// Every control of the effect is in one group, FX. Faust makes a program's only group its root,
// and a key leaves the root out, so the effect's Level has the voice's key, Level, and here its
// range and start too. They are two controls all the same: two addresses, as in the builder.
fxLevel = hslider("h:FX/Level", 0.5, 0, 1, 0.01);
fxDrive = hslider("h:FX/Drive", 2, 1, 8, 0.01);

process = os.triangle(freq) * en.adsr(0.005, 0.1, 0.7, 0.2, gate) * gain * level <: _, _;
effect = par(i, 2, *(fxDrive) : ma.tanh : *(fxLevel) : max(-1) : min(1));
