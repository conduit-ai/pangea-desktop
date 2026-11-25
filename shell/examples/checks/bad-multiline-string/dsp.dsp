import("stdfaust.lib");
g = hslider("gain
", 0.5, 0, 1, 0.01);
process = _,_ : *(g), *(g);
