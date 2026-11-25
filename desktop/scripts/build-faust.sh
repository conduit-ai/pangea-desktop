#!/bin/bash
# build-faust.sh: the Faust compiler and the Ninja the desktop app carries.
#
#   desktop/scripts/build-faust.sh
#
# Faust: the release desktop/tools.json pins (it must be library/faust-versions.json's native.faust),
# built from GRAME's source tarball without LLVM (Faust's own "regular" set of backends: C++ and
# the other source backends, no LLVM, no interpreter), compiler only, for this Mac's architecture.
# Beside it the headers the shell compiles against (include/faust, Faust's architecture/faust) and
# the libraries a program imports (share/faust: libraries/*.lib, dx7/*.lib and old/*.lib, as Faust
# installs them), so the layout is the one Homebrew's Faust has and the shell's scripts expect.
# Ninja: the pinned release binary.
#
# Every download is checked against its SHA-256 before it is opened, and kept in the cache, which
# lies outside git: PANGEA_TOOLS_CACHE, by default ~/Library/Caches/pangea-desktop-tools. What is
# built goes to <cache>/toolchain/faust-<version>-<arch> and <cache>/toolchain/ninja-<version>; a
# second run with nothing changed does nothing. desktop/scripts/package-mac.sh takes both from there.
#
# Needs: curl, shasum, tar, unzip, python3, cmake (3.22 or later) and Apple's compiler. Run on the
# Mac that packages the app; a member's Mac never runs it.
#
# The last line is one JSON object: {"ok": true, "faust": "<dir>", "ninja": "<dir>", ...}.
set -euo pipefail

DESKTOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_DIR="$(cd "$DESKTOP_DIR/.." && pwd)"
PINS="$DESKTOP_DIR/tools.json"
CACHE="${PANGEA_TOOLS_CACHE:-$HOME/Library/Caches/pangea-desktop-tools}"
DOWNLOADS="$CACHE/downloads"
OUT="$CACHE/toolchain"
# Raised whenever the way Faust is built or assembled changes, so the cache is built again.
RECIPE=2
ARCH="$(uname -m)"
STARTED=$(date +%s)

say() { echo "[$(( $(date +%s) - STARTED ))s] $*"; }
finish() {
    local ok="$1" error="${2:-}"
    OK="$ok" ERROR="$error" FAUST_DIR="${FAUST_DIR:-}" NINJA_DIR="${NINJA_DIR:-}" SECONDS_TAKEN=$(( $(date +%s) - STARTED )) \
    python3 -I -c '
import json, os
e = os.environ
out = {"ok": e["OK"] == "1", "faust": e["FAUST_DIR"], "ninja": e["NINJA_DIR"], "seconds": int(e["SECONDS_TAKEN"])}
if e["ERROR"]:
    out["error"] = e["ERROR"]
print(json.dumps(out))
'
    if [ "$ok" = "1" ]; then exit 0; else exit 1; fi
}
fail() { echo "error: $1" >&2; finish 0 "$1"; }

pin() {   # pin <tool> <field>
    python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))[sys.argv[2]][sys.argv[3]])' "$PINS" "$1" "$2"
}

# fetch <url> <sha256> <file name>: into the cache, resumed if a part is there, checked before use.
fetch() {
    local url="$1" sum="$2" name="$3" file="$DOWNLOADS/$3"
    mkdir -p "$DOWNLOADS"
    if [ -f "$file" ] && [ "$(shasum -a 256 "$file" | cut -d' ' -f1)" = "$sum" ]; then
        say "cached: $name"
        return 0
    fi
    rm -f "$file"
    say "fetching $url"
    curl --fail --location --silent --show-error --retry 3 --continue-at - --output "$file.part" "$url" \
        || fail "could not download $url"
    local got
    got="$(shasum -a 256 "$file.part" | cut -d' ' -f1)"
    if [ "$got" != "$sum" ]; then
        rm -f "$file.part"
        fail "$name has SHA-256 $got, not the pinned $sum; it was deleted"
    fi
    mv "$file.part" "$file"
    say "checked: $name ($sum)"
}

for tool in curl shasum tar unzip python3 cmake; do
    command -v "$tool" >/dev/null || fail "$tool not found"
done

FAUST_VERSION="$(pin faust version)"
NATIVE_FAUST="$(python3 -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["native"]["faust"])' "$REPO_DIR/library/faust-versions.json")"
[ "$FAUST_VERSION" = "$NATIVE_FAUST" ] \
    || fail "desktop/tools.json pins Faust $FAUST_VERSION but library/faust-versions.json pins $NATIVE_FAUST"
FAUST_SHA="$(pin faust sha256)"
NINJA_VERSION="$(pin ninja version)"
NINJA_SHA="$(pin ninja sha256)"

# 1. Ninja: the release binary and its licence.
NINJA_DIR="$OUT/ninja-$NINJA_VERSION"
if [ -f "$NINJA_DIR/BUILD.json" ] && grep -q "\"$NINJA_SHA\"" "$NINJA_DIR/BUILD.json"; then
    say "ninja $NINJA_VERSION is ready in $NINJA_DIR"
else
    fetch "$(pin ninja url)" "$NINJA_SHA" "ninja-$NINJA_VERSION-mac.zip"
    fetch "$(pin ninja licenceUrl)" "$(pin ninja licenceSha256)" "ninja-$NINJA_VERSION-COPYING"
    rm -rf "$NINJA_DIR" "$NINJA_DIR.tmp"
    mkdir -p "$NINJA_DIR.tmp"
    unzip -q "$DOWNLOADS/ninja-$NINJA_VERSION-mac.zip" ninja -d "$NINJA_DIR.tmp"
    cp "$DOWNLOADS/ninja-$NINJA_VERSION-COPYING" "$NINJA_DIR.tmp/COPYING"
    chmod 755 "$NINJA_DIR.tmp/ninja"
    [ "$("$NINJA_DIR.tmp/ninja" --version)" = "$NINJA_VERSION" ] || fail "the downloaded ninja does not say $NINJA_VERSION"
    NINJA_VERSION="$NINJA_VERSION" NINJA_SHA="$NINJA_SHA" BIN_SHA="$(shasum -a 256 "$NINJA_DIR.tmp/ninja" | cut -d' ' -f1)" \
    python3 -I -c '
import json, os
e = os.environ
print(json.dumps({"tool": "ninja", "version": e["NINJA_VERSION"], "archiveSha256": e["NINJA_SHA"], "binarySha256": e["BIN_SHA"]}, indent=2))
' > "$NINJA_DIR.tmp/BUILD.json"
    mv "$NINJA_DIR.tmp" "$NINJA_DIR"
    say "ninja $NINJA_VERSION is ready in $NINJA_DIR"
fi
NINJA="$NINJA_DIR/ninja"

# 2. Faust, built from source without LLVM.
FAUST_DIR="$OUT/faust-$FAUST_VERSION-$ARCH"
if [ -f "$FAUST_DIR/BUILD.json" ] && grep -q "\"recipe\": $RECIPE," "$FAUST_DIR/BUILD.json" && grep -q "\"$FAUST_SHA\"" "$FAUST_DIR/BUILD.json"; then
    say "faust $FAUST_VERSION is ready in $FAUST_DIR"
    finish 1
fi
fetch "$(pin faust url)" "$FAUST_SHA" "faust-$FAUST_VERSION.tar.gz"

WORK="$CACHE/work/faust-$FAUST_VERSION-$ARCH"
rm -rf "$WORK"
mkdir -p "$WORK/full" "$WORK/src"
say "unpacking faust-$FAUST_VERSION.tar.gz"
tar -xzf "$DOWNLOADS/faust-$FAUST_VERSION.tar.gz" -C "$WORK/full"
FULL="$WORK/full/faust-$FAUST_VERSION"
[ -f "$FULL/build/CMakeLists.txt" ] || fail "the Faust source has no build/CMakeLists.txt"
grep -q "^version := $FAUST_VERSION\$" "$FULL/Makefile" || fail "the Faust source's Makefile does not say version $FAUST_VERSION"

# The compiler's source, as the app carries it beside the binary (GPL v2, section 3(a)): the parts
# of GRAME's tarball the build reads (compiler/, build/, and architecture/faust, which it includes),
# with COPYING.txt. The compiler is built from this archive, not from the full tarball, so the
# archive is shown to be its complete source.
SOURCE_ARCHIVE="faust-$FAUST_VERSION-compiler-source.tar.gz"
tar -czf "$WORK/$SOURCE_ARCHIVE" -C "$WORK/full" \
    "faust-$FAUST_VERSION/compiler" "faust-$FAUST_VERSION/build" "faust-$FAUST_VERSION/architecture/faust" \
    "faust-$FAUST_VERSION/COPYING.txt" "faust-$FAUST_VERSION/Makefile" "faust-$FAUST_VERSION/README.md"
tar -xzf "$WORK/$SOURCE_ARCHIVE" -C "$WORK/src"
SRC="$WORK/src/faust-$FAUST_VERSION"
say "built from $SOURCE_ARCHIVE ($(( $(wc -c < "$WORK/$SOURCE_ARCHIVE") / 1024 )) KB)"

# Faust's regular backends (no LLVM, no interpreter) and only the compiler, without its OSC and
# HTTP libraries, wasm glue or emscripten targets.
cat > "$WORK/pangea-targets.cmake" <<'CMAKE'
set ( INCLUDE_EXECUTABLE  ON  CACHE STRING "Include Faust compiler" FORCE )
set ( INCLUDE_STATIC      OFF CACHE STRING "Include static Faust library" FORCE )
set ( INCLUDE_DYNAMIC     OFF CACHE STRING "Include dynamic Faust library" FORCE )
set ( INCLUDE_OSC         OFF CACHE STRING "Include Faust OSC static library" FORCE )
set ( INCLUDE_HTTP        OFF CACHE STRING "Include Faust HTTPD static library" FORCE )
set ( OSCDYNAMIC          OFF CACHE STRING "Include Faust OSC dynamic library" FORCE )
set ( HTTPDYNAMIC         OFF CACHE STRING "Include Faust HTTP dynamic library" FORCE )
set ( INCLUDE_ITP         OFF CACHE STRING "Include Faust Machine library" FORCE )
set ( ITPDYNAMIC          OFF CACHE STRING "Include Faust Machine library" FORCE )
set ( INCLUDE_EMCC        OFF CACHE STRING "Include emcc targets" FORCE )
set ( INCLUDE_WASM_GLUE   OFF CACHE STRING "Include wasm glue targets" FORCE )
CMAKE
grep -q 'LLVM_BACKEND *OFF' "$SRC/build/backends/regular.cmake" || fail "Faust's regular backends include LLVM in this release"

say "configuring Faust (regular backends, no LLVM) for $ARCH"
cmake -S "$SRC/build" -B "$WORK/build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" \
    -C "$SRC/build/backends/regular.cmake" -C "$WORK/pangea-targets.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    > "$WORK/configure.log" 2>&1 || { tail -30 "$WORK/configure.log" >&2; fail "cmake could not configure Faust (see $WORK/configure.log)"; }
if grep -q "include LLVM backend" "$WORK/configure.log"; then fail "the configuration includes the LLVM backend"; fi
say "backends: $(sed -n 's/^-- In target faust: include \(.*\) backend$/\1/p' "$WORK/configure.log" | tr '\n' ' ')"

say "building faust (a few minutes)"
cmake --build "$WORK/build" --target faust > "$WORK/build.log" 2>&1 \
    || { grep -E 'error' "$WORK/build.log" | head -20 >&2; fail "Faust did not build (see $WORK/build.log)"; }
# Faust's CMake writes the compiler into the source tree's build/bin.
BIN="$SRC/build/bin/faust"
[ -x "$BIN" ] || fail "the build made no faust binary at $BIN"

# 3. The tree the app carries.
rm -rf "$FAUST_DIR.tmp"
mkdir -p "$FAUST_DIR.tmp/bin" "$FAUST_DIR.tmp/include" "$FAUST_DIR.tmp/share/faust" "$FAUST_DIR.tmp/licences"
cp "$BIN" "$FAUST_DIR.tmp/bin/faust"
cp -R "$SRC/architecture/faust" "$FAUST_DIR.tmp/include/faust"
cp "$FULL"/libraries/*.lib "$FULL"/libraries/dx7/*.lib "$FULL"/libraries/old/*.lib "$FAUST_DIR.tmp/share/faust/"
cp "$FULL/COPYING.txt" "$FAUST_DIR.tmp/licences/COPYING.txt"
# The GPL v2 text: twelve of the compiler's sources are GPL-2.0-or-later, so the binary is too.
cp "$FULL/tools/faust2ck/COPYING" "$FAUST_DIR.tmp/licences/GPL-2.0.txt"
grep -q "Version 2, June 1991" "$FAUST_DIR.tmp/licences/GPL-2.0.txt" || fail "tools/faust2ck/COPYING is not the GPL v2 text"
if [ -d "$FULL/libraries/licenses" ]; then cp -R "$FULL/libraries/licenses" "$FAUST_DIR.tmp/licences/libraries"; fi
cp "$WORK/$SOURCE_ARCHIVE" "$FAUST_DIR.tmp/licences/$SOURCE_ARCHIVE"
cat > "$FAUST_DIR.tmp/licences/SOURCE.txt" <<TEXT
The Faust compiler in Pangea is Faust $FAUST_VERSION by GRAME, built without changes.

Its source: $(pin faust url)
(SHA-256 $FAUST_SHA)

$SOURCE_ARCHIVE in this folder holds the parts of that release the compiler is built from:
compiler/, build/, architecture/faust/, COPYING.txt. build-faust.sh, in this folder too, is the
script that built it: Faust's own "regular" backends, without LLVM, the compiler only, Release,
for macOS 13 or later on $ARCH. To build it yourself:

  tar -xzf $SOURCE_ARCHIVE
  cmake -S faust-$FAUST_VERSION/build -B out -G Ninja -C faust-$FAUST_VERSION/build/backends/regular.cmake \\
      -DINCLUDE_OSC=OFF -DINCLUDE_HTTP=OFF -DINCLUDE_EMCC=OFF -DINCLUDE_WASM_GLUE=OFF -DCMAKE_BUILD_TYPE=Release
  cmake --build out --target faust

The compiler is free software: COPYING.txt gives its licence, the GNU Lesser General Public
License 2.1 or later, and twelve of its source files are under the GNU General Public License 2
or later (GPL-2.0.txt), so the compiler as a whole is under the GPL 2 or later. Its headers
(include/faust) and libraries (share/faust) are under the LGPL 2.1 or later with the Faust
exception, as each file says.
TEXT

# Proof that it is the pinned Faust and that LLVM is not in it.
VERSION_TEXT="$("$FAUST_DIR.tmp/bin/faust" --version)"
FIRST_LINE="$(head -1 <<< "$VERSION_TEXT")"
grep -q "Version $FAUST_VERSION" <<< "$FIRST_LINE" || fail "the built faust says: $FIRST_LINE"
if grep -q "LLVM IR" <<< "$VERSION_TEXT"; then fail "the built faust has the LLVM backend"; fi
LINKS="$(otool -L "$FAUST_DIR.tmp/bin/faust")"
if grep -qi llvm <<< "$LINKS"; then fail "the built faust links LLVM"; fi
LINKED="$(otool -L "$FAUST_DIR.tmp/bin/faust" | tail -n +2 | awk '{print $1}' | tr '\n' ' ')"
say "faust links only: $LINKED"
echo 'import("stdfaust.lib"); process = os.osc(440) * 0.1;' > "$WORK/smoke.dsp"
"$FAUST_DIR.tmp/bin/faust" -lang cpp -o "$WORK/smoke.h" "$WORK/smoke.dsp" || fail "the built faust could not compile a program that imports stdfaust.lib"

FAUST_VERSION="$FAUST_VERSION" FAUST_SHA="$FAUST_SHA" RECIPE="$RECIPE" ARCH="$ARCH" URL="$(pin faust url)" \
BIN_SHA="$(shasum -a 256 "$FAUST_DIR.tmp/bin/faust" | cut -d' ' -f1)" BIN_BYTES="$(wc -c < "$FAUST_DIR.tmp/bin/faust" | tr -d ' ')" \
BACKENDS="$(echo "$VERSION_TEXT" | sed -n 's/^ *DSP to //p' | tr '\n' ',' | sed 's/,$//')" \
CMAKE_VERSION_TEXT="$(cmake --version | head -1)" CLANG_TEXT="$(clang --version | head -1)" \
python3 -I -c '
import json, os, datetime
e = os.environ
print(json.dumps({
    "tool": "faust", "version": e["FAUST_VERSION"], "recipe": int(e["RECIPE"]), "sourceSha256": e["FAUST_SHA"], "source": e["URL"],
    "arch": e["ARCH"], "llvm": False, "backends": e["BACKENDS"].split(","),
    "binarySha256": e["BIN_SHA"], "binaryBytes": int(e["BIN_BYTES"]),
    "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
    "with": [e["CMAKE_VERSION_TEXT"], e["CLANG_TEXT"]],
    "how": "cmake -S build -G Ninja -C build/backends/regular.cmake -C <compiler only> -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0; cmake --build --target faust",
}, indent=2))
' > "$FAUST_DIR.tmp/BUILD.json"
cp "$0" "$FAUST_DIR.tmp/licences/build-faust.sh"
rm -rf "$FAUST_DIR"
mv "$FAUST_DIR.tmp" "$FAUST_DIR"
rm -rf "$WORK"
say "faust $FAUST_VERSION is ready in $FAUST_DIR ($(du -sh "$FAUST_DIR" | cut -f1))"
finish 1
