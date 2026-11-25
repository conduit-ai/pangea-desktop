// Forbidden words in comments must not trip the check: malloc, free, system("rm -rf /"),
// #include <cstdio>, std::thread, extern "C", asm("nop"), (*fp)(), [[nodiscard]].
/* A block comment with a quote " and a #define X and new int[4] and a backslash \ mid-line,
   continuing the comment on this line. */
#include "logic_api.h"

static const char* const kLogicControls[] = { "gate" };   // "free" is a word in this comment only
static const int kNumLogicControls = 1;
static const LogicParam kLogicParams[] = { { "Rate", 0.0f, 1.0f, 0.5f, 0.01f } };
static const int kNumLogicParams = 1;

struct Logic
{
    void prepare (double) {}
    int process (const LogicBlock& b, LogicEvent* out, int maxEvents)
    {
        const char* url = "example.com/one/two"; // a string with slashes but no comment marker; "quotes" inside this comment are fine
        (void) url;
        if (maxEvents > 0 && b.playing) { out[0] = { 0, 0, 1.0f }; return 1; }
        return 0;
    }
};
