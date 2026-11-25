# Building Pangea from source

Pangea supports one kind of computer: a Mac with Apple silicon. That is the only one we build and test on. This page says how to build there, and, for people who want to try anyway, how far a build for an Intel Mac or for Windows is known to get. We make no promise for either.

## A Mac with Apple silicon (supported)

Follow `docs/setup-a-mac.md`, "For developers". In short:

1. Install Apple's Command Line Tools (`xcode-select --install`), Rust 1.90 or later (https://rustup.rs), Node 23 and pnpm 9. For a development build, also `brew install cmake ninja faust`.
2. Clone JUCE 8.0.10 into the shell: `git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git shell/JUCE`.
3. Run the app in development: `cd desktop && pnpm install --ignore-workspace && pnpm tauri dev`. Without an account, `pnpm mock` runs a stand-in for the site (`desktop/README.md`).
4. Or make the app as members get it, with Faust and Ninja inside: `desktop/scripts/package-mac.sh https://pangea.audio`. It builds Faust 2.88.0 from GRAME's release, checks every download against its SHA-256, and writes `downloads/Pangea-mac-arm64.zip`. It compares the C++ its Faust writes with the Faust on your path; with none there, set `PANGEA_COMPARE_FAUST=none`.
5. Build one plugin with the shell: `shell/build.sh shell/examples/tape-drift`. Add `--install` to put it in your plug-in folders. `shell/build-host.sh` builds the Pangea plugin.

The app is ad-hoc signed, so macOS asks before you open it the first time (`docs/setup-a-mac.md`, step 2).

## An Intel Mac (not supported, not tested by us)

We have no Intel Mac. What follows was tried on a Mac with Apple silicon, building for Intel (`x86_64`). Each step says whether it was built here, run here, or not tried. Nothing was run on Intel, because this Mac has no Rosetta and no Intel processor.

On an Intel Mac you need the same things as above: Apple's Command Line Tools, Rust, Node 23, pnpm 9, and for development Homebrew's `cmake`, `ninja` and `faust`. The scripts take the architecture from the Mac they run on, so on an Intel Mac they should need no change.

1. **The Rust target.** `rustup target add x86_64-apple-darwin` is needed only to build for Intel on an Apple silicon Mac. On an Intel Mac it is the default. *Built here.*
2. **The app, without the tools inside** (as `pnpm tauri dev` and a plain `pnpm tauri build` make it): `cd desktop && pnpm install --ignore-workspace && PANGEA_API_BASE_URL=https://pangea.audio pnpm tauri build --target x86_64-apple-darwin`. It built, an `x86_64` Pangea.app of 9.5 MB, in under two minutes. *Built here, not run.*
3. **Faust, as the app carries it** (`desktop/scripts/build-faust.sh`). The script builds for the Mac's own architecture. Made to build for `x86_64` here, it compiled Faust 2.88.0 without LLVM, a 7.2 MB `x86_64` program that links only `libc++` and `libSystem`, and then stopped where it runs that program to check it ("Bad CPU type in executable"). So its checks (the version, no LLVM, a test program) and `compare-faust.sh` were not run for Intel. *Built here, not run.*
4. **Ninja.** The release the app carries (`desktop/tools.json`, `ninja-mac.zip`) is one binary for both `x86_64` and `arm64`. *Read here; run here as `arm64` only.*
5. **CMake and JUCE**, which the app downloads on a member's Mac. CMake's release is `cmake-3.31.12-macos-universal.tar.gz`, for both; JUCE comes as source. *Read here, not tried.*
6. **The app with Faust and Ninja inside.** `desktop/scripts/package-mac.sh https://pangea.audio` builds for the Mac it runs on and names its download `Pangea-mac-x86_64.zip` on an Intel Mac. It could not run here, because step 3 stops. Built by hand instead, with the `x86_64` Faust from step 3 and the staged files laid out as `stage-payload.sh` lays them out, `pnpm tauri build --target x86_64-apple-darwin --config src-tauri/tauri.bundle.conf.json` made an `x86_64` Pangea.app with Faust and Ninja inside. *Built here by hand, not run; `package-mac.sh` on Intel not tried.*
7. **A plugin with the shell.** `shell/build.sh shell/examples/tape-drift` on an Intel Mac builds for it. Here, building for `x86_64` compiled and linked Tape Drift's Audio Unit and VST3, and MIDI Steps (a MIDI tool with a logic module) as its two Audio Units, all `x86_64`, and `shell/tools/check-instrumented.py` passed on the `x86_64` logic module ("all 9 functions of the module count against the step budget"). The build then stopped where JUCE runs a small helper it has just built for the VST3 (`juce_vst3_helper`, which writes `moduleinfo.json`), since that helper is `x86_64` too. It should run on an Intel Mac; not tried. *Built here, not run.* Two notes for anyone building for Intel on Apple silicon: the environment variable `CMAKE_OSX_ARCHITECTURES=x86_64` also makes JUCE build its `juceaide` tool for Intel, which then cannot run; giving the architecture in the build folder's `CMakeCache.txt` alone (`CMAKE_OSX_ARCHITECTURES:STRING=x86_64`, with `PANGEA_BUILD_DIR` pointing at that folder) does not.
8. **Running anything**: the app, Faust, a plugin in a DAW, `--verify`, `auval`, `shell/build-host.sh` and the app's Setup steps. *Not tried.*

### Asking Claude Code to do it

Open Claude Code in your clone and paste this:

> Build Pangea's desktop app and one plugin from this repository on my Intel Mac, following BUILDING.md, "An Intel Mac". Install what is missing (Apple's Command Line Tools, Rust, Node 23, pnpm 9, and Homebrew's cmake, ninja and faust: package-mac.sh compares the Faust it builds with Homebrew's), clone JUCE 8.0.10 into shell/JUCE, run desktop/scripts/package-mac.sh https://pangea.audio, then shell/build.sh shell/examples/tape-drift --verify. Show me the last lines of each. Do not install anything into ~/Library/Audio/Plug-Ins unless I say so, and do not sign or notarise anything. If a step fails, stop and tell me what it said.

## Windows (not supported)

Nothing here has been built on Windows, and we have no plan to. From reading the code, this is what stands in the way:

- **The scripts are bash.** `shell/build.sh`, `build-host.sh`, `build-module.sh`, `check-project.sh`, the tools in `shell/tools` and the app's packaging scripts are bash, with macOS tools in them (`ditto`, `xattr`, `codesign`, `otool`, `lipo`, `launchctl`, `shasum`). WSL or Git Bash might run some of them; none was written for that.
- **Audio Units are Mac only.** Every plugin is built as an AU and a VST3, and the Pangea MIDI FX and a MIDI tool's FX plugin are AU only. On Windows only the VST3 could be built, and `shell/CMakeLists.txt` would need its formats changed.
- **The logic modules' step budget is clang's.** A logic module is compiled with `-fsanitize-coverage=trace-pc-guard` and other clang flags (`shell/platform/logic_native.flags`), stopped with `_longjmp`, and checked by reading its object file with `objdump` (`shell/tools/check-instrumented.py`). MSVC has none of this; clang-cl might.
- **The web panel.** The plugin's panel is a web view. On Windows JUCE needs Microsoft's WebView2 (`shell/platform/PangeaWebEditor.h` asks for it), which was never built or tried. `--native-editor` draws the older panel without a web view.
- **The desktop app assumes macOS throughout.** Its Setup screen installs Apple's Command Line Tools, fetches the macOS CMake and JUCE releases (`desktop/tools.json`), puts its files in `~/Library/Application Support`, installs plugins into `~/Library/Audio/Plug-Ins` and validates them with Apple's `auval`, and the Faust it carries is built for macOS by `desktop/scripts/build-faust.sh`. None of these has a Windows path in the code. Tauri itself and the keychain library it uses do run on Windows.
- **The plugin's audio code** (`shell/platform/HybridShell.cpp`) uses a clang pragma for the rounding a logic module needs (`#pragma clang fp contract`); MSVC would need its own.

## Help

This repository is published so that the source of what Pangea gives its members is open. Pangea's own help is for members on a Mac with Apple silicon; it cannot help with an Intel Mac or Windows.
