# Setting up a Mac to build Pangea plugins

This gets the Pangea desktop app and the Pangea plugin working on a Mac. With them, any plugin you have made on the web opens inside the Pangea plugin in your DAW, and a new version comes in while the session plays. You can also install one project as an AU and VST3 plug-in of its own.

Since the app sets up the Mac by itself (`docs/desktop-setup.md`), a musician needs no Terminal: they open the app, and its Setup screen does the rest. The first part below is that, step by step, as the site's Settings, The desktop app shows it. The parts after it are for developers, for a download made before the app could do this, and for when something goes wrong. Each step ends with a check.

What the Mac ends up with:

- Pangea.app in /Applications.
- Apple's Command Line Tools, from Apple's own installer.
- In `~/Library/Application Support/Pangea/`: the app's copy of its tools and the Pangea shell (`app/<version>-<commit>/`: Faust 2.88.0, Ninja 1.13.2, the shell), and the tools it fetched and checked (`tools/`: CMake 3.31.12, JUCE 8.0.10).
- The Pangea plugin in the plug-in folders: Pangea, Pangea Instrument and Pangea MIDI FX (`docs/pangea-plugin.md`).
- The app signed in to the live site.

Nothing here needs Homebrew, git, a clone of the repository, Azure access, a `.env` file, Postgres or Docker.

## With the download

### 1. Move it to Applications

Sign in at the live site in a browser. Open Your library, then Desktop app, then Download for Mac. Open the zip in Downloads and drag Pangea into Applications. Open it from Applications, not from Downloads. It is for Apple silicon only.

Check: Pangea is in /Applications.

### 2. Open it the first time

Until Apple has notarised the app (see "Notarising the app" at the end), macOS stops it the first time:

1. macOS says Pangea was not opened, because Apple could not check it. Click Done, not Move to Bin.
2. Open System Settings, then Privacy and Security. Scroll down to Security, where it says Pangea was blocked.
3. Click Open Anyway. The button stays there for about an hour after the first try.
4. Enter your Mac login password, then click Open.

This happens once for each download, so again after each update. Do not change Gatekeeper's settings to get round it.

If macOS says instead that Pangea is damaged, there is no Open Anyway: stop, download it again, and tell Pangea. That message means a broken signature, and nobody has seen it with this download yet; a test on a Mac that has never had Pangea is still to be done.

A notarised download opens with one question from macOS, Open or Cancel: click Open.

Check: Pangea opens on its Setup screen.

### 3. Let it set up your Mac

The Setup screen has five steps. Each says done, under way, needs you, stopped or waiting, and how long it usually takes. One button at the top does the next thing.

1. **Apple's build tools.** Pangea makes your plugins on your Mac, so it needs Apple's compiler. Press Install Apple's build tools. macOS asks whether to install the Command Line Tools: click Install, then Agree to Apple's licence. It downloads about 1 GB and takes about 5 to 30 minutes; the step turns to a tick by itself. If Apple's installer says the software is not currently available, try again in a few minutes. If you have Xcode and have never opened it, the step says so: open Xcode once and choose Agree.
2. **Sign in.** You can do this while Apple's tools install. Press Sign in. The app opens the site in your browser and shows a code; approve the same code there, signed in with your usual account. A downloaded app already has the live site's address; if it opens another, set the web address in the app's Settings first.
3. **Pangea's tools.** Press Get Pangea's tools. Pangea fetches CMake and JUCE from their makers' own releases, about 130 MB, checks each against its checksum, and puts them in place. A minute or two. Faust, Ninja and the shell come inside the app.
4. **The Pangea plugin.** Close your DAW first: the step names it if it is open. Press Install the Pangea plugin. Pangea builds it (a few minutes the first time, while JUCE compiles, seconds after), puts Pangea, Pangea Instrument and Pangea MIDI FX in your plug-in folders, and runs Apple's check on the three Audio Units. After an update of the app, the button may say Update the Pangea plugin: the plugin and the app's shell must share one module number (`docs/pangea-plugin.md`, "The module").
5. **Check it works.** Press Check it works. Pangea builds a small test plugin, then opens test plugins of every kind in the Pangea plugin and checks what they do. A Pangea window shows for a few seconds. Two to four minutes.

None of the steps asks for Terminal. When a step stops, it says why in a sentence, with Try again and Copy details for Pangea's help.

Check: the Setup screen shows five ticks and says Pangea is ready.

### 4. Add the Pangea plugin in your DAW

Keep the app open: it tells the plugin what you have made, and builds a version when the plugin asks for one. In the DAW, add Pangea to an audio track for an effect, or Pangea Instrument to an instrument track for an instrument or a MIDI tool. Both are under the maker Pangea. The plugin shows your plugins. Choose one: the app builds it, which takes from a few seconds to about twenty, and the plugin opens it.

- **Logic:** it finds the three Audio Units when it next starts. If it does not, open Logic Pro, Settings, Plug-in Manager, and press Reset and Rescan Selection. A MIDI tool can also go in an instrument track's MIDI FX slot, as Pangea MIDI FX.
- **Ableton Live:** open Settings, Plug-Ins. Turn on Use Audio Units, or Use VST3 Plug-In System Folders, or both. Then press Rescan. For a MIDI tool, use the VST3 Pangea Instrument: Live takes a plugin's MIDI from a VST3 only.

When you save a new version on the web, the plugin says so. Press Refresh, or tick Follow new versions and it changes by itself. A control you have moved keeps its value, and automation follows a control from version to version. A song opens again with the version it was saved with.

For now the plugin opens only your own projects. With the app closed it still opens every version it has opened before.

Check: the Setup screen's last step says "Pangea is working in your DAW", which it does the first time the Pangea plugin asks the app for your plugins.

### 5. One project as a plugin of its own

A project can also be installed as an AU and a VST3 with its own name. Use this for a plugin another member shared with you: the Pangea plugin does not open those yet. In the app, choose a project and press Install in Logic. On a plugin another member shared with you, Install AU / VST3 in the web builder does the same; on your own, the builder's button is Add to Pangea plugin (decision 87). The first build takes a few minutes and later ones about twenty seconds. The AU goes in `~/Library/Audio/Plug-Ins/Components` and is validated with Apple's `auval`; the VST3 goes in `~/Library/Audio/Plug-Ins/VST3`. It stays at the version you installed: for a newer one, install it again, then restart Logic or rescan.

## For developers

### Build the app from source

This needs Rust 1.90 or later (from https://rustup.rs), Node 23 and pnpm 9. A development build carries no tools of its own: it uses Homebrew's Faust, Ninja and CMake (`brew install cmake ninja faust`) and the repository's `shell/`, with JUCE cloned into it.

```
cd <your clone of the repository>
test -f shell/JUCE/CMakeLists.txt || git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git shell/JUCE
cd desktop
pnpm install --ignore-workspace
pnpm tauri dev
```

To make the download as members get it, with the tools inside: `desktop/scripts/package-mac.sh https://pangea.audio`. It builds Faust 2.88.0 without LLVM and fetches Ninja into a cache outside git (`desktop/scripts/build-faust.sh`; `PANGEA_TOOLS_CACHE`, by default `~/Library/Caches/pangea-desktop-tools`), checks that this Faust writes the same C++ as the Faust installed here for every `.dsp` in the repository (`desktop/scripts/compare-faust.sh`), stages the shell as committed, and builds the app with them inside. It needs a clean `desktop/`, `shell/` and `library/`, so the download matches its commit.

`examples/setup-run.rs` runs every Setup step for real without the window, into a scratch home folder, with Homebrew off the path:

```
cd desktop/src-tauri
PANGEA_DESKTOP_HOME=/tmp/pangea-home PANGEA_DESKTOP_PAYLOAD=$PWD/target/pangea-payload \
PANGEA_DESKTOP_SYSTEM_PATH= cargo run --example setup-run
```

### Use your own shell folder

A developer's own clone of the shell can take the place of the app's copy: in the app's Settings, under For developers, name the repository's `shell` folder and press Save. Builds then use it, with the JUCE inside it, and a CMake of 3.22 or later on the Mac is used as it is. Use Pangea's own clears it. The same setting is in a file, which helps when Claude Code does the setup (write it with the app quit):

```
mkdir -p ~/Library/Application\ Support/com.pangea.desktop
cat > ~/Library/Application\ Support/com.pangea.desktop/settings.json <<JSON
{
  "apiBaseUrl": "https://<the live site>",
  "shellDir": "<your clone of the repository>/shell"
}
JSON
```

An existing member who saved a shell folder before keeps it. With your own shell, `shell/build-host.sh --install` still builds and installs the Pangea plugin from Terminal, and Setup counts a plugin installed that way, at the shell's version, as installed.

### With Claude Code

Open Claude Code in the repository and paste this:

> Set up this Mac to build Pangea plugins by following docs/setup-a-mac.md. Use the download, and stop to tell me at each step that needs me in macOS (Open Anyway, Apple's installer, signing in). Before the Pangea plugin step, ask me to close my DAW. Do not create a .env, do not run anything in infra/azure, and do not sign or notarise anything.

## Before the app set up the Mac

A download built before the app could set up the Mac by itself (the site says so beside it) needs the older steps, in Terminal:

1. `xcode-select -p || xcode-select --install`, then Homebrew from https://brew.sh and `brew install cmake ninja faust`.
2. A clone of the repository, with JUCE inside its shell: `git clone --depth 1 --branch 8.0.10 https://github.com/juce-framework/JUCE.git shell/JUCE`.
3. The app from the download, opened once with `xattr -dr com.apple.quarantine /Applications/Pangea.app`, or through Open Anyway as above.
4. In the app's Settings, the shell folder set to the clone's `shell`.
5. With every DAW closed, `shell/build-host.sh --install` from the clone, and again after every `git pull`.
6. Sign in, and add the Pangea plugin in the DAW as above.

## When something goes wrong

- **"Pangea is damaged" or "cannot be opened":** download it again. If it says so again, tell Pangea; do not get round it.
- **Apple's build tools stopped:** Apple's installer was closed, or Apple's server did not have the tools just then ("not currently available"). Press Install Apple's build tools again in a few minutes. Apple's download page (developer.apple.com/download, with an Apple ID) has the Command Line Tools too; the app's Settings, Tools on this Mac, has a button that opens it.
- **"Xcode is installed, but its licence has not been accepted":** open Xcode once and choose Agree, then press Check again. If the Command Line Tools are also on the Mac, the app uses them and does not ask.
- **A stray macOS SDK from a beta of the Command Line Tools** (newer than the linker, so even an empty program does not link; `tapi error: malformed file`, `unknown architecture arm64e.x1`): the app tries the other SDKs itself and builds with the first that works (`SDKROOT`), so it needs no `sudo`. Settings, Tools on this Mac, names the SDK it chose.
- **"Pangea could not reach github.com":** the Mac is offline, or a network blocks GitHub. Check the connection and press Try again. A download that stopped carries on from where it was.
- **"did not match its checksum":** Pangea deleted the file and used nothing from it. Press Try again. If it happens again, tell Pangea.
- **The Pangea plugin step names a DAW:** close it, then press Install again. Install anyway replaces the plugin while the DAW may hold the old one.
- **The keychain asks for a password:** this happens after each new copy of an app that is not notarised. Choose Always Allow, or Deny and sign in again.
- **The Pangea plugin shows "Open the Pangea app and sign in. Your plugins will show here." although the app is open and signed in:** the app is older than the Pangea plugin. Get a newer download.
- **The Pangea plugin says "The Pangea app did not answer about" a plugin:** the app is closed or signed out. Open it and sign in, then choose the plugin again.
- **After an update the app builds the same module again each time, or a plugin does not open:** the Pangea plugin is older than the app's shell. Setup shows Update the Pangea plugin: press it.
- **A MIDI tool sends no notes in Ableton Live:** Live loaded the Audio Unit. Use the VST3 Pangea Instrument.
- **`ditto: /Applications: Operation not permitted` when unpacking the download in Terminal:** unpack to a temporary folder and copy only `Pangea.app`, or open the zip in Finder.

## Notarising the app (for the owner with the Apple account)

The download is ad-hoc signed, so macOS asks each member to allow it once in System Settings. A notarised app opens like any other. Only the owner who holds the Apple developer account can do this, on his own Mac; nobody else, and no agent, signs anything for distribution. Plugins are never signed: Pangea builds them on each member's Mac (decision 2).

Once, before the first time:

1. In the Apple Developer account, make a **Developer ID Application** certificate (the Account Holder makes it; in Xcode: Settings, Accounts, Manage Certificates, the plus button, Developer ID Application). Check that the keychain has it:

   ```
   security find-identity -v -p codesigning
   ```

   It lists `"Developer ID Application: <Name> (<TEAMID>)"`.

2. Make an app-specific password for the Apple ID at account.apple.com (Sign-In and Security, App-Specific Passwords), then keep it in the login keychain under a profile name. It is asked for once and never written into the repository:

   ```
   xcrun notarytool store-credentials "pangea-notary" --apple-id "<Apple ID>" --team-id "<TEAMID>"
   ```

Each time the app is released:

1. Build and check it as usual, which makes the ad-hoc signed app and the zip in `downloads/`:

   ```
   desktop/scripts/package-mac.sh https://pangea.audio
   ```

2. Sign, notarise and staple it, which replaces the zip and marks `downloads/desktop.json` as notarised:

   ```
   PANGEA_SIGNING_IDENTITY="Developer ID Application: <Name> (<TEAMID>)" \
   PANGEA_NOTARY_PROFILE="pangea-notary" \
   desktop/scripts/notarise-mac.sh
   ```

   It refuses to start without both. It signs inside out (any code in the resources, then Faust and Ninja in `Contents/MacOS`, then the app with `desktop/src-tauri/Entitlements.plist`), each with the hardened runtime and a secure timestamp; checks the signatures; sends a zip to Apple and waits, usually a few minutes, printing Apple's log if it is refused; staples the ticket; and checks that Gatekeeper says `source=Notarized Developer ID`.

3. Deploy as usual (`infra/azure/03-deploy-app.sh`).

The entitlements file is empty on purpose: the app starts the tools it carries and Apple's compiler as processes of their own, loads no plugin, and runs its page in WebKit's own processes, so none of the hardened runtime's exceptions is needed. The file says which need maps to which exception. A Developer ID signature also stops the keychain asking again after each update.
