#!/usr/bin/env python3
"""Checks that every function of a compiled logic module counts against the step budget.

    shell/tools/check-instrumented.py <logic_native.cpp.o>

The step budget (docs/midi-tools.md, "The step budget in the plugin"; decision 46) works only if
clang instrumented all of the module's code, so that every function calls
__sanitizer_cov_trace_pc_guard on entry. The checkers refuse the source that could turn that off
(attributes, pragmas, the platform's own names), and this is the check that does not depend on
spotting any syntax: it reads the object file's disassembly and fails, naming the functions,
when one has no call to the callback. build.sh runs it after compiling a module.

Exits 0 when every function is counted, 1 when one is not, 2 when the object cannot be read.
"""
import re
import subprocess
import sys

CALLBACK = "___sanitizer_cov_trace_pc_guard"
# The instrumentation's own constructor calls the init callback, never the counting one.
EXEMPT = {"_sancov.module_ctor_trace_pc_guard", "sancov.module_ctor_trace_pc_guard"}
HEADER = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSTRUCTION = re.compile(r"^\s+[0-9a-f]+:\s+\S")


def functions(disassembly: str) -> dict[str, tuple[int, int]]:
    """Each function's name with (instructions, calls to the callback)."""
    found: dict[str, list[int]] = {}
    current = None
    for line in disassembly.splitlines():
        if line.startswith("Disassembly of section"):
            current = None
            continue
        header = HEADER.match(line)
        if header:
            current = header.group(1)
            found.setdefault(current, [0, 0])
            continue
        if current is None:
            continue
        if line.rstrip().endswith(CALLBACK) and "RELOC" in line:
            found[current][1] += 1
        elif INSTRUCTION.match(line):
            found[current][0] += 1
    return {name: (counts[0], counts[1]) for name, counts in found.items()}


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: check-instrumented.py <logic_native.cpp.o>", file=sys.stderr)
        return 2
    try:
        disassembly = subprocess.run(
            ["objdump", "-d", "-r", "--no-show-raw-insn", sys.argv[1]],
            check=True, capture_output=True, text=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"check-instrumented: cannot read {sys.argv[1]}: {error}", file=sys.stderr)
        return 2
    found = functions(disassembly)
    uncounted = sorted(name for name, (instructions, calls) in found.items()
                       if instructions > 0 and calls == 0 and name not in EXEMPT)
    counted = sum(1 for name, (instructions, calls) in found.items() if calls > 0)
    if counted == 0:
        print("check-instrumented: no function calls the step budget: the module was compiled without it")
        return 1
    if uncounted:
        print("check-instrumented: these functions are not counted by the step budget: " + ", ".join(uncounted))
        return 1
    print(f"check-instrumented: all {counted} functions of the module count against the step budget")
    return 0


if __name__ == "__main__":
    sys.exit(main())
