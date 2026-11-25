# Pangea desktop app

The small macOS app that turns a Pangea project into a native plugin on the user's own Mac and installs it into Logic. Tauri 2 (Rust) with a thin React page. Everything that touches the network, the keychain, the file system or a child process lives in Rust and is exposed as commands; the page only renders state. The bearer token never reaches the page.

What it does:

1. Sets up the Mac by itself, from its Setup screen (`src/screens/Setup.tsx`, `src-tauri/src/setup.rs`; `docs/desktop-setup.md` has the decisions): starts Apple's installer for the Command Line Tools and checks them by building a program (`apple.rs`), unpacks the Faust 2.88.0 (without LLVM), Ninja and Pangea shell it carries (`toolchain.rs`), fetches CMake and JUCE from their makers' releases and checks their SHA-256 (`tools.rs`), installs and updates the Pangea plugin (`host_install.rs`) and ends with a self-check (`selfcheck.rs`). No step asks for Terminal. A Faust version that differs from `library/faust-versions.json` (`native.faust`) is a warning, not a block.
2. Signs in with the OAuth device flow (see below) and keeps the token in the macOS keychain.
3. Lists the projects the user owns or has been shared (name, category, kind, head version, owner, visibility, summary and tags) with a search box and a category menu. The search is the site's rule (`src/lib/search.ts`, decision 85): what is filed under a typed word comes first, then what is named or tagged with it, then what only mentions it. A project of kind `midi` is listed as "MIDI tool" (`docs/midi-tools.md`). Each row has "Install in Logic" and "Open on the web"; the button keeps that label for a MIDI tool too.
4. Installs: fetches the version, writes a project directory, creates a `native_builds` row, runs `build.sh --install` with its output streamed live, parses the JSON summary line, reports the result back. One build at a time; further requests queue. A MIDI tool installs as two plugins, `<Name>` (an instrument with MIDI out, AU and VST3, for Ableton Live and Bitwig) and `<Name> FX` (an AU MIDI effect for Logic's MIDI FX slot); the result names both components and the VST3, the log has a line for each with its auval verdict (`auval`, `auvalFx`), and the message says where each one goes.
5. Handles `pangea://install?project=<id>&version=<n>` links, including being launched by one, and `pangea://add?project=<id>&version=<n>` (decision 87): that version is built as a module for the Pangea plugin, or found ready, and reported to the site.

Screenshots of each screen are in `docs/`, in Pangea's own repository only: the public copy leaves them out.

## Run it in development

Requirements: Rust (1.90 or later), Node 23, pnpm 9, Xcode. The Tauri CLI is a dev dependency (`pnpm tauri`).

```
cd desktop
pnpm install --ignore-workspace     # desktop/ is its own package, not part of the root workspace
pnpm tauri dev                      # Vite on :1420, the app in debug
```

Without the web app, use the mock server, which implements the device flow and the desktop API with three fixed projects (an effect, an instrument, and a MIDI tool that is the shell's `midi-steps` fixture, read from `shell/examples/midi-steps`) and a fake shell that prints a few lines, waits two seconds and ends with a JSON summary:

```
pnpm mock                           # http://localhost:3000
```

Then in the app's Settings set the shell directory to `desktop/mock-server/fake-shell`. The mock's approval page (`/device?user_code=...`) has Approve and Deny buttons; `curl -X POST localhost:3000/api/auth/device/approve -H 'content-type: application/json' -d '{"userCode":"XXXX"}'` does the same from a terminal, and `MOCK_AUTO_APPROVE_MS=3000 pnpm mock` approves every code by itself. Creating a file named `FAIL` in `fake-shell/` makes the fake build fail. Project ids are UUIDs (the mock prints its two on start; the effect is `3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f`), and the app refuses a link whose project is not a UUID or whose version is not a positive integer before it makes any request. Version 1 of the mock's effect contains a `soundfile` so the safety check's refusal can be seen with `open "pangea://install?project=3f2a1c4e-9b7d-4e6a-8c1f-0a2b3c4d5e6f&version=1"`.

Checks: `pnpm typecheck` (TypeScript), `cd src-tauri && cargo test` (the URL parser, the Faust version parser, the safety checker, which must pass the example projects `midi-steps`, `acid-line` and, when it is there, `two-hands`, and the MIDI tool's summary, message and ownership rule), `pnpm check` for both.

Download: `scripts/package-mac.sh [web address]` builds the release bundle for the live site, zips it into `downloads/` at the repository root and writes `downloads/desktop.json`; the next deploy offers it on the web app's Settings, The desktop app (decision 39). Setting up another Mac: `docs/setup-a-mac.md`.

Release bundle: `pnpm tauri build` writes `src-tauri/target/release/bundle/macos/Pangea.app`, ad-hoc signed (`"signingIdentity": "-"` in `tauri.conf.json`). The release profile sets `strip = "none"` on purpose: with Cargo's default (`strip = "debuginfo"`), rustc runs macOS 26's `strip` on the proc-macro dylibs it builds and dyld then refuses some of them ("mis-aligned LINKEDIT string pool"), which shows up as `can't find crate for `tauri_macros`` (or `phf_macros`, `derive_more-impl`) part way through the build; which crate breaks depends on file layout, so it looks random. Leave it off. Deep links only work from the bundle, because the `pangea` scheme is registered through the bundle's Info.plist (`CFBundleURLTypes`, written by the deep-link plugin from `plugins.deep-link.desktop.schemes`), not from `tauri dev`. Run the bundle once with `open src-tauri/target/release/bundle/macos/Pangea.app` so Launch Services learns the scheme, then `open "pangea://install?project=<id>&version=<n>"`.

### The keychain in development

The token is stored in the login keychain (service `Pangea`, account = the API base URL). macOS ties access to the binary's code signature, so after every rebuild the next launch asks whether the new binary may read the item (password required for "Always Allow"). The app handles a refusal: it removes the stale item and shows the sign-in screen with a note, and the next sign-in creates an item that belongs to the new binary. The token is also cached in memory for the life of the process, so the keychain is read once per launch, not on every call. A Developer ID signed build will not have this problem across updates.

## Settings

Persisted with tauri-plugin-store in `~/Library/Application Support/com.pangea.desktop/settings.json`:

| Key | Default | Meaning |
|---|---|---|
| `apiBaseUrl` | `PANGEA_API_BASE_URL` at compile time, else `http://localhost:3000` | The Pangea web app. `scripts/package-mac.sh` sets it to the live site for the download. The token is kept per address, so changing it means signing in to the new one. |
| `shellDir` | the repository's `shell/` in a debug build, empty in a release build | A developer's own shell folder (with `build.sh` and JUCE), used in place of the app's own copy. Empty: the shell the app carries, unpacked into `~/Library/Application Support/Pangea/app/<version>-<commit>/shell`. |

Every child process (`build.sh`, `check-project.sh`, the tool checks) runs with a clean, fixed environment: `PATH` is the app's own tools (Faust and Ninja it carries, then the CMake it fetched), then `/usr/bin:/bin:/usr/sbin:/sbin`, then `/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin`; `DEVELOPER_DIR` and `SDKROOT` when Apple's defaults do not build (`apple.rs`); `HOME`, `USER`, `TMPDIR`, `LANG`, nothing inherited. The user's shell setup must not reach the build: when the bundle was started from a terminal with pyenv initialised, pyenv's Python hung inside `build.sh` before it printed a line (`PYENV_DIR` had even been cut at the space in "Application Support").

Projects are written to `~/Library/Application Support/Pangea/projects/<slug>/v<number>/` (`plugin.json`, `dsp.dsp`, `logic.cpp` when the version has one, `ui.json`) before they are built. Built plugins go wherever `build.sh --install` puts them (`~/Library/Audio/Plug-Ins/Components` and `.../VST3`).

## How the device sign-in works

Better Auth's `deviceAuthorization` plugin, RFC 8628, all from Rust (`src-tauri/src/auth.rs`, `api.rs`):

1. `POST {api}/api/auth/device/code` with JSON `{ "client_id": "pangea-desktop", "scope": "" }`. Response: `device_code`, `user_code`, `verification_uri`, `verification_uri_complete`, `expires_in`, `interval`.
2. The app shows the user code and opens `verification_uri_complete` in the default browser (tauri-plugin-opener). The user approves there while signed in to the web app.
3. The app polls `POST {api}/api/auth/device/token` with `{ "grant_type": "urn:ietf:params:oauth:grant-type:device_code", "device_code", "client_id": "pangea-desktop" }` every `interval` seconds (plus five on `slow_down`, as the RFC says) until it gets `{ access_token, token_type: "Bearer", expires_in, scope }`. `authorization_pending` keeps polling; `access_denied` and `expired_token` end with a message; Cancel in the app stops it.
4. The token goes into the macOS keychain (`keyring` crate, service `Pangea`, account = API base URL) and never into a file. Every API call sends `Authorization: Bearer <token>` (Better Auth's `bearer` plugin accepts the session token). `GET /api/desktop/me` gives the email shown in the top bar; a 401 clears the token and shows the sign-in screen again. Sign out removes the keychain item.

## What the web app must provide

The routes in `docs/architecture.md`, with these exact shapes (the mock in `mock-server/server.mjs` is the reference):

- `GET /api/desktop/me` -> `{ id, email, name }`.
- `GET /api/desktop/projects` -> `{ projects: [ { id, name, slug, kind, visibility, headVersion, owner: { id, name, email }, description?, updatedAt? } ] }`. A bare array is accepted too; `owner` may also be a string. `owner.id` or `owner.email` must match `/api/desktop/me` for the user's own projects, because the owner decides whether a confirmation is shown and whether a logic module may be built. Since decision 85 each project may also carry `category`, `categoryName`, `tags`, `keywords` and `summary`: where it is filed, for the list's search and its category menu. The app passes the five on to the Pangea plugin in `library.json`; a site that sends none lists every project as not filed.
- `GET /api/desktop/projects/:id/versions/:number` -> `{ versionId, plugin, faust, logic, ui }`. `versionId` is the versions row id the app passes to `POST /api/desktop/builds`; the architecture doc lists only `plugin`, `faust`, `logic`, `ui`, so this one field is an addition. `plugin` is written out verbatim as `plugin.json`, which `shell/tools/read-plugin-json.py` reads, so it must carry what the shell requires: `name`, `slug`, `kind` (`effect`, `instrument` or `midi`), `bundleId` (or `bundleIdentifier`; default `com.pangea.<slug>`), `manufacturerCode` (`Pnga`), `pluginCode` (four characters, the first upper case), `version` (the whole version number is fine; the shell turns `3` into `0.3.0`), `description`, plus `owner: { name, email }` for the app's ownership rules. A MIDI tool also needs `fxPluginCode` (four characters, the first upper case, not the same as `pluginCode`) and may give `fxBundleId` (default `<bundleId>.fx`, which must differ from `bundleId`); `logic` must hold its module and `faust` the platform's silent program.
- `POST /api/desktop/builds` with `{ projectId, versionId, target: "both", machine }` -> `{ id }`. `machine` looks like `arm64, macOS 26.0.1`.
- `PATCH /api/desktop/builds/:id` with `{ status: "running" }` when the script starts, then `{ status: "done" | "failed", log, faustVersion, shellVersion }`. `shellVersion` is the shell's `VERSION` file if it has one, else the repository's git short hash (`git <hash>`), else `juce <version>` from the summary.
- `GET /projects/:id` is what "Open on the web" opens.
- The device flow routes above, and `verification_uri_complete` pointing at a page that approves the code for a signed-in user.

## Safety: whose code is this

A project is someone else's code compiled on this Mac, so three things happen before `build.sh` runs:

1. **Confirmation.** When the project is not owned by the signed-in user, and always when a `pangea://` link started the install, the app shows a dialog with the project name, version and the owner's name and email, says it will compile and install code from that person on this Mac, and offers Install or Cancel. Nothing is written or built before Install. The dialog is the app's own (the native message dialog of tauri-plugin-dialog did not appear when called from the build worker on macOS 26, so the worker asks the page and waits for the answer).
2. **Logic modules only from the owner.** A `logic.cpp` is plain C++ with no sandbox yet, so it is compiled natively only when the signed-in user owns the project. For a shared or public project with a logic module the build is refused with: "Logic modules from other members are not built yet. The sound part of this project is Faust and safe to build; logic modules will be sandboxed in a later version." A MIDI tool is all logic module, so one from another member is refused with its own reason: "MIDI tools from other members are not built yet. A MIDI tool is a logic module, which is plain C++; logic modules will be sandboxed in a later version." The confirmation dialog says so before anything is written or built. Faust-only projects from anyone still build after the confirmation. The shell enforces the same rule on its side: `build.sh` refuses a project with `logic.cpp` unless it is called with `--allow-logic`, and the app passes that flag only for the user's own projects, so a logic module from another member is refused in both layers even if one of them were bypassed.
3. **Source checks**, in `src-tauri/src/safety.rs` on the sources in memory before anything is written, again on the written files, then `shell/check-project.sh <project dir>` (all must pass; a refusal after the files were written removes the project directory). Faust: no `ffunction`, `fconstant`, `fvariable`, `foreign`, `soundfile`, `component(...)`; every `import`/`library` token must be a plain call with a string naming one of Faust 2.88.0's standard libraries (the same list as the shell's checker, `soundfiles.lib` excluded), whatever whitespace, newlines or comments sit between the token and the parenthesis; and every string literal must open and close on one line, because an unterminated or multi-line string would make the checker and the compiler read the file differently. A Faust string may not contain a backslash at all (so no escape rule needs to be trusted), a string or character literal in either language may not contain `//`, `/*` or `*/`, and a Faust source containing a documentation marker (`<mdoc`, `</mdoc>`, `<equation`, `<diagram`, `<metadata`, `<notice`, `<listing`) anywhere in its raw text is refused outright, since the Faust lexer treats those sections by other rules. Before any of this, both files are refused if they contain a control character other than newline and tab (carriage returns included), so a line end cannot differ between readers; and a logic module may not contain an apostrophe outside a comment (no character literals, no `1'000` digit separators), so there is no character-literal state at all. Logic module: no line may end in a backslash (a line continuation could hide a directive), no raw string literals, digraphs or trigraphs, the only preprocessor line allowed is exactly `#include "logic_api.h"`, no `[[` attributes, no `reinterpret_cast`, `const_cast` or `(*` casts, no `goto`, and none of the identifiers that mean allocation, threads, files, processes, the network, inline assembly or the standard library (`new`, `malloc`, `thread`, `std`, `fopen`, `socket`, `system`, `asm`, `extern`, the `exec`, `pthread_` and `__builtin_` prefixes, and so on; the full list is in the file). Comments are stripped with string and character literals respected, so a `/*` inside a string cannot hide what follows. A refusal is reported as a failed build with the reason in the log, and nothing is compiled.

## Layout

```
desktop/
  src/                 React page: App.tsx, screens/ (SignIn, Projects, Build, Settings), components/, lib/ (typed command wrappers), styles/
  src-tauri/src/       Rust: lib.rs (plugins, commands), commands.rs, api.rs (the web API), auth.rs (device flow),
                       keychain.rs, settings.rs, prereqs.rs, build.rs (the install worker), safety.rs, deep_link.rs, state.rs
  src-tauri/tauri.conf.json   identifier com.pangea.desktop, the pangea:// scheme, ad-hoc signing
  mock-server/         server.mjs (the stand-in web app) and fake-shell/build.sh
  docs/                screenshots (not in the public copy)
```

## What remains

- **A native Faust 2.90.19**, built from the commit faustwasm 0.20.0 was built from, in place of 2.88.0 (`library/faust-versions.json` explains the gap; `desktop/scripts/build-faust.sh` would build it).
- **Notarise** for distribution: the bundle is built for it (hardened runtime, `src-tauri/Entitlements.plist`), and `scripts/notarise-mac.sh` is the owner's, with his Developer ID (`docs/setup-a-mac.md`, "Notarising the app"). A Developer ID signature also stops the keychain prompts after updates.
- **A sandbox for logic modules** from other members, so the ownership rule above can be relaxed.
- Build queue persistence across launches, a menu bar presence and "open the plugin folder" niceties.
