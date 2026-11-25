// A forbidden word inside a string literal: must be refused.
#include "logic_api.h"
static const char* const kLogicControls[] = { "gate" };
static const int kNumLogicControls = 1;
static const LogicParam kLogicParams[] = { { "malloc", 0.0f, 1.0f, 0.5f, 0.01f } };
static const int kNumLogicParams = 1;
struct Logic { void prepare (double) {} int process (const LogicBlock&, LogicEvent*, int) { return 0; } };
