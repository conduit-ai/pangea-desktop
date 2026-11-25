// A logic module that breaks the rules: it includes a system header.
#include "logic_api.h"
#include <cstdio>

static const char* const kLogicControls[] = { "gate" };
static const int kNumLogicControls = 1;
static const LogicParam kLogicParams[] = { { "Rate", 0.0f, 1.0f, 0.5f, 0.01f } };
static const int kNumLogicParams = 1;

struct Logic
{
    void prepare (double) {}
    int process (const LogicBlock&, LogicEvent*, int) { printf ("hello"); return 0; }
};
