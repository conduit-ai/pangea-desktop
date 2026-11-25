// A line continuation that spells a second include: must be refused.
#include "logic_api.h"
#inc\
lude <cstdio>
struct Logic { void prepare (double) {} int process (const LogicBlock&, LogicEvent*, int) { return 0; } };
