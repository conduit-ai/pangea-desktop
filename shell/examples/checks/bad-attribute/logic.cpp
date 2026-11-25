// An attribute and a cast: must be refused.
#include "logic_api.h"
struct Logic { void prepare (double) {} [[gnu::noinline]] int process (const LogicBlock&, LogicEvent* out, int) { return 0; } };
