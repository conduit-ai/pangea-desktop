#!/bin/bash
# A stand-in for shell/build.sh: prints a few lines, waits two seconds and ends
# with the JSON summary line the desktop app parses. Nothing is compiled.
# Usage: build.sh <project dir> [--install]
# Set PANGEA_FAKE_FAIL=1, or create a file named FAIL next to this script, to make
# it fail the way a compile error would (the file works for the GUI app too).
set -u
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
[ -f "$here/FAIL" ] && PANGEA_FAKE_FAIL=1

dir="${1:-}"
if [ -z "$dir" ] || [ ! -d "$dir" ]; then
  echo "usage: build.sh <project dir> [--install]" >&2
  echo '{"ok":false,"error":"no project directory"}'
  exit 2
fi
install=0
for a in "${@:2}"; do
  [ "$a" = "--install" ] && install=1
done

name=$(sed -n 's/.*"name": *"\([^"]*\)".*/\1/p' "$dir/plugin.json" | head -1)
kind=$(sed -n 's/.*"kind": *"\([^"]*\)".*/\1/p' "$dir/plugin.json" | head -1)
start=$(date +%s)

echo "fake shell: building ${name:-unknown} (${kind:-effect}) from $dir"
echo "tools: xcode-select ok, cmake ok, ninja ok, faust 2.88.0 (pretend)"
echo "faust -lang cpp -cn mydsp -ftz 2 -o FaustDsp.h dsp.dsp"
sleep 1
echo "cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPANGEA_NAME=\"$name\""
echo "cmake --build build"
echo "warning: this is the fake shell, nothing was compiled" >&2
sleep 1

if [ -n "${PANGEA_FAKE_FAIL:-}" ]; then
  echo "dsp.dsp:3: error: pretend compile error (PANGEA_FAKE_FAIL is set)" >&2
  echo "{\"ok\":false,\"error\":\"pretend compile error\",\"faust\":\"2.88.0\",\"shell\":\"fake-0.1\",\"seconds\":$(( $(date +%s) - start ))}"
  exit 1
fi

component="$HOME/Library/Audio/Plug-Ins/Components/${name:-Pangea}.component"
vst3="$HOME/Library/Audio/Plug-Ins/VST3/${name:-Pangea}.vst3"
# A MIDI tool is two plugins: the instrument with MIDI out, and "<name> FX" for Logic's MIDI FX slot.
fx=""
if [ "$kind" = "midi" ]; then
  component_fx="$HOME/Library/Audio/Plug-Ins/Components/${name:-Pangea} FX.component"
  fx=",\"kind\":\"midi\",\"componentFx\":\"$component_fx\",\"auvalFx\":\"passed\""
fi
if [ $install = 1 ]; then
  echo "install: would copy to $component and $vst3 (pretend)"
  [ "$kind" = "midi" ] && echo "install: would copy to $component_fx (pretend)"
  echo "auval: pass (pretend)"
fi
echo "{\"ok\":true,\"component\":\"$component\",\"vst3\":\"$vst3\",\"auval\":\"passed\"$fx,\"faust\":\"2.88.0\",\"shell\":\"fake-0.1\",\"seconds\":$(( $(date +%s) - start ))}"
