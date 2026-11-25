declare name "Bad Project";
import("stdfaust.lib");
// A comment mentioning ffunction is fine; the real one below is not.
evil = ffunction(float getenv_f (int), <stdlib.h>, "");
process = os.osc(440) * evil(0) <: _, _;
